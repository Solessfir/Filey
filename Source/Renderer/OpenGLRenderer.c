// OpenGL 3.3 core backend.
// Entry points are loaded at runtime through the platform, so no system GL headers are needed.
//
// Every primitive is one instanced quad; the fragment shader turns it into a solid, rounded, outlined or texture-masked rectangle.
// Draw calls only split when the clip or the texture changes.

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
#define GL_BGRA                  0x80E1
#define GL_VERSION               0x1F02
#define GL_NEAREST               0x2600
#define GL_TEXTURE_MAG_FILTER    0x2800
#define GL_TEXTURE_MIN_FILTER    0x2801
#define GL_TEXTURE_WRAP_S        0x2802
#define GL_TEXTURE_WRAP_T        0x2803
#define GL_COLOR_BUFFER_BIT      0x00004000
#define GL_CLAMP_TO_EDGE         0x812F
#define GL_REPEAT                0x2901
#define GL_RGBA8                 0x8058
#define GL_R8                    0x8229
#define GL_ARRAY_BUFFER          0x8892
#define GL_STREAM_DRAW           0x88E0
#define GL_FRAGMENT_SHADER       0x8B30
#define GL_VERTEX_SHADER         0x8B31
#define GL_COMPILE_STATUS        0x8B81
#define GL_LINK_STATUS           0x8B82
#define GL_ZERO                  0
#define GL_TRIANGLES             0x0004
#define GL_LINEAR                0x2601
#define GL_LINEAR_MIPMAP_LINEAR  0x2703
#define GL_TEXTURE_LOD_BIAS      0x8501
#define GL_DEPTH_TEST            0x0B71
#define GL_DEPTH_BUFFER_BIT      0x00000100
#define GL_ELEMENT_ARRAY_BUFFER  0x8893
#define GL_STATIC_DRAW           0x88E4
#define GL_UNSIGNED_INT          0x1405
#define GL_RENDERBUFFER          0x8D41
#define GL_DEPTH_COMPONENT24     0x81A6
#define GL_DEPTH_ATTACHMENT      0x8D00
#define GL_DRAW_FRAMEBUFFER      0x8CA9
#define GL_FRAMEBUFFER           0x8D40
#define GL_READ_FRAMEBUFFER      0x8CA8
#define GL_COLOR_ATTACHMENT0     0x8CE0

#if defined(_WIN32)
#define GL_API __stdcall
#else
#define GL_API
#endif

#define GL_FUNCTIONS(X) \
    X(void,            Viewport,                GLint x, GLint y, GLsizei width, GLsizei height) \
    X(void,            ClearColor,              GLfloat r, GLfloat g, GLfloat b, GLfloat a) \
    X(void,            Clear,                   GLbitfield mask) \
    X(void,            ReadPixels,              GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels) \
    X(const GLubyte *, GetString,               GLenum name) \
    X(void,            Enable,                  GLenum cap)     X(void,            DepthMask,               GLboolean flag) \
    X(void,            BlendFuncSeparate,       GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) \
    X(void,            Scissor,                 GLint x, GLint y, GLsizei width, GLsizei height) \
    X(void,            GenTextures,             GLsizei n, GLuint *textures) \
    X(void,            DeleteTextures,          GLsizei n, const GLuint *textures) \
    X(void,            BindTexture,             GLenum target, GLuint texture) \
    X(void,            TexImage2D,              GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) \
    X(void,            TexSubImage2D,           GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) \
    X(void,            TexParameteri,           GLenum target, GLenum pname, GLint param) \
    X(void,            GenerateMipmap,          GLenum target) \
    X(void,            TexParameterf,           GLenum target, GLenum pname, GLfloat param) \
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
    X(void,            BufferSubData,           GLenum target, GLintptr offset, GLsizeiptr size, const void *data) \
    X(void,            VertexAttribPointer,     GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) \
    X(void,            VertexAttribDivisor,     GLuint index, GLuint divisor) \
    X(void,            EnableVertexAttribArray, GLuint index) \
    X(void,            DrawArraysInstanced,     GLenum mode, GLint first, GLsizei count, GLsizei instanceCount) \
    X(void,            CopyTexSubImage2D,       GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) \
    X(void,            GenFramebuffers,         GLsizei n, GLuint *framebuffers) \
    X(void,            BindFramebuffer,         GLenum target, GLuint framebuffer) \
    X(void,            FramebufferTexture2D,    GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) \
    X(void,            Uniform1i,               GLint location, GLint v0) \
    X(void,            Disable,                 GLenum cap) \
    X(void,            DrawElements,            GLenum mode, GLsizei count, GLenum type, const void *indices) \
    X(void,            DeleteBuffers,           GLsizei n, const GLuint *buffers) \
    X(void,            DeleteVertexArrays,      GLsizei n, const GLuint *arrays) \
    X(void,            GenRenderbuffers,        GLsizei n, GLuint *renderbuffers) \
    X(void,            BindRenderbuffer,        GLenum target, GLuint renderbuffer) \
    X(void,            DeleteRenderbuffers,     GLsizei n, const GLuint *renderbuffers) \
    X(void,            RenderbufferStorageMultisample, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) \
    X(void,            FramebufferRenderbuffer, GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) \
    X(void,            BlitFramebuffer,         GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) \
    X(void,            UniformMatrix4fv,        GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) \
    X(void,            Uniform4f,               GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)

#define X(ret, name, ...) typedef ret GL_API GL_##name##Fn(__VA_ARGS__);
GL_FUNCTIONS(X)
#undef X

typedef struct GL_Instance {
    R_Rect  rect;
    R_Rect  uv;
    R_Color color;
    f32     radius;
    f32     thickness;  // 0 fills; otherwise an inner outline this wide
    f32     textured;   // 1 multiplies alpha by the texture's red channel; 2 multiplies by its RGBA; 3 draws its premultiplied RGBA, as copied from the frame;
                        // 4 multiplies by its RGBA, which is premultiplied, as a model's drawing is
    f32     softness;   // 0 has a sharp edge; otherwise a fill fades in from it over this width
    f32     shape;      // R_Shape flags, and GL_ShapeLine: a line between the points in `uv`, `radius` from its middle to its edge
    R_Color corners[3]; // with GL_ShapeGradient, the top right, bottom left and bottom right corners' colors, `color` being the top left's
} GL_Instance;

