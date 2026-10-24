//**********************************************************
// Project: RCD330-Plus Desay Emulator (Chroot)
//**********************************************************
// glfwd-client.c -> LD_PRELOAD library for ARM apps, replaces EGL and GLES2
//
//   GL calls are serialized and sent to glfwd-server, which executes them with
//   the real GL. On eglSwapBuffers the finished image comes back and is attached as wl_shm buffer
//   to the Wayland surface of the app (device Weston, ivi layers and input stay unchanged).
//
//   Active if MIB2_GLFWD=1, the server is reachable and the process is not listed in MIB2_GLFWD_DENY.
//   Otherwise every call is passed through to the real implementation (Mesa, via the shim).
//
//   MIB2_GLFWD_SOCK = <unix socket path> (default /tmp/run/emu/glfwd.sock)
//
//**********************************************************

#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wayland-client.h>
#include "glfwd-proto.h"
#include "glfwd-gen-ops.h"

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Preprocessor directive
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

#define GL_HALF_FLOAT_OES 0x8D61
#define LOG(...) fprintf(stderr, "[glfwd] " __VA_ARGS__)

#define MAX_ATTRIBS 32
#define MAX_SHADOW_BUFFERS 65536
#define BUFFER_COUNT 3

// Without forwarding -> call the real GL function
#define FALLBACK(name, ret, args, call) \
	if (!glfwdOn) { static ret (*real) args; if (!real) real = glfwdReal(#name); return real call; }

// Without forwarding -> call the real EGL function (the first EGL call connects to the server)
#define EGLFALLBACK(name, ret, args, call) \
	if (!glfwdTry()) { static ret (*real) args; if (!real) real = glfwdReal(#name); return real call; }

#define OUR_DISPLAY ((EGLDisplay)&eglDummyDisplay)
#define OUR_CONFIG ((EGLConfig)&ourConfig)

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Data structures
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

struct clientContext
{
	uint32_t id;
};

// Layout from wayland-egl-backend.h
struct wl_egl_window;
struct wl_egl_window_v3
{
	const intptr_t version;
	int width, height, dx, dy, attached_width, attached_height;
	void *driver_private;
	void (*resize_callback)(struct wl_egl_window *, void *);
	void (*destroy_window_callback)(void *);
	struct wl_surface *surface;
};

struct clientSurface
{
	uint32_t id;                        // surface ID on the server
	struct wl_egl_window_v3 *eglWindow; // NULL for pbuffer
	int width;                          // size of the buffers
	int height;
	int swapInterval;
	struct wl_surface *waylandSurface;  // wrapper on our event queue
	struct wl_callback *frameCallback;

	struct
	{
		struct wl_buffer *waylandBuffer;
		void *memory;
		int busy;
	} buffers[BUFFER_COUNT];

	int lastBufferIndex;
	void *poolMemory;
	size_t poolSize;
};

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

//--------------------------activation and transport--------------------------
int glfwdOn;
static int serverSocket = -1;

static pthread_mutex_t glfwdMutex;
static unsigned char *outputBuffer;
static size_t outputLength;
static size_t outputCapacity;
static size_t messageStart;
uint32_t glfwdBlobLength;

//--------------------------context management (current context per thread)--------------------------
static __thread struct clientContext *currentContext;
static __thread struct clientSurface *currentDrawSurface;
static __thread EGLint eglErrorCode = EGL_SUCCESS;
static uint32_t serverContextId;
static uint32_t serverSurfaceId;

//--------------------------client state for the special cases--------------------------
static struct
{
	int enabled;
	int isClient;
	GLint size;
	GLenum type;
	GLboolean normalized;
	GLsizei stride;
	const void *pointer;
} attribState[MAX_ATTRIBS];

static GLuint arrayBuffer;
static GLuint elementBuffer;
static int unpackAlignment = 4;
static int packAlignment = 4;

static struct
{
	unsigned char *data;
	uint32_t size;
} indexShadow[MAX_SHADOW_BUFFERS];

//--------------------------Wayland--------------------------
static struct wl_display *waylandDisplay;
static struct wl_event_queue *eventQueue;
static struct wl_shm *waylandShm;

//--------------------------EGL--------------------------
static int eglDummyDisplay;
static int ourConfig;
static EGLenum boundApi = EGL_OPENGL_ES_API;

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Function prototypes
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static void *glfwdReal(const char *name);
static int connectServer(void);
static int isDenied(void);

static void ensureCapacity(size_t size);
static void writeAll(const void *data, size_t size);
static void readAll(void *data, size_t size);
static void flushOutput(void);
static void glfwdPutU32(uint32_t value);
static void glfwdPutF32(float value);
static void glfwdPutBlob(const void *data, uint32_t length);
static void glfwdBegin(uint32_t opcode);
static void glfwdEnd(void);
static void glfwdSync(void);
static uint32_t glfwdGetU32(void);
static void glfwdGetInto(void *data, uint32_t length);
static void glfwdLock(void);
static void glfwdUnlock(void);
static uint32_t getSurfaceId(struct clientSurface *surface);

static uint32_t getTypeSize(GLenum type);
static void sendClientAttribs(uint32_t lowIndex, uint32_t highIndex);
static int hasClientAttrib(void);
static uint32_t getImageSize(GLsizei width, GLsizei height, GLenum format, GLenum type, int alignment);
static uint32_t getValues(uint32_t kind, GLenum pname, void *output);

static int setupWayland(void);
static void dispatchQueue(int timeoutMs);
static void freeBuffers(struct clientSurface *surface);
static int allocBuffers(struct clientSurface *surface, int width, int height);

static EGLint getConfigAttrib(EGLint attribute);
static struct clientSurface *createSurface(int width, int height);
static int glfwdTry(void);

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// additional - functions -> activation, real functions, connection to the server
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static void *glfwdReal(const char *name)
{
	void *function = dlsym(RTLD_NEXT, name);

	if (!function)
	{
		LOG("Error: real function %s not found\n", name);
	}

	return function;
}

static int connectServer(void)
{
	const char *path = getenv("MIB2_GLFWD_SOCK");
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	strncpy(address.sun_path, path ? path : GLFWD_SOCK_DEFAULT, sizeof(address.sun_path) - 1);

	if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
	{
		if (fd >= 0)
		{
			close(fd);
		}
		return -1;
	}

	return fd;
}

static int isDenied(void)
{
	const char *denyList = getenv("MIB2_GLFWD_DENY");
	char list[512];
	char *token;
	char *savePointer;

	if (!denyList)
	{
		denyList = "weston,weston-keyboard,cameraservice";
	}

	snprintf(list, sizeof(list), "%s", denyList);

	for (token = strtok_r(list, ",", &savePointer); token; token = strtok_r(NULL, ",", &savePointer))
	{
		if (strcmp(token, program_invocation_short_name) == 0)
		{
			return 1;
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// additional - functions -> transport (output buffer, read/write, message frame)
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static void ensureCapacity(size_t size)
{
	if (outputLength + size <= outputCapacity)
	{
		return;
	}

	while (outputLength + size > outputCapacity)
	{
		outputCapacity = outputCapacity ? outputCapacity * 2 : (1 << 20);
	}

	outputBuffer = realloc(outputBuffer, outputCapacity);
}

static void writeAll(const void *data, size_t size)
{
	const unsigned char *position = data;

	while (size)
	{
		ssize_t written = write(serverSocket, position, size);

		if (written < 0 && errno == EINTR)
		{
			continue;
		}

		if (written <= 0)
		{
			LOG("Error: lost connection to the server (%s) -> exit\n", strerror(errno));
			_exit(1);
		}

		position += written;
		size -= (size_t)written;
	}
}

static void readAll(void *data, size_t size)
{
	unsigned char *position = data;

	while (size)
	{
		ssize_t result = read(serverSocket, position, size);

		if (result < 0 && errno == EINTR)
		{
			continue;
		}

		if (result <= 0)
		{
			LOG("Error: lost connection to the server while reading -> exit\n");
			_exit(1);
		}

		position += result;
		size -= (size_t)result;
	}
}

static void flushOutput(void)
{
	if (outputLength)
	{
		writeAll(outputBuffer, outputLength);
	}

	outputLength = 0;
}

static void glfwdPutU32(uint32_t value)
{
	ensureCapacity(4);
	memcpy(outputBuffer + outputLength, &value, 4);
	outputLength += 4;
}

static void glfwdPutF32(float value)
{
	ensureCapacity(4);
	memcpy(outputBuffer + outputLength, &value, 4);
	outputLength += 4;
}

static void glfwdPutBlob(const void *data, uint32_t length)
{
	if (!data)
	{
		glfwdPutU32(GLFWD_NULL);
		return;
	}

	glfwdPutU32(length);
	ensureCapacity((length + 3) & ~3u);
	memcpy(outputBuffer + outputLength, data, length);
	memset(outputBuffer + outputLength + length, 0, ((length + 3) & ~3u) - length);
	outputLength += (length + 3) & ~3u;
}

static void glfwdBegin(uint32_t opcode)
{
	messageStart = outputLength;
	glfwdPutU32(opcode);
	glfwdPutU32(0);
}

static void glfwdEnd(void)
{
	uint32_t length = (uint32_t)(outputLength - messageStart - 8);

	memcpy(outputBuffer + messageStart + 4, &length, 4);

	if (outputLength > (1u << 20))
	{
		flushOutput();
	}
}

static void glfwdSync(void)
{
	flushOutput();
}

static uint32_t glfwdGetU32(void)
{
	uint32_t value;

	readAll(&value, 4);

	return value;
}

static void glfwdGetInto(void *data, uint32_t length)
{
	unsigned char temp[4096];

	if (data)
	{
		readAll(data, length);
		return;
	}

	while (length)
	{
		uint32_t chunk = length < sizeof(temp) ? length : sizeof(temp);

		readAll(temp, chunk);
		length -= chunk;
	}
}

static void glfwdLock(void)
{
	pthread_mutex_lock(&glfwdMutex);

	uint32_t contextId = currentContext ? currentContext->id : 0;
	uint32_t surfaceId = getSurfaceId(currentDrawSurface);

	if (contextId != serverContextId || surfaceId != serverSurfaceId)
	{
		glfwdBegin(GLFWD_OP_MAKE_CURRENT);
		glfwdPutU32(contextId);
		glfwdPutU32(surfaceId);
		glfwdEnd();

		serverContextId = contextId;
		serverSurfaceId = surfaceId;
	}
}

static void glfwdUnlock(void)
{
	pthread_mutex_unlock(&glfwdMutex);
}

static uint32_t getSurfaceId(struct clientSurface *surface)
{
	return surface ? surface->id : 0;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// generated GL wrappers (glfwd-gen.py)
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

#include "glfwd-gen-client.inc"

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// GL special cases -> buffers, client vertex arrays, draw calls, pixel store
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static uint32_t getTypeSize(GLenum type)
{
	switch (type)
	{
	case GL_BYTE:
	case GL_UNSIGNED_BYTE:
		return 1;

	case GL_SHORT:
	case GL_UNSIGNED_SHORT:
	case GL_HALF_FLOAT_OES:
		return 2;

	default:
		return 4;
	}
}

GL_APICALL void GL_APIENTRY glBindBuffer(GLenum target, GLuint buffer)
{
	FALLBACK(glBindBuffer, void, (GLenum, GLuint), (target, buffer));

	pthread_mutex_lock(&glfwdMutex);

	if (target == GL_ARRAY_BUFFER)
	{
		arrayBuffer = buffer;
	}
	else if (target == GL_ELEMENT_ARRAY_BUFFER)
	{
		elementBuffer = buffer;
	}

	glfwdSend_glBindBuffer(target, buffer);
	pthread_mutex_unlock(&glfwdMutex);
}

GL_APICALL void GL_APIENTRY glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
	FALLBACK(glBufferData, void, (GLenum, GLsizeiptr, const void *, GLenum), (target, size, data, usage));

	pthread_mutex_lock(&glfwdMutex);

	// keep a copy of index buffers
	if (target == GL_ELEMENT_ARRAY_BUFFER && elementBuffer && elementBuffer < MAX_SHADOW_BUFFERS)
	{
		free(indexShadow[elementBuffer].data);
		indexShadow[elementBuffer].data = calloc(1, size > 0 ? (size_t)size : 1);
		indexShadow[elementBuffer].size = (uint32_t)size;

		if (data)
		{
			memcpy(indexShadow[elementBuffer].data, data, (size_t)size);
		}
	}

	glfwdSend_glBufferData(target, size, data, usage);
	pthread_mutex_unlock(&glfwdMutex);
}

GL_APICALL void GL_APIENTRY glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)
{
	FALLBACK(glBufferSubData, void, (GLenum, GLintptr, GLsizeiptr, const void *), (target, offset, size, data));

	pthread_mutex_lock(&glfwdMutex);

	if (target == GL_ELEMENT_ARRAY_BUFFER && elementBuffer && elementBuffer < MAX_SHADOW_BUFFERS && data &&
	    (uint32_t)(offset + size) <= indexShadow[elementBuffer].size)
	{
		memcpy(indexShadow[elementBuffer].data + offset, data, (size_t)size);
	}

	glfwdSend_glBufferSubData(target, offset, size, data);
	pthread_mutex_unlock(&glfwdMutex);
}

GL_APICALL void GL_APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
	FALLBACK(glDeleteBuffers, void, (GLsizei, const GLuint *), (n, buffers));

	pthread_mutex_lock(&glfwdMutex);

	for (GLsizei counter = 0; buffers && counter < n; counter++)
	{
		GLuint buffer = buffers[counter];

		if (buffer < MAX_SHADOW_BUFFERS)
		{
			free(indexShadow[buffer].data);
			indexShadow[buffer].data = NULL;
			indexShadow[buffer].size = 0;
		}

		if (buffer == arrayBuffer)
		{
			arrayBuffer = 0;
		}

		if (buffer == elementBuffer)
		{
			elementBuffer = 0;
		}
	}

	glfwdSend_glDeleteBuffers(n, buffers);
	pthread_mutex_unlock(&glfwdMutex);
}

GL_APICALL void GL_APIENTRY glEnableVertexAttribArray(GLuint index)
{
	FALLBACK(glEnableVertexAttribArray, void, (GLuint), (index));

	pthread_mutex_lock(&glfwdMutex);

	if (index < MAX_ATTRIBS)
	{
		attribState[index].enabled = 1;
	}

	glfwdSend_glEnableVertexAttribArray(index);
	pthread_mutex_unlock(&glfwdMutex);
}

GL_APICALL void GL_APIENTRY glDisableVertexAttribArray(GLuint index)
{
	FALLBACK(glDisableVertexAttribArray, void, (GLuint), (index));

	pthread_mutex_lock(&glfwdMutex);

	if (index < MAX_ATTRIBS)
	{
		attribState[index].enabled = 0;
	}

	glfwdSend_glDisableVertexAttribArray(index);
	pthread_mutex_unlock(&glfwdMutex);
}

GL_APICALL void GL_APIENTRY glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                                                  GLsizei stride, const void *pointer)
{
	FALLBACK(glVertexAttribPointer, void, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *),
	         (index, size, type, normalized, stride, pointer));

	pthread_mutex_lock(&glfwdMutex);

	if (index < MAX_ATTRIBS)
	{
		attribState[index].isClient = arrayBuffer == 0;
		attribState[index].size = size;
		attribState[index].type = type;
		attribState[index].normalized = normalized;
		attribState[index].stride = stride;
		attribState[index].pointer = pointer;
	}

	if (arrayBuffer)
	{
		glfwdSend_glVertexAttribPointer(index, size, type, normalized, stride, pointer);
	}

	pthread_mutex_unlock(&glfwdMutex);
}

// Send the client vertex arrays for the index range [lowIndex, highIndex] to the server
static void sendClientAttribs(uint32_t lowIndex, uint32_t highIndex)
{
	for (int counter = 0; counter < MAX_ATTRIBS; counter++)
	{
		if (!attribState[counter].enabled || !attribState[counter].isClient || !attribState[counter].pointer)
		{
			continue;
		}

		uint32_t elementSize = (uint32_t)attribState[counter].size * getTypeSize(attribState[counter].type);
		uint32_t effectiveStride = attribState[counter].stride ? (uint32_t)attribState[counter].stride : elementSize;

		glfwdBegin(GLFWD_OP_CLIENT_ATTRIB);
		glfwdPutU32(counter);
		glfwdPutU32(attribState[counter].size);
		glfwdPutU32(attribState[counter].type);
		glfwdPutU32(attribState[counter].normalized);
		glfwdPutU32(effectiveStride);
		glfwdPutU32(lowIndex);
		glfwdPutBlob((const unsigned char *)attribState[counter].pointer + (size_t)lowIndex * effectiveStride,
		             (highIndex - lowIndex) * effectiveStride + elementSize);
		glfwdEnd();
	}
}

static int hasClientAttrib(void)
{
	for (int counter = 0; counter < MAX_ATTRIBS; counter++)
	{
		if (attribState[counter].enabled && attribState[counter].isClient)
		{
			return 1;
		}
	}

	return 0;
}

GL_APICALL void GL_APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	FALLBACK(glDrawArrays, void, (GLenum, GLint, GLsizei), (mode, first, count));

	if (count <= 0)
	{
		return;
	}

	glfwdLock();

	if (hasClientAttrib())
	{
		sendClientAttribs((uint32_t)first, (uint32_t)(first + count - 1));
	}

	glfwdSend_glDrawArrays(mode, first, count);
	glfwdUnlock();
}

GL_APICALL void GL_APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
	FALLBACK(glDrawElements, void, (GLenum, GLsizei, GLenum, const void *), (mode, count, type, indices));

	if (count <= 0)
	{
		return;
	}

	glfwdLock();

	uint32_t indexSize = getTypeSize(type);
	const unsigned char *indexData = NULL;

	// index data -> from shadow copy of the IBO or directly from client memory
	if (elementBuffer)
	{
		if (elementBuffer < MAX_SHADOW_BUFFERS && indexShadow[elementBuffer].data &&
		    (uintptr_t)indices + (size_t)count * indexSize <= indexShadow[elementBuffer].size)
		{
			indexData = indexShadow[elementBuffer].data + (uintptr_t)indices;
		}
	}
	else
	{
		indexData = indices;
	}

	// client arrays -> find the used index range
	if (hasClientAttrib() && indexData)
	{
		uint32_t lowIndex = 0xFFFFFFFFu;
		uint32_t highIndex = 0;

		for (GLsizei counter = 0; counter < count; counter++)
		{
			uint32_t value = indexSize == 1 ? indexData[counter] : indexSize == 2 ? ((const uint16_t *)indexData)[counter] : ((const uint32_t *)indexData)[counter];

			if (value < lowIndex)
			{
				lowIndex = value;
			}

			if (value > highIndex)
			{
				highIndex = value;
			}
		}

		sendClientAttribs(lowIndex, highIndex);
	}

	if (elementBuffer)
	{
		glfwdSend_glDrawElements(mode, count, type, indices);
	}
	else
	{
		glfwdBegin(GLFWD_OP_DRAW_ELEMENTS_DATA);
		glfwdPutU32(mode);
		glfwdPutU32((uint32_t)count);
		glfwdPutU32(type);
		glfwdPutBlob(indices, (uint32_t)count * indexSize);
		glfwdEnd();
	}

	glfwdUnlock();
}

GL_APICALL void GL_APIENTRY glPixelStorei(GLenum pname, GLint param)
{
	FALLBACK(glPixelStorei, void, (GLenum, GLint), (pname, param));

	pthread_mutex_lock(&glfwdMutex);

	if (pname == GL_UNPACK_ALIGNMENT)
	{
		unpackAlignment = param;
	}
	else if (pname == GL_PACK_ALIGNMENT)
	{
		packAlignment = param;
	}

	glfwdSend_glPixelStorei(pname, param);
	pthread_mutex_unlock(&glfwdMutex);
}

static uint32_t getImageSize(GLsizei width, GLsizei height, GLenum format, GLenum type, int alignment)
{
	uint32_t bytesPerPixel;

	if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1)
	{
		bytesPerPixel = 2;
	}
	else
	{
		// 0x80E1 = BGRA
		uint32_t components = format == GL_RGBA || format == 0x80E1 ? 4 : format == GL_RGB ? 3 : format == GL_LUMINANCE_ALPHA ? 2 : 1;
		bytesPerPixel = components * getTypeSize(type);
	}

	if (width <= 0 || height <= 0)
	{
		return 0;
	}

	uint32_t rowSize = (uint32_t)width * bytesPerPixel;
	uint32_t alignedRowSize = (rowSize + (uint32_t)alignment - 1) & ~((uint32_t)alignment - 1);

	return alignedRowSize * (uint32_t)(height - 1) + rowSize;
}

