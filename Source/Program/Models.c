#include <math.h>

// Model previews: small readers for the common 3D formats, which gather a file's triangles into one mesh placed as the file places its
// parts, and a software rasterizer that draws the mesh from a fixed view in front of it, above and to its right. Both run in the preview job.

// FBX compresses its arrays with zlib, whose decoder comes with the stb_image each platform builds.

// Compressed .blend files are zstd frames.
#pragma push_macro("function")
#pragma push_macro("global")
#undef function
#undef global
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define ZDEC_NO_MESSAGE
#include "Lib/zstd/zstd_decompress.c"
#undef ERROR
#undef MIN
#undef MAX
#pragma clang diagnostic pop
#pragma pop_macro("global")
#pragma pop_macro("function")
#include <setjmp.h>

// ponytail: a file is read whole, so larger ones get no preview; reading the formats in pieces lifts it if that ever matters.
#define Prog_ModelMaxBytes Megabytes(256)

// Pictures a model's materials use that are read at most, and the side of the square they are shrunk to fit for the inspector and for
// thumbnails.
#define Prog_ModelMaxImages 16
#define Prog_ModelImageSide 1024
#define Prog_ModelThumbnailImageSide 256
// What a model without colors of its own is drawn in, as straight RGBA.
#define Prog_ModelGray 0xFFD4CCC8u

typedef struct Prog_ArrayF32 { ArrayHeader header; f32 *v; } Prog_ArrayF32;
typedef struct Prog_ArrayMeshPart { ArrayHeader header; Prog_MeshPart *v; } Prog_ArrayMeshPart;
typedef struct Prog_ArrayImage { ArrayHeader header; P_Image *v; } Prog_ArrayImage;
typedef struct Prog_ArrayString { ArrayHeader header; String *v; } Prog_ArrayString;

// A picture a reader found, decoded on a lane of its own while the reader goes on, into an arena of its own.
typedef struct Prog__Picture {
    String name, bytes;
    Arena arena;
    P_Image image;
} Prog__Picture;

typedef struct Prog__Pictures {
    P_Mutex mutex;
    P_Condition wake;
    Prog__Picture list[Prog_ModelMaxImages];
    u32 count, next;
    b32 closed;  // the reader is done, so no more come
} Prog__Pictures;

typedef struct Prog_Mesh {
    Prog_ArrayF32 positions;  // x, y and z of each vertex
    Prog_ArrayU32 indices;    // the three vertices of each triangle; one out of range, as a broken file can give, leaves its triangle out
    // Once a vertex has them, each vertex's texture coordinate, from a picture's top left, and straight RGBA color; the vertices before
    // are padded to 0 and Prog_ModelUnset, and so are those after when the shape is made.
    Prog_ArrayF32 uvs;
    Prog_ArrayU32 colors;
    Prog_ArrayMeshPart parts;  // none draws every triangle in the default color
    Prog_ArrayImage images;    // the parts' pictures, with the name each was read by
    Prog_ArrayString imageNames;
    String path;               // the file, next to which the pictures it names are looked for
    i32 imageSide;             // the pictures are read to fit this square; 0 reads none
    Prog__Pictures *pictures;  // where they are queued to decode alongside the reading, or NULL to decode them as they come
    b32 zUp;                   // Z points up rather than Y
} Prog_Mesh;

// A vertex color left unset: the default gray in a part without a color or picture of its own, else white.
#define Prog_ModelUnset 0x00FFFFFFu

// The colors of every vertex so far, padding the new ones unset, for the reader to set those it has.
function u32 *Prog__MeshColors(Arena *arena, Prog_Mesh *mesh)
{
    u64 vertices = mesh->positions.header.count / 3, have = mesh->colors.header.count;
    if (vertices > have) {
        u32 *added = ArrayAdd(arena, mesh->colors, vertices - have);
        for (u64 i = 0; i < vertices - have; ++i) added[i] = Prog_ModelUnset;
    }
    return mesh->colors.v;
}

// The texture coordinates of every vertex so far, padding the new ones with 0, for the reader to set those it has.
function f32 *Prog__MeshUvs(Arena *arena, Prog_Mesh *mesh)
{
    u64 vertices = mesh->positions.header.count / 3, have = mesh->uvs.header.count / 2;
    if (vertices > have) MemZero(ArrayAdd(arena, mesh->uvs, (vertices - have) * 2), (vertices - have) * 8);
    return mesh->uvs.v;
}

// Starts a part at the triangles added next, in `color` (0 for the default) and picture `image`, or in `bare` when that does not decode
// (0 for `color`); one started where no triangle followed is replaced.
function void Prog__MeshPart(Arena *arena, Prog_Mesh *mesh, u32 color, u32 image, u32 bare)
{
    u64 first = mesh->indices.header.count;
    if (mesh->parts.header.count && mesh->parts.v[mesh->parts.header.count - 1].first == first) --mesh->parts.header.count;
    ArrayPush(arena, mesh->parts, ((Prog_MeshPart){ .first = first, .color = color, .image = image, .bare = bare }));
}

// Starts a part in the look `look`, see-through as it is.
function void Prog__MeshLook(Arena *arena, Prog_Mesh *mesh, const Prog_MeshPart *look)
{
    Prog__MeshPart(arena, mesh, look->color, look->image, look->bare);
    mesh->parts.v[mesh->parts.header.count - 1].blend = look->blend;
}

// A look's color made see-through by `alpha`, from 0, clear, to 1, solid, which leaves it as it was.
function void Prog__MeshAlpha(Prog_MeshPart *look, f32 alpha)
{
    if (!(alpha < 0.999f)) return;
    u32 a = (u32)((alpha <= 0 ? 0 : alpha) * 255 + 0.5f) << 24;
    look->color = look->color ? (look->color & 0x00FFFFFFu) | a : 0x00FFFFFFu | a;
    if (look->bare) look->bare = (look->bare & 0x00FFFFFFu) | a;
    look->blend = true;
}

// Linear 0 to 1 as an sRGB byte, as the colors are kept.
function u32 Prog__SrgbByte(f32 linear)
{
    f32 c = linear <= 0 ? 0 : linear >= 1 ? 1 : linear;
    c = c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1 / 2.4f) - 0.055f;
    return (u32)(c * 255 + 0.5f);
}

// Straight RGBA from linear red, green and blue and an alpha, as glTF's, Blender's and FBX's colors are.
function u32 Prog__LinearColor(f32 r, f32 g, f32 b, f32 a)
{
    u32 alpha = (u32)((a <= 0 ? 0 : a >= 1 ? 1 : a) * 255 + 0.5f);
    return Prog__SrgbByte(r) | Prog__SrgbByte(g) << 8 | Prog__SrgbByte(b) << 16 | alpha << 24;
}

// The picture file at `path`, a game's texture or one the system reads.
function b32 Prog__LoadPicture(Arena *arena, String path, i32 side, P_Image *image)
{
    i32 width = 0, height = 0;
    return Prog_IsBlp(path) ? Prog_LoadBlp(arena, path, side, image, NULL, NULL) : P_LoadImageFile(arena, path, side, image, &width, &height);
}

// The picture `bytes` hold, or else the file `name` names, absolute or next to the model at `model`, or by its own name there when that
// is missing, to fit `side` squared.
function b32 Prog__DecodeImage(Arena *arena, String model, i32 side, String name, String bytes, P_Image *out)
{
    P_Image image = {0};
    b32 read = bytes.length && (Prog_DecodeBlp(arena, bytes, side, &image, NULL, NULL) || P_LoadImageBytes(arena, bytes, side, &image));
    if (!read && name.length) {
        // Written with either slash, relative to the model or not.
        Scratch scratch = GetScratchEx(arena);
        u8 *slashed = ArenaPushArrayNoZero(scratch.arena, name.length + 1, u8);
        for (u64 i = 0; i < name.length; ++i) slashed[i] = name.value[i] == '\\' ? '/' : (u8)name.value[i];
        String file = StrMake((const char *)slashed, name.length);
        // Blender writes a path next to its file as "//name".
        if (StrIsEqual(StrFromTo(file, 0, 2), S("//"))) file = StrFrom(file, 2);
        b32 absolute = (file.length && file.value[0] == '/') || (file.length > 1 && file.value[1] == ':');
        String parent = Prog_ParentPath(model);
        if (!absolute) read = Prog__LoadPicture(arena, Prog_JoinPath(scratch.arena, parent, file), side, &image);
        if (!read && absolute) read = Prog__LoadPicture(arena, file, side, &image);
        Index slash = StrIndexChar(file, '/', SSF_SearchBackwards);
        if (!read && slash.exists) read = Prog__LoadPicture(arena, Prog_JoinPath(scratch.arena, parent, StrFrom(file, slash.value + 1)), side, &image);
        ReleaseScratch(scratch);
    }
    if (read) *out = image;
    return read;
}

// The picture for the name `name`, from `bytes` or the file it names (see Prog__DecodeImage); UINT32_MAX when there is none or the mesh
// reads no pictures. A queued one is known to decode only once the model is read; those that do not are left out then.
function u32 Prog__MeshImage(Arena *arena, Prog_Mesh *mesh, String name, String bytes)
{
    if (!mesh->imageSide || (!name.length && !bytes.length)) return UINT32_MAX;
    for (u64 i = 0; name.length && i < mesh->imageNames.header.count; ++i) if (StrIsEqual(mesh->imageNames.v[i], name)) return (u32)i;
    if (mesh->images.header.count >= Prog_ModelMaxImages) return UINT32_MAX;
    P_Image image = {0};
    Prog__Pictures *pictures = mesh->pictures;
    if (!pictures && !Prog__DecodeImage(arena, mesh->path, mesh->imageSide, name, bytes, &image)) return UINT32_MAX;
    name = StrCopy(arena, name);
    ArrayPush(arena, mesh->images, image);
    ArrayPush(arena, mesh->imageNames, name);
    if (pictures) {
        P_MutexLock(&pictures->mutex);
        pictures->list[pictures->count++] = (Prog__Picture){ .name = name, .bytes = bytes };
        P_ConditionBroadcast(&pictures->wake);
        P_MutexUnlock(&pictures->mutex);
    }
    return (u32)(mesh->images.header.count - 1);
}

// A row-major 3 by 4 matrix: x' = m[0] x + m[1] y + m[2] z + m[3], and so on.
typedef struct Prog__Affine { f32 m[12]; } Prog__Affine;

global const Prog__Affine Prog__AffineIdentity = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 } };

function Prog__Affine Prog__AffineMultiply(Prog__Affine a, Prog__Affine b)
{
    Prog__Affine result = {0};
    for (u32 row = 0; row < 3; ++row) {
        const f32 *r = a.m + row * 4;
        for (u32 column = 0; column < 4; ++column) {
            result.m[row * 4 + column] = r[0] * b.m[column] + r[1] * b.m[4 + column] + r[2] * b.m[8 + column] + (column == 3 ? r[3] : 0);
        }
    }
    return result;
}

// Translation `t`, then rotation by the unit quaternion `q` (x, y, z, w), then scale `s`, as applied to a point read right to left.
function Prog__Affine Prog__AffineCompose(const f32 t[3], const f32 q[4], const f32 s[3])
{
    f32 x = q[0], y = q[1], z = q[2], w = q[3];
    Prog__Affine result = { {
        (1 - 2 * (y * y + z * z)) * s[0], 2 * (x * y - z * w) * s[1],       2 * (x * z + y * w) * s[2],       t[0],
        2 * (x * y + z * w) * s[0],       (1 - 2 * (x * x + z * z)) * s[1], 2 * (y * z - x * w) * s[2],       t[1],
        2 * (x * z - y * w) * s[0],       2 * (y * z + x * w) * s[1],       (1 - 2 * (x * x + y * y)) * s[2], t[2],
    } };
    return result;
}

// Euler angles in degrees, turned about X first, then Y, then Z, as a quaternion.
function void Prog__EulerQuaternion(const f32 degrees[3], f32 q[4])
{
    f32 half = 3.14159265f / 360.0f;
    f32 cx = cosf(degrees[0] * half), sx = sinf(degrees[0] * half), cy = cosf(degrees[1] * half), sy = sinf(degrees[1] * half);
    f32 cz = cosf(degrees[2] * half), sz = sinf(degrees[2] * half);
    q[0] = sx * cy * cz - cx * sy * sz;
    q[1] = cx * sy * cz + sx * cy * sz;
    q[2] = cx * cy * sz - sx * sy * cz;
    q[3] = cx * cy * cz + sx * sy * sz;
}

function void Prog__MeshPoint(f32 *out, const Prog__Affine *place, f32 x, f32 y, f32 z)
{
    const f32 *m = place->m;
    out[0] = m[0] * x + m[1] * y + m[2] * z + m[3];
    out[1] = m[4] * x + m[5] * y + m[6] * z + m[7];
    out[2] = m[8] * x + m[9] * y + m[10] * z + m[11];
}

function void Prog__MeshTriangle(Arena *arena, Prog_Mesh *mesh, u32 a, u32 b, u32 c)
{
    u32 *triangle = ArrayAdd(arena, mesh->indices, 3);
    triangle[0] = a;
    triangle[1] = b;
    triangle[2] = c;
}

function b32 Prog__IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// The decimal number at *at after any spaces, as "-1.5e3" or "2", moving *at past it; *at stays when there is none. Not exact to the last
// bit, which a preview never shows.
function b32 Prog__Number(const char **at, const char *end, f64 *value)
{
    local_persist const f64 powers[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
    const char *p = *at;
    while (p < end && Prog__IsSpace(*p)) ++p;
    b32 negative = p < end && *p == '-';
    if (p < end && (*p == '-' || *p == '+')) ++p;
    u64 mantissa = 0;
    i32 exponent = 0, digits = 0;
    for (; p < end && (u8)(*p - '0') < 10; ++p, ++digits) {
        if (mantissa < UINT64_C(100000000000000000)) mantissa = mantissa * 10 + (u64)(*p - '0');
        else ++exponent;
    }
    if (p < end && *p == '.') {
        for (++p; p < end && (u8)(*p - '0') < 10; ++p, ++digits) {
            if (mantissa < UINT64_C(100000000000000000)) {
                mantissa = mantissa * 10 + (u64)(*p - '0');
                --exponent;
            }
        }
    }
    if (!digits) return false;
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        b32 minus = q < end && *q == '-';
        if (q < end && (*q == '-' || *q == '+')) ++q;
        i32 e = 0;
        const char *first = q;
        for (; q < end && (u8)(*q - '0') < 10; ++q) if (e < 1000) e = e * 10 + (*q - '0');
        if (q > first) {
            exponent += minus ? -e : e;
            p = q;
        }
    }
    f64 result = (f64)mantissa;
    for (; exponent > 22; exponent -= 22) result *= 1e22;
    for (; exponent < -22; exponent += 22) result /= 1e22;
    result = exponent < 0 ? result / powers[-exponent] : result * powers[exponent];
    *value = negative ? -result : result;
    *at = p;
    return true;
}

// -----------------------------------------------------------------------------
// OBJ: "v x y z" lines, with an "r g b" color after them as some programs write, "vt u v" texture coordinates and "f a b c ..." polygons,
// whose corners may carry texture and normal indices after slashes and count from the end when negative. Each corner's vertex and
// texture coordinate together make one vertex of the mesh. "usemtl" picks a material of the .mtl files "mtllib" names, whose "Kd" color
// or "map_Kd" picture its faces are drawn in.
// -----------------------------------------------------------------------------

// A corner of a face by its point and the other values it was given, which together make one vertex of the mesh.
typedef struct Prog__MeshCorner { u32 point, a, b, c; } Prog__MeshCorner;

// The vertices made so far, found again by their corner: each point's chain of the corners made from it, as a point has only a few that
// differ, which is much faster than hashing every corner.
typedef struct Prog__Corners {
    u32 *first;             // each point's last made, UINT32_MAX for none
    u32 *next, *vertex;     // each made one's next in its point's chain and its vertex
    Prog__MeshCorner *keys;
    u64 count, capacity;
} Prog__Corners;

function Prog__Corners Prog__CornersMake(Arena *arena, u64 points, u64 corners)
{
    Prog__Corners result = { ArenaPushArrayNoZero(arena, Max(points, (u64)1), u32), ArenaPushArrayNoZero(arena, Max(corners, (u64)1), u32),
                             ArenaPushArrayNoZero(arena, Max(corners, (u64)1), u32), ArenaPushArrayNoZero(arena, Max(corners, (u64)1), Prog__MeshCorner), 0, corners };
    for (u64 i = 0; i < points; ++i) result.first[i] = UINT32_MAX;
    return result;
}

// The vertex made for `key`, whose point is in range, or UINT32_MAX when there is none yet.
function u32 Prog__CornersFind(const Prog__Corners *corners, Prog__MeshCorner key)
{
    for (u32 at = corners->first[key.point]; at != UINT32_MAX; at = corners->next[at]) {
        if (MemCompare(&corners->keys[at], &key, sizeof(key)) == 0) return corners->vertex[at];
    }
    return UINT32_MAX;
}

function void Prog__CornersAdd(Prog__Corners *corners, Prog__MeshCorner key, u32 vertex)
{
    if (corners->count >= corners->capacity) return;
    u64 at = corners->count++;
    corners->keys[at] = key;
    corners->vertex[at] = vertex;
    corners->next[at] = corners->first[key.point];
    corners->first[key.point] = (u32)at;
}

typedef struct Prog__ObjMaterial { String name; u32 color, image; f32 alpha; } Prog__ObjMaterial;
typedef struct Prog__ArrayObjMaterial { ArrayHeader header; Prog__ObjMaterial *v; } Prog__ArrayObjMaterial;

// The next word on the line at *at, spaces and tabs between words.
function String Prog__Word(const char **at, const char *end)
{
    const char *p = *at;
    while (p < end && Prog__IsSpace(*p)) ++p;
    const char *start = p;
    while (p < end && !Prog__IsSpace(*p) && *p != '\n') ++p;
    *at = p;
    return StrMake(start, (u64)(p - start));
}

// The rest of the line at *at, without its trailing spaces.
function String Prog__LineRest(const char **at, const char *end)
{
    const char *p = *at;
    while (p < end && Prog__IsSpace(*p)) ++p;
    const char *start = p;
    while (p < end && *p != '\n') ++p;
    *at = p;
    const char *last = p;
    while (last > start && Prog__IsSpace(last[-1])) --last;
    return StrMake(start, (u64)(last - start));
}

