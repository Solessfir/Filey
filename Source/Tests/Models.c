// 3D model previews: each format's reader on a small file built here, the placement it reads, files cut short at every length, and the drawing.

typedef struct TestBytes {
    u8 data[2048];
    u64 length;
} TestBytes;

function void TestPut(TestBytes *bytes, const void *data, u64 length)
{
    MemCopy(bytes->data + bytes->length, data, length);
    bytes->length += length;
}

function void TestPutU32(TestBytes *bytes, u32 value) { TestPut(bytes, &value, 4); }

// Reads every shorter start of `data` as `kind`, which must fail or read less without reading past the end, then `data` itself into `arena`.
function b32 TestReadModel(Arena *arena, Prog_ModelKind kind, String data, Prog_Mesh *mesh)
{
    Arena cuts = {0};
    P_CancelToken token = {0};
    for (u64 length = 0; length < data.length; ++length) {
        Prog_Mesh cut = {0};
        Prog_ReadModel(&cuts, kind, S("/model"), StrMake(data.value, length), &token, &cut);
        ArenaClear(&cuts);
    }
    ArenaRelease(&cuts);
    *mesh = (Prog_Mesh){0};
    return Prog_ReadModel(arena, kind, S("/model"), data, &token, mesh);
}

// The FBX binary form: a node's header is patched once its values and children are written.
function u64 TestFbxBegin(TestBytes *bytes, const char *name)
{
    u64 at = bytes->length;
    u8 length = (u8)StrFromCStr(name).length;
    TestPutU32(bytes, 0);
    TestPutU32(bytes, 0);
    TestPutU32(bytes, 0);
    TestPut(bytes, &length, 1);
    TestPut(bytes, name, length);
    return at;
}

function void TestFbxValues(TestBytes *bytes, u64 at, u32 count)
{
    u32 length = (u32)(bytes->length - at - 13 - bytes->data[at + 12]);
    MemCopy(bytes->data + at + 4, &count, 4);
    MemCopy(bytes->data + at + 8, &length, 4);
}

function void TestFbxEnd(TestBytes *bytes, u64 at, b32 children)
{
    if (children) {
        u8 zero[13] = {0};
        TestPut(bytes, zero, 13);
    }
    u32 end = (u32)bytes->length;
    MemCopy(bytes->data + at, &end, 4);
}

function void TestFbxString(TestBytes *bytes, const char *text)
{
    u8 type = 'S';
    TestPut(bytes, &type, 1);
    TestPutU32(bytes, (u32)StrFromCStr(text).length);
    TestPut(bytes, text, StrFromCStr(text).length);
}

function void TestFbxId(TestBytes *bytes, i64 id)
{
    u8 type = 'L';
    TestPut(bytes, &type, 1);
    TestPut(bytes, &id, 8);
}

function void TestFbxDouble(TestBytes *bytes, f64 value)
{
    u8 type = 'D';
    TestPut(bytes, &type, 1);
    TestPut(bytes, &value, 8);
}

function void TestFbxArray(TestBytes *bytes, u8 type, u32 count, u32 encoding, const void *data, u32 length)
{
    TestPut(bytes, &type, 1);
    TestPutU32(bytes, count);
    TestPutU32(bytes, encoding);
    TestPutU32(bytes, length);
    TestPut(bytes, data, length);
}

// A 3DS chunk's id and room for its length, which Test3dsEnd fills in once its contents are written.
function u64 Test3dsBegin(TestBytes *bytes, u16 id)
{
    u64 at = bytes->length;
    TestPut(bytes, &id, 2);
    TestPutU32(bytes, 0);
    return at;
}

function void Test3dsEnd(TestBytes *bytes, u64 at)
{
    u32 length = (u32)(bytes->length - at);
    MemCopy(bytes->data + at + 2, &length, 4);
}

