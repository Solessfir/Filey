// Headless self-checks. Built and run by `Build test`.

#include "Base/Base.h"
#include "Platform/Platform.h"
#include "Platform/Jobs.h"
#include "Platform/Files.h"
#include "Platform/Profile.h"
#include "Renderer/Renderer.h"
#include "UI/UI.h"
#include "Program/Program.h"

#if defined(_WIN32)
#include "Win/WinPlatform.c"
#include "Win/WinThreads.c"
#include "Win/WinFiles.c"
#elif defined(__linux__)
#include "Linux/LinuxPlatform.c"
#include "Linux/LinuxThreads.c"
#include "Linux/LinuxFiles.c"
#endif

#include "Base/Base.c"
#include "Platform/PlatformEvents.c"
#include "Platform/PlatformJobs.c"
#include "Platform/PlatformProfile.c"
#include "UI/UI.c"
#include "Program/Directory.c"
#include "Program/Commands.c"
#include "Program/Program.c"

global i32 CheckFailures;

#define Check(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s(%d): check failed: %s\n", __FILE__, __LINE__, #condition); \
            ++CheckFailures; \
        } \
    } while (0)

#include "Tests/UI.c"
#include "Tests/Browsing.c"
#include "Tests/Codegen.c"

global u32 TestModalFrameCount;

function void TestModalFrame(void *user, PlatformEventList events)
{
    Check(user == &TestModalFrameCount);
    ++TestModalFrameCount;
    Check(events.count == 1 && events.first->kind == PlatformEventKind_Resized);
    PlatformPushEvent(PlatformEventKind_MouseMove);
    Platform__RunModalFrame();
    Check(TestModalFrameCount == 1 && events.first->kind == PlatformEventKind_Resized);
}

function void TestModalFrames(void)
{
    PlatformSetModalFrameCallback(TestModalFrame, &TestModalFrameCount);
    PlatformPushEvent(PlatformEventKind_Resized);
    Platform__RunModalFrame();
    Check(TestModalFrameCount == 1 && !ModalFrameActive);
    PlatformEventList pending = PlatformTakeEvents();
    Check(pending.count == 1 && pending.first->kind == PlatformEventKind_MouseMove);
    PlatformSetModalFrameCallback(NULL, NULL);
    PlatformPushEvent(PlatformEventKind_CloseRequested);
    Platform__RunModalFrame();
    pending = PlatformTakeEvents();
    Check(TestModalFrameCount == 1 && pending.count == 1 && pending.first->kind == PlatformEventKind_CloseRequested);
    ArenaRelease(&EventQueue.arenas[0]);
    ArenaRelease(&EventQueue.arenas[1]);
    ZeroStruct(&EventQueue);
}

// -----------------------------------------------------------------------------

function void TestArena(void)
{
    Arena arena = {0};
    Check(ArenaPos(&arena) == 0);

    u8 *a = ArenaPush(&arena, 1);
    Check(a != NULL && a[0] == 0);
    u8 *b = _ArenaPush(&arena, 8, 64, 1);
    Check(((uintptr_t)b & 63) == 0);

    // Dirty memory must come back zeroed after a restore.
    ArenaMark mark = ArenaGetMark(&arena);
    u8 *dirty = ArenaPush(&arena, 128);
    MemCopy(dirty, "garbage", 7);
    ArenaRestore(mark);
    u8 *clean = ArenaPush(&arena, 128);
    Check(clean == dirty && clean[0] == 0);

    // A push bigger than a block chains; earlier pointers stay valid.
    MemCopy(a, "x", 1);
    u64 big = DefaultReserveSize + Kilobytes(1);
    ArenaMark beforeBig = ArenaGetMark(&arena);
    u8 *huge = ArenaPush(&arena, big);
    huge[big - 1] = 7;
    Check(arena.prev != NULL);
    Check(a[0] == 'x');
    u8 *afterBig = ArenaPush(&arena, 16);
    Check(afterBig != NULL);

    ArenaRestore(beforeBig);
    Check(arena.prev == NULL);
    Check(ArenaPos(&arena) == beforeBig.pos);

    ArenaClear(&arena);
    Check(ArenaPos(&arena) == 0 && arena.base != NULL);

    ArenaRelease(&arena);
    Check(arena.base == NULL && arena.reserved == 0);

    Check(ArenaPushArrayChecked(&arena, UINT64_MAX, 16, 8) == NULL);
    ArenaRelease(&arena);
}