// The materials of the .mtl file `name`, next to the model.
function void Prog__ReadMtl(Arena *arena, Prog_Mesh *mesh, String name, Prog__ArrayObjMaterial *materials)
{
    String data = {0};
    if (!P_ReadFileStart(arena, Prog_JoinPath(arena, Prog_ParentPath(mesh->path), name), Megabytes(16), &data)) return;
    const char *p = data.value, *end = data.value + data.length;
    Prog__ObjMaterial *material = NULL;
    while (p < end) {
        String word = Prog__Word(&p, end);
        if (StrIsEqual(word, S("newmtl"))) {
            ArrayPush(arena, *materials, ((Prog__ObjMaterial){ .name = Prog__LineRest(&p, end), .image = UINT32_MAX, .alpha = 1 }));
            material = &materials->v[materials->header.count - 1];
        } else if (material && StrIsEqual(word, S("Kd"))) {
            f64 rgb[3] = { 1, 1, 1 };
            for (u32 k = 0; k < 3; ++k) Prog__Number(&p, end, &rgb[k]);
            material->color = Prog__LinearColor((f32)rgb[0], (f32)rgb[1], (f32)rgb[2], 1);
        } else if (material && (StrIsEqual(word, S("d")) || StrIsEqual(word, S("Tr")))) {
            f64 value = 1;
            if (Prog__Number(&p, end, &value)) material->alpha = (f32)(word.length == 1 ? value : 1 - value);
        } else if (material && StrIsEqual(word, S("map_Kd"))) {
            // Options such as "-s 1 1 1" come before the file, which is the rest of the line after them.
            String rest = Prog__LineRest(&p, end);
            while (rest.length && rest.value[0] == '-') {
                const char *q = rest.value, *stop = rest.value + rest.length;
                String option = Prog__Word(&q, stop);
                f64 value;
                for (u32 k = 0; k < 3 && !StrIsEqual(option, S("-clamp")) && !StrIsEqual(option, S("-blendu")) && !StrIsEqual(option, S("-blendv")) && Prog__Number(&q, stop, &value); ++k) {}
                if (StrIsEqual(option, S("-clamp")) || StrIsEqual(option, S("-blendu")) || StrIsEqual(option, S("-blendv")) || StrIsEqual(option, S("-imfchan"))) Prog__Word(&q, stop);
                while (q < stop && Prog__IsSpace(*q)) ++q;
                rest = StrMake(q, (u64)(stop - q));
            }
            material->image = Prog__MeshImage(arena, mesh, rest, (String){0});
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
}

// The index at *at as written, counting from 1, or back from the end when negative; false when there is none.
function b32 Prog__ObjIndex(const char **at, const char *end, i64 *index)
{
    const char *p = *at;
    b32 negative = p < end && *p == '-';
    if (negative) ++p;
    const char *digits = p;
    i64 value = 0;
    for (; p < end && (u8)(*p - '0') < 10; ++p) if (value < (INT64_C(1) << 40)) value = value * 10 + (*p - '0');
    *at = p;
    *index = negative ? -value : value;
    return p != digits;
}

// A written index among the `count` so far, from 0; -1 when it is out of range.
function i64 Prog__ObjResolve(i64 written, i64 count)
{
    i64 result = written < 0 ? count + written : written - 1;
    return written != 0 && result >= 0 && result < count ? result : -1;
}

// A face as one lane read it: its corners' written point and texture indices, and how many points and texture coordinates its lane had
// read before it, which the indices that count back from the end count from.
typedef struct Prog__ObjFace { u64 firstCorner; u32 cornerCount, points, textures; } Prog__ObjFace;
typedef struct Prog__ObjCorner { i64 point, texture; b32 textured; } Prog__ObjCorner;

// A line that changes the material, the object or the material files, in the order the faces come in.
typedef enum Prog__ObjEventKind { Prog__ObjMtllib, Prog__ObjObject, Prog__ObjUsemtl } Prog__ObjEventKind;
typedef struct Prog__ObjEvent { Prog__ObjEventKind kind; String rest; u64 face; } Prog__ObjEvent;

typedef struct Prog__ObjChunk {
    const char *from, *to;
    Arena arena;
    Prog_ArrayF32 points, texcoords;
    struct { ArrayHeader header; u64 *v; } colorPoints;  // the points with colors, in the chunk's count, and their colors
    Prog_ArrayU32 colors;
    struct { ArrayHeader header; Prog__ObjFace *v; } faces;
    struct { ArrayHeader header; Prog__ObjCorner *v; } corners;
    struct { ArrayHeader header; Prog__ObjEvent *v; } events;
    b32 cancelled;
} Prog__ObjChunk;

typedef struct Prog__ObjWork {
    Prog__ObjChunk *chunks;
    const P_CancelToken *cancel;
} Prog__ObjWork;

// A lane's lines, read without what the lines before them hold.
function void Prog__ObjLane(void *user, u32 lane)
{
    Prog__ObjWork *work = user;
    Prog__ObjChunk *chunk = &work->chunks[lane];
    Arena *arena = &chunk->arena;
    const char *p = chunk->from, *end = chunk->to;
    for (u64 line = 1; p < end; ++line) {
        if ((line & 0xFFFF) == 0 && P_IsCancelled(work->cancel)) {
            chunk->cancelled = true;
            return;
        }
        while (p < end && Prog__IsSpace(*p)) ++p;
        b32 spaced = end - p > 1 && (p[1] == ' ' || p[1] == '\t');
        if (spaced && p[0] == 'v') {
            p += 2;
            f64 xyz[3] = {0}, rgb[3] = {0};
            for (u32 i = 0; i < 3; ++i) Prog__Number(&p, end, &xyz[i]);
            f32 *v = ArrayAdd(arena, chunk->points, 3);
            v[0] = (f32)xyz[0];
            v[1] = (f32)xyz[1];
            v[2] = (f32)xyz[2];
            if (Prog__Number(&p, end, &rgb[0]) && Prog__Number(&p, end, &rgb[1]) && Prog__Number(&p, end, &rgb[2])) {
                u32 color = 0xFF000000u;
                for (u32 k = 0; k < 3; ++k) color |= (u32)(MinF32(MaxF32((f32)rgb[k], 0), 1) * 255 + 0.5f) << (k * 8);
                ArrayPush(arena, chunk->colorPoints, chunk->points.header.count / 3 - 1);
                ArrayPush(arena, chunk->colors, color);
            }
        } else if (end - p > 2 && p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
            p += 3;
            f64 uv[2] = {0};
            for (u32 i = 0; i < 2; ++i) Prog__Number(&p, end, &uv[i]);
            f32 *t = ArrayAdd(arena, chunk->texcoords, 2);
            t[0] = (f32)uv[0];
            t[1] = 1 - (f32)uv[1];
        } else if (spaced && p[0] == 'f') {
            p += 2;
            Prog__ObjFace face = { chunk->corners.header.count, 0, (u32)(chunk->points.header.count / 3), (u32)(chunk->texcoords.header.count / 2) };
            for (;;) {
                while (p < end && Prog__IsSpace(*p)) ++p;
                Prog__ObjCorner corner = {0};
                if (!Prog__ObjIndex(&p, end, &corner.point)) break;
                if (p < end && *p == '/') ++p, corner.textured = Prog__ObjIndex(&p, end, &corner.texture);
                while (p < end && !Prog__IsSpace(*p) && *p != '\n') ++p;
                ArrayPush(arena, chunk->corners, corner);
                ++face.cornerCount;
            }
            ArrayPush(arena, chunk->faces, face);
        } else if (end - p > 6 && MemCompare(p, "mtllib", 6) == 0 && Prog__IsSpace(p[6])) {
            p += 6;
            ArrayPush(arena, chunk->events, ((Prog__ObjEvent){ Prog__ObjMtllib, Prog__LineRest(&p, end), chunk->faces.header.count }));
        } else if (spaced && p[0] == 'o') {
            ArrayPush(arena, chunk->events, ((Prog__ObjEvent){ Prog__ObjObject, {0}, chunk->faces.header.count }));
        } else if (end - p > 6 && MemCompare(p, "usemtl", 6) == 0 && Prog__IsSpace(p[6])) {
            p += 6;
            ArrayPush(arena, chunk->events, ((Prog__ObjEvent){ Prog__ObjUsemtl, Prog__LineRest(&p, end), chunk->faces.header.count }));
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
}

// The file is cut at line ends into a chunk for each lane, whose numbers are read side by side; the faces are then made in order.
function b32 Prog__ReadObj(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    Scratch scratch = GetScratchEx(arena);
    u32 lanes = (u32)Min((u64)Max(1u, Min(P_ProcessorCount(), 8u)), Max(data.length / Megabytes(1), (u64)1));
    Prog__ObjChunk *chunks = ArenaPushArray(scratch.arena, lanes, Prog__ObjChunk);
    const char *at = data.value, *end = data.value + data.length;
    for (u32 i = 0; i < lanes; ++i) {
        const char *to = i + 1 == lanes ? end : data.value + data.length * (i + 1) / lanes;
        if (to < at) to = at;
        while (to < end && to[-1] != '\n') ++to;
        chunks[i].from = at, chunks[i].to = to;
        at = to;
    }
    Prog__ObjWork work = { chunks, cancel };
    P_RunLanes(lanes, Prog__ObjLane, &work);
    b32 cancelled = P_IsCancelled(cancel);
    u64 pointCount = 0, textureCount = 0, cornerCount = 0, colorCount = 0;
    for (u32 i = 0; i < lanes; ++i) {
        cancelled |= chunks[i].cancelled;
        pointCount += chunks[i].points.header.count / 3, textureCount += chunks[i].texcoords.header.count / 2;
        cornerCount += chunks[i].corners.header.count, colorCount += chunks[i].colors.header.count;
    }
    if (!cancelled && pointCount < UINT32_MAX && textureCount < UINT32_MAX) {
        // Every chunk's points, texture coordinates and colors in one place.
        f32 *points = ArenaPushArrayNoZero(scratch.arena, Max(pointCount, (u64)1) * 3, f32), *texcoords = ArenaPushArrayNoZero(scratch.arena, Max(textureCount, (u64)1) * 2, f32);
        u32 *pointColors = colorCount ? ArenaPushArrayNoZero(scratch.arena, pointCount, u32) : NULL;
        for (u64 i = 0; pointColors && i < pointCount; ++i) pointColors[i] = Prog_ModelUnset;
        u64 pointBase = 0, textureBase = 0;
        for (u32 i = 0; i < lanes; ++i) {
            Prog__ObjChunk *chunk = &chunks[i];
            MemCopy(points + pointBase * 3, chunk->points.v, chunk->points.header.count * sizeof(f32));
            MemCopy(texcoords + textureBase * 2, chunk->texcoords.v, chunk->texcoords.header.count * sizeof(f32));
            for (u64 k = 0; k < chunk->colors.header.count; ++k) pointColors[pointBase + chunk->colorPoints.v[k]] = chunk->colors.v[k];
            pointBase += chunk->points.header.count / 3, textureBase += chunk->texcoords.header.count / 2;
        }
        Prog__ArrayObjMaterial materials = {0};
        Prog__Corners made = Prog__CornersMake(scratch.arena, pointCount, cornerCount);
        pointBase = textureBase = 0;
        for (u32 i = 0; i < lanes; ++i) {
            Prog__ObjChunk *chunk = &chunks[i];
            u64 event = 0;
            for (u64 f = 0; f <= chunk->faces.header.count; ++f) {
                for (; event < chunk->events.header.count && chunk->events.v[event].face == f; ++event) {
                    const Prog__ObjEvent *line = &chunk->events.v[event];
                    if (line->kind == Prog__ObjMtllib) {
                        Prog__ReadMtl(arena, mesh, line->rest, &materials);
                    } else if (line->kind == Prog__ObjObject && materials.header.count) {
                        // A new object starts without a material, as Blender reads it.
                        Prog__MeshPart(arena, mesh, 0, UINT32_MAX, 0);
                    } else if (line->kind == Prog__ObjUsemtl) {
                        const Prog__ObjMaterial *material = NULL;
                        for (u64 k = 0; k < materials.header.count && !material; ++k) if (StrIsEqual(materials.v[k].name, line->rest)) material = &materials.v[k];
                        // A picture is drawn as it is, as Blender reads it; its Kd is left out.
                        Prog_MeshPart look = { .color = material && material->image == UINT32_MAX ? material->color : 0, .image = material ? material->image : UINT32_MAX,
                                               .bare = material ? material->color : 0 };
                        if (material) Prog__MeshAlpha(&look, material->alpha);
                        Prog__MeshLook(arena, mesh, &look);
                    }
                }
                if (f == chunk->faces.header.count) break;
                const Prog__ObjFace *face = &chunk->faces.v[f];
                i64 count = (i64)(pointBase + face->points), textures = (i64)(textureBase + face->textures);
                u32 first = 0, previous = 0;
                for (u32 c = 0; c < face->cornerCount; ++c) {
                    const Prog__ObjCorner *written = &chunk->corners.v[face->firstCorner + c];
                    i64 point = Prog__ObjResolve(written->point, count), texture = written->textured ? Prog__ObjResolve(written->texture, textures) : -1;
                    u32 corner = UINT32_MAX;
                    if (point >= 0) {
                        Prog__MeshCorner key = { (u32)point, (u32)(texture + 1), 0, 0 };
                        corner = Prog__CornersFind(&made, key);
                        if (corner == UINT32_MAX && mesh->positions.header.count / 3 < UINT32_MAX) {
                            corner = (u32)(mesh->positions.header.count / 3);
                            MemCopy(ArrayAdd(arena, mesh->positions, 3), points + point * 3, 12);
                            if (texture >= 0) MemCopy(Prog__MeshUvs(arena, mesh) + (u64)corner * 2, texcoords + texture * 2, 8);
                            if (pointColors && pointColors[point] != Prog_ModelUnset) Prog__MeshColors(arena, mesh)[corner] = pointColors[point];
                            Prog__CornersAdd(&made, key, corner);
                        }
                    }
                    if (!c) first = corner;
                    else if (c >= 2) Prog__MeshTriangle(arena, mesh, first, previous, corner);
                    previous = corner;
                }
            }
            pointBase += chunk->points.header.count / 3, textureBase += chunk->texcoords.header.count / 2;
        }
    }
    for (u32 i = 0; i < lanes; ++i) ArenaRelease(&chunks[i].arena);
    ReleaseScratch(scratch);
    return !cancelled && mesh->indices.header.count != 0;
}

// -----------------------------------------------------------------------------
// STL: binary, an 80-byte header, the triangle count and 50 bytes for each triangle, or text with "vertex x y z" lines, three to a facet.
// Its files have Z up, as the CAD programs and printers they come from do.
// -----------------------------------------------------------------------------

function b32 Prog__ReadStl(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    mesh->zUp = true;
    const u8 *bytes = (const u8 *)data.value;
    u32 count = 0;
    if (data.length >= 84) MemCopy(&count, bytes + 80, 4);
    // A binary file's size follows from its count; some still begin with "solid", as the text form does.
    if (data.length >= 84 && data.length == 84 + (u64)count * 50) {
        f32 *v = ArrayAdd(arena, mesh->positions, (u64)count * 9);
        u32 *t = ArrayAdd(arena, mesh->indices, (u64)count * 3);
        for (u64 i = 0; i < count; ++i) {
            MemCopy(v + i * 9, bytes + 84 + i * 50 + 12, 36);
            t[i * 3] = (u32)(i * 3);
            t[i * 3 + 1] = (u32)(i * 3 + 1);
            t[i * 3 + 2] = (u32)(i * 3 + 2);
        }
        // A facet's last two bytes may hold its color in five bits for each of blue, green and red, valid when the top bit is set, as
        // VisCAM and SolidView write them; Magics, whose header says "COLOR=" and the default color, puts red first and clears the bit
        // for a color of the facet's own.
        Index magics = StrIndexStr(StrMake(data.value, 80), S("COLOR="), SSF_None);
        u32 fallback = Prog_ModelUnset;
        if (magics.exists && magics.value + 10 <= 80) fallback = 0xFF000000u | bytes[magics.value + 6] | (u32)bytes[magics.value + 7] << 8 | (u32)bytes[magics.value + 8] << 16;
        b32 colored = magics.exists;
        for (u64 i = 0; i < count && !colored; ++i) colored = (bytes[84 + i * 50 + 49] & 0x80) != 0;
        if (colored) {
            u32 *colors = Prog__MeshColors(arena, mesh);
            for (u64 i = 0; i < count; ++i) {
                u16 packed = (u16)(bytes[84 + i * 50 + 48] | bytes[84 + i * 50 + 49] << 8);
                u32 low = (u32)(packed & 31) * 255 / 31, middle = (u32)((packed >> 5) & 31) * 255 / 31, high = (u32)((packed >> 10) & 31) * 255 / 31;
                b32 own = magics.exists ? !(packed & 0x8000) : (packed & 0x8000) != 0;
                u32 color = !own ? fallback : magics.exists ? 0xFF000000u | low | middle << 8 | high << 16 : 0xFF000000u | high | middle << 8 | low << 16;
                colors[i * 3] = colors[i * 3 + 1] = colors[i * 3 + 2] = color;
            }
        }
        return count != 0;
    }
    const char *p = data.value, *end = data.value + data.length;
    u32 corner = 0;
    for (u64 line = 1; p < end; ++line) {
        if ((line & 0xFFFF) == 0 && P_IsCancelled(cancel)) return false;
        while (p < end && Prog__IsSpace(*p)) ++p;
        if (end - p > 6 && MemCompare(p, "vertex", 6) == 0) {
            p += 6;
            f64 xyz[3] = {0};
            for (u32 i = 0; i < 3; ++i) Prog__Number(&p, end, &xyz[i]);
            f32 *v = ArrayAdd(arena, mesh->positions, 3);
            v[0] = (f32)xyz[0];
            v[1] = (f32)xyz[1];
            v[2] = (f32)xyz[2];
            if (++corner == 3) {
                corner = 0;
                u32 last = (u32)(mesh->positions.header.count / 3);
                Prog__MeshTriangle(arena, mesh, last - 3, last - 2, last - 1);
            }
        } else if (end - p > 5 && MemCompare(p, "outer", 5) == 0) {
            corner = 0;
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
    return mesh->indices.header.count != 0;
}

// -----------------------------------------------------------------------------
// PLY: a text header naming its elements and their properties, then the elements in text or in little- or big-endian binary. The vertex
// element's x, y and z, its color and texture coordinates, and the face element's index list are read; everything else is stepped over.
// The picture is the one a "comment TextureFile" names. Z is taken to point up, as Blender and the printing tools write it.
// -----------------------------------------------------------------------------

enum { Prog__PlyAscii, Prog__PlyLittle, Prog__PlyBig };

typedef struct Prog__PlyProperty {
    u8 type, countType;  // sizes in bytes, 0 for none; floats are told apart by `real`
    b32 real, list;
    // 1, 2 or 3 for x, y and z of a vertex, 4 for a face's corners, 5, 6 and 7 for its red, green and blue, 8 and 9 for its u and v, 10 for
    // its alpha, 11 for a face's corners' u and v, as MeshLab writes them
    u8 role;
} Prog__PlyProperty;

typedef struct Prog__PlyElement {
    u64 count;
    u32 propertyCount;
    b32 vertex, face;
    Prog__PlyProperty properties[32];
} Prog__PlyElement;

function b32 Prog__PlyType(String word, u8 *size, b32 *real)
{
    local_persist const String names[] = {
        SInit("char"), SInit("int8"), SInit("uchar"), SInit("uint8"), SInit("short"), SInit("int16"), SInit("ushort"), SInit("uint16"),
        SInit("int"), SInit("int32"), SInit("uint"), SInit("uint32"), SInit("float"), SInit("float32"), SInit("double"), SInit("float64"),
    };
    local_persist const u8 sizes[] = { 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 4, 8, 8 };
    for (u32 i = 0; i < CArrayCount(names); ++i) {
        if (StrIsEqual(word, names[i])) {
            *size = sizes[i];
            *real = i >= 12;
            // Signedness is kept in bit 7, read back by Prog__PlyValue.
            if (i < 12 && (i / 2) % 2 == 0) *size = (u8)(*size | 0x80);
            return true;
        }
    }
    return false;
}

function b32 Prog__PlyValue(const char **at, const char *end, u8 type, b32 real, u32 format, f64 *value)
{
    if (format == Prog__PlyAscii) {
        const char *p = *at;
        while (p < end && (Prog__IsSpace(*p) || *p == '\n')) ++p;
        *at = p;
        return Prog__Number(at, end, value);
    }
    u32 size = type & 0x7F;
    if ((u64)(end - *at) < size) return false;
    u8 b[8] = {0};
    for (u32 i = 0; i < size; ++i) b[i] = (u8)(*at)[format == Prog__PlyBig ? size - 1 - i : i];
    *at += size;
    b32 signedType = (type & 0x80) != 0;
    if (real && size == 4) {
        f32 v;
        MemCopy(&v, b, 4);
        *value = v;
    } else if (real) {
        MemCopy(value, b, 8);
    } else {
        u64 v = 0;
        MemCopy(&v, b, size);
        if (signedType && size < 8 && (v >> (size * 8 - 1))) v |= ~UINT64_C(0) << (size * 8);
        *value = signedType ? (f64)(i64)v : (f64)v;
    }
    return true;
}

// Where each of `element`'s rows starts at `p`, with where the next element starts after them; how many there are, fewer than its count in
// a file cut short. A text row is a line.
function u64 Prog__PlyRows(const Prog__PlyElement *element, u32 format, const char *p, const char *end, const char **rows)
{
    u64 fixed = 0;
    b32 lists = false;
    for (u32 k = 0; k < element->propertyCount; ++k) lists |= element->properties[k].list, fixed += element->properties[k].type & 0x7F;
    u64 i = 0;
    for (; i < element->count; ++i) {
        if (format == Prog__PlyAscii) {
            while (p < end && (Prog__IsSpace(*p) || *p == '\n')) ++p;
            if (p >= end) break;
            rows[i] = p;
            Index line = StrIndexChar(StrMake(p, (u64)(end - p)), '\n', 0);
            p = line.exists ? p + line.value + 1 : end;
            continue;
        }
        rows[i] = p;
        if (!lists) {
            if ((u64)(end - p) < fixed) break;
            p += fixed;
            continue;
        }
        b32 whole = true;
        for (u32 k = 0; k < element->propertyCount && whole; ++k) {
            const Prog__PlyProperty *property = &element->properties[k];
            f64 count = 0;
            u64 size = property->type & 0x7F;
            if (!property->list) whole = (u64)(end - p) >= size, p += whole ? size : 0;
            else whole = Prog__PlyValue(&p, end, property->countType, false, format, &count) && count >= 0 && (u64)count <= (u64)(end - p) / Max(size, (u64)1),
                 p += whole ? (u64)count * size : 0;
        }
        if (!whole) break;
    }
    rows[i] = format == Prog__PlyAscii || i == element->count ? p : rows[i];
    return i;
}

// One element's rows, a lane's share each: a vertex's values straight into its place, a face's triangles into the lane's own list.
typedef struct Prog__PlyWork {
    const Prog__PlyElement *element;
    u32 format, lanes;
    const char **rows, *end;
    u64 rowCount;
    const P_CancelToken *cancel;
    b32 colored, mapped, cornered, faceMapped;
    f32 *positions, *uvs;
    u32 *colors;
    Prog_ArrayU32 triangles[P_MaxLanes];
    Prog_ArrayF32 cornerUvs[P_MaxLanes];  // with faceMapped, each triangle's corners' u and v
    Arena arenas[P_MaxLanes];
} Prog__PlyWork;

function void Prog__PlyLane(void *user, u32 lane)
{
    Prog__PlyWork *work = user;
    const Prog__PlyElement *element = work->element;
    u64 from = work->rowCount * lane / work->lanes, to = work->rowCount * (lane + 1) / work->lanes;
    for (u64 i = from; i < to; ++i) {
        if ((i & 0xFFFF) == 0xFFFF && P_IsCancelled(work->cancel)) return;
        const char *p = work->rows[i], *end = work->format == Prog__PlyAscii ? work->rows[i + 1] : work->end;
        // Position, then red, green and blue from 0 to 1, then u and v, then alpha.
        f64 values[9] = { 0, 0, 0, 1, 1, 1, 0, 0, 1 };
        u32 cornerAt[256], cornerCount = 0, uvCount = 0;
        f32 cornerUv[512];
        for (u32 k = 0; k < element->propertyCount; ++k) {
            const Prog__PlyProperty *property = &element->properties[k];
            f64 value = 0;
            if (!property->list) {
                if (!Prog__PlyValue(&p, end, property->type, property->real, work->format, &value)) break;
                // A color's integers count up to their type's most.
                if (property->role >= 5 && property->role != 8 && property->role != 9 && !property->real) value /= (property->type & 0x7F) == 1 ? 255.0 : (property->type & 0x7F) == 2 ? 65535.0 : 4294967295.0;
                if (property->role && property->role != 4) values[property->role - 1 - (property->role > 4)] = value;
                continue;
            }
            if (!Prog__PlyValue(&p, end, property->countType, false, work->format, &value) || value < 0) break;
            u64 corners = (u64)value;
            for (u64 c = 0; c < corners; ++c) {
                if (!Prog__PlyValue(&p, end, property->type, property->real, work->format, &value)) break;
                if (property->role == 4 && cornerCount < CArrayCount(cornerAt)) cornerAt[cornerCount++] = value >= 0 && value < (f64)UINT32_MAX ? (u32)value : UINT32_MAX;
                if (property->role == 11 && uvCount < CArrayCount(cornerUv)) cornerUv[uvCount++] = (f32)value;
            }
        }
        // A face's corners as a fan of triangles, with their own u and v when the face has them.
        for (u32 c = 2; c < cornerCount; ++c) {
            u32 *triangle = ArrayAdd(&work->arenas[lane], work->triangles[lane], 3);
            triangle[0] = cornerAt[0], triangle[1] = cornerAt[c - 1], triangle[2] = cornerAt[c];
            if (!work->faceMapped) continue;
            f32 *uv = ArrayAdd(&work->arenas[lane], work->cornerUvs[lane], 6);
            u32 from[3] = { 0, c - 1, c };
            for (u32 k = 0; k < 3; ++k) {
                b32 has = from[k] * 2 + 1 < uvCount;
                uv[k * 2] = has ? cornerUv[from[k] * 2] : 0;
                uv[k * 2 + 1] = has ? 1 - cornerUv[from[k] * 2 + 1] : 0;
            }
        }
        cornerCount = uvCount = 0;
        if (!element->vertex) continue;
        f32 *v = work->positions + i * 3;
        v[0] = (f32)values[0];
        v[1] = (f32)values[1];
        v[2] = (f32)values[2];
        // Blender writes a vertex of a mesh without colors as black and clear, which is no color.
        if (work->colored && !(values[3] == 0 && values[4] == 0 && values[5] == 0 && values[8] == 0)) {
            u32 color = 0xFF000000u;
            for (u32 k = 0; k < 3; ++k) color |= (u32)(MinF32(MaxF32((f32)values[3 + k], 0), 1) * 255 + 0.5f) << (k * 8);
            work->colors[i] = color;
        }
        if (work->mapped) {
            work->uvs[i * 2] = (f32)values[6];
            work->uvs[i * 2 + 1] = 1 - (f32)values[7];
        }
    }
}

function b32 Prog__ReadPly(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    const char *p = data.value, *end = data.value + data.length;
    if (data.length < 4 || MemCompare(p, "ply", 3) != 0) return false;
    mesh->zUp = true;
    Prog__PlyElement elements[16];
    u32 elementCount = 0, format = UINT32_MAX;
    b32 header = false;
    String texture = {0};
    while (p < end && !header) {
        String word = Prog__Word(&p, end);
        if (StrIsEqual(word, S("format"))) {
            String kind = Prog__Word(&p, end);
            format = StrIsEqual(kind, S("ascii")) ? Prog__PlyAscii : StrIsEqual(kind, S("binary_little_endian")) ? Prog__PlyLittle
                   : StrIsEqual(kind, S("binary_big_endian")) ? Prog__PlyBig : UINT32_MAX;
        } else if (StrIsEqual(word, S("element"))) {
            if (elementCount == CArrayCount(elements)) return false;
            Prog__PlyElement *element = &elements[elementCount++];
            *element = (Prog__PlyElement){0};
            String name = Prog__Word(&p, end), count = Prog__Word(&p, end);
            element->vertex = StrIsEqual(name, S("vertex"));
            element->face = StrIsEqual(name, S("face"));
            const char *digits = count.value;
            f64 value = 0;
            if (!Prog__Number(&digits, count.value + count.length, &value) || value < 0) return false;
            element->count = (u64)value;
        } else if (StrIsEqual(word, S("property"))) {
            if (!elementCount) return false;
            Prog__PlyElement *element = &elements[elementCount - 1];
            if (element->propertyCount == CArrayCount(element->properties)) return false;
            Prog__PlyProperty *property = &element->properties[element->propertyCount++];
            *property = (Prog__PlyProperty){0};
            String type = Prog__Word(&p, end);
            if (StrIsEqual(type, S("list"))) {
                b32 countReal = false;
                property->list = true;
                if (!Prog__PlyType(Prog__Word(&p, end), &property->countType, &countReal) || countReal) return false;
                type = Prog__Word(&p, end);
            }
            if (!Prog__PlyType(type, &property->type, &property->real)) return false;
            String name = Prog__Word(&p, end);
            if (element->vertex && !property->list && name.length == 1 && name.value[0] >= 'x' && name.value[0] <= 'z') property->role = (u8)(name.value[0] - 'x' + 1);
            if (element->face && property->list && (StrIsEqual(name, S("vertex_indices")) || StrIsEqual(name, S("vertex_index")))) property->role = 4;
            if (element->face && property->list && property->real && StrIsEqual(name, S("texcoord"))) property->role = 11;
            if (element->vertex && !property->list) {
                local_persist const String names[] = {
                    SInit("red"), SInit("green"), SInit("blue"), SInit("s"), SInit("t"), SInit("diffuse_red"), SInit("diffuse_green"), SInit("diffuse_blue"),
                    SInit("u"), SInit("v"), SInit("texture_u"), SInit("texture_v"), SInit("texture_s"), SInit("texture_t"),
                };
                local_persist const u8 roles[] = { 5, 6, 7, 8, 9, 5, 6, 7, 8, 9, 8, 9, 8, 9 };
                if (StrIsEqual(name, S("alpha"))) property->role = 10;
                for (u32 i = 0; i < CArrayCount(names); ++i) if (StrIsEqual(name, names[i])) property->role = roles[i];
            }
        } else if (StrIsEqual(word, S("comment")) && StrIsEqual(Prog__Word(&p, end), S("TextureFile"))) {
            texture = Prog__LineRest(&p, end);
        } else if (StrIsEqual(word, S("end_header"))) {
            header = true;
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
    if (!header || format == UINT32_MAX) return false;
    u64 vertices = 0;
    // The picture, drawn as it is, for the whole mesh.
    u32 image = Prog__MeshImage(arena, mesh, texture, (String){0});
    if (image != UINT32_MAX) Prog__MeshPart(arena, mesh, 0, image, 0);
    for (u32 e = 0; e < elementCount && !P_IsCancelled(cancel); ++e) {
        const Prog__PlyElement *element = &elements[e];
        // An element without properties takes no room, whatever its count says.
        if (!element->propertyCount) continue;
        // Where each row starts, which a short file cuts off, then the rows read side by side.
        Scratch scratch = GetScratchEx(arena);
        const char **rows = ArenaPushArrayNoZero(scratch.arena, element->count + 1, const char *);
        u64 rowCount = Prog__PlyRows(element, format, p, end, rows);
        p = rows[rowCount];
        Prog__PlyWork work = { .element = element, .format = format, .rows = rows, .rowCount = rowCount, .end = end, .cancel = cancel };
        for (u32 k = 0; k < element->propertyCount; ++k) {
            work.colored |= element->properties[k].role >= 5 && element->properties[k].role <= 7;
            work.mapped |= element->properties[k].role >= 8 && element->properties[k].role <= 9;
            work.cornered |= element->properties[k].role == 4;
            work.faceMapped |= element->properties[k].role == 11;
        }
        if (element->vertex) {
            work.positions = ArrayAdd(arena, mesh->positions, rowCount * 3);
            if (work.colored) work.colors = Prog__MeshColors(arena, mesh) + vertices;
            if (work.mapped) work.uvs = Prog__MeshUvs(arena, mesh) + vertices * 2;
        }
        work.lanes = (u32)Min((u64)Max(1u, Min(P_ProcessorCount(), 8u)), Max(rowCount / 16384, (u64)1));
        if (element->vertex || work.cornered) P_RunLanes(work.lanes, Prog__PlyLane, &work);
        for (u32 lane = 0; lane < work.lanes && work.cornered; ++lane) {
            Prog_ArrayU32 *triangles = &work.triangles[lane];
            if (!work.faceMapped && triangles->header.count) MemCopy(ArrayAdd(arena, mesh->indices, triangles->header.count), triangles->v, triangles->header.count * sizeof(u32));
            // Faces with their own u and v: each corner a vertex of its own, at its point and in its color.
            for (u64 t = 0; work.faceMapped && t < triangles->header.count; ++t) {
                u32 point = triangles->v[t], made = UINT32_MAX;
                if (point < vertices && mesh->positions.header.count / 3 < UINT32_MAX) {
                    made = (u32)(mesh->positions.header.count / 3);
                    f32 *at = ArrayAdd(arena, mesh->positions, 3);
                    MemCopy(at, mesh->positions.v + (u64)point * 3, 12);
                    if (mesh->colors.header.count > point) {
                        u32 color = mesh->colors.v[point];
                        Prog__MeshColors(arena, mesh)[made] = color;
                    }
                    f32 *uv = Prog__MeshUvs(arena, mesh) + (u64)made * 2;
                    uv[0] = work.cornerUvs[lane].v[t * 2], uv[1] = work.cornerUvs[lane].v[t * 2 + 1];
                }
                ArrayPush(arena, mesh->indices, made);
            }
            ArenaRelease(&work.arenas[lane]);
        }
        if (element->vertex) vertices += rowCount;
        ReleaseScratch(scratch);
        if (rowCount < element->count) break;
    }
    return !P_IsCancelled(cancel) && vertices && mesh->indices.header.count != 0;
}

// -----------------------------------------------------------------------------
// glTF: a JSON scene of nodes and meshes whose triangles are in buffers, inside a .glb or in files or data URIs next to a .gltf. Plain float
// positions are read; meshes compressed by an extension are left out.
// -----------------------------------------------------------------------------

typedef struct Prog__Json Prog__Json;
struct Prog__Json {
    String key;          // its name, in an object
    String text;         // a string's characters with escapes as written, or a number's or keyword's
    Prog__Json **items;  // an object's or array's members
    u64 count;
    u8 kind;             // '{', '[', '"', or 'v' for a number, true, false or null
};

typedef struct Prog__JsonList { ArrayHeader header; Prog__Json **v; } Prog__JsonList;

function void Prog__JsonSkip(const char **at, const char *end)
{
    const char *p = *at;
    while (p < end && (Prog__IsSpace(*p) || *p == '\n')) ++p;
    *at = p;
}

function b32 Prog__JsonString(const char **at, const char *end, String *out)
{
    const char *p = *at;
    if (p >= end || *p != '"') return false;
    const char *start = ++p;
    while (p < end && *p != '"') p += *p == '\\' ? 2 : 1;
    if (p >= end) return false;
    *out = StrMake(start, (u64)(p - start));
    *at = p + 1;
    return true;
}

function Prog__Json *Prog__JsonValue(Arena *arena, const char **at, const char *end, u32 depth)
{
    Prog__JsonSkip(at, end);
    const char *p = *at;
    if (p >= end || depth > 64) return NULL;
    Prog__Json *node = ArenaPushStruct(arena, Prog__Json);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        node->kind = (u8)*p++;
        Prog__JsonList members = {0};
        for (;;) {
            Prog__JsonSkip(&p, end);
            if (p < end && *p == close && !members.header.count) {
                ++p;
                break;
            }
            String key = {0};
            if (close == '}') {
                if (!Prog__JsonString(&p, end, &key)) return NULL;
                Prog__JsonSkip(&p, end);
                if (p >= end || *p != ':') return NULL;
                ++p;
            }
            Prog__Json *member = Prog__JsonValue(arena, &p, end, depth + 1);
            if (!member) return NULL;
            member->key = key;
            ArrayPush(arena, members, member);
            Prog__JsonSkip(&p, end);
            if (p < end && *p == ',') {
                ++p;
            } else if (p < end && *p == close) {
                ++p;
                break;
            } else {
                return NULL;
            }
        }
        node->items = members.v;
        node->count = members.header.count;
    } else if (*p == '"') {
        node->kind = '"';
        if (!Prog__JsonString(&p, end, &node->text)) return NULL;
    } else {
        const char *start = p;
        while (p < end && *p != ',' && *p != '}' && *p != ']' && !Prog__IsSpace(*p) && *p != '\n') ++p;
        if (p == start) return NULL;
        node->kind = 'v';
        node->text = StrMake(start, (u64)(p - start));
    }
    *at = p;
    return node;
}

function Prog__Json *Prog__JsonGet(const Prog__Json *object, String key)
{
    if (object && object->kind == '{') {
        for (u64 i = 0; i < object->count; ++i) if (StrIsEqual(object->items[i]->key, key)) return object->items[i];
    }
    return NULL;
}

function Prog__Json *Prog__JsonAt(const Prog__Json *array, f64 index)
{
    return array && array->kind == '[' && index >= 0 && index < (f64)array->count ? array->items[(u64)index] : NULL;
}

function f64 Prog__JsonNumber(const Prog__Json *node, f64 fallback)
{
    f64 value = fallback;
    if (node && node->kind == 'v') {
        const char *p = node->text.value;
        if (!Prog__Number(&p, p + node->text.length, &value)) value = fallback;
    }
    return value;
}

// A whole count or offset, or `fallback` for none; UINT64_MAX for one that is negative or too large to be real.
function u64 Prog__JsonCount(const Prog__Json *node, u64 fallback)
{
    if (!node) return fallback;
    f64 value = Prog__JsonNumber(node, -1);
    return value >= 0 && value < 1e15 ? (u64)value : UINT64_MAX;
}

typedef struct Prog__Gltf {
    const Prog__Json *root;
    String *buffers;
    u64 bufferCount;
} Prog__Gltf;

function u8 Prog__Base64(char c)
{
    return (u8)(c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : 64);
}

function i32 Prog__Hex(char c)
{
    char lower = (char)(c | 0x20);
    return c >= '0' && c <= '9' ? c - '0' : lower >= 'a' && lower <= 'f' ? lower - 'a' + 10 : -1;
}

function String Prog__DecodeBase64(Arena *arena, String text)
{
    u8 *out = ArenaPushArrayNoZero(arena, text.length / 4 * 3 + 3, u8);
    u64 length = 0;
    u32 bits = 0, have = 0;
    for (u64 i = 0; i < text.length; ++i) {
        u8 value = Prog__Base64(text.value[i]);
        if (value == 64) continue;
        bits = (bits << 6) | value;
        have += 6;
        if (have >= 8) {
            have -= 8;
            out[length++] = (u8)(bits >> have);
        }
    }
    return StrMake((const char *)out, length);
}

// A URI's file name with its %XX escapes undone.
function String Prog__UriName(Arena *arena, String text)
{
    char *name = ArenaPushArrayNoZero(arena, Max(text.length, (u64)1), char);
    u64 n = 0;
    for (u64 i = 0; i < text.length; ++i) {
        i32 high = i + 2 < text.length ? Prog__Hex(text.value[i + 1]) : -1, low = i + 2 < text.length ? Prog__Hex(text.value[i + 2]) : -1;
        if (text.value[i] == '%' && high >= 0 && low >= 0) {
            name[n++] = (char)(high * 16 + low);
            i += 2;
        } else {
            name[n++] = text.value[i];
        }
    }
    return StrMake(name, n);
}

// A data URI's bytes; empty for another URI.
function String Prog__DataUri(Arena *arena, String text)
{
    if (text.length <= 5 || MemCompare(text.value, "data:", 5) != 0) return (String){0};
    Index comma = StrIndexChar(text, ',', SSF_None);
    return comma.exists ? Prog__DecodeBase64(arena, StrFrom(text, comma.value + 1)) : (String){0};
}

// A buffer's bytes: the .glb's own chunk, a data URI's, or a file's next to the .gltf.
function String Prog__GltfBuffer(Arena *arena, String path, const Prog__Json *buffer, String glb)
{
    String result = {0};
    const Prog__Json *uri = Prog__JsonGet(buffer, S("uri"));
    u64 length = Prog__JsonCount(Prog__JsonGet(buffer, S("byteLength")), 0);
    if (!uri) return glb;
    if (uri->kind != '"' || length == UINT64_MAX) return result;
    if (StrIsEqual(StrFromTo(uri->text, 0, 5), S("data:"))) return Prog__DataUri(arena, uri->text);
    String file = Prog_JoinPath(arena, Prog_ParentPath(path), Prog__UriName(arena, uri->text));
    if (!P_ReadFileStart(arena, file, length, &result) || result.length < length) result = (String){0};
    return result;
}

// Accessor `index`'s elements: `count` of them `stride` bytes apart from `bytes`, checked to lie inside their buffer; false for one that does
// not, or that is sparse or has no buffer view, as an extension's compressed data has.
function b32 Prog__GltfAccessor(const Prog__Gltf *gltf, const Prog__Json *index, u32 *componentType, u32 *components, const u8 **bytes, u64 *count, u64 *stride)
{
    const Prog__Json *accessor = Prog__JsonAt(Prog__JsonGet(gltf->root, S("accessors")), Prog__JsonNumber(index, -1));
    const Prog__Json *view = Prog__JsonAt(Prog__JsonGet(gltf->root, S("bufferViews")), Prog__JsonNumber(Prog__JsonGet(accessor, S("bufferView")), -1));
    if (!accessor || !view || Prog__JsonGet(accessor, S("sparse"))) return false;
    u64 buffer = Prog__JsonCount(Prog__JsonGet(view, S("buffer")), UINT64_MAX), viewOffset = Prog__JsonCount(Prog__JsonGet(view, S("byteOffset")), 0);
    u64 viewLength = Prog__JsonCount(Prog__JsonGet(view, S("byteLength")), UINT64_MAX), offset = Prog__JsonCount(Prog__JsonGet(accessor, S("byteOffset")), 0);
    *count = Prog__JsonCount(Prog__JsonGet(accessor, S("count")), UINT64_MAX);
    *componentType = (u32)Prog__JsonNumber(Prog__JsonGet(accessor, S("componentType")), 0);
    const Prog__Json *type = Prog__JsonGet(accessor, S("type"));
    String name = type && type->kind == '"' ? type->text : (String){0};
    *components = StrIsEqual(name, S("SCALAR")) ? 1 : StrIsEqual(name, S("VEC2")) ? 2 : StrIsEqual(name, S("VEC3")) ? 3 : StrIsEqual(name, S("VEC4")) ? 4 : 0;
    u64 componentSize = *componentType == 5120 || *componentType == 5121 ? 1 : *componentType == 5122 || *componentType == 5123 ? 2
                      : *componentType == 5125 || *componentType == 5126 ? 4 : 0;
    u64 element = componentSize * *components;
    *stride = Prog__JsonCount(Prog__JsonGet(view, S("byteStride")), 0);
    if (!*stride) *stride = element;
    if (!element || buffer >= gltf->bufferCount || viewOffset == UINT64_MAX || viewLength == UINT64_MAX || offset == UINT64_MAX || *count == UINT64_MAX ||
        *stride == UINT64_MAX || *stride > 255 || viewOffset > gltf->buffers[buffer].length || viewLength > gltf->buffers[buffer].length - viewOffset) return false;
    // The last element ends inside the view: (count - 1) * stride + element <= room.
    u64 room = offset <= viewLength ? viewLength - offset : 0;
    if (offset > viewLength || (*count && (element > room || *count - 1 > (room - element) / *stride))) return false;
    *bytes = (const u8 *)gltf->buffers[buffer].value + viewOffset + offset;
    return true;
}

// Component `k` of an element at `at`, as a float: a float's own value, or a normalized integer's from 0 to 1, or -1 to 1 when signed.
function f32 Prog__GltfComponent(const u8 *at, u32 type, u32 k)
{
    switch (type) {
        case 5126: { f32 v; MemCopy(&v, at + k * 4, 4); return v; }
        case 5121: return (f32)at[k] / 255.0f;
        case 5120: return MaxF32((f32)(i8)at[k] / 127.0f, -1);
        case 5123: { u16 v; MemCopy(&v, at + k * 2, 2); return (f32)v / 65535.0f; }
        case 5122: { i16 v; MemCopy(&v, at + k * 2, 2); return MaxF32((f32)v / 32767.0f, -1); }
        default: return 0;
    }
}

// The picture a material's texture info names, through its texture's image: in a buffer view, a data URI or a file next to the .gltf.
function u32 Prog__GltfImage(Arena *arena, const Prog__Gltf *gltf, const Prog__Json *info, Prog_Mesh *out)
{
    const Prog__Json *texture = Prog__JsonAt(Prog__JsonGet(gltf->root, S("textures")), Prog__JsonNumber(Prog__JsonGet(info, S("index")), -1));
    f64 source = Prog__JsonNumber(Prog__JsonGet(texture, S("source")), -1);
    // The extensions that keep the picture in a newer format still have it in a fallback source most of the time.
    const Prog__Json *image = Prog__JsonAt(Prog__JsonGet(gltf->root, S("images")), source);
    if (!image || !out->imageSide) return UINT32_MAX;
    const Prog__Json *uri = Prog__JsonGet(image, S("uri"));
    String key = StrFormat(arena, "#%u", (u32)source);
    if (uri && uri->kind == '"' && !StrIsEqual(StrFromTo(uri->text, 0, 5), S("data:"))) return Prog__MeshImage(arena, out, Prog__UriName(arena, uri->text), (String){0});
    if (uri && uri->kind == '"') return Prog__MeshImage(arena, out, key, Prog__DataUri(arena, uri->text));
    const Prog__Json *view = Prog__JsonAt(Prog__JsonGet(gltf->root, S("bufferViews")), Prog__JsonNumber(Prog__JsonGet(image, S("bufferView")), -1));
    u64 buffer = Prog__JsonCount(Prog__JsonGet(view, S("buffer")), UINT64_MAX), offset = Prog__JsonCount(Prog__JsonGet(view, S("byteOffset")), 0);
    u64 length = Prog__JsonCount(Prog__JsonGet(view, S("byteLength")), UINT64_MAX);
    if (!view || buffer >= gltf->bufferCount || offset > gltf->buffers[buffer].length || length > gltf->buffers[buffer].length - offset) return UINT32_MAX;
    return Prog__MeshImage(arena, out, key, StrMake(gltf->buffers[buffer].value + offset, length));
}

function void Prog__GltfMesh(Arena *arena, const Prog__Gltf *gltf, const Prog__Json *mesh, const Prog__Affine *place, Prog_Mesh *out)
{
    const Prog__Json *primitives = Prog__JsonGet(mesh, S("primitives"));
    for (u64 i = 0; primitives && i < primitives->count; ++i) {
        const Prog__Json *primitive = primitives->items[i];
        if (Prog__JsonNumber(Prog__JsonGet(primitive, S("mode")), 4) != 4) continue;
        u32 type = 0, components = 0;
        const u8 *bytes = NULL;
        u64 count = 0, stride = 0;
        const Prog__Json *attributes = Prog__JsonGet(primitive, S("attributes"));
        if (!Prog__GltfAccessor(gltf, Prog__JsonGet(attributes, S("POSITION")), &type, &components, &bytes, &count, &stride) || type != 5126 || components != 3) continue;
        u64 base = out->positions.header.count / 3;
        if (base + count >= UINT32_MAX) return;
        f32 *v = ArrayAdd(arena, out->positions, count * 3);
        for (u64 k = 0; k < count; ++k) {
            f32 xyz[3];
            MemCopy(xyz, bytes + k * stride, 12);
            Prog__MeshPoint(v + k * 3, place, xyz[0], xyz[1], xyz[2]);
        }
        // Its material's base color and picture, or the older specular-glossiness one's diffuse ones, and the texture coordinates the
        // picture is drawn by.
        const Prog__Json *material = Prog__JsonAt(Prog__JsonGet(gltf->root, S("materials")), Prog__JsonNumber(Prog__JsonGet(primitive, S("material")), -1));
        const Prog__Json *pbr = Prog__JsonGet(material, S("pbrMetallicRoughness"));
        const Prog__Json *factor = Prog__JsonGet(pbr, S("baseColorFactor")), *info = Prog__JsonGet(pbr, S("baseColorTexture"));
        const Prog__Json *glossy = Prog__JsonGet(Prog__JsonGet(material, S("extensions")), S("KHR_materials_pbrSpecularGlossiness"));
        if (glossy && !pbr) factor = Prog__JsonGet(glossy, S("diffuseFactor")), info = Prog__JsonGet(glossy, S("diffuseTexture"));
        u32 color = 0, image = UINT32_MAX;
        b32 blended = false;
        if (material) {
            color = Prog__LinearColor((f32)Prog__JsonNumber(Prog__JsonAt(factor, 0), 1), (f32)Prog__JsonNumber(Prog__JsonAt(factor, 1), 1),
                                      (f32)Prog__JsonNumber(Prog__JsonAt(factor, 2), 1), 1);
            if (info) image = Prog__GltfImage(arena, gltf, info, out);
            const Prog__Json *mode = Prog__JsonGet(material, S("alphaMode"));
            blended = mode && mode->kind == '"' && StrIsEqual(mode->text, S("BLEND"));
        }
        Prog_MeshPart look = { .color = color, .image = image };
        if (blended) {
            Prog__MeshAlpha(&look, (f32)Prog__JsonNumber(Prog__JsonAt(factor, 3), 1));
            look.blend = true;
        }
        Prog__MeshLook(arena, out, &look);
        // The picture's place on its coordinates, as KHR_texture_transform moves, turns and scales it: offset * rotation * scale, which may
        // take another set of coordinates.
        const Prog__Json *transform = Prog__JsonGet(Prog__JsonGet(info, S("extensions")), S("KHR_texture_transform"));
        f64 texCoord = Prog__JsonNumber(Prog__JsonGet(transform, S("texCoord")), Prog__JsonNumber(Prog__JsonGet(info, S("texCoord")), 0));
        f32 offset[2] = { (f32)Prog__JsonNumber(Prog__JsonAt(Prog__JsonGet(transform, S("offset")), 0), 0), (f32)Prog__JsonNumber(Prog__JsonAt(Prog__JsonGet(transform, S("offset")), 1), 0) };
        f32 scale[2] = { (f32)Prog__JsonNumber(Prog__JsonAt(Prog__JsonGet(transform, S("scale")), 0), 1), (f32)Prog__JsonNumber(Prog__JsonAt(Prog__JsonGet(transform, S("scale")), 1), 1) };
        f32 turn = (f32)Prog__JsonNumber(Prog__JsonGet(transform, S("rotation")), 0), c = cosf(turn), s = sinf(turn);
        String set = StrFormat(arena, "TEXCOORD_%u", (u32)texCoord);
        u64 n = 0;
        if (image != UINT32_MAX && Prog__GltfAccessor(gltf, Prog__JsonGet(attributes, set), &type, &components, &bytes, &n, &stride) && components == 2) {
            f32 *uvs = Prog__MeshUvs(arena, out) + base * 2;
            for (u64 k = 0; k < Min(n, count); ++k) {
                f32 u = Prog__GltfComponent(bytes + k * stride, type, 0) * scale[0], v = Prog__GltfComponent(bytes + k * stride, type, 1) * scale[1];
                uvs[k * 2] = c * u + s * v + offset[0];
                uvs[k * 2 + 1] = -s * u + c * v + offset[1];
            }
        }
        // Its vertices' colors, linear as glTF keeps them.
        if (Prog__GltfAccessor(gltf, Prog__JsonGet(attributes, S("COLOR_0")), &type, &components, &bytes, &n, &stride) && (components == 3 || components == 4)) {
            u32 *colors = Prog__MeshColors(arena, out) + base;
            for (u64 k = 0; k < Min(n, count); ++k) {
                const u8 *at = bytes + k * stride;
                colors[k] = Prog__LinearColor(Prog__GltfComponent(at, type, 0), Prog__GltfComponent(at, type, 1), Prog__GltfComponent(at, type, 2), 1);
            }
        }
        const Prog__Json *indices = Prog__JsonGet(primitive, S("indices"));
        const u8 *corners = NULL;
        u64 cornerCount = 0, cornerStride = 0;
        if (!indices) {
            u32 *t = ArrayAdd(arena, out->indices, count / 3 * 3);
            for (u64 k = 0; k < count / 3 * 3; ++k) t[k] = (u32)(base + k);
        } else if (Prog__GltfAccessor(gltf, indices, &type, &components, &corners, &cornerCount, &cornerStride) && components == 1 && (type == 5121 || type == 5123 || type == 5125)) {
            u32 *t = ArrayAdd(arena, out->indices, cornerCount / 3 * 3);
            for (u64 k = 0; k < cornerCount / 3 * 3; ++k) {
                u32 corner = 0;
                if (type == 5121) corner = corners[k * cornerStride];
                else if (type == 5123) {
                    u16 value;
                    MemCopy(&value, corners + k * cornerStride, 2);
                    corner = value;
                } else {
                    MemCopy(&corner, corners + k * cornerStride, 4);
                }
                t[k] = corner < count ? (u32)(base + corner) : UINT32_MAX;
            }
        }
    }
}

function void Prog__GltfNode(Arena *arena, const Prog__Gltf *gltf, const Prog__Json *node, Prog__Affine parent, u32 depth, const P_CancelToken *cancel, Prog_Mesh *out)
{
    if (!node || depth > 32 || P_IsCancelled(cancel)) return;
    Prog__Affine local = Prog__AffineIdentity;
    const Prog__Json *matrix = Prog__JsonGet(node, S("matrix"));
    if (matrix && matrix->count == 16) {
        // Stored column after column.
        for (u32 row = 0; row < 3; ++row) {
            for (u32 column = 0; column < 4; ++column) local.m[row * 4 + column] = (f32)Prog__JsonNumber(matrix->items[column * 4 + row], 0);
        }
    } else {
        f32 t[3] = { 0, 0, 0 }, q[4] = { 0, 0, 0, 1 }, s[3] = { 1, 1, 1 };
        const Prog__Json *translation = Prog__JsonGet(node, S("translation")), *rotation = Prog__JsonGet(node, S("rotation")), *scale = Prog__JsonGet(node, S("scale"));
        for (u32 i = 0; i < 3; ++i) t[i] = (f32)Prog__JsonNumber(Prog__JsonAt(translation, (f64)i), t[i]);
        for (u32 i = 0; i < 4; ++i) q[i] = (f32)Prog__JsonNumber(Prog__JsonAt(rotation, (f64)i), q[i]);
        for (u32 i = 0; i < 3; ++i) s[i] = (f32)Prog__JsonNumber(Prog__JsonAt(scale, (f64)i), s[i]);
        local = Prog__AffineCompose(t, q, s);
    }
    Prog__Affine place = Prog__AffineMultiply(parent, local);
    const Prog__Json *mesh = Prog__JsonAt(Prog__JsonGet(gltf->root, S("meshes")), Prog__JsonNumber(Prog__JsonGet(node, S("mesh")), -1));
    if (mesh) Prog__GltfMesh(arena, gltf, mesh, &place, out);
    const Prog__Json *children = Prog__JsonGet(node, S("children")), *nodes = Prog__JsonGet(gltf->root, S("nodes"));
    for (u64 i = 0; children && i < children->count; ++i) {
        Prog__GltfNode(arena, gltf, Prog__JsonAt(nodes, Prog__JsonNumber(children->items[i], -1)), place, depth + 1, cancel, out);
    }
}

function b32 Prog__ReadGltf(Arena *arena, String path, String data, b32 binary, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    String json = data, glb = {0};
    if (binary) {
        // A 12-byte header, then chunks of a length, a type and the bytes: the JSON first, then the binary buffer.
        const u8 *bytes = (const u8 *)data.value;
        u32 magic = 0, length = 0, type = 0;
        if (data.length < 20) return false;
        MemCopy(&magic, bytes, 4);
        if (magic != 0x46546C67) return false;
        json = (String){0};
        for (u64 at = 12; at + 8 <= data.length;) {
            MemCopy(&length, bytes + at, 4);
            MemCopy(&type, bytes + at + 4, 4);
            if (length > data.length - at - 8) return false;
            String chunk = StrMake(data.value + at + 8, length);
            if (type == 0x4E4F534A && !json.length) json = chunk;
            else if (type == 0x004E4942 && !glb.length) glb = chunk;
            at += 8 + ((u64)length + 3) / 4 * 4;
        }
    }
    const char *p = json.value;
    Prog__Gltf gltf = { .root = Prog__JsonValue(arena, &p, json.value + json.length, 0) };
    if (!gltf.root || gltf.root->kind != '{') return false;
    const Prog__Json *buffers = Prog__JsonGet(gltf.root, S("buffers"));
    gltf.bufferCount = buffers && buffers->kind == '[' ? buffers->count : 0;
    gltf.buffers = ArenaPushArray(arena, gltf.bufferCount + 1, String);
    for (u64 i = 0; i < gltf.bufferCount && !P_IsCancelled(cancel); ++i) gltf.buffers[i] = Prog__GltfBuffer(arena, path, buffers->items[i], i == 0 ? glb : (String){0});
    const Prog__Json *scenes = Prog__JsonGet(gltf.root, S("scenes")), *nodes = Prog__JsonGet(gltf.root, S("nodes"));
    const Prog__Json *scene = Prog__JsonAt(scenes, Prog__JsonNumber(Prog__JsonGet(gltf.root, S("scene")), 0));
    const Prog__Json *roots = Prog__JsonGet(scene, S("nodes"));
    if (roots) {
        for (u64 i = 0; i < roots->count; ++i) Prog__GltfNode(arena, &gltf, Prog__JsonAt(nodes, Prog__JsonNumber(roots->items[i], -1)), Prog__AffineIdentity, 0, cancel, mesh);
    } else {
        // Without a scene, each mesh once where it stands.
        const Prog__Json *meshes = Prog__JsonGet(gltf.root, S("meshes"));
        for (u64 i = 0; meshes && i < meshes->count; ++i) Prog__GltfMesh(arena, &gltf, meshes->items[i], &Prog__AffineIdentity, mesh);
    }
    return !P_IsCancelled(cancel) && mesh->indices.header.count != 0;
}

// -----------------------------------------------------------------------------
// FBX: the binary form, a tree of named nodes with typed values, whose arrays may be zlib-compressed. Each mesh's vertices and polygons
// are read, placed by the models they hang from through the file's connections, with each model's translation, pre-rotation, rotation
// (in the default X, Y, Z order) and scale, and its geometric offset. The text form, skinning and animation are left out.
// -----------------------------------------------------------------------------

typedef struct Prog__FbxNode Prog__FbxNode;
struct Prog__FbxNode {
    String name;
    const u8 *properties, *propertiesEnd;
    Prog__FbxNode *first, *next;
    const u8 *inflated;  // its compressed array, inflated ahead of reading it
};

typedef struct Prog__FbxProperty {
    u8 type;
    const u8 *data;
    u64 length;       // of data, compressed or not
    u32 count;        // an array's elements
    u32 encoding;     // an array's: 0 plain, 1 zlib
} Prog__FbxProperty;

// The nodes from `at` to `end`, until the empty record that ends a list; offsets in a node's header count from the start of the file.
function Prog__FbxNode *Prog__FbxList(Arena *arena, const u8 *file, const u8 *at, const u8 *end, b32 wide, u32 depth)
{
    Prog__FbxNode *first = NULL, *last = NULL;
    u64 header = wide ? 25 : 13;
    while ((u64)(end - at) >= header && depth < 64) {
        u64 next = 0, bytes = 0;
        u8 nameLength = 0;
        if (wide) {
            MemCopy(&next, at, 8);
            MemCopy(&bytes, at + 16, 8);
            nameLength = at[24];
        } else {
            u32 next32 = 0, bytes32 = 0;
            MemCopy(&next32, at, 4);
            MemCopy(&bytes32, at + 8, 4);
            next = next32;
            bytes = bytes32;
            nameLength = at[12];
        }
        if (!next) break;
        u64 start = (u64)(at - file), stop = (u64)(end - file);
        if (next > stop || next <= start || header + nameLength > next - start || bytes > next - start - header - nameLength) break;
        Prog__FbxNode *node = ArenaPushStruct(arena, Prog__FbxNode);
        node->name = StrMake((const char *)at + header, nameLength);
        node->properties = at + header + nameLength;
        node->propertiesEnd = node->properties + bytes;
        if (node->propertiesEnd < file + next) node->first = Prog__FbxList(arena, file, node->propertiesEnd, file + next, wide, depth + 1);
        if (last) last->next = node;
        else first = node;
        last = node;
        at = file + next;
    }
    return first;
}

function Prog__FbxNode *Prog__FbxChild(const Prog__FbxNode *list, String name)
{
    for (const Prog__FbxNode *node = list; node; node = node->next) if (StrIsEqual(node->name, name)) return (Prog__FbxNode *)node;
    return NULL;
}

function b32 Prog__FbxPropertyAt(const Prog__FbxNode *node, u32 index, Prog__FbxProperty *out)
{
    const u8 *p = node ? node->properties : NULL, *end = node ? node->propertiesEnd : NULL;
    for (u32 i = 0; p < end; ++i) {
        Prog__FbxProperty property = { .type = *p++ };
        u64 size = 0;
        u32 length = 0;
        switch (property.type) {
            case 'C': size = 1; break;
            case 'Y': size = 2; break;
            case 'I': case 'F': size = 4; break;
            case 'D': case 'L': size = 8; break;
            case 'S': case 'R':
                if (end - p < 4) return false;
                MemCopy(&length, p, 4);
                p += 4;
                size = length;
                break;
            case 'f': case 'd': case 'i': case 'l': case 'b':
                if (end - p < 12) return false;
                MemCopy(&property.count, p, 4);
                MemCopy(&property.encoding, p + 4, 4);
                MemCopy(&length, p + 8, 4);
                p += 12;
                size = length;
                break;
            default: return false;
        }
        if ((u64)(end - p) < size) return false;
        property.data = p;
        property.length = size;
        if (i == index) {
            *out = property;
            return true;
        }
        p += size;
    }
    return false;
}

function f64 Prog__FbxNumberAt(const Prog__FbxNode *node, u32 index)
{
    Prog__FbxProperty property = {0};
    if (!Prog__FbxPropertyAt(node, index, &property)) return 0;
    switch (property.type) {
        case 'C': return property.data[0];
        case 'Y': { i16 v; MemCopy(&v, property.data, 2); return v; }
        case 'I': { i32 v; MemCopy(&v, property.data, 4); return v; }
        case 'F': { f32 v; MemCopy(&v, property.data, 4); return v; }
        case 'D': { f64 v; MemCopy(&v, property.data, 8); return v; }
        case 'L': { i64 v; MemCopy(&v, property.data, 8); return (f64)v; }
        default: return 0;
    }
}

function i64 Prog__FbxIdAt(const Prog__FbxNode *node, u32 index)
{
    Prog__FbxProperty property = {0};
    i64 result = 0;
    if (Prog__FbxPropertyAt(node, index, &property) && property.type == 'L') MemCopy(&result, property.data, 8);
    return result;
}

function String Prog__FbxStringAt(const Prog__FbxNode *node, u32 index)
{
    Prog__FbxProperty property = {0};
    return Prog__FbxPropertyAt(node, index, &property) && property.type == 'S' ? StrMake((const char *)property.data, property.length) : (String){0};
}

// An array's elements, 4 or 8 bytes each as its type says, inflated into `arena` when compressed; NULL for one that does not decode.
function const u8 *Prog__FbxArray(Arena *arena, const Prog__FbxNode *node, u8 *type, u64 *count)
{
    Prog__FbxProperty property = {0};
    if (!Prog__FbxPropertyAt(node, 0, &property) || !(property.type == 'f' || property.type == 'd' || property.type == 'i' || property.type == 'l')) return NULL;
    u64 bytes = (u64)property.count * (property.type == 'd' || property.type == 'l' ? 8 : 4);
    *type = property.type;
    *count = property.count;
    if (property.encoding == 0) return property.length >= bytes ? property.data : NULL;
    if (node->inflated) return node->inflated;
    if (property.encoding != 1 || bytes > INT32_MAX || property.length > INT32_MAX) return NULL;
    u8 *out = ArenaPushArrayNoZero(arena, bytes, u8);
    return stbi_zlib_decode_buffer((char *)out, (i32)bytes, (const char *)property.data, (i32)property.length) == (i32)bytes ? out : NULL;
}

// The compressed arrays of the geometries, inflated side by side ahead of reading them, as most of an FBX is.
typedef struct Prog__FbxInflate {
    struct { ArrayHeader header; Prog__FbxNode **v; } nodes;
    u8 **outs;
    atomic_uint_fast64_t next;
} Prog__FbxInflate;

function void Prog__FbxCollect(Arena *arena, Prog__FbxNode *list, Prog__FbxInflate *work, u32 depth)
{
    for (Prog__FbxNode *node = list; node && depth < 64; node = node->next) {
        Prog__FbxProperty property = {0};
        // Only those read: the normals and tangents beside them are as large and left out.
        local_persist const String read[] = { SInit("Vertices"), SInit("PolygonVertexIndex"), SInit("UV"), SInit("UVIndex"), SInit("Colors"), SInit("ColorIndex"), SInit("Materials") };
        b32 wanted = false;
        for (u32 k = 0; k < CArrayCount(read) && !wanted; ++k) wanted = StrIsEqual(node->name, read[k]);
        if (wanted && Prog__FbxPropertyAt(node, 0, &property) && property.encoding == 1 && (property.type == 'f' || property.type == 'd' || property.type == 'i' || property.type == 'l')) {
            ArrayPush(arena, work->nodes, node);
        }
        Prog__FbxCollect(arena, node->first, work, depth + 1);
    }
}

function void Prog__FbxInflateLane(void *user, u32 lane)
{
    (void)lane;
    Prog__FbxInflate *work = user;
    for (u64 i; (i = atomic_fetch_add(&work->next, 1)) < work->nodes.header.count;) {
        Prog__FbxNode *node = work->nodes.v[i];
        Prog__FbxProperty property = {0};
        Prog__FbxPropertyAt(node, 0, &property);
        u64 bytes = (u64)property.count * (property.type == 'd' || property.type == 'l' ? 8 : 4);
        if (!work->outs[i] || bytes > INT32_MAX || property.length > INT32_MAX) continue;
        if (stbi_zlib_decode_buffer((char *)work->outs[i], (i32)bytes, (const char *)property.data, (i32)property.length) == (i32)bytes) node->inflated = work->outs[i];
    }
}

function void Prog__FbxInflateAll(Arena *arena, Prog__FbxNode **geometries, u64 count)
{
    Scratch scratch = GetScratchEx(arena);
    Prog__FbxInflate work = {0};
    for (u64 i = 0; i < count; ++i) Prog__FbxCollect(scratch.arena, geometries[i]->first, &work, 0);
    // The outputs are made here, as the arena is not the lanes' to share.
    work.outs = ArenaPushArray(scratch.arena, Max(work.nodes.header.count, (u64)1), u8 *);
    for (u64 i = 0; i < work.nodes.header.count; ++i) {
        Prog__FbxProperty property = {0};
        Prog__FbxPropertyAt(work.nodes.v[i], 0, &property);
        u64 bytes = (u64)property.count * (property.type == 'd' || property.type == 'l' ? 8 : 4);
        if (bytes <= INT32_MAX) work.outs[i] = ArenaPushArrayNoZero(arena, Max(bytes, (u64)1), u8);
    }
    if (work.nodes.header.count) P_RunLanes((u32)Min((u64)Max(1u, Min(P_ProcessorCount(), 8u)), work.nodes.header.count), Prog__FbxInflateLane, &work);
    ReleaseScratch(scratch);
}

function f64 Prog__FbxElement(const u8 *data, u8 type, u64 index)
{
    switch (type) {
        case 'f': { f32 v; MemCopy(&v, data + index * 4, 4); return v; }
        case 'd': { f64 v; MemCopy(&v, data + index * 8, 8); return v; }
        case 'i': { i32 v; MemCopy(&v, data + index * 4, 4); return v; }
        default: { i64 v; MemCopy(&v, data + index * 8, 8); return (f64)v; }
    }
}

// The "P" entry named `name` in a node's Properties70.
function Prog__FbxNode *Prog__FbxSetting(const Prog__FbxNode *node, String name)
{
    const Prog__FbxNode *settings = node ? Prog__FbxChild(node->first, S("Properties70")) : NULL;
    for (const Prog__FbxNode *p = settings ? settings->first : NULL; p; p = p->next) {
        if (StrIsEqual(p->name, S("P")) && StrIsEqual(Prog__FbxStringAt(p, 0), name)) return (Prog__FbxNode *)p;
    }
    return NULL;
}

function void Prog__FbxVector(const Prog__FbxNode *node, String name, f32 out[3])
{
    const Prog__FbxNode *setting = Prog__FbxSetting(node, name);
    if (setting) for (u32 i = 0; i < 3; ++i) out[i] = (f32)Prog__FbxNumberAt(setting, 4 + i);
}

// A model's place in its parent, or with `geometric` the offset of its mesh from it.
function Prog__Affine Prog__FbxPlacement(const Prog__FbxNode *model, b32 geometric)
{
    f32 t[3] = {0}, r[3] = {0}, s[3] = { 1, 1, 1 }, pre[3] = {0}, q[4], none[3] = {0}, one[3] = { 1, 1, 1 };
    Prog__FbxVector(model, geometric ? S("GeometricTranslation") : S("Lcl Translation"), t);
    Prog__FbxVector(model, geometric ? S("GeometricRotation") : S("Lcl Rotation"), r);
    Prog__FbxVector(model, geometric ? S("GeometricScaling") : S("Lcl Scaling"), s);
    if (!geometric) Prog__FbxVector(model, S("PreRotation"), pre);
    Prog__EulerQuaternion(pre, q);
    Prog__Affine result = Prog__AffineCompose(t, q, one);
    Prog__EulerQuaternion(r, q);
    return Prog__AffineMultiply(result, Prog__AffineCompose(none, q, s));
}

// A layer element of a geometry, as LayerElementColor or LayerElementUV: its values, `width` to an element, and how a corner finds its
// element: by its polygon vertex, its control point or its polygon, or all the same, directly or through the index array.
typedef struct Prog__FbxLayer {
    const u8 *values, *indices;
    u8 valueType, indexType;
    u64 valueCount, indexCount;
    u32 width;
    u8 mapping;  // 'C' by polygon vertex, 'P' by control point, 'F' by polygon, 'A' all the same
    b32 indexed;
} Prog__FbxLayer;

function b32 Prog__FbxReadLayer(Arena *arena, const Prog__FbxNode *geometry, String name, String valuesName, String indexName, u32 width, Prog__FbxLayer *layer)
{
    *layer = (Prog__FbxLayer){ .width = width };
    const Prog__FbxNode *node = Prog__FbxChild(geometry->first, name);
    if (!node) return false;
    String mapping = Prog__FbxStringAt(Prog__FbxChild(node->first, S("MappingInformationType")), 0);
    String reference = Prog__FbxStringAt(Prog__FbxChild(node->first, S("ReferenceInformationType")), 0);
    layer->mapping = StrIsEqual(mapping, S("ByPolygonVertex")) ? 'C' : StrIsEqual(mapping, S("ByVertice")) || StrIsEqual(mapping, S("ByVertex")) ? 'P'
                   : StrIsEqual(mapping, S("ByPolygon")) ? 'F' : StrIsEqual(mapping, S("AllSame")) ? 'A' : 0;
    layer->indexed = StrIsEqual(reference, S("IndexToDirect")) || StrIsEqual(reference, S("Index"));
    layer->values = Prog__FbxArray(arena, Prog__FbxChild(node->first, valuesName), &layer->valueType, &layer->valueCount);
    if (indexName.length && layer->indexed) layer->indices = Prog__FbxArray(arena, Prog__FbxChild(node->first, indexName), &layer->indexType, &layer->indexCount);
    return layer->mapping && layer->values && (!layer->indexed || !indexName.length || layer->indices);
}

// The element of `layer` for the corner at polygon vertex `corner` of control point `point` in polygon `polygon`, or UINT64_MAX.
function u64 Prog__FbxLayerAt(const Prog__FbxLayer *layer, u64 corner, u64 point, u64 polygon)
{
    u64 at = layer->mapping == 'C' ? corner : layer->mapping == 'P' ? point : layer->mapping == 'F' ? polygon : 0;
    if (layer->indexed && layer->indices) {
        if (at >= layer->indexCount) return UINT64_MAX;
        i64 index = (i64)Prog__FbxElement(layer->indices, layer->indexType, at);
        at = index >= 0 ? (u64)index : UINT64_MAX;
    }
    return at < layer->valueCount / layer->width ? at : UINT64_MAX;
}

// A corner of a polygon as a vertex of the mesh: its point and its texture coordinate's and color's elements, each one past its index or 0
// for none.

// Adds `geometry`'s polygons, fanned into triangles, placed by `place`, in the parts its model's `materials` give them, grouped by part.
// A corner's control point, texture coordinate and color together make one vertex of the mesh.
function void Prog__FbxGeometry(Arena *arena, const Prog__FbxNode *geometry, const Prog__Affine *place, const Prog_MeshPart *materials, u64 materialCount, Prog_Mesh *mesh)
{
    u8 vertexType = 0, cornerType = 0;
    u64 values = 0, corners = 0;
    const u8 *vertices = Prog__FbxArray(arena, Prog__FbxChild(geometry->first, S("Vertices")), &vertexType, &values);
    const u8 *polygons = Prog__FbxArray(arena, Prog__FbxChild(geometry->first, S("PolygonVertexIndex")), &cornerType, &corners);
    u64 count = values / 3;
    if (!vertices || !polygons || (vertexType != 'd' && vertexType != 'f') || (cornerType != 'i' && cornerType != 'l') || count >= UINT32_MAX) return;
    Scratch scratch = GetScratchEx(arena);
    f32 *points = ArenaPushArrayNoZero(scratch.arena, Max(count, (u64)1) * 3, f32);
    for (u64 i = 0; i < count; ++i) {
        f32 xyz[3];
        if (vertexType == 'd') {
            f64 d[3];
            MemCopy(d, vertices + i * 24, 24);
            xyz[0] = (f32)d[0], xyz[1] = (f32)d[1], xyz[2] = (f32)d[2];
        } else {
            MemCopy(xyz, vertices + i * 12, 12);
        }
        Prog__MeshPoint(points + i * 3, place, xyz[0], xyz[1], xyz[2]);
    }
    Prog__FbxLayer uv, color, material;
    b32 mapped = Prog__FbxReadLayer(scratch.arena, geometry, S("LayerElementUV"), S("UV"), S("UVIndex"), 2, &uv) && (uv.valueType == 'd' || uv.valueType == 'f');
    b32 colored = Prog__FbxReadLayer(scratch.arena, geometry, S("LayerElementColor"), S("Colors"), S("ColorIndex"), 4, &color) && (color.valueType == 'd' || color.valueType == 'f');
    b32 materialed = materialCount && Prog__FbxReadLayer(scratch.arena, geometry, S("LayerElementMaterial"), S("Materials"), (String){0}, 1, &material) &&
                     (material.valueType == 'i' || material.valueType == 'l');
    material.indexed = false;
    // Each polygon's first corner and its part, then its triangles part by part.
    struct { ArrayHeader header; u64 *v; } starts = {0};
    for (u64 i = 0; i < corners; ++i) {
        if (!i || (i64)Prog__FbxElement(polygons, cornerType, i - 1) < 0) ArrayPush(scratch.arena, starts, i);
    }
    u64 polygonCount = starts.header.count;
    u32 *partOf = ArenaPushArray(scratch.arena, Max(polygonCount, (u64)1), u32);
    for (u64 f = 0; materialed && f < polygonCount; ++f) {
        u64 at = Prog__FbxLayerAt(&material, starts.v[f], 0, f);
        i64 index = at != UINT64_MAX ? (i64)Prog__FbxElement(material.values, material.valueType, at) : 0;
        partOf[f] = index >= 0 && (u64)index < materialCount ? (u32)index : 0;
    }
    Prog__Corners made = Prog__CornersMake(scratch.arena, count, corners);
    for (u64 part = 0; part < Max(materialCount, (u64)1); ++part) {
        Prog_MeshPart none = { .image = UINT32_MAX };
        Prog__MeshLook(arena, mesh, materialCount ? &materials[part] : &none);
        for (u64 f = 0; f < polygonCount; ++f) {
            if (partOf[f] != part) continue;
            u32 first = 0, previous = 0, at = 0;
            for (u64 i = starts.v[f]; i < corners; ++i) {
                i64 raw = (i64)Prog__FbxElement(polygons, cornerType, i);
                // A polygon's last corner is stored as its index's complement.
                b32 last = raw < 0;
                i64 index = last ? ~raw : raw;
                u32 corner = UINT32_MAX;
                if (index >= 0 && index < (i64)count) {
                    u64 uvAt = mapped ? Prog__FbxLayerAt(&uv, i, (u64)index, f) : UINT64_MAX, colorAt = colored ? Prog__FbxLayerAt(&color, i, (u64)index, f) : UINT64_MAX;
                    Prog__MeshCorner key = { (u32)index, (u32)(uvAt + 1), (u32)(colorAt + 1), 0 };
                    corner = Prog__CornersFind(&made, key);
                    if (corner == UINT32_MAX && mesh->positions.header.count / 3 < UINT32_MAX) {
                        corner = (u32)(mesh->positions.header.count / 3);
                        MemCopy(ArrayAdd(arena, mesh->positions, 3), points + index * 3, 12);
                        if (uvAt != UINT64_MAX) {
                            f32 *t = Prog__MeshUvs(arena, mesh) + (u64)corner * 2;
                            t[0] = (f32)Prog__FbxElement(uv.values, uv.valueType, uvAt * 2);
                            t[1] = 1 - (f32)Prog__FbxElement(uv.values, uv.valueType, uvAt * 2 + 1);
                        }
                        // Written as sRGB, as Blender does by default and most programs do.
                        if (colorAt != UINT64_MAX) {
                            u32 rgba = 0xFF000000u;
                            for (u32 k = 0; k < 3; ++k) rgba |= (u32)(MinF32(MaxF32((f32)Prog__FbxElement(color.values, color.valueType, colorAt * 4 + k), 0), 1) * 255 + 0.5f) << (k * 8);
                            Prog__MeshColors(arena, mesh)[corner] = rgba;
                        }
                        Prog__CornersAdd(&made, key, corner);
                    }
                }
                if (!at) first = corner;
                else if (at >= 2) Prog__MeshTriangle(arena, mesh, first, previous, corner);
                previous = corner;
                ++at;
                if (last) break;
            }
        }
    }
    ReleaseScratch(scratch);
}

typedef struct Prog__IdMap { MapHeader header; struct { i64 key; u64 value; } *v; } Prog__IdMap;

typedef struct Prog__FbxModel {
    const Prog__FbxNode *node;
    i64 parent;
} Prog__FbxModel;

// A text FBX, read into the nodes a binary one makes, each node's values written as the binary format writes its properties: numbers with
// a point or an exponent as doubles and the rest as 64-bit integers, words and quoted text as strings, and an array, "*N { a: ... }", as a
// plain array of doubles, or of integers for a list of indices. Only FBX 7's layout, as the binary one has, is understood after.
typedef struct Prog__FbxText { const char *at, *end; } Prog__FbxText;

// Past spaces, line ends and comments, which run from ';' to the end of the line.
function void Prog__FbxTextSkip(Prog__FbxText *text)
{
    while (text->at < text->end) {
        char c = *text->at;
        if (c == ';') {
            while (text->at < text->end && *text->at != '\n') ++text->at;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++text->at;
        } else {
            break;
        }
    }
}

function b32 Prog__FbxTextWordByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+' || c == '.' || c == '|';
}

function String Prog__FbxTextWord(Prog__FbxText *text)
{
    const char *from = text->at;
    while (text->at < text->end && Prog__FbxTextWordByte(*text->at)) ++text->at;
    return StrMake(from, (u64)(text->at - from));
}

// Whether `word` is a number, and whether it has a fraction or an exponent; its value as an integer and as a double.
function b32 Prog__FbxTextNumber(String word, b32 *real, i64 *integer, f64 *value)
{
    const char *p = word.value, *end = word.value + word.length;
    if (!word.length || !Prog__Number(&p, end, value) || p != end) return false;
    *real = StrIndexChar(word, '.', SSF_None).exists || StrIndexChar(word, 'e', SSF_None).exists || StrIndexChar(word, 'E', SSF_None).exists;
    // Ids are 64-bit integers, more than a double holds exactly.
    i64 sum = 0;
    b32 negative = word.value[0] == '-';
    for (u64 i = word.value[0] == '-' || word.value[0] == '+'; !*real && i < word.length; ++i) sum = sum * 10 + (word.value[i] - '0');
    *integer = negative ? -sum : sum;
    return true;
}

function void Prog__FbxTextBytes(Arena *arena, StrBuilder *blob, const void *bytes, u64 length)
{
    StrBuildStr(arena, blob, StrMake((const char *)bytes, length));
}

function void Prog__FbxTextString(Arena *arena, StrBuilder *blob, String text)
{
    u32 length = (u32)text.length;
    StrBuildChar(arena, blob, 'S');
    Prog__FbxTextBytes(arena, blob, &length, 4);
    StrBuildStr(arena, blob, text);
}

// An array's numbers, from after its "*N" to past its closing brace.
function void Prog__FbxTextArray(Arena *arena, Prog__FbxText *text, String name, StrBuilder *blob)
{
    Scratch scratch = GetScratchEx(arena);
    struct { ArrayHeader header; f64 *v; } reals = {0};
    struct { ArrayHeader header; i64 *v; } integers = {0};
    b32 whole = true;
    Prog__FbxTextSkip(text);
    if (text->at < text->end && *text->at == '{') {
        ++text->at;
        Prog__FbxTextSkip(text);
        Prog__FbxTextWord(text);
        Prog__FbxTextSkip(text);
        if (text->at < text->end && *text->at == ':') ++text->at;
        for (;;) {
            Prog__FbxTextSkip(text);
            if (text->at >= text->end || *text->at == '}') break;
            if (*text->at == ',') {
                ++text->at;
                continue;
            }
            String word = Prog__FbxTextWord(text);
            b32 real = false;
            i64 integer = 0;
            f64 value = 0;
            if (!word.length || !Prog__FbxTextNumber(word, &real, &integer, &value)) {
                if (!word.length) ++text->at;
                continue;
            }
            whole &= !real;
            ArrayPush(scratch.arena, reals, value);
            ArrayPush(scratch.arena, integers, integer);
        }
        if (text->at < text->end) ++text->at;
    }
    // Lists of indices are integers, as the binary format writes them; coordinates whose numbers happen to be whole stay doubles.
    b32 indices = (name.length >= 5 && StrIsEqual(StrFrom(name, name.length - 5), S("Index"))) || StrIsEqual(name, S("Materials")) || StrIsEqual(name, S("Edges"));
    whole &= indices;
    u32 count = (u32)reals.header.count, encoding = 0, length = count * 8;
    StrBuildChar(arena, blob, whole ? 'l' : 'd');
    Prog__FbxTextBytes(arena, blob, &count, 4);
    Prog__FbxTextBytes(arena, blob, &encoding, 4);
    Prog__FbxTextBytes(arena, blob, &length, 4);
    if (count) Prog__FbxTextBytes(arena, blob, whole ? (const void *)integers.v : (const void *)reals.v, length);
    ReleaseScratch(scratch);
}

// The nodes up to the brace that closes the list, or the end of the file at the top.
function Prog__FbxNode *Prog__FbxTextList(Arena *arena, Prog__FbxText *text, u32 depth)
{
    Prog__FbxNode *first = NULL, *last = NULL;
    while (depth < 64) {
        Prog__FbxTextSkip(text);
        if (text->at >= text->end) break;
        if (*text->at == '}') {
            ++text->at;
            break;
        }
        String name = Prog__FbxTextWord(text);
        Prog__FbxTextSkip(text);
        if (!name.length || text->at >= text->end || *text->at != ':') {
            // Something not understood is passed over.
            if (text->at < text->end) ++text->at;
            continue;
        }
        ++text->at;
        StrBuilder blob = {0};
        for (;;) {
            Prog__FbxTextSkip(text);
            if (text->at >= text->end || *text->at == '{' || *text->at == '}') break;
            if (*text->at == '"') {
                const char *from = ++text->at;
                while (text->at < text->end && *text->at != '"') ++text->at;
                Prog__FbxTextString(arena, &blob, StrMake(from, (u64)(text->at - from)));
                if (text->at < text->end) ++text->at;
            } else if (*text->at == '*') {
                ++text->at;
                Prog__FbxTextWord(text);
                Prog__FbxTextArray(arena, text, name, &blob);
                break;
            } else {
                // A word followed by a colon names the next node; this one had no values.
                const char *back = text->at;
                String word = Prog__FbxTextWord(text);
                if (!word.length) {
                    ++text->at;
                    continue;
                }
                const char *after = text->at;
                Prog__FbxTextSkip(text);
                if (text->at < text->end && *text->at == ':') {
                    text->at = back;
                    break;
                }
                text->at = after;
                b32 real = false;
                i64 integer = 0;
                f64 value = 0;
                if (Prog__FbxTextNumber(word, &real, &integer, &value)) {
                    StrBuildChar(arena, &blob, real ? 'D' : 'L');
                    if (real) Prog__FbxTextBytes(arena, &blob, &value, 8);
                    else Prog__FbxTextBytes(arena, &blob, &integer, 8);
                } else {
                    Prog__FbxTextString(arena, &blob, word);
                }
            }
            Prog__FbxTextSkip(text);
            if (text->at < text->end && *text->at == ',') ++text->at;
            else break;
        }
        Prog__FbxNode *node = ArenaPushStruct(arena, Prog__FbxNode);
        node->name = name;
        node->properties = (const u8 *)blob.buffer.value;
        node->propertiesEnd = node->properties + blob.buffer.length;
        Prog__FbxTextSkip(text);
        if (text->at < text->end && *text->at == '{') {
            ++text->at;
            node->first = Prog__FbxTextList(arena, text, depth + 1);
        }
        if (last) last->next = node;
        else first = node;
        last = node;
    }
    return first;
}

function b32 Prog__ReadFbx(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    const u8 *file = (const u8 *)data.value;
    Prog__FbxNode *top = NULL;
    if (data.length >= 27 && MemCompare(file, "Kaydara FBX Binary  \0", 21) == 0) {
        u32 version = 0;
        MemCopy(&version, file + 23, 4);
        top = Prog__FbxList(arena, file, file + 27, file + data.length, version >= 7500, 0);
    } else if (StrIndexStr(StrFromTo(data, 0, (i64)Min(data.length, (u64)4096)), S("FBXHeaderExtension"), SSF_None).exists) {
        Prog__FbxText text = { data.value, data.value + data.length };
        top = Prog__FbxTextList(arena, &text, 0);
    } else {
        return false;
    }
    Prog__FbxNode *up = Prog__FbxSetting(Prog__FbxChild(top, S("GlobalSettings")), S("UpAxis"));
    mesh->zUp = up && Prog__FbxNumberAt(up, 4) == 2;
    Prog__FbxNode *objects = Prog__FbxChild(top, S("Objects")), *connections = Prog__FbxChild(top, S("Connections"));
    if (!objects) return false;
    struct { ArrayHeader header; Prog__FbxModel *v; } models = {0};
    struct { ArrayHeader header; const Prog__FbxNode **v; } geometries = {0}, materialNodes = {0}, textures = {0}, videos = {0};
    Prog__IdMap modelAt = {0}, geometryAt = {0}, materialAt = {0}, textureAt = {0}, videoAt = {0};
    for (Prog__FbxNode *object = objects->first; object; object = object->next) {
        if (StrIsEqual(object->name, S("Model"))) {
            MapPut(arena, modelAt, Prog__FbxIdAt(object, 0), models.header.count);
            ArrayPush(arena, models, ((Prog__FbxModel){ .node = object }));
        } else if (StrIsEqual(object->name, S("Geometry")) && Prog__FbxChild(object->first, S("Vertices"))) {
            MapPut(arena, geometryAt, Prog__FbxIdAt(object, 0), geometries.header.count);
            ArrayPush(arena, geometries, object);
        } else if (StrIsEqual(object->name, S("Material"))) {
            MapPut(arena, materialAt, Prog__FbxIdAt(object, 0), materialNodes.header.count);
            ArrayPush(arena, materialNodes, object);
        } else if (StrIsEqual(object->name, S("Texture"))) {
            MapPut(arena, textureAt, Prog__FbxIdAt(object, 0), textures.header.count);
            ArrayPush(arena, textures, object);
        } else if (StrIsEqual(object->name, S("Video"))) {
            MapPut(arena, videoAt, Prog__FbxIdAt(object, 0), videos.header.count);
            ArrayPush(arena, videos, object);
        }
    }
    u64 materialCount = materialNodes.header.count, textureCount = textures.header.count;
    b32 *placed = ArenaPushArray(arena, geometries.header.count + 1, b32);
    // Each material's diffuse texture and each texture's video, by index, UINT64_MAX for none; each model's materials in their order.
    u64 *diffuse = ArenaPushArrayNoZero(arena, materialCount + 1, u64), *video = ArenaPushArrayNoZero(arena, textureCount + 1, u64);
    for (u64 i = 0; i <= materialCount; ++i) diffuse[i] = UINT64_MAX;
    for (u64 i = 0; i <= textureCount; ++i) video[i] = UINT64_MAX;
    struct { ArrayHeader header; u64 *v; } uses = {0}, owned = {0};  // a geometry's index and its model's, and a material's and its model's, in pairs
    for (Prog__FbxNode *link = connections ? connections->first : NULL; link; link = link->next) {
        String kind = Prog__FbxStringAt(link, 0);
        i64 child = Prog__FbxIdAt(link, 1), parent = Prog__FbxIdAt(link, 2);
        u64 model = 0, geometry = 0, parentModel = 0, from = 0, to = 0;
        if (StrIsEqual(kind, S("OP"))) {
            String property = Prog__FbxStringAt(link, 3);
            if ((StrIsEqual(property, S("DiffuseColor")) || StrIsEqual(property, S("Maya|baseColor")) || StrIsEqual(property, S("3dsMax|Parameters|base_color_map"))) &&
                MapTryGet(textureAt, child, &from) && MapTryGet(materialAt, parent, &to) && diffuse[to] == UINT64_MAX) diffuse[to] = from;
            continue;
        }
        if (!StrIsEqual(kind, S("OO"))) continue;
        if (MapTryGet(videoAt, child, &from) && MapTryGet(textureAt, parent, &to)) video[to] = from;
        if (!MapTryGet(modelAt, parent, &parentModel)) continue;
        if (MapTryGet(modelAt, child, &model)) {
            models.v[model].parent = parent;
        } else if (MapTryGet(geometryAt, child, &geometry)) {
            ArrayPush(arena, uses, geometry);
            ArrayPush(arena, uses, parentModel);
            placed[geometry] = true;
        } else if (MapTryGet(materialAt, child, &from)) {
            ArrayPush(arena, owned, from);
            ArrayPush(arena, owned, parentModel);
        }
    }
    // Each material's color and picture: the diffuse color, linear, and its texture's video's own bytes or the file it names.
    Prog_MeshPart *looks = ArenaPushArray(arena, materialCount + 1, Prog_MeshPart);
    for (u64 i = 0; i < materialCount; ++i) {
        f32 rgb[3] = { 1, 1, 1 };
        if (Prog__FbxSetting(materialNodes.v[i], S("DiffuseColor"))) Prog__FbxVector(materialNodes.v[i], S("DiffuseColor"), rgb);
        else Prog__FbxVector(materialNodes.v[i], S("Diffuse"), rgb);
        looks[i] = (Prog_MeshPart){ .color = Prog__LinearColor(rgb[0], rgb[1], rgb[2], 1), .image = UINT32_MAX };
        Prog__FbxNode *opacity = Prog__FbxSetting(materialNodes.v[i], S("Opacity")), *transparency = Prog__FbxSetting(materialNodes.v[i], S("TransparencyFactor"));
        if (opacity || transparency) Prog__MeshAlpha(&looks[i], opacity ? (f32)Prog__FbxNumberAt(opacity, 4) : 1 - (f32)Prog__FbxNumberAt(transparency, 4));
        if (diffuse[i] == UINT64_MAX) continue;
        const Prog__FbxNode *texture = textures.v[diffuse[i]], *clip = video[diffuse[i]] != UINT64_MAX ? videos.v[video[diffuse[i]]] : NULL;
        String relative = Prog__FbxStringAt(Prog__FbxChild(texture->first, S("RelativeFilename")), 0), absolute = Prog__FbxStringAt(Prog__FbxChild(texture->first, S("FileName")), 0);
        Prog__FbxProperty content = {0};
        if (clip && Prog__FbxPropertyAt(Prog__FbxChild(clip->first, S("Content")), 0, &content) && content.type == 'R' && content.length) {
            looks[i].image = Prog__MeshImage(arena, mesh, relative.length ? relative : absolute, StrMake((const char *)content.data, content.length));
        }
        if (looks[i].image == UINT32_MAX && relative.length) looks[i].image = Prog__MeshImage(arena, mesh, relative, (String){0});
        if (looks[i].image == UINT32_MAX && absolute.length) looks[i].image = Prog__MeshImage(arena, mesh, absolute, (String){0});
        // A picture is drawn as it is.
        if (looks[i].image != UINT32_MAX) looks[i].bare = looks[i].color, looks[i].color = 0;
    }
    Prog__FbxInflateAll(arena, (Prog__FbxNode **)geometries.v, geometries.header.count);
    for (u64 i = 0; i + 1 < uses.header.count && !P_IsCancelled(cancel); i += 2) {
        const Prog__FbxModel *model = &models.v[uses.v[i + 1]];
        // The model's materials, in the order they were connected, which the geometry's material indices count in.
        struct { ArrayHeader header; Prog_MeshPart *v; } own = {0};
        for (u64 k = 0; k + 1 < owned.header.count; k += 2) if (owned.v[k + 1] == uses.v[i + 1]) ArrayPush(arena, own, looks[owned.v[k]]);
        Prog__Affine place = Prog__AffineMultiply(Prog__FbxPlacement(model->node, false), Prog__FbxPlacement(model->node, true));
        u64 above = 0;
        for (u32 depth = 0; depth < 64 && model->parent && MapTryGet(modelAt, model->parent, &above); ++depth) {
            model = &models.v[above];
            place = Prog__AffineMultiply(Prog__FbxPlacement(model->node, false), place);
        }
        Prog__FbxGeometry(arena, geometries.v[uses.v[i]], &place, own.v, own.header.count, mesh);
    }
    for (u64 i = 0; i < geometries.header.count && !P_IsCancelled(cancel); ++i) if (!placed[i]) Prog__FbxGeometry(arena, geometries.v[i], &Prog__AffineIdentity, NULL, 0, mesh);
    return !P_IsCancelled(cancel) && mesh->indices.header.count != 0;
}

// -----------------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Preview
// -----------------------------------------------------------------------------
// Blender: the meshes of a .blend's mesh objects, placed by their location, rotation, scale and parents, without their modifiers.
// A .blend holds its structs as Blender laid them out, and describes them in its SDNA block, so fields are found by name there rather than
// at offsets of one version. Blender 5 keeps a mesh's positions and corners as named attributes, Blender 3.4 to 4.x as named custom data
// layers, and older ones in vertex, face and loop structs; all three are read. Files saved compressed are zstd frames, or gzip before 3.0.

typedef struct Prog__BlendBlock { u64 old; const u8 *data; u64 length; u32 sdna; u32 count; u32 code; } Prog__BlendBlock;
typedef struct Prog__ArrayBlendBlock { ArrayHeader header; Prog__BlendBlock *v; } Prog__ArrayBlendBlock;
typedef struct Prog__BlendIndex { MapHeader header; struct { u64 key; u32 value; } *v; } Prog__BlendIndex;

typedef struct Prog__Blend {
    u32 pointer;           // 4 or 8 bytes
    Prog__ArrayBlendBlock blocks;  // in the file's order
    Prog__BlendIndex byOld;        // each block's index by the address it had when it was written, which pointers in other blocks name
    // The SDNA: field names, type names and sizes, and each struct's type and fields as (type, name) pairs.
    String *names, *types;
    u16 *typeSizes;
    u16 **structs;
    u32 nameCount, typeCount, structCount;
} Prog__Blend;

#define Prog__BlendCode(a, b, c, d) ((u32)(a) | (u32)(b) << 8 | (u32)(c) << 16 | (u32)(d) << 24)

function u32 Prog__Read32(const u8 *at) { return (u32)at[0] | (u32)at[1] << 8 | (u32)at[2] << 16 | (u32)at[3] << 24; }
function u64 Prog__Read64(const u8 *at) { return (u64)Prog__Read32(at) | (u64)Prog__Read32(at + 4) << 32; }

// Blender's zstd frames, each a megabyte or so unpacked, shared out to lanes as they finish the last.
typedef struct Prog__BlendFrames {
    const u8 *in;
    u8 *out;
    const u64 *from, *to;  // each frame's place in the file and in the output, with one past the last
    u64 count;
    atomic_uint_fast64_t next;
    atomic_bool failed;
} Prog__BlendFrames;

function void Prog__BlendFrameLane(void *user, u32 lane)
{
    (void)lane;
    Prog__BlendFrames *frames = user;
    jmp_buf failed;
    zstd_error_jump = &failed;
    if (setjmp(failed)) {
        atomic_store(&frames->failed, true);
        return;
    }
    for (u64 i; !atomic_load(&frames->failed) && (i = atomic_fetch_add(&frames->next, 1)) < frames->count;) {
        u64 unpacked = frames->to[i + 1] - frames->to[i];
        if (ZSTD_decompress(frames->out + frames->to[i], unpacked, frames->in + frames->from[i], frames->from[i + 1] - frames->from[i]) != unpacked) atomic_store(&frames->failed, true);
    }
}

// The decompressed file: zstd frames laid out by the seek table at their end, as Blender writes them, or one frame; gzip; or as it is.
function String Prog__BlendInflate(Arena *arena, String data)
{
    const u8 *d = (const u8 *)data.value;
    if (data.length >= 4 && Prog__Read32(d) == 0xFD2FB528) {
        // The seek table's footer: its frame count, a descriptor whose high bit adds checksums to its entries, and its magic.
        u64 frames = 0, entry = 8, table = 0;
        if (data.length >= 9 && Prog__Read32(d + data.length - 4) == 0x8F92EAB1) {
            frames = Prog__Read32(d + data.length - 9);
            entry = (d[data.length - 5] & 0x80) ? 12 : 8;
            if (frames * entry + 9 + 8 > data.length) frames = 0;
            else table = data.length - 9 - frames * entry;
        }
        u64 total = 0;
        for (u64 i = 0; i < frames; ++i) total += Prog__Read32(d + table + i * entry + 4);
        if (!frames) total = ZSTD_get_decompressed_size(data.value, data.length);
        if (!total || total == (u64)-1 || total > Prog_ModelMaxBytes * 4) return (String){0};
        u8 *out = ArenaPushArrayNoZero(arena, total, u8);
        jmp_buf failed;
        zstd_error_jump = &failed;
        if (setjmp(failed)) return (String){0};
        if (!frames) return StrMake((const char *)out, ZSTD_decompress(out, total, data.value, data.length));
        Scratch scratch = GetScratchEx(arena);
        u64 *from = ArenaPushArrayNoZero(scratch.arena, frames + 1, u64), *to = ArenaPushArrayNoZero(scratch.arena, frames + 1, u64);
        from[0] = to[0] = 0;
        for (u64 i = 0; i < frames; ++i) {
            from[i + 1] = from[i] + Prog__Read32(d + table + i * entry);
            to[i + 1] = to[i] + Prog__Read32(d + table + i * entry + 4);
        }
        Prog__BlendFrames work = { .in = d, .out = out, .from = from, .to = to, .count = frames };
        if (from[frames] > table) work.failed = true;
        // The frames stand alone, so they unpack side by side.
        else P_RunLanes((u32)Min((u64)Max(1u, Min(P_ProcessorCount(), 8u)), frames), Prog__BlendFrameLane, &work);
        ReleaseScratch(scratch);
        return atomic_load(&work.failed) ? (String){0} : StrMake((const char *)out, total);
    }
    if (data.length >= 18 && d[0] == 0x1F && d[1] == 0x8B && d[2] == 8) {
        // gzip: its header, with the optional parts its flags name, then deflate; the last 4 bytes hold the size modulo 4 GB.
        u8 flags = d[3];
        u64 at = 10;
        if (flags & 4) at += 2 + (u64)(d[at] | d[at + 1] << 8);
        if (flags & 8) while (at < data.length && d[at++]) {}
        if (flags & 16) while (at < data.length && d[at++]) {}
        if (flags & 2) at += 2;
        u64 size = Prog__Read32(d + data.length - 4);
        if (at >= data.length || !size || size > Prog_ModelMaxBytes * 4) return (String){0};
        char *out = ArenaPushArrayNoZero(arena, size, char);
        i32 made = stbi_zlib_decode_noheader_buffer(out, (i32)size, (const char *)d + at, (i32)(data.length - at));
        return made > 0 ? StrMake(out, (u64)made) : (String){0};
    }
    return data;
}

// The block an old pointer was written for; NULL for a null pointer or one into no block. Blender 5 reuses addresses from one ID's data to
// the next, so a pointer in `owner` or its data is looked for among the data blocks written after it, up to the next ID, and else among
// the IDs.
function const Prog__BlendBlock *Prog__BlendAt(const Prog__Blend *blend, const Prog__BlendBlock *owner, u64 old)
{
    if (!old) return NULL;
    for (const Prog__BlendBlock *at = owner ? owner + 1 : NULL, *end = blend->blocks.v + blend->blocks.header.count; at && at < end && at->code == Prog__BlendCode('D', 'A', 'T', 'A'); ++at) {
        if (at->old == old) return at;
    }
    const u32 *index = MapGetPtrNull(blend->byOld, old);
    return index ? &blend->blocks.v[*index] : NULL;
}

// The struct type `name`'s index among the SDNA's structs, or UINT32_MAX.
function u32 Prog__BlendStruct(const Prog__Blend *blend, String name)
{
    for (u32 i = 0; i < blend->structCount; ++i) if (StrIsEqual(blend->types[blend->structs[i][0]], name)) return i;
    return UINT32_MAX;
}

// The offset and size in struct `index` of the field named `name`, without its pointer stars or array sizes; false when it has none.
function b32 Prog__BlendField(const Prog__Blend *blend, u32 index, String name, u64 *offset, u64 *size)
{
    if (index >= blend->structCount) return false;
    const u16 *s = blend->structs[index];
    u64 at = 0;
    for (u32 f = 0; f < s[1]; ++f) {
        String full = blend->names[s[3 + 2 * f]];
        u64 from = 0;
        while (from < full.length && (full.value[from] == '*' || full.value[from] == '(')) ++from;
        u64 to = from;
        while (to < full.length && full.value[to] != '[' && full.value[to] != ')') ++to;
        u64 count = 1;
        for (u64 k = to; k < full.length; ++k) {
            if (full.value[k] != '[') continue;
            u64 n = 0;
            while (++k < full.length && full.value[k] >= '0' && full.value[k] <= '9') n = n * 10 + (u64)(full.value[k] - '0');
            count *= Max(n, (u64)1);
        }
        b32 pointer = full.length && (full.value[0] == '*' || full.value[0] == '(');
        u64 bytes = (pointer ? blend->pointer : blend->typeSizes[s[2 + 2 * f]]) * count;
        if (StrIsEqual(StrFromTo(full, from, (i64)to), name)) {
            *offset = at, *size = bytes;
            return true;
        }
        at += bytes;
    }
    return false;
}

// A field of the struct at `base`, `limit` bytes long, read as a number, a pointer or floats; 0 when it is missing or past the end.
function u64 Prog__BlendPointer(const Prog__Blend *blend, u32 type, const u8 *base, u64 limit, String name)
{
    u64 offset, size;
    if (!Prog__BlendField(blend, type, name, &offset, &size) || offset + blend->pointer > limit) return 0;
    return blend->pointer == 8 ? Prog__Read64(base + offset) : Prog__Read32(base + offset);
}

function i64 Prog__BlendInt(const Prog__Blend *blend, u32 type, const u8 *base, u64 limit, String name)
{
    u64 offset, size;
    if (!Prog__BlendField(blend, type, name, &offset, &size) || offset + size > limit) return 0;
    if (size == 1) return (i8)base[offset];
    if (size == 2) return (i16)(base[offset] | base[offset + 1] << 8);
    if (size == 4) return (i32)Prog__Read32(base + offset);
    return (i64)Prog__Read64(base + offset);
}

function b32 Prog__BlendFloats(const Prog__Blend *blend, u32 type, const u8 *base, u64 limit, String name, f32 *out, u32 count)
{
    u64 offset, size;
    if (!Prog__BlendField(blend, type, name, &offset, &size) || size < count * 4 || offset + size > limit) return false;
    MemCopy(out, base + offset, count * 4);
    return true;
}

function b32 Prog__BlendParse(Arena *arena, String data, Prog__Blend *blend)
{
    const u8 *d = (const u8 *)data.value;
    if (data.length < 12 || !StrIsEqual(StrFromTo(data, 0, 7), S("BLENDER"))) return false;
    // Blender 5: "BLENDER17-01v0502", the header's size, its format and the version; before it "BLENDER-v402", the pointer size and the byte order.
    u64 at;
    b32 large = false;
    if (d[7] >= '0' && d[7] <= '9') {
        at = (u64)(d[7] - '0') * 10 + (u64)(d[8] - '0');
        large = d[10] == '0' && d[11] == '1';
        if (d[12] != 'v' || !large) return false;
        blend->pointer = 8;
    } else {
        if (d[8] != 'v') return false;
        at = 12;
        blend->pointer = d[7] == '-' ? 8 : 4;
    }
    const u8 *dna = NULL;
    u64 dnaLength = 0;
    u64 head = large ? 32 : blend->pointer == 8 ? 24 : 20;
    while (at + head <= data.length) {
        const u8 *h = d + at;
        Prog__BlendBlock block = { .code = Prog__Read32(h) };
        if (large) {
            block.sdna = Prog__Read32(h + 4), block.old = Prog__Read64(h + 8), block.length = Prog__Read64(h + 16), block.count = (u32)Prog__Read64(h + 24);
        } else {
            block.length = Prog__Read32(h + 4);
            block.old = blend->pointer == 8 ? Prog__Read64(h + 8) : Prog__Read32(h + 8);
            block.sdna = Prog__Read32(h + head - 8), block.count = Prog__Read32(h + head - 4);
        }
        if (at + head + block.length > data.length) break;
        block.data = d + at + head;
        at += head + block.length;
        if (block.code == Prog__BlendCode('E', 'N', 'D', 'B')) break;
        if (block.code == Prog__BlendCode('D', 'N', 'A', '1')) dna = block.data, dnaLength = block.length;
        ArrayPush(arena, blend->blocks, block);
    }
    if (!dna || dnaLength < 16 || Prog__Read32(dna) != Prog__BlendCode('S', 'D', 'N', 'A')) return false;
    // NAME, TYPE, TLEN and STRC, each four-byte aligned.
    u64 p = 4;
    if (Prog__Read32(dna + p) != Prog__BlendCode('N', 'A', 'M', 'E')) return false;
    blend->nameCount = Prog__Read32(dna + p + 4), p += 8;
    blend->names = ArenaPushArray(arena, Max(blend->nameCount, 1u), String);
    for (u32 i = 0; i < blend->nameCount && p < dnaLength; ++i) {
        u64 end = p;
        while (end < dnaLength && dna[end]) ++end;
        blend->names[i] = StrMake((const char *)dna + p, end - p), p = end + 1;
    }
    p = (p + 3) & ~(u64)3;
    if (p + 8 > dnaLength || Prog__Read32(dna + p) != Prog__BlendCode('T', 'Y', 'P', 'E')) return false;
    blend->typeCount = Prog__Read32(dna + p + 4), p += 8;
    blend->types = ArenaPushArray(arena, Max(blend->typeCount, 1u), String);
    for (u32 i = 0; i < blend->typeCount && p < dnaLength; ++i) {
        u64 end = p;
        while (end < dnaLength && dna[end]) ++end;
        blend->types[i] = StrMake((const char *)dna + p, end - p), p = end + 1;
    }
    p = (p + 3) & ~(u64)3;
    if (p + 4 + 2 * (u64)blend->typeCount > dnaLength || Prog__Read32(dna + p) != Prog__BlendCode('T', 'L', 'E', 'N')) return false;
    blend->typeSizes = ArenaPushArray(arena, Max(blend->typeCount, 1u), u16);
    for (u32 i = 0; i < blend->typeCount; ++i) blend->typeSizes[i] = (u16)(dna[p + 4 + 2 * i] | dna[p + 5 + 2 * i] << 8);
    p = (p + 4 + 2 * (u64)blend->typeCount + 3) & ~(u64)3;
    if (p + 8 > dnaLength || Prog__Read32(dna + p) != Prog__BlendCode('S', 'T', 'R', 'C')) return false;
    blend->structCount = Prog__Read32(dna + p + 4), p += 8;
    // Each struct as its type, its field count and its fields' types and names, the indices checked against the lists.
    blend->structs = ArenaPushArray(arena, Max(blend->structCount, 1u), u16 *);
    for (u32 i = 0; i < blend->structCount; ++i) {
        if (p + 4 > dnaLength) return false;
        u32 fields = (u32)(dna[p + 2] | dna[p + 3] << 8);
        if (p + 4 + 4 * (u64)fields > dnaLength) return false;
        u16 *s = blend->structs[i] = ArenaPushArray(arena, 2 + 2 * (u64)fields, u16);
        for (u32 k = 0; k < 2 + 2 * fields; ++k) s[k] = (u16)(dna[p + 2 * k] | dna[p + 2 * k + 1] << 8);
        if (s[0] >= blend->typeCount) return false;
        for (u32 f = 0; f < fields; ++f) if (s[2 + 2 * f] >= blend->typeCount || s[3 + 2 * f] >= blend->nameCount) return false;
        p += 4 + 4 * (u64)fields;
    }
    // Only the IDs, whose addresses stay unique; data blocks are found among their owner's.
    for (u64 i = 0; i < blend->blocks.header.count; ++i) {
        const Prog__BlendBlock *block = &blend->blocks.v[i];
        if (block->old && block->code != Prog__BlendCode('D', 'A', 'T', 'A')) MapPut(arena, blend->byOld, block->old, (u32)i);
    }
    return true;
}

// The text at `at` up to its first zero byte, at most `limit` long.
function String Prog__BlendName(const u8 *at, u64 limit)
{
    u64 length = 0;
    while (length < limit && at[length]) ++length;
    return StrMake((const char *)at, length);
}

// Attribute domains, as Blender 5 numbers them and as the custom data stores hold them before it.
enum { Prog__BlendOnPoint = 0, Prog__BlendOnFace = 2, Prog__BlendOnCorner = 3 };

function const Prog__BlendBlock *Prog__BlendAttribute(const Prog__Blend *blend, const Prog__BlendBlock *owner, u32 meshType, String store, String name, i32 *domain);

// The data an attribute or a custom data layer named `name` points at, at least `bytes` long.
function const u8 *Prog__BlendLayer(const Prog__Blend *blend, const Prog__BlendBlock *owner, u32 meshType, String store, String name, u64 bytes)
{
    i32 domain = 0;
    const Prog__BlendBlock *values = Prog__BlendAttribute(blend, owner, meshType, store, name, &domain);
    return values && values->length >= bytes ? values->data : NULL;
}

// The block of the data an attribute or custom data layer named `name` points at, with its domain: Blender 5's attributes hold an array
// struct and say their domain, and before it the store `store` it is in says it, or each of the four when `store` is empty.
function const Prog__BlendBlock *Prog__BlendAttribute(const Prog__Blend *blend, const Prog__BlendBlock *owner, u32 meshType, String store, String name, i32 *domain)
{
    const u8 *mesh = owner->data;
    u64 meshLength = owner->length;
    // Blender 5: the mesh's attribute storage, an array of attributes, each with its name, domain and an AttributeArray holding the data.
    u32 storageType = Prog__BlendStruct(blend, S("AttributeStorage")), attributeType = Prog__BlendStruct(blend, S("Attribute"));
    u32 arrayType = Prog__BlendStruct(blend, S("AttributeArray"));
    u64 storageAt, storageSize;
    if (storageType != UINT32_MAX && attributeType != UINT32_MAX && arrayType != UINT32_MAX && Prog__BlendField(blend, meshType, S("attribute_storage"), &storageAt, &storageSize) &&
        storageAt + storageSize <= meshLength) {
        const u8 *storage = mesh + storageAt;
        const Prog__BlendBlock *list = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, storageType, storage, storageSize, S("dna_attributes")));
        i64 count = Prog__BlendInt(blend, storageType, storage, storageSize, S("dna_attributes_num"));
        u64 stride = blend->typeSizes[blend->structs[attributeType][0]];
        for (i64 i = 0; list && i < count && (u64)(i + 1) * stride <= list->length; ++i) {
            const u8 *attribute = list->data + (u64)i * stride;
            const Prog__BlendBlock *named = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, attributeType, attribute, stride, S("name")));
            if (!named || !StrIsEqual(Prog__BlendName(named->data, named->length), name)) continue;
            if (Prog__BlendInt(blend, attributeType, attribute, stride, S("storage_type")) != 0) return NULL;
            *domain = (i32)Prog__BlendInt(blend, attributeType, attribute, stride, S("domain"));
            const Prog__BlendBlock *array = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, attributeType, attribute, stride, S("data")));
            return array ? Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, arrayType, array->data, array->length, S("data"))) : NULL;
        }
    }
    // Blender 3.4 to 4.x: the custom data `store`'s layers, each with its name and data.
    u32 customType = Prog__BlendStruct(blend, S("CustomData")), layerType = Prog__BlendStruct(blend, S("CustomDataLayer"));
    u64 nameAt, nameSize;
    if (customType == UINT32_MAX || layerType == UINT32_MAX || !Prog__BlendField(blend, layerType, S("name"), &nameAt, &nameSize)) return NULL;
    local_persist const String stores[] = { SInit("vdata"), SInit("pdata"), SInit("ldata") };
    local_persist const i32 domains[] = { Prog__BlendOnPoint, Prog__BlendOnFace, Prog__BlendOnCorner };
    for (u32 s = 0; s < CArrayCount(stores); ++s) {
        u64 customAt, customSize;
        if ((store.length && !StrIsEqual(store, stores[s])) || !Prog__BlendField(blend, meshType, stores[s], &customAt, &customSize) || customAt + customSize > meshLength) continue;
        const u8 *custom = mesh + customAt;
        const Prog__BlendBlock *layers = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, customType, custom, customSize, S("layers")));
        i64 count = Prog__BlendInt(blend, customType, custom, customSize, S("totlayer"));
        u64 stride = blend->typeSizes[blend->structs[layerType][0]];
        for (i64 i = 0; layers && i < count && (u64)(i + 1) * stride <= layers->length; ++i) {
            const u8 *layer = layers->data + (u64)i * stride;
            if (!StrIsEqual(Prog__BlendName(layer + nameAt, nameSize), name)) continue;
            *domain = domains[s];
            return Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, layerType, layer, stride, S("data")));
        }
    }
    return NULL;
}

