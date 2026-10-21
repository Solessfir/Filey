// DirectWrite glyph rasterizer.
// DirectWrite only ships C++ COM headers, so this is the one C++ translation unit; it is built separately and linked into the C application through WinFontRasterizer.h.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwrite_3.h>
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

// The few characters the system's fallback is asked about, one at a time, as DirectWrite reads text through this interface.
class WinOneCharacter final : public IDWriteTextAnalysisSource {
public:
    wchar text[2] = {};
    UINT32 length = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **object) override
    {
        if (id == __uuidof(IUnknown) || id == __uuidof(IDWriteTextAnalysisSource)) {
            *object = this;
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR **at, UINT32 *count) override
    {
        *at = position < length ? text + position : nullptr;
        *count = position < length ? length - position : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR **at, UINT32 *count) override
    {
        *at = position > 0 && position <= length ? text : nullptr;
        *count = position <= length ? position : 0;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32, UINT32 *count, const WCHAR **locale) override
    {
        *locale = L"";
        *count = length;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32, UINT32 *count, IDWriteNumberSubstitution **substitution) override
    {
        *substitution = nullptr;
        *count = length;
        return S_OK;
    }
};

#define WinMaxFallbacks 16

struct WinFontFace {
    IDWriteFactory5 *factory;
    IDWriteInMemoryFontFileLoader *loader;  // only for a font from memory, which then has no fallback
    b32 loaderRegistered;
    IDWriteFontCollection *collection;
    IDWriteFontFallback *fallback;
    IDWriteFontFace *face;
    wchar family[128];
    f32 pixelSize;
    IDWriteBitmapRenderTarget *target;
    IDWriteRenderingParams *params;
    u8 *bits;
    i32 stride, size;
    // The faces other fonts lent glyphs from, each with its size relative to this one's.
    IDWriteFontFace *fallbacks[WinMaxFallbacks];
    f32 fallbackScales[WinMaxFallbacks];
    u32 fallbackCount;
};

extern "C" void WinFontClose(WinFontFace *font)
{
    if (font == nullptr) return;
    for (u32 i = 0; i < font->fallbackCount; ++i) SafeRelease(font->fallbacks[i]);
    SafeRelease(font->params);
    SafeRelease(font->target);
    SafeRelease(font->face);
    SafeRelease(font->fallback);
    SafeRelease(font->collection);
    if (font->loaderRegistered) font->factory->UnregisterFontFileLoader(font->loader);
    SafeRelease(font->loader);
    SafeRelease(font->factory);
    HeapFree(GetProcessHeap(), 0, font);
}

