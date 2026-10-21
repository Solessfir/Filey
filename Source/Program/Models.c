#include <math.h>

// Model previews: small readers for the common 3D formats, which gather a file's triangles into one mesh placed as the file places its
// parts, and a software rasterizer that draws the mesh from a fixed view in front of it, above and to its right. Both run in the preview job.

#if defined(_WIN32)
// FBX compresses its arrays with zlib. Linux builds stb_image for its pictures; Windows, which decodes pictures through the shell, builds it
// here for its zlib decoder alone.
#pragma push_macro("function")
#pragma push_macro("global")
#undef function
#undef global
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "Lib/stb/stb_image.h"
#pragma clang diagnostic pop
#pragma pop_macro("global")
#pragma pop_macro("function")
#endif

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

typedef struct Prog_ArrayF32 { ArrayHeader header; f32 *v; } Prog_ArrayF32;

typedef struct Prog_Mesh {
    Prog_ArrayF32 positions;  // x, y and z of each vertex
    Prog_ArrayU32 indices;    // the three vertices of each triangle; one out of range, as a broken file can give, leaves its triangle out
    b32 zUp;                  // Z points up rather than Y
} Prog_Mesh;

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
// OBJ: "v x y z" lines and "f a b c ..." polygons, whose corners may carry texture and normal indices after slashes and count from the end
// when negative.
// -----------------------------------------------------------------------------

