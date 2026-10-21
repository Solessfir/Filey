// Blizzard's BLP textures, as Warcraft III (BLP1) and World of Warcraft (BLP2) keep them, for the inspector and the icon views. A header
// with the size and where each of up to 16 mipmaps is, then the mipmaps: 256 colors and an index a pixel, with 0, 1, 4 or 8 bits of alpha
// after them; DXT1, DXT3 or DXT5 blocks; plain BGRA; or, in BLP1 only, JPEG with its header shared by every mipmap.

#define Prog_BlpMaxBytes Megabytes(64)

function b32 Prog_IsBlp(String path) { return path.length >= 4 && Prog__EqualFolded(StrFrom(path, path.length - 4), S(".blp")); }

function u32 Prog__BlpRead32(const u8 *at) { return (u32)at[0] | (u32)at[1] << 8 | (u32)at[2] << 16 | (u32)at[3] << 24; }

// A BGRA color, as BLP's palettes and plain pixels are, as RGBA with red in the lowest byte.
function u32 Prog__BlpSwap(u32 bgra) { return (bgra & 0xFF00FF00u) | (bgra >> 16 & 0xFF) | (bgra & 0xFF) << 16; }

// A 5:6:5 color as RGBA, opaque.
function u32 Prog__Blp565(u32 c)
{
    u32 r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return (r << 3 | r >> 2) | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2) << 16 | 0xFF000000u;
}

function u32 Prog__BlpMix(u32 a, u32 b, u32 wa, u32 wb, u32 total)
{
    u32 result = 0;
    for (u32 shift = 0; shift < 24; shift += 8) result |= ((((a >> shift) & 0xFF) * wa + ((b >> shift) & 0xFF) * wb + total / 2) / total) << shift;
    return result | 0xFF000000u;
}

// One 4 by 4 block of DXT1 (`kind` 1), DXT3 (3) or DXT5 (5) into `out`, `stride` pixels a row, clipped to `w` by `h`.
function void Prog__BlpBlock(const u8 *block, u32 kind, u32 *out, i32 stride, i32 w, i32 h)
{
    const u8 *colors = kind == 1 ? block : block + 8;
    u32 c0 = (u32)colors[0] | (u32)colors[1] << 8, c1 = (u32)colors[2] | (u32)colors[3] << 8;
    u32 palette[4] = { Prog__Blp565(c0), Prog__Blp565(c1) };
    // DXT1 with the first color not above the second has a third halfway and a clear fourth.
    if (kind != 1 || c0 > c1) palette[2] = Prog__BlpMix(palette[0], palette[1], 2, 1, 3), palette[3] = Prog__BlpMix(palette[0], palette[1], 1, 2, 3);
    else palette[2] = Prog__BlpMix(palette[0], palette[1], 1, 1, 2), palette[3] = 0;
    u8 alphas[8] = {0};
    u64 alphaBits = 0;
    if (kind == 5) {
        alphas[0] = block[0], alphas[1] = block[1];
        if (alphas[0] > alphas[1]) for (u32 i = 2; i < 8; ++i) alphas[i] = (u8)(((8 - i) * alphas[0] + (i - 1) * alphas[1] + 3) / 7);
        else {
            for (u32 i = 2; i < 6; ++i) alphas[i] = (u8)(((6 - i) * alphas[0] + (i - 1) * alphas[1] + 2) / 5);
            alphas[6] = 0, alphas[7] = 255;
        }
        for (u32 i = 0; i < 6; ++i) alphaBits |= (u64)block[2 + i] << (8 * i);
    }
    u32 indices = Prog__BlpRead32(colors + 4);
    for (i32 y = 0; y < Min(4, h); ++y) {
        for (i32 x = 0; x < Min(4, w); ++x) {
            u32 n = (u32)(y * 4 + x);
            u32 pixel = palette[(indices >> (2 * n)) & 3];
            if (kind == 3) {
                u32 a = (block[n / 2] >> (4 * (n & 1))) & 15;
                pixel = (pixel & 0x00FFFFFFu) | (a * 17) << 24;
            } else if (kind == 5) {
                pixel = (pixel & 0x00FFFFFFu) | (u32)alphas[(alphaBits >> (3 * n)) & 7] << 24;
            }
            out[(i64)y * stride + x] = pixel;
        }
    }
}

