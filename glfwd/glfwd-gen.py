#!/usr/bin/env python3
#**********************************************************
# Project: RCD330-Plus Desay Emulator (Chroot)
#**********************************************************
# glfwd-gen.py -> code generator for the GL forwarding (called by build-glfwd.sh)
#
#       glfwd-gen-ops.h       -> opcodes
#       glfwd-gen-client.inc  -> ARM wrappers (serialize, wait for the answer if needed)
#       glfwd-gen-server.inc  -> x86 dispatcher (deserialize, call the real GL, answer)
#       glfwd-gen-procs.inc   -> name table for eglGetProcAddress
#
#       "in:<expression>"   -> input data, length in bytes (expression over other parameters)
#       "str"               -> null terminated input string
#       "out:<expression>"  -> output data, length in bytes -> call becomes synchronous
#       "offset"            -> buffer offset instead of a pointer (VBO/IBO bound)
#
#   Functions with a return value are always synchronous.
#

import os
import re
import sys

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# Specification -> (prototype, annotations of the pointer parameters)
# Client and server must always be built together!
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

SPEC = [
    ("void glActiveTexture(GLenum texture)", {}),
    ("void glAttachShader(GLuint program, GLuint shader)", {}),
    ("void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name)", {"name": "str"}),
    ("void glBindFramebuffer(GLenum target, GLuint framebuffer)", {}),
    ("void glBindRenderbuffer(GLenum target, GLuint renderbuffer)", {}),
    ("void glBindTexture(GLenum target, GLuint texture)", {}),
    ("void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)", {}),
    ("void glBlendEquation(GLenum mode)", {}),
    ("void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)", {}),
    ("void glBlendFunc(GLenum sfactor, GLenum dfactor)", {}),
    ("void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)", {}),
    ("GLenum glCheckFramebufferStatus(GLenum target)", {}),
    ("void glClear(GLbitfield mask)", {}),
    ("void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)", {}),
    ("void glClearDepthf(GLfloat d)", {}),
    ("void glClearStencil(GLint s)", {}),
    ("void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)", {}),
    ("void glCompileShader(GLuint shader)", {}),
    ("void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data)", {"data": "in:imageSize"}),
    ("void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data)", {"data": "in:imageSize"}),
    ("void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)", {}),
    ("void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)", {}),
    ("GLuint glCreateProgram(void)", {}),
    ("GLuint glCreateShader(GLenum type)", {}),
    ("void glCullFace(GLenum mode)", {}),
    ("void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers)", {"framebuffers": "in:n*4"}),
    ("void glDeleteProgram(GLuint program)", {}),
    ("void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers)", {"renderbuffers": "in:n*4"}),
    ("void glDeleteShader(GLuint shader)", {}),
    ("void glDeleteTextures(GLsizei n, const GLuint *textures)", {"textures": "in:n*4"}),
    ("void glDepthFunc(GLenum func)", {}),
    ("void glDepthMask(GLboolean flag)", {}),
    ("void glDepthRangef(GLfloat n, GLfloat f)", {}),
    ("void glDetachShader(GLuint program, GLuint shader)", {}),
    ("void glDisable(GLenum cap)", {}),
    ("void glEnable(GLenum cap)", {}),
    ("void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)", {}),
    ("void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)", {}),
    ("void glFrontFace(GLenum mode)", {}),
    ("void glGenBuffers(GLsizei n, GLuint *buffers)", {"buffers": "out:n*4"}),
    ("void glGenerateMipmap(GLenum target)", {}),
    ("void glGenFramebuffers(GLsizei n, GLuint *framebuffers)", {"framebuffers": "out:n*4"}),
    ("void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers)", {"renderbuffers": "out:n*4"}),
    ("void glGenTextures(GLsizei n, GLuint *textures)", {"textures": "out:n*4"}),
    ("void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)",
     {"length": "out:4", "size": "out:4", "type": "out:4", "name": "out:bufSize"}),
    ("void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name)",
     {"length": "out:4", "size": "out:4", "type": "out:4", "name": "out:bufSize"}),
    ("void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders)",
     {"count": "out:4", "shaders": "out:maxCount*4"}),
    ("GLint glGetAttribLocation(GLuint program, const GLchar *name)", {"name": "str"}),
    ("void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params)", {"params": "out:4"}),
    ("void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params)", {"params": "out:4"}),
    ("void glGetProgramiv(GLuint program, GLenum pname, GLint *params)", {"params": "out:4"}),
    ("void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)", {"length": "out:4", "infoLog": "out:bufSize"}),
    ("void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params)", {"params": "out:4"}),
    ("void glGetShaderiv(GLuint shader, GLenum pname, GLint *params)", {"params": "out:4"}),
    ("void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)", {"length": "out:4", "infoLog": "out:bufSize"}),
    ("void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision)", {"range": "out:8", "precision": "out:4"}),
    ("void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source)", {"length": "out:4", "source": "out:bufSize"}),
    ("void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)", {"params": "out:4"}),
    ("void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)", {"params": "out:4"}),
    ("GLint glGetUniformLocation(GLuint program, const GLchar *name)", {"name": "str"}),
    ("void glHint(GLenum target, GLenum mode)", {}),
    ("GLboolean glIsBuffer(GLuint buffer)", {}),
    ("GLboolean glIsEnabled(GLenum cap)", {}),
    ("GLboolean glIsFramebuffer(GLuint framebuffer)", {}),
    ("GLboolean glIsProgram(GLuint program)", {}),
    ("GLboolean glIsRenderbuffer(GLuint renderbuffer)", {}),
    ("GLboolean glIsShader(GLuint shader)", {}),
    ("GLboolean glIsTexture(GLuint texture)", {}),
    ("void glLineWidth(GLfloat width)", {}),
    ("void glLinkProgram(GLuint program)", {}),
    ("void glPolygonOffset(GLfloat factor, GLfloat units)", {}),
    ("void glReleaseShaderCompiler(void)", {}),
    ("void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)", {}),
    ("void glSampleCoverage(GLfloat value, GLboolean invert)", {}),
    ("void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)", {}),
    ("void glStencilFunc(GLenum func, GLint ref, GLuint mask)", {}),
    ("void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)", {}),
    ("void glStencilMask(GLuint mask)", {}),
    ("void glStencilMaskSeparate(GLenum face, GLuint mask)", {}),
    ("void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)", {}),
    ("void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass)", {}),
    ("void glTexParameterf(GLenum target, GLenum pname, GLfloat param)", {}),
    ("void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)", {"params": "in:4"}),
    ("void glTexParameteri(GLenum target, GLenum pname, GLint param)", {}),
    ("void glTexParameteriv(GLenum target, GLenum pname, const GLint *params)", {"params": "in:4"}),
    ("void glUniform1f(GLint location, GLfloat v0)", {}),
    ("void glUniform2f(GLint location, GLfloat v0, GLfloat v1)", {}),
    ("void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)", {}),
    ("void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)", {}),
    ("void glUniform1i(GLint location, GLint v0)", {}),
    ("void glUniform2i(GLint location, GLint v0, GLint v1)", {}),
    ("void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2)", {}),
    ("void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3)", {}),
    ("void glUniform1fv(GLint location, GLsizei count, const GLfloat *value)", {"value": "in:count*4"}),
    ("void glUniform2fv(GLint location, GLsizei count, const GLfloat *value)", {"value": "in:count*8"}),
    ("void glUniform3fv(GLint location, GLsizei count, const GLfloat *value)", {"value": "in:count*12"}),
    ("void glUniform4fv(GLint location, GLsizei count, const GLfloat *value)", {"value": "in:count*16"}),
    ("void glUniform1iv(GLint location, GLsizei count, const GLint *value)", {"value": "in:count*4"}),
    ("void glUniform2iv(GLint location, GLsizei count, const GLint *value)", {"value": "in:count*8"}),
    ("void glUniform3iv(GLint location, GLsizei count, const GLint *value)", {"value": "in:count*12"}),
    ("void glUniform4iv(GLint location, GLsizei count, const GLint *value)", {"value": "in:count*16"}),
    ("void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)", {"value": "in:count*16"}),
    ("void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)", {"value": "in:count*36"}),
    ("void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)", {"value": "in:count*64"}),
    ("void glUseProgram(GLuint program)", {}),
    ("void glValidateProgram(GLuint program)", {}),
    ("void glVertexAttrib1f(GLuint index, GLfloat x)", {}),
    ("void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y)", {}),
    ("void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z)", {}),
    ("void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)", {}),
    ("void glVertexAttrib1fv(GLuint index, const GLfloat *v)", {"v": "in:4"}),
    ("void glVertexAttrib2fv(GLuint index, const GLfloat *v)", {"v": "in:8"}),
    ("void glVertexAttrib3fv(GLuint index, const GLfloat *v)", {"v": "in:12"}),
    ("void glVertexAttrib4fv(GLuint index, const GLfloat *v)", {"v": "in:16"}),
    ("void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)", {}),
    ("void glBindBuffer(GLenum target, GLuint buffer)", {}),
    ("void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)", {"data": "in:size"}),
    ("void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data)", {"data": "in:size"}),
    ("void glDeleteBuffers(GLsizei n, const GLuint *buffers)", {"buffers": "in:n*4"}),
    ("void glEnableVertexAttribArray(GLuint index)", {}),
    ("void glDisableVertexAttribArray(GLuint index)", {}),
    ("void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)", {"pointer": "offset"}),
    ("void glDrawArrays(GLenum mode, GLint first, GLsizei count)", {}),
    ("void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)", {"indices": "offset"}),
    ("void glPixelStorei(GLenum pname, GLint param)", {}),
    ("void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)", {"pixels": "in:0"}),
    ("void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)", {"pixels": "in:0"}),
    ("void glFlush(void)", {}),
]

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# Special cases
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

