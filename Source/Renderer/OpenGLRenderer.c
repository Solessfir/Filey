// OpenGL 3.3 core backend. Entry points are loaded at runtime through the
// platform, so no system GL headers are needed.
//
// Every primitive is one instanced quad; the fragment shader turns it into a
// solid, rounded, outlined or texture-masked rectangle. Draw calls only split
// when the clip or the texture changes.

#include <math.h>

typedef u32       GLenum;
typedef u32       GLbitfield;
typedef u32       GLuint;
typedef i32       GLint;
typedef i32       GLsizei;
typedef f32       GLfloat;
typedef u8        GLubyte;
typedef u8        GLboolean;
typedef char      GLchar;
typedef i64       GLintptr;
typedef i64       GLsizeiptr;

#define GL_TRIANGLE_STRIP        0x0005
#define GL_SRC_ALPHA             0x0302
#define GL_ONE                   1
#define GL_ONE_MINUS_SRC_ALPHA   0x0303
#define GL_BLEND                 0x0BE2
#define GL_SCISSOR_TEST          0x0C11
#define GL_UNPACK_ALIGNMENT      0x0CF5
#define GL_TEXTURE_2D            0x0DE1
#define GL_UNSIGNED_BYTE         0x1401
#define GL_FLOAT                 0x1406
#define GL_RED                   0x1903
#define GL_RGBA                  0x1908
#define GL_VERSION               0x1F02
#define GL_NEAREST               0x2600
#define GL_TEXTURE_MAG_FILTER    0x2800
#define GL_TEXTURE_MIN_FILTER    0x2801
#define GL_TEXTURE_WRAP_S        0x2802
#define GL_TEXTURE_WRAP_T        0x2803
#define GL_COLOR_BUFFER_BIT      0x00004000
#define GL_CLAMP_TO_EDGE         0x812F
#define GL_RGBA8                 0x8058
#define GL_R8                    0x8229
#define GL_ARRAY_BUFFER          0x8892
#define GL_STREAM_DRAW           0x88E0
#define GL_FRAGMENT_SHADER       0x8B30
#define GL_VERTEX_SHADER         0x8B31
#define GL_COMPILE_STATUS        0x8B81
#define GL_LINK_STATUS           0x8B82

#if defined(_WIN32)
#define GL_API __stdcall
#else
#define GL_API
#endif

#define GL_FUNCTIONS(X) \
    X(void,            Viewport,                GLint x, GLint y, GLsizei width, GLsizei height) \
    X(void,            ClearColor,              GLfloat r, GLfloat g, GLfloat b, GLfloat a) \
    X(void,            Clear,                   GLbitfield mask) \
    X(const GLubyte *, GetString,               GLenum name) \
    X(void,            Enable,                  GLenum cap) \
    X(void,            BlendFuncSeparate,       GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) \
    X(void,            Scissor,                 GLint x, GLint y, GLsizei width, GLsizei height) \
    X(void,            GenTextures,             GLsizei n, GLuint *textures) \
    X(void,            BindTexture,             GLenum target, GLuint texture) \
    X(void,            TexImage2D,              GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) \
    X(void,            TexSubImage2D,           GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) \
    X(void,            TexParameteri,           GLenum target, GLenum pname, GLint param) \
    X(void,            PixelStorei,             GLenum pname, GLint param) \
    X(GLuint,          CreateShader,            GLenum type) \
    X(void,            ShaderSource,            GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) \
    X(void,            CompileShader,           GLuint shader) \
    X(void,            GetShaderiv,             GLuint shader, GLenum pname, GLint *params) \
    X(void,            GetShaderInfoLog,        GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) \
    X(void,            DeleteShader,            GLuint shader) \
    X(GLuint,          CreateProgram,           void) \
    X(void,            AttachShader,            GLuint program, GLuint shader) \
    X(void,            LinkProgram,             GLuint program) \
    X(void,            GetProgramiv,            GLuint program, GLenum pname, GLint *params) \
    X(void,            GetProgramInfoLog,       GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) \
    X(void,            UseProgram,              GLuint program) \
    X(GLint,           GetUniformLocation,      GLuint program, const GLchar *name) \
    X(void,            Uniform2f,               GLint location, GLfloat v0, GLfloat v1) \
    X(void,            GenVertexArrays,         GLsizei n, GLuint *arrays) \
    X(void,            BindVertexArray,         GLuint array) \
    X(void,            GenBuffers,              GLsizei n, GLuint *buffers) \
    X(void,            BindBuffer,              GLenum target, GLuint buffer) \
    X(void,            BufferData,              GLenum target, GLsizeiptr size, const void *data, GLenum usage) \
    X(void,            VertexAttribPointer,     GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) \
    X(void,            VertexAttribDivisor,     GLuint index, GLuint divisor) \
    X(void,            EnableVertexAttribArray, GLuint index) \
    X(void,            DrawArraysInstanced,     GLenum mode, GLint first, GLsizei count, GLsizei instanceCount)

