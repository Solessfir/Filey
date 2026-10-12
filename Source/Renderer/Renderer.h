#pragma once

// Backend-neutral renderer contract.

typedef struct R_Color {
    f32 r;
    f32 g;
    f32 b;
    f32 a;
} R_Color;

// Requires a current OpenGL context (PlatformWindowOpen).
function b32  R_Init(void);
function void R_BeginFrame(i32 width, i32 height, R_Color clear);
function void R_EndFrame(void);