CLIENT_CUSTOM = {"glBindBuffer", "glBufferData", "glBufferSubData", "glDeleteBuffers",
                 "glEnableVertexAttribArray", "glDisableVertexAttribArray", "glVertexAttribPointer",
                 "glDrawArrays", "glDrawElements", "glPixelStorei", "glTexImage2D", "glTexSubImage2D",
                 "glFlush"}

SERVER_CUSTOM = {"glBindFramebuffer"}

EXTRA_PROCS = ["glGetString", "glGetIntegerv", "glGetFloatv", "glGetBooleanv", "glGetError", "glFinish",
               "glReadPixels", "glShaderSource", "glShaderBinary"]

FLOAT_TYPES = {"GLfloat", "GLclampf"}

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# Functions -> parse prototype, generate client/server code
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

def parsePrototype(prototype):
    # "ret name(type a, type b)" -> (ret, name, [(type, name), ...])
    match = re.match(r"(\w+)\s+(\w+)\((.*)\)$", prototype)
    returnType, name, parameters = match.group(1), match.group(2), match.group(3).strip()
    arguments = []

    if parameters and parameters != "void":
        for parameter in parameters.split(","):
            parameter = parameter.strip()
            parameterMatch = re.match(r"(.*?)(\w+)$", parameter)
            argumentType, argumentName = parameterMatch.group(1).strip(), parameterMatch.group(2)
            arguments.append((argumentType, argumentName))

    return returnType, name, arguments


