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

// Draws `mesh` into `size` squared straight RGBA pixels, seen from in front of it, above and to its right without perspective, fitted to the
// square and lit from the upper left, on a clear background. It draws at twice the size and averages, for smooth edges. Each triangle shows
// the side that faces the view, so that meshes wound either way look alike. NULL when nothing was drawn or the job was cancelled.
function u32 *Prog_DrawMesh(Arena *arena, const Prog_Mesh *mesh, i32 size, const P_CancelToken *cancel)
{
    u64 vertices = mesh->positions.header.count / 3, triangles = mesh->indices.header.count / 3;
    if (!vertices || !triangles || size <= 0) return NULL;
    Scratch scratch = GetScratchEx(arena);
    u32 *result = NULL;
    // Turned 30 degrees about the up axis and tipped 20 degrees toward the view.
    f32 cy = 0.8660254f, sy = 0.5f, cp = 0.9396926f, sp = 0.3420201f;
    f32 *view = ArenaPushArrayNoZero(scratch.arena, vertices * 3, f32);
    u8 *valid = ArenaPushArrayNoZero(scratch.arena, vertices, u8);
    f32 minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    const f32 *positions = mesh->positions.v;
    for (u64 i = 0; i < vertices; ++i) {
        f32 x = positions[i * 3], y = positions[i * 3 + 1], z = positions[i * 3 + 2];
        if (mesh->zUp) {
            f32 up = z;
            z = -y;
            y = up;
        }
        f32 x1 = cy * x - sy * z, z1 = sy * x + cy * z;
        f32 y2 = cp * y - sp * z1, z2 = sp * y + cp * z1;
        view[i * 3] = x1;
        view[i * 3 + 1] = y2;
        view[i * 3 + 2] = z2;
        valid[i] = (u8)(isfinite(x1) && isfinite(y2) && isfinite(z2));
        if (valid[i]) {
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
        u8 *shade = ArenaPushArray(scratch.arena, samples, u8);
        for (u64 i = 0; i < samples; ++i) depth[i] = -INFINITY;
        // The light, toward the upper left and the view.
        const f32 lx = -0.398f, ly = 0.597f, lz = 0.697f;
        b32 cancelled = false;
        const u32 *indices = mesh->indices.v;
        for (u64 t = 0; t < triangles; ++t) {
            if ((t & 0x3FFF) == 0x3FFF && P_IsCancelled(cancel)) {
                cancelled = true;
                break;
            }
            u32 ia = indices[t * 3], ib = indices[t * 3 + 1], ic = indices[t * 3 + 2];
            if (ia >= vertices || ib >= vertices || ic >= vertices || !valid[ia] || !valid[ib] || !valid[ic]) continue;
            const f32 *a = view + (u64)ia * 3, *b = view + (u64)ib * 3, *c = view + (u64)ic * 3;
            // The normal, in a space with Y up again.
            f32 ux = b[0] - a[0], uy = a[1] - b[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = a[1] - c[1], vz = c[2] - a[2];
            f32 nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            f32 length = sqrtf(nx * nx + ny * ny + nz * nz);
            if (!(length > 0)) continue;
            if (nz < 0) nx = -nx, ny = -ny, nz = -nz;
            f32 light = (nx * lx + ny * ly + nz * lz) / length;
            u8 tone = (u8)(255.0f * (0.22f + 0.78f * MaxF32(light, 0)));
            f32 area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            if (area < 0) {
                const f32 *swap = b;
                b = c;
                c = swap;
                area = -area;
            }
            i32 x0 = (i32)MaxF32(floorf(MinF32(a[0], MinF32(b[0], c[0]))), 0), x1 = (i32)MinF32(floorf(MaxF32(a[0], MaxF32(b[0], c[0]))), (f32)(side - 1));
            i32 y0 = (i32)MaxF32(floorf(MinF32(a[1], MinF32(b[1], c[1]))), 0), y1 = (i32)MinF32(floorf(MaxF32(a[1], MaxF32(b[1], c[1]))), (f32)(side - 1));
            if (x0 > x1 || y0 > y1) continue;
            // A triangle inside one sample may miss its center; it takes the sample, so that dense meshes show no holes.
            if (x0 == x1 && y0 == y1) {
                u64 at = (u64)y0 * (u64)side + (u64)x0;
                f32 z = MaxF32(a[2], MaxF32(b[2], c[2]));
                if (z > depth[at]) depth[at] = z, shade[at] = tone;
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
                u8 *shadeRow = shade + (u64)y * (u64)side;
                for (i32 x = x0; x <= x1; ++x) {
                    if (w0 >= 0 && w1 >= 0 && w2 >= 0 && z > depthRow[x]) {
                        depthRow[x] = z;
                        shadeRow[x] = tone;
                    }
                    w0 += e0x, w1 += e1x, w2 += e2x, z += zx;
                }
                w0Row += e0y, w1Row += e1y, w2Row += e2y;
            }
        }
        if (!cancelled) {
            // Each pixel averages its four samples: the shade of those covered, and how many are covered as its alpha.
            const u32 red = 200, green = 204, blue = 212;
            result = ArenaPushArrayNoZero(arena, (u64)size * (u64)size, u32);
            for (i32 y = 0; y < size; ++y) {
                for (i32 x = 0; x < size; ++x) {
                    u64 at = (u64)y * 2 * (u64)side + (u64)x * 2;
                    u64 corners[4] = { at, at + 1, at + (u64)side, at + (u64)side + 1 };
                    u32 covered = 0, sum = 0;
                    for (u32 k = 0; k < 4; ++k) {
                        if (depth[corners[k]] > -INFINITY) {
                            ++covered;
                            sum += shade[corners[k]];
                        }
                    }
                    u32 pixel = 0;
                    if (covered) {
                        u32 tone = sum / covered;
                        pixel = red * tone / 255 | (green * tone / 255) << 8 | (blue * tone / 255) << 16 | (covered * 255 / 4) << 24;
                    }
                    result[(u64)y * (u64)size + (u64)x] = pixel;
                }
            }
        }
    }
    ReleaseScratch(scratch);
    return result;
}

// -----------------------------------------------------------------------------
// Preview
// -----------------------------------------------------------------------------

typedef enum Prog_ModelKind {
    Prog_ModelKind_None,
    Prog_ModelKind_Obj,
    Prog_ModelKind_Stl,
    Prog_ModelKind_Ply,
    Prog_ModelKind_Gltf,
    Prog_ModelKind_Glb,
    Prog_ModelKind_Fbx,
    Prog_ModelKind_Count,
} Prog_ModelKind;

function Prog_ModelKind Prog_ModelKindOf(String path)
{
    local_persist const String extensions[Prog_ModelKind_Count] = { {0}, SInit(".obj"), SInit(".stl"), SInit(".ply"), SInit(".gltf"), SInit(".glb"), SInit(".fbx") };
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
        default: return false;
    }
}

function u32 *Prog_ModelPreview(Arena *arena, String path, u64 size, const P_CancelToken *cancel)
{
    Prog_ModelKind kind = Prog_ModelKindOf(path);
    if (!kind || !size || size > Prog_ModelMaxBytes) return NULL;
    Scratch scratch = GetScratchEx(arena);
    String data = {0};
    Prog_Mesh mesh = {0};
    u32 *result = NULL;
    if (P_ReadFileStart(scratch.arena, path, size, &data) && data.length == size && !P_IsCancelled(cancel) && Prog_ReadModel(scratch.arena, kind, path, data, cancel, &mesh)) {
        result = Prog_DrawMesh(arena, &mesh, Prog_PreviewSize, cancel);
    }
    ReleaseScratch(scratch);
    return result;
}