extern "C" WinFontFace *WinFontOpen(const wchar *family, const u8 *data, u64 dataSize, f32 pixelSize, WinRasterMetrics *metrics)
{
    WinFontFace *font = static_cast<WinFontFace *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WinFontFace)));
    if (font == nullptr) return nullptr;
    font->pixelSize = pixelSize;
    IDWriteFontFile *file = nullptr;
    IDWriteFontFamily *fontFamily = nullptr;
    IDWriteFont *match = nullptr;
    IDWriteGdiInterop *interop = nullptr;
    IDWriteRenderingParams *defaultParams = nullptr;
    IDWriteFactory2 *factory2 = nullptr;
    b32 ok = 0;
    do {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), reinterpret_cast<IUnknown **>(&font->factory)))) break;
        if (data != nullptr) {
            // The loader stays registered while the face lives.
            if (FAILED(font->factory->CreateInMemoryFontFileLoader(&font->loader)) || FAILED(font->factory->RegisterFontFileLoader(font->loader))) break;
            font->loaderRegistered = 1;
            if (FAILED(font->loader->CreateInMemoryFontFileReference(font->factory, data, (UINT32)dataSize, nullptr, &file)) ||
                FAILED(font->factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &font->face))) break;
        } else {
            UINT32 familyIndex = 0;
            BOOL exists = FALSE;
            if (FAILED(font->factory->GetSystemFontCollection(&font->collection, FALSE))) break;
            font->collection->FindFamilyName(family, &familyIndex, &exists);
            lstrcpynW(font->family, exists ? family : L"Segoe UI", 128);
            if (!exists) {
                font->collection->FindFamilyName(L"Segoe UI", &familyIndex, &exists);
                if (!exists) familyIndex = 0;
            }
            if (FAILED(font->collection->GetFontFamily(familyIndex, &fontFamily)) ||
                FAILED(fontFamily->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &match)) ||
                FAILED(match->CreateFontFace(&font->face))) break;
            // Characters the font lacks come from the font the system picks for them, as text elsewhere on Windows does.
            if (SUCCEEDED(font->factory->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void **>(&factory2)))) factory2->GetSystemFontFallback(&font->fallback);
        }

        DWRITE_FONT_METRICS fontMetrics = {};
        font->face->GetMetrics(&fontMetrics);
        f32 scale = pixelSize / (f32)fontMetrics.designUnitsPerEm;
        metrics->ascent = (f32)fontMetrics.ascent * scale;
        metrics->descent = (f32)fontMetrics.descent * scale;
        metrics->lineGap = (f32)fontMetrics.lineGap * scale;

        // Glyphs are drawn one at a time around the target's center and cropped to the black box DirectWrite reports.
        font->size = (i32)(pixelSize * 3.0f) + 8;
        if (FAILED(font->factory->GetGdiInterop(&interop)) ||
            FAILED(interop->CreateBitmapRenderTarget(nullptr, (UINT32)font->size, (UINT32)font->size, &font->target)) ||
            FAILED(font->target->SetPixelsPerDip(1.0f)) ||
            FAILED(font->factory->CreateRenderingParams(&defaultParams))) break;
        // ClearType level 0 gives grayscale coverage, identical in every channel; GDI-natural outlines snap stems to the pixel grid, which
        // gives crisper text at the same advances.
        if (FAILED(font->factory->CreateCustomRenderingParams(defaultParams->GetGamma(), defaultParams->GetEnhancedContrast(), 0.0f,
                                                              DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_GDI_NATURAL, &font->params))) break;
        HDC dc = font->target->GetMemoryDC();
        DIBSECTION dib = {};
        if (GetObjectW(GetCurrentObject(dc, OBJ_BITMAP), sizeof(dib), &dib) != sizeof(dib)) break;
        // The interop bitmap is a top-down 32bpp DIB.
        font->bits = static_cast<u8 *>(dib.dsBm.bmBits);
        font->stride = dib.dsBm.bmWidthBytes;
        memset(font->bits, 0, (u64)font->stride * (u64)font->size);
        ok = 1;
    } while (0);
    SafeRelease(factory2);
    SafeRelease(defaultParams);
    SafeRelease(interop);
    SafeRelease(match);
    SafeRelease(fontFamily);
    SafeRelease(file);
    if (!ok) {
        WinFontClose(font);
        return nullptr;
    }
    return font;
}