def generateClient(returnType, name, arguments, annotations, opcode, isSync, outputs):
    parameterList = ", ".join(f"{argType} {argName}" for argType, argName in arguments) or "void"
    argumentNames = ", ".join(argName for _, argName in arguments)

    functionName = f"glfwdSend_{name}" if name in CLIENT_CUSTOM else name
    storage = "static " if name in CLIENT_CUSTOM else "GL_APICALL "
    code = [f"{storage}{returnType} GL_APIENTRY {functionName}({parameterList})", "{"]

    if name not in CLIENT_CUSTOM:
        code.append(f"    if (!glfwdOn) {{ static {returnType} (*real)({parameterList}); if (!real) real = glfwdReal(\"{name}\");"
                    f" {'return ' if returnType != 'void' else ''}real({argumentNames}); {'return;' if returnType == 'void' else ''} }}")

    if returnType != "void":
        code.append(f"    {returnType} r;")

    code.append(f"    glfwdLock(); glfwdBegin({opcode});")

    for argType, argName in arguments:
        annotation = annotations.get(argName, "")

        if annotation.startswith("in:"):
            expression = annotation[3:]
            if expression == "0":
                code.append(f"    glfwdPutBlob({argName}, {argName} ? glfwdBlobLength : 0);")
            else:
                code.append(f"    glfwdPutBlob({argName}, {argName} ? (uint32_t)({expression}) : 0);")
        elif annotation == "str":
            code.append(f"    glfwdPutBlob({argName}, {argName} ? (uint32_t)strlen({argName}) + 1 : 0);")
        elif annotation.startswith("out:"):
            code.append(f"    glfwdPutU32((uint32_t)({annotation[4:]}) > 0x10000000u ? 0 : (uint32_t)({annotation[4:]}));")
        elif annotation == "offset":
            code.append(f"    glfwdPutU32((uint32_t)(uintptr_t){argName});")
        elif argType in FLOAT_TYPES:
            code.append(f"    glfwdPutF32({argName});")
        else:
            code.append(f"    glfwdPutU32((uint32_t){argName});")

    code.append("    glfwdEnd();")

    if isSync:
        code.append("    glfwdSync();")
        if returnType != "void":
            code.append(f"    r = ({returnType})glfwdGetU32();")
        for argType, argName in outputs:
            expression = annotations[argName][4:]
            code.append(f"    glfwdGetInto({argName}, (uint32_t)({expression}) > 0x10000000u ? 0 : (uint32_t)({expression}));")

    code.append("    glfwdUnlock();")

    if returnType != "void":
        code.append("    return r;")

    code.append("}")
    return "\n".join(code)


