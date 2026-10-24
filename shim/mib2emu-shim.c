//**********************************************************
// Project: RCD330-Plus Desay Emulator (Chroot)
//**********************************************************
//
// LD_PRELOAD library that redirects the hardware accesses of the G5/MIB2 firmware
//
//   /dev/mcu_spi   -> AF_UNIX SOCK_SEQPACKET to $MIB2_MCU_SOCK (MCU simulator)
//                     MIB2_MCU_SOCK=tcp:<ip>:<port> -> packets with 16 bit length prefix via TCP (VM)
//                     Without simulator -> socketpair -> a "silent" MCU
//   /dev/uinput    -> /dev/null; all ioctls return 0 (qemu-user does not translate uinput ioctls)
//   /dev/mtdN      -> $MIB2_FAKEROOT/dev/mtdN (image file), MEMGETINFO/MEMERASE emulated
//   otherwise      -> if $MIB2_FAKEROOT<path> exists, this file is opened
//   libusb_*       -> (libusb_init fails -> file-explorerservice crashes) -> A virtual stick is plugged in as long as
//                     $MIB2_USBSTICK_FLAG (default /tmp/run/emu/usbstick) exists.
//
//
//**********************************************************

#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <mtd/mtd-user.h>
#include <pthread.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Preprocessor directive
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

#define MAX_FD 4096

#define MTD_ERASESIZE 0x2000            // g_blockSize in persistservice

//--------------------------GL constants (no GL headers in the shim)--------------------------
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_SCISSOR_TEST 0x0C11
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_STENCIL_WRITEMASK 0x0B98
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_LINEAR 0x2601

//--------------------------libusb constants--------------------------
#define LIBUSB_ERROR_IO (-1)
#define LIBUSB_ERROR_NO_DEVICE (-4)
#define LIBUSB_ERROR_NOT_FOUND (-5)
#define LIBUSB_ERROR_PIPE (-9)
#define LIBUSB_HOTPLUG_ENUMERATE 1
#define LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED 1
#define LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT 2
#define USBSTICK_FLAG_DEFAULT "/tmp/run/emu/usbstick"

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Data structures -> libusb descriptors
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

enum { FD_NONE, FD_UINPUT, FD_MTD };

struct usbDeviceDescriptor
{
	uint8_t bLength, bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
	uint16_t idVendor, idProduct, bcdDevice;
	uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
};

struct usbEndpointDescriptor
{
	uint8_t bLength, bDescriptorType, bEndpointAddress, bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval, bRefresh, bSynchAddress;
	const unsigned char *extra;
	int extra_length;
};

struct usbInterfaceDescriptor
{
	uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints,
	        bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
	const struct usbEndpointDescriptor *endpoint;
	const unsigned char *extra;
	int extra_length;
};

struct usbInterface
{
	const struct usbInterfaceDescriptor *altsetting;
	int num_altsetting;
};

struct usbConfigDescriptor
{
	uint8_t bLength, bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces, bConfigurationValue, iConfiguration, bmAttributes, MaxPower;
	const struct usbInterface *interface;
	const unsigned char *extra;
	int extra_length;
};

typedef int (*usbHotplugCallback)(void *context, void *device, int event, void *userData);

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

//--------------------------file descriptors--------------------------
static unsigned char fdKind[MAX_FD];

static int (*realOpen)(const char *, int, ...);
static int (*realOpenat)(int, const char *, int, ...);
static FILE *(*realFopen)(const char *, const char *);
static int (*realIoctl)(int, unsigned long, ...);
static int (*realClose)(int);

//--------------------------virtual USB stick--------------------------
static const struct usbDeviceDescriptor stickDevice =
{
	18, 1, 0x0200, 0, 0, 0, 64, 0x0781, 0x5567, 0x0100, 1, 2, 3, 1
};

static const struct usbEndpointDescriptor stickEndpoints[2] =
{
	{ 7, 5, 0x81, 2, 512, 0, 0, 0, NULL, 0 },
	{ 7, 5, 0x02, 2, 512, 0, 0, 0, NULL, 0 },
};