#define X(ret, name, ...) typedef ret GL_API GL_##name##Fn(__VA_ARGS__);
GL_FUNCTIONS(X)
#undef X

typedef struct GL_Instance {
    R_Rect  rect;
    R_Rect  uv;
    R_Color color;
    f32     radius;
    f32     thickness;  // 0 fills; otherwise an inner outline this wide
    f32     textured;   // 1 multiplies alpha by the texture's red channel; 2 multiplies by its RGBA
    f32     pad;
} GL_Instance;

typedef struct GL_Batch {
    GLuint texture;     // 0 until a textured instance joins the batch
    R_Rect clip;
    u32    first;
    u32    count;
} GL_Batch;

typedef struct GL_ArrayInstance { ArrayHeader header; GL_Instance *v; } GL_ArrayInstance;
typedef struct GL_ArrayBatch    { ArrayHeader header; GL_Batch *v; } GL_ArrayBatch;

#define GL_MaxClipDepth 64

typedef struct GL_State {
#define X(ret, name, ...) GL_##name##Fn *name;
    GL_FUNCTIONS(X)
#undef X

    GLuint program;
    GLint  viewportLocation;
    GLuint vertexArray;
    GLuint instanceBuffer;

    Arena            frameArena;
    GL_ArrayInstance instances;
    GL_ArrayBatch    batches;
    R_Rect           clips[GL_MaxClipDepth];
    u32              clipDepth;
    i32              width;
    i32              height;
} GL_State;

global GL_State GL;

global const char GL_VertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 aRect;\n"
    "layout(location = 1) in vec4 aUV;\n"
    "layout(location = 2) in vec4 aColor;\n"
    "layout(location = 3) in vec3 aStyle;\n"
    "uniform vec2 uViewport;\n"
    "out vec2 vUV;\n"
    "out vec4 vColor;\n"
    "out vec2 vLocal;\n"
    "out vec2 vHalfSize;\n"
    "out vec3 vStyle;\n"
    "void main() {\n"
    "    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    "    vec2 position = mix(aRect.xy, aRect.zw, corner);\n"
    "    vHalfSize = 0.5 * (aRect.zw - aRect.xy);\n"
    "    vLocal = (corner * 2.0 - 1.0) * vHalfSize;\n"
    "    vUV = mix(aUV.xy, aUV.zw, corner);\n"
    "    vColor = aColor;\n"
    "    vStyle = aStyle;\n"
    "    vec2 ndc = position / uViewport * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "}\n";

global const char GL_FragmentShader[] =
    "#version 330 core\n"
    "uniform sampler2D uTexture;\n"
    "in vec2 vUV;\n"
    "in vec4 vColor;\n"
    "in vec2 vLocal;\n"
    "in vec2 vHalfSize;\n"
    "in vec3 vStyle;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float radius = min(vStyle.x, min(vHalfSize.x, vHalfSize.y));\n"
    "    vec2 q = abs(vLocal) - vHalfSize + radius;\n"
    "    float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;\n"
    "    float alpha = clamp(0.5 - distance, 0.0, 1.0);\n"
    "    if (vStyle.y > 0.0) {\n"
    "        alpha *= clamp(0.5 + distance + vStyle.y, 0.0, 1.0);\n"
    "    }\n"
    "    vec4 color = vColor;\n"
    "    if (vStyle.z > 1.5) {\n"
    "        color *= texture(uTexture, vUV);\n"
    "    } else if (vStyle.z > 0.0) {\n"
    "        alpha *= texture(uTexture, vUV).r;\n"
    "    }\n"
    "    fragColor = vec4(color.rgb, color.a * alpha);\n"
    "}\n";

function R_TextureHandle GL__TextureHandleFromRef(GLuint ref)
{
    R_TextureHandle result = {0};
    static_assert(sizeof(ref) <= sizeof(result));
    MemCopy(&result, &ref, sizeof(ref));
    return result;
}