#define GL_ShapeLine     32
#define GL_ShapeGradient 64

typedef struct GL_Batch {
    GLuint texture;     // 0 until a textured instance joins the batch
    R_Rect clip;
    u32    first;
    u32    count;
    // A blur of the frame drawn so far under `region` (whole pixels, top-down), `radius` across, laid back in through its two instances.
    b32    blur;
    i32    region[4];
    f32    radius;
    // 1 starts drawing into the layer texture, cleared under `region`; 2 lays it back into the frame through its one instance.
    u8     layer;
    // Copies the frame as it is for the blurs that read it; a blur with fromSource reads that copy rather than the frame.
    b32    source;
    b32    fromSource;
} GL_Batch;

typedef struct GL_ArrayInstance { ArrayHeader header; GL_Instance *v; } GL_ArrayInstance;
typedef struct GL_ArrayBatch    { ArrayHeader header; GL_Batch *v; } GL_ArrayBatch;

#define GL_MaxClipDepth 64
#define GL_BlurLevels 6

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
    // The scale in effect, p * scale + offset, and the ones it was pushed over.
    struct { f32 scale, x, y; } scales[GL_MaxClipDepth];
    u32              scaleDepth;
    f32              scale, offsetX, offsetY;
    i32              width;
    i32              height;
    // Blurs, by the dual Kawase filter: the frame under a box copied at its size, halved level by level and grown back level by level.
    GLuint           blurProgram;
    GLint            blurSourceLocation, blurModeLocation, blurScaleLocation, blurTexelLocation, blurLimitLocation;
    GLuint           blurVertexArray;
    GLuint           framebuffer;
    GLuint           blurLevels[GL_BlurLevels + 1];  // the copy, then each level about half the one before
    i32              blurWidth, blurHeight;          // the window's size they were made for, as the layer texture is
    GLuint           layerTexture;
    b32              inLayer;
    u32              shape;
    GLuint           sourceTexture;
    b32              inSource;
    // Meshes are drawn into a texture through a multisampled color and depth target, made at the largest size asked for so far.
    GLuint           meshProgram;
    GLint            meshViewLocation, meshScaleLocation, meshColorLocation, meshTexturedLocation, meshBlendLocation;
    b32              blending;  // a mesh's see-through parts are being drawn
    GLuint           meshFramebuffer, meshColor, meshDepth;
    i32              meshWidth, meshHeight;
} GL_State;

// A mesh's handle holds its vertex array and vertex buffer in its first word, and its index buffer and index count in its second. The
// vertex buffer holds all the positions, then all the texture coordinates, then all the colors.
typedef struct GL_Mesh {
    GLuint vertexArray, positions, indices;
    GLsizei count;
} GL_Mesh;

function GL_Mesh GL__MeshFromHandle(R_MeshHandle handle)
{
    return (GL_Mesh){ (GLuint)handle.u64[0], (GLuint)(handle.u64[0] >> 32), (GLuint)handle.u64[1], (GLsizei)(handle.u64[1] >> 32) };
}

// The mesh shader: vertices in a view space where X and Y from -1 to 1 fill the target's shorter side, upside down, as a texture's first
// row is its top; Z from -1 to 1, nearer larger. Each triangle is lit by its own normal, from the screen-space slopes of its view
// positions, turned toward the view, so that meshes wound either way look alike. Its color is the part's times the vertex's, times the
// part's picture when it has one.
global const char *GL_MeshVertexShader =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec2 aUv;\n"
    "layout(location = 2) in vec4 aColor;\n"
    "uniform mat4 uView;\n"
    "uniform vec4 uScale;\n"
    "out vec3 vView;\n"
    "out vec2 vUv;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    vView = (uView * vec4(aPosition, 1.0)).xyz;\n"
    "    vUv = aUv;\n"
    "    vColor = aColor;\n"
    "    gl_Position = vec4(vView.x * uScale.x, -vView.y * uScale.y, -vView.z * 0.5, 1.0);\n"
    "}\n";

global const char *GL_MeshFragmentShader =
    "#version 330 core\n"
    "in vec3 vView;\n"
    "in vec2 vUv;\n"
    "in vec4 vColor;\n"
    "uniform vec4 uColor;\n"
    "uniform sampler2D uImage;\n"
    "uniform int uTextured;\n"
    "uniform int uBlend;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec3 n = normalize(cross(dFdx(vView), dFdy(vView)));\n"
    "    if (n.z < 0.0) n = -n;\n"
    "    float light = max(dot(n, vec3(-0.398, 0.597, 0.697)), 0.0);\n"
    "    vec3 base = uColor.rgb * vColor.rgb;\n"
    "    float alpha = uBlend != 0 ? uColor.a * vColor.a : 1.0;\n"
    "    if (uTextured != 0) {\n"
    "        vec4 picture = texture(uImage, vUv);\n"
    "        if (uBlend == 0 && picture.a < 0.5) discard;\n"
    "        base *= picture.rgb;\n"
    "        if (uBlend != 0) alpha *= picture.a;\n"
    "    }\n"
    "    fragColor = vec4(base * (0.22 + 0.78 * light) * alpha, alpha);\n"
    "}\n";

global GL_State GL;