function b32 Prog__ReadObj(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    const char *p = data.value, *end = data.value + data.length;
    for (u64 line = 1; p < end; ++line) {
        if ((line & 0xFFFF) == 0 && P_IsCancelled(cancel)) return false;
        while (p < end && Prog__IsSpace(*p)) ++p;
        if (end - p > 1 && (p[0] == 'v' || p[0] == 'f') && (p[1] == ' ' || p[1] == '\t')) {
            b32 vertex = p[0] == 'v';
            p += 2;
            if (vertex) {
                f64 xyz[3] = {0};
                for (u32 i = 0; i < 3; ++i) Prog__Number(&p, end, &xyz[i]);
                f32 *v = ArrayAdd(arena, mesh->positions, 3);
                v[0] = (f32)xyz[0];
                v[1] = (f32)xyz[1];
                v[2] = (f32)xyz[2];
            } else {
                i64 count = (i64)(mesh->positions.header.count / 3);
                u32 first = 0, previous = 0, corners = 0;
                for (;;) {
                    while (p < end && Prog__IsSpace(*p)) ++p;
                    b32 negative = p < end && *p == '-';
                    if (negative) ++p;
                    const char *digits = p;
                    i64 index = 0;
                    for (; p < end && (u8)(*p - '0') < 10; ++p) if (index < (INT64_C(1) << 40)) index = index * 10 + (*p - '0');
                    if (p == digits) break;
                    while (p < end && !Prog__IsSpace(*p) && *p != '\n') ++p;
                    i64 at = negative ? count - index : index - 1;
                    u32 corner = at >= 0 && at < UINT32_MAX ? (u32)at : UINT32_MAX;
                    if (!corners) first = corner;
                    else if (corners >= 2) Prog__MeshTriangle(arena, mesh, first, previous, corner);
                    previous = corner;
                    ++corners;
                }
            }
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
    return mesh->indices.header.count != 0;
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
// element's x, y and z and the face element's index list are read; everything else is stepped over. Z is taken to point up, as Blender and
// the printing tools write it.
// -----------------------------------------------------------------------------

enum { Prog__PlyAscii, Prog__PlyLittle, Prog__PlyBig };

typedef struct Prog__PlyProperty {
    u8 type, countType;  // sizes in bytes, 0 for none; floats are told apart by `real`
    b32 real, list;
    u8 role;             // 1, 2 or 3 for x, y and z of a vertex, 4 for a face's corners
} Prog__PlyProperty;

typedef struct Prog__PlyElement {
    u64 count;
    u32 propertyCount;
    b32 vertex, face;
    Prog__PlyProperty properties[32];
} Prog__PlyElement;

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

function b32 Prog__ReadPly(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    const char *p = data.value, *end = data.value + data.length;
    if (data.length < 4 || MemCompare(p, "ply", 3) != 0) return false;
    mesh->zUp = true;
    Prog__PlyElement elements[16];
    u32 elementCount = 0, format = UINT32_MAX;
    b32 header = false;
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
        } else if (StrIsEqual(word, S("end_header"))) {
            header = true;
        }
        while (p < end && *p != '\n') ++p;
        if (p < end) ++p;
    }
    if (!header || format == UINT32_MAX) return false;
    u64 vertices = 0;
    for (u32 e = 0; e < elementCount; ++e) {
        const Prog__PlyElement *element = &elements[e];
        // An element without properties takes no room, whatever its count says.
        if (!element->propertyCount) continue;
        for (u64 i = 0; i < element->count; ++i) {
            if ((i & 0xFFFF) == 0xFFFF && P_IsCancelled(cancel)) return false;
            f64 xyz[3] = {0};
            for (u32 k = 0; k < element->propertyCount; ++k) {
                const Prog__PlyProperty *property = &element->properties[k];
                f64 value = 0;
                if (!property->list) {
                    if (!Prog__PlyValue(&p, end, property->type, property->real, format, &value)) return mesh->indices.header.count != 0;
                    if (property->role) xyz[property->role - 1] = value;
                    continue;
                }
                if (!Prog__PlyValue(&p, end, property->countType, false, format, &value) || value < 0) return mesh->indices.header.count != 0;
                u64 corners = (u64)value;
                u32 first = 0, previous = 0;
                for (u64 c = 0; c < corners; ++c) {
                    if (!Prog__PlyValue(&p, end, property->type, property->real, format, &value)) return mesh->indices.header.count != 0;
                    if (property->role != 4) continue;
                    u32 corner = value >= 0 && value < (f64)UINT32_MAX ? (u32)value : UINT32_MAX;
                    if (!c) first = corner;
                    else if (c >= 2) Prog__MeshTriangle(arena, mesh, first, previous, corner);
                    previous = corner;
                }
            }
            if (element->vertex) {
                f32 *v = ArrayAdd(arena, mesh->positions, 3);
                v[0] = (f32)xyz[0];
                v[1] = (f32)xyz[1];
                v[2] = (f32)xyz[2];
                ++vertices;
            }
        }
    }
    return vertices && mesh->indices.header.count != 0;
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

// A buffer's bytes: the .glb's own chunk, a data URI's, or a file's next to the .gltf, with %XX escapes in its name undone.
function String Prog__GltfBuffer(Arena *arena, String path, const Prog__Json *buffer, String glb)
{
    String result = {0};
    const Prog__Json *uri = Prog__JsonGet(buffer, S("uri"));
    u64 length = Prog__JsonCount(Prog__JsonGet(buffer, S("byteLength")), 0);
    if (!uri) return glb;
    if (uri->kind != '"' || length == UINT64_MAX) return result;
    String text = uri->text;
    if (text.length > 5 && MemCompare(text.value, "data:", 5) == 0) {
        Index comma = StrIndexChar(text, ',', SSF_None);
        return comma.exists ? Prog__DecodeBase64(arena, StrFrom(text, comma.value + 1)) : result;
    }
    char *name = ArenaPushArrayNoZero(arena, text.length, char);
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
    String file = Prog_JoinPath(arena, Prog_ParentPath(path), StrMake(name, n));
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

function void Prog__GltfMesh(Arena *arena, const Prog__Gltf *gltf, const Prog__Json *mesh, const Prog__Affine *place, Prog_Mesh *out)
{
    const Prog__Json *primitives = Prog__JsonGet(mesh, S("primitives"));
    for (u64 i = 0; primitives && i < primitives->count; ++i) {
        const Prog__Json *primitive = primitives->items[i];
        if (Prog__JsonNumber(Prog__JsonGet(primitive, S("mode")), 4) != 4) continue;
        u32 type = 0, components = 0;
        const u8 *bytes = NULL;
        u64 count = 0, stride = 0;
        if (!Prog__GltfAccessor(gltf, Prog__JsonGet(Prog__JsonGet(primitive, S("attributes")), S("POSITION")), &type, &components, &bytes, &count, &stride) ||
            type != 5126 || components != 3) continue;
        u64 base = out->positions.header.count / 3;
        if (base + count >= UINT32_MAX) return;
        f32 *v = ArrayAdd(arena, out->positions, count * 3);
        for (u64 k = 0; k < count; ++k) {
            f32 xyz[3];
            MemCopy(xyz, bytes + k * stride, 12);
            Prog__MeshPoint(v + k * 3, place, xyz[0], xyz[1], xyz[2]);
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
    if (property.encoding != 1 || bytes > INT32_MAX || property.length > INT32_MAX) return NULL;
    u8 *out = ArenaPushArrayNoZero(arena, bytes, u8);
    return stbi_zlib_decode_buffer((char *)out, (i32)bytes, (const char *)property.data, (i32)property.length) == (i32)bytes ? out : NULL;
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

function void Prog__FbxGeometry(Arena *arena, const Prog__FbxNode *geometry, const Prog__Affine *place, Prog_Mesh *mesh)
{
    u8 vertexType = 0, cornerType = 0;
    u64 values = 0, corners = 0;
    const u8 *vertices = Prog__FbxArray(arena, Prog__FbxChild(geometry->first, S("Vertices")), &vertexType, &values);
    const u8 *polygons = Prog__FbxArray(arena, Prog__FbxChild(geometry->first, S("PolygonVertexIndex")), &cornerType, &corners);
    u64 count = values / 3, base = mesh->positions.header.count / 3;
    if (!vertices || !polygons || (vertexType != 'd' && vertexType != 'f') || (cornerType != 'i' && cornerType != 'l') || base + count >= UINT32_MAX) return;
    f32 *v = ArrayAdd(arena, mesh->positions, count * 3);
    for (u64 i = 0; i < count; ++i) {
        f32 xyz[3];
        if (vertexType == 'd') {
            f64 d[3];
            MemCopy(d, vertices + i * 24, 24);
            xyz[0] = (f32)d[0], xyz[1] = (f32)d[1], xyz[2] = (f32)d[2];
        } else {
            MemCopy(xyz, vertices + i * 12, 12);
        }
        Prog__MeshPoint(v + i * 3, place, xyz[0], xyz[1], xyz[2]);
    }
    // A polygon's last corner is stored as its index's complement.
    u32 first = 0, previous = 0, at = 0;
    for (u64 i = 0; i < corners; ++i) {
        i64 raw = (i64)Prog__FbxElement(polygons, cornerType, i);
        b32 last = raw < 0;
        i64 index = last ? ~raw : raw;
        u32 corner = index < (i64)count ? (u32)(base + (u64)index) : UINT32_MAX;
        if (!at) first = corner;
        else if (at >= 2) Prog__MeshTriangle(arena, mesh, first, previous, corner);
        previous = corner;
        at = last ? 0 : at + 1;
    }
}

typedef struct Prog__IdMap { MapHeader header; struct { i64 key; u64 value; } *v; } Prog__IdMap;

typedef struct Prog__FbxModel {
    const Prog__FbxNode *node;
    i64 parent;
} Prog__FbxModel;

function b32 Prog__ReadFbx(Arena *arena, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    const u8 *file = (const u8 *)data.value;
    if (data.length < 27 || MemCompare(file, "Kaydara FBX Binary  \0", 21) != 0) return false;
    u32 version = 0;
    MemCopy(&version, file + 23, 4);
    Prog__FbxNode *top = Prog__FbxList(arena, file, file + 27, file + data.length, version >= 7500, 0);
    Prog__FbxNode *up = Prog__FbxSetting(Prog__FbxChild(top, S("GlobalSettings")), S("UpAxis"));
    mesh->zUp = up && Prog__FbxNumberAt(up, 4) == 2;
    Prog__FbxNode *objects = Prog__FbxChild(top, S("Objects")), *connections = Prog__FbxChild(top, S("Connections"));
    if (!objects) return false;
    struct { ArrayHeader header; Prog__FbxModel *v; } models = {0};
    struct { ArrayHeader header; const Prog__FbxNode **v; } geometries = {0};
    Prog__IdMap modelAt = {0}, geometryAt = {0};
    for (Prog__FbxNode *object = objects->first; object; object = object->next) {
        if (StrIsEqual(object->name, S("Model"))) {
            MapPut(arena, modelAt, Prog__FbxIdAt(object, 0), models.header.count);
            ArrayPush(arena, models, ((Prog__FbxModel){ .node = object }));
        } else if (StrIsEqual(object->name, S("Geometry")) && Prog__FbxChild(object->first, S("Vertices"))) {
            MapPut(arena, geometryAt, Prog__FbxIdAt(object, 0), geometries.header.count);
            ArrayPush(arena, geometries, object);
        }
    }
    b32 *placed = ArenaPushArray(arena, geometries.header.count + 1, b32);
    struct { ArrayHeader header; u64 *v; } uses = {0};  // a geometry's index and its model's, in pairs
    for (Prog__FbxNode *link = connections ? connections->first : NULL; link; link = link->next) {
        if (!StrIsEqual(Prog__FbxStringAt(link, 0), S("OO"))) continue;
        i64 child = Prog__FbxIdAt(link, 1), parent = Prog__FbxIdAt(link, 2);
        u64 model = 0, geometry = 0, parentModel = 0;
        if (!MapTryGet(modelAt, parent, &parentModel)) continue;
        if (MapTryGet(modelAt, child, &model)) {
            models.v[model].parent = parent;
        } else if (MapTryGet(geometryAt, child, &geometry)) {
            ArrayPush(arena, uses, geometry);
            ArrayPush(arena, uses, parentModel);
            placed[geometry] = true;
        }
    }
    for (u64 i = 0; i + 1 < uses.header.count && !P_IsCancelled(cancel); i += 2) {
        const Prog__FbxModel *model = &models.v[uses.v[i + 1]];
        Prog__Affine place = Prog__AffineMultiply(Prog__FbxPlacement(model->node, false), Prog__FbxPlacement(model->node, true));
        u64 above = 0;
        for (u32 depth = 0; depth < 64 && model->parent && MapTryGet(modelAt, model->parent, &above); ++depth) {
            model = &models.v[above];
            place = Prog__AffineMultiply(Prog__FbxPlacement(model->node, false), place);
        }
        Prog__FbxGeometry(arena, geometries.v[uses.v[i]], &place, mesh);
    }
    for (u64 i = 0; i < geometries.header.count && !P_IsCancelled(cancel); ++i) if (!placed[i]) Prog__FbxGeometry(arena, geometries.v[i], &Prog__AffineIdentity, mesh);
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
        u64 from = 0, to = 0;
        for (u64 i = 0; i < frames; ++i) {
            u64 packed = Prog__Read32(d + table + i * entry), unpacked = Prog__Read32(d + table + i * entry + 4);
            if (from + packed > table || to + unpacked > total) return (String){0};
            to += ZSTD_decompress(out + to, unpacked, d + from, packed);
            from += packed;
        }
        return StrMake((const char *)out, to);
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

// The data an attribute or a custom data layer named `name` points at, at least `bytes` long; Blender 5's attributes hold an array struct.
function const u8 *Prog__BlendLayer(const Prog__Blend *blend, const Prog__BlendBlock *owner, u32 meshType, String store, String name, u64 bytes)
{
    const u8 *mesh = owner->data;
    u64 meshLength = owner->length;
    // Blender 5: the mesh's attribute storage, an array of attributes, each with its name and an AttributeArray holding the data.
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
            const Prog__BlendBlock *array = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, attributeType, attribute, stride, S("data")));
            const Prog__BlendBlock *values = array ? Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, arrayType, array->data, array->length, S("data"))) : NULL;
            return values && values->length >= bytes ? values->data : NULL;
        }
    }
    // Blender 3.4 to 4.x: the custom data `store`'s layers, each with its name and data.
    u32 customType = Prog__BlendStruct(blend, S("CustomData")), layerType = Prog__BlendStruct(blend, S("CustomDataLayer"));
    u64 customAt, customSize;
    if (customType == UINT32_MAX || layerType == UINT32_MAX || !Prog__BlendField(blend, meshType, store, &customAt, &customSize) || customAt + customSize > meshLength) return NULL;
    const u8 *custom = mesh + customAt;
    const Prog__BlendBlock *layers = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, customType, custom, customSize, S("layers")));
    i64 count = Prog__BlendInt(blend, customType, custom, customSize, S("totlayer"));
    u64 stride = blend->typeSizes[blend->structs[layerType][0]], nameAt, nameSize;
    if (!Prog__BlendField(blend, layerType, S("name"), &nameAt, &nameSize)) return NULL;
    for (i64 i = 0; layers && i < count && (u64)(i + 1) * stride <= layers->length; ++i) {
        const u8 *layer = layers->data + (u64)i * stride;
        if (!StrIsEqual(Prog__BlendName(layer + nameAt, nameSize), name)) continue;
        const Prog__BlendBlock *values = Prog__BlendAt(blend, owner, Prog__BlendPointer(blend, layerType, layer, stride, S("data")));
        return values && values->length >= bytes ? values->data : NULL;
    }
    return NULL;
}