GL_APICALL void GL_APIENTRY glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
                                         GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
	FALLBACK(glTexImage2D, void, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *),
	         (target, level, internalformat, width, height, border, format, type, pixels));

	pthread_mutex_lock(&glfwdMutex);
	glfwdBlobLength = getImageSize(width, height, format, type, unpackAlignment);
	glfwdSend_glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
	pthread_mutex_unlock(&glfwdMutex);
}

GL_APICALL void GL_APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                            GLsizei height, GLenum format, GLenum type, const void *pixels)
{
	FALLBACK(glTexSubImage2D, void, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *),
	         (target, level, xoffset, yoffset, width, height, format, type, pixels));

	pthread_mutex_lock(&glfwdMutex);
	glfwdBlobLength = getImageSize(width, height, format, type, unpackAlignment);
	glfwdSend_glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
	pthread_mutex_unlock(&glfwdMutex);
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// GL special cases -> flush/finish, errors, strings, glGet*, read pixels, shaders
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

GL_APICALL void GL_APIENTRY glFlush(void)
{
	FALLBACK(glFlush, void, (void), ());

	glfwdLock();
	glfwdSend_glFlush();
	glfwdUnlock();
}

GL_APICALL void GL_APIENTRY glFinish(void)
{
	FALLBACK(glFinish, void, (void), ());

	glfwdLock();
	glfwdBegin(GLFWD_OP_FINISH);
	glfwdEnd();
	glfwdSync();
	glfwdGetU32();
	glfwdUnlock();
}