global const char GL_VertexShader[] =
    "#version 330 core\n"
    "layout(location = 0) in vec4 aRect;\n"
    "layout(location = 1) in vec4 aUV;\n"
    "layout(location = 2) in vec4 aColor;\n"
    "layout(location = 3) in vec4 aStyle;\n"
    "layout(location = 4) in float aShape;\n"
    "layout(location = 5) in vec4 aTopRight;\n"
    "layout(location = 6) in vec4 aBottomLeft;\n"
    "layout(location = 7) in vec4 aBottomRight;\n"
    "uniform vec2 uViewport;\n"
    "out vec2 vUV;\n"
    "out vec4 vColor;\n"
    "flat out vec4 vCorners[4];\n"
    "out vec2 vAcross;\n"
    "out vec2 vLocal;\n"
    "out vec2 vHalfSize;\n"
    "out vec4 vStyle;\n"
    "flat out int vShape;\n"
    "flat out vec4 vSegment;\n"
    "out vec2 vPosition;\n"
    "void main() {\n"
    "    vShape = int(aShape + 0.5);\n"
    "    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
    // A shape drawn from its distance reaches a pixel past its box, for an edge between pixels to shade the one it half covers; images, inverted
    // shapes, which cover all of their box, and empty boxes are not.
    "    float fringe = aStyle.z > 0.0 || (vShape & 16) != 0 || aRect.z <= aRect.x || aRect.w <= aRect.y ? 0.0 : 1.0;\n"
    "    vec2 position = mix(aRect.xy - fringe, aRect.zw + fringe, corner);\n"
    "    vHalfSize = 0.5 * (aRect.zw - aRect.xy);\n"
    "    vLocal = position - 0.5 * (aRect.xy + aRect.zw);\n"
    "    vUV = mix(aUV.xy, aUV.zw, corner);\n"
    "    vSegment = aUV;\n"
    "    vPosition = position;\n"
    "    vColor = aColor;\n"
    "    vCorners = vec4[4](aColor, aTopRight, aBottomLeft, aBottomRight);\n"
    "    vAcross = (position - aRect.xy) / max(aRect.zw - aRect.xy, vec2(1e-6));\n"
    "    vStyle = aStyle;\n"
    "    vec2 ndc = position / uViewport * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "}\n";

global const char GL_FragmentShader[] =
    "#version 330 core\n"
    "uniform sampler2D uTexture;\n"
    "in vec2 vUV;\n"
    "in vec4 vColor;\n"
    "flat in vec4 vCorners[4];\n"
    "in vec2 vAcross;\n"
    "in vec2 vLocal;\n"
    "in vec2 vHalfSize;\n"
    "in vec4 vStyle;\n"
    "flat in int vShape;\n"
    "flat in vec4 vSegment;\n"
    "in vec2 vPosition;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    // A gradient goes between its corners' colors by where the pixel is across the box.
    "    vec2 t = clamp(vAcross, 0.0, 1.0);\n"
    "    vec4 shade = (vShape & 64) == 0 ? vColor : mix(mix(vCorners[0], vCorners[1], t.x), mix(vCorners[2], vCorners[3], t.x), t.y);\n"
    // A line shades by its distance from the segment between its points, less its half width.
    "    if ((vShape & 32) != 0) {\n"
    "        vec2 along = vSegment.zw - vSegment.xy, from = vPosition - vSegment.xy;\n"
    "        float t = clamp(dot(from, along) / max(dot(along, along), 1e-6), 0.0, 1.0);\n"
    "        float edge = length(from - along * t) - vStyle.x;\n"
    "        fragColor = vec4(shade.rgb, shade.a * clamp(0.5 - edge, 0.0, 1.0));\n"
    "        return;\n"
    "    }\n"
    "    float radius = min(vStyle.x, min(vHalfSize.x, vHalfSize.y));\n"
    "    int corner = vLocal.x < 0.0 ? (vLocal.y < 0.0 ? 1 : 8) : (vLocal.y < 0.0 ? 2 : 4);\n"
    "    if ((vShape & corner) != 0) radius = 0.0;\n"
    "    vec2 q = abs(vLocal) - vHalfSize + radius;\n"
    "    float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;\n"
    "    if ((vShape & 16) != 0) distance = -distance;\n"
    "    float alpha = clamp(0.5 - distance, 0.0, 1.0);\n"
    "    if (vStyle.w > 0.0) alpha = clamp(-distance / vStyle.w, 0.0, 1.0);\n"
    "    if (vStyle.y > 0.0) {\n"
    "        alpha *= clamp(0.5 + distance + vStyle.y, 0.0, 1.0);\n"
    "    }\n"
    "    if (vStyle.z > 2.5 && vStyle.z < 3.5) {\n"
    "        fragColor = texture(uTexture, vUV) * (shade.a * alpha);\n"
    "        return;\n"
    "    }\n"
    "    vec4 color = shade;\n"
    "    if (vStyle.z > 3.5) {\n"
    "        vec4 picture = texture(uTexture, vUV);\n"
    "        color *= vec4(picture.rgb / max(picture.a, 0.004), picture.a);\n"
    "    } else if (vStyle.z > 1.5) {\n"
    "        color *= texture(uTexture, vUV);\n"
    "    } else if (vStyle.z > 0.0) {\n"
    "        alpha *= texture(uTexture, vUV).r;\n"
    "    }\n"
    "    fragColor = vec4(color.rgb, color.a * alpha);\n"
    "}\n";

// One triangle over the whole target, which is a level of the dual Kawase blur: mode 0 halves the source, mode 1 doubles it.
// `uScale` maps the target's pixels to the source's coordinates, `uTexel` is half a source texel or more, and reads stay below `uLimit`,
// the corner of the part of the source in use.
global const char GL_BlurVertexShader[] =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

global const char GL_BlurFragmentShader[] =
    "#version 330 core\n"
    "uniform sampler2D uSource;\n"
    "uniform int uMode;\n"
    "uniform vec2 uScale;\n"
    "uniform vec2 uTexel;\n"
    "uniform vec2 uLimit;\n"
    "out vec4 fragColor;\n"
    "vec4 Read(vec2 uv) { return texture(uSource, min(uv, uLimit)); }\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy * uScale, h = uTexel;\n"
    "    if (uMode == 0) {\n"
    "        fragColor = (Read(uv) * 4.0 + Read(uv - h) + Read(uv + h) + Read(uv + vec2(h.x, -h.y)) + Read(uv - vec2(h.x, -h.y))) / 8.0;\n"
    "    } else {\n"
    "        fragColor = (Read(uv + vec2(-2.0 * h.x, 0.0)) + Read(uv + vec2(-h.x, h.y)) * 2.0 + Read(uv + vec2(0.0, 2.0 * h.y)) + Read(uv + vec2(h.x, h.y)) * 2.0 +\n"
    "                     Read(uv + vec2(2.0 * h.x, 0.0)) + Read(uv + vec2(h.x, -h.y)) * 2.0 + Read(uv + vec2(0.0, -2.0 * h.y)) + Read(uv + vec2(-h.x, -h.y)) * 2.0) / 12.0;\n"
    "    }\n"
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