// A color glyph's layers, each drawn as coverage and laid over the ones before in its color, cropped into `glyph` as straight RGBA.
static b32 WinDrawColorGlyph(WinFontFace *font, IDWriteColorGlyphRunEnumerator1 *layers, WinRasterAllocFn *alloc, void *allocUser, WinRasterGlyph *glyph)
{
    i32 size = font->size;
    // Premultiplied red, green, blue and alpha of every pixel of the target.
    f32 *sum = static_cast<f32 *>(alloc(allocUser, (u64)size * (u64)size * 4 * sizeof(f32)));
    if (sum == nullptr) return 0;
    memset(sum, 0, (u64)size * (u64)size * 4 * sizeof(f32));
    i32 left = size, top = size, right = 0, bottom = 0;
    BOOL more = FALSE;
    while (SUCCEEDED(layers->MoveNext(&more)) && more) {
        const DWRITE_COLOR_GLYPH_RUN1 *layer = nullptr;
        if (FAILED(layers->GetCurrentRun(&layer)) || layer == nullptr) continue;
        RECT box = {};
        if (FAILED(font->target->DrawGlyphRun(layer->baselineOriginX, layer->baselineOriginY, layer->measuringMode, &layer->glyphRun, font->params,
                                              RGB(255, 255, 255), &box))) continue;
        i32 x0 = MaxI32((i32)box.left, 0), y0 = MaxI32((i32)box.top, 0), x1 = MinI32((i32)box.right, size), y1 = MinI32((i32)box.bottom, size);
        if (x1 <= x0 || y1 <= y0) continue;
        left = MinI32(left, x0), top = MinI32(top, y0), right = MaxI32(right, x1), bottom = MaxI32(bottom, y1);
        // A layer without a color of its own takes the text's, which for an emoji is white.
        DWRITE_COLOR_F color = layer->runColor;
        if (layer->paletteIndex == 0xFFFF) color.r = color.g = color.b = color.a = 1;
        GdiFlush();
        for (i32 y = y0; y < y1; ++y) {
            u8 *row = font->bits + (u64)y * (u64)font->stride;
            for (i32 x = x0; x < x1; ++x) {
                f32 a = color.a * (f32)row[x * 4] / 255.0f, *at = sum + ((u64)y * (u64)size + (u64)x) * 4;
                at[0] = color.r * a + at[0] * (1 - a);
                at[1] = color.g * a + at[1] * (1 - a);
                at[2] = color.b * a + at[2] * (1 - a);
                at[3] = a + at[3] * (1 - a);
            }
            memset(row + (u64)x0 * 4, 0, (u64)(x1 - x0) * 4);
        }
    }
    if (right <= left || bottom <= top) return 1;
    f32 origin = (f32)(size / 2);
    glyph->width = right - left;
    glyph->height = bottom - top;
    glyph->offsetX = (f32)left - origin;
    glyph->offsetY = (f32)top - origin;
    glyph->color = 1;
    glyph->pixels = static_cast<u8 *>(alloc(allocUser, (u64)glyph->width * (u64)glyph->height * 4));
    if (glyph->pixels == nullptr) return 0;
    for (i32 y = 0; y < glyph->height; ++y) {
        for (i32 x = 0; x < glyph->width; ++x) {
            const f32 *at = sum + ((u64)(top + y) * (u64)size + (u64)(left + x)) * 4;
            u8 *out = glyph->pixels + ((u64)y * (u64)glyph->width + (u64)x) * 4;
            f32 a = at[3];
            for (u32 k = 0; k < 3; ++k) out[k] = a > 0 ? (u8)(MinI32((i32)(at[k] / a * 255.0f + 0.5f), 255)) : 0;
            out[3] = (u8)(a * 255.0f + 0.5f);
        }
    }
    return 1;
}

// The glyph `index` of `face` at `emSize`, drawn and cropped into `glyph`: in its own colors when the font has them for it.
static b32 WinDrawGlyph(WinFontFace *font, IDWriteFontFace *face, f32 emSize, UINT16 index, WinRasterAllocFn *alloc, void *allocUser, WinRasterGlyph *glyph)
{
    DWRITE_FONT_METRICS fontMetrics = {};
    DWRITE_GLYPH_METRICS glyphMetrics = {};
    face->GetMetrics(&fontMetrics);
    face->GetDesignGlyphMetrics(&index, 1, &glyphMetrics, FALSE);
    // Whole-pixel advances keep text on the pixel grid.
    *glyph = WinRasterGlyph{};
    glyph->advance = (f32)(i32)((f32)glyphMetrics.advanceWidth * emSize / (f32)fontMetrics.designUnitsPerEm + 0.5f);

    FLOAT advance = 0.0f;
    DWRITE_GLYPH_OFFSET offset = {};
    DWRITE_GLYPH_RUN run = {};
    run.fontFace = face;
    run.fontEmSize = emSize;
    run.glyphCount = 1;
    run.glyphIndices = &index;
    run.glyphAdvances = &advance;
    run.glyphOffsets = &offset;
    f32 origin = (f32)(font->size / 2);
    IDWriteColorGlyphRunEnumerator1 *layers = nullptr;
    if (SUCCEEDED(font->factory->TranslateColorGlyphRun(D2D1_POINT_2F{ origin, origin }, &run, nullptr, DWRITE_GLYPH_IMAGE_FORMATS_COLR,
                                                         DWRITE_MEASURING_MODE_NATURAL, nullptr, 0, &layers)) && layers != nullptr) {
        b32 drawn = WinDrawColorGlyph(font, layers, alloc, allocUser, glyph);
        layers->Release();
        return drawn;
    }
    RECT box = {};
    if (FAILED(font->target->DrawGlyphRun(origin, origin, DWRITE_MEASURING_MODE_NATURAL, &run, font->params, RGB(255, 255, 255), &box))) return 0;
    i32 left = MaxI32((i32)box.left, 0), top = MaxI32((i32)box.top, 0);
    i32 right = MinI32((i32)box.right, font->size), bottom = MinI32((i32)box.bottom, font->size);
    if (right <= left || bottom <= top) return 1;
    glyph->width = right - left;
    glyph->height = bottom - top;
    glyph->offsetX = (f32)left - origin;
    glyph->offsetY = (f32)top - origin;
    glyph->pixels = static_cast<u8 *>(alloc(allocUser, (u64)glyph->width * (u64)glyph->height));
    if (glyph->pixels == nullptr) return 0;
    GdiFlush();
    for (i32 y = 0; y < glyph->height; ++y) {
        u8 *row = font->bits + (u64)(top + y) * (u64)font->stride + (u64)left * 4;
        u8 *dst = glyph->pixels + (u64)y * (u64)glyph->width;
        for (i32 x = 0; x < glyph->width; ++x) dst[x] = row[x * 4];
        memset(row, 0, (u64)glyph->width * 4);
    }
    return 1;
}