// Adds mesh `block`'s faces, placed by `place`, as triangles fanned from each face's first corner.
function void Prog__BlendMesh(Arena *arena, const Prog__Blend *blend, const Prog__BlendBlock *block, const Prog__Affine *place, Prog_Mesh *out)
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
        f32 *points = ArrayAdd(arena, out->positions, (u64)vertices * 3);
        for (i64 v = 0; v < vertices; ++v) {
            f32 p[3];
            MemCopy(p, (const u8 *)positions + v * 12, 12);
            Prog__MeshPoint(points + v * 3, place, p[0], p[1], p[2]);
        }
        for (i64 f = 0; f < faces; ++f) {
            u32 from = Prog__Read32(offsets->data + f * 4), to = Prog__Read32(offsets->data + (f + 1) * 4);
            if (to > (u64)corners || from >= to) continue;
            for (u32 c = from + 2; c < to; ++c) {
                Prog__MeshTriangle(arena, out, (u32)base + Prog__Read32(cornerVerts + from * 4), (u32)base + Prog__Read32(cornerVerts + (c - 1) * 4),
                                   (u32)base + Prog__Read32(cornerVerts + c * 4));
            }
        }
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
        if (data && data->code == Prog__BlendCode('M', 'E', 0, 0)) Prog__BlendMesh(arena, &blend, data, &place, mesh);
    }
    return mesh->indices.header.count > 0;
}

