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
    R_TextureHandle page;  // the atlas page it is in
    f32 advance;
    b8 color;              // drawn in its own colors, from a page of RGBA, such as an emoji
} R_Glyph;

// The glyphs drawn so far and the face that draws the rest, in Renderer/Font.c.
typedef struct R_FontCache R_FontCache;

// {0} is an empty font that measures and draws nothing. Glyphs are drawn into the atlas as text first needs them.
typedef struct R_Font {
    f32 ascent;
    f32 descent;
    f32 lineHeight;
    f32 capShift;  // whole pixels text is drawn above where its line box puts it, so that its capitals center where icons do
    R_FontCache *cache;
} R_Font;

// Requires a current OpenGL context (PlatformWindowOpen).
function b32  R_Init(void);
function void R_BeginFrame(i32 width, i32 height, R_Color clear);
function void R_EndFrame(void);
// The drawn frame before it is presented, bottom row first, 4 bytes a pixel in blue, green, red, alpha order.
function void R_ReadPixels(u32 width, u32 height, void *bgra);
// The drawn frame's pixels in `width` by `height` from (`x`, `y`), measured from the top left, top row first, as premultiplied BGRA.
function void R_ReadRect(i32 x, i32 y, i32 width, i32 height, u32 *bgra);

// Single-channel coverage texture, sampled as alpha.
function R_TextureHandle R_TextureCreateAlpha(i32 width, i32 height, const u8 *pixels);
function void R_TextureUpdateAlpha(R_TextureHandle texture, i32 x, i32 y, i32 width, i32 height, const u8 *pixels);
// Color texture of straight-alpha RGBA8 pixels (red in the lowest byte), top row first.
// NULL pixels leave it undefined for later updates.
function R_TextureHandle R_TextureCreateRGBA(i32 width, i32 height, const u32 *pixels);
function void R_TextureUpdateRGBA(R_TextureHandle texture, i32 x, i32 y, i32 width, i32 height, const u32 *pixels);
// A picture drawn smaller than its size, as a page fitted to the inspector, averages its pixels from mipmaps made of it as it is now,
// rather than skipping most of them; drawn larger it keeps its pixels sharp. Called again after each update.
function void R_TextureSmooth(R_TextureHandle texture);
// A picture drawn a little larger or smaller than its size blends the pixels around each point rather than taking the nearest, without mipmaps, so that updates need nothing more.
function void R_TextureLinear(R_TextureHandle texture);
// A {0} handle is ignored.
function void R_TextureRelease(R_TextureHandle texture);
// The same texture, marked as holding colors premultiplied by their alpha, which R_DrawImage then draws as they are.
function R_TextureHandle R_TexturePremultiplied(R_TextureHandle texture);

// A triangle mesh on the GPU: `vertexCount` vertices, each with three floats of position, a texture coordinate of two, 0 to 1 from a picture's
// top left, which repeats past them, and a straight RGBA color, and `indexCount` indices, three a triangle, each below vertexCount.
typedef struct R_MeshHandle { u64 u64[2]; } R_MeshHandle;
function R_MeshHandle R_MeshCreate(const f32 *positions, const f32 *uvs, const u32 *colors, u32 vertexCount, const u32 *indices, u64 indexCount);
// A {0} handle is ignored.
function void R_MeshRelease(R_MeshHandle mesh);
// Writes `count` of `mesh`'s indices from the `first`, as a part's triangles put in another order.
function void R_MeshIndices(R_MeshHandle mesh, u64 first, const u32 *indices, u64 count);
// A run of a mesh's indices, `count` from `first`, drawn in `color` times its vertices' colors and, unless {0}, `texture`'s picture, whose
// pixels with less than half their alpha are left out; with `blend`, see-through by their alphas instead, after the parts without.
typedef struct R_MeshPart {
    u64 first, count;
    R_Color color;
    R_TextureHandle texture;
    b32 blend;
} R_MeshPart;
// Draws `mesh`'s parts into `target`, a color texture `width` by `height`, cleared to see-through first, flat shaded and lit from the
// upper left, its colors premultiplied by their alpha, as R_TexturePremultiplied marks it to be drawn. The column-major `view` takes its positions to a view space where X and Y from -1 to 1 fill the target's shorter side,
// Y up, and Z from -1 to 1 is nearer as it grows. It draws at once, outside the frame's batches, so call it before the frame ends.
function void R_MeshDraw(R_MeshHandle mesh, R_TextureHandle target, i32 width, i32 height, const f32 view[16], const R_MeshPart *parts, u32 partCount);

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
// A line from (x0, y0) to (x1, y1), `thickness` wide, with round ends.
function void R_DrawLine(f32 x0, f32 y0, f32 x1, f32 y1, f32 thickness, R_Color color);
function void R_DrawRectOutline(R_Rect rect, R_Color color, f32 cornerRadius, f32 thickness);
// A fill that fades from nothing at its edge to `color` `softness` pixels inside it, for shadows.
function void R_DrawSoftRect(R_Rect rect, R_Color color, f32 cornerRadius, f32 softness);
// A fill whose color goes between the colors of its corners, top left, top right, bottom left and bottom right.
function void R_DrawGradientRect(R_Rect rect, const R_Color corners[4], f32 cornerRadius);
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
// The same without drawing the common glyphs ahead, for text that shows a few letters of a font, such as its name.
function b32  R_FontCreateLazy(Arena *arena, String family, f32 pixelSize, R_Font *font);
// A font file in memory, such as the embedded icon font, with the codepoints from `firstCodepoint` to `lastCodepoint` drawn at once.
function b32  R_FontCreateFromMemory(Arena *arena, const u8 *data, u64 size, f32 pixelSize, u32 firstCodepoint, u32 lastCodepoint, R_Font *font);
// Closes the font's face and frees its atlas; the arena it was made in is the caller's.
function void R_FontRelease(R_Font *font);
function f32  R_TextWidth(const R_Font *font, String text);
function void R_DrawText(const R_Font *font, f32 x, f32 y, R_Color color, String text);
