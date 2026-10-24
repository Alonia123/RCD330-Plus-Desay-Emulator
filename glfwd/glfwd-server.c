//**********************************************************
// Project: RCD330-Plus Desay Emulator (Chroot)
//**********************************************************
// glfwd-server.c -> native x86 render server for the GL forwarding
//
//**********************************************************

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "glfwd-proto.h"
#include "glfwd-gen-ops.h"

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Preprocessor directive
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

#define MAX_OBJECTS 256
#define MAX_ATTRIBS 32

#define LOG_PREFIX "[glfwd-server] "

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Data structures
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

struct serverContext
{
	EGLContext eglContext;
	GLuint appFramebuffer;
	int initialized;
	GLuint surfaceFbo[MAX_OBJECTS];
};

struct serverSurface
{
	int used;
	int width;
	int height;
	GLuint colorBuffer;                 // renderbuffers (shared between the shared contexts)
	GLuint depthBuffer;
	unsigned char *lastFrame;
	int lastWidth;
	int lastHeight;
};

struct connection
{
	int fd;
	char name[64];

	unsigned char *buffer;              // current message
	size_t capacity;
	size_t position;
	size_t length;

	unsigned char *replyBuffer;         // answer
	size_t replyLength;
	size_t replyCapacity;

	struct serverContext *contexts[MAX_OBJECTS];
	struct serverSurface surfaces[MAX_OBJECTS];
	uint32_t currentContext;
	uint32_t currentSurface;
	EGLContext shareContext;

	void *attribMemory[MAX_ATTRIBS];
};

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static EGLDisplay eglDisplay;
static EGLConfig eglConfig;

static const char *allowedExtensions[] =
{
	"GL_OES_texture_npot", "GL_OES_depth24", "GL_OES_packed_depth_stencil", "GL_OES_rgb8_rgba8",
	"GL_OES_element_index_uint", "GL_OES_standard_derivatives", "GL_OES_depth_texture",
	"GL_EXT_texture_format_BGRA8888", "GL_EXT_blend_minmax", "GL_OES_texture_float",
	"GL_OES_texture_half_float", "GL_OES_vertex_half_float", "GL_OES_compressed_ETC1_RGB8_texture",
	"GL_EXT_texture_filter_anisotropic", "GL_OES_fragment_precision_high", NULL
};

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Function prototypes
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static int readAll(int fd, void *data, size_t size);
static uint32_t readU32(struct connection *connection);
static float readF32(struct connection *connection);
static const void *readBlob(struct connection *connection, uint32_t *lengthOut);
static void replyBegin(struct connection *connection);
static void replyBytes(struct connection *connection, const void *data, size_t size);
static void replyU32(struct connection *connection, uint32_t value);
static void replyEnd(struct connection *connection);

static GLuint getSurfaceFbo(struct connection *connection);
static void makeCurrent(struct connection *connection, uint32_t contextId, uint32_t surfaceId);
static void resizeSurface(struct connection *connection, uint32_t surfaceId, int width, int height);

static int getValueCount(GLenum pname);
static void handleGetV(struct connection *connection);
static void handleGetString(struct connection *connection);
static void handleSwap(struct connection *connection);
static void dispatchMessage(struct connection *connection, uint32_t opcode);

static void *connectionThread(void *argument);
static int waitForClients(const char *path);

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// main - function
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : GLFWD_SOCK_DEFAULT;

	signal(SIGPIPE, SIG_IGN);

	// EGL without window system (surfaceless)
	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	eglDisplay = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : EGL_NO_DISPLAY;

	if (eglDisplay == EGL_NO_DISPLAY || !eglInitialize(eglDisplay, NULL, NULL))
	{
		fprintf(stderr, LOG_PREFIX "Error: EGL surfaceless not available\n");
		return 1;
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	const EGLint configAttribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
	EGLint configCount = 0;

	if (!eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &configCount) || configCount < 1)
	{
		fprintf(stderr, LOG_PREFIX "Error: no matching EGL configuration\n");
		return 1;
	}

	{
		const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
		EGLContext testContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);

		eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, testContext);
		fprintf(stderr, LOG_PREFIX "Renderer: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
		eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(eglDisplay, testContext);
	}

	return waitForClients(path);
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// additional functions -> read message, write answer
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static int readAll(int fd, void *data, size_t size)
{
	unsigned char *position = data;

	while (size)
	{
		ssize_t result = read(fd, position, size);

		if (result < 0 && errno == EINTR)
		{
			continue;
		}

		if (result <= 0)
		{
			return -1;
		}

		position += result;
		size -= (size_t)result;
	}

	return 0;
}

