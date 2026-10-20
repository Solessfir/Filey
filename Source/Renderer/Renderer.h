#pragma once

// Backend-neutral renderer contract.
// Coordinates are physical pixels with the origin at the window's top-left.

typedef struct R_Color {
    f32 r;
    f32 g;
    f32 b;
    f32 a;
} R_Color;

typedef struct R_Rect {
    f32 x0;
    f32 y0;
    f32 x1;
    f32 y1;
} R_Rect;

// Opaque backend texture reference. {0} is no texture.
typedef struct R_TextureHandle {
    u64 u64[1];
} R_TextureHandle;

typedef struct R_Glyph {
    R_Rect box;    // relative to the pen position on the baseline
    R_Rect uv;
    f32 advance;
} R_Glyph;

typedef struct R_GlyphRange {
    u32 firstCodepoint;
    u32 glyphCount;
    R_Glyph *glyphs;
} R_GlyphRange;

// {0} is an empty font that measures and draws nothing.
typedef struct R_Font {
    R_TextureHandle atlas;
    f32 ascent;
    f32 descent;
    f32 lineHeight;
    u32 rangeCount;
    R_GlyphRange *ranges;
} R_Font;

// Requires a current OpenGL context (PlatformWindowOpen).
function b32  R_Init(void);
function void R_BeginFrame(i32 width, i32 height, R_Color clear);
function void R_EndFrame(void);
// The drawn frame before it is presented, bottom row first, 4 bytes a pixel in blue, green, red, alpha order.
function void R_ReadPixels(u32 width, u32 height, void *bgra);

// Single-channel coverage texture, sampled as alpha.
function R_TextureHandle R_TextureCreateAlpha(i32 width, i32 height, const u8 *pixels);
// Color texture of straight-alpha RGBA8 pixels (red in the lowest byte), top row first.
// NULL pixels leave it undefined for later updates.
function R_TextureHandle R_TextureCreateRGBA(i32 width, i32 height, const u32 *pixels);
function void R_TextureUpdateRGBA(R_TextureHandle texture, i32 x, i32 y, i32 width, i32 height, const u32 *pixels);
// A picture drawn smaller than its size, as a page fitted to the inspector, averages its pixels from mipmaps made of it as it is now,
// rather than skipping most of them; drawn larger it keeps its pixels sharp. Called again after each update.
function void R_TextureSmooth(R_TextureHandle texture);
// A {0} handle is ignored.
function void R_TextureRelease(R_TextureHandle texture);

// How the rounded boxes drawn after it are shaped, until it is set again: corners left square, or the box's inside and outside swapped,
// which with a radius of half its size leaves the box's corners outside its circle.
typedef enum R_Shape {
    R_Shape_SquareTopLeft     = 1 << 0,
    R_Shape_SquareTopRight    = 1 << 1,
    R_Shape_SquareBottomRight = 1 << 2,
    R_Shape_SquareBottomLeft  = 1 << 3,
    R_Shape_Inverted          = 1 << 4,
} R_Shape;
function void R_SetShape(u32 shape);

// Draw calls are only valid between R_BeginFrame and R_EndFrame and are submitted in order.
function void R_DrawRect(R_Rect rect, R_Color color, f32 cornerRadius);
function void R_DrawRectOutline(R_Rect rect, R_Color color, f32 cornerRadius, f32 thickness);
// A fill that fades from nothing at its edge to `color` `softness` pixels inside it, for shadows.
function void R_DrawSoftRect(R_Rect rect, R_Color color, f32 cornerRadius, f32 softness);
function void R_DrawTexturedRect(R_Rect rect, R_Rect uv, R_TextureHandle texture, R_Color color);
// Draws an RGBA texture multiplied by `tint`.
function void R_DrawImage(R_Rect rect, R_Rect uv, R_TextureHandle texture, R_Color tint);
// Blurs what has been drawn so far under `rect`, inside its rounded corners, about `radius` pixels across, faded in by `opacity`.
// What is drawn after it lies over the blur.
function void R_BlurBehind(R_Rect rect, f32 cornerRadius, f32 radius, f32 opacity);
// Between these, blurs read the frame as it was at the first, so that boxes drawn side by side blur what is behind them all alike.
function void R_BeginBlurSource(void);
function void R_EndBlurSource(void);
// Clips intersect with the enclosing clip.
function void R_PushClip(R_Rect rect);
function void R_PopClip(void);
// Everything drawn until the matching pop is scaled by `scale` around (x, y), clips included, as for a block that zooms in.
function void R_PushScale(f32 x, f32 y, f32 scale);
function void R_PopScale(void);
// Everything drawn until the matching pop goes into a layer, which is then laid over what is under `rect` at `opacity`,
// so that a fading group shows what is under it evenly instead of through each of its parts. Layers do not nest, and nothing blurs inside one.
function void R_PushLayer(R_Rect rect);
function void R_PopLayer(f32 opacity);

// Text (Renderer/Font.c). `y` is the top of the line box.
function b32  R_FontCreate(Arena *arena, String family, f32 pixelSize, R_Font *font);
// One codepoint range of a font file in memory, such as the embedded icon font.
function b32  R_FontCreateFromMemory(Arena *arena, const u8 *data, u64 size, f32 pixelSize, u32 firstCodepoint, u32 lastCodepoint, R_Font *font);
function f32  R_TextWidth(const R_Font *font, String text);
function void R_DrawText(const R_Font *font, f32 x, f32 y, R_Color color, String text);