// A zip of `count` files stored as they are, as a .3mf package can be.
function void TestZip(TestBytes *bytes, const String *names, const String *contents, u32 count)
{
    u32 offsets[8] = {0};
    for (u32 i = 0; i < count; ++i) {
        offsets[i] = (u32)bytes->length;
        u32 size = (u32)contents[i].length;
        u16 nameLength = (u16)names[i].length, zero = 0;
        TestPutU32(bytes, 0x04034B50);
        TestPut(bytes, "\x14\0\0\0\0\0\0\0\0\0\0\0\0\0", 14);
        TestPutU32(bytes, size);
        TestPutU32(bytes, size);
        TestPut(bytes, &nameLength, 2);
        TestPut(bytes, &zero, 2);
        TestPut(bytes, names[i].value, names[i].length);
        TestPut(bytes, contents[i].value, contents[i].length);
    }
    u32 directory = (u32)bytes->length;
    for (u32 i = 0; i < count; ++i) {
        u32 size = (u32)contents[i].length;
        u16 nameLength = (u16)names[i].length;
        TestPutU32(bytes, 0x02014B50);
        TestPut(bytes, "\x14\0\x14\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);
        TestPutU32(bytes, size);
        TestPutU32(bytes, size);
        TestPut(bytes, &nameLength, 2);
        TestPut(bytes, "\0\0\0\0\0\0\0\0\0\0\0\0", 12);
        TestPutU32(bytes, offsets[i]);
        TestPut(bytes, names[i].value, names[i].length);
    }
    u32 size = (u32)bytes->length - directory;
    u16 entries = (u16)count;
    TestPutU32(bytes, 0x06054B50);
    TestPut(bytes, "\0\0\0\0", 4);
    TestPut(bytes, &entries, 2);
    TestPut(bytes, &entries, 2);
    TestPutU32(bytes, size);
    TestPutU32(bytes, directory);
    TestPut(bytes, "\0\0", 2);
}

function void TestModels(void)
{
    Arena arena = {0};
    Prog_Mesh mesh = {0};

    // OBJ: a quad and a triangle given from the end, with texture and normal indices; a vertex with another texture coordinate, or none,
    // is a vertex of its own.
    String obj = S("# test\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0.25\nf 1/1/1 2/1/1 3/1/1 4/1/1\nf -4 -3 -1\n");
    Check(TestReadModel(&arena, Prog_ModelKind_Obj, obj, &mesh) && mesh.positions.header.count == 21 && mesh.indices.header.count == 9 && !mesh.zUp);
    Check(mesh.indices.v[3] == 0 && mesh.indices.v[4] == 2 && mesh.indices.v[5] == 3 && mesh.indices.v[6] == 4 && mesh.indices.v[8] == 6 && mesh.positions.v[6 * 3 + 1] == 1);
    Check(mesh.uvs.header.count >= 8 && mesh.uvs.v[1] == 0.75f && !mesh.colors.header.count);
    // Colors after a vertex's position, sRGB from 0 to 1.
    Check(TestReadModel(&arena, Prog_ModelKind_Obj, S("v 0 0 0 1 0 0\nv 1 0 0\nv 0 1 0 0 0 1\nf 1 2 3\n"), &mesh) && mesh.colors.header.count == 3);
    Check(mesh.colors.v[0] == 0xFF0000FFu && mesh.colors.v[1] == Prog_ModelUnset && mesh.colors.v[2] == 0xFFFF0000u);

    // STL, as text and in binary, with Z up.
    String stl = S("solid t\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid t\n");
    Check(TestReadModel(&arena, Prog_ModelKind_Stl, stl, &mesh) && mesh.positions.header.count == 9 && mesh.indices.header.count == 3 && mesh.zUp && mesh.positions.v[3] == 1);
    {
        TestBytes bytes = {0};
        u8 header[80] = {0};
        f32 triangle[12] = { 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 2, 0 };
        u16 attributes = 0;
        TestPut(&bytes, header, 80);
        TestPutU32(&bytes, 1);
        TestPut(&bytes, triangle, 48);
        TestPut(&bytes, &attributes, 2);
        Check(TestReadModel(&arena, Prog_ModelKind_Stl, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.indices.header.count == 3 && mesh.positions.v[3] == 2);
        Check(!mesh.colors.header.count);
        // A facet's color, red in the high bits with the top bit set; Magics puts red low and clears the bit for a facet's own color.
        u16 red = 0x8000 | 31 << 10;
        MemCopy(bytes.data + 84 + 48, &red, 2);
        Check(TestReadModel(&arena, Prog_ModelKind_Stl, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.colors.header.count == 3 && mesh.colors.v[2] == 0xFF0000FFu);
        MemCopy(bytes.data, "COLOR=\x00\xFF\x00\xFF", 10);
        u16 magicsRed = 31, shared = 0x8000;
        MemCopy(bytes.data + 84 + 48, &magicsRed, 2);
        Check(TestReadModel(&arena, Prog_ModelKind_Stl, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.colors.v[0] == 0xFF0000FFu);
        MemCopy(bytes.data + 84 + 48, &shared, 2);
        Check(TestReadModel(&arena, Prog_ModelKind_Stl, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.colors.v[0] == 0xFF00FF00u);
    }

    // PLY, as text with an extra property and element to step over, and in little-endian binary.
    String ply = S("ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\n"
                   "element face 1\nproperty list uchar int vertex_indices\nelement edge 1\nproperty int a\nend_header\n"
                   "0 0 0 9\n1 0 0 9\n1 1 0 9\n0 1 0 9\n4 0 1 2 3\n7\n");
    Check(TestReadModel(&arena, Prog_ModelKind_Ply, ply, &mesh) && mesh.positions.header.count == 12 && mesh.indices.header.count == 6 && mesh.zUp && mesh.indices.v[5] == 3);
    // Colors as bytes, with black and clear, as Blender writes a vertex without one, left unset, and texture coordinates.
    String colored = S("ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\nproperty uchar green\n"
                       "property uchar blue\nproperty uchar alpha\nproperty float s\nproperty float t\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n"
                       "0 0 0 255 0 0 255 0 0.25\n1 0 0 0 0 0 0 1 0\n0 1 0 0 0 255 255 0 1\n3 0 1 2\n");
    Check(TestReadModel(&arena, Prog_ModelKind_Ply, colored, &mesh) && mesh.colors.header.count == 3 && mesh.uvs.header.count == 6);
    Check(mesh.colors.v[0] == 0xFF0000FFu && mesh.colors.v[1] == Prog_ModelUnset && mesh.colors.v[2] == 0xFFFF0000u && mesh.uvs.v[1] == 0.75f && mesh.uvs.v[2] == 1);
    // A face's own texture coordinates, as MeshLab writes them, make its corners vertices of their own with them.
    String faceMapped = S("ply\nformat ascii 1.0\ncomment TextureFile skin.png\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                          "element face 1\nproperty list uchar int vertex_indices\nproperty list uchar float texcoord\nend_header\n"
                          "0 0 0\n1 0 0\n0 1 0\n3 0 1 2 6 0 0 1 0 0.5 1\n");
    Check(TestReadModel(&arena, Prog_ModelKind_Ply, faceMapped, &mesh) && mesh.positions.header.count == 18 && mesh.indices.header.count == 3);
    Check(mesh.indices.v[0] == 3 && mesh.indices.v[2] == 5 && mesh.positions.v[3 * 4] == 1 && mesh.uvs.v[3 * 2 + 1] == 1 && mesh.uvs.v[5 * 2] == 0.5f && mesh.uvs.v[5 * 2 + 1] == 0);
    {
        TestBytes bytes = {0};
        const char *header = "ply\nformat binary_little_endian 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\n"
                             "element face 1\nproperty list uchar uint vertex_indices\nend_header\n";
        TestPut(&bytes, header, StrFromCStr(header).length);
        f32 vertices[9] = { 0, 0, 0, 3, 0, 0, 0, 3, 0 };
        u8 corners = 3;
        u32 face[3] = { 0, 1, 2 };
        TestPut(&bytes, vertices, sizeof(vertices));
        TestPut(&bytes, &corners, 1);
        TestPut(&bytes, face, sizeof(face));
        Check(TestReadModel(&arena, Prog_ModelKind_Ply, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.indices.header.count == 3 && mesh.positions.v[3] == 3);
    }

    // glTF with its buffer in a data URI, placed by its node one to the right, and the same as a .glb with the buffer in its own chunk.
    const char *scene = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"translation\":[1,0,0]}],"
                        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                        "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
                        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],";
    {
        String gltf = StrFormat(&arena, "%s\"buffers\":[{\"byteLength\":44,\"uri\":\"data:application/octet-stream;base64,%s\"}]}", scene,
                                "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA=");
        Check(TestReadModel(&arena, Prog_ModelKind_Gltf, gltf, &mesh) && mesh.positions.header.count == 9 && mesh.indices.header.count == 3);
        Check(mesh.positions.v[0] == 1 && mesh.positions.v[3] == 2 && mesh.positions.v[7] == 1 && mesh.indices.v[2] == 2);
        // A blended material is see-through by its base color's alpha.
        String blended = StrFormat(&arena, "{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"material\":0}]}],"
                                   "\"materials\":[{\"alphaMode\":\"BLEND\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,0.5]}}],"
                                   "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
                                   "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
                                   "\"buffers\":[{\"byteLength\":44,\"uri\":\"data:application/octet-stream;base64,%s\"}]}", "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA=");
        Check(TestReadModel(&arena, Prog_ModelKind_Gltf, blended, &mesh) && mesh.parts.header.count == 1 && mesh.parts.v[0].blend && mesh.parts.v[0].color == 0x800000FFu);
        String json = StrFormat(&arena, "%s\"buffers\":[{\"byteLength\":44}]}", scene);
        TestBytes bytes = {0};
        f32 positions[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        u16 indices[4] = { 0, 1, 2, 0 };
        u32 padded = (u32)(json.length + 3) / 4 * 4;
        TestPutU32(&bytes, 0x46546C67);
        TestPutU32(&bytes, 2);
        TestPutU32(&bytes, 12 + 8 + padded + 8 + 44);
        TestPutU32(&bytes, padded);
        TestPutU32(&bytes, 0x4E4F534A);
        TestPut(&bytes, json.value, json.length);
        TestPut(&bytes, "   ", padded - json.length);
        TestPutU32(&bytes, 44);
        TestPutU32(&bytes, 0x004E4942);
        TestPut(&bytes, positions, 36);
        TestPut(&bytes, indices, 8);
        Check(TestReadModel(&arena, Prog_ModelKind_Glb, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.indices.header.count == 3 && mesh.positions.v[3] == 2);
        // Its material's base color, linear, and picture, from a data URI, and its vertices' colors and texture coordinates.
        {
            String painted = S("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"COLOR_0\":2,\"TEXCOORD_0\":3},\"indices\":1,\"material\":0}]}],"
                               "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,1,1,1],\"baseColorTexture\":{\"index\":0,\"extensions\":{\"KHR_texture_transform\":{\"offset\":[0.5,0],\"scale\":[2,2]}}}}}],\"textures\":[{\"source\":0}],"
                               "\"images\":[{\"uri\":\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNg+M/wHwAEAQH/cetH5QAAAABJRU5ErkJggg==\"}],"
                               "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6},{\"buffer\":0,\"byteOffset\":44,\"byteLength\":36},"
                               "{\"buffer\":0,\"byteOffset\":80,\"byteLength\":24}],"
                               "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
                               "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":3,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}],"
                               "\"buffers\":[{\"byteLength\":104,\"uri\":\"data:,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8=\"}]}");
            Prog_Mesh textured = { .imageSide = 64 };
            P_CancelToken token = {0};
            Check(Prog_ReadModel(&arena, Prog_ModelKind_Gltf, S("/model.gltf"), painted, &token, &textured) && textured.parts.header.count == 1 && textured.images.header.count == 1);
            Check(textured.parts.v[0].color == 0xFFFFFFBCu && textured.parts.v[0].image == 0 && textured.images.v[0].width == 1 && textured.images.v[0].pixels[0] == 0xFF00FF00u);
            Check(textured.colors.header.count == 3 && textured.colors.v[0] == 0xFF0000FFu && textured.colors.v[2] == 0xFFFF0000u && textured.uvs.v[0] == 0.5f && textured.uvs.v[2] == 2.5f && textured.uvs.v[5] == 2);
            Prog_ModelShape shape;
            Check(Prog__ModelShape(&arena, &textured, &shape) && shape.partCount == 1 && shape.parts[0].image == 0 && shape.imageCount == 1 && shape.images[0].pixels[0] == 0xFF00FF00u);
            // Without the pictures asked for, it keeps its color alone.
            Check(TestReadModel(&arena, Prog_ModelKind_Gltf, painted, &mesh) && mesh.parts.v[0].image == UINT32_MAX && !mesh.images.header.count);
        }
        // An accessor past its buffer's end is left out rather than read.
        String past = StrFormat(&arena, "%s\"buffers\":[{\"byteLength\":20,\"uri\":\"data:,AAAAAAAAAAAAAAAAAACAPwAAAAA=\"}]}", scene);
        Check(!TestReadModel(&arena, Prog_ModelKind_Gltf, past, &mesh));
    }

    // Binary FBX: a mesh whose vertices are zlib-compressed, hung from a model moved five to the right.
    {
        TestBytes bytes = {0};
        u32 version = 7400;
        TestPut(&bytes, "Kaydara FBX Binary  \0\x1a\0", 23);
        TestPut(&bytes, &version, 4);
        u64 objects = TestFbxBegin(&bytes, "Objects");
        TestFbxValues(&bytes, objects, 0);
        u64 geometry = TestFbxBegin(&bytes, "Geometry");
        TestFbxId(&bytes, 10);
        TestFbxString(&bytes, "Geometry::g");
        TestFbxString(&bytes, "Mesh");
        TestFbxValues(&bytes, geometry, 3);
        u64 vertices = TestFbxBegin(&bytes, "Vertices");
        local_persist const u8 packed[] = { 0x78, 0x9C, 0x63, 0x60, 0xC0, 0x07, 0x3E, 0xD8, 0xE3, 0x95, 0x46, 0x92, 0x07, 0x00, 0x3D, 0x56, 0x02, 0x5F };
        TestFbxArray(&bytes, 'd', 9, 1, packed, sizeof(packed));
        TestFbxValues(&bytes, vertices, 1);
        TestFbxEnd(&bytes, vertices, false);
        u64 polygons = TestFbxBegin(&bytes, "PolygonVertexIndex");
        i32 corners[3] = { 0, 1, ~2 };
        TestFbxArray(&bytes, 'i', 3, 0, corners, sizeof(corners));
        TestFbxValues(&bytes, polygons, 1);
        TestFbxEnd(&bytes, polygons, false);
        TestFbxEnd(&bytes, geometry, true);
        u64 model = TestFbxBegin(&bytes, "Model");
        TestFbxId(&bytes, 20);
        TestFbxString(&bytes, "Model::m");
        TestFbxString(&bytes, "Mesh");
        TestFbxValues(&bytes, model, 3);
        u64 settings = TestFbxBegin(&bytes, "Properties70");
        TestFbxValues(&bytes, settings, 0);
        u64 p = TestFbxBegin(&bytes, "P");
        TestFbxString(&bytes, "Lcl Translation");
        TestFbxString(&bytes, "Lcl Translation");
        TestFbxString(&bytes, "");
        TestFbxString(&bytes, "A");
        TestFbxDouble(&bytes, 5);
        TestFbxDouble(&bytes, 0);
        TestFbxDouble(&bytes, 0);
        TestFbxValues(&bytes, p, 7);
        TestFbxEnd(&bytes, p, false);
        TestFbxEnd(&bytes, settings, true);
        TestFbxEnd(&bytes, model, true);
        TestFbxEnd(&bytes, objects, true);
        u64 connections = TestFbxBegin(&bytes, "Connections");
        TestFbxValues(&bytes, connections, 0);
        u64 link = TestFbxBegin(&bytes, "C");
        TestFbxString(&bytes, "OO");
        TestFbxId(&bytes, 10);
        TestFbxId(&bytes, 20);
        TestFbxValues(&bytes, link, 3);
        TestFbxEnd(&bytes, link, false);
        TestFbxEnd(&bytes, connections, true);
        u8 zero[13] = {0};
        TestPut(&bytes, zero, 13);
        Check(TestReadModel(&arena, Prog_ModelKind_Fbx, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.positions.header.count == 9 && mesh.indices.header.count == 3);
        Check(mesh.positions.v[0] == 5 && mesh.positions.v[3] == 6 && mesh.positions.v[7] == 1 && mesh.indices.v[2] == 2);
    }

    // Text USD: a quad under a prim moved five along X, Z up, red by its display color and bound to a green material through its shader;
    // documentation, time samples and a variant set are passed over.
    {
        String text = S("#usda 1.0\n(\n    doc = \"\"\"A (long) \"doc\" string\"\"\"\n    upAxis = \"Z\"\n)\n\n"
                        "def Xform \"root\" (\n    customData = { dictionary d = { bool b = 1 } }\n)\n{\n"
                        "    double3 xformOp:translate = (5, 0, 0)\n"
                        "    float3 xformOp:scale.timeSamples = { 0: (1, 1, 1), 10: (2, 2, 2) }\n"
                        "    uniform token[] xformOpOrder = [\"xformOp:translate\"]\n"
                        "    variantSet \"look\" = { \"a\" { } }\n"
                        "    def Mesh \"quad\" (\n        prepend apiSchemas = [\"MaterialBindingAPI\"]\n    )\n    {\n"
                        "        int[] faceVertexCounts = [4]\n        int[] faceVertexIndices = [0, 1, 2, 3]\n"
                        "        point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]\n"
                        "        color3f[] primvars:displayColor = [(1, 0, 0)] (\n            interpolation = \"constant\"\n        )\n"
                        "        rel material:binding = </root/look/green>\n    }\n"
                        "    def Scope \"look\"\n    {\n        def Material \"green\"\n        {\n"
                        "            token outputs:surface.connect = </root/look/green/surface.outputs:surface>\n"
                        "            def Shader \"surface\"\n            {\n                uniform token info:id = \"UsdPreviewSurface\"\n"
                        "                color3f inputs:diffuseColor = (0, 1, 0)\n                token outputs:surface\n            }\n        }\n    }\n}\n");
        Check(TestReadModel(&arena, Prog_ModelKind_Usd, text, &mesh) && mesh.zUp && mesh.positions.header.count == 12 && mesh.indices.header.count == 6);
        Check(mesh.positions.v[0] == 5 && mesh.positions.v[3] == 6 && mesh.colors.header.count == 4 && mesh.colors.v[0] == 0xFF0000FFu);
        Check(mesh.parts.header.count == 1 && mesh.parts.v[0].color == 0xFF00FF00u);
    }

    // Collada: a quad as a polylist and a triangle with vertex colors in one geometry, put in the scene twice by nodes moved apart, the quad
    // in its material's red through the node's binding, with Z up.
    {
        String dae = S("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                       "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
                       "  <asset><up_axis>Z_UP</up_axis></asset>\n"
                       "  <library_effects><effect id=\"red-effect\"><profile_COMMON><technique sid=\"common\"><lambert>\n"
                       "    <diffuse><color sid=\"diffuse\">1 0 0 1</color></diffuse></lambert></technique></profile_COMMON></effect></library_effects>\n"
                       "  <library_materials><material id=\"red\" name=\"red\"><instance_effect url=\"#red-effect\"/></material></library_materials>\n"
                       "  <library_geometries><geometry id=\"shape\" name=\"shape\"><mesh>\n"
                       "    <source id=\"shape-positions\"><float_array id=\"shape-positions-array\" count=\"21\">0 0 0 1 0 0 1 1 0 0 1 0\n 5 5 5 6 5 5 5 6 5</float_array>\n"
                       "      <technique_common><accessor source=\"#shape-positions-array\" count=\"7\" stride=\"3\"/></technique_common></source>\n"
                       "    <source id=\"shape-colors\"><float_array id=\"shape-colors-array\" count=\"4\">0 1 0 1</float_array>\n"
                       "      <technique_common><accessor source=\"#shape-colors-array\" count=\"1\" stride=\"4\"/></technique_common></source>\n"
                       "    <vertices id=\"shape-vertices\"><input semantic=\"POSITION\" source=\"#shape-positions\"/></vertices>\n"
                       "    <polylist material=\"red-material\" count=\"1\"><input semantic=\"VERTEX\" source=\"#shape-vertices\" offset=\"0\"/>\n"
                       "      <vcount>4 </vcount><p>0 1 2 3</p></polylist>\n"
                       "    <triangles count=\"1\"><input semantic=\"VERTEX\" source=\"#shape-vertices\" offset=\"0\"/><input semantic=\"COLOR\" source=\"#shape-colors\" offset=\"1\"/>\n"
                       "      <p>4 0 5 0 6 0</p></triangles>\n"
                       "  </mesh></geometry></library_geometries>\n"
                       "  <library_visual_scenes><visual_scene id=\"scene\">\n"
                       "    <node id=\"a\"><matrix sid=\"transform\">1 0 0 10 0 1 0 0 0 0 1 0 0 0 0 1</matrix>\n"
                       "      <instance_geometry url=\"#shape\"><bind_material><technique_common><instance_material symbol=\"red-material\" target=\"#red\"/></technique_common></bind_material></instance_geometry></node>\n"
                       "    <node id=\"b\"><translate>0 0 -3</translate><instance_geometry url=\"#shape\"/></node>\n"
                       "  </visual_scene></library_visual_scenes>\n"
                       "  <scene><instance_visual_scene url=\"#scene\"/></scene>\n"
                       "</COLLADA>\n");
        Check(TestReadModel(&arena, Prog_ModelKind_Dae, dae, &mesh) && mesh.zUp && mesh.indices.header.count == 18 && mesh.positions.header.count == 42);
        Check(mesh.positions.v[0] == 10 && mesh.positions.v[3] == 11 && mesh.positions.v[21 + 2] == -3 && mesh.parts.header.count == 4);
        Check(mesh.parts.v[0].color == 0xFF0000FFu && !mesh.parts.v[1].color && !mesh.parts.v[2].color && mesh.colors.v[4] == 0xFF00FF00u);
        Check(Prog_ModelKindOf(S("scene.DAE")) == Prog_ModelKind_Dae && Prog_ModelKindOf(S("scene.usda")) == Prog_ModelKind_Usda);
    }

    // 3MF: a triangle in a base material's red, but for a corner a color group makes green, placed by a component and by the build's item,
    // the model part named by the package's relationships.
    {
        String names[] = { S("_rels/.rels"), S("3D/part.model") };
        String contents[] = {
            S("<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
              "<Relationship Target=\"/3D/part.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>"),
            S("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"millimeter\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\""
              " xmlns:m=\"http://schemas.microsoft.com/3dmanufacturing/material/2015/02\">\n<resources>\n"
              "<basematerials id=\"1\"><base name=\"red\" displaycolor=\"#FF0000\"/></basematerials>\n"
              "<m:colorgroup id=\"4\"><m:color color=\"#00FF00FF\"/></m:colorgroup>\n"
              "<object id=\"2\" type=\"model\" pid=\"1\" pindex=\"0\"><mesh><vertices><vertex x=\"0\" y=\"0\" z=\"0\"/><vertex x=\"1\" y=\"0\" z=\"0\"/>"
              "<vertex x=\"0\" y=\"1\" z=\"0\"/></vertices><triangles><triangle v1=\"0\" v2=\"1\" v3=\"2\"/>"
              "<triangle v1=\"0\" v2=\"2\" v3=\"1\" pid=\"4\" p1=\"0\"/></triangles></mesh></object>\n"
              "<object id=\"3\" type=\"model\"><components><component objectid=\"2\" transform=\"1 0 0 0 1 0 0 0 1 0 0 7\"/></components></object>\n"
              "</resources>\n<build><item objectid=\"3\" transform=\"1 0 0 0 1 0 0 0 1 5 0 0\"/></build>\n</model>\n"),
        };
        TestBytes bytes = {0};
        TestZip(&bytes, names, contents, 2);
        Check(TestReadModel(&arena, Prog_ModelKind_3mf, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.zUp && mesh.indices.header.count == 6);
        Check(mesh.positions.header.count == 18 && mesh.positions.v[0] == 5 && mesh.positions.v[2] == 7 && mesh.positions.v[3] == 6);
        Check(mesh.colors.header.count == 6 && mesh.colors.v[0] == 0xFF0000FFu && mesh.colors.v[3] == 0xFF00FF00u && Prog_ModelKindOf(S("print.3MF")) == Prog_ModelKind_3mf);
    }

    // 3DS: two triangles of one object, the second in a material's red by its face group and the first in none, and their texture
    // coordinates flipped to start at the top.
    {
        TestBytes bytes = {0};
        u64 main = Test3dsBegin(&bytes, 0x4D4D), editor = Test3dsBegin(&bytes, 0x3D3D);
        u64 object = Test3dsBegin(&bytes, 0x4000);
        TestPut(&bytes, "box", 4);
        u64 shape = Test3dsBegin(&bytes, 0x4100), points = Test3dsBegin(&bytes, 0x4110);
        u16 four = 4, two = 2, one = 1;
        f32 xyz[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 }, uv[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
        TestPut(&bytes, &four, 2);
        TestPut(&bytes, xyz, sizeof(xyz));
        Test3dsEnd(&bytes, points);
        u64 coordinates = Test3dsBegin(&bytes, 0x4140);
        TestPut(&bytes, &four, 2);
        TestPut(&bytes, uv, sizeof(uv));
        Test3dsEnd(&bytes, coordinates);
        u64 faces = Test3dsBegin(&bytes, 0x4120);
        u16 list[8] = { 0, 1, 2, 7, 0, 2, 3, 7 };
        TestPut(&bytes, &two, 2);
        TestPut(&bytes, list, sizeof(list));
        u64 group = Test3dsBegin(&bytes, 0x4130);
        TestPut(&bytes, "red", 4);
        TestPut(&bytes, &one, 2);
        TestPut(&bytes, &one, 2);
        Test3dsEnd(&bytes, group);
        Test3dsEnd(&bytes, faces);
        Test3dsEnd(&bytes, shape);
        Test3dsEnd(&bytes, object);
        u64 material = Test3dsBegin(&bytes, 0xAFFF), named = Test3dsBegin(&bytes, 0xA000);
        TestPut(&bytes, "red", 4);
        Test3dsEnd(&bytes, named);
        u64 diffuse = Test3dsBegin(&bytes, 0xA020), color = Test3dsBegin(&bytes, 0x0011);
        TestPut(&bytes, "\xFF\0\0", 3);
        Test3dsEnd(&bytes, color);
        Test3dsEnd(&bytes, diffuse);
        Test3dsEnd(&bytes, material);
        Test3dsEnd(&bytes, editor);
        Test3dsEnd(&bytes, main);
        Check(TestReadModel(&arena, Prog_ModelKind_3ds, StrMake((const char *)bytes.data, bytes.length), &mesh) && mesh.zUp && mesh.positions.header.count == 12);
        Check(mesh.indices.header.count == 6 && mesh.indices.v[0] == 0 && mesh.indices.v[2] == 3 && mesh.indices.v[5] == 2 && mesh.parts.header.count == 2 && mesh.parts.v[0].color == 0xFF0000FFu);
        Check(!mesh.parts.v[1].color && mesh.parts.v[1].first == 3 && mesh.uvs.v[1] == 1 && mesh.uvs.v[5] == 0 && Prog_ModelKindOf(S("old.3DS")) == Prog_ModelKind_3ds);
    }

    // Text FBX: the same mesh and model as the binary one, its arrays over more than one line, with comments, and a value-less node.
    {
        String text = S("; FBX 7.4.0 project file\n"
                        "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n}\n"
                        "Objects:  {\n"
                        "\tGeometry: 10, \"Geometry::g\", \"Mesh\" {\n"
                        "\t\tVertices: *9 {\n\t\t\ta: 0,0,0,1,0,\n0,0,1,0\n\t\t} \n"
                        "\t\tPolygonVertexIndex: *3 {\n\t\t\ta: 0,1,-3\n\t\t} \n"
                        "\t\tGeometryVersion: 124\n"
                        "\t}\n"
                        "\tModel: 20, \"Model::m\", \"Mesh\" {\n"
                        "\t\tProperties70:  {\n"
                        "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",5,0,0\n"
                        "\t\t}\n"
                        "\t\tShading: T\n"
                        "\t\tCulling:\n"
                        "\t}\n"
                        "}\n"
                        "Connections:  {\n\t;Model::m, Model::RootNode\n\tC: \"OO\",10,20\n}\n");
        Check(TestReadModel(&arena, Prog_ModelKind_Fbx, text, &mesh) && mesh.positions.header.count == 9 && mesh.indices.header.count == 3);
        Check(mesh.positions.v[0] == 5 && mesh.positions.v[3] == 6 && mesh.positions.v[7] == 1 && mesh.indices.v[2] == 2);
    }

    // The thumbnail drawing fills the middle and leaves the corners clear; a triangle naming a vertex that is not there is left out.
    {
        Prog_Mesh square = {0};
        f32 *v = ArrayAdd(&arena, square.positions, 12);
        f32 corners[12] = { -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0 };
        MemCopy(v, corners, sizeof(corners));
        u32 *t = ArrayAdd(&arena, square.indices, 9);
        u32 triangles[9] = { 0, 1, 2, 0, 2, 3, 0, 1, 7 };
        MemCopy(t, triangles, sizeof(triangles));
        P_CancelToken token = {0};
        Prog_ModelShape drawn;
        u32 *pixels = Prog__ModelShape(&arena, &square, &drawn) ? Prog_DrawMesh(&arena, &drawn, 64, &token) : NULL;
        Check(pixels && (pixels[32 * 64 + 32] >> 24) == 255 && (pixels[0] >> 24) == 0 && (pixels[63 * 64 + 63] >> 24) == 0);
        // Without colors of its own it is gray; a red part over white corners is red, lit but never more than its color.
        Check(pixels && (pixels[32 * 64 + 32] & 0xFF) > 0 && (pixels[32 * 64 + 32] & 0xFF) < 0xC8 && ((pixels[32 * 64 + 32] >> 16) & 0xFF) > (pixels[32 * 64 + 32] & 0xFF));
        Prog__MeshPart(&arena, &square, 0xFF0000FFu, UINT32_MAX, 0);
        MemCopy(ArrayAdd(&arena, square.indices, 3), triangles, 12);
        square.parts.v[0].first = 0;
        pixels = Prog__ModelShape(&arena, &square, &drawn) ? Prog_DrawMesh(&arena, &drawn, 64, &token) : NULL;
        Check(drawn.partCount == 1 && drawn.parts[0].count == 9 && pixels && (pixels[32 * 64 + 32] & 0xFF) > 0x40 && ((pixels[32 * 64 + 32] >> 8) & 0xFFFF) == 0);
        // A see-through blue square in front of half of it: half covered where it is alone, blue over red where they meet, whichever is
        // drawn first.
        Prog_Mesh layered = {0};
        f32 both[24] = { -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0, -2, -1, 1, 0, -1, 1, 0, 1, 1, -2, 1, 1 };
        MemCopy(ArrayAdd(&arena, layered.positions, 24), both, sizeof(both));
        u32 order[12] = { 4, 5, 6, 4, 6, 7, 0, 1, 2, 0, 2, 3 };
        MemCopy(ArrayAdd(&arena, layered.indices, 12), order, sizeof(order));
        Prog_MeshPart glass = { .color = 0x80FF0000u, .image = UINT32_MAX, .blend = true }, red = { .color = 0xFF0000FFu, .image = UINT32_MAX };
        layered.indices.header.count = 0;
        Prog__MeshLook(&arena, &layered, &glass);
        layered.indices.header.count = 6;
        Prog__MeshLook(&arena, &layered, &red);
        layered.indices.header.count = 12;
        pixels = Prog__ModelShape(&arena, &layered, &drawn) ? Prog_DrawMesh(&arena, &drawn, 64, &token) : NULL;
        u32 alone = 0, over = 0, solid = 0;
        for (i32 x = 0; pixels && x < 64; ++x) {
            u32 p = pixels[32 * 64 + x], a = p >> 24, r = p & 0xFF, b = (p >> 16) & 0xFF;
            alone += a > 100 && a < 160 && b > r;
            over += a == 255 && b > 0x20 && r > 0x20;
            solid += a == 255 && r > 0x40 && b == 0;
        }
        Check(pixels && alone && over && solid);
    }
    // A .blend saved compressed as one zstd frame, without a seek table, and the frame cut short.
    {
        String packed = S("\x28\xB5\x2F\xFD\x24\x2E\x25\x01\x00\xF0\x42\x4C\x45\x4E\x44\x45\x52\x2D\x76\x33\x30\x30\x20\x7A\x73\x74\x64\x20\x72\x6F\x75\x6E\x64\x20\x74\x72\x69\x70\x2C\x2E\x01\x00\x11\x32\x9D\x82\x38\xF9\xAE");
        Check(StrIsEqual(Prog__BlendInflate(&arena, packed), S("BLENDER-v300 zstd round trip, zstd round trip.")));
        Check(!Prog__BlendInflate(&arena, StrFromTo(packed, 0, 30)).length);
    }
    // A .blend's modifiers on its polygons: a cube's Catmull-Clark level makes a quad of each corner, its points pulled in toward a sphere,
    // its corners' colors taken between; a mirror doubles it across an axis, and an array copies it a step apart.
    {
        f32 cube[24] = { -1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1 };
        u32 sides[24] = { 0, 3, 2, 1, 4, 5, 6, 7, 0, 1, 5, 4, 1, 2, 6, 5, 2, 3, 7, 6, 3, 0, 4, 7 };
        u32 starts[7] = { 0, 4, 8, 12, 16, 20, 24 }, looks[6] = {0}, colors[24];
        for (u32 i = 0; i < 24; ++i) colors[i] = i < 4 ? 0xFF0000FFu : 0xFF00FF00u;
        Prog__BlendPolys polys = { .points = cube, .pointCount = 8, .faceStart = starts, .faceLook = looks, .faceCount = 6, .cornerPoint = sides,
                                   .cornerColor = colors, .cornerCount = 24 };
        Prog__BlendSubdivide(&arena, &polys);
        Check(polys.pointCount == 8 + 12 + 6 && polys.faceCount == 24 && polys.cornerCount == 96 && polys.faceStart[24] == 96);
        f32 corner = polys.points[0], side = polys.points[(8 + 12) * 3 + 2];
        Check(corner > -1 && corner < -0.5f && side == -1 && polys.cornerColor[0] == 0xFF0000FFu && polys.cornerColor[2] == 0xFF0000FFu);
        Prog__BlendMirror mirror = { .axis = 0 };
        Prog__BlendCopies(&arena, &polys, 2, Prog__BlendMirrorMove, &mirror);
        Check(polys.pointCount == 52 && polys.faceCount == 48 && polys.cornerPoint[96] == polys.cornerPoint[0] + 26 && polys.points[26 * 3] == -polys.points[0]);
        f32 step[3] = { 0, 3, 0 };
        Prog__BlendCopies(&arena, &polys, 3, Prog__BlendArrayMove, step);
        Check(polys.pointCount == 156 && polys.faceStart[polys.faceCount] == polys.cornerCount && polys.points[104 * 3 + 1] == polys.points[1] + 6);
    }
    // Solidify on an open square facing up: the surface where it was, a shell below it by the thickness, facing down, and a rim of four
    // quads round its open edges; a cap appended moved by its step, without colors of its own.
    {
        f32 square[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
        u32 starts[2] = { 0, 4 }, looks[1] = {0}, corners[4] = { 0, 1, 2, 3 }, colors[4] = { 0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu, 0xFF0000FFu };
        Prog__BlendPolys polys = { .points = square, .pointCount = 4, .faceStart = starts, .faceLook = looks, .faceCount = 1, .cornerPoint = corners,
                                   .cornerColor = colors, .cornerCount = 4 };
        Prog__BlendSolidify(&arena, &polys, 0.5f, -1, true);
        Check(polys.pointCount == 8 && polys.faceCount == 6 && polys.cornerCount == 24 && polys.points[2] == 0 && polys.points[4 * 3 + 2] == -0.5f);
        Check(polys.cornerPoint[4] == 7 && polys.cornerPoint[7] == 4 && polys.cornerColor[23] == 0xFF0000FFu);
        Prog__BlendPolys cap = { .points = square, .pointCount = 4, .faceStart = starts, .faceLook = looks, .faceCount = 1, .cornerPoint = corners, .cornerCount = 4 };
        f32 shift[3] = { 0, 0, 2 };
        Prog__BlendAppend(&arena, &polys, &cap, shift);
        Check(polys.pointCount == 12 && polys.faceCount == 7 && polys.cornerPoint[24] == 8 && polys.points[8 * 3 + 2] == 2 && polys.cornerColor[24] == Prog_ModelUnset);
        Check(Prog__AffineInverse(Prog__AffineCompose((f32[3]){ 1, 2, 3 }, (f32[4]){ 0, 0, 0, 1 }, (f32[3]){ 2, 2, 2 })).m[3] == -0.5f);
    }
    // Sorting by depth: floats of either sign in their order; the inspector's see-through parts from the farthest along the view, in the
    // places see-through parts had, and the solid one where it was.
    {
        f32 keys[5] = { 2.5f, -1, 0, -7.25f, 1e9f };
        u32 order[5] = { 0, 1, 2, 3, 4 };
        Prog_SortByKey(&arena, keys, order, 5);
        Check(order[0] == 3 && order[1] == 1 && order[2] == 2 && order[3] == 0 && order[4] == 4);
        Prog_State *state = ArenaPushStruct(&arena, Prog_State);
        R_MeshPart parts[3] = { { .first = 0, .count = 3, .blend = true }, { .first = 3, .count = 3 }, { .first = 6, .count = 3, .blend = true } };
        u32 indices[6] = { 0, 1, 2, 6, 7, 8 };
        f32 middles[6] = { 0, 0, 5, 0, 0, -5 };
        state->model = (Prog_ModelView){ .parts = parts, .partCount = 3, .seeIndices = indices, .seeMiddles = middles, .seeTriangles = 2 };
        f32 toward[4] = { 0, 0, 1, 0 };
        const R_MeshPart *drawn = Prog__ModelOrder(state, toward);
        Check(drawn[0].first == 6 && drawn[1].first == 3 && !drawn[1].blend && drawn[2].first == 0);
        ArenaRelease(&state->frameArena);
    }
    // The mesh for the GPU: a triangle naming a vertex that is not there is left out, Z up turns Y up, and its bounds' center and radius.
    {
        Prog_Mesh square = { .zUp = true };
        f32 *v = ArrayAdd(&arena, square.positions, 12);
        f32 corners[12] = { -1, 0, -1, 1, 0, -1, 1, 0, 1, -1, 0, 1 };
        MemCopy(v, corners, sizeof(corners));
        u32 *t = ArrayAdd(&arena, square.indices, 9);
        u32 triangles[9] = { 0, 1, 2, 0, 2, 3, 0, 1, 7 };
        MemCopy(t, triangles, sizeof(triangles));
        Prog_ModelShape shape;
        Check(Prog__ModelShape(&arena, &square, &shape) && shape.vertexCount == 4 && shape.indexCount == 6 && shape.indices[5] == 3);
        Check(shape.positions[1] == -1 && shape.positions[2] == 0 && shape.center[1] == 0 && fabsf(shape.radius - 1.4142136f) < 1e-5f);
        Check(Prog_ModelKindOf(S("C:/a/Robot.FBX")) == Prog_ModelKind_Fbx && Prog_ModelKindOf(S("/a/b.glb")) == Prog_ModelKind_Glb && !Prog_ModelKindOf(S("/a/b.txt")));
        Check(Prog_ModelKindOf(S("/a/Scene.blend")) == Prog_ModelKind_Blend && Prog_ModelKindOf(S("/a/Scene.blend12")) == Prog_ModelKind_Blend && !Prog_ModelKindOf(S("/a/b.obj2")));
        // LZ4: literals alone, and a match that overlaps what it copies, as USD files use for runs; a match before the start is broken.
        u8 lz4[8];
        Check(Prog__Lz4((const u8 *)"\x30" "abc", 4, lz4, sizeof(lz4)) == 3 && !MemCompare(lz4, "abc", 3));
        Check(Prog__Lz4((const u8 *)"\x10" "a\x01\x00\x00", 5, lz4, sizeof(lz4)) == 5 && !MemCompare(lz4, "aaaaa", 5));
        Check(Prog__Lz4((const u8 *)"\x10" "a\x05\x00", 4, lz4, sizeof(lz4)) == UINT64_MAX);
    }
    ArenaRelease(&arena);
}