static uint32_t readU32(struct connection *connection)
{
	uint32_t value = 0;

	if (connection->position + 4 <= connection->length)
	{
		memcpy(&value, connection->buffer + connection->position, 4);
	}

	connection->position += 4;

	return value;
}

static float readF32(struct connection *connection)
{
	uint32_t value = readU32(connection);
	float result;

	memcpy(&result, &value, 4);

	return result;
}

static const void *readBlob(struct connection *connection, uint32_t *lengthOut)
{
	uint32_t length = readU32(connection);

	if (length == GLFWD_NULL)
	{
		if (lengthOut)
		{
			*lengthOut = 0;
		}
		return NULL;
	}

	const void *data = connection->buffer + connection->position;

	// blocks are padded to 4 bytes
	connection->position += (length + 3) & ~3u;

	if (lengthOut)
	{
		*lengthOut = length;
	}

	return data;
}

static void replyBegin(struct connection *connection)
{
	connection->replyLength = 0;
}

static void replyBytes(struct connection *connection, const void *data, size_t size)
{
	if (connection->replyLength + size > connection->replyCapacity)
	{
		while (connection->replyLength + size > connection->replyCapacity)
		{
			connection->replyCapacity = connection->replyCapacity ? connection->replyCapacity * 2 : 65536;
		}

		connection->replyBuffer = realloc(connection->replyBuffer, connection->replyCapacity);
	}

	memcpy(connection->replyBuffer + connection->replyLength, data, size);
	connection->replyLength += size;
}

static void replyU32(struct connection *connection, uint32_t value)
{
	replyBytes(connection, &value, 4);
}

static void replyEnd(struct connection *connection)
{
	size_t offset = 0;

	while (offset < connection->replyLength)
	{
		ssize_t written = write(connection->fd, connection->replyBuffer + offset, connection->replyLength - offset);

		if (written < 0 && errno == EINTR)
		{
			continue;
		}

		if (written <= 0)
		{
			return;
		}

		offset += (size_t)written;
	}
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// additional functions -> surfaces and contexts
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static GLuint getSurfaceFbo(struct connection *connection)
{
	struct serverContext *context = connection->contexts[connection->currentContext];
	struct serverSurface *surface = connection->currentSurface < MAX_OBJECTS ? &connection->surfaces[connection->currentSurface] : NULL;

	if (!context || !surface || !surface->used)
	{
		return 0;
	}

	// Renderbuffers of the surface
	if (!surface->colorBuffer)
	{
		glGenRenderbuffers(1, &surface->colorBuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, surface->colorBuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, surface->width, surface->height);

		glGenRenderbuffers(1, &surface->depthBuffer);
		glBindRenderbuffer(GL_RENDERBUFFER, surface->depthBuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, surface->width, surface->height);
	}

	// FBO per context (FBOs are not shared)
	if (!context->surfaceFbo[connection->currentSurface])
	{
		GLuint framebuffer;

		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, surface->colorBuffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, surface->depthBuffer);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, surface->depthBuffer);

		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

		if (status != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, LOG_PREFIX "%s: Info: surface FBO incomplete 0x%x\n", connection->name, status);
		}

		context->surfaceFbo[connection->currentSurface] = framebuffer;
	}

	return context->surfaceFbo[connection->currentSurface];
}

static void makeCurrent(struct connection *connection, uint32_t contextId, uint32_t surfaceId)
{
	connection->currentContext = contextId < MAX_OBJECTS ? contextId : 0;
	connection->currentSurface = surfaceId < MAX_OBJECTS ? surfaceId : 0;

	struct serverContext *context = connection->contexts[connection->currentContext];

	if (!context)
	{
		eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		return;
	}

	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, context->eglContext);

	GLuint framebuffer = getSurfaceFbo(connection);

	if (context->appFramebuffer == 0)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	}

	if (!context->initialized && connection->surfaces[connection->currentSurface].used)
	{
		glViewport(0, 0, connection->surfaces[connection->currentSurface].width, connection->surfaces[connection->currentSurface].height);
		glScissor(0, 0, connection->surfaces[connection->currentSurface].width, connection->surfaces[connection->currentSurface].height);
		context->initialized = 1;
	}
}