// The text of a char array field, as a node's or socket's name.
function String Prog__BlendText(const Prog__Blend *blend, u32 type, const u8 *base, u64 limit, String name)
{
    u64 offset, size;
    return Prog__BlendField(blend, type, name, &offset, &size) && offset + size <= limit ? Prog__BlendName(base + offset, size) : (String){0};
}

// The picture of image block `image`: its packed file's bytes, or the file it names, which a path from "//" is next to the .blend.
function u32 Prog__BlendImage(Arena *arena, const Prog__Blend *blend, const Prog__BlendBlock *image, Prog_Mesh *mesh)
{
    u32 imageType = Prog__BlendStruct(blend, S("Image")), packedType = Prog__BlendStruct(blend, S("PackedFile")), entryType = Prog__BlendStruct(blend, S("ImagePackedFile"));
    if (!image || image->sdna != imageType || !mesh->imageSide) return UINT32_MAX;
    String file = Prog__BlendText(blend, imageType, image->data, image->length, S("filepath"));
    if (!file.length) file = Prog__BlendText(blend, imageType, image->data, image->length, S("name"));
    // Packed since 2.8 as a list of files, one for each view or tile, and as one file before.
    const Prog__BlendBlock *packed = Prog__BlendAt(blend, image, Prog__BlendPointer(blend, imageType, image->data, image->length, S("packedfile")));
    const Prog__BlendBlock *entry = entryType != UINT32_MAX ? Prog__BlendAt(blend, image, Prog__BlendPointer(blend, imageType, image->data, image->length, S("packedfiles"))) : NULL;
    if (!packed && entry) packed = Prog__BlendAt(blend, image, Prog__BlendPointer(blend, entryType, entry->data, entry->length, S("packedfile")));
    if (packed && packedType != UINT32_MAX) {
        i64 size = Prog__BlendInt(blend, packedType, packed->data, packed->length, S("size"));
        const Prog__BlendBlock *bytes = Prog__BlendAt(blend, image, Prog__BlendPointer(blend, packedType, packed->data, packed->length, S("data")));
        if (bytes && size > 0 && (u64)size <= bytes->length) return Prog__MeshImage(arena, mesh, StrFormat(arena, "#%llx", (unsigned long long)image->old), StrMake((const char *)bytes->data, (u64)size));
    }
    return Prog__MeshImage(arena, mesh, file, (String){0});
}