// A mark above the texture's name, which it never reaches.
#define GL__TexturePremultiplied (UINT64_C(1) << 40)

function R_TextureHandle R_TexturePremultiplied(R_TextureHandle texture)
{
    if (texture.u64[0]) texture.u64[0] |= GL__TexturePremultiplied;
    return texture;
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

    GLuint blurVertex = GL__CompileShader(GL_VERTEX_SHADER, GL_BlurVertexShader), blurFragment = GL__CompileShader(GL_FRAGMENT_SHADER, GL_BlurFragmentShader);
    if (blurVertex == 0 || blurFragment == 0) return false;
    GL.blurProgram = GL.CreateProgram();
    GL.AttachShader(GL.blurProgram, blurVertex);
    GL.AttachShader(GL.blurProgram, blurFragment);
    GL.LinkProgram(GL.blurProgram);
    GL.DeleteShader(blurVertex);
    GL.DeleteShader(blurFragment);
    GL.GetProgramiv(GL.blurProgram, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    GL.blurSourceLocation = GL.GetUniformLocation(GL.blurProgram, "uSource");
    GL.blurModeLocation = GL.GetUniformLocation(GL.blurProgram, "uMode");
    GL.blurScaleLocation = GL.GetUniformLocation(GL.blurProgram, "uScale");
    GL.blurTexelLocation = GL.GetUniformLocation(GL.blurProgram, "uTexel");
    GL.blurLimitLocation = GL.GetUniformLocation(GL.blurProgram, "uLimit");
    GL.GenVertexArrays(1, &GL.blurVertexArray);
    GL.GenFramebuffers(1, &GL.framebuffer);

    GLuint meshVertex = GL__CompileShader(GL_VERTEX_SHADER, GL_MeshVertexShader), meshFragment = GL__CompileShader(GL_FRAGMENT_SHADER, GL_MeshFragmentShader);
    if (meshVertex == 0 || meshFragment == 0) return false;
    GL.meshProgram = GL.CreateProgram();
    GL.AttachShader(GL.meshProgram, meshVertex);
    GL.AttachShader(GL.meshProgram, meshFragment);
    GL.LinkProgram(GL.meshProgram);
    GL.DeleteShader(meshVertex);
    GL.DeleteShader(meshFragment);
    GL.GetProgramiv(GL.meshProgram, GL_LINK_STATUS, &linked);
    if (!linked) return false;
    GL.meshViewLocation = GL.GetUniformLocation(GL.meshProgram, "uView");
    GL.meshScaleLocation = GL.GetUniformLocation(GL.meshProgram, "uScale");
    GL.meshColorLocation = GL.GetUniformLocation(GL.meshProgram, "uColor");
    GL.meshTexturedLocation = GL.GetUniformLocation(GL.meshProgram, "uTextured");
    GL.meshBlendLocation = GL.GetUniformLocation(GL.meshProgram, "uBlend");
    GL.GenFramebuffers(1, &GL.meshFramebuffer);

    GL.GenVertexArrays(1, &GL.vertexArray);
    GL.BindVertexArray(GL.vertexArray);
    GL.GenBuffers(1, &GL.instanceBuffer);
    GL.BindBuffer(GL_ARRAY_BUFFER, GL.instanceBuffer);
    for (GLuint i = 0; i < 8; ++i) {
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

function void R_TextureUpdateAlpha(R_TextureHandle texture, i32 x, i32 y, i32 width, i32 height, const u8 *pixels)
{
    GL.BindTexture(GL_TEXTURE_2D, GL__TextureRefFromHandle(texture));
    GL.TexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RED, GL_UNSIGNED_BYTE, pixels);
}

function void R_TextureRelease(R_TextureHandle texture)
{
    GLuint ref = GL__TextureRefFromHandle(texture);
    if (ref) GL.DeleteTextures(1, &ref);
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

function void R_TextureLinear(R_TextureHandle texture)
{
    GL.BindTexture(GL_TEXTURE_2D, GL__TextureRefFromHandle(texture));
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

function void R_TextureSmooth(R_TextureHandle texture)
{
    GL.BindTexture(GL_TEXTURE_2D, GL__TextureRefFromHandle(texture));
    GL.GenerateMipmap(GL_TEXTURE_2D);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // A level sharper than the size asks for: blending toward the next smaller level softens text, and a still picture shows no shimmer.
    GL.TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, -1.0f);
}

function R_MeshHandle R_MeshCreate(const f32 *positions, const f32 *uvs, const u32 *colors, u32 vertexCount, const u32 *indices, u64 indexCount)
{
    if (!vertexCount || !indexCount || indexCount > INT32_MAX) return (R_MeshHandle){0};
    GL_Mesh made = { .count = (GLsizei)indexCount }, *mesh = &made;
    GL.GenVertexArrays(1, &mesh->vertexArray);
    GL.BindVertexArray(mesh->vertexArray);
    GL.GenBuffers(1, &mesh->positions);
    GL.BindBuffer(GL_ARRAY_BUFFER, mesh->positions);
    GLsizeiptr n = (GLsizeiptr)vertexCount;
    GL.BufferData(GL_ARRAY_BUFFER, n * 24, NULL, GL_STATIC_DRAW);
    GL.BufferSubData(GL_ARRAY_BUFFER, 0, n * 12, positions);
    GL.BufferSubData(GL_ARRAY_BUFFER, n * 12, n * 8, uvs);
    GL.BufferSubData(GL_ARRAY_BUFFER, n * 20, n * 4, colors);
    for (GLuint i = 0; i < 3; ++i) GL.EnableVertexAttribArray(i);
    GL.VertexAttribPointer(0, 3, GL_FLOAT, 0, 12, NULL);
    GL.VertexAttribPointer(1, 2, GL_FLOAT, 0, 8, (const void *)(n * 12));
    GL.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, 4, (const void *)(n * 20));
    GL.GenBuffers(1, &mesh->indices);
    GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->indices);
    GL.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)indexCount * 4, indices, GL_STATIC_DRAW);
    // The 2D batches keep their own vertex array bound.
    GL.BindVertexArray(GL.vertexArray);
    GL.BindBuffer(GL_ARRAY_BUFFER, GL.instanceBuffer);
    return (R_MeshHandle){ { (u64)made.vertexArray | (u64)made.positions << 32, (u64)made.indices | (u64)(u32)made.count << 32 } };
}

