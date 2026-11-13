// SVG drawings for both platforms through nanosvg, which each platform's icon files include after nanosvg itself.

// SVGs from this size up are not drawn, nor are those whose own size is not a sane number of pixels.
#define P__MostSvgBytes (UINT64_C(1) << 24)
#define P__MostSvgSide (1 << 20)

typedef b32 P__SvgDraw(Arena *arena, String data, i32 width, i32 height, P_Image *image);

// An SVG drawn to `side` on its longer side, by `draw` when given and it can, else by nanosvg, which leaves out clip paths, masks and filters;
// its own size in `sourceWidth` and `sourceHeight`, and false when it draws nothing.
function b32 P__DecodeSvg(Arena *arena, String data, i32 side, P__SvgDraw *draw, P_Image *image, i32 *sourceWidth, i32 *sourceHeight)
{
    if (!data.length || data.length >= P__MostSvgBytes || side <= 0) return false;
    Scratch scratch = GetScratchEx(arena);
    // nanosvg parses in place, so it reads a NUL-terminated copy, leaving the bytes for `draw`.
    char *text = ArenaPushArrayNoZero(scratch.arena, data.length + 1, char);
    MemCopy(text, data.value, data.length);
    text[data.length] = 0;
    NSVGimage *svg = nsvgParse(text, "px", 96.0f);
    b32 result = false;
    // Written so that NaN fails too.
    if (svg && svg->width > 0 && svg->height > 0 && svg->width <= P__MostSvgSide && svg->height <= P__MostSvgSide) {
        f32 scale = (f32)side / MaxF32(svg->width, svg->height);
        i32 width = Max(1, (i32)(svg->width * scale + 0.5f)), height = Max(1, (i32)(svg->height * scale + 0.5f));
        result = draw && draw(arena, data, width, height, image);
        NSVGrasterizer *rasterizer = result ? NULL : nsvgCreateRasterizer();
        if (rasterizer) {
            u32 *pixels = ArenaPushArray(arena, (u64)width * (u64)height, u32);
            nsvgRasterize(rasterizer, svg, 0, 0, scale, (u8 *)pixels, width, height, width * 4);
            nsvgDeleteRasterizer(rasterizer);
            for (u64 i = 0; i < (u64)width * (u64)height && !result; ++i) result = (pixels[i] >> 24) != 0;
            if (result) *image = (P_Image){ .pixels = pixels, .width = width, .height = height };
        }
        if (result) {
            *sourceWidth = Max(1, (i32)(svg->width + 0.5f));
            *sourceHeight = Max(1, (i32)(svg->height + 0.5f));
        }
    }
    if (svg) nsvgDelete(svg);
    ReleaseScratch(scratch);
    return result;
}