// A material's look: what its surface shader's base color input is linked to from an image texture node, or that input's own color,
// linear, or the material's viewport color when it has no node tree.
function Prog_MeshPart Prog__BlendLook(Arena *arena, const Prog__Blend *blend, const Prog__BlendBlock *material, Prog_Mesh *mesh)
{
    Prog_MeshPart result = { .image = UINT32_MAX };
    u32 materialType = Prog__BlendStruct(blend, S("Material")), treeType = Prog__BlendStruct(blend, S("bNodeTree")), nodeType = Prog__BlendStruct(blend, S("bNode"));
    u32 socketType = Prog__BlendStruct(blend, S("bNodeSocket")), linkType = Prog__BlendStruct(blend, S("bNodeLink")), rgbaType = Prog__BlendStruct(blend, S("bNodeSocketValueRGBA"));
    if (!material || material->sdna != materialType) return result;
    f32 rgb[3] = { 0.8f, 0.8f, 0.8f };
    Prog__BlendFloats(blend, materialType, material->data, material->length, S("r"), &rgb[0], 1);
    Prog__BlendFloats(blend, materialType, material->data, material->length, S("g"), &rgb[1], 1);
    Prog__BlendFloats(blend, materialType, material->data, material->length, S("b"), &rgb[2], 1);
    result.color = Prog__LinearColor(rgb[0], rgb[1], rgb[2], 1);
    const Prog__BlendBlock *tree = Prog__BlendAt(blend, material, Prog__BlendPointer(blend, materialType, material->data, material->length, S("nodetree")));
    if (!tree || tree->sdna != treeType || nodeType == UINT32_MAX || socketType == UINT32_MAX || linkType == UINT32_MAX) return result;
    // The surface shader and its color input: Principled's Base Color or Diffuse's Color.
    u64 socketOld = 0;
    const Prog__BlendBlock *socket = NULL;
    u32 steps = 0;
    for (const Prog__BlendBlock *node = Prog__BlendAt(blend, tree, Prog__BlendPointer(blend, treeType, tree->data, tree->length, S("nodes"))); node && !socket && steps < 4096;
         node = Prog__BlendAt(blend, tree, Prog__BlendPointer(blend, nodeType, node->data, node->length, S("next"))), ++steps) {
        String idname = Prog__BlendText(blend, nodeType, node->data, node->length, S("idname"));
        b32 principled = StrIsEqual(idname, S("ShaderNodeBsdfPrincipled"));
        if (!principled && !StrIsEqual(idname, S("ShaderNodeBsdfDiffuse"))) continue;
        u64 old = Prog__BlendPointer(blend, nodeType, node->data, node->length, S("inputs"));
        for (const Prog__BlendBlock *input = Prog__BlendAt(blend, tree, old); input && steps < 4096; ++steps) {
            String name = Prog__BlendText(blend, socketType, input->data, input->length, S("identifier"));
            if (StrIsEqual(name, principled ? S("Base Color") : S("Color"))) {
                socket = input, socketOld = old;
                break;
            }
            old = Prog__BlendPointer(blend, socketType, input->data, input->length, S("next"));
            input = Prog__BlendAt(blend, tree, old);
        }
    }
    if (!socket) return result;
    const Prog__BlendBlock *value = rgbaType != UINT32_MAX ? Prog__BlendAt(blend, tree, Prog__BlendPointer(blend, socketType, socket->data, socket->length, S("default_value"))) : NULL;
    f32 rgba[4];
    if (value && Prog__BlendFloats(blend, rgbaType, value->data, value->length, S("value"), rgba, 4)) result.color = Prog__LinearColor(rgba[0], rgba[1], rgba[2], 1);
    // Principled's Alpha, the socket after its Base Color's among its inputs, left unlinked.
    u32 floatType = Prog__BlendStruct(blend, S("bNodeSocketValueFloat"));
    f32 alpha = 1;
    steps = 0;
    for (const Prog__BlendBlock *input = socket; input && floatType != UINT32_MAX && steps < 256; ++steps) {
        if (StrIsEqual(Prog__BlendText(blend, socketType, input->data, input->length, S("identifier")), S("Alpha"))) {
            const Prog__BlendBlock *number = Prog__BlendAt(blend, tree, Prog__BlendPointer(blend, socketType, input->data, input->length, S("default_value")));
            if (number) Prog__BlendFloats(blend, floatType, number->data, number->length, S("value"), &alpha, 1);
            break;
        }
        input = Prog__BlendAt(blend, tree, Prog__BlendPointer(blend, socketType, input->data, input->length, S("next")));
    }
    steps = 0;
    for (const Prog__BlendBlock *link = Prog__BlendAt(blend, tree, Prog__BlendPointer(blend, treeType, tree->data, tree->length, S("links"))); link && steps < 4096;
         link = Prog__BlendAt(blend, tree, Prog__BlendPointer(blend, linkType, link->data, link->length, S("next"))), ++steps) {
        if (Prog__BlendPointer(blend, linkType, link->data, link->length, S("tosock")) != socketOld) continue;
        const Prog__BlendBlock *from = Prog__BlendAt(blend, tree, Prog__BlendPointer(blend, linkType, link->data, link->length, S("fromnode")));
        if (!from || !StrIsEqual(Prog__BlendText(blend, nodeType, from->data, from->length, S("idname")), S("ShaderNodeTexImage"))) break;
        result.image = Prog__BlendImage(arena, blend, Prog__BlendAt(blend, NULL, Prog__BlendPointer(blend, nodeType, from->data, from->length, S("id"))), mesh);
        if (result.image != UINT32_MAX) result.bare = result.color, result.color = 0;
        break;
    }
    Prog__MeshAlpha(&result, alpha);
    return result;
}


// The color of element `at` of a color attribute, `bytes` long: four floats, linear, or four bytes, sRGB.
function u32 Prog__BlendColor(const Prog__BlendBlock *values, u64 at, u32 bytes)
{
    if (bytes == 16) {
        f32 c[4];
        MemCopy(c, values->data + at * 16, 16);
        return Prog__LinearColor(c[0], c[1], c[2], 1);
    }
    return Prog__Read32(values->data + at * 4) | 0xFF000000u;
}