typedef struct Popup {
    String text;
    Arena arena;
} Popup;

function void TestBootstrap(void)
{
    Popup *popup = ArenaBootstrapStruct(Popup, arena);
    popup->text = StrCopy(&popup->arena, S("Is this talk boring?"));
    Check(StrIsEqual(popup->text, S("Is this talk boring?")));

    // Force a chained block so release has to walk the chain.
    ArenaPush(&popup->arena, DefaultReserveSize);
    Check(popup->arena.prev != NULL);
    ArenaRelease(&popup->arena);
}

function void TestScratch(void)
{
    Scratch outer = GetScratch();
    String kept = StrCopy(outer.arena, S("kept"));

    Scratch inner = GetScratchEx(outer.arena);
    Check(inner.arena != outer.arena);
    StrCopy(inner.arena, S("temporary"));
    ReleaseScratch(inner);

    Check(StrIsEqual(kept, S("kept")));
    ReleaseScratch(outer);
}

typedef struct Node Node;
struct Node {
    Node *next;
    Node *prev;
    Node *freeNext;
    i32 value;
};

function void TestPool(void)
{
    Arena arena = {0};
    Node *pool = NULL;

    Node *nodes[PoolBatchCount + 1] = {0};
    for (u32 i = 0; i < CArrayCount(nodes); ++i) {
        nodes[i] = PoolAlloc(&arena, pool, Node);
        nodes[i]->value = (i32)i;
    }
    Check(nodes[1] > nodes[0]);

    Node *freed = nodes[3];
    PoolFree(pool, freed);
    Node *reused = PoolAlloc(&arena, pool, Node);
    Check(reused == freed && reused->value == 0);

    ArenaRelease(&arena);
}

function void TestLists(void)
{
    Node n[4] = {0};
    for (i32 i = 0; i < 4; ++i) {
        n[i].value = i;
    }

    Node *stack = NULL;
    StackPush(stack, &n[0]);
    StackPush(stack, &n[1]);
    Check(stack == &n[1]);
    StackPop(stack);
    Check(stack == &n[0]);
    StackPop(stack);
    Check(stack == NULL);
    StackPop(stack);

    Node *first = NULL;
    Node *last = NULL;
    QueuePush(first, last, &n[0]);
    QueuePush(first, last, &n[1]);
    Check(first == &n[0] && last == &n[1] && last->next == NULL);
    QueuePop(first, last);
    QueuePop(first, last);
    Check(first == NULL && last == NULL);

    // Same nodes in a second, independent list through another link.
    Node *freeList = NULL;
    StackPushN(freeList, &n[2], freeNext);
    Check(freeList == &n[2]);

    first = last = NULL;
    DLLPushBack(first, last, &n[1]);
    DLLPushFront(first, last, &n[0]);
    DLLInsertAfter(first, last, &n[1], &n[3]);
    DLLInsertBefore(first, last, &n[3], &n[2]);

    i32 expected = 0;
    for (Node *it = first; it != NULL; it = it->next) {
        Check(it->value == expected++);
    }
    Check(expected == 4 && first->prev == NULL && last->next == NULL);

    DLLRemove(first, last, &n[0]);
    DLLRemove(first, last, &n[3]);
    DLLRemove(first, last, &n[2]);
    Check(first == &n[1] && last == &n[1]);
    DLLRemove(first, last, &n[1]);
    Check(first == NULL && last == NULL);
}

