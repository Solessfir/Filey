#pragma once

// C boundary of the C++ DirectWrite rasterizer, included from both languages.
// C gets the type aliases from Base.h; C++ cannot include it, so it gets the same ones here.

#if defined(__cplusplus)
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  i32;
typedef float    f32;
typedef i32      b32;
typedef wchar_t  wchar;
#endif

typedef struct WinRasterGlyph {
    u8 *pixels;
    i32 width;
    i32 height;
    f32 offsetX;
    f32 offsetY;
    f32 advance;
} WinRasterGlyph;

typedef struct WinRasterMetrics {
    f32 ascent;
    f32 descent;
    f32 lineGap;
} WinRasterMetrics;

// Returns NULL on failure; the memory need not be zeroed.
typedef void *WinRasterAllocFn(void *user, u64 size);

#if defined(__cplusplus)
extern "C"
#endif
// `data` (a font file in memory) takes precedence over `family`.
b32 WinRasterizeFont(const wchar *family, const u8 *data, u64 dataSize, f32 pixelSize, u32 firstCodepoint, u32 glyphCount,
                     WinRasterAllocFn *alloc, void *allocUser, WinRasterGlyph *glyphs, WinRasterMetrics *metrics);