// The pointer at slot `index` of the pointer array `slots`, or 0.
function u64 Prog__BlendSlot(const Prog__Blend *blend, const Prog__BlendBlock *slots, u32 index)
{
    if (!slots || (u64)(index + 1) * blend->pointer > slots->length) return 0;
    return blend->pointer == 8 ? Prog__Read64(slots->data + index * 8) : Prog__Read32(slots->data + index * 4);
}

// Adds mesh `block`'s faces, placed by `place`, as triangles fanned from each face's first corner, in the parts its materials give them:
// a slot's own, or the object `object`'s where the slot is linked to the object.
function void Prog__BlendMesh(Arena *arena, const Prog__Blend *blend, const Prog__BlendBlock *object, const Prog__BlendBlock *block, const Prog__Affine *place, Prog_Mesh *out)
{
    u32 meshType = Prog__BlendStruct(blend, S("Mesh"));
    if (meshType == UINT32_MAX || block->sdna != meshType) return;
    const u8 *mesh = block->data;
    u64 length = block->length;
    const Prog__BlendBlock *owner = block;
    i64 vertices = Prog__BlendInt(blend, meshType, mesh, length, S("totvert")), faces = Prog__BlendInt(blend, meshType, mesh, length, S("totpoly"));
    i64 corners = Prog__BlendInt(blend, meshType, mesh, length, S("totloop"));
    if (vertices <= 0 || faces <= 0 || corners <= 0 || vertices > (1 << 26) || corners > (1 << 28)) return;
    const f32 *positions = (const f32 *)Prog__BlendLayer(blend, block, meshType, S("vdata"), S("position"), (u64)vertices * 12);
    const u8 *cornerVerts = Prog__BlendLayer(blend, block, meshType, S("ldata"), S(".corner_vert"), (u64)corners * 4);
    const Prog__BlendBlock *offsets = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, meshType, mesh, length, S("poly_offset_indices")));
    u64 base = out->positions.header.count / 3;
    if (positions && cornerVerts && offsets && offsets->length >= (u64)(faces + 1) * 4) {
        Scratch scratch = GetScratchEx(arena);
        f32 *points = ArenaPushArrayNoZero(scratch.arena, (u64)vertices * 3, f32);
        for (i64 v = 0; v < vertices; ++v) {
            f32 p[3];
            MemCopy(p, (const u8 *)positions + v * 12, 12);
            Prog__MeshPoint(points + v * 3, place, p[0], p[1], p[2]);
        }
        // Its materials, and each face's.
        Prog_MeshPart looks[32];
        u32 lookCount = (u32)Min(Max(Prog__BlendInt(blend, meshType, mesh, length, S("totcol")), (i64)0), (i64)CArrayCount(looks));
        const Prog__BlendBlock *slots = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, meshType, mesh, length, S("mat")));
        u32 objectType = Prog__BlendStruct(blend, S("Object"));
        const Prog__BlendBlock *own = object ? Prog__BlendAt(blend, object, Prog__BlendPointer(blend, objectType, object->data, object->length, S("mat"))) : NULL;
        const Prog__BlendBlock *linked = object ? Prog__BlendAt(blend, object, Prog__BlendPointer(blend, objectType, object->data, object->length, S("matbits"))) : NULL;
        i64 objectSlots = object ? Prog__BlendInt(blend, objectType, object->data, object->length, S("totcol")) : 0;
        for (u32 i = 0; i < lookCount; ++i) {
            b32 byObject = linked && own && (i64)i < objectSlots && i < linked->length && linked->data[i];
            u64 old = byObject ? Prog__BlendSlot(blend, own, i) : Prog__BlendSlot(blend, slots, i);
            looks[i] = Prog__BlendLook(arena, blend, Prog__BlendAt(blend, NULL, old), out);
        }
        i32 domain = 0;
        const Prog__BlendBlock *materialIndex = Prog__BlendAttribute(blend, owner, meshType, S("pdata"), S("material_index"), &domain);
        if (materialIndex && (domain != Prog__BlendOnFace || materialIndex->length < (u64)faces * 4)) materialIndex = NULL;
        // Its active texture coordinates, two floats for each corner, and its active color attribute, for each point or corner.
        const Prog__BlendBlock *named = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, meshType, mesh, length, S("active_uv_map_attribute")));
        String uvName = named ? Prog__BlendName(named->data, named->length) : S("UVMap");
        const Prog__BlendBlock *uvs = Prog__BlendAttribute(blend, owner, meshType, S("ldata"), uvName.length ? uvName : S("UVMap"), &domain);
        if (uvs && (domain != Prog__BlendOnCorner || uvs->length < (u64)corners * 8)) uvs = NULL;
        named = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, meshType, mesh, length, S("active_color_attribute")));
        const Prog__BlendBlock *colors = named ? Prog__BlendAttribute(blend, owner, meshType, (String){0}, Prog__BlendName(named->data, named->length), &domain) : NULL;
        u64 elements = domain == Prog__BlendOnCorner ? (u64)corners : (u64)vertices;
        u32 colorBytes = colors && colors->length >= elements * 16 ? 16 : colors && colors->length >= elements * 4 ? 4 : 0;
        b32 cornerColors = colorBytes && domain == Prog__BlendOnCorner;
        if (!colorBytes || (domain != Prog__BlendOnCorner && domain != Prog__BlendOnPoint)) colors = NULL;
        Prog__Corners made = Prog__CornersMake(scratch.arena, (u64)vertices, (u64)corners);
        for (u32 look = 0; look < Max(lookCount, 1u); ++look) {
            Prog_MeshPart none = { .image = UINT32_MAX };
            Prog__MeshLook(arena, out, lookCount ? &looks[look] : &none);
            for (i64 f = 0; f < faces; ++f) {
                i32 index = materialIndex ? (i32)Prog__Read32(materialIndex->data + f * 4) : 0;
                if ((u32)Min(Max(index, 0), (i32)Max(lookCount, 1u) - 1) != look) continue;
                u32 from = Prog__Read32(offsets->data + f * 4), to = Prog__Read32(offsets->data + (f + 1) * 4);
                if (to > (u64)corners || from >= to) continue;
                u32 fan = 0, previous = 0;
                for (u32 c = from; c < to; ++c) {
                    u32 point = Prog__Read32(cornerVerts + (u64)c * 4), corner = UINT32_MAX;
                    if (point < (u64)vertices) {
                        // The texture coordinate's two floats as they are, and the color.
                        Prog__MeshCorner key = { point, 0, 0, 0 };
                        if (uvs) MemCopy(&key.a, uvs->data + (u64)c * 8, 8);
                        if (colors) key.c = Prog__BlendColor(colors, cornerColors ? c : point, colorBytes);
                        corner = Prog__CornersFind(&made, key);
                        if (corner == UINT32_MAX && out->positions.header.count / 3 < UINT32_MAX) {
                            corner = (u32)(out->positions.header.count / 3);
                            MemCopy(ArrayAdd(arena, out->positions, 3), points + (u64)point * 3, 12);
                            if (uvs) {
                                f32 *t = Prog__MeshUvs(arena, out) + (u64)corner * 2;
                                MemCopy(t, &key.a, 8);
                                t[1] = 1 - t[1];
                            }
                            if (colors) Prog__MeshColors(arena, out)[corner] = key.c;
                            Prog__CornersAdd(&made, key, corner);
                        }
                    }
                    if (c == from) fan = corner;
                    else if (c >= from + 2) Prog__MeshTriangle(arena, out, fan, previous, corner);
                    previous = corner;
                }
            }
        }
        ReleaseScratch(scratch);
        return;
    }
    // Before 3.4: arrays of MVert, with its position first, MPoly, with where its loops start and how many, and MLoop, with its vertex.
    u32 vertType = Prog__BlendStruct(blend, S("MVert")), polyType = Prog__BlendStruct(blend, S("MPoly")), loopType = Prog__BlendStruct(blend, S("MLoop"));
    const Prog__BlendBlock *verts = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, meshType, mesh, length, S("mvert")));
    const Prog__BlendBlock *polys = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, meshType, mesh, length, S("mpoly")));
    const Prog__BlendBlock *loops = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, meshType, mesh, length, S("mloop")));
    if (vertType == UINT32_MAX || polyType == UINT32_MAX || loopType == UINT32_MAX || !verts || !polys || !loops) return;
    u64 vertStride = blend->typeSizes[blend->structs[vertType][0]], polyStride = blend->typeSizes[blend->structs[polyType][0]];
    u64 loopStride = blend->typeSizes[blend->structs[loopType][0]];
    if (verts->length < (u64)vertices * vertStride || polys->length < (u64)faces * polyStride || loops->length < (u64)corners * loopStride) return;
    f32 *points = ArrayAdd(arena, out->positions, (u64)vertices * 3);
    for (i64 v = 0; v < vertices; ++v) {
        f32 p[3] = {0};
        Prog__BlendFloats(blend, vertType, verts->data + (u64)v * vertStride, vertStride, S("co"), p, 3);
        Prog__MeshPoint(points + v * 3, place, p[0], p[1], p[2]);
    }
    for (i64 f = 0; f < faces; ++f) {
        const u8 *poly = polys->data + (u64)f * polyStride;
        i64 from = Prog__BlendInt(blend, polyType, poly, polyStride, S("loopstart")), count = Prog__BlendInt(blend, polyType, poly, polyStride, S("totloop"));
        if (from < 0 || count < 3 || from + count > corners) continue;
        for (i64 c = from + 2; c < from + count; ++c) {
            u32 a = (u32)Prog__BlendInt(blend, loopType, loops->data + (u64)from * loopStride, loopStride, S("v"));
            u32 b = (u32)Prog__BlendInt(blend, loopType, loops->data + (u64)(c - 1) * loopStride, loopStride, S("v"));
            u32 e = (u32)Prog__BlendInt(blend, loopType, loops->data + (u64)c * loopStride, loopStride, S("v"));
            Prog__MeshTriangle(arena, out, (u32)base + a, (u32)base + b, (u32)base + e);
        }
    }
}

// An object's place among its parents': its parents' places, its inverse of the parent at the time it was parented, then its own
// location, rotation and scale.
function Prog__Affine Prog__BlendPlace(const Prog__Blend *blend, u32 objectType, const Prog__BlendBlock *object, u32 depth)
{
    const u8 *o = object->data;
    u64 length = object->length;
    f32 location[3] = {0}, euler[3] = {0}, scale[3] = { 1, 1, 1 }, q[4] = { 0, 0, 0, 1 }, wxyz[4] = { 1, 0, 0, 0 };
    Prog__BlendFloats(blend, objectType, o, length, S("loc"), location, 3);
    Prog__BlendFloats(blend, objectType, o, length, S("rot"), euler, 3);
    Prog__BlendFloats(blend, objectType, o, length, S("size"), scale, 3);
    i64 mode = Prog__BlendInt(blend, objectType, o, length, S("rotmode"));
    if (mode == 0 && Prog__BlendFloats(blend, objectType, o, length, S("quat"), wxyz, 4)) {
        f32 norm = sqrtf(wxyz[0] * wxyz[0] + wxyz[1] * wxyz[1] + wxyz[2] * wxyz[2] + wxyz[3] * wxyz[3]);
        if (norm > 0) q[0] = wxyz[1] / norm, q[1] = wxyz[2] / norm, q[2] = wxyz[3] / norm, q[3] = wxyz[0] / norm;
    } else {
        // ponytail: every Euler order turns as XYZ, Blender's default, and axis-angle as no turn; the other orders' products if needed.
        f32 degrees[3] = { euler[0] * 57.2957795f, euler[1] * 57.2957795f, euler[2] * 57.2957795f };
        Prog__EulerQuaternion(degrees, q);
    }
    Prog__Affine result = Prog__AffineCompose(location, q, scale);
    const Prog__BlendBlock *parent = Prog__BlendAt(blend, NULL, Prog__BlendPointer(blend, objectType, o, length, S("parent")));
    if (parent && parent->sdna == objectType && depth < 64) {
        f32 inverse[16];
        Prog__Affine between = Prog__AffineIdentity;
        // Blender's matrices are column by column, the translation last.
        if (Prog__BlendFloats(blend, objectType, o, length, S("parentinv"), inverse, 16)) {
            for (u32 r = 0; r < 3; ++r) for (u32 c = 0; c < 4; ++c) between.m[r * 4 + c] = inverse[c * 4 + r];
        }
        result = Prog__AffineMultiply(Prog__AffineMultiply(Prog__BlendPlace(blend, objectType, parent, depth + 1), between), result);
    }
    return result;
}

function b32 Prog__ReadBlend(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    Prog__Blend blend = {0};
    if (!Prog__BlendParse(arena, Prog__BlendInflate(arena, data), &blend)) return false;
    u32 objectType = Prog__BlendStruct(&blend, S("Object"));
    if (objectType == UINT32_MAX) return false;
    mesh->zUp = true;
    for (u64 i = 0; i < blend.blocks.header.count && !P_IsCancelled(cancel); ++i) {
        const Prog__BlendBlock *object = &blend.blocks.v[i];
        // Mesh objects, Blender's type 1.
        if (object->code != Prog__BlendCode('O', 'B', 0, 0) || object->sdna != objectType) continue;
        if (Prog__BlendInt(&blend, objectType, object->data, object->length, S("type")) != 1) continue;
        const Prog__BlendBlock *data = Prog__BlendAt(&blend, NULL, Prog__BlendPointer(&blend, objectType, object->data, object->length, S("data")));
        Prog__Affine place = Prog__BlendPlace(&blend, objectType, object, 0);
        if (data && data->code == Prog__BlendCode('M', 'E', 0, 0)) Prog__BlendMesh(arena, &blend, object, data, &place, mesh);
    }
    return mesh->indices.header.count > 0;
}

// -----------------------------------------------------------------------------
// USD: the meshes of a binary .usdc, or of the .usdc a .usdz zips, placed by their prims' transform ops and their parents', as of their
// default values (time samples are not read). The crate format keeps its tokens, fields, paths and specs in sections, most of them LZ4
// compressed, and arrays of integers as deltas in 1, 2 or 4 bytes as each needs.

// The bytes of `name` inside the zip archive `zip`, already read, from its central directory: a view of a stored entry, a deflated one
// inflated into `arena`; empty when there is no such entry. An empty `name` takes the first entry, as a .usdz's root layer is.
function String Prog__ZipView(Arena *arena, String zip, String name)
{
    const u8 *z = (const u8 *)zip.value;
    // The end of central directory record is in the last 22 bytes, after a comment of up to 64 kB.
    u64 end = UINT64_MAX;
    for (u64 i = zip.length >= 22 ? zip.length - 22 + 1 : 0; i-- > 0 && i + 65557 >= zip.length && end == UINT64_MAX;) if (Prog__Read32(z + i) == 0x06054B50) end = i;
    if (end == UINT64_MAX) return (String){0};
    u32 entries = (u32)z[end + 10] | (u32)z[end + 11] << 8;
    u64 directoryAt = Prog__Read32(z + end + 16);
    for (u64 at = directoryAt, i = 0; i < entries && at + 46 <= zip.length; ++i) {
        if (Prog__Read32(z + at) != 0x02014B50) break;
        u32 method = (u32)z[at + 10] | (u32)z[at + 11] << 8, nameLength = (u32)z[at + 28] | (u32)z[at + 29] << 8;
        u32 extra = (u32)z[at + 30] | (u32)z[at + 31] << 8, comment = (u32)z[at + 32] | (u32)z[at + 33] << 8;
        u64 packed = Prog__Read32(z + at + 20), unpacked = Prog__Read32(z + at + 24), local = Prog__Read32(z + at + 42);
        String entry = StrMake((const char *)z + at + 46, Min((u64)nameLength, zip.length - at - 46));
        at += 46 + nameLength + extra + comment;
        if ((name.length && !StrIsEqual(entry, name)) || unpacked > Prog_ModelMaxBytes || (method != 0 && method != 8)) continue;
        // The local header's name and extra field may differ in length from the central directory's.
        if (local + 30 > zip.length) break;
        u64 dataAt = local + 30 + ((u64)z[local + 26] | (u64)z[local + 27] << 8) + ((u64)z[local + 28] | (u64)z[local + 29] << 8);
        if (dataAt + packed > zip.length) break;
        if (method == 0) return StrMake((const char *)z + dataAt, packed);
        char *out = ArenaPushArrayNoZero(arena, Max(unpacked, (u64)1), char);
        i32 made = packed <= INT32_MAX && unpacked <= INT32_MAX ? stbi_zlib_decode_noheader_buffer(out, (i32)unpacked, (const char *)z + dataAt, (i32)packed) : -1;
        return made >= 0 ? StrMake(out, (u64)made) : (String){0};
    }
    return (String){0};
}

// An LZ4 block into `out`, `size` bytes at most; how many it made, or UINT64_MAX when the block is broken.
function u64 Prog__Lz4(const u8 *in, u64 length, u8 *out, u64 size)
{
    u64 i = 0, o = 0;
    while (i < length) {
        u8 token = in[i++];
        u64 literals = token >> 4;
        if (literals == 15) for (u8 more = 255; more == 255 && i < length;) literals += more = in[i++];
        if (i + literals > length || o + literals > size) return UINT64_MAX;
        MemCopy(out + o, in + i, literals);
        i += literals, o += literals;
        // The last sequence has literals only.
        if (i >= length) break;
        if (i + 2 > length) return UINT64_MAX;
        u64 offset = (u64)in[i] | (u64)in[i + 1] << 8, match = token & 15;
        i += 2;
        if (match == 15) for (u8 more = 255; more == 255 && i < length;) match += more = in[i++];
        match += 4;
        if (!offset || offset > o || o + match > size) return UINT64_MAX;
        // Byte by byte, as a match may overlap what it copies.
        for (u64 k = 0; k < match; ++k, ++o) out[o] = out[o - offset];
    }
    return o;
}

// USD's TfFastCompression: a chunk count, 0 for one chunk that is the rest, else each chunk's compressed size and its bytes.
function u64 Prog__UsdInflate(const u8 *in, u64 length, u8 *out, u64 size)
{
    if (!length) return UINT64_MAX;
    if (!in[0]) return Prog__Lz4(in + 1, length - 1, out, size);
    u64 at = 1, made = 0;
    for (u32 chunk = 0; chunk < in[0]; ++chunk) {
        if (at + 4 > length) return UINT64_MAX;
        u64 packed = Prog__Read32(in + at);
        at += 4;
        if (at + packed > length) return UINT64_MAX;
        u64 got = Prog__Lz4(in + at, packed, out + made, size - made);
        if (got == UINT64_MAX) return UINT64_MAX;
        made += got, at += packed;
    }
    return made;
}

typedef struct Prog__Usd {
    String file;
    b32 counts64;          // arrays start with a 64-bit count, from version 0.7
    String *tokens;
    u32 tokenCount;
    const u8 *strings;    // each string value's token, four bytes each
    u32 stringCount;
    u32 *fieldTokens;
    u64 *fieldValues;
    u32 fieldCount;
    i32 *fieldSets;
    u32 fieldSetCount;
    u32 *pathParent, *pathToken;  // each path's parent, UINT32_MAX for the root, and its last element
    b8 *pathProperty;             // the element is a property, ".points", rather than a prim
    u32 pathCount;
    u32 *specPath, *specFieldSet, *specType;
    u32 specCount;
    u32 xformOpOrder;     // the token, or UINT32_MAX
} Prog__Usd;

// Each property's spec by its prim's path and its name token, as prim << 32 | token.
typedef struct Prog__UsdProperties { MapHeader header; struct { u64 key; u32 value; } *v; } Prog__UsdProperties;

function u32 Prog__UsdTokenIndex(const Prog__Usd *usd, String name)
{
    for (u32 i = 0; i < usd->tokenCount; ++i) if (StrIsEqual(usd->tokens[i], name)) return i;
    return UINT32_MAX;
}

