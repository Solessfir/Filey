// BLP textures: a 2 by 2 BLP2 in each of its kinds, and a paletted BLP1, built here.

// A BLP header for `w` by `h` with one mipmap of `size` bytes at `at` (`at` past the header and any palette).
function u8 *TestBlpHeader(Arena *arena, b32 two, u32 compression, u32 alphaDepth, u32 alphaType, u32 w, u32 h, u32 at, u32 size)
{
    u8 *d = ArenaPushArray(arena, at + size, u8);
    MemCopy(d, two ? "BLP2" : "BLP1", 4);
    u32 fields[5] = { two ? 1u : compression, two ? 0u : alphaDepth, w, h, 0 };
    MemCopy(d + 4, fields, sizeof(fields));
    if (two) d[8] = (u8)compression, d[9] = (u8)alphaDepth, d[10] = (u8)alphaType, d[11] = 0;
    u8 *offsets = d + (two ? 20 : 28);
    MemCopy(offsets, &at, 4);
    MemCopy(offsets + 64, &size, 4);
    return d;
}

function void TestBlp(void)
{
    Arena arena = {0};
    P_Image image = {0};
    i32 w = 0, h = 0;
    // Paletted with 8 bits of alpha after the indices: blue, red, green and white, as BGRA in the palette.
    u8 *paletted = TestBlpHeader(&arena, true, 1, 8, 8, 2, 2, 148 + 1024, 8);
    u32 palette[4] = { 0x000000FFu, 0x00FF0000u, 0x0000FF00u, 0x00FFFFFFu };
    MemCopy(paletted + 148, palette, sizeof(palette));
    u8 pixels[8] = { 0, 1, 2, 3, 255, 128, 0, 17 };
    MemCopy(paletted + 148 + 1024, pixels, 8);
    Check(Prog_DecodeBlp(&arena, StrMake((const char *)paletted, 148 + 1024 + 8), 0, &image, &w, &h) && image.width == 2 && w == 2 && h == 2);
    Check(image.pixels[0] == 0xFFFF0000u && image.pixels[1] == 0x800000FFu && image.pixels[2] == 0x0000FF00u && image.pixels[3] == 0x11FFFFFFu);
    // DXT1 without alpha: the first color red, the second blue, the pixels each of the four, the third two thirds red. A block's rows are 4
    // pixels, so the second row of 2 by 2 is its pixels 4 and 5.
    u8 *dxt1 = TestBlpHeader(&arena, true, 2, 0, 0, 2, 2, 148 + 1024, 8);
    u8 block1[8] = { 0x00, 0xF8, 0x1F, 0x00, 0x04, 0x0E, 0, 0 };
    MemCopy(dxt1 + 148 + 1024, block1, 8);
    Check(Prog_DecodeBlp(&arena, StrMake((const char *)dxt1, 148 + 1024 + 8), 0, &image, NULL, NULL));
    Check(image.pixels[0] == 0xFF0000FFu && image.pixels[1] == 0xFFFF0000u && image.pixels[2] == 0xFF5500AAu && image.pixels[3] == 0xFFAA0055u);
    // DXT5: alpha from 255 to 0 in eight steps, its pixels 0, 1 and 4 at the first, the second and the third, the colors all white.
    u8 *dxt5 = TestBlpHeader(&arena, true, 2, 8, 7, 2, 2, 148 + 1024, 16);
    u8 block5[16] = { 255, 0, 0x08, 0x20, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0 };
    MemCopy(dxt5 + 148 + 1024, block5, 16);
    Check(Prog_DecodeBlp(&arena, StrMake((const char *)dxt5, 148 + 1024 + 16), 0, &image, NULL, NULL));
    Check(image.pixels[0] == 0xFFFFFFFFu && image.pixels[1] == 0x00FFFFFFu && image.pixels[2] == 0xDBFFFFFFu);
    // Plain BGRA.
    u8 *plain = TestBlpHeader(&arena, true, 3, 8, 0, 2, 2, 148 + 1024, 16);
    u32 bgra[4] = { 0x80FF0000u, 0xFF00FF00u, 0xFF0000FFu, 0 };
    MemCopy(plain + 148 + 1024, bgra, sizeof(bgra));
    Check(Prog_DecodeBlp(&arena, StrMake((const char *)plain, 148 + 1024 + 16), 0, &image, NULL, NULL));
    Check(image.pixels[0] == 0x800000FFu && image.pixels[1] == 0xFF00FF00u && image.pixels[2] == 0xFFFF0000u && image.pixels[3] == 0);
    // BLP1 paletted without alpha is opaque.
    u8 *one = TestBlpHeader(&arena, false, 1, 0, 0, 2, 2, 156 + 1024, 4);
    MemCopy(one + 156, palette, sizeof(palette));
    MemCopy(one + 156 + 1024, pixels, 4);
    Check(Prog_DecodeBlp(&arena, StrMake((const char *)one, 156 + 1024 + 4), 0, &image, NULL, NULL));
    Check(image.pixels[0] == 0xFFFF0000u && image.pixels[3] == 0xFFFFFFFFu);
    // A file cut short, and one that is not a BLP, read as nothing.
    Check(!Prog_DecodeBlp(&arena, StrMake((const char *)dxt5, 148 + 1024 + 8), 0, &image, NULL, NULL) && !Prog_DecodeBlp(&arena, S("BLP3 not a texture"), 0, &image, NULL, NULL));
    Check(Prog_IsBlp(S("C:/Textures/Knob.BLP")) && !Prog_IsBlp(S("/a/b.png")));
    ArenaRelease(&arena);
}