function void TestStrings(void)
{
    Arena arena = {0};

    String empty = {0};
    Check(StrIsEqual(empty, S("")));
    Check(StrFrom(empty, 5).length == 0);
    Check(StrSplit(&arena, empty, '\\').header.count == 0);

    String s1 = S("Strings should be simple.");
    String s2 = StrFrom(s1, 8);
    Check(StrIsEqual(s2, S("should be simple.")));
    Check(StrIsEqual(StrFromTo(s2, 0, -8), S("should be")));
    Check(StrIsEqual(StrFromTo(S("abcdef"), 1, -2), S("bcd")));
    Check(StrIsEqual(StrFromTo(S("abcdef"), 1, 3), S("bc")));
    Check(StrFromTo(S("abc"), 2, 1).length == 0);

    String path = S("C:\\Users\\Vjekoslav");
    Index slash = StrIndexChar(path, '\\', SSF_SearchBackwards | SSF_IndexPlusOne | SSF_CountOnFail);
    Check(slash.exists && StrIsEqual(StrFrom(path, slash.value), S("Vjekoslav")));

    Index none = StrIndexChar(S("C:"), '\\', SSF_SearchBackwards | SSF_IndexPlusOne | SSF_CountOnFail);
    Check(!none.exists && StrFrom(S("C:"), none.value).length == 0);

    Check(StrIndexStr(S("a.tar.gz"), S(".gz"), SSF_None).value == 5);
    Check(StrIndexStr(S("a.tar.gz"), S("."), SSF_SearchBackwards).value == 5);
    Check(!StrIndexStr(S("ab"), S("abc"), SSF_None).exists);

    ArrayStr parts = StrSplit(&arena, S("\\C:\\\\Users\\"), '\\');
    Check(parts.header.count == 2);
    Check(StrIsEqual(parts.v[0], S("C:")) && StrIsEqual(parts.v[1], S("Users")));

    // "aé€😀" then an overlong '/', a lone continuation byte and a truncated sequence.
    String utf8 = S("a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xC0\xAF\x80\xE2\x82");
    rune expected[] = { 'a', 0xE9, 0x20AC, 0x1F600, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD, 0xFFFD };
    u64 pos = 0;
    for (u64 i = 0; i < CArrayCount(expected); ++i) {
        Check(StrDecodeUtf8(utf8, &pos) == expected[i]);
    }
    Check(pos == utf8.length);

    StrBuilder builder = {0};
    StrBuild(&arena, &builder, "Files: ");
    String number = StrBuild(&arena, &builder, (u64)42);
    StrBuildChar(&arena, &builder, '\n');
    Check(StrIsEqual(number, S("42")));
    Check(StrIsEqual(builder.buffer, S("Files: 42\n")));

    StrBuilder grow = {0};
    for (i32 i = 0; i < 1000; ++i) {
        StrBuild(&arena, &grow, S("ab"));
    }
    Check(grow.buffer.length == 2000 && grow.buffer.value[1999] == 'b');

    String formatted = StrFormat(&arena, "%S|%d|%llu|%5.2f|%%|%s|%x|%c",
                                 S("str"), -3, 18446744073709551615ULL, 3.14159, "c", 255u, 'z');
    Check(StrIsEqual(formatted, S("str|-3|18446744073709551615| 3.14|%|c|ff|z")));

    StrBuilder appended = {0};
    StrBuildF(&arena, &appended, "done");
    StrBuildF(&arena, &appended, " %S", S("twice"));
    Check(StrIsEqual(appended.buffer, S("done twice")));

    ArenaRelease(&arena);
}

typedef struct ArrayI32 {
    ArrayHeader header;
    i32 *v;
} ArrayI32;

function void TestArrays(void)
{
    Arena arena = {0};
    ArrayI32 a = {0};

    for (i32 i = 0; i < 1000; ++i) {
        ArrayPush(&arena, a, i);
    }
    Check(a.header.count == 1000 && a.v[999] == 999);

    ArrayInsert(&arena, a, 0, -1);
    Check(a.v[0] == -1 && a.v[1] == 0 && a.header.count == 1001);

    ArrayRemove(a, 0, 2);
    Check(a.v[0] == 1 && a.header.count == 999);

    ArrayClear(a);
    i32 *added = ArrayAdd(&arena, a, 4);
    added[0] = 5;
    ArrayClear(a);
    i32 *cleared = ArrayAddClear(&arena, a, 4);
    Check(cleared[0] == 0 && a.header.count == 4);

    ArrayI32 reserved = {0};
    ArrayReserve(&arena, reserved, 128);
    Check(reserved.header.capacity == 128 && reserved.header.count == 0);

    ArenaRelease(&arena);
}

typedef struct KVU64 {
    u64 key;
    u64 value;
} KVU64;

typedef struct MapU64 {
    MapHeader header;
    KVU64 *v;
} MapU64;

typedef struct KVStr {
    String key;
    i32 value;
} KVStr;

typedef struct MapStr {
    MapHeader header;
    KVStr *v;
} MapStr;

