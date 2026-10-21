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

function void TestModels(void)
{
    Arena arena = {0};
    Prog_Mesh mesh = {0};

    // OBJ: a quad and a triangle given from the end, with texture and normal indices.
    String obj = S("# test\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nf 1/1/1 2/1/1 3/1/1 4/1/1\nf -4 -3 -1\n");
    Check(TestReadModel(&arena, Prog_ModelKind_Obj, obj, &mesh) && mesh.positions.header.count == 12 && mesh.indices.header.count == 9 && !mesh.zUp);
    Check(mesh.indices.v[3] == 0 && mesh.indices.v[4] == 2 && mesh.indices.v[5] == 3 && mesh.indices.v[6] == 0 && mesh.indices.v[8] == 3);

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
    }

    // PLY, as text with an extra property and element to step over, and in little-endian binary.
    String ply = S("ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\nproperty uchar red\n"
                   "element face 1\nproperty list uchar int vertex_indices\nelement edge 1\nproperty int a\nend_header\n"
                   "0 0 0 9\n1 0 0 9\n1 1 0 9\n0 1 0 9\n4 0 1 2 3\n7\n");
    Check(TestReadModel(&arena, Prog_ModelKind_Ply, ply, &mesh) && mesh.positions.header.count == 12 && mesh.indices.header.count == 6 && mesh.zUp && mesh.indices.v[5] == 3);
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
        u32 *pixels = Prog_DrawMesh(&arena, &square, 64, &token);
        Check(pixels && (pixels[32 * 64 + 32] >> 24) == 255 && (pixels[0] >> 24) == 0 && (pixels[63 * 64 + 63] >> 24) == 0);
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