// `count` integers compressed at `*at`, after their compressed size: a common delta, two bits each for which width its delta takes,
// then the deltas that are not the common one; the values are their running sum.
function i32 *Prog__UsdInts(Arena *arena, const Prog__Usd *usd, u64 *at, u64 count)
{
    const u8 *d = (const u8 *)usd->file.value;
    if (*at + 8 > usd->file.length || count > (1u << 28)) return NULL;
    u64 packed = Prog__Read64(d + *at);
    *at += 8;
    if (*at + packed > usd->file.length) return NULL;
    u64 codes = (count * 2 + 7) / 8, size = 4 + codes + count * 4;
    Scratch scratch = GetScratchEx(arena);
    u8 *raw = ArenaPushArrayNoZero(scratch.arena, Max(size, (u64)1), u8);
    u64 made = Prog__UsdInflate(d + *at, packed, raw, size);
    *at += packed;
    i32 *result = made != UINT64_MAX && made >= 4 + codes ? ArenaPushArray(arena, Max(count, (u64)1), i32) : NULL;
    if (result) {
        i32 common = (i32)Prog__Read32(raw), value = 0;
        u64 p = 4 + codes;
        for (u64 i = 0; i < count; ++i) {
            u32 code = (raw[4 + i / 4] >> ((i % 4) * 2)) & 3;
            i32 delta = common;
            if (code == 1 && p + 1 <= made) delta = (i8)raw[p], p += 1;
            else if (code == 2 && p + 2 <= made) delta = (i16)(raw[p] | raw[p + 1] << 8), p += 2;
            else if (code == 3 && p + 4 <= made) delta = (i32)Prog__Read32(raw + p), p += 4;
            else if (code) { result = NULL; break; }
            value += delta;
            result[i] = value;
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 Prog__UsdParse(Arena *arena, String file, Prog__Usd *usd)
{
    const u8 *d = (const u8 *)file.value;
    if (file.length < 88 || !StrIsEqual(StrFromTo(file, 0, 8), S("PXR-USDC"))) return false;
    usd->file = file;
    usd->counts64 = d[8] > 0 || d[9] >= 7;
    u64 toc = Prog__Read64(d + 16);
    if (toc + 8 > file.length) return false;
    u64 sectionCount = Prog__Read64(d + toc);
    u64 tokensAt = 0, fieldsAt = 0, fieldSetsAt = 0, pathsAt = 0, specsAt = 0;
    for (u64 i = 0; i < sectionCount && toc + 8 + (i + 1) * 32 <= file.length; ++i) {
        const u8 *s = d + toc + 8 + i * 32;
        String name = Prog__BlendName(s, 16);
        u64 start = Prog__Read64(s + 16);
        if (StrIsEqual(name, S("TOKENS"))) tokensAt = start;
        else if (StrIsEqual(name, S("FIELDS"))) fieldsAt = start;
        else if (StrIsEqual(name, S("FIELDSETS"))) fieldSetsAt = start;
        else if (StrIsEqual(name, S("PATHS"))) pathsAt = start;
        else if (StrIsEqual(name, S("SPECS"))) specsAt = start;
        // The strings: their count, then each one's token, as they are.
        else if (StrIsEqual(name, S("STRINGS")) && start + 8 <= file.length) {
            u64 strings = Prog__Read64(d + start);
            if (strings <= (file.length - start - 8) / 4) usd->strings = d + start + 8, usd->stringCount = (u32)strings;
        }
    }
    if (!tokensAt || !fieldsAt || !fieldSetsAt || !pathsAt || !specsAt || tokensAt + 24 > file.length) return false;
    // The tokens: their count, then their text, each ending in a zero byte, compressed.
    u64 count = Prog__Read64(d + tokensAt), size = Prog__Read64(d + tokensAt + 8), packed = Prog__Read64(d + tokensAt + 16);
    if (count > (1u << 26) || size > Prog_ModelMaxBytes || tokensAt + 24 + packed > file.length) return false;
    u8 *text = ArenaPushArrayNoZero(arena, Max(size, (u64)1), u8);
    if (Prog__UsdInflate(d + tokensAt + 24, packed, text, size) != size) return false;
    usd->tokens = ArenaPushArray(arena, Max(count, (u64)1), String);
    usd->tokenCount = (u32)count;
    for (u64 i = 0, p = 0; i < count && p <= size; ++i) {
        usd->tokens[i] = Prog__BlendName(text + p, size - p);
        p += usd->tokens[i].length + 1;
    }
    // The fields: each one's name token, then its value, a 64-bit rep, all of those compressed together.
    if (fieldsAt + 8 > file.length) return false;
    u64 at = fieldsAt + 8;
    usd->fieldCount = (u32)Min(Prog__Read64(d + fieldsAt), (u64)(1u << 26));
    usd->fieldTokens = (u32 *)Prog__UsdInts(arena, usd, &at, usd->fieldCount);
    if (!usd->fieldTokens || at + 8 > file.length) return false;
    packed = Prog__Read64(d + at);
    usd->fieldValues = ArenaPushArray(arena, Max(usd->fieldCount, 1u), u64);
    if (at + 8 + packed > file.length || Prog__UsdInflate(d + at + 8, packed, (u8 *)usd->fieldValues, (u64)usd->fieldCount * 8) != (u64)usd->fieldCount * 8) return false;
    // The field sets: runs of field indices, each run ending in -1.
    if (fieldSetsAt + 8 > file.length) return false;
    at = fieldSetsAt + 8;
    usd->fieldSetCount = (u32)Min(Prog__Read64(d + fieldSetsAt), (u64)(1u << 26));
    usd->fieldSets = Prog__UsdInts(arena, usd, &at, usd->fieldSetCount);
    // The paths: as a tree walked depth first, each entry its path's index, its element's token, negative for a property, and a jump
    // that says whether it has a child, which is the next entry, and a sibling, that far on.
    if (!usd->fieldSets || pathsAt + 16 > file.length) return false;
    usd->pathCount = (u32)Min(Prog__Read64(d + pathsAt), (u64)(1u << 26));
    u64 encoded = Min(Prog__Read64(d + pathsAt + 8), (u64)(1u << 26));
    at = pathsAt + 16;
    i32 *indices = Prog__UsdInts(arena, usd, &at, encoded), *elements = Prog__UsdInts(arena, usd, &at, encoded), *jumps = Prog__UsdInts(arena, usd, &at, encoded);
    if (!indices || !elements || !jumps) return false;
    usd->pathParent = ArenaPushArray(arena, Max(usd->pathCount, 1u), u32);
    usd->pathToken = ArenaPushArray(arena, Max(usd->pathCount, 1u), u32);
    usd->pathProperty = ArenaPushArray(arena, Max(usd->pathCount, 1u), b8);
    for (u32 i = 0; i < usd->pathCount; ++i) usd->pathParent[i] = UINT32_MAX;
    // Siblings to come back to, with the parent they share.
    Scratch scratch = GetScratchEx(arena);
    u64 *pending = ArenaPushArray(scratch.arena, Max(encoded, (u64)1) * 2, u64), pendingCount = 0;
    u64 entry = 0, parent = UINT32_MAX;
    for (u64 steps = 0; steps <= encoded && entry < encoded; ++steps) {
        u32 path = (u32)indices[entry];
        i32 element = elements[entry], jump = jumps[entry];
        if (path >= usd->pathCount) break;
        usd->pathParent[path] = (u32)parent;
        usd->pathToken[path] = (u32)(element < 0 ? -element : element);
        usd->pathProperty[path] = element < 0;
        b32 child = jump > 0 || jump == -1, sibling = jump >= 0;
        if (child && sibling) pending[pendingCount++] = entry + (u64)jump, pending[pendingCount++] = parent;
        if (child) {
            parent = path, ++entry;
        } else if (sibling) {
            ++entry;
        } else if (pendingCount) {
            parent = pending[--pendingCount], entry = pending[--pendingCount];
        } else {
            break;
        }
    }
    ReleaseScratch(scratch);
    // The specs: each one's path, field set and kind, 1 for an attribute and 6 for a prim.
    if (specsAt + 8 > file.length) return false;
    at = specsAt + 8;
    usd->specCount = (u32)Min(Prog__Read64(d + specsAt), (u64)(1u << 26));
    usd->specPath = (u32 *)Prog__UsdInts(arena, usd, &at, usd->specCount);
    usd->specFieldSet = (u32 *)Prog__UsdInts(arena, usd, &at, usd->specCount);
    usd->specType = (u32 *)Prog__UsdInts(arena, usd, &at, usd->specCount);
    return usd->specPath && usd->specFieldSet && usd->specType;
}

// The value of field `name` among spec `spec`'s, 0 when it has none.
function u64 Prog__UsdField(const Prog__Usd *usd, u32 spec, String name)
{
    for (u32 i = usd->specFieldSet[spec]; i < usd->fieldSetCount && usd->fieldSets[i] >= 0; ++i) {
        u32 field = (u32)usd->fieldSets[i];
        if (field < usd->fieldCount && usd->fieldTokens[field] < usd->tokenCount && StrIsEqual(usd->tokens[usd->fieldTokens[field]], name)) return usd->fieldValues[field];
    }
    return 0;
}

// A value rep's parts: an array, inlined in its 48 low bits, compressed, and its type, of which 3 is int, 11 token, 15 matrix4d,
// 20 vec2f, 23 vec3d, 24 vec3f and 27 to 30 the vec4s and quaternions.
#define Prog__UsdArray(rep) (((rep) >> 63) & 1)
#define Prog__UsdInlined(rep) (((rep) >> 62) & 1)
#define Prog__UsdCompressed(rep) (((rep) >> 61) & 1)
#define Prog__UsdType(rep) ((u32)((rep) >> 48) & 0xFF)
#define Prog__UsdPayload(rep) ((rep) & UINT64_C(0xFFFFFFFFFFFF))

// An array value's count and where its elements start.
function b32 Prog__UsdArrayAt(const Prog__Usd *usd, u64 rep, u64 *count, u64 *at)
{
    if (!Prog__UsdArray(rep) || Prog__UsdInlined(rep)) return false;
    u64 p = Prog__UsdPayload(rep), head = usd->counts64 ? 8 : 4;
    if (p + head > usd->file.length) return false;
    *count = usd->counts64 ? Prog__Read64((const u8 *)usd->file.value + p) : Prog__Read32((const u8 *)usd->file.value + p);
    *at = p + head;
    return *count < (1u << 28);
}

// An int array value, compressed or not.
function i32 *Prog__UsdIntArray(Arena *arena, const Prog__Usd *usd, u64 rep, u64 *count)
{
    u64 at;
    if (Prog__UsdType(rep) != 3 || !Prog__UsdArrayAt(usd, rep, count, &at)) return NULL;
    if (Prog__UsdCompressed(rep)) return Prog__UsdInts(arena, usd, &at, *count);
    if (at + *count * 4 > usd->file.length) return NULL;
    i32 *result = ArenaPushArrayNoZero(arena, Max(*count, (u64)1), i32);
    MemCopy(result, usd->file.value + at, *count * 4);
    return result;
}

// A vec3 value or vec3 array's elements as floats, from vec3f or vec3d; `count` is 1 for a single one.
function f32 *Prog__UsdVec3(Arena *arena, const Prog__Usd *usd, u64 rep, u64 *count)
{
    u32 type = Prog__UsdType(rep);
    if (type != 23 && type != 24) return NULL;
    u64 at = Prog__UsdPayload(rep);
    *count = 1;
    if (Prog__UsdArray(rep) && !Prog__UsdArrayAt(usd, rep, count, &at)) return NULL;
    if (!Prog__UsdArray(rep) && Prog__UsdInlined(rep)) {
        // A vec3f with small whole parts, as (0, 0, 1), can be inlined as three signed bytes.
        f32 *result = ArenaPushArray(arena, 3, f32);
        for (u32 k = 0; k < 3; ++k) result[k] = (f32)(i8)(Prog__UsdPayload(rep) >> (k * 8));
        return result;
    }
    u64 size = type == 24 ? 12 : 24;
    if (at + *count * size > usd->file.length) return NULL;
    f32 *result = ArenaPushArrayNoZero(arena, Max(*count, (u64)1) * 3, f32);
    const u8 *p = (const u8 *)usd->file.value + at;
    for (u64 i = 0; i < *count * 3; ++i) {
        if (type == 24) {
            MemCopy(&result[i], p + i * 4, 4);
        } else {
            f64 v;
            MemCopy(&v, p + i * 8, 8);
            result[i] = (f32)v;
        }
    }
    return result;
}

// The transform ops of the prim at path `prim`, in its xformOpOrder, as one placement: a translate, a rotateXYZ in degrees, a scale or
// a transform matrix. ponytail: the other ops, as orient and the other rotation orders, are left out; add them as files need them.
function Prog__Affine Prog__UsdLocal(Arena *arena, const Prog__Usd *usd, u32 prim, const u32 *primSpec, const Prog__UsdProperties *properties)
{
    Prog__Affine result = Prog__AffineIdentity;
    if (primSpec[prim] == UINT32_MAX) return result;
    u64 order = Prog__UsdField(usd, primSpec[prim], S("xformOpOrder")), count, at;
    if (!order) {
        // On the xformOpOrder attribute, as a newer file keeps it.
        const u32 *spec = usd->xformOpOrder != UINT32_MAX ? MapGetPtrNull(*properties, (u64)prim << 32 | usd->xformOpOrder) : NULL;
        if (spec) order = Prog__UsdField(usd, *spec, S("default"));
    }
    if (!order || Prog__UsdType(order) != 11 || !Prog__UsdArrayAt(usd, order, &count, &at) || at + count * 4 > usd->file.length) return result;
    for (u64 i = 0; i < count; ++i) {
        u32 token = Prog__Read32((const u8 *)usd->file.value + at + i * 4);
        if (token >= usd->tokenCount) continue;
        const u32 *spec = MapGetPtrNull(*properties, (u64)prim << 32 | token);
        u64 value = spec ? Prog__UsdField(usd, *spec, S("default")) : 0;
        String op = usd->tokens[token];
        Prog__Affine step = Prog__AffineIdentity;
        u64 n;
        if (StrIndexStr(op, S("xformOp:transform"), SSF_None).exists && Prog__UsdType(value) == 15 && !Prog__UsdInlined(value) && Prog__UsdPayload(value) + 128 <= usd->file.length) {
            // Row by row with the translation in the last row, as USD multiplies row vectors.
            f64 m[16];
            MemCopy(m, usd->file.value + Prog__UsdPayload(value), 128);
            for (u32 r = 0; r < 3; ++r) for (u32 c = 0; c < 4; ++c) step.m[r * 4 + c] = (f32)m[c * 4 + r];
        } else {
            f32 *v = Prog__UsdVec3(arena, usd, value, &n);
            f32 zero[3] = {0}, ones[3] = { 1, 1, 1 }, identity[4] = { 0, 0, 0, 1 }, q[4];
            if (!v) continue;
            if (StrIndexStr(op, S("xformOp:translate"), SSF_None).exists) step = Prog__AffineCompose(v, identity, ones);
            else if (StrIndexStr(op, S("xformOp:scale"), SSF_None).exists) step = Prog__AffineCompose(zero, identity, v);
            else if (StrIndexStr(op, S("xformOp:rotateXYZ"), SSF_None).exists) Prog__EulerQuaternion(v, q), step = Prog__AffineCompose(zero, q, ones);
            else continue;
        }
        result = Prog__AffineMultiply(result, step);
    }
    return result;
}

// An array value of 2, 3 or 4 floats an element, from its float or double form, as `width` floats an element; NULL for another.
function f32 *Prog__UsdFloats(Arena *arena, const Prog__Usd *usd, u64 rep, u32 width, u64 *count)
{
    u32 type = Prog__UsdType(rep), floats = type == 20 || type == 24 || type == 28, doubles = type == 19 || type == 23 || type == 27;
    u32 have = type == 19 || type == 20 ? 2 : type == 23 || type == 24 ? 3 : 4;
    u64 at;
    if ((!floats && !doubles) || Prog__UsdCompressed(rep) || !Prog__UsdArrayAt(usd, rep, count, &at)) return NULL;
    u64 size = (u64)have * (floats ? 4 : 8);
    if (at + *count * size > usd->file.length) return NULL;
    f32 *result = ArenaPushArray(arena, Max(*count, (u64)1) * width, f32);
    const u8 *p = (const u8 *)usd->file.value + at;
    for (u64 i = 0; i < *count; ++i) {
        for (u32 k = 0; k < Min(have, width); ++k) {
            if (floats) {
                MemCopy(&result[i * width + k], p + i * size + k * 4, 4);
            } else {
                f64 v;
                MemCopy(&v, p + i * size + k * 8, 8);
                result[i * width + k] = (f32)v;
            }
        }
    }
    return result;
}

// A float or double value, which the file inlines as a float's bits where it can; `fallback` for another.
function f32 Prog__UsdScalar(const Prog__Usd *usd, u64 rep, f32 fallback)
{
    u32 type = Prog__UsdType(rep);
    if (Prog__UsdArray(rep) || (type != 8 && type != 9)) return fallback;
    if (Prog__UsdInlined(rep)) {
        u32 bits = (u32)Prog__UsdPayload(rep);
        f32 value;
        MemCopy(&value, &bits, 4);
        return value;
    }
    u64 at = Prog__UsdPayload(rep);
    if (at + 8 > usd->file.length) return fallback;
    f64 value;
    MemCopy(&value, usd->file.value + at, 8);
    return (f32)value;
}

// The text of a token, asset path or string value; empty for another.
function String Prog__UsdText(const Prog__Usd *usd, u64 rep)
{
    u32 type = Prog__UsdType(rep);
    if (Prog__UsdArray(rep) || !Prog__UsdInlined(rep) || (type != 10 && type != 11 && type != 12)) return (String){0};
    u64 index = Prog__UsdPayload(rep);
    if (type == 10) index = index < usd->stringCount ? Prog__Read32(usd->strings + index * 4) : UINT64_MAX;
    return index < usd->tokenCount ? usd->tokens[index] : (String){0};
}

// The first path a relationship's targets or an attribute's connections name, from their list op; UINT32_MAX for none.
function u32 Prog__UsdTarget(const Prog__Usd *usd, u64 rep)
{
    if (Prog__UsdType(rep) != 34 || Prog__UsdInlined(rep)) return UINT32_MAX;
    u64 at = Prog__UsdPayload(rep);
    if (at + 1 > usd->file.length) return UINT32_MAX;
    // Which lists follow, each a count and that many paths: explicit, added, prepended and appended items, then the deleted and ordered.
    u8 header = (u8)usd->file.value[at++];
    local_persist const u8 lists[] = { 1 << 1, 1 << 2, 1 << 5, 1 << 6 };
    for (u32 i = 0; i < CArrayCount(lists); ++i) {
        if (!(header & lists[i])) continue;
        if (at + 8 > usd->file.length) return UINT32_MAX;
        u64 count = Prog__Read64((const u8 *)usd->file.value + at);
        at += 8;
        if (count && at + 4 <= usd->file.length) {
            u32 path = Prog__Read32((const u8 *)usd->file.value + at);
            return path < usd->pathCount ? path : UINT32_MAX;
        }
        if (count > (usd->file.length - at) / 4) return UINT32_MAX;
        at += count * 4;
    }
    return UINT32_MAX;
}

// The spec of prim `prim`'s property `name`, or NULL.
function const u32 *Prog__UsdProperty(const Prog__Usd *usd, const Prog__UsdProperties *properties, u32 prim, String name)
{
    u32 token = Prog__UsdTokenIndex(usd, name);
    return token != UINT32_MAX && prim < usd->pathCount ? MapGetPtrNull(*properties, (u64)prim << 32 | token) : NULL;
}

// The prim whose property the connection of prim `prim`'s attribute `name` comes from, or UINT32_MAX.
function u32 Prog__UsdSource(const Prog__Usd *usd, const Prog__UsdProperties *properties, u32 prim, String name)
{
    const u32 *spec = Prog__UsdProperty(usd, properties, prim, name);
    u32 path = spec ? Prog__UsdTarget(usd, Prog__UsdField(usd, *spec, S("connectionPaths"))) : UINT32_MAX;
    return path != UINT32_MAX && usd->pathProperty[path] ? usd->pathParent[path] : path;
}

// How a primvar's values go with a mesh: 'C' for each polygon corner, 'P' for each point, 'F' for each polygon and 'A' all the same.
function u8 Prog__UsdInterpolation(const Prog__Usd *usd, u32 spec)
{
    String text = Prog__UsdText(usd, Prog__UsdField(usd, spec, S("interpolation")));
    return StrIsEqual(text, S("faceVarying")) ? 'C' : StrIsEqual(text, S("uniform")) ? 'F' : StrIsEqual(text, S("constant")) ? 'A' : 'P';
}

// A primvar of a mesh as the corners read it: its values, `width` floats each, how they go with the mesh, and its indices when it has them.
typedef struct Prog__UsdPrimvar {
    f32 *values;
    u64 count;
    i32 *indices;
    u64 indexCount;
    u8 mapping;
} Prog__UsdPrimvar;

function b32 Prog__UsdReadPrimvar(Arena *arena, const Prog__Usd *usd, const Prog__UsdProperties *properties, u32 prim, String name, u32 width, Prog__UsdPrimvar *out)
{
    *out = (Prog__UsdPrimvar){0};
    const u32 *spec = Prog__UsdProperty(usd, properties, prim, name);
    if (!spec || !(out->values = Prog__UsdFloats(arena, usd, Prog__UsdField(usd, *spec, S("default")), width, &out->count))) return false;
    out->mapping = Prog__UsdInterpolation(usd, *spec);
    const u32 *indices = Prog__UsdProperty(usd, properties, prim, StrFormat(arena, "%S:indices", name));
    if (indices) out->indices = Prog__UsdIntArray(arena, usd, Prog__UsdField(usd, *indices, S("default")), &out->indexCount);
    return true;
}

// The element of `primvar` for corner `corner` of point `point` in polygon `polygon`, or UINT64_MAX.
function u64 Prog__UsdPrimvarAt(const Prog__UsdPrimvar *primvar, u64 corner, u64 point, u64 polygon)
{
    u64 at = primvar->mapping == 'C' ? corner : primvar->mapping == 'P' ? point : primvar->mapping == 'F' ? polygon : 0;
    if (primvar->indices) at = at < primvar->indexCount && primvar->indices[at] >= 0 ? (u64)primvar->indices[at] : UINT64_MAX;
    return at < primvar->count ? at : UINT64_MAX;
}

// A bound material's look: its surface shader's diffuse color, linear, or the picture of the texture connected to it, from the .usdz's
// own files or next to the file, with the name of the primvar its coordinates come from in `uvName`.
function Prog_MeshPart Prog__UsdLook(Arena *arena, const Prog__Usd *usd, const Prog__UsdProperties *properties, u32 material, String zip,
                                     Prog_Mesh *mesh, String *uvName)
{
    Prog_MeshPart result = { .image = UINT32_MAX };
    u32 shader = Prog__UsdSource(usd, properties, material, S("outputs:surface"));
    if (shader == UINT32_MAX) return result;
    u32 texture = Prog__UsdSource(usd, properties, shader, S("inputs:diffuseColor"));
    const u32 *file = texture != UINT32_MAX ? Prog__UsdProperty(usd, properties, texture, S("inputs:file")) : NULL;
    if (file) {
        String name = Prog__UsdText(usd, Prog__UsdField(usd, *file, S("default")));
        if (StrIsEqual(StrFromTo(name, 0, 2), S("./"))) name = StrFrom(name, 2);
        String bytes = zip.length && name.length && mesh->imageSide ? Prog__ZipView(arena, zip, name) : (String){0};
        result.image = Prog__MeshImage(arena, mesh, name, bytes);
        // Its coordinates' primvar, named by the reader its st input connects to.
        u32 reader = Prog__UsdSource(usd, properties, texture, S("inputs:st"));
        const u32 *varname = reader != UINT32_MAX ? Prog__UsdProperty(usd, properties, reader, S("inputs:varname")) : NULL;
        String uv = varname ? Prog__UsdText(usd, Prog__UsdField(usd, *varname, S("default"))) : (String){0};
        if (uv.length) *uvName = StrFormat(arena, "primvars:%S", uv);
    }
    const u32 *diffuse = Prog__UsdProperty(usd, properties, shader, S("inputs:diffuseColor"));
    u64 n = 0;
    f32 *rgb = diffuse ? Prog__UsdVec3(arena, usd, Prog__UsdField(usd, *diffuse, S("default")), &n) : NULL;
    if (rgb && result.image == UINT32_MAX) result.color = Prog__LinearColor(rgb[0], rgb[1], rgb[2], 1);
    else if (rgb) result.bare = Prog__LinearColor(rgb[0], rgb[1], rgb[2], 1);
    const u32 *opacity = Prog__UsdProperty(usd, properties, shader, S("inputs:opacity"));
    if (opacity) Prog__MeshAlpha(&result, Prog__UsdScalar(usd, Prog__UsdField(usd, *opacity, S("default")), 1));
    return result;
}

// The material bound to prim `prim`, or UINT32_MAX.
function u32 Prog__UsdBound(const Prog__Usd *usd, const Prog__UsdProperties *properties, u32 prim)
{
    const u32 *binding = Prog__UsdProperty(usd, properties, prim, S("material:binding"));
    return binding ? Prog__UsdTarget(usd, Prog__UsdField(usd, *binding, S("targetPaths"))) : UINT32_MAX;
}

// A text layer, .usda, read into the tables a binary one fills: its prims and properties as paths and specs, each spec's fields, and their
// values written into a buffer of its own as the binary format lays them out, so that the meshes are read from it the same way. What the
// meshes do not need, as time samples, variants and references, is passed over.
function b32 Prog__UsdIsText(String data)
{
    return data.length >= 5 && MemCompare(data.value, "#usda", 5) == 0;
}

typedef struct Prog__UsdLayer {
    const char *at, *end;
    Arena *arena;
    Prog__Usd *usd;
    StrBuilder values;
    struct { ArrayHeader header; String *v; } tokens;
    struct { ArrayHeader header; u32 *v; } fieldTokens, pathParent, pathToken, specPath, specFieldSet;
    struct { ArrayHeader header; u64 *v; } fieldValues;
    struct { ArrayHeader header; i32 *v; } fieldSets;
    struct { ArrayHeader header; b8 *v; } pathProperty;
    struct { ArrayHeader header; String *v; } pathNames;                        // each path whole, as "/World/Cube.points", to find targets by
    struct { ArrayHeader header; struct { u64 at; String target; } *v; } links;  // list ops written before the paths they name were met
    u32 open[8], openCount;  // the fields of the spec being written
    u32 fieldsOpen;
} Prog__UsdLayer;

function void Prog__UsdTextSkip(Prog__UsdLayer *t)
{
    while (t->at < t->end) {
        char c = *t->at;
        if (c == '#') {
            while (t->at < t->end && *t->at != '\n') ++t->at;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';') {
            ++t->at;
        } else {
            break;
        }
    }
}

function b32 Prog__UsdTextNameByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.' || c == '-' || c == '+';
}

function String Prog__UsdTextName(Prog__UsdLayer *t)
{
    Prog__UsdTextSkip(t);
    const char *from = t->at;
    while (t->at < t->end && Prog__UsdTextNameByte(*t->at)) ++t->at;
    return StrMake(from, (u64)(t->at - from));
}

function b32 Prog__UsdTextIs(Prog__UsdLayer *t, char c)
{
    Prog__UsdTextSkip(t);
    if (t->at < t->end && *t->at == c) {
        ++t->at;
        return true;
    }
    return false;
}

// Text between the quote at the cursor and the one that ends it, three of them for a long one; between '@'s for an asset path.
function String Prog__UsdTextQuoted(Prog__UsdLayer *t)
{
    Prog__UsdTextSkip(t);
    if (t->at >= t->end) return (String){0};
    char quote = *t->at;
    b32 triple = t->end - t->at >= 3 && t->at[1] == quote && t->at[2] == quote;
    t->at += triple ? 3 : 1;
    const char *from = t->at;
    while (t->at < t->end && !(*t->at == quote && (!triple || (t->end - t->at >= 3 && t->at[1] == quote && t->at[2] == quote)))) t->at += *t->at == '\\' && t->end - t->at > 1 ? 2 : 1;
    String result = StrMake(from, (u64)(t->at - from));
    t->at = t->end - t->at > (triple ? 3 : 1) ? t->at + (triple ? 3 : 1) : t->end;
    return result;
}

// Past a value of any shape, nested brackets and braces with what is in them.
function void Prog__UsdTextPass(Prog__UsdLayer *t)
{
    Prog__UsdTextSkip(t);
    u32 depth = 0;
    do {
        if (t->at >= t->end) return;
        char c = *t->at;
        if (c == '"' || c == '\'' || c == '@') {
            Prog__UsdTextQuoted(t);
        } else if (c == '<') {
            while (t->at < t->end && *t->at != '>') ++t->at;
            ++t->at;
        } else if (c == '(' || c == '[' || c == '{') {
            ++depth, ++t->at;
        } else if (c == ')' || c == ']' || c == '}') {
            if (depth) --depth;
            ++t->at;
        } else if (depth) {
            ++t->at;
        } else if (!Prog__UsdTextName(t).length) {
            // A bare value is a word; anything else is passed a byte at a time.
            ++t->at;
        }
        Prog__UsdTextSkip(t);
    } while (depth);
}

function u32 Prog__UsdTextToken(Prog__UsdLayer *t, String text)
{
    for (u64 i = 0; i < t->tokens.header.count; ++i) if (StrIsEqual(t->tokens.v[i], text)) return (u32)i;
    ArrayPush(t->arena, t->tokens, StrCopy(t->arena, text));
    return (u32)(t->tokens.header.count - 1);
}

// A path's index, made under `parent` when it is new.
function u32 Prog__UsdTextPath(Prog__UsdLayer *t, u32 parent, String name, b32 property)
{
    String whole = parent == UINT32_MAX ? S("/") : StrFormat(t->arena, property ? "%S.%S" : parent == 0 ? "%S%S" : "%S/%S", t->pathNames.v[parent], name);
    for (u64 i = 0; i < t->pathNames.header.count; ++i) if (StrIsEqual(t->pathNames.v[i], whole)) return (u32)i;
    ArrayPush(t->arena, t->pathNames, whole);
    ArrayPush(t->arena, t->pathParent, parent);
    ArrayPush(t->arena, t->pathToken, Prog__UsdTextToken(t, parent == UINT32_MAX ? S("") : name));
    ArrayPush(t->arena, t->pathProperty, (b8)property);
    return (u32)(t->pathNames.header.count - 1);
}

// A spec for path `path`, whose fields the next Prog__UsdTextField calls add until Prog__UsdTextEndSpec.
function void Prog__UsdTextSpec(Prog__UsdLayer *t, u32 path)
{
    ArrayPush(t->arena, t->specPath, path);
    ArrayPush(t->arena, t->specFieldSet, (u32)t->fieldSets.header.count);
}

function void Prog__UsdTextField(Prog__UsdLayer *t, String name, u64 rep)
{
    ArrayPush(t->arena, t->fieldTokens, Prog__UsdTextToken(t, name));
    ArrayPush(t->arena, t->fieldValues, rep);
    ArrayPush(t->arena, t->fieldSets, (i32)(t->fieldTokens.header.count - 1));
}

function void Prog__UsdTextEndSpec(Prog__UsdLayer *t) { ArrayPush(t->arena, t->fieldSets, -1); }

#define Prog__UsdRep(type, array, inlined, payload) ((u64)(array) << 63 | (u64)(inlined) << 62 | (u64)(type) << 48 | ((u64)(payload) & UINT64_C(0xFFFFFFFFFFFF)))

function u64 Prog__UsdTextTokenRep(Prog__UsdLayer *t, String text) { return Prog__UsdRep(11, 0, 1, Prog__UsdTextToken(t, text)); }

// A target path written as a list op of one explicit item, its index filled in once every path is known.
function u64 Prog__UsdTextLink(Prog__UsdLayer *t, String target)
{
    u64 at = t->values.buffer.length;
    u8 header = 1 << 1;
    u64 count = 1;
    u32 none = UINT32_MAX;
    StrBuildStr(t->arena, &t->values, StrMake((const char *)&header, 1));
    StrBuildStr(t->arena, &t->values, StrMake((const char *)&count, 8));
    ArrayPush(t->arena, t->links, ((typeof(*t->links.v)){ t->values.buffer.length, StrCopy(t->arena, target) }));
    StrBuildStr(t->arena, &t->values, StrMake((const char *)&none, 4));
    return Prog__UsdRep(34, 0, 0, at);
}

// The numbers of a value, flattened, with how many make one element: 1 for plain numbers, else the size of its tuples.
function void Prog__UsdTextNumbers(Prog__UsdLayer *t, Prog_ArrayF32 *numbers, u32 *width, b32 *array)
{
    Prog__UsdTextSkip(t);
    *width = 1;
    *array = t->at < t->end && *t->at == '[';
    u32 depth = 0, inTuple = 0, tupleDepth = *array ? 2 : 1;
    do {
        Prog__UsdTextSkip(t);
        if (t->at >= t->end) break;
        char c = *t->at;
        if (c == '[' || c == '(') {
            ++depth, ++t->at;
            if (depth == tupleDepth) inTuple = 0;
        } else if (c == ']' || c == ')') {
            if (depth == tupleDepth && inTuple) *width = inTuple;
            if (depth) --depth;
            ++t->at;
        } else if (c == ',') {
            ++t->at;
        } else {
            f64 value = 0;
            const char *before = t->at;
            if (Prog__Number(&t->at, t->end, &value)) {
                ArrayPush(t->arena, *numbers, (f32)value);
                if (depth == tupleDepth) ++inTuple;
            } else {
                t->at = before;
                Prog__UsdTextPass(t);
            }
        }
    } while (depth && t->at < t->end);
}

// An attribute's value, by its type's name, as the binary format writes it; 0 for one the meshes do not use.
function u64 Prog__UsdTextValue(Prog__UsdLayer *t, String type)
{
    Prog__UsdTextSkip(t);
    if (t->at >= t->end) return 0;
    char c = *t->at;
    if (c == '"' || c == '\'' || c == '@') return Prog__UsdTextTokenRep(t, Prog__UsdTextQuoted(t));
    if (c == '<') {
        const char *from = ++t->at;
        while (t->at < t->end && *t->at != '>') ++t->at;
        String target = StrMake(from, (u64)(t->at - from));
        if (t->at < t->end) ++t->at;
        return Prog__UsdTextLink(t, target);
    }
    b32 tokens = StrIsEqual(type, S("token[]"));
    if (tokens) {
        // A list of tokens, as xformOpOrder is.
        struct { ArrayHeader header; u32 *v; } list = {0};
        Prog__UsdTextIs(t, '[');
        while (!Prog__UsdTextIs(t, ']') && t->at < t->end) {
            if (Prog__UsdTextIs(t, ',')) continue;
            Prog__UsdTextSkip(t);
            if (*t->at == '"' || *t->at == '\'') ArrayPush(t->arena, list, Prog__UsdTextToken(t, Prog__UsdTextQuoted(t)));
            else ++t->at;
        }
        u64 at = t->values.buffer.length, count = list.header.count;
        StrBuildStr(t->arena, &t->values, StrMake((const char *)&count, 8));
        if (count) StrBuildStr(t->arena, &t->values, StrMake((const char *)list.v, count * 4));
        return Prog__UsdRep(11, 1, 0, at);
    }
    Prog_ArrayF32 numbers = {0};
    u32 width = 1;
    b32 array = false;
    Prog__UsdTextNumbers(t, &numbers, &width, &array);
    u64 count = width ? numbers.header.count / width : 0;
    u64 at = t->values.buffer.length;
    if (StrIsEqual(type, S("int[]"))) {
        StrBuildStr(t->arena, &t->values, StrMake((const char *)&count, 8));
        for (u64 i = 0; i < numbers.header.count; ++i) {
            i32 v = (i32)numbers.v[i];
            StrBuildStr(t->arena, &t->values, StrMake((const char *)&v, 4));
        }
        return Prog__UsdRep(3, 1, 0, at);
    }
    if (StrIsEqual(type, S("matrix4d")) && numbers.header.count == 16) {
        for (u32 i = 0; i < 16; ++i) {
            f64 v = numbers.v[i];
            StrBuildStr(t->arena, &t->values, StrMake((const char *)&v, 8));
        }
        return Prog__UsdRep(15, 0, 0, at);
    }
    // A single number of a float type, inlined as the file inlines it.
    if (!array && width == 1 && numbers.header.count == 1 && (StrIsEqual(type, S("float")) || StrIsEqual(type, S("double")) || StrIsEqual(type, S("half")))) {
        u32 bits;
        MemCopy(&bits, &numbers.v[0], 4);
        return Prog__UsdRep(8, 0, 1, bits);
    }
    // Tuples of two, three or four floats: vec2f, vec3f and vec4f, single or arrays.
    if (width < 2 || width > 4 || !count) return 0;
    u32 kind = width == 2 ? 20 : width == 3 ? 24 : 28;
    if (array) StrBuildStr(t->arena, &t->values, StrMake((const char *)&count, 8));
    else count = 1;
    StrBuildStr(t->arena, &t->values, StrMake((const char *)numbers.v, count * width * 4));
    return Prog__UsdRep(kind, array, 0, at);
}

// The metadata in parentheses after a prim or a property: the fields kept are written to the spec being written.
function void Prog__UsdTextMetadata(Prog__UsdLayer *t)
{
    if (!Prog__UsdTextIs(t, '(')) return;
    while (!Prog__UsdTextIs(t, ')') && t->at < t->end) {
        Prog__UsdTextSkip(t);
        if (*t->at == '"') {
            // A layer's or prim's documentation.
            Prog__UsdTextQuoted(t);
            continue;
        }
        String word = Prog__UsdTextName(t);
        if (!word.length) {
            ++t->at;
            continue;
        }
        if (StrIsEqual(word, S("prepend")) || StrIsEqual(word, S("append")) || StrIsEqual(word, S("delete")) || StrIsEqual(word, S("add"))) word = Prog__UsdTextName(t);
        if (!Prog__UsdTextIs(t, '=')) continue;
        Prog__UsdTextSkip(t);
        if ((StrIsEqual(word, S("upAxis")) || StrIsEqual(word, S("interpolation"))) && t->at < t->end && (*t->at == '"' || *t->at == '\'')) {
            Prog__UsdTextField(t, word, Prog__UsdTextTokenRep(t, Prog__UsdTextQuoted(t)));
        } else {
            Prog__UsdTextPass(t);
        }
    }
}

// The prims and properties inside a prim's braces, or at the top of the layer, under the path `parent`.
function void Prog__UsdTextBody(Prog__UsdLayer *t, u32 parent, u32 depth)
{
    while (t->at < t->end && depth < 64) {
        Prog__UsdTextSkip(t);
        if (t->at >= t->end) break;
        if (*t->at == '}') {
            ++t->at;
            break;
        }
        String word = Prog__UsdTextName(t);
        if (!word.length) {
            ++t->at;
            continue;
        }
        if (StrIsEqual(word, S("def")) || StrIsEqual(word, S("over")) || StrIsEqual(word, S("class"))) {
            Prog__UsdTextSkip(t);
            String type = t->at < t->end && *t->at != '"' ? Prog__UsdTextName(t) : (String){0};
            String name = Prog__UsdTextQuoted(t);
            u32 path = Prog__UsdTextPath(t, parent, name, false);
            Prog__UsdTextSpec(t, path);
            if (type.length) Prog__UsdTextField(t, S("typeName"), Prog__UsdTextTokenRep(t, type));
            Prog__UsdTextMetadata(t);
            Prog__UsdTextEndSpec(t);
            if (Prog__UsdTextIs(t, '{')) Prog__UsdTextBody(t, path, depth + 1);
            continue;
        }
        if (StrIsEqual(word, S("variantSet")) || StrIsEqual(word, S("reorder"))) {
            // Variants are not chosen between; what follows is passed over to the end of its braces or line.
            while (t->at < t->end && *t->at != '{' && *t->at != '\n') ++t->at;
            if (t->at < t->end && *t->at == '{') Prog__UsdTextPass(t);
            continue;
        }
        // A property: its qualifiers, its type, a relationship's "rel", then its name and value.
        while (StrIsEqual(word, S("custom")) || StrIsEqual(word, S("uniform")) || StrIsEqual(word, S("varying")) || StrIsEqual(word, S("config"))) word = Prog__UsdTextName(t);
        b32 relationship = StrIsEqual(word, S("rel"));
        String type = relationship ? S("rel") : word;
        if (!relationship && Prog__UsdTextIs(t, '[')) {
            Prog__UsdTextIs(t, ']');
            type = StrFormat(t->arena, "%S[]", word);
        }
        String name = Prog__UsdTextName(t);
        b32 connection = name.length > 8 && StrIsEqual(StrFrom(name, name.length - 8), S(".connect"));
        b32 samples = name.length > 12 && StrIsEqual(StrFrom(name, name.length - 12), S(".timeSamples"));
        if (connection) name.length -= 8;
        u64 value = 0;
        if (Prog__UsdTextIs(t, '=')) {
            Prog__UsdTextSkip(t);
            if (samples || (t->at < t->end && t->at[0] == 'N' && t->end - t->at >= 4 && MemCompare(t->at, "None", 4) == 0)) Prog__UsdTextPass(t);
            else if (relationship && t->at < t->end && *t->at == '[') {
                // Several targets: the first is kept.
                ++t->at;
                Prog__UsdTextSkip(t);
                if (t->at < t->end && *t->at == '<') value = Prog__UsdTextValue(t, type);
                while (t->at < t->end && *t->at != ']') ++t->at;
                if (t->at < t->end) ++t->at;
            } else {
                value = Prog__UsdTextValue(t, type);
            }
        }
        if (samples || !name.length) {
            Prog__UsdTextMetadata(t);
            continue;
        }
        u32 path = Prog__UsdTextPath(t, parent, name, true);
        Prog__UsdTextSpec(t, path);
        Prog__UsdTextField(t, S("typeName"), Prog__UsdTextTokenRep(t, type));
        if (value) Prog__UsdTextField(t, relationship ? S("targetPaths") : connection ? S("connectionPaths") : S("default"), value);
        Prog__UsdTextMetadata(t);
        Prog__UsdTextEndSpec(t);
    }
}

function b32 Prog__UsdParseText(Arena *arena, String data, Prog__Usd *usd)
{
    Prog__UsdLayer t = { .at = data.value, .end = data.value + data.length, .arena = arena, .usd = usd };
    // Past the "#usda 1.0" line, then the layer's metadata, which goes to the root's spec.
    while (t.at < t.end && *t.at != '\n') ++t.at;
    u32 root = Prog__UsdTextPath(&t, UINT32_MAX, (String){0}, false);
    Prog__UsdTextSpec(&t, root);
    Prog__UsdTextMetadata(&t);
    Prog__UsdTextEndSpec(&t);
    Prog__UsdTextBody(&t, root, 0);
    // Each target now that every path is known: a prim, or a property of one, made where it was not met.
    u8 *values = (u8 *)t.values.buffer.value;
    for (u64 i = 0; i < t.links.header.count; ++i) {
        String target = t.links.v[i].target;
        Index dot = StrIndexChar(target, '.', SSF_SearchBackwards);
        String prim = dot.exists ? StrFromTo(target, 0, (i64)dot.value) : target;
        u32 found = UINT32_MAX;
        for (u64 p = 0; p < t.pathNames.header.count && found == UINT32_MAX; ++p) if (StrIsEqual(t.pathNames.v[p], prim)) found = (u32)p;
        if (found != UINT32_MAX && dot.exists) found = Prog__UsdTextPath(&t, found, StrFrom(target, dot.value + 1), true);
        MemCopy(values + t.links.v[i].at, &found, 4);
    }
    usd->file = t.values.buffer;
    usd->counts64 = true;
    usd->tokens = t.tokens.v;
    usd->tokenCount = (u32)t.tokens.header.count;
    usd->fieldTokens = t.fieldTokens.v;
    usd->fieldValues = t.fieldValues.v;
    usd->fieldCount = (u32)t.fieldTokens.header.count;
    usd->fieldSets = t.fieldSets.v;
    usd->fieldSetCount = (u32)t.fieldSets.header.count;
    usd->pathParent = t.pathParent.v;
    usd->pathToken = t.pathToken.v;
    usd->pathProperty = t.pathProperty.v;
    usd->pathCount = (u32)t.pathNames.header.count;
    usd->specPath = t.specPath.v;
    usd->specFieldSet = t.specFieldSet.v;
    usd->specType = ArenaPushArray(arena, Max(t.specPath.header.count, (u64)1), u32);
    usd->specCount = (u32)t.specPath.header.count;
    return usd->pathCount > 1;
}

// The meshes of a parsed stage, placed, colored and with their materials' looks, from the .usdz `zip` when it is one.
function b32 Prog__UsdMeshes(Arena *arena, Prog__Usd *usd, String zip, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    usd->xformOpOrder = Prog__UsdTokenIndex(usd, S("xformOpOrder"));
    // Each prim path's spec, and each property's by its prim and name; the geometry subsets, which bind materials to some of a mesh's faces.
    u32 *primSpec = ArenaPushArray(arena, Max(usd->pathCount, 1u), u32);
    for (u32 i = 0; i < usd->pathCount; ++i) primSpec[i] = UINT32_MAX;
    Prog__UsdProperties properties = {0};
    for (u32 s = 0; s < usd->specCount; ++s) {
        u32 p = usd->specPath[s];
        if (p >= usd->pathCount) continue;
        if (usd->pathProperty[p] && usd->pathParent[p] < usd->pathCount) MapPut(arena, properties, (u64)usd->pathParent[p] << 32 | usd->pathToken[p], s);
        else if (!usd->pathProperty[p]) primSpec[p] = s;
    }
    struct { ArrayHeader header; u32 *v; } subsets = {0};
    for (u32 prim = 0; prim < usd->pathCount; ++prim) {
        u64 type = primSpec[prim] != UINT32_MAX ? Prog__UsdField(usd, primSpec[prim], S("typeName")) : 0;
        if (type && StrIsEqual(Prog__UsdText(usd, type), S("GeomSubset"))) ArrayPush(arena, subsets, prim);
    }
    // Z is up as the stage says, which Blender's exports do; Y otherwise.
    u32 root = UINT32_MAX;
    for (u32 i = 0; i < usd->pathCount && root == UINT32_MAX; ++i) if (usd->pathParent[i] == UINT32_MAX && primSpec[i] != UINT32_MAX) root = i;
    u64 up = root != UINT32_MAX ? Prog__UsdField(usd, primSpec[root], S("upAxis")) : 0;
    mesh->zUp = up && Prog__UsdInlined(up) && Prog__UsdPayload(up) < usd->tokenCount && StrIsEqual(usd->tokens[Prog__UsdPayload(up)], S("Z"));
    Prog__Affine *world = ArenaPushArray(arena, Max(usd->pathCount, 1u), Prog__Affine);
    b8 *placed = ArenaPushArray(arena, Max(usd->pathCount, 1u), b8);
    u32 pointsToken = Prog__UsdTokenIndex(usd, S("points")), countsToken = Prog__UsdTokenIndex(usd, S("faceVertexCounts"));
    u32 indicesToken = Prog__UsdTokenIndex(usd, S("faceVertexIndices"));
    for (u32 prim = 0; prim < usd->pathCount && !P_IsCancelled(cancel); ++prim) {
        u64 type = primSpec[prim] != UINT32_MAX ? Prog__UsdField(usd, primSpec[prim], S("typeName")) : 0;
        if (!type || !Prog__UsdInlined(type) || Prog__UsdPayload(type) >= usd->tokenCount || !StrIsEqual(usd->tokens[Prog__UsdPayload(type)], S("Mesh"))) continue;
        const u32 *pointsSpec = MapGetPtrNull(properties, (u64)prim << 32 | pointsToken);
        const u32 *countsSpec = MapGetPtrNull(properties, (u64)prim << 32 | countsToken), *indicesSpec = MapGetPtrNull(properties, (u64)prim << 32 | indicesToken);
        if (!pointsSpec || !countsSpec || !indicesSpec) continue;
        u64 pointCount, faceCount, cornerCount;
        f32 *points = Prog__UsdVec3(arena, usd, Prog__UsdField(usd, *pointsSpec, S("default")), &pointCount);
        i32 *counts = Prog__UsdIntArray(arena, usd, Prog__UsdField(usd, *countsSpec, S("default")), &faceCount);
        i32 *corners = Prog__UsdIntArray(arena, usd, Prog__UsdField(usd, *indicesSpec, S("default")), &cornerCount);
        if (!points || !counts || !corners || pointCount >= UINT32_MAX) continue;
        // Its place: its own ops, then each parent's, worked out once for each.
        u32 chain[256], depth = 0;
        for (u32 p = prim; p < usd->pathCount && !placed[p] && depth < CArrayCount(chain); p = usd->pathParent[p]) chain[depth++] = p;
        while (depth--) {
            u32 p = chain[depth], up2 = usd->pathParent[p];
            Prog__Affine above = up2 < usd->pathCount && placed[up2] ? world[up2] : Prog__AffineIdentity;
            world[p] = Prog__AffineMultiply(above, Prog__UsdLocal(arena, usd, p, primSpec, &properties));
            placed[p] = true;
        }
        Scratch scratch = GetScratchEx(arena);
        f32 *at = ArenaPushArrayNoZero(scratch.arena, Max(pointCount, (u64)1) * 3, f32);
        for (u64 v = 0; v < pointCount; ++v) Prog__MeshPoint(at + v * 3, &world[prim], points[v * 3], points[v * 3 + 1], points[v * 3 + 2]);
        // Its looks: its own material's, then each subset's, whose faces they are drawn in; the texture coordinates the first picture names.
        Prog_MeshPart looks[17];
        u32 lookCount = 1, *lookOf = ArenaPushArray(scratch.arena, Max(faceCount, (u64)1), u32);
        String uvName = S("primvars:st");
        u32 bound = Prog__UsdBound(usd, &properties, prim);
        looks[0] = bound != UINT32_MAX ? Prog__UsdLook(arena, usd, &properties, bound, zip, mesh, &uvName) : (Prog_MeshPart){ .image = UINT32_MAX };
        for (u64 i = 0; i < subsets.header.count && lookCount < CArrayCount(looks); ++i) {
            u32 subset = subsets.v[i];
            const u32 *faces = usd->pathParent[subset] == prim ? Prog__UsdProperty(usd, &properties, subset, S("indices")) : NULL;
            u32 material = faces ? Prog__UsdBound(usd, &properties, subset) : UINT32_MAX;
            u64 n = 0;
            i32 *list = material != UINT32_MAX ? Prog__UsdIntArray(scratch.arena, usd, Prog__UsdField(usd, *faces, S("default")), &n) : NULL;
            if (!list) continue;
            looks[lookCount] = Prog__UsdLook(arena, usd, &properties, material, zip, mesh, &uvName);
            for (u64 k = 0; k < n; ++k) if (list[k] >= 0 && (u64)list[k] < faceCount) lookOf[list[k]] = lookCount;
            ++lookCount;
        }
        // Its vertices' colors, linear: displayColor, or else the first color primvar, as Blender writes a color attribute.
        Prog__UsdPrimvar uv, color;
        b32 mapped = Prog__UsdReadPrimvar(scratch.arena, usd, &properties, prim, uvName, 2, &uv);
        b32 colored = Prog__UsdReadPrimvar(scratch.arena, usd, &properties, prim, S("primvars:displayColor"), 4, &color);
        for (u32 s = 0; s < usd->specCount && !colored; ++s) {
            u32 p = usd->specPath[s];
            if (p >= usd->pathCount || !usd->pathProperty[p] || usd->pathParent[p] != prim) continue;
            String name = usd->tokens[usd->pathToken[p] < usd->tokenCount ? usd->pathToken[p] : 0], typeName = Prog__UsdText(usd, Prog__UsdField(usd, s, S("typeName")));
            if (StrIsEqual(StrFromTo(name, 0, 9), S("primvars:")) && StrIsEqual(StrFromTo(typeName, 0, 5), S("color"))) colored = Prog__UsdReadPrimvar(scratch.arena, usd, &properties, prim, name, 4, &color);
        }
        // A color3 has no alpha, which reads as 0 here.
        b32 opaque = colored && color.count && color.values[3] == 0;
        Prog__Corners made = Prog__CornersMake(scratch.arena, pointCount, cornerCount);
        u64 *starts = ArenaPushArrayNoZero(scratch.arena, Max(faceCount, (u64)1), u64);
        for (u64 f = 0, first = 0; f < faceCount; first += (u64)Max(counts[f], 0), ++f) starts[f] = first;
        for (u32 look = 0; look < lookCount; ++look) {
            Prog__MeshLook(arena, mesh, &looks[look]);
            for (u64 f = 0; f < faceCount; ++f) {
                if (lookOf[f] != look) continue;
                u64 first = starts[f], end = Min(first + (u64)Max(counts[f], 0), cornerCount);
                u32 fan = 0, previous = 0;
                for (u64 c = first; c < end; ++c) {
                    i32 point = corners[c];
                    u32 corner = UINT32_MAX;
                    if (point >= 0 && (u64)point < pointCount) {
                        u64 uvAt = mapped ? Prog__UsdPrimvarAt(&uv, c, (u64)point, f) : UINT64_MAX, colorAt = colored ? Prog__UsdPrimvarAt(&color, c, (u64)point, f) : UINT64_MAX;
                        Prog__MeshCorner key = { (u32)point, (u32)(uvAt + 1), (u32)(colorAt + 1), 0 };
                        corner = Prog__CornersFind(&made, key);
                        if (corner == UINT32_MAX && mesh->positions.header.count / 3 < UINT32_MAX) {
                            corner = (u32)(mesh->positions.header.count / 3);
                            MemCopy(ArrayAdd(arena, mesh->positions, 3), at + (u64)point * 3, 12);
                            if (uvAt != UINT64_MAX) {
                                f32 *t = Prog__MeshUvs(arena, mesh) + (u64)corner * 2;
                                t[0] = uv.values[uvAt * 2];
                                t[1] = 1 - uv.values[uvAt * 2 + 1];
                            }
                            if (colorAt != UINT64_MAX) {
                                const f32 *rgba = color.values + colorAt * 4;
                                Prog__MeshColors(arena, mesh)[corner] = Prog__LinearColor(rgba[0], rgba[1], rgba[2], opaque ? 1 : rgba[3]) | 0xFF000000u;
                            }
                            Prog__CornersAdd(&made, key, corner);
                        }
                    }
                    if (c == first) fan = corner;
                    else if (c >= first + 2) Prog__MeshTriangle(arena, mesh, fan, previous, corner);
                    previous = corner;
                }
            }
        }
        ReleaseScratch(scratch);
    }
    return mesh->indices.header.count > 0;
}

function b32 Prog__ReadUsd(Arena *arena, String path, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    // A .usdz is a zip whose first file is its root layer, and which holds the pictures its materials use.
    String zip = {0};
    if (data.length >= 4 && Prog__Read32((const u8 *)data.value) == 0x04034B50) zip = data, data = Prog__ZipView(arena, zip, (String){0});
    Prog__Usd usd = {0};
    if (!(Prog__UsdIsText(data) ? Prog__UsdParseText(arena, data, &usd) : Prog__UsdParse(arena, data, &usd))) return false;
    return Prog__UsdMeshes(arena, &usd, zip, cancel, mesh);
}

// -----------------------------------------------------------------------------
// XML, for Collada and 3MF: elements with their attributes left as written, to be looked through by name, and the text that first follows
// each element's start, as a list of numbers is, in place in the file.

typedef struct Prog__Xml Prog__Xml;
struct Prog__Xml {
    String name, attributes, text;
    Prog__Xml *first, *last, *next;
};

// Past `from` to just after `marker`, or to the end.
function const char *Prog__XmlPast(const char *from, const char *end, const char *marker)
{
    u64 length = 0;
    while (marker[length]) ++length;
    for (const char *p = from; p + length <= end; ++p) if (MemCompare(p, marker, length) == 0) return p + length;
    return end;
}

function Prog__Xml *Prog__XmlParse(Arena *arena, String data, const P_CancelToken *cancel)
{
    Prog__Xml *document = ArenaPushStruct(arena, Prog__Xml), *stack[256] = { document };
    u32 depth = 0;
    const char *p = data.value, *end = data.value + data.length;
    for (u64 step = 0; p < end; ++step) {
        if ((step & 0xFFFF) == 0 && P_IsCancelled(cancel)) return NULL;
        const char *open = p;
        while (open < end && *open != '<') ++open;
        Prog__Xml *top = stack[depth];
        if (open > p && !top->text.length) top->text = StrMake(p, (u64)(open - p));
        if (open >= end) break;
        p = open + 1;
        if (p < end && *p == '?') {
            p = Prog__XmlPast(p, end, "?>");
        } else if (end - p >= 3 && MemCompare(p, "!--", 3) == 0) {
            p = Prog__XmlPast(p, end, "-->");
        } else if (end - p >= 8 && MemCompare(p, "![CDATA[", 8) == 0) {
            const char *from = p + 8;
            p = Prog__XmlPast(from, end, "]]>");
            if (!top->text.length) top->text = StrMake(from, (u64)Max((i64)(p - from) - 3, (i64)0));
        } else if (p < end && *p == '!') {
            p = Prog__XmlPast(p, end, ">");
        } else if (p < end && *p == '/') {
            p = Prog__XmlPast(p, end, ">");
            if (depth) --depth;
        } else {
            const char *name = p;
            while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '/' && *p != '>') ++p;
            const char *attributes = p;
            char quote = 0;
            while (p < end && (quote || *p != '>')) {
                if (quote ? *p == quote : (*p == '"' || *p == '\'')) quote = quote ? 0 : *p;
                ++p;
            }
            b32 closed = p > attributes && p[-1] == '/';
            Prog__Xml *element = ArenaPushStruct(arena, Prog__Xml);
            element->name = StrMake(name, (u64)(attributes - name));
            element->attributes = StrMake(attributes, (u64)(p - attributes - (closed ? 1 : 0)));
            if (top->last) top->last->next = element;
            else top->first = element;
            top->last = element;
            if (p < end) ++p;
            if (!closed && depth + 1 < CArrayCount(stack)) stack[++depth] = element;
        }
    }
    return document;
}

// Whether the element is named `name`, whatever namespace prefix it is written with.
function b32 Prog__XmlIs(const Prog__Xml *element, String name)
{
    if (!element) return false;
    Index colon = StrIndexChar(element->name, ':', SSF_None);
    return StrIsEqual(colon.exists ? StrFrom(element->name, colon.value + 1) : element->name, name);
}

function Prog__Xml *Prog__XmlChild(const Prog__Xml *element, String name)
{
    for (Prog__Xml *child = element ? element->first : NULL; child; child = child->next) if (Prog__XmlIs(child, name)) return child;
    return NULL;
}

// The value of the attribute `name`, as written between its quotes; empty without it.
function String Prog__XmlValue(const Prog__Xml *element, String name)
{
    if (!element) return (String){0};
    String a = element->attributes;
    for (u64 at = 0; at + name.length < a.length; ++at) {
        if (MemCompare(a.value + at, name.value, name.length) != 0 || (at && !Prog__IsSpace(a.value[at - 1]) && a.value[at - 1] != '\n')) continue;
        u64 p = at + name.length;
        while (p < a.length && (Prog__IsSpace(a.value[p]) || a.value[p] == '\n')) ++p;
        if (p >= a.length || a.value[p] != '=') continue;
        ++p;
        while (p < a.length && (Prog__IsSpace(a.value[p]) || a.value[p] == '\n')) ++p;
        if (p >= a.length || (a.value[p] != '"' && a.value[p] != '\'')) continue;
        char quote = a.value[p++];
        u64 from = p;
        while (p < a.length && a.value[p] != quote) ++p;
        return StrFromTo(a, from, (i64)p);
    }
    return (String){0};
}

function f64 Prog__XmlNumber(const Prog__Xml *element, String name, f64 fallback)
{
    String text = Prog__XmlValue(element, name);
    const char *p = text.value;
    f64 value = 0;
    return text.length && Prog__Number(&p, text.value + text.length, &value) ? value : fallback;
}

// The numbers of an element's text, as many as there are up to `limit`.
function f32 *Prog__XmlNumbers(Arena *arena, String text, u64 limit, u64 *count)
{
    f32 *result = ArenaPushArrayNoZero(arena, Max(Min(limit, text.length / 2 + 1), (u64)1), f32);
    const char *p = text.value, *end = text.value + text.length;
    u64 n = 0;
    while (p < end && n < limit) {
        while (p < end && (Prog__IsSpace(*p) || *p == '\n' || *p == ',')) ++p;
        f64 value = 0;
        const char *before = p;
        if (p >= end) break;
        if (Prog__Number(&p, end, &value)) result[n++] = (f32)value;
        if (p == before) ++p;
    }
    *count = n;
    return result;
}

// Elements by their ids, kept as pointers.
typedef struct Prog__XmlIds { MapHeader header; struct { String key; u64 value; } *v; } Prog__XmlIds;

// The element a "#id" or "id" names.
function Prog__Xml *Prog__XmlAt(const Prog__XmlIds *ids, String reference)
{
    if (reference.length && reference.value[0] == '#') reference = StrFrom(reference, 1);
    return reference.length ? (Prog__Xml *)(uintptr_t)MapsGet(*ids, u64, reference) : NULL;
}

// -----------------------------------------------------------------------------
// Collada (.dae): the visual scene's nodes, placed by their matrix, translate, rotate and scale steps, and the geometries they name, each
// primitive in its bound material's diffuse color or texture; without a scene, every geometry where it is.


typedef struct Prog__Dae {
    Prog__XmlIds ids;
    Prog_Mesh *mesh;
    const P_CancelToken *cancel;
} Prog__Dae;

function Prog__Xml *Prog__DaeAt(const Prog__Dae *dae, String reference) { return Prog__XmlAt(&dae->ids, reference); }

function void Prog__DaeIndex(Arena *arena, Prog__Dae *dae, Prog__Xml *element, u32 depth)
{
    for (Prog__Xml *child = element->first; child && depth < 128; child = child->next) {
        String id = Prog__XmlValue(child, S("id"));
        if (id.length && !MapsGet(dae->ids, u64, id)) MapsPut(arena, dae->ids, id, (u64)(uintptr_t)child);
        Prog__DaeIndex(arena, dae, child, depth + 1);
    }
}

// A source's numbers and how many make one element.
function f32 *Prog__DaeSource(Arena *arena, const Prog__Dae *dae, String reference, u64 *count, u32 *stride)
{
    Prog__Xml *source = Prog__DaeAt(dae, reference), *values = Prog__XmlChild(source, S("float_array"));
    Prog__Xml *accessor = Prog__XmlChild(Prog__XmlChild(source, S("technique_common")), S("accessor"));
    if (!values) return NULL;
    u64 n = 0;
    f32 *numbers = Prog__XmlNumbers(arena, values->text, (u64)Prog__XmlNumber(values, S("count"), (f64)UINT32_MAX), &n);
    *stride = (u32)Max(Prog__XmlNumber(accessor, S("stride"), 1), 1.0);
    *count = n / *stride;
    return numbers;
}

// A bound material's look: its effect's diffuse color, or the picture its texture samples, found through the effect's sampler and surface
// parameters to the image they name.
function Prog_MeshPart Prog__DaeLook(Arena *arena, const Prog__Dae *dae, Prog__Xml *material)
{
    Prog_MeshPart result = { .image = UINT32_MAX };
    Prog__Xml *effect = Prog__DaeAt(dae, Prog__XmlValue(Prog__XmlChild(material, S("instance_effect")), S("url")));
    Prog__Xml *common = Prog__XmlChild(effect, S("profile_COMMON")), *technique = Prog__XmlChild(common, S("technique")), *shading = NULL;
    String kinds[] = { S("phong"), S("lambert"), S("blinn"), S("constant") };
    for (u32 i = 0; i < CArrayCount(kinds) && !shading; ++i) shading = Prog__XmlChild(technique, kinds[i]);
    Prog__Xml *diffuse = Prog__XmlChild(shading, S("diffuse"));
    if (!diffuse) diffuse = Prog__XmlChild(shading, S("emission"));
    Prog__Xml *color = Prog__XmlChild(diffuse, S("color")), *texture = Prog__XmlChild(diffuse, S("texture"));
    if (color) {
        u64 n = 0;
        f32 *rgba = Prog__XmlNumbers(arena, color->text, 4, &n);
        if (n >= 3) result.color = Prog__LinearColor(rgba[0], rgba[1], rgba[2], 1);
    }
    // A transparent color's alpha by the transparency, as the default A_ONE mode has it.
    Prog__Xml *transparent = Prog__XmlChild(shading, S("transparent")), *amount = Prog__XmlChild(Prog__XmlChild(shading, S("transparency")), S("float"));
    if (transparent && amount && !StrIsEqual(Prog__XmlValue(transparent, S("opaque")), S("RGB_ZERO"))) {
        u64 n = 0, m = 0;
        f32 *rgba = Prog__XmlNumbers(arena, Prog__XmlChild(transparent, S("color")) ? Prog__XmlChild(transparent, S("color"))->text : (String){0}, 4, &n);
        f32 *factor = Prog__XmlNumbers(arena, amount->text, 1, &m);
        f32 alpha = (n >= 4 ? rgba[3] : 1) * (m ? factor[0] : 1);
        if (n >= 4 || m) Prog__MeshAlpha(&result, alpha);
    }
    if (texture) {
        // The sampler's source names the surface, whose init_from names the image; Collada 1.5 and some exporters name the image at once.
        String name = Prog__XmlValue(texture, S("texture")), image = name;
        for (u32 hop = 0; hop < 2; ++hop) {
            for (Prog__Xml *param = common ? common->first : NULL; param; param = param->next) {
                if (!Prog__XmlIs(param, S("newparam")) || !StrIsEqual(Prog__XmlValue(param, S("sid")), image)) continue;
                Prog__Xml *sampler = Prog__XmlChild(param, S("sampler2D")), *surface = Prog__XmlChild(param, S("surface"));
                Prog__Xml *next = sampler ? Prog__XmlChild(sampler, S("source")) : Prog__XmlChild(surface, S("init_from"));
                Prog__Xml *instance = Prog__XmlChild(sampler, S("instance_image"));
                if (instance) image = Prog__XmlValue(instance, S("url"));
                else if (next) image = next->text;
                break;
            }
        }
        Prog__Xml *picture = Prog__DaeAt(dae, image), *from = Prog__XmlChild(picture, S("init_from"));
        Prog__Xml *ref = Prog__XmlChild(from, S("ref"));
        String file = ref ? ref->text : from ? from->text : (String){0};
        while (file.length && (Prog__IsSpace(file.value[0]) || file.value[0] == '\n')) file = StrFrom(file, 1);
        while (file.length && (Prog__IsSpace(file.value[file.length - 1]) || file.value[file.length - 1] == '\n')) --file.length;
        if (file.length > 8 && MemCompare(file.value, "file:///", 8) == 0) file = StrFrom(file, 8);
        else if (file.length > 7 && MemCompare(file.value, "file://", 7) == 0) file = StrFrom(file, 7);
        if (file.length) {
            result.bare = result.color;
            result.image = Prog__MeshImage(arena, dae->mesh, Prog__UriName(arena, file), (String){0});
        }
    }
    return result;
}

// One of a mesh's primitives, its corners' indices in `p`: the triangles, polylist or polygons element `primitive`, with `vcount` corners a
// polygon, or three.
function void Prog__DaePrimitive(Arena *arena, const Prog__Dae *dae, const Prog__Xml *primitive, const Prog__Affine *place, Prog_MeshPart look)
{
    Prog_Mesh *mesh = dae->mesh;
    Scratch scratch = GetScratchEx(arena);
    // Its inputs, each at an offset in a corner's indices: the points through the vertices element, and the first texture coordinates and
    // colors.
    u32 stride = 1, pointAt = UINT32_MAX, uvAt = UINT32_MAX, colorAt = UINT32_MAX;
    u32 pointWidth = 3, uvWidth = 2, colorWidth = 3;
    u64 pointCount = 0, uvCount = 0, colorCount = 0;
    f32 *points = NULL, *uvs = NULL, *colors = NULL;
    for (Prog__Xml *input = primitive->first; input; input = input->next) {
        if (!Prog__XmlIs(input, S("input"))) continue;
        u32 offset = (u32)Prog__XmlNumber(input, S("offset"), 0);
        stride = Max(stride, offset + 1);
        String semantic = Prog__XmlValue(input, S("semantic")), source = Prog__XmlValue(input, S("source"));
        if (StrIsEqual(semantic, S("VERTEX"))) {
            Prog__Xml *vertices = Prog__DaeAt(dae, source);
            for (Prog__Xml *v = vertices ? vertices->first : NULL; v; v = v->next) {
                String kind = Prog__XmlValue(v, S("semantic"));
                if (StrIsEqual(kind, S("POSITION"))) points = Prog__DaeSource(scratch.arena, dae, Prog__XmlValue(v, S("source")), &pointCount, &pointWidth), pointAt = offset;
            }
        } else if (StrIsEqual(semantic, S("TEXCOORD")) && uvAt == UINT32_MAX) {
            uvs = Prog__DaeSource(scratch.arena, dae, source, &uvCount, &uvWidth), uvAt = offset;
        } else if (StrIsEqual(semantic, S("COLOR")) && colorAt == UINT32_MAX) {
            colors = Prog__DaeSource(scratch.arena, dae, source, &colorCount, &colorWidth), colorAt = offset;
        }
    }
    if (!points || pointWidth < 3 || pointCount >= UINT32_MAX) {
        ReleaseScratch(scratch);
        return;
    }
    if (!uvs || uvWidth < 2) uvAt = UINT32_MAX;
    if (!colors || colorWidth < 3) colorAt = UINT32_MAX;
    f32 *at = ArenaPushArrayNoZero(scratch.arena, Max(pointCount, (u64)1) * 3, f32);
    for (u64 v = 0; v < pointCount; ++v) Prog__MeshPoint(at + v * 3, place, points[v * pointWidth], points[v * pointWidth + 1], points[v * pointWidth + 2]);
    Prog__MeshLook(arena, mesh, &look);
    b32 polylist = Prog__XmlIs(primitive, S("polylist"));
    u64 sizeCount = 0;
    f32 *sizes = polylist ? Prog__XmlNumbers(scratch.arena, Prog__XmlChild(primitive, S("vcount")) ? Prog__XmlChild(primitive, S("vcount"))->text : (String){0}, UINT32_MAX, &sizeCount) : NULL;
    Prog__Corners made = Prog__CornersMake(scratch.arena, pointCount, pointCount * 2);
    // Polygons have a <p> each; the others one for all of them.
    for (Prog__Xml *list = primitive->first; list; list = list->next) {
        if (!Prog__XmlIs(list, S("p"))) continue;
        u64 n = 0;
        f32 *indices = Prog__XmlNumbers(scratch.arena, list->text, UINT32_MAX, &n);
        u64 cornerCount = n / stride, corner = 0;
        for (u64 polygon = 0; corner < cornerCount && !P_IsCancelled(dae->cancel); ++polygon) {
            u64 size = polylist ? (polygon < sizeCount ? (u64)sizes[polygon] : 0) : Prog__XmlIs(primitive, S("polygons")) ? cornerCount : 3;
            if (!size) break;
            u32 fan = 0, previous = 0;
            for (u64 k = 0; k < size && corner < cornerCount; ++k, ++corner) {
                const f32 *c = indices + corner * stride;
                u64 point = (u64)c[pointAt];
                u64 uv = uvAt != UINT32_MAX ? (u64)c[uvAt] : UINT64_MAX, color = colorAt != UINT32_MAX ? (u64)c[colorAt] : UINT64_MAX;
                if (uv != UINT64_MAX && uv >= uvCount) uv = UINT64_MAX;
                if (color != UINT64_MAX && color >= colorCount) color = UINT64_MAX;
                u32 vertex = UINT32_MAX;
                if (point < pointCount) {
                    Prog__MeshCorner key = { (u32)point, (u32)(uv + 1), (u32)(color + 1), 0 };
                    vertex = Prog__CornersFind(&made, key);
                    if (vertex == UINT32_MAX && mesh->positions.header.count / 3 < UINT32_MAX) {
                        vertex = (u32)(mesh->positions.header.count / 3);
                        MemCopy(ArrayAdd(arena, mesh->positions, 3), at + point * 3, 12);
                        if (uv != UINT64_MAX) {
                            f32 *t = Prog__MeshUvs(arena, mesh) + (u64)vertex * 2;
                            t[0] = uvs[uv * uvWidth];
                            t[1] = 1 - uvs[uv * uvWidth + 1];
                        }
                        if (color != UINT64_MAX) {
                            const f32 *rgb = colors + color * colorWidth;
                            Prog__MeshColors(arena, mesh)[vertex] = Prog__LinearColor(rgb[0], rgb[1], rgb[2], colorWidth >= 4 ? rgb[3] : 1) | 0xFF000000u;
                        }
                        Prog__CornersAdd(&made, key, vertex);
                    }
                }
                if (k == 0) fan = vertex;
                else if (k >= 2) Prog__MeshTriangle(arena, mesh, fan, previous, vertex);
                previous = vertex;
            }
        }
    }
    ReleaseScratch(scratch);
}

// A geometry placed by `place`, its primitives' materials bound by the instance `instance` names them in.
function void Prog__DaeGeometry(Arena *arena, const Prog__Dae *dae, const Prog__Xml *geometry, const Prog__Xml *instance, const Prog__Affine *place)
{
    Prog__Xml *mesh = Prog__XmlChild(geometry, S("mesh"));
    Prog__Xml *bound = Prog__XmlChild(Prog__XmlChild(instance, S("bind_material")), S("technique_common"));
    for (Prog__Xml *primitive = mesh ? mesh->first : NULL; primitive && !P_IsCancelled(dae->cancel); primitive = primitive->next) {
        if (!Prog__XmlIs(primitive, S("triangles")) && !Prog__XmlIs(primitive, S("polylist")) && !Prog__XmlIs(primitive, S("polygons"))) continue;
        String symbol = Prog__XmlValue(primitive, S("material"));
        Prog__Xml *material = NULL;
        for (Prog__Xml *link = bound ? bound->first : NULL; link && symbol.length && !material; link = link->next) {
            if (Prog__XmlIs(link, S("instance_material")) && StrIsEqual(Prog__XmlValue(link, S("symbol")), symbol)) material = Prog__DaeAt(dae, Prog__XmlValue(link, S("target")));
        }
        if (!material) material = Prog__DaeAt(dae, symbol);
        Prog_MeshPart look = material ? Prog__DaeLook(arena, dae, material) : (Prog_MeshPart){ .image = UINT32_MAX };
        Prog__DaePrimitive(arena, dae, primitive, place, look);
    }
}

// A node's own steps in their order, after its parent's placement.
function Prog__Affine Prog__DaePlace(Arena *arena, const Prog__Xml *node, Prog__Affine place)
{
    for (Prog__Xml *step = node->first; step; step = step->next) {
        u64 n = 0;
        Scratch scratch = GetScratchEx(arena);
        f32 *v = Prog__XmlNumbers(scratch.arena, step->text, 16, &n);
        Prog__Affine local = Prog__AffineIdentity;
        f32 zero[3] = {0}, ones[3] = { 1, 1, 1 }, identity[4] = { 0, 0, 0, 1 };
        b32 used = true;
        if (Prog__XmlIs(step, S("matrix")) && n == 16) {
            for (u32 i = 0; i < 12; ++i) local.m[i] = v[i];
        } else if (Prog__XmlIs(step, S("translate")) && n == 3) {
            local = Prog__AffineCompose(v, identity, ones);
        } else if (Prog__XmlIs(step, S("scale")) && n == 3) {
            local = Prog__AffineCompose(zero, identity, v);
        } else if (Prog__XmlIs(step, S("rotate")) && n == 4) {
            f32 length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), half = v[3] * 3.14159265f / 360.0f, s = length > 0 ? sinf(half) / length : 0;
            f32 q[4] = { v[0] * s, v[1] * s, v[2] * s, cosf(half) };
            local = Prog__AffineCompose(zero, q, ones);
        } else {
            used = false;
        }
        if (used) place = Prog__AffineMultiply(place, local);
        ReleaseScratch(scratch);
    }
    return place;
}