function void R_MeshIndices(R_MeshHandle handle, u64 first, const u32 *indices, u64 count)
{
    GL_Mesh mesh = GL__MeshFromHandle(handle);
    if (!mesh.vertexArray || first + count > (u64)mesh.count || !count) return;
    GL.BindVertexArray(mesh.vertexArray);
    GL.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)(first * 4), (GLsizeiptr)(count * 4), indices);
    GL.BindVertexArray(GL.vertexArray);
}

function void R_MeshRelease(R_MeshHandle handle)
{
    GL_Mesh mesh = GL__MeshFromHandle(handle);
    if (!mesh.vertexArray) return;
    GL.DeleteBuffers(1, &mesh.positions);
    GL.DeleteBuffers(1, &mesh.indices);
    GL.DeleteVertexArrays(1, &mesh.vertexArray);
}

function void R_MeshDraw(R_MeshHandle handle, R_TextureHandle target, i32 width, i32 height, const f32 view[16], const R_MeshPart *parts, u32 partCount)
{
    GL_Mesh mesh = GL__MeshFromHandle(handle);
    if (!mesh.vertexArray || !target.u64[0] || width <= 0 || height <= 0) return;
    if (width > GL.meshWidth || height > GL.meshHeight) {
        GL.meshWidth = Max(GL.meshWidth, width), GL.meshHeight = Max(GL.meshHeight, height);
        if (GL.meshColor) GL.DeleteRenderbuffers(1, &GL.meshColor);
        if (GL.meshDepth) GL.DeleteRenderbuffers(1, &GL.meshDepth);
        GL.GenRenderbuffers(1, &GL.meshColor);
        GL.BindRenderbuffer(GL_RENDERBUFFER, GL.meshColor);
        GL.RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, GL.meshWidth, GL.meshHeight);
        GL.GenRenderbuffers(1, &GL.meshDepth);
        GL.BindRenderbuffer(GL_RENDERBUFFER, GL.meshDepth);
        GL.RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, GL.meshWidth, GL.meshHeight);
    }
    GL.BindFramebuffer(GL_FRAMEBUFFER, GL.meshFramebuffer);
    GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, GL.meshColor);
    GL.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, GL.meshDepth);
    GL.Viewport(0, 0, width, height);
    GL.Disable(GL_SCISSOR_TEST);
    GL.Disable(GL_BLEND);
    GL.Enable(GL_DEPTH_TEST);
    GL.ClearColor(0, 0, 0, 0);
    GL.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GL.UseProgram(GL.meshProgram);
    GL.UniformMatrix4fv(GL.meshViewLocation, 1, 0, view);
    f32 shorter = (f32)Min(width, height);
    GL.Uniform4f(GL.meshScaleLocation, shorter / (f32)width, shorter / (f32)height, 0, 0);
    GL.BindVertexArray(mesh.vertexArray);
    // The parts that hide what is behind them first, then the see-through ones over them, which leave the depth as it was, premultiplied
    // into what is drawn already; they are not sorted among themselves.
    for (u32 i = 0; i < partCount * 2; ++i) {
        const R_MeshPart *part = &parts[i % partCount];
        b32 blend = i >= partCount;
        if (part->first >= (u64)mesh.count || !part->count || (b32)(part->blend != 0) != blend) continue;
        if (blend && !GL.blending) {
            GL.Enable(GL_BLEND);
            GL.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            GL.DepthMask(0);
            GL.blending = true;
        }
        GLuint texture = GL__TextureRefFromHandle(part->texture);
        GL.Uniform4f(GL.meshColorLocation, part->color.r, part->color.g, part->color.b, part->color.a);
        GL.Uniform1i(GL.meshTexturedLocation, texture != 0);
        GL.Uniform1i(GL.meshBlendLocation, blend);
        // A model's picture repeats and is smoothed both ways, without the sharpening a still picture gets, which would shimmer as it turns.
        if (texture) {
            GL.BindTexture(GL_TEXTURE_2D, texture);
            GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            GL.TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, 0.0f);
        }
        GL.DrawElements(GL_TRIANGLES, (GLsizei)Min(part->count, (u64)mesh.count - part->first), GL_UNSIGNED_INT, (const void *)(part->first * 4));
    }
    if (GL.blending) {
        GL.DepthMask(1);
        GL.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        GL.blending = false;
    }
    // Resolved into the target texture through the blur's framebuffer, which is free between frames.
    GL.BindFramebuffer(GL_READ_FRAMEBUFFER, GL.meshFramebuffer);
    GL.BindFramebuffer(GL_DRAW_FRAMEBUFFER, GL.framebuffer);
    GL.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GL__TextureRefFromHandle(target), 0);
    GL.BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    GL.Disable(GL_DEPTH_TEST);
    GL.Enable(GL_BLEND);
    GL.Enable(GL_SCISSOR_TEST);
    GL.Viewport(0, 0, GL.width, GL.height);
    GL.UseProgram(GL.program);
    GL.BindVertexArray(GL.vertexArray);
}

function R_Rect GL__CurrentClip(void)
{
    if (GL.clipDepth > 0) {
        return GL.clips[GL.clipDepth - 1];
    }
    return (R_Rect){ 0.0f, 0.0f, (f32)GL.width, (f32)GL.height };
}

function GLuint GL__BlurTexture(i32 width, i32 height);

