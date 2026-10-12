// DirectWrite glyph rasterizer. DirectWrite only ships C++ COM headers, so this
// is the one C++ translation unit; it is built separately and linked into the
// C application through WinFontRasterizer.h.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwrite.h>
#include <string.h>

#include "Win/WinFontRasterizer.h"

template <typename T>
static void SafeRelease(T *&object)
{
    if (object != nullptr) {
        object->Release();
        object = nullptr;
    }
}

static i32 MinI32(i32 a, i32 b) { return a < b ? a : b; }
static i32 MaxI32(i32 a, i32 b) { return a > b ? a : b; }

extern "C" b32 WinRasterizeFont(const wchar *family, f32 pixelSize, u32 firstCodepoint, u32 glyphCount,
                                WinRasterAllocFn *alloc, void *allocUser, WinRasterGlyph *glyphs, WinRasterMetrics *metrics)
{
    IDWriteFactory *factory = nullptr;
    IDWriteFontCollection *collection = nullptr;
    IDWriteFontFamily *fontFamily = nullptr;
    IDWriteFont *font = nullptr;
    IDWriteFontFace *face = nullptr;
    IDWriteGdiInterop *interop = nullptr;
    IDWriteBitmapRenderTarget *target = nullptr;
    IDWriteRenderingParams *defaultParams = nullptr;
    IDWriteRenderingParams *params = nullptr;
    b32 ok = 0;

    do {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&factory))) ||
            FAILED(factory->GetSystemFontCollection(&collection, FALSE))) {
            break;
        }

        UINT32 familyIndex = 0;
        BOOL exists = FALSE;
        collection->FindFamilyName(family, &familyIndex, &exists);
        if (!exists) {
            collection->FindFamilyName(L"Segoe UI", &familyIndex, &exists);
            if (!exists) {
                familyIndex = 0;
            }
        }
        if (FAILED(collection->GetFontFamily(familyIndex, &fontFamily)) ||
            FAILED(fontFamily->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font)) ||
            FAILED(font->CreateFontFace(&face))) {
            break;
        }

        DWRITE_FONT_METRICS fontMetrics = {};
        face->GetMetrics(&fontMetrics);
        f32 scale = pixelSize / (f32)fontMetrics.designUnitsPerEm;
        metrics->ascent = (f32)fontMetrics.ascent * scale;
        metrics->descent = (f32)fontMetrics.descent * scale;
        metrics->lineGap = (f32)fontMetrics.lineGap * scale;

        // Glyphs are drawn one at a time around the target's center and cropped
        // to the black box DirectWrite reports.
        i32 size = (i32)(pixelSize * 3.0f) + 8;
        f32 origin = (f32)(size / 2);
        if (FAILED(factory->GetGdiInterop(&interop)) ||
            FAILED(interop->CreateBitmapRenderTarget(nullptr, (UINT32)size, (UINT32)size, &target)) ||
            FAILED(target->SetPixelsPerDip(1.0f)) ||
            FAILED(factory->CreateRenderingParams(&defaultParams))) {
            break;
        }
        // ClearType level 0 gives grayscale coverage, identical in every channel.
        if (FAILED(factory->CreateCustomRenderingParams(defaultParams->GetGamma(), defaultParams->GetEnhancedContrast(), 0.0f,
                                                        DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &params))) {
            break;
        }

        HDC dc = target->GetMemoryDC();
        DIBSECTION dib = {};
        if (GetObjectW(GetCurrentObject(dc, OBJ_BITMAP), sizeof(dib), &dib) != sizeof(dib)) {
            break;
        }
        // The interop bitmap is a top-down 32bpp DIB.
        u8 *bits = static_cast<u8 *>(dib.dsBm.bmBits);
        i32 stride = dib.dsBm.bmWidthBytes;
        memset(bits, 0, (u64)stride * (u64)size);

        ok = 1;
        for (u32 i = 0; i < glyphCount && ok; ++i) {
            UINT32 codepoint = firstCodepoint + i;
            UINT16 glyphIndex = 0;
            DWRITE_GLYPH_METRICS glyphMetrics = {};
            face->GetGlyphIndices(&codepoint, 1, &glyphIndex);
            face->GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics, FALSE);

            WinRasterGlyph *glyph = &glyphs[i];
            // Whole-pixel advances keep text on the pixel grid.
            glyph->advance = (f32)(i32)((f32)glyphMetrics.advanceWidth * scale + 0.5f);

            FLOAT advance = 0.0f;
            DWRITE_GLYPH_OFFSET offset = {};
            DWRITE_GLYPH_RUN run = {};
            run.fontFace = face;
            run.fontEmSize = pixelSize;
            run.glyphCount = 1;
            run.glyphIndices = &glyphIndex;
            run.glyphAdvances = &advance;
            run.glyphOffsets = &offset;

            RECT box = {};
            if (FAILED(target->DrawGlyphRun(origin, origin, DWRITE_MEASURING_MODE_NATURAL, &run, params, RGB(255, 255, 255), &box))) {
                ok = 0;
                break;
            }
            i32 left = MaxI32((i32)box.left, 0);
            i32 top = MaxI32((i32)box.top, 0);
            i32 right = MinI32((i32)box.right, size);
            i32 bottom = MinI32((i32)box.bottom, size);
            if (right <= left || bottom <= top) {
                continue;
            }

            glyph->width = right - left;
            glyph->height = bottom - top;
            glyph->offsetX = (f32)left - origin;
            glyph->offsetY = (f32)top - origin;
            glyph->pixels = static_cast<u8 *>(alloc(allocUser, (u64)glyph->width * (u64)glyph->height));
            if (glyph->pixels == nullptr) {
                ok = 0;
                break;
            }

            GdiFlush();
            for (i32 y = 0; y < glyph->height; ++y) {
                u8 *row = bits + (u64)(top + y) * (u64)stride + (u64)left * 4;
                u8 *dst = glyph->pixels + (u64)y * (u64)glyph->width;
                for (i32 x = 0; x < glyph->width; ++x) {
                    dst[x] = row[x * 4];
                }
                memset(row, 0, (u64)glyph->width * 4);
            }
        }
    } while (0);

    SafeRelease(params);
    SafeRelease(defaultParams);
    SafeRelease(target);
    SafeRelease(interop);
    SafeRelease(face);
    SafeRelease(font);
    SafeRelease(fontFamily);
    SafeRelease(collection);
    SafeRelease(factory);
    return ok;
}