function void Prog__DaeNode(Arena *arena, const Prog__Dae *dae, const Prog__Xml *node, Prog__Affine parent, u32 depth)
{
    if (depth > 64 || P_IsCancelled(dae->cancel)) return;
    Prog__Affine place = Prog__DaePlace(arena, node, parent);
    for (Prog__Xml *child = node->first; child; child = child->next) {
        if (Prog__XmlIs(child, S("instance_geometry"))) {
            Prog__Xml *geometry = Prog__DaeAt(dae, Prog__XmlValue(child, S("url")));
            if (geometry) Prog__DaeGeometry(arena, dae, geometry, child, &place);
        } else if (Prog__XmlIs(child, S("instance_node"))) {
            Prog__Xml *shared = Prog__DaeAt(dae, Prog__XmlValue(child, S("url")));
            if (shared) Prog__DaeNode(arena, dae, shared, place, depth + 1);
        } else if (Prog__XmlIs(child, S("node"))) {
            Prog__DaeNode(arena, dae, child, place, depth + 1);
        }
    }
}

function b32 Prog__ReadCollada(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    Prog__Xml *document = Prog__XmlParse(arena, data, cancel), *collada = Prog__XmlChild(document, S("COLLADA"));
    if (!collada) return false;
    Prog__Dae dae = { .mesh = mesh, .cancel = cancel };
    Prog__DaeIndex(arena, &dae, collada, 0);
    Prog__Xml *up = Prog__XmlChild(Prog__XmlChild(collada, S("asset")), S("up_axis"));
    mesh->zUp = up && StrIndexStr(up->text, S("Z_UP"), SSF_None).exists;
    Prog__Xml *scene = Prog__DaeAt(&dae, Prog__XmlValue(Prog__XmlChild(Prog__XmlChild(collada, S("scene")), S("instance_visual_scene")), S("url")));
    if (!scene) scene = Prog__XmlChild(Prog__XmlChild(collada, S("library_visual_scenes")), S("visual_scene"));
    if (scene) {
        for (Prog__Xml *node = scene->first; node; node = node->next) if (Prog__XmlIs(node, S("node"))) Prog__DaeNode(arena, &dae, node, Prog__AffineIdentity, 0);
    }
    if (!mesh->indices.header.count) {
        Prog__Xml *library = Prog__XmlChild(collada, S("library_geometries"));
        for (Prog__Xml *geometry = library ? library->first : NULL; geometry; geometry = geometry->next) {
            if (Prog__XmlIs(geometry, S("geometry"))) Prog__DaeGeometry(arena, &dae, geometry, NULL, &Prog__AffineIdentity);
        }
    }
    return mesh->indices.header.count > 0;
}

// -----------------------------------------------------------------------------
// 3MF: a zip whose model part lists objects, each a mesh or components placed in it, and the build's items that place them, Z up. A
// triangle takes its color from a base material or a color group, its corners' each, or its picture's coordinates from a texture group.

typedef struct Prog__3mf {
    Prog__XmlIds ids;  // the resources by their ids
    String zip;
    Prog_Mesh *mesh;
    const P_CancelToken *cancel;
} Prog__3mf;

// A "#RRGGBB" or "#RRGGBBAA" color, as straight RGBA; 0 for another.
function u32 Prog__3mfColor(String text)
{
    if ((text.length != 7 && text.length != 9) || text.value[0] != '#') return 0;
    u32 bytes[4] = { 0, 0, 0, 255 };
    for (u32 i = 0; i < (u32)(text.length - 1) / 2; ++i) {
        i32 high = Prog__Hex(text.value[1 + i * 2]), low = Prog__Hex(text.value[2 + i * 2]);
        if (high < 0 || low < 0) return 0;
        bytes[i] = (u32)(high * 16 + low);
    }
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24;
}

// A transform attribute's twelve numbers, written for row vectors, as a placement.
function Prog__Affine Prog__3mfTransform(Arena *arena, const Prog__Xml *element)
{
    Prog__Affine result = Prog__AffineIdentity;
    String text = Prog__XmlValue(element, S("transform"));
    u64 n = 0;
    Scratch scratch = GetScratchEx(arena);
    f32 *v = text.length ? Prog__XmlNumbers(scratch.arena, text, 12, &n) : NULL;
    if (n == 12) {
        for (u32 row = 0; row < 3; ++row) for (u32 column = 0; column < 4; ++column) result.m[row * 4 + column] = v[column * 3 + row];
    }
    ReleaseScratch(scratch);
    return result;
}

// The `index`th child of a property group: a base material, a color or a texture coordinate.
function Prog__Xml *Prog__3mfEntry(const Prog__Xml *group, u64 index)
{
    u64 n = 0;
    for (Prog__Xml *child = group ? group->first : NULL; child; child = child->next) if (child->name.length && n++ == index) return child;
    return NULL;
}

function void Prog__3mfMesh(Arena *arena, const Prog__3mf *m, const Prog__Xml *object, const Prog__Xml *mesh, const Prog__Affine *place)
{
    Prog_Mesh *out = m->mesh;
    Scratch scratch = GetScratchEx(arena);
    Prog__Xml *vertices = Prog__XmlChild(mesh, S("vertices")), *triangles = Prog__XmlChild(mesh, S("triangles"));
    u64 pointCount = 0;
    for (Prog__Xml *v = vertices ? vertices->first : NULL; v; v = v->next) pointCount += Prog__XmlIs(v, S("vertex")) ? 1 : 0;
    if (!pointCount || pointCount >= UINT32_MAX) {
        ReleaseScratch(scratch);
        return;
    }
    f32 *at = ArenaPushArrayNoZero(scratch.arena, pointCount * 3, f32);
    u64 p = 0;
    for (Prog__Xml *v = vertices->first; v; v = v->next) {
        if (!Prog__XmlIs(v, S("vertex"))) continue;
        Prog__MeshPoint(at + p * 3, place, (f32)Prog__XmlNumber(v, S("x"), 0), (f32)Prog__XmlNumber(v, S("y"), 0), (f32)Prog__XmlNumber(v, S("z"), 0));
        ++p;
    }
    String objectGroup = Prog__XmlValue(object, S("pid"));
    f64 objectIndex = Prog__XmlNumber(object, S("pindex"), 0);
    Prog__Corners made = Prog__CornersMake(scratch.arena, pointCount, pointCount * 2);
    u32 shownImage = UINT32_MAX - 1;
    for (Prog__Xml *t = triangles ? triangles->first : NULL; t && !P_IsCancelled(m->cancel); t = t->next) {
        if (!Prog__XmlIs(t, S("triangle"))) continue;
        String groupId = Prog__XmlValue(t, S("pid"));
        if (!groupId.length) groupId = objectGroup;
        Prog__Xml *group = groupId.length ? Prog__XmlAt(&m->ids, groupId) : NULL;
        f64 first = Prog__XmlNumber(t, S("p1"), groupId.length && !Prog__XmlValue(t, S("pid")).length ? objectIndex : 0);
        f64 indices[3] = { first, Prog__XmlNumber(t, S("p2"), first), Prog__XmlNumber(t, S("p3"), first) };
        b32 textured = Prog__XmlIs(group, S("texture2dgroup")), colors = Prog__XmlIs(group, S("colorgroup")), materials = Prog__XmlIs(group, S("basematerials"));
        // A texture group's picture starts a part of its own.
        u32 image = UINT32_MAX;
        if (textured) {
            Prog__Xml *texture = Prog__XmlAt(&m->ids, Prog__XmlValue(group, S("texid")));
            String path = Prog__XmlValue(texture, S("path"));
            if (path.length && path.value[0] == '/') path = StrFrom(path, 1);
            if (path.length) image = Prog__MeshImage(arena, out, path, out->imageSide ? Prog__ZipView(arena, m->zip, path) : (String){0});
        }
        if (image != shownImage) {
            Prog__MeshPart(arena, out, 0, image, 0);
            shownImage = image;
        }
        u32 corners[3];
        for (u32 k = 0; k < 3; ++k) {
            f64 point = Prog__XmlNumber(t, k == 0 ? S("v1") : k == 1 ? S("v2") : S("v3"), -1);
            corners[k] = UINT32_MAX;
            if (point < 0 || point >= (f64)pointCount) continue;
            Prog__Xml *entry = group ? Prog__3mfEntry(group, (u64)indices[k]) : NULL;
            u32 color = materials ? Prog__3mfColor(Prog__XmlValue(entry, S("displaycolor"))) : colors ? Prog__3mfColor(Prog__XmlValue(entry, S("color"))) : 0;
            Prog__MeshCorner key = { (u32)point, textured && entry ? (u32)indices[k] + 1 : 0, color, textured ? image + 1 : 0 };
            u32 vertex = Prog__CornersFind(&made, key);
            if (vertex == UINT32_MAX && out->positions.header.count / 3 < UINT32_MAX) {
                vertex = (u32)(out->positions.header.count / 3);
                MemCopy(ArrayAdd(arena, out->positions, 3), at + (u64)point * 3, 12);
                if (textured && entry) {
                    f32 *uv = Prog__MeshUvs(arena, out) + (u64)vertex * 2;
                    uv[0] = (f32)Prog__XmlNumber(entry, S("u"), 0);
                    uv[1] = 1 - (f32)Prog__XmlNumber(entry, S("v"), 0);
                }
                if (color) Prog__MeshColors(arena, out)[vertex] = color;
                Prog__CornersAdd(&made, key, vertex);
            }
            corners[k] = vertex;
        }
        Prog__MeshTriangle(arena, out, corners[0], corners[1], corners[2]);
    }
    ReleaseScratch(scratch);
}

function void Prog__3mfObject(Arena *arena, const Prog__3mf *m, const Prog__Xml *object, Prog__Affine place, u32 depth)
{
    if (!object || depth > 32 || P_IsCancelled(m->cancel)) return;
    Prog__Xml *mesh = Prog__XmlChild(object, S("mesh")), *components = Prog__XmlChild(object, S("components"));
    if (mesh) Prog__3mfMesh(arena, m, object, mesh, &place);
    for (Prog__Xml *c = components ? components->first : NULL; c; c = c->next) {
        if (!Prog__XmlIs(c, S("component"))) continue;
        Prog__Xml *part = Prog__XmlAt(&m->ids, Prog__XmlValue(c, S("objectid")));
        Prog__3mfObject(arena, m, part, Prog__AffineMultiply(place, Prog__3mfTransform(arena, c)), depth + 1);
    }
}

