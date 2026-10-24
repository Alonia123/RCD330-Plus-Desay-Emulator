//**********************************************************
// Project: RCD330-Plus Desay Emulator (Chroot)
//**********************************************************
// glfwd-proto.h -> protocol between the ARM client (libglfwd.so, in the device apps) and the x86 render server
//
//**********************************************************

#ifndef GLFWD_PROTO_H
#define GLFWD_PROTO_H

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Preprocessor directive
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

#define GLFWD_NULL 0xFFFFFFFFu

#define GLFWD_VERSION 2
#define GLFWD_SOCK_DEFAULT "/tmp/run/emu/glfwd.sock"

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
// Opcodes -> control and EGL opcodes; the generated GL opcodes start at GLFWD_OP_GL_BASE
//-----------------------------------------------------------------------------------------------------------------------------------------------------------

enum
{
	GLFWD_OP_HELLO = 1,          // str process name                        -> u32 version
	GLFWD_OP_CREATE_CONTEXT,     // u32 major                               -> u32 context
	GLFWD_OP_DESTROY_CONTEXT,    // u32 context
	GLFWD_OP_CREATE_SURFACE,     // u32 width, u32 height                   -> u32 surface
	GLFWD_OP_DESTROY_SURFACE,    // u32 surface
	GLFWD_OP_MAKE_CURRENT,       // u32 context, u32 surface (0 = none)
	GLFWD_OP_SWAP,               // u32 surface, u32 width, u32 height      -> u32 width, u32 height, u32 y0, u32 y1 |
	                             //   rows y0..y1-1 (each width*4 bytes XRGB8888, top first); y0 == y1 -> image unchanged.
	                             //   After a size change always the whole image (0..height).
	GLFWD_OP_GET_STRING,         // u32 name                                -> u32 length | bytes
	GLFWD_OP_GET_V,              // u32 kind (0=i, 1=f, 2=b), u32 pname     -> u32 n | n values (4 bytes)
	GLFWD_OP_CLIENT_ATTRIB,      // u32 index, size, type, normalized, stride, first | data (client vertex array)
	GLFWD_OP_DRAW_ELEMENTS_DATA, // u32 mode, count, type | index data
	GLFWD_OP_READ_PIXELS,        // i32 x, y, width, height, u32 format, type, length -> length bytes
	GLFWD_OP_SHADER_SOURCE,      // u32 shader | source text
	GLFWD_OP_FINISH,             //                                         -> u32 0
	GLFWD_OP_GL_BASE = 100
};

#endif

//-----------------------------------------------------------------------------------------------------------------------------------------------------------