static const struct usbInterfaceDescriptor stickInterfaceDescriptor = { 9, 4, 0, 0, 2, 8, 6, 0x50, 0, stickEndpoints, NULL, 0 };
static const struct usbInterface stickInterface = { &stickInterfaceDescriptor, 1 };
static const struct usbConfigDescriptor stickConfig = { 9, 2, 32, 1, 1, 0, 0x80, 50, &stickInterface, NULL, 0 };
static const char *const stickStrings[] = { NULL, "SanDisk", "Cruzer Blade", "MIB2EMU00000001" };

static int usbContextDummy;
static int usbStickDevice;
static int usbStickHandle;

static struct
{
	usbHotplugCallback callback;
	void *context;
	void *userData;
	int events;
} usbCallbacks[8];

static int usbCallbackCount;
static int usbStickSeen = -1;
static pthread_mutex_t usbMutex = PTHREAD_MUTEX_INITIALIZER;

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Function prototypes
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static void initReal(void);
static const char *getFakeroot(void);
static void setFdKind(int fd, int kind);

static int readFull(int fd, void *buffer, size_t size);
static void *mcuTcpBridge(void *argument);
static int openMcuTcp(const char *spec);
static int openMcu(void);
static int isAbsolutePath(const char *path);
static int redirectPath(const char *path, int flags, mode_t mode);
static int handleMtdIoctl(int fd, unsigned long request, void *argument);

static void discardDepthStencil(void);
static void fixTextureCompleteness(unsigned target);

static int isUsbStickPresent(void);
static int isUsbStick(const void *device);

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

__attribute__((constructor)) static void setLineBufferedLogs(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);
}

static void initReal(void)
{
	if (realOpen)
	{
		return;
	}

	realOpen = dlsym(RTLD_NEXT, "open");
	realOpenat = dlsym(RTLD_NEXT, "openat");
	realFopen = dlsym(RTLD_NEXT, "fopen");
	realIoctl = dlsym(RTLD_NEXT, "ioctl");
	realClose = dlsym(RTLD_NEXT, "close");
}

static const char *getFakeroot(void)
{
	const char *root = getenv("MIB2_FAKEROOT");

	return root ? root : "/opt/mib2/fakeroot";
}

static void setFdKind(int fd, int kind)
{
	if (fd >= 0 && fd < MAX_FD)
	{
		fdKind[fd] = kind;
	}
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// MCU
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

// MCU via TCP (VM with original root)
static int readFull(int fd, void *buffer, size_t size)
{
	size_t received = 0;

	while (received < size)
	{
		ssize_t result = recv(fd, (char *)buffer + received, size - received, 0);

		if (result <= 0)
		{
			return -1;
		}

		received += (size_t)result;
	}

	return 0;
}

static void *mcuTcpBridge(void *argument)
{
	int *fds = argument;
	int appSocket = fds[0];
	int tcpSocket = fds[1];
	unsigned char buffer[4096 + 2];
	struct pollfd pollEntries[2] = { { appSocket, POLLIN, 0 }, { tcpSocket, POLLIN, 0 } };

	while (1)
	{
		if (poll(pollEntries, 2, -1) < 0)
		{
			continue;
		}

		if (pollEntries[0].revents & (POLLIN | POLLHUP))
		{
			ssize_t length = recv(appSocket, buffer + 2, sizeof(buffer) - 2, 0);

			if (length <= 0)
			{
				break;
			}

			buffer[0] = (unsigned char)(length >> 8);
			buffer[1] = (unsigned char)length;

			if (send(tcpSocket, buffer, (size_t)length + 2, MSG_NOSIGNAL) != length + 2)
			{
				break;
			}
		}

		if (pollEntries[1].revents & (POLLIN | POLLHUP))
		{
			unsigned char header[2];
			size_t length;

			if (readFull(tcpSocket, header, 2) != 0)
			{
				break;
			}

			length = ((size_t)header[0] << 8) | header[1];

			if (length > sizeof(buffer) || readFull(tcpSocket, buffer, length) != 0)
			{
				break;
			}

			send(appSocket, buffer, length, MSG_NOSIGNAL);
		}
	}

	return NULL;
}

static int openMcuTcp(const char *spec)
{
	static int fds[2];
	char host[64];
	int port = 0;
	int enable = 1;
	int socketPair[2];
	struct sockaddr_in address = { .sin_family = AF_INET };
	pthread_t thread;

	if (sscanf(spec, "tcp:%63[^:]:%d", host, &port) != 2 || inet_pton(AF_INET, host, &address.sin_addr) != 1)
	{
		return -1;
	}

	address.sin_port = htons(port);

	int tcpSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (tcpSocket < 0 || connect(tcpSocket, (struct sockaddr *)&address, sizeof(address)) != 0 ||
	    socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socketPair) != 0)
	{
		if (tcpSocket >= 0)
		{
			realClose(tcpSocket);
		}
		return -1;
	}

	setsockopt(tcpSocket, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));

	fds[0] = socketPair[1];
	fds[1] = tcpSocket;

	pthread_create(&thread, NULL, mcuTcpBridge, fds);
	pthread_detach(thread);

	return socketPair[0];
}