function void R_BeginFrame(i32 width, i32 height, R_Color clear)
{
    ArenaClear(&GL.frameArena);
    GL.instances = (GL_ArrayInstance){0};
    GL.batches = (GL_ArrayBatch){0};
    GL.clipDepth = 0;
    GL.scaleDepth = 0;
    GL.scale = 1;
    GL.offsetX = GL.offsetY = 0;
    GL.inSource = false;
    GL.width = width;
    GL.height = height;

    GL.Viewport(0, 0, width, height);
    GL.Scissor(0, 0, width, height);
    GL.ClearColor(clear.r * clear.a, clear.g * clear.a, clear.b * clear.a, clear.a);
    GL.Clear(GL_COLOR_BUFFER_BIT);
    // The blur levels follow the window's size.
    if (GL.blurWidth != width || GL.blurHeight != height) {
        if (GL.blurLevels[0]) GL.DeleteTextures(GL_BlurLevels + 1, GL.blurLevels);
        if (GL.layerTexture) GL.DeleteTextures(1, &GL.layerTexture);
        if (GL.sourceTexture) GL.DeleteTextures(1, &GL.sourceTexture);
        GL.layerTexture = GL__BlurTexture(width, height);
        GL.sourceTexture = GL__BlurTexture(width, height);
        i32 levelWidth = width, levelHeight = height;
        for (u32 level = 0; level <= GL_BlurLevels; ++level) {
            GL.blurLevels[level] = GL__BlurTexture(levelWidth, levelHeight);
            levelWidth = levelWidth / 2 + 1;
            levelHeight = levelHeight / 2 + 1;
        }
        GL.blurWidth = width;
        GL.blurHeight = height;
    }
}

function R_Rect GL__Scaled(R_Rect rect)
{
    return (R_Rect){ rect.x0 * GL.scale + GL.offsetX, rect.y0 * GL.scale + GL.offsetY, rect.x1 * GL.scale + GL.offsetX, rect.y1 * GL.scale + GL.offsetY };
}

function void GL__PushInstance(GL_Instance instance, GLuint texture)
{
    b32 line = ((u32)instance.shape & GL_ShapeLine) != 0;
    if (GL.scale != 1) {
        instance.rect = GL__Scaled(instance.rect);
        if (line) instance.uv = GL__Scaled(instance.uv);
        instance.radius *= GL.scale;
        instance.thickness *= GL.scale;
        instance.softness *= GL.scale;
    }
    instance.shape = (f32)(GL.shape | (line ? GL_ShapeLine : 0) | ((u32)instance.shape & GL_ShapeGradient));
    R_Rect clip = GL__CurrentClip();
    GL_Batch *batch = GL.batches.header.count > 0 ? &GL.batches.v[GL.batches.header.count - 1] : NULL;
    b32 sameClip = batch != NULL && MemCompare(&batch->clip, &clip, sizeof(clip)) == 0;
    b32 compatibleTexture = batch != NULL && (texture == 0 || batch->texture == 0 || batch->texture == texture);
    if (!sameClip || !compatibleTexture || batch->blur || batch->layer || batch->source) {
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

// A filtered texture of `width` by `height` whose pixels are left undefined, for the blur passes to draw into.
function GLuint GL__BlurTexture(i32 width, i32 height)
{
    GLuint texture = 0;
    GL.GenTextures(1, &texture);
    GL.BindTexture(GL_TEXTURE_2D, texture);
    GL.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

function void R_BlurBehind(R_Rect rect, f32 cornerRadius, f32 radius, f32 opacity)
{
    // A layer's texture is not the frame, which the blur reads.
    if (opacity <= 0 || radius <= 0 || GL.inLayer) return;
    if (GL.scale != 1) {
        rect = GL__Scaled(rect);
        cornerRadius *= GL.scale;
        radius *= GL.scale;
    }
    // What is drawn around the box blurs into its edge, so the copy reaches past it by the blur's width.
    R_Rect clip = GL__CurrentClip();
    f32 margin = ceilf(radius);
    i32 region[4] = {
        (i32)MaxF32(0, floorf(MaxF32(rect.x0, clip.x0) - margin)), (i32)MaxF32(0, floorf(MaxF32(rect.y0, clip.y0) - margin)),
        (i32)MinF32((f32)GL.width, ceilf(MinF32(rect.x1, clip.x1) + margin)), (i32)MinF32((f32)GL.height, ceilf(MinF32(rect.y1, clip.y1) + margin)),
    };
    if (region[2] <= region[0] || region[3] <= region[1]) return;
    // The blur ends in the first level, which holds the region at about half its size from its bottom-left corner, bottom row first, as the frame is.
    f32 width = (f32)(region[2] - region[0]), height = (f32)(region[3] - region[1]);
    f32 halfWidth = (f32)((region[2] - region[0] + 1) / 2), halfHeight = (f32)((region[3] - region[1] + 1) / 2);
    f32 levelWidth = (f32)(GL.width / 2 + 1), levelHeight = (f32)(GL.height / 2 + 1);
    R_Rect uv = {
        (rect.x0 - (f32)region[0]) / width * halfWidth / levelWidth, ((f32)region[3] - rect.y0) / height * halfHeight / levelHeight,
        (rect.x1 - (f32)region[0]) / width * halfWidth / levelWidth, ((f32)region[3] - rect.y1) / height * halfHeight / levelHeight,
    };
    GL_Batch *batch = ArrayAddClear(&GL.frameArena, GL.batches, 1);
    *batch = (GL_Batch){ .clip = clip, .first = (u32)GL.instances.header.count, .count = 2, .blur = true, .region = { region[0], region[1], region[2], region[3] }, .radius = radius,
                         .fromSource = GL.inSource };
    // First the frame under the box is taken away, then the blur is added in its place, both by the box's rounded coverage.
    ArrayPush(&GL.frameArena, GL.instances, ((GL_Instance){ .rect = rect, .color = { 0, 0, 0, opacity }, .radius = cornerRadius, .shape = (f32)GL.shape }));
    ArrayPush(&GL.frameArena, GL.instances, ((GL_Instance){ .rect = rect, .uv = uv, .color = { 1, 1, 1, opacity }, .radius = cornerRadius, .textured = 3.0f, .shape = (f32)GL.shape }));
}

// Draws one instance of the main program, `index` in the uploaded instances.
function void GL__DrawInstance(u32 index, GLuint texture)
{
    u64 base = index * sizeof(GL_Instance);
    GLsizei stride = (GLsizei)sizeof(GL_Instance);
    GL.VertexAttribPointer(0, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, rect)));
    GL.VertexAttribPointer(1, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, uv)));
    GL.VertexAttribPointer(2, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, color)));
    GL.VertexAttribPointer(3, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, radius)));
    GL.VertexAttribPointer(4, 1, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, shape)));
    for (GLuint i = 0; i < 3; ++i) GL.VertexAttribPointer(5 + i, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, corners) + i * sizeof(R_Color)));
    GL.BindTexture(GL_TEXTURE_2D, texture);
    GL.DrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
}