static void resizeSurface(struct connection *connection, uint32_t surfaceId, int width, int height)
{
	struct serverSurface *surface = &connection->surfaces[surfaceId];

	if (width <= 0 || height <= 0 || (width == surface->width && height == surface->height))
	{
		return;
	}

	surface->width = width;
	surface->height = height;

	if (surface->colorBuffer)
	{
		glBindRenderbuffer(GL_RENDERBUFFER, surface->colorBuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, width, height);
		glBindRenderbuffer(GL_RENDERBUFFER, surface->depthBuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, width, height);
	}
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// additional functions -> glGet*, glGetString, swap
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static int getValueCount(GLenum pname)
{
	// number of values per pname
	switch (pname)
	{
	case GL_VIEWPORT:
	case GL_SCISSOR_BOX:
	case GL_COLOR_WRITEMASK:
	case GL_COLOR_CLEAR_VALUE:
	case GL_BLEND_COLOR:
		return 4;

	case GL_DEPTH_RANGE:
	case GL_ALIASED_LINE_WIDTH_RANGE:
	case GL_ALIASED_POINT_SIZE_RANGE:
	case GL_MAX_VIEWPORT_DIMS:
		return 2;

	case GL_COMPRESSED_TEXTURE_FORMATS:
	{
		GLint count = 0;
		glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &count);
		return count;
	}

	case GL_SHADER_BINARY_FORMATS:
	case GL_PROGRAM_BINARY_FORMATS_OES:
		return 0;

	default:
		return 1;
	}
}

static void handleGetV(struct connection *connection)
{
	uint32_t kind = readU32(connection);            // 0 = integer, 1 = float, 2 = boolean
	GLenum pname = readU32(connection);
	struct serverContext *context = connection->contexts[connection->currentContext];
	uint32_t values[256];
	int count = getValueCount(pname);

	if (count > 256)
	{
		count = 256;
	}

	if (pname == GL_FRAMEBUFFER_BINDING)
	{
		// report the surface FBO as framebuffer 0
		values[0] = context ? context->appFramebuffer : 0;

		if (kind == 1)
		{
			float floatValue = (float)values[0];
			memcpy(&values[0], &floatValue, 4);
		}

		count = 1;
	}
	else if (pname == GL_NUM_SHADER_BINARY_FORMATS || pname == GL_NUM_PROGRAM_BINARY_FORMATS_OES)
	{
		values[0] = 0;
		count = 1;
	}
	else if (kind == 0)
	{
		GLint temp[256] = { 0 };
		glGetIntegerv(pname, temp);
		memcpy(values, temp, sizeof(values));
	}
	else if (kind == 1)
	{
		GLfloat temp[256] = { 0 };
		glGetFloatv(pname, temp);
		memcpy(values, temp, sizeof(values));
	}
	else
	{
		GLboolean temp[256] = { 0 };
		glGetBooleanv(pname, temp);

		for (int counter = 0; counter < count; counter++)
		{
			values[counter] = temp[counter];
		}
	}

	replyBegin(connection);
	replyU32(connection, (uint32_t)count);
	replyBytes(connection, values, (size_t)count * 4);
	replyEnd(connection);
}

static void handleGetString(struct connection *connection)
{
	GLenum name = readU32(connection);
	char output[4096] = "";

	if (name == GL_EXTENSIONS)
	{
		const char *allExtensions = (const char *)glGetString(GL_EXTENSIONS);

		for (int counter = 0; allowedExtensions[counter] && allExtensions; counter++)
		{
			const char *found = strstr(allExtensions, allowedExtensions[counter]);
			size_t length = strlen(allowedExtensions[counter]);

			if (found && (found[length] == ' ' || found[length] == 0))
			{
				strcat(output, allowedExtensions[counter]);
				strcat(output, " ");
			}
		}
	}
	else if (name == GL_VERSION)
	{
		snprintf(output, sizeof(output), "OpenGL ES 2.0 glfwd (%s)", (const char *)glGetString(GL_VERSION));
	}
	else if (name == GL_SHADING_LANGUAGE_VERSION)
	{
		snprintf(output, sizeof(output), "OpenGL ES GLSL ES 1.00");
	}
	else
	{
		const char *value = (const char *)glGetString(name);
		snprintf(output, sizeof(output), "%s", value ? value : "");
	}

	replyBegin(connection);
	replyU32(connection, (uint32_t)strlen(output));
	replyBytes(connection, output, strlen(output));
	replyEnd(connection);
}