static int openMcu(void)
{
	const char *path = getenv("MIB2_MCU_SOCK");
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int fd;

	if (path && strncmp(path, "tcp:", 4) == 0)
	{
		fd = openMcuTcp(path);

		if (fd >= 0)
		{
			return fd;
		}
	}

	fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);

	if (!path)
	{
		path = "/tmp/run/emu/mcu.sock";
	}

	strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);

	if (fd >= 0 && connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0)
	{
		return fd;
	}

	if (fd >= 0)
	{
		realClose(fd);
	}

	int socketPair[2];

	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, socketPair) != 0)
	{
		return -1;
	}

	return socketPair[0];
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// File hooks -> open, openat, fopen, ioctl, close
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static int isAbsolutePath(const char *path)
{
	return path && path[0] == '/';
}

// returns -2 if the path is not redirected
static int redirectPath(const char *path, int flags, mode_t mode)
{
	char alternativePath[512];
	int fd;

	if (!path)
	{
		return -2;
	}

	if (strcmp(path, "/dev/mcu_spi") == 0)
	{
		return openMcu();
	}

	if (strcmp(path, "/dev/uinput") == 0 || strcmp(path, "/dev/input/uinput") == 0)
	{
		fd = realOpen("/dev/null", O_RDWR);
		setFdKind(fd, FD_UINPUT);
		return fd;
	}

	snprintf(alternativePath, sizeof(alternativePath), "%s%s", getFakeroot(), path);

	if (access(alternativePath, F_OK) != 0)
	{
		return -2;
	}

	fd = realOpen(alternativePath, flags & ~O_CREAT, mode);

	if (strncmp(path, "/dev/mtd", 8) == 0)
	{
		setFdKind(fd, FD_MTD);
	}

	return fd;
}

int open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	int fd;

	initReal();

	if (flags & (O_CREAT | O_TMPFILE))
	{
		va_list arguments;
		va_start(arguments, flags);
		mode = va_arg(arguments, int);
		va_end(arguments);
	}

	fd = redirectPath(path, flags, mode);

	if (fd != -2)
	{
		return fd;
	}

	fd = realOpen(path, flags, mode);
	setFdKind(fd, FD_NONE);

	return fd;
}

int open64(const char *path, int flags, ...) __attribute__((alias("open")));

int openat(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	int fd;

	initReal();

	if (flags & (O_CREAT | O_TMPFILE))
	{
		va_list arguments;
		va_start(arguments, flags);
		mode = va_arg(arguments, int);
		va_end(arguments);
	}

	if (isAbsolutePath(path))
	{
		fd = redirectPath(path, flags, mode);

		if (fd != -2)
		{
			return fd;
		}
	}

	fd = realOpenat(dirfd, path, flags, mode);
	setFdKind(fd, FD_NONE);

	return fd;
}

int openat64(int dirfd, const char *path, int flags, ...) __attribute__((alias("openat")));

FILE *fopen(const char *path, const char *mode)
{
	char alternativePath[512];

	initReal();

	if (isAbsolutePath(path))
	{
		snprintf(alternativePath, sizeof(alternativePath), "%s%s", getFakeroot(), path);

		if (access(alternativePath, F_OK) == 0)
		{
			return realFopen(alternativePath, mode);
		}
	}

	return realFopen(path, mode);
}

FILE *fopen64(const char *path, const char *mode) __attribute__((alias("fopen")));