// glGetError
GL_APICALL GLenum GL_APIENTRY glGetError(void)
{
	FALLBACK(glGetError, GLenum, (void), ());

	return GL_NO_ERROR;
}

GL_APICALL const GLubyte *GL_APIENTRY glGetString(GLenum name)
{
	FALLBACK(glGetString, const GLubyte *, (GLenum), (name));

	static char *cache[8];
	static const GLenum names[8] = { GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS, GL_SHADING_LANGUAGE_VERSION };
	int slot = -1;

	for (int counter = 0; counter < 5; counter++)
	{
		if (names[counter] == name)
		{
			slot = counter;
		}
	}

	if (slot < 0)
	{
		return NULL;
	}

	if (!cache[slot])
	{
		glfwdLock();
		glfwdBegin(GLFWD_OP_GET_STRING);
		glfwdPutU32(name);
		glfwdEnd();
		glfwdSync();

		uint32_t length = glfwdGetU32();
		cache[slot] = calloc(1, length + 1);
		glfwdGetInto(cache[slot], length);

		glfwdUnlock();
	}

	return (const GLubyte *)cache[slot];
}

static uint32_t getValues(uint32_t kind, GLenum pname, void *output)
{
	// 0 = integer, 1 = float, 2 = boolean
	glfwdLock();
	glfwdBegin(GLFWD_OP_GET_V);
	glfwdPutU32(kind);
	glfwdPutU32(pname);
	glfwdEnd();
	glfwdSync();

	uint32_t count = glfwdGetU32();

	for (uint32_t counter = 0; counter < count; counter++)
	{
		uint32_t value = glfwdGetU32();

		if (!output)
		{
			continue;
		}

		if (kind == 2)
		{
			((GLboolean *)output)[counter] = (GLboolean)value;
		}
		else
		{
			memcpy((char *)output + 4 * counter, &value, 4);
		}
	}

	glfwdUnlock();

	return count;
}