static void handleSwap(struct connection *connection)
{
	uint32_t surfaceId = readU32(connection);
	int windowWidth = (int)readU32(connection);
	int windowHeight = (int)readU32(connection);

	struct serverSurface *surface = surfaceId < MAX_OBJECTS ? &connection->surfaces[surfaceId] : NULL;
	int width = surface && surface->used ? surface->width : 1;
	int height = surface && surface->used ? surface->height : 1;
	size_t size = (size_t)width * height * 4;

	unsigned char *pixels = malloc(size);
	unsigned char *output = malloc(size);

	memset(pixels, 0, size);

	//--------------------------read back the image of the surface FBO--------------------------
	if (surface && surface->used && connection->contexts[connection->currentContext])
	{
		GLint previousFramebuffer = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

		uint32_t keepSurface = connection->currentSurface;
		connection->currentSurface = surfaceId;
		glBindFramebuffer(GL_FRAMEBUFFER, getSurfaceFbo(connection));
		connection->currentSurface = keepSurface;

		GLint packAlignment;
		glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);

		resizeSurface(connection, surfaceId, windowWidth, windowHeight);
	}

	//--------------------------GL: bottom first -> Wayland: top first; RGBA -> B,G,R,X--------------------------
	for (int row = 0; row < height; row++)
	{
		const unsigned char *source = pixels + (size_t)(height - 1 - row) * width * 4;
		unsigned char *target = output + (size_t)row * width * 4;

		for (int column = 0; column < width; column++)
		{
			target[4 * column + 0] = source[4 * column + 2];
			target[4 * column + 1] = source[4 * column + 1];
			target[4 * column + 2] = source[4 * column + 0];
			target[4 * column + 3] = 0xFF;
		}
	}

	//--------------------------send only the changed rows--------------------------
	int firstRow = 0;
	int endRow = height;

	if (surface && surface->lastFrame && surface->lastWidth == width && surface->lastHeight == height)
	{
		size_t rowSize = (size_t)width * 4;

		while (firstRow < height && memcmp(output + firstRow * rowSize, surface->lastFrame + firstRow * rowSize, rowSize) == 0)
		{
			firstRow++;
		}

		while (endRow > firstRow && memcmp(output + (endRow - 1) * rowSize, surface->lastFrame + (endRow - 1) * rowSize, rowSize) == 0)
		{
			endRow--;
		}
	}

	replyBegin(connection);
	replyU32(connection, (uint32_t)width);
	replyU32(connection, (uint32_t)height);
	replyU32(connection, (uint32_t)firstRow);
	replyU32(connection, (uint32_t)endRow);
	replyBytes(connection, output + (size_t)firstRow * width * 4, (size_t)(endRow - firstRow) * width * 4);
	replyEnd(connection);

	free(pixels);

	// keep the image for the next diff
	if (surface)
	{
		free(surface->lastFrame);
		surface->lastFrame = output;
		surface->lastWidth = width;
		surface->lastHeight = height;
	}
	else
	{
		free(output);
	}
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// additional - functions -> dispatcher for all opcodes
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static void dispatchMessage(struct connection *connection, uint32_t opcode)
{
	switch (opcode)
	{
	case GLFWD_OP_HELLO:
	{
		const char *name = readBlob(connection, NULL);

		snprintf(connection->name, sizeof(connection->name), "%s", name ? name : "?");
		fprintf(stderr, LOG_PREFIX "client connected: %s\n", connection->name);

		replyBegin(connection);
		replyU32(connection, GLFWD_VERSION);
		replyEnd(connection);
		break;
	}

	case GLFWD_OP_CREATE_CONTEXT:
	{
		readU32(connection);
		uint32_t contextId = 0;

		for (uint32_t counter = 1; counter < MAX_OBJECTS; counter++)
		{
			if (!connection->contexts[counter])
			{
				contextId = counter;
				break;
			}
		}

		if (contextId)
		{
			const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
			struct serverContext *context = calloc(1, sizeof(*context));

			context->eglContext = eglCreateContext(eglDisplay, eglConfig, connection->shareContext ? connection->shareContext : EGL_NO_CONTEXT, contextAttribs);

			if (context->eglContext == EGL_NO_CONTEXT)
			{
				fprintf(stderr, LOG_PREFIX "Error: eglCreateContext failed 0x%x\n", eglGetError());
				free(context);
				contextId = 0;
			}
			else
			{
				if (!connection->shareContext)
				{
					connection->shareContext = context->eglContext;
				}
				connection->contexts[contextId] = context;
			}
		}

		replyBegin(connection);
		replyU32(connection, contextId);
		replyEnd(connection);
		break;
	}

	case GLFWD_OP_DESTROY_CONTEXT:
	{
		uint32_t contextId = readU32(connection);

		if (contextId < MAX_OBJECTS && connection->contexts[contextId] && connection->contexts[contextId]->eglContext != connection->shareContext)
		{
			if (connection->currentContext == contextId)
			{
				makeCurrent(connection, 0, 0);
			}

			eglDestroyContext(eglDisplay, connection->contexts[contextId]->eglContext);
			free(connection->contexts[contextId]);
			connection->contexts[contextId] = NULL;
		}
		break;
	}

	case GLFWD_OP_CREATE_SURFACE:
	{
		int width = (int)readU32(connection);
		int height = (int)readU32(connection);
		uint32_t surfaceId = 0;

		for (uint32_t counter = 1; counter < MAX_OBJECTS; counter++)
		{
			if (!connection->surfaces[counter].used)
			{
				surfaceId = counter;
				break;
			}
		}

		if (surfaceId)
		{
			free(connection->surfaces[surfaceId].lastFrame);
			memset(&connection->surfaces[surfaceId], 0, sizeof(connection->surfaces[surfaceId]));
			connection->surfaces[surfaceId].used = 1;
			connection->surfaces[surfaceId].width = width > 0 ? width : 1;
			connection->surfaces[surfaceId].height = height > 0 ? height : 1;
		}

		replyBegin(connection);
		replyU32(connection, surfaceId);
		replyEnd(connection);
		break;
	}

	case GLFWD_OP_DESTROY_SURFACE:
	{
		uint32_t surfaceId = readU32(connection);

		if (surfaceId < MAX_OBJECTS)
		{
			connection->surfaces[surfaceId].used = 0;
			free(connection->surfaces[surfaceId].lastFrame);
			connection->surfaces[surfaceId].lastFrame = NULL;
		}
		break;
	}

	case GLFWD_OP_MAKE_CURRENT:
	{
		uint32_t contextId = readU32(connection);
		uint32_t surfaceId = readU32(connection);

		makeCurrent(connection, contextId, surfaceId);
		break;
	}

	case GLFWD_OP_SWAP:
		handleSwap(connection);
		break;

	case GLFWD_OP_GET_STRING:
		handleGetString(connection);
		break;

	case GLFWD_OP_GET_V:
		handleGetV(connection);
		break;

	case GLFWD_OP_CLIENT_ATTRIB:
	{
		uint32_t index = readU32(connection);
		uint32_t size = readU32(connection);
		uint32_t type = readU32(connection);
		uint32_t normalized = readU32(connection);
		uint32_t stride = readU32(connection);
		uint32_t first = readU32(connection);
		uint32_t length;
		const void *data = readBlob(connection, &length);

		if (index >= MAX_ATTRIBS)
		{
			break;
		}

		free(connection->attribMemory[index]);
		connection->attribMemory[index] = malloc(length ? length : 1);
		memcpy(connection->attribMemory[index], data, length);

		GLint previousBuffer = 0;
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glVertexAttribPointer(index, (GLint)size, type, (GLboolean)normalized, (GLsizei)stride,
		                      (const char *)connection->attribMemory[index] - (size_t)first * stride);
		glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previousBuffer);
		break;
	}

	case GLFWD_OP_DRAW_ELEMENTS_DATA:
	{
		GLenum mode = readU32(connection);
		GLsizei count = (GLsizei)readU32(connection);
		GLenum type = readU32(connection);
		const void *indices = readBlob(connection, NULL);

		GLint previousBuffer = 0;
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glDrawElements(mode, count, type, indices);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)previousBuffer);
		break;
	}

	case GLFWD_OP_READ_PIXELS:
	{
		GLint x = (GLint)readU32(connection);
		GLint y = (GLint)readU32(connection);
		GLsizei width = (GLsizei)readU32(connection);
		GLsizei height = (GLsizei)readU32(connection);
		GLenum format = readU32(connection);
		GLenum type = readU32(connection);
		uint32_t length = readU32(connection);
		void *buffer = calloc(1, length + 4);

		glReadPixels(x, y, width, height, format, type, buffer);

		replyBegin(connection);
		replyBytes(connection, buffer, length);
		replyEnd(connection);

		free(buffer);
		break;
	}

	case GLFWD_OP_SHADER_SOURCE:
	{
		GLuint shader = readU32(connection);
		uint32_t length;
		const char *source = readBlob(connection, &length);
		GLint sourceLength = length ? (GLint)length - 1 : 0;

		glShaderSource(shader, 1, &source, &sourceLength);
		break;
	}

	case GLFWD_OP_FINISH:
		glFinish();

		replyBegin(connection);
		replyU32(connection, 0);
		replyEnd(connection);
		break;

	case GLFWD_OP_glBindFramebuffer:
	{
		GLenum target = readU32(connection);
		GLuint framebuffer = readU32(connection);
		struct serverContext *context = connection->contexts[connection->currentContext];

		if (context)
		{
			context->appFramebuffer = framebuffer;
		}

		glBindFramebuffer(target, framebuffer ? framebuffer : getSurfaceFbo(connection));
		break;
	}