static int handleMtdIoctl(int fd, unsigned long request, void *argument)
{
	struct stat fileState;

	switch (request)
	{
	case MEMGETINFO:
	{
		struct mtd_info_user *info = argument;

		fstat(fd, &fileState);
		memset(info, 0, sizeof(*info));
		info->type = MTD_NORFLASH;
		info->flags = MTD_CAP_NORFLASH;
		info->size = fileState.st_size;
		info->erasesize = MTD_ERASESIZE;
		info->writesize = 1;
		return 0;
	}

	case MEMERASE:
	{
		struct erase_info_user *erase = argument;
		static unsigned char erasedBlock[MTD_ERASESIZE];
		uint32_t done;

		memset(erasedBlock, 0xff, sizeof(erasedBlock));

		for (done = 0; done < erase->length; done += sizeof(erasedBlock))
		{
			uint32_t size = erase->length - done < sizeof(erasedBlock) ? erase->length - done : sizeof(erasedBlock);

			if (pwrite(fd, erasedBlock, size, erase->start + done) != (ssize_t)size)
			{
				return -1;
			}
		}
		return 0;
	}

	default:
		return 0;
	}
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list arguments;
	void *argument;

	initReal();

	va_start(arguments, request);
	argument = va_arg(arguments, void *);
	va_end(arguments);

	if (fd >= 0 && fd < MAX_FD)
	{
		if (fdKind[fd] == FD_UINPUT)
		{
			return 0;
		}

		if (fdKind[fd] == FD_MTD)
		{
			return handleMtdIoctl(fd, request, argument);
		}
	}

	return realIoctl(fd, request, argument);
}

int close(int fd)
{
	initReal();
	setFdKind(fd, FD_NONE);

	return realClose(fd);
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// GL corrections (only active without GL forwarding)
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static void discardDepthStencil(void)
{
	static int initialized;
	static void (*clear)(unsigned);
	static void (*depthMask)(unsigned char);
	static void (*stencilMask)(unsigned);
	static unsigned char (*isEnabled)(unsigned);
	static void (*enable)(unsigned);
	static void (*disable)(unsigned);
	static void (*getIntegerv)(unsigned, int *);
	static void (*getBooleanv)(unsigned, unsigned char *);
	unsigned char depthWriteMask;
	unsigned char scissorEnabled;
	int stencilWriteMask;

	if (!initialized)
	{
		initialized = 1;
		clear = dlsym(RTLD_DEFAULT, "glClear");
		depthMask = dlsym(RTLD_DEFAULT, "glDepthMask");
		stencilMask = dlsym(RTLD_DEFAULT, "glStencilMask");
		isEnabled = dlsym(RTLD_DEFAULT, "glIsEnabled");
		enable = dlsym(RTLD_NEXT, "glEnable");
		disable = dlsym(RTLD_DEFAULT, "glDisable");
		getIntegerv = dlsym(RTLD_DEFAULT, "glGetIntegerv");
		getBooleanv = dlsym(RTLD_DEFAULT, "glGetBooleanv");
	}

	if (!clear || !getBooleanv)
	{
		return;
	}

	// save the state, clear depth + stencil completely, restore the state
	getBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask);
	getIntegerv(GL_STENCIL_WRITEMASK, &stencilWriteMask);
	scissorEnabled = isEnabled(GL_SCISSOR_TEST);

	depthMask(1);
	stencilMask(~0u);

	if (scissorEnabled)
	{
		disable(GL_SCISSOR_TEST);
	}

	clear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	if (scissorEnabled)
	{
		enable(GL_SCISSOR_TEST);
	}

	depthMask(depthWriteMask);
	stencilMask((unsigned)stencilWriteMask);
}