GL_APICALL void GL_APIENTRY glGetIntegerv(GLenum pname, GLint *data)
{
	FALLBACK(glGetIntegerv, void, (GLenum, GLint *), (pname, data));

	getValues(0, pname, data);
}

GL_APICALL void GL_APIENTRY glGetFloatv(GLenum pname, GLfloat *data)
{
	FALLBACK(glGetFloatv, void, (GLenum, GLfloat *), (pname, data));

	getValues(1, pname, data);
}

GL_APICALL void GL_APIENTRY glGetBooleanv(GLenum pname, GLboolean *data)
{
	FALLBACK(glGetBooleanv, void, (GLenum, GLboolean *), (pname, data));

	getValues(2, pname, data);
}

GL_APICALL void GL_APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                                         void *pixels)
{
	FALLBACK(glReadPixels, void, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *),
	         (x, y, width, height, format, type, pixels));

	glfwdLock();

	uint32_t length = getImageSize(width, height, format, type, packAlignment);

	glfwdBegin(GLFWD_OP_READ_PIXELS);
	glfwdPutU32((uint32_t)x);
	glfwdPutU32((uint32_t)y);
	glfwdPutU32((uint32_t)width);
	glfwdPutU32((uint32_t)height);
	glfwdPutU32(format);
	glfwdPutU32(type);
	glfwdPutU32(length);
	glfwdEnd();
	glfwdSync();
	glfwdGetInto(pixels, length);

	glfwdUnlock();
}