#include "glfwd-gen-server.inc"

	default:
		fprintf(stderr, LOG_PREFIX "%s: Info: unknown opcode %u\n", connection->name, opcode);
	}
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// additional functions -> threads (one per client, one per listen address)
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

static void *connectionThread(void *argument)
{
	struct connection *connection = argument;

	snprintf(connection->name, sizeof(connection->name), "?");

	while (1)
	{
		// header: u32 opcode | u32 payload length
		uint32_t header[2];

		if (readAll(connection->fd, header, 8) != 0)
		{
			break;
		}

		if (header[1] > connection->capacity)
		{
			connection->capacity = header[1] + 65536;
			connection->buffer = realloc(connection->buffer, connection->capacity);
		}

		if (readAll(connection->fd, connection->buffer, header[1]) != 0)
		{
			break;
		}

		connection->length = header[1];
		connection->position = 0;

		dispatchMessage(connection, header[0]);
	}

	//--------------------------client disconnected -> free everything--------------------------
	fprintf(stderr, LOG_PREFIX "client disconnected: %s\n", connection->name);
	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	for (int counter = 0; counter < MAX_OBJECTS; counter++)
	{
		if (connection->contexts[counter])
		{
			eglDestroyContext(eglDisplay, connection->contexts[counter]->eglContext);
			free(connection->contexts[counter]);
		}
	}

	for (int counter = 0; counter < MAX_ATTRIBS; counter++)
	{
		free(connection->attribMemory[counter]);
	}

	for (int counter = 0; counter < MAX_OBJECTS; counter++)
	{
		free(connection->surfaces[counter].lastFrame);
	}

	close(connection->fd);
	free(connection->buffer);
	free(connection->replyBuffer);
	free(connection);
	eglReleaseThread();

	return NULL;
}

// Unix socket -> one thread per client
static int waitForClients(const char *path)
{
	struct sockaddr_un address = { .sun_family = AF_UNIX };
	int listenSocket = socket(AF_UNIX, SOCK_STREAM, 0);

	strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
	unlink(path);

	if (bind(listenSocket, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listenSocket, 16) != 0)
	{
		perror(LOG_PREFIX "Error: bind/listen");
		return 1;
	}

	chmod(path, 0777);

	fprintf(stderr, LOG_PREFIX "waiting on %s\n", path);

	while (1)
	{
		int fd = accept(listenSocket, NULL, NULL);

		if (fd < 0)
		{
			continue;
		}

		struct connection *connection = calloc(1, sizeof(*connection));
		connection->fd = fd;

		pthread_t thread;
		pthread_create(&thread, NULL, connectionThread, connection);
		pthread_detach(thread);
	}
}

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