static void fixTextureCompleteness(unsigned target)
{
	static int initialized;
	static void (*getTexParameteriv)(unsigned, unsigned, int *);
	static void (*setTexParameteri)(unsigned, unsigned, int);
	int minFilter = 0;

	if (!initialized)
	{
		initialized = 1;
		getTexParameteriv = dlsym(RTLD_DEFAULT, "glGetTexParameteriv");
		setTexParameteri = dlsym(RTLD_NEXT, "glTexParameteri");
	}

	if (target != GL_TEXTURE_2D || !getTexParameteriv || !setTexParameteri)
	{
		return;
	}

	getTexParameteriv(target, GL_TEXTURE_MIN_FILTER, &minFilter);

	if (minFilter >= 0x2700 && minFilter <= 0x2703)
	{
		setTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
}

unsigned int eglSwapBuffers(void *dpy, void *surface)
{
	static unsigned int (*realSwap)(void *, void *);

	if (!realSwap)
	{
		realSwap = dlsym(RTLD_NEXT, "eglSwapBuffers");
	}

	unsigned result = realSwap(dpy, surface);

	discardDepthStencil();

	return result;
}

void glTexImage2D(unsigned target, int level, int internalFormat, int width, int height, int border,
                  unsigned format, unsigned type, const void *pixels)
{
	static void (*real)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);

	if (!real)
	{
		real = dlsym(RTLD_NEXT, "glTexImage2D");
	}

	real(target, level, internalFormat, width, height, border, format, type, pixels);

	if (level == 0)
	{
		fixTextureCompleteness(target);
	}
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// libusb -> USB bus with an optional virtual stick
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static int isUsbStickPresent(void)
{
	const char *flag = getenv("MIB2_USBSTICK_FLAG");

	return access(flag ? flag : USBSTICK_FLAG_DEFAULT, F_OK) == 0;
}

static int isUsbStick(const void *device)
{
	return device == &usbStickDevice || device == &usbStickHandle;
}

int libusb_init(void **context)
{
	if (context)
	{
		*context = &usbContextDummy;
	}

	return 0;
}

void libusb_exit(void *context)
{
	(void)context;
}

int libusb_has_capability(uint32_t capability)
{
	(void)capability;

	return 1;
}

void libusb_set_debug(void *context, int level)
{
	(void)context;
	(void)level;
}

int libusb_hotplug_register_callback(void *context, int events, int flags, int vendorId, int productId,
                                     int deviceClass, void *callback, void *userData, int *handle)
{
	(void)vendorId;
	(void)productId;
	(void)deviceClass;

	pthread_mutex_lock(&usbMutex);

	int callbackHandle = usbCallbackCount;

	if (usbCallbackCount < (int)(sizeof(usbCallbacks) / sizeof(usbCallbacks[0])))
	{
		usbCallbacks[usbCallbackCount].callback = (usbHotplugCallback)callback;
		usbCallbacks[usbCallbackCount].context = context;
		usbCallbacks[usbCallbackCount].userData = userData;
		usbCallbacks[usbCallbackCount].events = events;
		usbCallbackCount++;
	}

	pthread_mutex_unlock(&usbMutex);

	if (handle)
	{
		*handle = callbackHandle + 1;
	}

	if ((flags & LIBUSB_HOTPLUG_ENUMERATE) && isUsbStickPresent() && (events & LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED))
	{
		((usbHotplugCallback)callback)(context, &usbStickDevice, LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, userData);
	}

	return 0;
}

void libusb_hotplug_deregister_callback(void *context, int handle)
{
	(void)context;

	pthread_mutex_lock(&usbMutex);

	if (handle >= 1 && handle <= usbCallbackCount)
	{
		usbCallbacks[handle - 1].callback = NULL;
	}

	pthread_mutex_unlock(&usbMutex);
}

// Event loop -> check the stick flag and call the hotplug callbacks
int libusb_handle_events(void *context)
{
	(void)context;

	sleep(1);

	int present = isUsbStickPresent();

	pthread_mutex_lock(&usbMutex);
	int previous = usbStickSeen;
	usbStickSeen = present;
	int callbackCount = usbCallbackCount;
	pthread_mutex_unlock(&usbMutex);

	if (previous == -1 || previous == present)
	{
		return 0;
	}

	int event = present ? LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED : LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT;

	for (int counter = 0; counter < callbackCount; counter++)
	{
		if (usbCallbacks[counter].callback && (usbCallbacks[counter].events & event))
		{
			usbCallbacks[counter].callback(usbCallbacks[counter].context, &usbStickDevice, event, usbCallbacks[counter].userData);
		}
	}

	return 0;
}

int libusb_handle_events_completed(void *context, int *completed)
{
	(void)completed;

	return libusb_handle_events(context);
}

ssize_t libusb_get_device_list(void *context, void ***list)
{
	(void)context;

	int count = isUsbStickPresent();

	*list = calloc(2, sizeof(void *));

	if (count)
	{
		(*list)[0] = &usbStickDevice;
	}

	return count;
}

void libusb_free_device_list(void **list, int unrefDevices)
{
	(void)unrefDevices;

	free(list);
}

void *libusb_ref_device(void *device)
{
	return device;
}

void libusb_unref_device(void *device)
{
	(void)device;
}

int libusb_get_device_descriptor(void *device, struct usbDeviceDescriptor *descriptor)
{
	if (!isUsbStick(device))
	{
		return LIBUSB_ERROR_NO_DEVICE;
	}

	*descriptor = stickDevice;

	return 0;
}

int libusb_get_config_descriptor(void *device, uint8_t index, const struct usbConfigDescriptor **config)
{
	if (!isUsbStick(device) || index != 0)
	{
		return LIBUSB_ERROR_NOT_FOUND;
	}

	*config = &stickConfig;

	return 0;
}

int libusb_get_active_config_descriptor(void *device, const struct usbConfigDescriptor **config)
{
	return libusb_get_config_descriptor(device, 0, config);
}

void libusb_free_config_descriptor(const struct usbConfigDescriptor *config)
{
	(void)config;
}

int libusb_open(void *device, void **handle)
{
	if (!isUsbStick(device) || !isUsbStickPresent())
	{
		return LIBUSB_ERROR_NO_DEVICE;
	}

	*handle = &usbStickHandle;

	return 0;
}

void *libusb_open_device_with_vid_pid(void *context, uint16_t vendorId, uint16_t productId)
{
	(void)context;

	if (isUsbStickPresent() && vendorId == stickDevice.idVendor && productId == stickDevice.idProduct)
	{
		return &usbStickHandle;
	}

	return NULL;
}

void libusb_close(void *handle)
{
	(void)handle;
}

void *libusb_get_device(void *handle)
{
	return isUsbStick(handle) ? (void *)&usbStickDevice : NULL;
}

int libusb_get_string_descriptor_ascii(void *handle, uint8_t index, unsigned char *data, int length)
{
	if (!isUsbStick(handle) || index == 0 || index >= sizeof(stickStrings) / sizeof(stickStrings[0]) || length <= 0)
	{
		return LIBUSB_ERROR_PIPE;
	}

	snprintf((char *)data, (size_t)length, "%s", stickStrings[index]);

	return (int)strlen((char *)data);
}

int libusb_control_transfer(void *handle, uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                            unsigned char *data, uint16_t length, unsigned int timeout)
{
	(void)handle;
	(void)requestType;
	(void)request;
	(void)value;
	(void)index;
	(void)data;
	(void)length;
	(void)timeout;

	return LIBUSB_ERROR_PIPE;
}

int libusb_bulk_transfer(void *handle, unsigned char endpoint, unsigned char *data, int length, int *transferred,
                         unsigned int timeout)
{
	(void)handle;
	(void)endpoint;
	(void)data;
	(void)length;
	(void)timeout;

	if (transferred)
	{
		*transferred = 0;
	}

	return LIBUSB_ERROR_IO;
}

int libusb_claim_interface(void *handle, int interfaceNumber)
{
	(void)handle;
	(void)interfaceNumber;

	return 0;
}

int libusb_release_interface(void *handle, int interfaceNumber)
{
	(void)handle;
	(void)interfaceNumber;

	return 0;
}

int libusb_set_configuration(void *handle, int configuration)
{
	(void)handle;
	(void)configuration;

	return 0;
}

int libusb_reset_device(void *handle)
{
	(void)handle;

	return 0;
}

int libusb_set_auto_detach_kernel_driver(void *handle, int enable)
{
	(void)handle;
	(void)enable;

	return 0;
}

const char *libusb_error_name(int errorCode)
{
	switch (errorCode)
	{
	case LIBUSB_ERROR_IO:
		return "LIBUSB_ERROR_IO";

	case LIBUSB_ERROR_NO_DEVICE:
		return "LIBUSB_ERROR_NO_DEVICE";

	case LIBUSB_ERROR_NOT_FOUND:
		return "LIBUSB_ERROR_NOT_FOUND";

	case LIBUSB_ERROR_PIPE:
		return "LIBUSB_ERROR_PIPE";

	default:
		return "LIBUSB_ERROR_OTHER";
	}
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