// -----------------------------------------------------------------------------
// USD: the meshes of a binary .usdc, or of the .usdc a .usdz zips, placed by their prims' transform ops and their parents', as of their
// default values (time samples are not read). The crate format keeps its tokens, fields, paths and specs in sections, most of them LZ4
// compressed, and arrays of integers as deltas in 1, 2 or 4 bytes as each needs.

function String Prog__ZipEntry(Arena *arena, String path, u64 size, String name);

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

function b32 Prog__ReadUsd(Arena *arena, String path, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    // A .usdz is a zip whose first file is its root layer.
    if (data.length >= 4 && Prog__Read32((const u8 *)data.value) == 0x04034B50) data = Prog__ZipEntry(arena, path, data.length, (String){0});
    Prog__Usd usd = {0};
    if (!Prog__UsdParse(arena, data, &usd)) return false;
    usd.xformOpOrder = Prog__UsdTokenIndex(&usd, S("xformOpOrder"));
    // Each prim path's spec, and each property's by its prim and name.
    u32 *primSpec = ArenaPushArray(arena, Max(usd.pathCount, 1u), u32);
    for (u32 i = 0; i < usd.pathCount; ++i) primSpec[i] = UINT32_MAX;
    Prog__UsdProperties properties = {0};
    for (u32 s = 0; s < usd.specCount; ++s) {
        u32 p = usd.specPath[s];
        if (p >= usd.pathCount) continue;
        if (usd.pathProperty[p] && usd.pathParent[p] < usd.pathCount) MapPut(arena, properties, (u64)usd.pathParent[p] << 32 | usd.pathToken[p], s);
        else if (!usd.pathProperty[p]) primSpec[p] = s;
    }
    // Z is up as the stage says, which Blender's exports do; Y otherwise.
    u32 root = UINT32_MAX;
    for (u32 i = 0; i < usd.pathCount && root == UINT32_MAX; ++i) if (usd.pathParent[i] == UINT32_MAX && primSpec[i] != UINT32_MAX) root = i;
    u64 up = root != UINT32_MAX ? Prog__UsdField(&usd, primSpec[root], S("upAxis")) : 0;
    mesh->zUp = up && Prog__UsdInlined(up) && Prog__UsdPayload(up) < usd.tokenCount && StrIsEqual(usd.tokens[Prog__UsdPayload(up)], S("Z"));
    Prog__Affine *world = ArenaPushArray(arena, Max(usd.pathCount, 1u), Prog__Affine);
    b8 *placed = ArenaPushArray(arena, Max(usd.pathCount, 1u), b8);
    u32 pointsToken = Prog__UsdTokenIndex(&usd, S("points")), countsToken = Prog__UsdTokenIndex(&usd, S("faceVertexCounts"));
    u32 indicesToken = Prog__UsdTokenIndex(&usd, S("faceVertexIndices"));
    for (u32 prim = 0; prim < usd.pathCount && !P_IsCancelled(cancel); ++prim) {
        u64 type = primSpec[prim] != UINT32_MAX ? Prog__UsdField(&usd, primSpec[prim], S("typeName")) : 0;
        if (!type || !Prog__UsdInlined(type) || Prog__UsdPayload(type) >= usd.tokenCount || !StrIsEqual(usd.tokens[Prog__UsdPayload(type)], S("Mesh"))) continue;
        const u32 *pointsSpec = MapGetPtrNull(properties, (u64)prim << 32 | pointsToken);
        const u32 *countsSpec = MapGetPtrNull(properties, (u64)prim << 32 | countsToken), *indicesSpec = MapGetPtrNull(properties, (u64)prim << 32 | indicesToken);
        if (!pointsSpec || !countsSpec || !indicesSpec) continue;
        u64 pointCount, faceCount, cornerCount;
        f32 *points = Prog__UsdVec3(arena, &usd, Prog__UsdField(&usd, *pointsSpec, S("default")), &pointCount);
        i32 *counts = Prog__UsdIntArray(arena, &usd, Prog__UsdField(&usd, *countsSpec, S("default")), &faceCount);
        i32 *corners = Prog__UsdIntArray(arena, &usd, Prog__UsdField(&usd, *indicesSpec, S("default")), &cornerCount);
        if (!points || !counts || !corners) continue;
        // Its place: its own ops, then each parent's, worked out once for each.
        u32 chain[256], depth = 0;
        for (u32 p = prim; p < usd.pathCount && !placed[p] && depth < CArrayCount(chain); p = usd.pathParent[p]) chain[depth++] = p;
        while (depth--) {
            u32 p = chain[depth], up2 = usd.pathParent[p];
            Prog__Affine above = up2 < usd.pathCount && placed[up2] ? world[up2] : Prog__AffineIdentity;
            world[p] = Prog__AffineMultiply(above, Prog__UsdLocal(arena, &usd, p, primSpec, &properties));
            placed[p] = true;
        }
        u64 base = mesh->positions.header.count / 3;
        f32 *out = ArrayAdd(arena, mesh->positions, pointCount * 3);
        for (u64 v = 0; v < pointCount; ++v) Prog__MeshPoint(out + v * 3, &world[prim], points[v * 3], points[v * 3 + 1], points[v * 3 + 2]);
        for (u64 f = 0, first = 0; f < faceCount && first < cornerCount; first += (u64)Max(counts[f], 0), ++f) {
            for (u64 c = first + 2; c < first + (u64)Max(counts[f], 0) && c < cornerCount; ++c) {
                Prog__MeshTriangle(arena, mesh, (u32)(base + (u32)corners[first]), (u32)(base + (u32)corners[c - 1]), (u32)(base + (u32)corners[c]));
            }
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
    Prog_ModelKind_Count,
} Prog_ModelKind;

function Prog_ModelKind Prog_ModelKindOf(String path)
{
    local_persist const String extensions[Prog_ModelKind_Count] = { {0}, SInit(".obj"), SInit(".stl"), SInit(".ply"), SInit(".gltf"), SInit(".glb"), SInit(".fbx"), SInit(".blend"), SInit(".usd"), SInit(".usdc"), SInit(".usdz") };
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

function b32 Prog_ReadModel(Arena *arena, Prog_ModelKind kind, String path, String data, const P_CancelToken *cancel, Prog_Mesh *mesh)
{
    switch (kind) {
        case Prog_ModelKind_Obj: return Prog__ReadObj(arena, data, cancel, mesh);
        case Prog_ModelKind_Stl: return Prog__ReadStl(arena, data, cancel, mesh);
        case Prog_ModelKind_Ply: return Prog__ReadPly(arena, data, cancel, mesh);
        case Prog_ModelKind_Gltf: return Prog__ReadGltf(arena, path, data, false, cancel, mesh);
        case Prog_ModelKind_Glb: return Prog__ReadGltf(arena, path, data, true, cancel, mesh);
        case Prog_ModelKind_Fbx: return Prog__ReadFbx(arena, data, cancel, mesh);
        case Prog_ModelKind_Blend: return Prog__ReadBlend(arena, data, cancel, mesh);
        // A .usd may be binary or text; only the binary crate is read.
        case Prog_ModelKind_Usd:
        case Prog_ModelKind_Usdc:
        case Prog_ModelKind_Usdz: return Prog__ReadUsd(arena, path, data, cancel, mesh);
        default: return false;
    }
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
    b8 *valid = ArenaPushArrayNoZero(scratch.arena, vertices, b8);
    f32 low[3] = { INFINITY, INFINITY, INFINITY }, high[3] = { -INFINITY, -INFINITY, -INFINITY };
    for (u64 i = 0; i < vertices; ++i) {
        f32 x = mesh->positions.v[i * 3], y = mesh->positions.v[i * 3 + 1], z = mesh->positions.v[i * 3 + 2];
        f32 p[3] = { x, mesh->zUp ? z : y, mesh->zUp ? -y : z };
        valid[i] = (b8)(isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]));
        MemCopy(positions + i * 3, p, sizeof(p));
        for (u32 k = 0; valid[i] && k < 3; ++k) low[k] = MinF32(low[k], p[k]), high[k] = MaxF32(high[k], p[k]);
    }
    u32 *indices = ArenaPushArrayNoZero(arena, triangles * 3, u32);
    u64 kept = 0;
    for (u64 t = 0; t < triangles; ++t) {
        const u32 *at = mesh->indices.v + t * 3;
        if (at[0] >= vertices || at[1] >= vertices || at[2] >= vertices || !valid[at[0]] || !valid[at[1]] || !valid[at[2]]) continue;
        MemCopy(indices + kept * 3, at, 12);
        ++kept;
    }
    if (kept && low[0] <= high[0]) {
        f32 center[3], radius = 0;
        for (u32 k = 0; k < 3; ++k) center[k] = (low[k] + high[k]) * 0.5f;
        for (u64 i = 0; i < vertices; ++i) {
            if (!valid[i]) continue;
            f32 dx = positions[i * 3] - center[0], dy = positions[i * 3 + 1] - center[1], dz = positions[i * 3 + 2] - center[2];
            radius = MaxF32(radius, dx * dx + dy * dy + dz * dz);
        }
        *shape = (Prog_ModelShape){ positions, (u32)vertices, indices, kept * 3, { center[0], center[1], center[2] }, MaxF32(sqrtf(radius), 1e-6f) };
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
    Prog_Mesh mesh = {0};
    b32 result = P_ReadFileStart(scratch.arena, path, size, &data) && data.length == size && !P_IsCancelled(cancel) && Prog_ReadModel(scratch.arena, kind, path, data, cancel, &mesh) &&
                 Prog__ModelShape(arena, &mesh, shape);
    ReleaseScratch(scratch);
    return result;
}