def generateServer(returnType, name, arguments, annotations, opcode, isSync):
    code = [f"case {opcode}: {{"]
    callArguments = []
    freeList = []

    for argType, argName in arguments:
        annotation = annotations.get(argName, "")

        if annotation.startswith("in:") or annotation == "str":
            code.append(f"    const void *{argName} = readBlob(connection, NULL);")
            callArguments.append(f"({argType}){argName}")
        elif annotation.startswith("out:"):
            code.append(f"    uint32_t {argName}_len = readU32(connection); void *{argName} = calloc(1, {argName}_len + 4);")
            freeList.append(argName)
            callArguments.append(f"({argType}){argName}")
        elif annotation == "offset":
            code.append(f"    uintptr_t {argName} = readU32(connection);")
            callArguments.append(f"({argType}){argName}")
        elif argType in FLOAT_TYPES:
            code.append(f"    {argType} {argName} = readF32(connection);")
            callArguments.append(argName)
        else:
            code.append(f"    {argType} {argName} = ({argType})readU32(connection);")
            callArguments.append(argName)

    callString = f"{name}({', '.join(callArguments)})"

    if returnType != "void":
        code.append(f"    uint32_t r = (uint32_t){callString};")
    else:
        code.append(f"    {callString};")

    if isSync:
        code.append("    replyBegin(connection);")
        if returnType != "void":
            code.append("    replyU32(connection, r);")
        for argName in freeList:
            code.append(f"    replyBytes(connection, {argName}, {argName}_len);")
        code.append("    replyEnd(connection);")

    for argName in freeList:
        code.append(f"    free({argName});")

    code.append("    break; }")
    return "\n".join(code)

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
# main - function
#-----------------------------------------------------------------------------------------------------------------------------------------------------------

def generate(outputFolder):
    opcodes, clientCode, serverCode, procTable = [], [], [], []

    for index, (prototype, annotations) in enumerate(SPEC):
        returnType, name, arguments = parsePrototype(prototype)
        opcode = f"GLFWD_OP_{name}"
        opcodes.append(f"#define {opcode} (GLFWD_OP_GL_BASE + {index})")

        outputs = [argument for argument in arguments if annotations.get(argument[1], "").startswith("out:")]
        isSync = returnType != "void" or bool(outputs)

        clientCode.append(generateClient(returnType, name, arguments, annotations, opcode, isSync, outputs))

        if name not in CLIENT_CUSTOM:
            procTable.append(f'    {{ "{name}", (void *){name} }},')

        if name not in SERVER_CUSTOM:
            serverCode.append(generateServer(returnType, name, arguments, annotations, opcode, isSync))

    for name in EXTRA_PROCS + sorted(CLIENT_CUSTOM):
        procTable.append(f'    {{ "{name}", (void *){name} }},')

    #--------------------------write files--------------------------
    header = "/* generated by glfwd-gen.py -> do not edit */\n"

    with open(os.path.join(outputFolder, "glfwd-gen-ops.h"), "w") as file:
        file.write(header + "\n".join(opcodes) + "\n")
    with open(os.path.join(outputFolder, "glfwd-gen-client.inc"), "w") as file:
        file.write(header + "\n\n".join(clientCode) + "\n")
    with open(os.path.join(outputFolder, "glfwd-gen-server.inc"), "w") as file:
        file.write(header + "\n".join(serverCode) + "\n")
    with open(os.path.join(outputFolder, "glfwd-gen-procs.inc"), "w") as file:
        file.write(header + "\n".join(procTable) + "\n")

    print(f"Info: glfwd-gen: {len(SPEC)} functions, {len(SERVER_CUSTOM)} server and "
          f"{len(CLIENT_CUSTOM)} client special cases -> {outputFolder}")


if __name__ == "__main__":
    generate(sys.argv[1] if len(sys.argv) > 1 else ".")

#-----------------------------------------------------------------------------------------------------------------------------------------------------------