function b32 Prog__Read3mf(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    // The package's relationships name the model part; it is "3D/3dmodel.model" where they do not.
    String name = S("3D/3dmodel.model");
    Prog__Xml *rels = Prog__XmlChild(Prog__XmlParse(arena, Prog__ZipView(arena, data, S("_rels/.rels")), cancel), S("Relationships"));
    for (Prog__Xml *r = rels ? rels->first : NULL; r; r = r->next) {
        String target = Prog__XmlValue(r, S("Target"));
        if (!StrIndexStr(Prog__XmlValue(r, S("Type")), S("3dmodel"), SSF_None).exists || !target.length) continue;
        name = target.value[0] == '/' ? StrFrom(target, 1) : target;
        break;
    }
    Prog__Xml *model = Prog__XmlChild(Prog__XmlParse(arena, Prog__ZipView(arena, data, name), cancel), S("model"));
    if (!model) return false;
    Prog__3mf m = { .zip = data, .mesh = mesh, .cancel = cancel };
    Prog__Xml *resources = Prog__XmlChild(model, S("resources")), *build = Prog__XmlChild(model, S("build"));
    for (Prog__Xml *r = resources ? resources->first : NULL; r; r = r->next) {
        String id = Prog__XmlValue(r, S("id"));
        if (id.length && !MapsGet(m.ids, u64, id)) MapsPut(arena, m.ids, id, (u64)(uintptr_t)r);
    }
    mesh->zUp = true;
    for (Prog__Xml *item = build ? build->first : NULL; item; item = item->next) {
        if (Prog__XmlIs(item, S("item"))) Prog__3mfObject(arena, &m, Prog__XmlAt(&m.ids, Prog__XmlValue(item, S("objectid"))), Prog__3mfTransform(arena, item), 0);
    }
    return mesh->indices.header.count > 0;
}

// -----------------------------------------------------------------------------
// 3DS: chunks, each an id and its length with its header, nested. The editor's objects hold triangle meshes, their points already where
// they go, Z up, with a texture coordinate a point and their faces' material groups, whose materials have a diffuse color and a texture
// map's file.

typedef struct Prog__3dsMaterial { String name; Prog_MeshPart look; } Prog__3dsMaterial;
typedef struct Prog__3dsMaterials { ArrayHeader header; Prog__3dsMaterial *v; } Prog__3dsMaterials;

// The chunk at `*at` within `end`: its id and where its contents start and end; false where none fits.
function b32 Prog__3dsChunk(const u8 *file, u64 *at, u64 end, u32 *id, u64 *from, u64 *to)
{
    if (*at + 6 > end) return false;
    u32 length = Prog__Read32(file + *at + 2);
    if (length < 6 || length > end - *at) return false;
    *id = (u32)file[*at] | (u32)file[*at + 1] << 8;
    *from = *at + 6;
    *to = *at + length;
    *at = *to;
    return true;
}

// A string ended by a zero, from `*at`, which moves past it.
function String Prog__3dsName(const u8 *file, u64 *at, u64 end)
{
    u64 from = *at;
    while (*at < end && file[*at]) ++*at;
    String result = StrMake((const char *)file + from, *at - from);
    if (*at < end) ++*at;
    return result;
}

// A color chunk's color among a material's: bytes or floats, as written.
function u32 Prog__3dsColor(const u8 *file, u64 from, u64 to)
{
    u32 id = 0, result = 0;
    u64 start, stop;
    for (u64 at = from; Prog__3dsChunk(file, &at, to, &id, &start, &stop);) {
        if ((id == 0x0011 || id == 0x0012) && stop - start >= 3) {
            return file[start] | (u32)file[start + 1] << 8 | (u32)file[start + 2] << 16 | 0xFF000000u;
        }
        if ((id == 0x0010 || id == 0x0013) && stop - start >= 12) {
            f32 rgb[3];
            MemCopy(rgb, file + start, 12);
            u32 bytes[3];
            for (u32 k = 0; k < 3; ++k) bytes[k] = (u32)((rgb[k] <= 0 ? 0 : rgb[k] >= 1 ? 1 : rgb[k]) * 255 + 0.5f);
            result = bytes[0] | bytes[1] << 8 | bytes[2] << 16 | 0xFF000000u;
        }
    }
    return result;
}

function void Prog__3dsReadMaterial(Arena *arena, const u8 *file, u64 from, u64 to, Prog_Mesh *mesh, Prog__3dsMaterials *materials)
{
    Prog__3dsMaterial material = { .look = { .image = UINT32_MAX } };
    f32 transparency = 0;
    u32 id = 0;
    u64 start, stop;
    for (u64 at = from; Prog__3dsChunk(file, &at, to, &id, &start, &stop);) {
        if (id == 0xA000) {
            u64 p = start;
            material.name = Prog__3dsName(file, &p, stop);
        } else if (id == 0xA020) {
            material.look.color = Prog__3dsColor(file, start, stop);
        } else if (id == 0xA050) {
            u32 inner = 0;
            u64 a, b;
            for (u64 p = start; Prog__3dsChunk(file, &p, stop, &inner, &a, &b);) {
                if (inner == 0x0030 && b - a >= 2) transparency = (f32)((u32)file[a] | (u32)file[a + 1] << 8) / 100;
                if (inner == 0x0031 && b - a >= 4) MemCopy(&transparency, file + a, 4);
            }
        } else if (id == 0xA200) {
            u32 inner = 0;
            u64 a, b;
            for (u64 p = start; Prog__3dsChunk(file, &p, stop, &inner, &a, &b);) {
                if (inner != 0xA300) continue;
                u64 q = a;
                String name = Prog__3dsName(file, &q, b);
                if (name.length) material.look.image = Prog__MeshImage(arena, mesh, name, (String){0});
            }
        }
    }
    material.look.bare = material.look.color;
    if (material.look.image != UINT32_MAX) material.look.color = 0;
    Prog__MeshAlpha(&material.look, 1 - transparency);
    ArrayPush(arena, *materials, material);
}

// A triangle mesh: its points and texture coordinates, then its faces, each material group's in its look and those in none after.
function void Prog__3dsMesh(Arena *arena, const u8 *file, u64 from, u64 to, const Prog__3dsMaterials *materials, Prog_Mesh *mesh)
{
    Scratch scratch = GetScratchEx(arena);
    const u8 *points = NULL, *uvs = NULL, *faces = NULL;
    u32 pointCount = 0, uvCount = 0, faceCount = 0;
    u64 facesEnd = 0;
    u32 id = 0;
    u64 start, stop;
    for (u64 at = from; Prog__3dsChunk(file, &at, to, &id, &start, &stop);) {
        u32 count = stop - start >= 2 ? (u32)file[start] | (u32)file[start + 1] << 8 : 0;
        if (id == 0x4110 && (u64)count * 12 <= stop - start - 2) points = file + start + 2, pointCount = count;
        else if (id == 0x4140 && (u64)count * 8 <= stop - start - 2) uvs = file + start + 2, uvCount = count;
        else if (id == 0x4120 && (u64)count * 8 <= stop - start - 2) faces = file + start + 2, faceCount = count, facesEnd = stop;
    }
    if (!points || !faces || mesh->positions.header.count / 3 + pointCount >= UINT32_MAX) {
        ReleaseScratch(scratch);
        return;
    }
    u32 base = (u32)(mesh->positions.header.count / 3);
    f32 *at = ArrayAdd(arena, mesh->positions, (u64)pointCount * 3);
    MemCopy(at, points, (u64)pointCount * 12);
    if (uvs && uvCount == pointCount) {
        f32 *t = Prog__MeshUvs(arena, mesh) + (u64)base * 2;
        MemCopy(t, uvs, (u64)pointCount * 8);
        for (u32 i = 0; i < pointCount; ++i) t[i * 2 + 1] = 1 - t[i * 2 + 1];
    }
    b8 *drawn = ArenaPushArray(scratch.arena, Max(faceCount, 1u), b8);
    u64 groups = (u64)(faces - file) + (u64)faceCount * 8;
    for (u64 p = groups; Prog__3dsChunk(file, &p, facesEnd, &id, &start, &stop);) {
        if (id != 0x4130) continue;
        u64 q = start;
        String name = Prog__3dsName(file, &q, stop);
        u32 count = q + 2 <= stop ? (u32)file[q] | (u32)file[q + 1] << 8 : 0;
        q += 2;
        Prog_MeshPart look = { .image = UINT32_MAX };
        for (u64 m = 0; m < materials->header.count; ++m) if (StrIsEqual(materials->v[m].name, name)) look = materials->v[m].look;
        Prog__MeshLook(arena, mesh, &look);
        for (u32 k = 0; k < count && q + 2 <= stop; ++k, q += 2) {
            u32 face = (u32)file[q] | (u32)file[q + 1] << 8;
            if (face >= faceCount || drawn[face]) continue;
            const u8 *f = faces + (u64)face * 8;
            u32 a = (u32)f[0] | (u32)f[1] << 8, b = (u32)f[2] | (u32)f[3] << 8, c = (u32)f[4] | (u32)f[5] << 8;
            Prog__MeshTriangle(arena, mesh, base + a, base + b, base + c);
            drawn[face] = true;
        }
    }
    Prog__MeshPart(arena, mesh, 0, UINT32_MAX, 0);
    for (u32 face = 0; face < faceCount; ++face) {
        if (drawn[face]) continue;
        const u8 *f = faces + (u64)face * 8;
        Prog__MeshTriangle(arena, mesh, base + ((u32)f[0] | (u32)f[1] << 8), base + ((u32)f[2] | (u32)f[3] << 8), base + ((u32)f[4] | (u32)f[5] << 8));
    }
    ReleaseScratch(scratch);
}

function b32 Prog__Read3ds(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    const u8 *file = (const u8 *)data.value;
    u32 id = 0;
    u64 start, stop, top = 0;
    if (!Prog__3dsChunk(file, &top, data.length, &id, &start, &stop) || id != 0x4D4D) return false;
    mesh->zUp = true;
    Prog__3dsMaterials materials = {0};
    u64 editorFrom = 0, editorTo = 0;
    for (u64 at = start; Prog__3dsChunk(file, &at, stop, &id, &editorFrom, &editorTo);) {
        if (id != 0x3D3D) continue;
        // The materials first, as the objects may come before them.
        u32 inner = 0;
        u64 a, b;
        for (u64 p = editorFrom; Prog__3dsChunk(file, &p, editorTo, &inner, &a, &b);) if (inner == 0xAFFF) Prog__3dsReadMaterial(arena, file, a, b, mesh, &materials);
        for (u64 p = editorFrom; Prog__3dsChunk(file, &p, editorTo, &inner, &a, &b) && !P_IsCancelled(cancel);) {
            if (inner != 0x4000) continue;
            u64 q = a;
            Prog__3dsName(file, &q, b);
            u32 kind = 0;
            u64 c, d;
            for (u64 r = q; Prog__3dsChunk(file, &r, b, &kind, &c, &d);) if (kind == 0x4100) Prog__3dsMesh(arena, file, c, d, &materials, mesh);
        }
    }
    return mesh->indices.header.count > 0;
}

// -----------------------------------------------------------------------------

typedef enum Prog_ModelKind {
    Prog_ModelKind_None,
    Prog_ModelKind_Obj,
    Prog_ModelKind_Stl,
    Prog_ModelKind_Ply,
    Prog_ModelKind_Gltf,
    Prog_ModelKind_Glb,
    Prog_ModelKind_Fbx,
    Prog_ModelKind_Blend,
    Prog_ModelKind_Usd,
    Prog_ModelKind_Usdc,
    Prog_ModelKind_Usdz,
    Prog_ModelKind_Usda,
    Prog_ModelKind_Dae,
    Prog_ModelKind_3mf,
    Prog_ModelKind_3ds,
    Prog_ModelKind_Count,
} Prog_ModelKind;

function Prog_ModelKind Prog_ModelKindOf(String path)
{
    local_persist const String extensions[Prog_ModelKind_Count] = { {0}, SInit(".obj"), SInit(".stl"), SInit(".ply"), SInit(".gltf"), SInit(".glb"), SInit(".fbx"), SInit(".blend"), SInit(".usd"), SInit(".usdc"), SInit(".usdz"), SInit(".usda"), SInit(".dae"), SInit(".3mf"), SInit(".3ds") };
    // Blender's backups, "scene.blend1" and on, are .blend files too.
    String backup = path;
    while (backup.length > 6 && backup.value[backup.length - 1] >= '0' && backup.value[backup.length - 1] <= '9') --backup.length;
    if (backup.length < path.length && Prog__EqualFolded(StrFrom(backup, backup.length - 6), S(".blend"))) return Prog_ModelKind_Blend;
    for (u32 i = 1; i < Prog_ModelKind_Count; ++i) {
        String tail = path.length >= extensions[i].length ? StrFrom(path, path.length - extensions[i].length) : (String){0};
        if (Prog__EqualFolded(tail, extensions[i])) return (Prog_ModelKind)i;
    }
    return Prog_ModelKind_None;
}

function b32 Prog_IsModel(String path) { return Prog_ModelKindOf(path) != Prog_ModelKind_None; }

function b32 Prog__ReadModelKind(Arena *arena, Prog_ModelKind kind, String path, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    switch (kind) {
        case Prog_ModelKind_Obj: return Prog__ReadObj(arena, data, cancel, mesh);
        case Prog_ModelKind_Stl: return Prog__ReadStl(arena, data, cancel, mesh);
        case Prog_ModelKind_Ply: return Prog__ReadPly(arena, data, cancel, mesh);
        case Prog_ModelKind_Gltf: return Prog__ReadGltf(arena, path, data, false, cancel, mesh);
        case Prog_ModelKind_Glb: return Prog__ReadGltf(arena, path, data, true, cancel, mesh);
        case Prog_ModelKind_Fbx: return Prog__ReadFbx(arena, data, cancel, mesh);
        case Prog_ModelKind_Blend: return Prog__ReadBlend(arena, data, cancel, mesh);
        // A .usd may be binary or text, which its first bytes tell.
        case Prog_ModelKind_Usd:
        case Prog_ModelKind_Usdc:
        case Prog_ModelKind_Usda:
        case Prog_ModelKind_Usdz: return Prog__ReadUsd(arena, path, data, cancel, mesh);
        case Prog_ModelKind_Dae: return Prog__ReadCollada(arena, data, cancel, mesh);
        case Prog_ModelKind_3mf: return Prog__Read3mf(arena, data, cancel, mesh);
        case Prog_ModelKind_3ds: return Prog__Read3ds(arena, data, cancel, mesh);
        default: return false;
    }
}

typedef struct Prog__ModelRead {
    Arena *arena;
    Prog_ModelKind kind;
    String path, data;
    const P_CancelToken *cancel;
    Prog_Mesh *mesh;
    Prog__Pictures pictures;
    b32 result;
} Prog__ModelRead;

// Lane 0 reads the model, then helps with the pictures; the others decode the pictures it queues as it finds them.
function void Prog__ModelReadLane(void *user, u32 lane)
{
    Prog__ModelRead *read = user;
    Prog__Pictures *pictures = &read->pictures;
    if (lane == 0) {
        read->result = Prog__ReadModelKind(read->arena, read->kind, read->path, read->data, read->cancel, read->mesh);
        P_MutexLock(&pictures->mutex);
        pictures->closed = true;
        P_ConditionBroadcast(&pictures->wake);
        P_MutexUnlock(&pictures->mutex);
    }
    for (;;) {
        P_MutexLock(&pictures->mutex);
        while (pictures->next >= pictures->count && !pictures->closed) P_ConditionWait(&pictures->wake, &pictures->mutex);
        u32 at = pictures->next < pictures->count ? pictures->next++ : UINT32_MAX;
        P_MutexUnlock(&pictures->mutex);
        if (at == UINT32_MAX) break;
        Prog__Picture *picture = &pictures->list[at];
        Prog__DecodeImage(&picture->arena, read->path, read->mesh->imageSide, picture->name, picture->bytes, &picture->image);
    }
}

function b32 Prog_ReadModel(Arena *arena, Prog_ModelKind kind, String path, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    mesh->path = path;
    Prog__ModelRead read = { .arena = arena, .kind = kind, .path = path, .data = data, .cancel = cancel, .mesh = mesh };
    if (!mesh->imageSide || !P_MutexInit(&read.pictures.mutex)) return Prog__ReadModelKind(arena, kind, path, data, cancel, mesh);
    if (!P_ConditionInit(&read.pictures.wake)) {
        P_MutexRelease(&read.pictures.mutex);
        return Prog__ReadModelKind(arena, kind, path, data, cancel, mesh);
    }
    mesh->pictures = &read.pictures;
    P_RunLanes(4, Prog__ModelReadLane, &read);
    mesh->pictures = NULL;
    P_ConditionRelease(&read.pictures.wake);
    P_MutexRelease(&read.pictures.mutex);
    // The decoded pictures, copied out of their lanes' arenas.
    for (u32 i = 0; i < read.pictures.count && i < mesh->images.header.count; ++i) {
        Prog__Picture *picture = &read.pictures.list[i];
        P_Image from = picture->image;
        if (from.pixels) {
            u64 count = (u64)from.width * (u64)from.height;
            mesh->images.v[i] = (P_Image){ ArenaPushArrayNoZero(arena, Max(count, (u64)1), u32), from.width, from.height };
            MemCopy(mesh->images.v[i].pixels, from.pixels, count * 4);
        }
        ArenaRelease(&picture->arena);
    }
    return read.result;
}

// A triangle's colors as drawn in software: its part's color and picture, its corners' colors and texture coordinates, and its light.
typedef struct Prog__Paint {
    u32 color;
    const P_Image *image;
    u32 corners[3];
    const f32 *uvs[3];
    f32 light;
} Prog__Paint;

// The lit color at the point weighted `l0`, `l1` and `l2` between the triangle's corners, as opaque RGBA; 0 where its picture is see-through.
function u32 Prog__PaintAt(const Prog__Paint *paint, f32 l0, f32 l1, f32 l2)
{
    f32 rgb[3];
    for (u32 k = 0; k < 3; ++k) {
        u32 shift = k * 8;
        f32 corner = ((f32)((paint->corners[0] >> shift) & 0xFF) * l0 + (f32)((paint->corners[1] >> shift) & 0xFF) * l1 + (f32)((paint->corners[2] >> shift) & 0xFF) * l2) / 255.0f;
        rgb[k] = (f32)((paint->color >> shift) & 0xFF) * corner;
    }
    if (paint->image) {
        f32 u = paint->uvs[0][0] * l0 + paint->uvs[1][0] * l1 + paint->uvs[2][0] * l2, v = paint->uvs[0][1] * l0 + paint->uvs[1][1] * l1 + paint->uvs[2][1] * l2;
        i32 w = paint->image->width, h = paint->image->height;
        i32 x = (i32)((u - floorf(u)) * (f32)w), y = (i32)((v - floorf(v)) * (f32)h);
        u32 texel = paint->image->pixels[(u64)Min(Max(y, 0), h - 1) * (u64)w + (u64)Min(Max(x, 0), w - 1)];
        if ((texel >> 24) < 128) return 0;
        for (u32 k = 0; k < 3; ++k) rgb[k] *= (f32)((texel >> (k * 8)) & 0xFF) / 255.0f;
    }
    u32 result = 0xFF000000u;
    for (u32 k = 0; k < 3; ++k) result |= (u32)MinF32(rgb[k] * paint->light + 0.5f, 255) << (k * 8);
    return result;
}

// Draws `shape` into `size` squared straight RGBA pixels, seen from in front of it, above and to its right without perspective, fitted to
// the square and lit from the upper left, on a clear background, in its parts' colors and pictures. It draws at twice the size and averages,
// for smooth edges. Each triangle shows the side that faces the view, so that meshes wound either way look alike. NULL when nothing was
// drawn or the job was cancelled.
function u32 *Prog_DrawMesh(Arena *arena, const Prog_ModelShape *shape, i32 size, const P_CancelToken *cancel)
{
    u64 vertices = shape->vertexCount, triangles = shape->indexCount / 3;
    if (!vertices || !triangles || size <= 0) return NULL;
    Scratch scratch = GetScratchEx(arena);
    u32 *result = NULL;
    // Turned 30 degrees about the up axis and tipped 20 degrees toward the view.
    f32 cy = 0.8660254f, sy = 0.5f, cp = 0.9396926f, sp = 0.3420201f;
    f32 *view = ArenaPushArrayNoZero(scratch.arena, vertices * 3, f32);
    f32 minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    const f32 *positions = shape->positions;
    for (u64 i = 0; i < vertices; ++i) {
        f32 x = positions[i * 3], y = positions[i * 3 + 1], z = positions[i * 3 + 2];
        f32 x1 = cy * x - sy * z, z1 = sy * x + cy * z;
        f32 y2 = cp * y - sp * z1, z2 = sp * y + cp * z1;
        view[i * 3] = x1;
        view[i * 3 + 1] = y2;
        view[i * 3 + 2] = z2;
        if (isfinite(x1) && isfinite(y2)) {
            minX = MinF32(minX, x1), maxX = MaxF32(maxX, x1);
            minY = MinF32(minY, y2), maxY = MaxF32(maxY, y2);
        }
    }
    i32 side = size * 2;
    if (minX <= maxX) {
        f32 extent = MaxF32(maxX - minX, maxY - minY);
        f32 scale = extent > 0 ? (f32)side * 0.88f / extent : 1;
        f32 ox = (f32)side * 0.5f - (minX + maxX) * 0.5f * scale, oy = (f32)side * 0.5f + (minY + maxY) * 0.5f * scale;
        for (u64 i = 0; i < vertices; ++i) {
            view[i * 3] = view[i * 3] * scale + ox;
            view[i * 3 + 1] = oy - view[i * 3 + 1] * scale;
            view[i * 3 + 2] *= scale;
        }
        u64 samples = (u64)side * (u64)side;
        f32 *depth = ArenaPushArrayNoZero(scratch.arena, samples, f32);
        u32 *shade = ArenaPushArray(scratch.arena, samples, u32);
        for (u64 i = 0; i < samples; ++i) depth[i] = -INFINITY;
        // The light, toward the upper left and the view.
        const f32 lx = -0.398f, ly = 0.597f, lz = 0.697f;
        b32 cancelled = false;
        const u32 *indices = shape->indices;
        u32 part = 0;
        for (u64 t = 0; t < triangles; ++t) {
            if ((t & 0x3FFF) == 0x3FFF && P_IsCancelled(cancel)) {
                cancelled = true;
                break;
            }
            while (part + 1 < shape->partCount && shape->parts[part + 1].first <= t * 3) ++part;
            u32 ia = indices[t * 3], ib = indices[t * 3 + 1], ic = indices[t * 3 + 2];
            const f32 *a = view + (u64)ia * 3, *b = view + (u64)ib * 3, *c = view + (u64)ic * 3;
            // The normal, in a space with Y up again.
            f32 ux = b[0] - a[0], uy = a[1] - b[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = a[1] - c[1], vz = c[2] - a[2];
            f32 nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            f32 length = sqrtf(nx * nx + ny * ny + nz * nz);
            if (!(length > 0)) continue;
            if (nz < 0) nx = -nx, ny = -ny, nz = -nz;
            f32 light = (nx * lx + ny * ly + nz * lz) / length;
            f32 area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            if (area < 0) {
                const f32 *swap = b;
                b = c;
                c = swap;
                u32 swapIndex = ib;
                ib = ic;
                ic = swapIndex;
                area = -area;
            }
            const Prog_MeshPart *drawn = shape->partCount ? &shape->parts[part] : NULL;
            Prog__Paint paint = {
                .color = drawn ? drawn->color : Prog_ModelGray,
                .image = drawn && drawn->image < shape->imageCount && shape->images[drawn->image].width > 0 ? &shape->images[drawn->image] : NULL,
                .corners = { shape->colors[ia], shape->colors[ib], shape->colors[ic] },
                .uvs = { shape->uvs + (u64)ia * 2, shape->uvs + (u64)ib * 2, shape->uvs + (u64)ic * 2 },
                .light = 0.22f + 0.78f * MaxF32(light, 0),
            };
            // One color for the whole triangle when nothing varies across it.
            b32 flat = !paint.image && paint.corners[0] == paint.corners[1] && paint.corners[1] == paint.corners[2];
            u32 tone = flat ? Prog__PaintAt(&paint, 1, 0, 0) : 0;
            i32 x0 = (i32)MaxF32(floorf(MinF32(a[0], MinF32(b[0], c[0]))), 0), x1 = (i32)MinF32(floorf(MaxF32(a[0], MaxF32(b[0], c[0]))), (f32)(side - 1));
            i32 y0 = (i32)MaxF32(floorf(MinF32(a[1], MinF32(b[1], c[1]))), 0), y1 = (i32)MinF32(floorf(MaxF32(a[1], MaxF32(b[1], c[1]))), (f32)(side - 1));
            if (x0 > x1 || y0 > y1) continue;
            // A triangle inside one sample may miss its center; it takes the sample, so that dense meshes show no holes.
            if (x0 == x1 && y0 == y1) {
                u64 at = (u64)y0 * (u64)side + (u64)x0;
                f32 z = MaxF32(a[2], MaxF32(b[2], c[2]));
                u32 color = flat ? tone : Prog__PaintAt(&paint, 1.0f / 3, 1.0f / 3, 1.0f / 3);
                if (color && z > depth[at]) depth[at] = z, shade[at] = color;
                continue;
            }
            if (!(area > 0)) continue;
            // Edge functions at sample centers, stepped across each row; inside where all three are not negative.
            f32 px = (f32)x0 + 0.5f, py = (f32)y0 + 0.5f, inverse = 1.0f / area;
            f32 e0x = b[1] - c[1], e1x = c[1] - a[1], e2x = a[1] - b[1];
            f32 e0y = c[0] - b[0], e1y = a[0] - c[0], e2y = b[0] - a[0];
            f32 w0Row = (c[0] - b[0]) * (py - b[1]) - (c[1] - b[1]) * (px - b[0]);
            f32 w1Row = (a[0] - c[0]) * (py - c[1]) - (a[1] - c[1]) * (px - c[0]);
            f32 w2Row = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
            f32 zx = (e0x * a[2] + e1x * b[2] + e2x * c[2]) * inverse;
            for (i32 y = y0; y <= y1; ++y) {
                f32 w0 = w0Row, w1 = w1Row, w2 = w2Row, z = (w0 * a[2] + w1 * b[2] + w2 * c[2]) * inverse;
                f32 *depthRow = depth + (u64)y * (u64)side;
                u32 *shadeRow = shade + (u64)y * (u64)side;
                for (i32 x = x0; x <= x1; ++x) {
                    if (w0 >= 0 && w1 >= 0 && w2 >= 0 && z > depthRow[x]) {
                        u32 color = flat ? tone : Prog__PaintAt(&paint, w0 * inverse, w1 * inverse, w2 * inverse);
                        if (color) {
                            depthRow[x] = z;
                            shadeRow[x] = color;
                        }
                    }
                    w0 += e0x, w1 += e1x, w2 += e2x, z += zx;
                }
                w0Row += e0y, w1Row += e1y, w2Row += e2y;
            }
        }
        if (!cancelled) {
            // Each pixel averages its four samples: the color of those covered, and how many are covered as its alpha.
            result = ArenaPushArrayNoZero(arena, (u64)size * (u64)size, u32);
            for (i32 y = 0; y < size; ++y) {
                for (i32 x = 0; x < size; ++x) {
                    u64 at = (u64)y * 2 * (u64)side + (u64)x * 2;
                    u64 corners[4] = { at, at + 1, at + (u64)side, at + (u64)side + 1 };
                    u32 covered = 0, sum[3] = {0};
                    for (u32 k = 0; k < 4; ++k) {
                        if (depth[corners[k]] > -INFINITY) {
                            ++covered;
                            for (u32 channel = 0; channel < 3; ++channel) sum[channel] += (shade[corners[k]] >> (channel * 8)) & 0xFF;
                        }
                    }
                    u32 pixel = 0;
                    if (covered) pixel = sum[0] / covered | (sum[1] / covered) << 8 | (sum[2] / covered) << 16 | (covered * 255 / 4) << 24;
                    result[(u64)y * (u64)size + (u64)x] = pixel;
                }
            }
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 Prog__ModelShape(Arena *arena, const Prog_Mesh *mesh, Prog_ModelShape *shape);

// The icon views' picture of the model at `path`, `size` squared, drawn in software on the thumbnail job's thread; NULL when it is no model,
// too large or has nothing to draw.
function u32 *Prog_ModelThumbnail(Arena *arena, String path, i32 size, const P_CancelToken *cancel)
{
    Prog_ModelKind kind = Prog_ModelKindOf(path);
    if (!kind) return NULL;
    Scratch scratch = GetScratchEx(arena);
    String data = {0};
    Prog_Mesh mesh = { .imageSide = Prog_ModelThumbnailImageSide };
    Prog_ModelShape shape = {0};
    u32 *result = NULL;
    // One byte past the most read, to tell a file that is too large from one that fits.
    if (P_ReadFileStart(scratch.arena, path, Prog_ModelMaxBytes + 1, &data) && data.length && data.length <= Prog_ModelMaxBytes && !P_IsCancelled(cancel) &&
        Prog_ReadModel(scratch.arena, kind, path, data, cancel, &mesh) && Prog__ModelShape(scratch.arena, &mesh, &shape)) {
        result = Prog_DrawMesh(arena, &shape, size, cancel);
    }
    ReleaseScratch(scratch);
    return result;
}

// `mesh` as `shape`, in the arena: Y up, as the view turns about it; a triangle naming a vertex that is not there, or one that is not finite,
// is left out. False when no triangle is left.
function b32 Prog__ModelShape(Arena *arena, const Prog_Mesh *mesh, Prog_ModelShape *shape)
{
    *shape = (Prog_ModelShape){0};
    u64 vertices = mesh->positions.header.count / 3, triangles = mesh->indices.header.count / 3;
    if (!vertices || vertices >= UINT32_MAX || !triangles) return false;
    Scratch scratch = GetScratchEx(arena);
    f32 *positions = ArenaPushArrayNoZero(arena, vertices * 3, f32);
    f32 *uvs = ArenaPushArray(arena, vertices * 2, f32);
    u32 *colors = ArenaPushArrayNoZero(arena, vertices, u32);
    MemCopy(uvs, mesh->uvs.v, Min(mesh->uvs.header.count, vertices * 2) * 4);
    u64 colored = Min(mesh->colors.header.count, vertices);
    MemCopy(colors, mesh->colors.v, colored * 4);
    for (u64 i = colored; i < vertices; ++i) colors[i] = Prog_ModelUnset;
    b8 *valid = ArenaPushArrayNoZero(scratch.arena, vertices, b8);
    f32 low[3] = { INFINITY, INFINITY, INFINITY }, high[3] = { -INFINITY, -INFINITY, -INFINITY };
    for (u64 i = 0; i < vertices; ++i) {
        f32 x = mesh->positions.v[i * 3], y = mesh->positions.v[i * 3 + 1], z = mesh->positions.v[i * 3 + 2];
        f32 p[3] = { x, mesh->zUp ? z : y, mesh->zUp ? -y : z };
        valid[i] = (b8)(isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]));
        MemCopy(positions + i * 3, p, sizeof(p));
        for (u32 k = 0; valid[i] && k < 3; ++k) low[k] = MinF32(low[k], p[k]), high[k] = MaxF32(high[k], p[k]);
        if (!isfinite(uvs[i * 2]) || !isfinite(uvs[i * 2 + 1])) uvs[i * 2] = uvs[i * 2 + 1] = 0;
    }
    // The parts, each starting where its first triangle is kept. A vertex without a color of its own is the default gray in a part
    // without one, so that the parts and vertices that have colors are drawn in theirs alone.
    u64 partCount = Max(mesh->parts.header.count, (u64)1) + 1;
    Prog_MeshPart *parts = ArenaPushArray(arena, partCount, Prog_MeshPart);
    // The pictures that decoded, renumbered without those that did not.
    u32 renumbered[Prog_ModelMaxImages], imageCount = 0;
    for (u64 i = 0; i < mesh->images.header.count && i < Prog_ModelMaxImages; ++i) renumbered[i] = mesh->images.v[i].pixels ? imageCount++ : UINT32_MAX;
    u32 made = 0, next = 0, unset = Prog_ModelGray;
    u32 *indices = ArenaPushArrayNoZero(arena, triangles * 3, u32);
    u64 kept = 0;
    for (u64 t = 0; t < triangles; ++t) {
        // A part starts here: the first one, or the reader's next one, the triangles before its first part drawn as without parts.
        b32 starts = !made;
        const Prog_MeshPart *from = NULL;
        while (next < mesh->parts.header.count && mesh->parts.v[next].first <= t * 3) from = &mesh->parts.v[next++], starts = true;
        if (starts) {
            if (made && parts[made - 1].first == kept * 3) --made;
            u32 image = from && from->image < mesh->images.header.count && from->image < Prog_ModelMaxImages ? renumbered[from->image] : UINT32_MAX;
            u32 color = from ? (image == UINT32_MAX && from->image != UINT32_MAX && from->bare ? from->bare : from->color) : 0;
            b32 own = color || image != UINT32_MAX;
            unset = own ? 0xFFFFFFFFu : Prog_ModelGray;
            parts[made++] = (Prog_MeshPart){ .first = kept * 3, .color = color ? color : 0xFFFFFFFFu, .image = image, .blend = from && from->blend };
        }
        const u32 *at = mesh->indices.v + t * 3;
        if (at[0] >= vertices || at[1] >= vertices || at[2] >= vertices || !valid[at[0]] || !valid[at[1]] || !valid[at[2]]) continue;
        MemCopy(indices + kept * 3, at, 12);
        for (u32 k = 0; k < 3; ++k) if (!(colors[at[k]] >> 24)) colors[at[k]] = unset;
        ++kept;
    }
    for (u32 i = 0; i < made; ++i) parts[i].count = (i + 1 < made ? parts[i + 1].first : kept * 3) - parts[i].first;
    if (kept && low[0] <= high[0]) {
        f32 center[3], radius = 0;
        for (u32 k = 0; k < 3; ++k) center[k] = (low[k] + high[k]) * 0.5f;
        for (u64 i = 0; i < vertices; ++i) {
            if (!valid[i]) continue;
            f32 dx = positions[i * 3] - center[0], dy = positions[i * 3 + 1] - center[1], dz = positions[i * 3 + 2] - center[2];
            radius = MaxF32(radius, dx * dx + dy * dy + dz * dz);
        }
        // The pictures, copied out of the arena they were read into.
        P_Image *images = ArenaPushArray(arena, Max(imageCount, 1u), P_Image);
        for (u32 i = 0; i < mesh->images.header.count && i < Prog_ModelMaxImages; ++i) {
            if (renumbered[i] == UINT32_MAX) continue;
            P_Image from = mesh->images.v[i];
            u64 bytes = (u64)from.width * (u64)from.height * 4;
            images[renumbered[i]] = (P_Image){ ArenaPushArrayNoZero(arena, Max(bytes / 4, (u64)1), u32), from.width, from.height };
            MemCopy(images[renumbered[i]].pixels, from.pixels, bytes);
        }
        *shape = (Prog_ModelShape){ positions, uvs, colors, (u32)vertices, indices, kept * 3, parts, made, images, imageCount,
                                    { center[0], center[1], center[2] }, MaxF32(sqrtf(radius), 1e-6f), mesh->zUp };
    }
    ReleaseScratch(scratch);
    return shape->indexCount > 0;
}

function b32 Prog_ModelMesh(Arena *arena, String path, u64 size, const P_CancelToken *cancel, Prog_ModelShape *shape)
{
    Prog_ModelKind kind = Prog_ModelKindOf(path);
    *shape = (Prog_ModelShape){0};
    if (!kind || !size || size > Prog_ModelMaxBytes) return false;
    Scratch scratch = GetScratchEx(arena);
    String data = {0};
    Prog_Mesh mesh = { .imageSide = Prog_ModelImageSide };
    b32 result = P_ReadFileStart(scratch.arena, path, size, &data) && data.length == size && !P_IsCancelled(cancel) && Prog_ReadModel(scratch.arena, kind, path, data, cancel, &mesh) &&
                 Prog__ModelShape(arena, &mesh, shape);
    ReleaseScratch(scratch);
    return result;
}