// The blur of `batch`: the frame under its region copied, halved down more levels the wider the blur and grown back to the first level,
// then laid back in through its two instances.
function void GL__Blur(const GL_Batch *batch)
{
    i32 sizes[GL_BlurLevels + 1][2], textures[GL_BlurLevels + 1][2];
    for (u32 level = 0; level <= GL_BlurLevels; ++level) {
        sizes[level][0] = level ? (sizes[level - 1][0] + 1) / 2 : batch->region[2] - batch->region[0];
        sizes[level][1] = level ? (sizes[level - 1][1] + 1) / 2 : batch->region[3] - batch->region[1];
        textures[level][0] = level ? textures[level - 1][0] / 2 + 1 : GL.width;
        textures[level][1] = level ? textures[level - 1][1] / 2 + 1 : GL.height;
    }
    // The blur widens with its radius all the way: its taps spread from one and a half to three texels apart, at the level whose texels keep
    // them so, as taps spread wider leave gaps between them that show.
    f32 spread = batch->radius * (2.0f / 3.0f);
    u32 levels = 1;
    while (spread >= 3 && levels < GL_BlurLevels) ++levels, spread *= 0.5f;
    spread = MaxF32(spread, 1);
    GL.Scissor(0, 0, GL.width, GL.height);
    if (batch->fromSource) {
        GL.BindFramebuffer(GL_READ_FRAMEBUFFER, GL.framebuffer);
        GL.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GL.sourceTexture, 0);
    }
    GL.BindTexture(GL_TEXTURE_2D, GL.blurLevels[0]);
    GL.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, batch->region[0], GL.height - batch->region[3], sizes[0][0], sizes[0][1]);
    if (batch->fromSource) GL.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    GL.BlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
    GL.UseProgram(GL.blurProgram);
    GL.BindVertexArray(GL.blurVertexArray);
    GL.Uniform1i(GL.blurSourceLocation, 0);
    GL.BindFramebuffer(GL_FRAMEBUFFER, GL.framebuffer);
    // Down to the smallest level, then back up to the first.
    for (u32 pass = 0; pass < 2 * levels - 1; ++pass) {
        b32 down = pass < levels;
        u32 from = down ? pass : 2 * levels - 1 - pass, to = down ? pass + 1 : from - 1;
        GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GL.blurLevels[to], 0);
        GL.Viewport(0, 0, sizes[to][0], sizes[to][1]);
        GL.Scissor(0, 0, sizes[to][0], sizes[to][1]);
        f32 sourceWidth = (f32)textures[from][0], sourceHeight = (f32)textures[from][1];
        GL.Uniform1i(GL.blurModeLocation, down ? 0 : 1);
        GL.Uniform2f(GL.blurScaleLocation, (f32)sizes[from][0] / (f32)sizes[to][0] / sourceWidth, (f32)sizes[from][1] / (f32)sizes[to][1] / sourceHeight);
        GL.Uniform2f(GL.blurTexelLocation, 0.5f * spread / sourceWidth, 0.5f * spread / sourceHeight);
        GL.Uniform2f(GL.blurLimitLocation, ((f32)sizes[from][0] - 0.5f) / sourceWidth, ((f32)sizes[from][1] - 0.5f) / sourceHeight);
        GL.BindTexture(GL_TEXTURE_2D, GL.blurLevels[from]);
        GL.DrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
    }
    GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
    GL.Viewport(0, 0, GL.width, GL.height);
    i32 cx0 = (i32)floorf(batch->clip.x0), cy0 = (i32)floorf(batch->clip.y0), cx1 = (i32)ceilf(batch->clip.x1), cy1 = (i32)ceilf(batch->clip.y1);
    GL.Scissor(cx0, GL.height - cy1, cx1 - cx0, cy1 - cy0);
    GL.UseProgram(GL.program);
    GL.BindVertexArray(GL.vertexArray);
    GL.BindBuffer(GL_ARRAY_BUFFER, GL.instanceBuffer);
    GL.BlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    GL__DrawInstance(batch->first, 0);
    GL.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
    GL__DrawInstance(batch->first + 1, GL.blurLevels[1]);
    GL.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

function void R_SetShape(u32 shape)
{
    GL.shape = shape;
}

function void R_DrawRect(R_Rect rect, R_Color color, f32 cornerRadius)
{
    GL__PushInstance((GL_Instance){ .rect = rect, .color = color, .radius = cornerRadius }, 0);
}

function void R_DrawLine(f32 x0, f32 y0, f32 x1, f32 y1, f32 thickness, R_Color color)
{
    f32 half = thickness * 0.5f;
    R_Rect box = { MinF32(x0, x1) - half, MinF32(y0, y1) - half, MaxF32(x0, x1) + half, MaxF32(y0, y1) + half };
    GL__PushInstance((GL_Instance){ .rect = box, .uv = { x0, y0, x1, y1 }, .color = color, .radius = half, .shape = GL_ShapeLine }, 0);
}

function void R_DrawSoftRect(R_Rect rect, R_Color color, f32 cornerRadius, f32 softness)
{
    GL__PushInstance((GL_Instance){ .rect = rect, .color = color, .radius = cornerRadius, .softness = softness }, 0);
}

function void R_DrawGradientRect(R_Rect rect, const R_Color corners[4], f32 cornerRadius)
{
    GL__PushInstance((GL_Instance){ .rect = rect, .color = corners[0], .corners = { corners[1], corners[2], corners[3] }, .radius = cornerRadius, .shape = GL_ShapeGradient }, 0);
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
    f32 kind = texture.u64[0] & GL__TexturePremultiplied ? 4.0f : 2.0f;
    GL__PushInstance((GL_Instance){ .rect = rect, .uv = uv, .color = tint, .textured = kind }, GL__TextureRefFromHandle(texture));
}