// The picture in the BLP file `data`, at its largest mipmap that fits `maxSide` (0 for any), and its full size.
function b32 Prog_DecodeBlp(Arena *arena, String data, i32 maxSide, P_Image *out, i32 *fullWidth, i32 *fullHeight)
{
    const u8 *d = (const u8 *)data.value;
    if (data.length < 156) return false;
    b32 two = MemCompare(d, "BLP2", 4) == 0, one = MemCompare(d, "BLP1", 4) == 0;
    if (!one && !two) return false;
    // BLP2: its type, then the compression, the alpha's depth and kind and whether it has mipmaps as a byte each. BLP1: the compression
    // and the alpha's depth as numbers.
    u32 compression = two ? d[8] : Prog__BlpRead32(d + 4), alphaDepth = two ? d[9] : Prog__BlpRead32(d + 8), alphaType = two ? d[10] : 0;
    u32 width = Prog__BlpRead32(d + 12), height = Prog__BlpRead32(d + 16);
    const u8 *offsets = d + (two ? 20 : 28), *sizes = offsets + 64;
    if (!width || !height || width > 16384 || height > 16384) return false;
    // The palette, for the paletted kinds, and the JPEG header BLP1's JPEG mipmaps share.
    u64 tables = two ? 148 : 156;
    b32 jpeg = one && compression == 0, paletted = (two && compression == 1) || (one && compression == 1);
    u32 level = 0;
    while (level + 1 < 16 && maxSide > 0 && Max(width >> level, height >> level) > (u32)maxSide && Prog__BlpRead32(offsets + (level + 1) * 4) &&
           Prog__BlpRead32(sizes + (level + 1) * 4)) ++level;
    u32 w = Max(width >> level, 1u), h = Max(height >> level, 1u);
    u64 at = Prog__BlpRead32(offsets + level * 4), size = Prog__BlpRead32(sizes + level * 4);
    if (!at || at > data.length || size > data.length - at) return false;
    const u8 *mip = d + at;
    P_Image image = {0};
    if (jpeg) {
        if (tables + 4 > data.length) return false;
        u64 headerSize = Prog__BlpRead32(d + tables);
        if (headerSize > data.length - tables - 4) return false;
        Scratch scratch = GetScratchEx(arena);
        u8 *whole = ArenaPushArrayNoZero(scratch.arena, headerSize + size, u8);
        MemCopy(whole, d + tables + 4, headerSize);
        MemCopy(whole + headerSize, mip, size);
        b32 decoded = P_LoadImageBytes(arena, StrMake((const char *)whole, headerSize + size), maxSide, &image);
        ReleaseScratch(scratch);
        if (!decoded) return false;
        // Its channels are stored blue first, and it has no alpha unless the header says so.
        for (u64 i = 0; i < (u64)image.width * (u64)image.height; ++i) {
            u32 p = Prog__BlpSwap(image.pixels[i]);
            image.pixels[i] = alphaDepth ? p : p | 0xFF000000u;
        }
    } else if (paletted) {
        if (tables + 1024 > data.length) return false;
        image = (P_Image){ ArenaPushArrayNoZero(arena, (u64)w * h, u32), (i32)w, (i32)h };
        const u8 *palette = d + tables;
        u64 pixels = (u64)w * h, alphaBytes = (pixels * alphaDepth + 7) / 8;
        if (size < pixels + (alphaDepth == 1 || alphaDepth == 4 || alphaDepth == 8 ? alphaBytes : 0)) return false;
        const u8 *alpha = mip + pixels;
        for (u64 i = 0; i < pixels; ++i) {
            u32 color = Prog__BlpSwap(Prog__BlpRead32(palette + 4 * (u64)mip[i])) & 0x00FFFFFFu, a = 255;
            if (alphaDepth == 1) a = (alpha[i / 8] >> (i % 8)) & 1 ? 255 : 0;
            else if (alphaDepth == 4) a = ((alpha[i / 2] >> (4 * (i & 1))) & 15) * 17;
            else if (alphaDepth == 8) a = alpha[i];
            image.pixels[i] = color | a << 24;
        }
    } else if (two && compression == 2) {
        // DXT1 without alpha or with one bit of it, DXT3 for 4 bits, DXT5 for the interpolated kind.
        u32 kind = alphaDepth <= 1 ? 1 : alphaType == 7 ? 5 : 3, blockBytes = kind == 1 ? 8 : 16;
        u64 across = (w + 3) / 4, down = (h + 3) / 4;
        if (size < across * down * blockBytes) return false;
        image = (P_Image){ ArenaPushArrayNoZero(arena, (u64)w * h, u32), (i32)w, (i32)h };
        for (u64 by = 0; by < down; ++by) {
            for (u64 bx = 0; bx < across; ++bx) {
                Prog__BlpBlock(mip + (by * across + bx) * blockBytes, kind, image.pixels + by * 4 * w + bx * 4, (i32)w, (i32)(w - bx * 4), (i32)(h - by * 4));
            }
        }
        // DXT1 without alpha has none, whatever its clear color says.
        if (kind == 1 && !alphaDepth) for (u64 i = 0; i < (u64)w * h; ++i) image.pixels[i] |= 0xFF000000u;
    } else if (two && compression == 3) {
        if (size < (u64)w * h * 4) return false;
        image = (P_Image){ ArenaPushArrayNoZero(arena, (u64)w * h, u32), (i32)w, (i32)h };
        for (u64 i = 0; i < (u64)w * h; ++i) image.pixels[i] = Prog__BlpSwap(Prog__BlpRead32(mip + i * 4));
    } else {
        return false;
    }
    *out = image;
    if (fullWidth) *fullWidth = (i32)width;
    if (fullHeight) *fullHeight = (i32)height;
    return true;
}

function b32 Prog_LoadBlp(Arena *arena, String path, i32 maxSide, P_Image *out, i32 *fullWidth, i32 *fullHeight)
{
    Scratch scratch = GetScratchEx(arena);
    String data = {0};
    b32 result = P_ReadFileStart(scratch.arena, path, Prog_BlpMaxBytes, &data) && Prog_DecodeBlp(arena, data, maxSide, out, fullWidth, fullHeight);
    ReleaseScratch(scratch);
    return result;
}