GL_APICALL void GL_APIENTRY glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
	FALLBACK(glShaderSource, void, (GLuint, GLsizei, const GLchar *const *, const GLint *), (shader, count, string, length));

	// join all parts to one null terminated string
	size_t totalLength = 0;

	for (GLsizei counter = 0; counter < count; counter++)
	{
		totalLength += length && length[counter] >= 0 ? (size_t)length[counter] : strlen(string[counter]);
	}

	char *source = malloc(totalLength + 1);
	char *position = source;

	for (GLsizei counter = 0; counter < count; counter++)
	{
		size_t partLength = length && length[counter] >= 0 ? (size_t)length[counter] : strlen(string[counter]);

		memcpy(position, string[counter], partLength);
		position += partLength;
	}

	*position = 0;

	glfwdLock();
	glfwdBegin(GLFWD_OP_SHADER_SOURCE);
	glfwdPutU32(shader);
	glfwdPutBlob(source, (uint32_t)totalLength + 1);
	glfwdEnd();
	glfwdUnlock();

	free(source);
}

// There are no shader binaries with the forwarding (0 formats reported)
GL_APICALL void GL_APIENTRY glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryFormat,
                                           const void *binary, GLsizei length)
{
	FALLBACK(glShaderBinary, void, (GLsizei, const GLuint *, GLenum, const void *, GLsizei),
	         (count, shaders, binaryFormat, binary, length));
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Wayland presentation -> shm buffers, event queue
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static void registryGlobal(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	(void)data;
	(void)version;

	if (strcmp(interface, "wl_shm") == 0)
	{
		waylandShm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	}
}

static void registryRemove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registryListener = { registryGlobal, registryRemove };

static int setupWayland(void)
{
	if (waylandShm)
	{
		return 1;
	}

	if (!waylandDisplay)
	{
		return 0;
	}

	eventQueue = wl_display_create_queue(waylandDisplay);

	struct wl_display *wrapper = wl_proxy_create_wrapper(waylandDisplay);
	wl_proxy_set_queue((struct wl_proxy *)wrapper, eventQueue);
	struct wl_registry *registry = wl_display_get_registry(wrapper);
	wl_proxy_wrapper_destroy(wrapper);

	wl_registry_add_listener(registry, &registryListener, NULL);
	wl_display_roundtrip_queue(waylandDisplay, eventQueue);

	if (!waylandShm)
	{
		LOG("Error: no wl_shm found\n");
	}

	return waylandShm != NULL;
}

static void dispatchQueue(int timeoutMs)
{
	while (wl_display_prepare_read_queue(waylandDisplay, eventQueue) != 0)
	{
		wl_display_dispatch_queue_pending(waylandDisplay, eventQueue);
	}

	wl_display_flush(waylandDisplay);

	struct pollfd pollEntry = { wl_display_get_fd(waylandDisplay), POLLIN, 0 };

	if (poll(&pollEntry, 1, timeoutMs) > 0)
	{
		wl_display_read_events(waylandDisplay);
	}
	else
	{
		wl_display_cancel_read(waylandDisplay);
	}

	wl_display_dispatch_queue_pending(waylandDisplay, eventQueue);
}

static void bufferRelease(void *data, struct wl_buffer *buffer)
{
	(void)buffer;

	*(int *)data = 0;
}

static const struct wl_buffer_listener bufferListener = { bufferRelease };

static void frameDone(void *data, struct wl_callback *callback, uint32_t time)
{
	(void)time;

	struct clientSurface *surface = data;

	wl_callback_destroy(callback);
	surface->frameCallback = NULL;
}

static const struct wl_callback_listener frameListener = { frameDone };

static void freeBuffers(struct clientSurface *surface)
{
	for (int counter = 0; counter < BUFFER_COUNT; counter++)
	{
		if (surface->buffers[counter].waylandBuffer)
		{
			wl_buffer_destroy(surface->buffers[counter].waylandBuffer);
			surface->buffers[counter].waylandBuffer = NULL;
		}
	}

	if (surface->poolMemory)
	{
		munmap(surface->poolMemory, surface->poolSize);
	}

	surface->poolMemory = NULL;
	surface->lastBufferIndex = -1;
}

static int allocBuffers(struct clientSurface *surface, int width, int height)
{
	freeBuffers(surface);

	size_t imageSize = (size_t)width * height * 4;
	surface->poolSize = imageSize * BUFFER_COUNT;

	int fd = memfd_create("glfwd", MFD_CLOEXEC);

	if (fd < 0 || ftruncate(fd, (off_t)surface->poolSize) != 0)
	{
		return 0;
	}

	surface->poolMemory = mmap(NULL, surface->poolSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	struct wl_shm_pool *pool = wl_shm_create_pool(waylandShm, fd, (int32_t)surface->poolSize);

	for (int counter = 0; counter < BUFFER_COUNT; counter++)
	{
		surface->buffers[counter].waylandBuffer = wl_shm_pool_create_buffer(pool, (int32_t)(counter * imageSize), width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
		surface->buffers[counter].memory = (char *)surface->poolMemory + counter * imageSize;
		surface->buffers[counter].busy = 0;

		wl_proxy_set_queue((struct wl_proxy *)surface->buffers[counter].waylandBuffer, eventQueue);
		wl_buffer_add_listener(surface->buffers[counter].waylandBuffer, &bufferListener, &surface->buffers[counter].busy);
	}

	wl_shm_pool_destroy(pool);
	close(fd);

	surface->width = width;
	surface->height = height;

	return 1;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// EGL -> display, configuration, surfaces, contexts
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static EGLint getConfigAttrib(EGLint attribute)
{
	switch (attribute)
	{
	case EGL_BUFFER_SIZE:
		return 32;

	case EGL_RED_SIZE:
	case EGL_GREEN_SIZE:
	case EGL_BLUE_SIZE:
	case EGL_ALPHA_SIZE:
		return 8;

	case EGL_DEPTH_SIZE:
		return 24;

	case EGL_STENCIL_SIZE:
		return 8;

	case EGL_CONFIG_ID:
		return 1;

	case EGL_SURFACE_TYPE:
		return EGL_WINDOW_BIT | EGL_PBUFFER_BIT;

	case EGL_RENDERABLE_TYPE:
	case EGL_CONFORMANT:
		return EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES_BIT;

	case EGL_COLOR_BUFFER_TYPE:
		return EGL_RGB_BUFFER;

	case EGL_CONFIG_CAVEAT:
		return EGL_NONE;

	case EGL_TRANSPARENT_TYPE:
		return EGL_NONE;

	case EGL_NATIVE_RENDERABLE:
		return EGL_TRUE;

	case EGL_MAX_SWAP_INTERVAL:
		return 1;

	case EGL_MAX_PBUFFER_WIDTH:
	case EGL_MAX_PBUFFER_HEIGHT:
		return 4096;

	case EGL_MAX_PBUFFER_PIXELS:
		return 4096 * 4096;

	default:
		return 0;   // SAMPLES, SAMPLE_BUFFERS, LEVEL, NATIVE_VISUAL_*, MIN_SWAP_INTERVAL, ...
	}
}

EGLAPI EGLint EGLAPIENTRY eglGetError(void)
{
	EGLFALLBACK(eglGetError, EGLint, (void), ());

	EGLint error = eglErrorCode;
	eglErrorCode = EGL_SUCCESS;

	return error;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType native)
{
	EGLFALLBACK(eglGetDisplay, EGLDisplay, (EGLNativeDisplayType), (native));

	if (!waylandDisplay)
	{
		if (native == EGL_DEFAULT_DISPLAY)
		{
			waylandDisplay = wl_display_connect(NULL);
		}
		else
		{
			waylandDisplay = (struct wl_display *)native;
		}
	}

	return OUR_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
	EGLFALLBACK(eglInitialize, EGLBoolean, (EGLDisplay, EGLint *, EGLint *), (dpy, major, minor));

	if (major)
	{
		*major = 1;
	}

	if (minor)
	{
		*minor = 4;
	}

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy)
{
	EGLFALLBACK(eglTerminate, EGLBoolean, (EGLDisplay), (dpy));

	return EGL_TRUE;
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
	EGLFALLBACK(eglQueryString, const char *, (EGLDisplay, EGLint), (dpy, name));

	switch (name)
	{
	case EGL_VENDOR:
		return "glfwd";

	case EGL_VERSION:
		return "1.4 glfwd";

	case EGL_CLIENT_APIS:
		return "OpenGL_ES";

	case EGL_EXTENSIONS:
		return "EGL_KHR_surfaceless_context";

	default:
		eglErrorCode = EGL_BAD_PARAMETER;
		return NULL;
	}
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint size, EGLint *num)
{
	EGLFALLBACK(eglGetConfigs, EGLBoolean, (EGLDisplay, EGLConfig *, EGLint, EGLint *), (dpy, configs, size, num));

	if (configs && size > 0)
	{
		configs[0] = OUR_CONFIG;
	}

	if (num)
	{
		*num = configs ? (size > 0) : 1;
	}

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib, EGLConfig *configs, EGLint size,
                                              EGLint *num)
{
	EGLFALLBACK(eglChooseConfig, EGLBoolean, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *),
	            (dpy, attrib, configs, size, num));

	int matches = 1;

	for (int counter = 0; attrib && attrib[counter] != EGL_NONE; counter += 2)
	{
		EGLint attribute = attrib[counter];
		EGLint value = attrib[counter + 1];

		if (value == EGL_DONT_CARE)
		{
			continue;
		}

		switch (attribute)
		{
		case EGL_RED_SIZE:
		case EGL_GREEN_SIZE:
		case EGL_BLUE_SIZE:
		case EGL_ALPHA_SIZE:
		case EGL_DEPTH_SIZE:
		case EGL_STENCIL_SIZE:
		case EGL_BUFFER_SIZE:
			matches &= value <= getConfigAttrib(attribute);
			break;

		case EGL_SAMPLES:
		case EGL_SAMPLE_BUFFERS:
			matches &= value == 0;
			break;

		case EGL_SURFACE_TYPE:
		case EGL_RENDERABLE_TYPE:
		case EGL_CONFORMANT:
			matches &= (value & ~getConfigAttrib(attribute)) == 0;
			break;

		default:
			break;
		}
	}

	if (num)
	{
		*num = matches ? 1 : 0;
	}

	if (matches && configs && size > 0)
	{
		configs[0] = OUR_CONFIG;
	}

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
	EGLFALLBACK(eglGetConfigAttrib, EGLBoolean, (EGLDisplay, EGLConfig, EGLint, EGLint *), (dpy, config, attribute, value));

	if (value)
	{
		*value = getConfigAttrib(attribute);
	}

	return EGL_TRUE;
}

static struct clientSurface *createSurface(int width, int height)
{
	struct clientSurface *surface = calloc(1, sizeof(*surface));

	surface->lastBufferIndex = -1;
	surface->swapInterval = 1;

	glfwdLock();
	glfwdBegin(GLFWD_OP_CREATE_SURFACE);
	glfwdPutU32((uint32_t)width);
	glfwdPutU32((uint32_t)height);
	glfwdEnd();
	glfwdSync();
	surface->id = glfwdGetU32();
	glfwdUnlock();

	surface->width = width;
	surface->height = height;

	return surface;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win,
                                                     const EGLint *attrib)
{
	EGLFALLBACK(eglCreateWindowSurface, EGLSurface, (EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *),
	            (dpy, config, win, attrib));

	struct wl_egl_window_v3 *eglWindow = (struct wl_egl_window_v3 *)win;

	if (!eglWindow || !setupWayland())
	{
		eglErrorCode = EGL_BAD_NATIVE_WINDOW;
		return EGL_NO_SURFACE;
	}

	struct clientSurface *surface = createSurface(eglWindow->width, eglWindow->height);

	surface->eglWindow = eglWindow;
	surface->waylandSurface = wl_proxy_create_wrapper(eglWindow->surface);
	wl_proxy_set_queue((struct wl_proxy *)surface->waylandSurface, eventQueue);

	surface->width = surface->height = 0;

	return (EGLSurface)surface;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib)
{
	EGLFALLBACK(eglCreatePbufferSurface, EGLSurface, (EGLDisplay, EGLConfig, const EGLint *), (dpy, config, attrib));

	int width = 1;
	int height = 1;

	for (int counter = 0; attrib && attrib[counter] != EGL_NONE; counter += 2)
	{
		if (attrib[counter] == EGL_WIDTH)
		{
			width = attrib[counter + 1];
		}

		if (attrib[counter] == EGL_HEIGHT)
		{
			height = attrib[counter + 1];
		}
	}

	return (EGLSurface)createSurface(width, height);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
	EGLFALLBACK(eglDestroySurface, EGLBoolean, (EGLDisplay, EGLSurface), (dpy, surface));

	struct clientSurface *clientSurface = surface;

	if (!clientSurface)
	{
		return EGL_FALSE;
	}

	glfwdLock();
	glfwdBegin(GLFWD_OP_DESTROY_SURFACE);
	glfwdPutU32(clientSurface->id);
	glfwdEnd();

	if (serverSurfaceId == clientSurface->id)
	{
		serverSurfaceId = 0;
	}

	glfwdUnlock();

	if (currentDrawSurface == clientSurface)
	{
		currentDrawSurface = NULL;
	}

	freeBuffers(clientSurface);

	if (clientSurface->frameCallback)
	{
		wl_callback_destroy(clientSurface->frameCallback);
	}

	if (clientSurface->waylandSurface)
	{
		wl_proxy_wrapper_destroy(clientSurface->waylandSurface);
	}

	free(clientSurface);

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value)
{
	EGLFALLBACK(eglQuerySurface, EGLBoolean, (EGLDisplay, EGLSurface, EGLint, EGLint *), (dpy, surface, attribute, value));

	struct clientSurface *clientSurface = surface;

	if (!clientSurface || !value)
	{
		return EGL_FALSE;
	}

	switch (attribute)
	{
	case EGL_WIDTH:
		*value = clientSurface->eglWindow ? clientSurface->eglWindow->width : clientSurface->width;
		break;

	case EGL_HEIGHT:
		*value = clientSurface->eglWindow ? clientSurface->eglWindow->height : clientSurface->height;
		break;

	case EGL_CONFIG_ID:
		*value = 1;
		break;

	case EGL_RENDER_BUFFER:
		*value = EGL_BACK_BUFFER;
		break;

	case EGL_SWAP_BEHAVIOR:
		*value = EGL_BUFFER_DESTROYED;
		break;

	default:
		*value = 0;
		break;
	}

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
	EGLFALLBACK(eglSurfaceAttrib, EGLBoolean, (EGLDisplay, EGLSurface, EGLint, EGLint), (dpy, surface, attribute, value));

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api)
{
	EGLFALLBACK(eglBindAPI, EGLBoolean, (EGLenum), (api));

	boundApi = api;

	return api == EGL_OPENGL_ES_API;
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void)
{
	EGLFALLBACK(eglQueryAPI, EGLenum, (void), ());

	return boundApi;
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share, const EGLint *attrib)
{
	EGLFALLBACK(eglCreateContext, EGLContext, (EGLDisplay, EGLConfig, EGLContext, const EGLint *), (dpy, config, share, attrib));

	uint32_t majorVersion = 1;

	for (int counter = 0; attrib && attrib[counter] != EGL_NONE; counter += 2)
	{
		if (attrib[counter] == EGL_CONTEXT_CLIENT_VERSION)
		{
			majorVersion = (uint32_t)attrib[counter + 1];
		}
	}

	struct clientContext *context = calloc(1, sizeof(*context));

	glfwdLock();
	glfwdBegin(GLFWD_OP_CREATE_CONTEXT);
	glfwdPutU32(majorVersion);
	glfwdEnd();
	glfwdSync();
	context->id = glfwdGetU32();
	glfwdUnlock();

	return (EGLContext)context;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
	EGLFALLBACK(eglDestroyContext, EGLBoolean, (EGLDisplay, EGLContext), (dpy, ctx));

	struct clientContext *context = ctx;

	if (!context)
	{
		return EGL_FALSE;
	}

	glfwdLock();
	glfwdBegin(GLFWD_OP_DESTROY_CONTEXT);
	glfwdPutU32(context->id);
	glfwdEnd();

	if (serverContextId == context->id)
	{
		serverContextId = 0;
	}

	glfwdUnlock();

	if (currentContext == context)
	{
		currentContext = NULL;
	}

	free(context);

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
	EGLFALLBACK(eglMakeCurrent, EGLBoolean, (EGLDisplay, EGLSurface, EGLSurface, EGLContext), (dpy, draw, read, ctx));

	(void)read;

	currentContext = ctx;
	currentDrawSurface = ctx ? draw : NULL;

	// glfwdLock reports the new context to the server
	glfwdLock();
	glfwdUnlock();

	return EGL_TRUE;
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
	EGLFALLBACK(eglGetCurrentContext, EGLContext, (void), ());

	return currentContext ? (EGLContext)currentContext : EGL_NO_CONTEXT;
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
	EGLFALLBACK(eglGetCurrentSurface, EGLSurface, (EGLint), (readdraw));

	return currentDrawSurface ? (EGLSurface)currentDrawSurface : EGL_NO_SURFACE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
	EGLFALLBACK(eglGetCurrentDisplay, EGLDisplay, (void), ());

	return currentContext ? OUR_DISPLAY : EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
	EGLFALLBACK(eglQueryContext, EGLBoolean, (EGLDisplay, EGLContext, EGLint, EGLint *), (dpy, ctx, attribute, value));

	if (!value)
	{
		return EGL_FALSE;
	}

	switch (attribute)
	{
	case EGL_CONFIG_ID:
		*value = 1;
		break;

	case EGL_CONTEXT_CLIENT_TYPE:
		*value = EGL_OPENGL_ES_API;
		break;

	case EGL_CONTEXT_CLIENT_VERSION:
		*value = 2;
		break;

	case EGL_RENDER_BUFFER:
		*value = EGL_BACK_BUFFER;
		break;

	default:
		*value = 0;
	}

	return EGL_TRUE;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// EGL -> swap (get the image from the server and attach it to the Wayland surface)
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
	EGLFALLBACK(eglSwapInterval, EGLBoolean, (EGLDisplay, EGLint), (dpy, interval));

	if (currentDrawSurface)
	{
		currentDrawSurface->swapInterval = interval > 0;
	}

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
	EGLFALLBACK(eglSwapBuffers, EGLBoolean, (EGLDisplay, EGLSurface), (dpy, surface));

	struct clientSurface *clientSurface = surface;

	if (!clientSurface || !clientSurface->eglWindow)
	{
		return EGL_TRUE;
	}

	int windowWidth = clientSurface->eglWindow->width;
	int windowHeight = clientSurface->eglWindow->height;

	// wait for the frame callback of the previous image
	// (with timeout, invisible ivi surfaces may get no callbacks)
	for (int counter = 0; clientSurface->swapInterval && clientSurface->frameCallback && counter < 10; counter++)
	{
		dispatchQueue(50);
	}

	//--------------------------request the image--------------------------
	glfwdLock();
	glfwdBegin(GLFWD_OP_SWAP);
	glfwdPutU32(clientSurface->id);
	glfwdPutU32((uint32_t)windowWidth);
	glfwdPutU32((uint32_t)windowHeight);
	glfwdEnd();
	glfwdSync();

	int width = (int)glfwdGetU32();
	int height = (int)glfwdGetU32();
	int firstRow = (int)glfwdGetU32();
	int endRow = (int)glfwdGetU32();

	if (width != clientSurface->width || height != clientSurface->height)
	{
		allocBuffers(clientSurface, width, height);
	}

	if (firstRow >= endRow && clientSurface->lastBufferIndex >= 0)
	{
		glfwdUnlock();
		dispatchQueue(0);
		return EGL_TRUE;
	}

	//--------------------------select a free buffer--------------------------
	wl_display_dispatch_queue_pending(waylandDisplay, eventQueue);

	int bufferIndex = -1;

	for (int attempt = 0; bufferIndex < 0; attempt++)
	{
		for (int counter = 0; counter < BUFFER_COUNT; counter++)
		{
			if (!clientSurface->buffers[counter].busy)
			{
				bufferIndex = counter;
				break;
			}
		}

		if (bufferIndex < 0)
		{
			// emergency -> overwrite the oldest buffer
			if (attempt >= 20)
			{
				bufferIndex = 0;
				break;
			}

			dispatchQueue(25);
		}
	}

	//--------------------------receive the image--------------------------
	// The server sends only the changed rows firstRow..endRow -> the rest comes from the last shown buffer
	size_t rowSize = (size_t)width * 4;

	if (clientSurface->lastBufferIndex >= 0 && clientSurface->lastBufferIndex != bufferIndex && (firstRow > 0 || endRow < height))
	{
		memcpy(clientSurface->buffers[bufferIndex].memory, clientSurface->buffers[clientSurface->lastBufferIndex].memory, rowSize * height);
	}

	glfwdGetInto((char *)clientSurface->buffers[bufferIndex].memory + rowSize * firstRow, (uint32_t)(rowSize * (endRow - firstRow)));
	glfwdUnlock();

	//--------------------------attach and commit--------------------------
	clientSurface->buffers[bufferIndex].busy = 1;
	clientSurface->lastBufferIndex = bufferIndex;

	wl_surface_attach(clientSurface->waylandSurface, clientSurface->buffers[bufferIndex].waylandBuffer, 0, 0);
	wl_surface_damage(clientSurface->waylandSurface, 0, firstRow, width, endRow - firstRow);

	if (clientSurface->swapInterval)
	{
		clientSurface->frameCallback = wl_surface_frame(clientSurface->waylandSurface);
		wl_callback_add_listener(clientSurface->frameCallback, &frameListener, clientSurface);
	}

	wl_surface_commit(clientSurface->waylandSurface);

	clientSurface->eglWindow->attached_width = width;
	clientSurface->eglWindow->attached_height = height;

	wl_display_flush(waylandDisplay);

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
{
	EGLFALLBACK(eglCopyBuffers, EGLBoolean, (EGLDisplay, EGLSurface, EGLNativePixmapType), (dpy, surface, target));

	eglErrorCode = EGL_BAD_NATIVE_PIXMAP;

	return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{
	EGLFALLBACK(eglReleaseThread, EGLBoolean, (void), ());

	currentContext = NULL;
	currentDrawSurface = NULL;

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitClient(void)
{
	EGLFALLBACK(eglWaitClient, EGLBoolean, (void), ());

	glFinish();

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void)
{
	EGLFALLBACK(eglWaitGL, EGLBoolean, (void), ());

	glFinish();

	return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine)
{
	EGLFALLBACK(eglWaitNative, EGLBoolean, (EGLint), (engine));

	return EGL_TRUE;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// EGL -> eglGetProcAddress
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static const struct
{
	const char *name;
	void *function;
} procTable[] =
{
#include "glfwd-gen-procs.inc"
	{ "eglGetError", (void *)eglGetError }, { "eglSwapBuffers", (void *)eglSwapBuffers },
	{ "eglMakeCurrent", (void *)eglMakeCurrent }, { "eglSwapInterval", (void *)eglSwapInterval },
};

static long glfwdNoop(void)
{
	return 0;
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *name)
{
	EGLFALLBACK(eglGetProcAddress, __eglMustCastToProperFunctionPointerType, (const char *), (name));

	for (size_t counter = 0; name && counter < sizeof(procTable) / sizeof(procTable[0]); counter++)
	{
		if (strcmp(procTable[counter].name, name) == 0)
		{
			return (__eglMustCastToProperFunctionPointerType)procTable[counter].function;
		}
	}

	return (__eglMustCastToProperFunctionPointerType)glfwdNoop;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

__attribute__((constructor)) static void glfwdInitMutex(void)
{
	pthread_mutexattr_t attributes;

	pthread_mutexattr_init(&attributes);
	pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&glfwdMutex, &attributes);
}

static void glfwdConnectOnce(void)
{
	const char *enabled = getenv("MIB2_GLFWD");

	if (!enabled || strcmp(enabled, "1") != 0 || isDenied())
	{
		return;
	}

	serverSocket = connectServer();

	if (serverSocket < 0)
	{
		LOG("%s: Info: server not reachable -> using local Mesa\n", program_invocation_short_name);
		return;
	}

	glfwdOn = 1;

	pthread_mutex_lock(&glfwdMutex);
	glfwdBegin(GLFWD_OP_HELLO);
	glfwdPutBlob(program_invocation_short_name, (uint32_t)strlen(program_invocation_short_name) + 1);
	glfwdEnd();
	glfwdSync();
	uint32_t version = glfwdGetU32();
	pthread_mutex_unlock(&glfwdMutex);

	LOG("%s: Info: GL forwarding active (server protocol v%u)\n", program_invocation_short_name, version);
}

static int glfwdTry(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, glfwdConnectOnce);

	return glfwdOn;
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