function void TestMaps(void)
{
    Arena arena = {0};

    MapU64 empty = {0};
    Check(!MapIndex(empty, 1).exists);
    Check(MapGet(empty, u64, 1) == 0);
    Check(!MapDel(empty, 1));

    MapU64 map = {0};
    for (u64 i = 0; i < 10000; ++i) {
        MapPut(&arena, map, i, i * 10);
    }
    Check(map.header.count == 10000);
    Check(MapGet(map, u64, 1234) == 12340);

    MapPut(&arena, map, 1234, 1);
    Check(map.header.count == 10000 && MapGet(map, u64, 1234) == 1);

    for (u64 i = 0; i < 10000; i += 2) {
        Check(MapDel(map, i));
    }
    Check(map.header.count == 5000);

    b32 allOdd = true;
    for (u64 i = 0; i < map.header.count; ++i) {
        allOdd &= (map.v[i].key & 1) == 1;
    }
    Check(allOdd);

    u64 out = 0;
    Check(MapTryGet(map, 4321, &out) && out == 43210);
    Check(!MapTryGet(map, 4320, &out));
    Check(MapGetPtrNull(map, 4320) == NULL);
    *MapGetPtrNull(map, 4321) = 7;
    Check(MapGet(map, u64, 4321) == 7);

    // Insert/delete churn must not grow the table without bound.
    MapU64 churn = {0};
    for (u64 i = 0; i < 100000; ++i) {
        MapPut(&arena, churn, i, i);
        MapDel(churn, i);
    }
    Check(churn.header.count == 0 && churn.header.tableSize <= 64);

    MapStr names = {0};
    MapsPut(&arena, names, S("Bob"), 1);
    MapsPut(&arena, names, S("Marta"), 2);
    Check(MapsGet(names, i32, S("Marta")) == 2);
    Index bob = MapsIndex(names, S("Bob"));
    Check(bob.exists && names.v[bob.value].value == 1);
    Check(MapsDel(names, S("Bob")) && !MapsIndex(names, S("Bob")).exists);
    Check(MapsGet(names, i32, S("Marta")) == 2);

    MapClear(names);
    Check(names.header.count == 0 && !MapsIndex(names, S("Marta")).exists);

    ArenaRelease(&arena);
}

function void TestProfiler(void)
{
    Check(P_ProfileInit(1));
    u64 capacity = P_Profile.capacity;
    Check(capacity >= 1);
    for (u64 i = 0; i < capacity; ++i) {
        P_ProfileRecord("Scope", 10, 25);
    }
    P_ProfileRecord("Dropped", 30, 40);
    Check(atomic_load(&P_Profile.count) == capacity + 1);
    Check(P_Profile.events[0].duration == 15 && P_Profile.events[0].thread == 1);

    FILE *file = tmpfile();
    Check(file != NULL);
    if (file != NULL) {
        Check(P_ProfileWriteTrace(file) == capacity);
        rewind(file);
        char text[512] = {0};
        u64 read = fread(text, 1, sizeof(text) - 1, file);
        fclose(file);
        String trace = { text, read };
        Index start = StrIndexStr(trace, S("{\"traceEvents\":["), 0);
        Check(start.exists && start.value == 0);
        Check(StrIndexStr(trace, S("\"name\":\"Scope\",\"ph\":\"X\",\"pid\":1,\"tid\":1,\"ts\":10,\"dur\":15"), 0).exists);
        Check(!StrIndexStr(trace, S("Dropped"), 0).exists);
    }
    P_ProfileRelease();
    P_ProfileRecord("AfterRelease", 0, 1);
    Check(P_Profile.events == NULL && atomic_load(&P_Profile.count) == 0);
}

int main(void)
{
    TestArena();
    TestBootstrap();
    TestScratch();
    TestPool();
    TestLists();
    TestStrings();
    TestArrays();
    TestMaps();
    TestModalFrames();
    TestUILayout();
    TestUINestedSizing();
    TestUIPropertiesAndLifetime();
    TestUIInteractions();
    TestUIClippingAndDrawOrder();
    TestUIScrolling();
    TestUIFrameFontLifetime();
    TestUICommandEventOrder();
    TestProgramCommands();
    TestCodegenParse();
    TestHotkeyDispatch();
    TestProgramVirtualRows();
    TestJobs();
    TestBrowsing();
    TestProfiler();

    if (CheckFailures != 0) {
        fprintf(stderr, "%d check(s) failed\n", CheckFailures);
        return 1;
    }
    printf("All headless tests passed\n");
    return 0;
}