function void R_PushClip(R_Rect rect)
{
    Assert(GL.clipDepth < GL_MaxClipDepth);
    R_Rect outer = GL__CurrentClip();
    rect = GL__Scaled(rect);
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

function void R_PushScale(f32 x, f32 y, f32 scale)
{
    Assert(GL.scaleDepth < GL_MaxClipDepth);
    GL.scales[GL.scaleDepth++] = (typeof(GL.scales[0])){ GL.scale, GL.offsetX, GL.offsetY };
    // The new scale applies first, around (x, y), then the one already in effect.
    GL.offsetX += GL.scale * x * (1 - scale);
    GL.offsetY += GL.scale * y * (1 - scale);
    GL.scale *= scale;
}

function void R_PopScale(void)
{
    Assert(GL.scaleDepth > 0);
    --GL.scaleDepth;
    GL.scale = GL.scales[GL.scaleDepth].scale;
    GL.offsetX = GL.scales[GL.scaleDepth].x;
    GL.offsetY = GL.scales[GL.scaleDepth].y;
}

function void R_BeginBlurSource(void)
{
    // A layer's texture is not the frame.
    if (GL.inLayer || GL.inSource) return;
    GL_Batch *batch = ArrayAddClear(&GL.frameArena, GL.batches, 1);
    *batch = (GL_Batch){ .clip = GL__CurrentClip(), .first = (u32)GL.instances.header.count, .source = true };
    GL.inSource = true;
}

function void R_EndBlurSource(void)
{
    GL.inSource = false;
}

function void R_PushLayer(R_Rect rect)
{
    Assert(!GL.inLayer);
    rect = GL__Scaled(rect);
    R_Rect clip = GL__CurrentClip();
    i32 region[4] = {
        (i32)MaxF32(0, floorf(MaxF32(rect.x0, clip.x0))), (i32)MaxF32(0, floorf(MaxF32(rect.y0, clip.y0))),
        (i32)MinF32((f32)GL.width, ceilf(MinF32(rect.x1, clip.x1))), (i32)MinF32((f32)GL.height, ceilf(MinF32(rect.y1, clip.y1))),
    };
    region[2] = Max(region[2], region[0]);
    region[3] = Max(region[3], region[1]);
    GL_Batch *batch = ArrayAddClear(&GL.frameArena, GL.batches, 1);
    *batch = (GL_Batch){ .clip = clip, .first = (u32)GL.instances.header.count, .layer = 1, .region = { region[0], region[1], region[2], region[3] } };
    GL.inLayer = true;
}

function void R_PopLayer(f32 opacity)
{
    Assert(GL.inLayer);
    GL.inLayer = false;
    const GL_Batch *start = NULL;
    for (u64 i = GL.batches.header.count; i-- > 0 && !start;) {
        if (GL.batches.v[i].layer == 1) start = &GL.batches.v[i];
    }
    if (!start) return;
    R_Rect rect = { (f32)start->region[0], (f32)start->region[1], (f32)start->region[2], (f32)start->region[3] };
    f32 width = (f32)GL.width, height = (f32)GL.height;
    R_Rect uv = { rect.x0 / width, (height - rect.y0) / height, rect.x1 / width, (height - rect.y1) / height };
    R_Rect clip = start->clip;
    GL_Batch *batch = ArrayAddClear(&GL.frameArena, GL.batches, 1);
    *batch = (GL_Batch){ .clip = clip, .first = (u32)GL.instances.header.count, .count = 1, .layer = 2 };
    ArrayPush(&GL.frameArena, GL.instances, ((GL_Instance){ .rect = rect, .uv = uv, .color = { 1, 1, 1, opacity }, .textured = 3.0f }));
}

function void R_ReadPixels(u32 width, u32 height, void *bgra)
{
    GL.ReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
}

function void R_ReadRect(i32 x, i32 y, i32 width, i32 height, u32 *bgra)
{
    GL.ReadPixels(x, GL.height - y - height, width, height, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
    // OpenGL's rows run from the bottom.
    for (i32 top = 0, bottom = height - 1; top < bottom; ++top, --bottom) {
        u32 *a = bgra + (i64)top * width, *b = bgra + (i64)bottom * width;
        for (i32 i = 0; i < width; ++i) {
            u32 swap = a[i];
            a[i] = b[i];
            b[i] = swap;
        }
    }
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
        if (batch->blur) {
            GL__Blur(batch);
            continue;
        }
        if (batch->source) {
            GL.BindTexture(GL_TEXTURE_2D, GL.sourceTexture);
            GL.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, GL.width, GL.height);
            continue;
        }
        if (batch->layer == 1) {
            GL.BindFramebuffer(GL_FRAMEBUFFER, GL.framebuffer);
            GL.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GL.layerTexture, 0);
            GL.Scissor(batch->region[0], GL.height - batch->region[3], batch->region[2] - batch->region[0], batch->region[3] - batch->region[1]);
            GL.ClearColor(0, 0, 0, 0);
            GL.Clear(GL_COLOR_BUFFER_BIT);
            continue;
        }
        if (batch->layer == 2) {
            // The layer holds premultiplied colors, as drawing over clear leaves them.
            GL.BindFramebuffer(GL_FRAMEBUFFER, 0);
            i32 cx0 = (i32)floorf(batch->clip.x0), cy0 = (i32)floorf(batch->clip.y0), cx1 = (i32)ceilf(batch->clip.x1), cy1 = (i32)ceilf(batch->clip.y1);
            GL.Scissor(cx0, GL.height - cy1, cx1 - cx0, cy1 - cy0);
            GL.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            GL__DrawInstance(batch->first, GL.layerTexture);
            GL.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            continue;
        }
        // GL 3.3 has no base-instance draw, so the attributes are rebased instead.
        u64 base = batch->first * sizeof(GL_Instance);
        GLsizei stride = (GLsizei)sizeof(GL_Instance);
        GL.VertexAttribPointer(0, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, rect)));
        GL.VertexAttribPointer(1, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, uv)));
        GL.VertexAttribPointer(2, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, color)));
        GL.VertexAttribPointer(3, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, radius)));
        GL.VertexAttribPointer(4, 1, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, shape)));
        for (GLuint i = 0; i < 3; ++i) GL.VertexAttribPointer(5 + i, 4, GL_FLOAT, 0, stride, (const void *)(uintptr_t)(base + offsetof(GL_Instance, corners) + i * sizeof(R_Color)));

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