extern "C" b32 WinFontGlyph(WinFontFace *font, u32 codepoint, WinRasterAllocFn *alloc, void *allocUser, WinRasterGlyph *glyph)
{
    UINT16 index = 0;
    font->face->GetGlyphIndices(&codepoint, 1, &index);
    if (index != 0 || font->fallback == nullptr) return index != 0 && WinDrawGlyph(font, font->face, font->pixelSize, index, alloc, allocUser, glyph);
    // The fonts already lent from first, then the one the system picks.
    for (u32 i = 0; i < font->fallbackCount; ++i) {
        font->fallbacks[i]->GetGlyphIndices(&codepoint, 1, &index);
        if (index != 0) return WinDrawGlyph(font, font->fallbacks[i], font->pixelSize * font->fallbackScales[i], index, alloc, allocUser, glyph);
    }
    WinOneCharacter text;
    if (codepoint >= 0x10000) {
        text.text[0] = (wchar)(0xD800 + ((codepoint - 0x10000) >> 10));
        text.text[1] = (wchar)(0xDC00 + ((codepoint - 0x10000) & 0x3FF));
        text.length = 2;
    } else {
        text.text[0] = (wchar)codepoint;
        text.length = 1;
    }
    UINT32 mapped = 0;
    IDWriteFont *match = nullptr;
    IDWriteFontFace *face = nullptr;
    FLOAT scale = 1.0f;
    b32 ok = 0;
    if (SUCCEEDED(font->fallback->MapCharacters(&text, 0, text.length, font->collection, font->family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, &mapped, &match, &scale)) &&
        match != nullptr && SUCCEEDED(match->CreateFontFace(&face))) {
        face->GetGlyphIndices(&codepoint, 1, &index);
        if (index != 0) {
            ok = WinDrawGlyph(font, face, font->pixelSize * scale, index, alloc, allocUser, glyph);
            if (font->fallbackCount < WinMaxFallbacks) {
                font->fallbacks[font->fallbackCount] = face;
                font->fallbackScales[font->fallbackCount++] = scale;
                face = nullptr;
            }
        }
    }
    SafeRelease(face);
    SafeRelease(match);
    return ok;
}

extern "C" void WinFontFamilies(WinFontFamilyFn *each, void *user)
{
    IDWriteFactory *factory = nullptr;
    IDWriteFontCollection *collection = nullptr;
    if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&factory))) &&
        SUCCEEDED(factory->GetSystemFontCollection(&collection, FALSE))) {
        UINT32 count = collection->GetFontFamilyCount();
        for (UINT32 i = 0; i < count; ++i) {
            IDWriteFontFamily *family = nullptr;
            IDWriteLocalizedStrings *names = nullptr;
            UINT32 index = 0, length = 0;
            BOOL english = FALSE;
            wchar name[256];
            if (SUCCEEDED(collection->GetFontFamily(i, &family)) && SUCCEEDED(family->GetFamilyNames(&names))) {
                if (FAILED(names->FindLocaleName(L"en-us", &index, &english)) || !english) index = 0;
                if (SUCCEEDED(names->GetStringLength(index, &length)) && length < 256 && SUCCEEDED(names->GetString(index, name, length + 1))) each(user, name, length);
            }
            SafeRelease(names);
            SafeRelease(family);
        }
    }
    SafeRelease(collection);
    SafeRelease(factory);
}