function GLuint GL__TextureRefFromHandle(R_TextureHandle handle)
{
    GLuint result = 0;
    MemCopy(&result, &handle, sizeof(result));
    return result;
}

function GLuint GL__CompileShader(GLenum type, const char *source)
{
    GLuint shader = GL.CreateShader(type);
    GL.ShaderSource(shader, 1, &source, NULL);
    GL.CompileShader(shader);

    GLint compiled = 0;
    GL.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLchar log[1024] = {0};
        GL.GetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "Shader compilation failed:\n%s\n", log);
        GL.DeleteShader(shader);
        return 0;
    }
    return shader;
}

function b32 R_Init(void)
{
#define X(ret, name, ...) \
    GL.name = (GL_##name##Fn *)PlatformGLGetProc("gl" #name); \
    if (GL.name == NULL) { \
        fprintf(stderr, "Missing OpenGL function gl" #name "\n"); \
        return false; \
    }
    GL_FUNCTIONS(X)
#undef X

#if defined(DEBUG)
    fprintf(stderr, "OpenGL %s\n", (const char *)GL.GetString(GL_VERSION));
#endif

    GLuint vertexShader = GL__CompileShader(GL_VERTEX_SHADER, GL_VertexShader);
    GLuint fragmentShader = GL__CompileShader(GL_FRAGMENT_SHADER, GL_FragmentShader);
    if (vertexShader == 0 || fragmentShader == 0) {
        return false;
    }
    GL.program = GL.CreateProgram();
    GL.AttachShader(GL.program, vertexShader);
    GL.AttachShader(GL.program, fragmentShader);
    GL.LinkProgram(GL.program);
    GL.DeleteShader(vertexShader);
    GL.DeleteShader(fragmentShader);

    GLint linked = 0;
    GL.GetProgramiv(GL.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLchar log[1024] = {0};
        GL.GetProgramInfoLog(GL.program, (GLsizei)sizeof(log), NULL, log);
        fprintf(stderr, "Shader linking failed:\n%s\n", log);
        return false;
    }
    GL.viewportLocation = GL.GetUniformLocation(GL.program, "uViewport");

    GL.GenVertexArrays(1, &GL.vertexArray);
    GL.BindVertexArray(GL.vertexArray);
    GL.GenBuffers(1, &GL.instanceBuffer);
    GL.BindBuffer(GL_ARRAY_BUFFER, GL.instanceBuffer);
    for (GLuint i = 0; i < 4; ++i) {
        GL.EnableVertexAttribArray(i);
        GL.VertexAttribDivisor(i, 1);
    }

    GL.Enable(GL_BLEND);
    // The window's alpha is premultiplied coverage, which compositors blend with what is behind it.
    GL.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    GL.Enable(GL_SCISSOR_TEST);
    GL.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return true;
}

function R_TextureHandle R_TextureCreateAlpha(i32 width, i32 height, const u8 *pixels)
{
    GLuint texture = 0;
    GL.GenTextures(1, &texture);
    GL.BindTexture(GL_TEXTURE_2D, texture);
    GL.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    // Glyphs are drawn at their rasterized size, so no filtering is wanted.
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return GL__TextureHandleFromRef(texture);
}

function R_TextureHandle R_TextureCreateRGBA(i32 width, i32 height, const u32 *pixels)
{
    GLuint texture = 0;
    GL.GenTextures(1, &texture);
    GL.BindTexture(GL_TEXTURE_2D, texture);
    GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    // Images are uploaded at the size they are drawn.
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return GL__TextureHandleFromRef(texture);
}

function void R_TextureUpdateRGBA(R_TextureHandle texture, i32 x, i32 y, i32 width, i32 height, const u32 *pixels)
{
    GL.BindTexture(GL_TEXTURE_2D, GL__TextureRefFromHandle(texture));
    GL.TexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

function R_Rect GL__CurrentClip(void)
{
    if (GL.clipDepth > 0) {
        return GL.clips[GL.clipDepth - 1];
    }
    return (R_Rect){ 0.0f, 0.0f, (f32)GL.width, (f32)GL.height };
}

function void R_BeginFrame(i32 width, i32 height, R_Color clear)
{
    ArenaClear(&GL.frameArena);
    GL.instances = (GL_ArrayInstance){0};
    GL.batches = (GL_ArrayBatch){0};
    GL.clipDepth = 0;
    GL.width = width;
    GL.height = height;

    GL.Viewport(0, 0, width, height);
    GL.Scissor(0, 0, width, height);
    GL.ClearColor(clear.r * clear.a, clear.g * clear.a, clear.b * clear.a, clear.a);
    GL.Clear(GL_COLOR_BUFFER_BIT);
}

function void GL__PushInstance(GL_Instance instance, GLuint texture)
{
    R_Rect clip = GL__CurrentClip();
    GL_Batch *batch = GL.batches.header.count > 0 ? &GL.batches.v[GL.batches.header.count - 1] : NULL;
    b32 sameClip = batch != NULL && MemCompare(&batch->clip, &clip, sizeof(clip)) == 0;
    b32 compatibleTexture = batch != NULL && (texture == 0 || batch->texture == 0 || batch->texture == texture);
    if (!sameClip || !compatibleTexture) {
        batch = ArrayAddClear(&GL.frameArena, GL.batches, 1);
        batch->clip = clip;
        batch->first = (u32)GL.instances.header.count;
    }
    if (texture != 0) {
        batch->texture = texture;
    }
    batch->count += 1;
    ArrayPush(&GL.frameArena, GL.instances, instance);
}

function void R_DrawRect(R_Rect rect, R_Color color, f32 cornerRadius)
{
    GL__PushInstance((GL_Instance){ .rect = rect, .color = color, .radius = cornerRadius }, 0);
}

function void R_DrawRectOutline(R_Rect rect, R_Color color, f32 cornerRadius, f32 thickness)
{
    GL__PushInstance((GL_Instance){ .rect = rect, .color = color, .radius = cornerRadius, .thickness = thickness }, 0);
}

function void R_DrawTexturedRect(R_Rect rect, R_Rect uv, R_TextureHandle texture, R_Color color)
{
    GL__PushInstance((GL_Instance){ .rect = rect, .uv = uv, .color = color, .textured = 1.0f }, GL__TextureRefFromHandle(texture));
}

function void R_DrawImage(R_Rect rect, R_Rect uv, R_TextureHandle texture, R_Color tint)
{
    GL__PushInstance((GL_Instance){ .rect = rect, .uv = uv, .color = tint, .textured = 2.0f }, GL__TextureRefFromHandle(texture));
}

function void R_PushClip(R_Rect rect)
{
    Assert(GL.clipDepth < GL_MaxClipDepth);
    R_Rect outer = GL__CurrentClip();
    R_Rect clip = {
        MaxF32(rect.x0, outer.x0),
        MaxF32(rect.y0, outer.y0),
        MinF32(rect.x1, outer.x1),
        MinF32(rect.y1, outer.y1),
    };
    clip.x1 = MaxF32(clip.x1, clip.x0);
    clip.y1 = MaxF32(clip.y1, clip.y0);
    GL.clips[GL.clipDepth++] = clip;
}

function void R_PopClip(void)
{
    Assert(GL.clipDepth > 0);
    GL.clipDepth -= 1;
}

function void R_EndFrame(void)
{
    Assert(GL.clipDepth == 0);
    if (GL.instances.header.count == 0) {
        return;
    }

    GL.UseProgram(GL.program);
    GL.Uniform2f(GL.viewportLocation, (f32)GL.width, (f32)GL.height);
    GL.BindVertexArray(GL.vertexArray);
    GL.BindBuffer(GL_ARRAY_BUFFER, GL.instanceBuffer);
    GL.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(GL.instances.header.count * sizeof(GL_Instance)), GL.instances.v, GL_STREAM_DRAW);

    for (u64 i = 0; i < GL.batches.header.count; ++i) {
        GL_Batch *batch = &GL.batches.v[i];
        // GL 3.3 has no base-instance draw, so the attributes are rebased instead.
        u64 base = batch->first * sizeof(GL_Instance);
        GLsizei stride = (GLsizei)sizeof(GL_Instance);
        GL.VertexAttribPointer(0, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, rect)));
        GL.VertexAttribPointer(1, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, uv)));
        GL.VertexAttribPointer(2, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, color)));
        GL.VertexAttribPointer(3, 3, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, radius)));

        // Scissor rects are bottom-up.
        i32 x0 = (i32)floorf(batch->clip.x0);
        i32 y0 = (i32)floorf(batch->clip.y0);
        i32 x1 = (i32)ceilf(batch->clip.x1);
        i32 y1 = (i32)ceilf(batch->clip.y1);
        GL.Scissor(x0, GL.height - y1, x1 - x0, y1 - y0);
        GL.BindTexture(GL_TEXTURE_2D, batch->texture);
        GL.DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)batch->count);
    }
}
