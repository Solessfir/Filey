// OpenGL 3.3 core backend. Entry points are loaded at runtime through the
// platform, so no system GL headers are needed.

typedef u32 GLenum;
typedef u32 GLbitfield;
typedef i32 GLint;
typedef i32 GLsizei;
typedef f32 GLfloat;
typedef u8  GLubyte;

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_VERSION          0x1F02

#if defined(_WIN32)
#define GL_API __stdcall
#else
#define GL_API
#endif

#define GL_FUNCTIONS(X) \
    X(void,            Viewport,   GLint x, GLint y, GLsizei width, GLsizei height) \
    X(void,            ClearColor, GLfloat r, GLfloat g, GLfloat b, GLfloat a) \
    X(void,            Clear,      GLbitfield mask) \
    X(const GLubyte *, GetString,  GLenum name)

#define X(ret, name, ...) typedef ret GL_API GL_##name##Fn(__VA_ARGS__);
GL_FUNCTIONS(X)
#undef X

typedef struct GL_State {
#define X(ret, name, ...) GL_##name##Fn *name;
    GL_FUNCTIONS(X)
#undef X
} GL_State;

global GL_State GL;

function b32 R_Init(void)
{
#define X(ret, name, ...) \
    GL.name = (GL_##name##Fn *)PlatformGLGetProc("gl" #name); \
    if (GL.name == NULL) { \
        return false; \
    }
    GL_FUNCTIONS(X)
#undef X

#if defined(DEBUG)
    fprintf(stderr, "OpenGL %s\n", (const char *)GL.GetString(GL_VERSION));
#endif
    return true;
}

function void R_BeginFrame(i32 width, i32 height, R_Color clear)
{
    GL.Viewport(0, 0, width, height);
    GL.ClearColor(clear.r, clear.g, clear.b, clear.a);
    GL.Clear(GL_COLOR_BUFFER_BIT);
}

function void R_EndFrame(void)
{
}
