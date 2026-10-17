typedef struct TestFileFixture {
    Arena arena;
    String path;
    b32 created;
} TestFileFixture;

typedef struct TestFixtureEntry {
    String name;
    u32 size;
    b32 directory;
    b32 hidden;
} TestFixtureEntry;

global const TestFixtureEntry TestFixtureEntries[] = {
    {.name = SInit("Folder10"), .directory = true},
    {.name = SInit("Folder2"), .directory = true},
    {.name = SInit("Empty"), .directory = true},
    {.name = SInit("file10.txt"), .size = 10},
    {.name = SInit("file2.txt"), .size = 2},
    {.name = SInit("plain.txt"), .size = 1},
    {.name = SInit(".hidden"), .hidden = true},
    {.name = SInit("Zażółć-🙂.txt"), .size = 3},
    {.name = SInit("Folder2/inside.txt"), .size = 5},
};

#if defined(_WIN32)
#include "Win/WinTestFiles.c"
#elif defined(__linux__)
#include "Linux/LinuxTestFiles.c"
#endif

function P_File *TestFindFile(P_FileArray files, String name)
{
    for (u64 i = 0; i < files.header.count; ++i) {
        if (StrIsEqual(files.v[i].name, name)) return &files.v[i];
    }
    return NULL;
}

function void TestDirectoryFiles(TestFileFixture *fixture)
{
    Arena arena = {0};
    P_DirectoryResult result = P_EnumerateDirectory(&arena, fixture->path, NULL);
    Check(result.errorCode == 0 && !result.cancelled);
#if defined(_WIN32)
    Check(result.files.header.count == 8);
#elif defined(__linux__)
    Check(result.files.header.count == 10);
    P_File *broken = TestFindFile(result.files, S("broken-link"));
    Check(broken && broken->symbolicLink && !broken->metadataValid && broken->kind == P_FileKind_Other);
    Check(TestFindFile(result.files, S("invalid-\xff.txt")) != NULL);
#endif
    for (u64 i = 0; i + 1 < CArrayCount(TestFixtureEntries); ++i) {
        TestFixtureEntry entry = TestFixtureEntries[i];
        P_File *file = TestFindFile(result.files, entry.name);
        Check(file && file->metadataValid);
        if (file) {
            Check(file->kind == (entry.directory ? P_FileKind_Directory : P_FileKind_File));
            Check(file->hidden == entry.hidden);
            if (!entry.directory) Check(file->size == entry.size);
            Check(file->modifiedTimeUnix > 0 && file->accessedTimeUnix > 0);
        }
    }
    Check(!TestFindFile(result.files, S(".")) && !TestFindFile(result.files, S("..")));
    ArenaClear(&arena);
    String emptyPath = StrFormat(&arena, "%S/Empty", fixture->path);
    result = P_EnumerateDirectory(&arena, emptyPath, NULL);
    Check(!result.errorCode && !result.cancelled && result.files.header.count == 0);
    ArenaClear(&arena);
    String missing = StrFormat(&arena, "%S/missing", fixture->path);
    result = P_EnumerateDirectory(&arena, missing, NULL);
    Check(result.errorCode && result.errorString.length && result.files.header.count == 0);
    ArenaClear(&arena);
    String regular = StrFormat(&arena, "%S/plain.txt", fixture->path);
    result = P_EnumerateDirectory(&arena, regular, NULL);
    Check(result.errorCode && result.errorString.length && result.files.header.count == 0);
    ArenaClear(&arena);
    result = P_EnumerateDirectory(&arena, S("bad\0path"), NULL);
    Check(result.errorCode && result.files.header.count == 0);
    Check(P_AbsolutePath(&arena, S("bad\0path")).length == 0);
#if defined(_WIN32)
    result = P_EnumerateDirectory(&arena, S("invalid-\xff"), NULL);
    Check(result.errorCode && result.files.header.count == 0);
    Check(P_AbsolutePath(&arena, S("invalid-\xff")).length == 0);
#endif
    P_CancelToken cancel = {0};
    Check(!P_IsCancelled(&cancel));
    P_RequestCancel(&cancel);
    result = P_EnumerateDirectory(&arena, fixture->path, &cancel);
    Check(result.cancelled && !result.errorCode && result.files.header.count == 0 && !result.files.v);
    String relative = StrFormat(&arena, "%S/Folder2/../Empty/.", fixture->path);
    String absolute = P_AbsolutePath(&arena, relative);
    String expected = StrFormat(&arena, "%S/Empty", fixture->path);
    Check(StrIsEqual(absolute, expected) && !StrIndexChar(absolute, '\\', SSF_None).exists);
    Check(P_AbsolutePath(&arena, S(".")).length != 0);
    Check(P_FormatFileTime(&arena, 0).length >= 10);
    Check(P_FormatFileTime(&arena, INT64_MAX).length == 0);
    ArenaRelease(&arena);
}

typedef struct TestJobInput {
    String text;
    _Atomic u32 *started;
    atomic_bool *gate;
    u32 value;
} TestJobInput;

typedef struct TestJobResult {
    String text;
    u32 value;
    b32 cancelled;
    b32 timedOut;
} TestJobResult;

// Each lane adds its number plus one and uses its own scratch, which lanes on threads of their own give back as they end.
function void TestLane(void *user, u32 lane)
{
    Scratch scratch = GetScratch();
    StrFormat(scratch.arena, "lane %u", lane);
    ReleaseScratch(scratch);
    atomic_fetch_add((atomic_uint_fast64_t *)user, (u64)lane + 1);
}

function void TestLanes(void)
{
    atomic_uint_fast64_t sum = 0;
    P_RunLanes(4, TestLane, &sum);
    Check(atomic_load(&sum) == 10);
    // More than there can be run as many as there can.
    atomic_store(&sum, 0);
    P_RunLanes(P_MaxLanes + 4, TestLane, &sum);
    Check(atomic_load(&sum) == (u64)P_MaxLanes * (P_MaxLanes + 1) / 2);
}

function void TestJobCallback(P_Job *job)
{
    TestJobInput *input = job->input;
    if (input->started) atomic_fetch_add(input->started, 1);
    u64 start = PlatformNowMicroseconds();
    while (input->gate && !atomic_load(input->gate) && !P_IsCancelled(&job->cancelToken) && PlatformNowMicroseconds() - start < Millions(5)) {
        P__TestPause();
    }
    TestJobResult *result = ArenaPushStruct(&job->arena, TestJobResult);
    result->text = StrCopy(&job->arena, input->text);
    result->value = input->value;
    result->cancelled = P_IsCancelled(&job->cancelToken);
    result->timedOut = input->gate && !atomic_load(input->gate) && !result->cancelled;
    job->result = result;
}

function P_Job *TestCreateJob(P_JobQueue *queue, String text, u32 value, _Atomic u32 *started, atomic_bool *gate)
{
    P_Job *job = P_JobCreate(queue, TestJobCallback);
    TestJobInput *input = ArenaPushStruct(&job->arena, TestJobInput);
    *input = (TestJobInput){.text = StrCopy(&job->arena, text), .value = value, .started = started, .gate = gate};
    job->input = input;
    return job;
}

function b32 TestWaitStarted(_Atomic u32 *started, u32 target)
{
    u64 start = PlatformNowMicroseconds();
    while (atomic_load(started) < target && PlatformNowMicroseconds() - start < Millions(5)) P__TestPause();
    return atomic_load(started) >= target;
}

function P_Job *TestWaitJob(P_JobQueue *queue)
{
    u64 start = PlatformNowMicroseconds();
    P_Job *result = NULL;
    while (!(result = P_JobTakeCompleted(queue)) && PlatformNowMicroseconds() - start < Millions(5)) P__TestPause();
    return result;
}

function void TestJobs(void)
{
    Arena permanent = {0};
    P_JobQueue queue = {0};
    Check(P_JobsInit(&queue, &permanent, 1, NULL, NULL));
    if (!queue.initialized) {
        ArenaRelease(&permanent);
        return;
    }
    _Atomic u32 started = 0;
    atomic_bool gate = false;
    char source[] = "copied input";
    P_Job *job = TestCreateJob(&queue, StrFromCStr(source), 17, &started, &gate);
    P_JobSubmit(job);
    Check(TestWaitStarted(&started, 1));
    for (u64 i = 0; i + 1 < sizeof(source); ++i) source[i] = 'x';
    atomic_store(&gate, true);
    P_Job *completed = TestWaitJob(&queue);
    Check(completed == job && completed && completed->state == P_JobState_Owned);
    if (!completed) goto finish;
    if (completed) {
        TestJobResult *result = completed->result;
        Check(result && StrIsEqual(result->text, S("copied input")) && result->value == 17 && !result->timedOut);
        P_Job *other = TestCreateJob(&queue, S("other result"), 23, NULL, NULL);
        P_JobSubmit(other);
        P_Job *otherCompleted = TestWaitJob(&queue);
        Check(otherCompleted == other);
        if (otherCompleted) P_JobRelease(otherCompleted);
        Check(result && StrIsEqual(result->text, S("copied input")));
        P_JobRelease(completed);
        P_Job *reused = TestCreateJob(&queue, S("reused"), 0, NULL, NULL);
        Check(reused == completed && reused->state == P_JobState_Owned && !reused->result && !P_IsCancelled(&reused->cancelToken));
        P_JobRelease(reused);
    }
    for (u32 round = 0; round < 3; ++round) {
        for (u32 i = 0; i < 64; ++i) P_JobSubmit(TestCreateJob(&queue, S("stress"), i, NULL, NULL));
        u64 seen = 0;
        for (u32 i = 0; i < 64; ++i) {
            completed = TestWaitJob(&queue);
            Check(completed != NULL);
            if (!completed) goto finish;
            if (completed) {
                TestJobResult *result = completed->result;
                Check(result && result->value < 64 && StrIsEqual(result->text, S("stress")) && !result->cancelled);
                if (result && result->value < 64) {
                    u64 bit = UINT64_C(1) << result->value;
                    Check(!(seen & bit));
                    seen |= bit;
                }
                P_JobRelease(completed);
            }
        }
        Check(seen == UINT64_MAX && !P_JobTakeCompleted(&queue));
    }
    atomic_store(&started, 0);
    atomic_store(&gate, false);
    P_Job *blocker = TestCreateJob(&queue, S("active cancellation"), 0, &started, &gate);
    P_JobSubmit(blocker);
    Check(TestWaitStarted(&started, 1));
    for (u32 i = 0; i < 64; ++i) {
        job = TestCreateJob(&queue, S("queued cancellation"), i, &started, NULL);
        P_JobSubmit(job);
        P_RequestCancel(&job->cancelToken);
    }
    P_RequestCancel(&blocker->cancelToken);
    for (u32 i = 0; i < 65; ++i) {
        completed = TestWaitJob(&queue);
        Check(completed && P_IsCancelled(&completed->cancelToken));
        if (!completed) goto finish;
        if (completed) {
            if (completed == blocker) {
                TestJobResult *result = completed->result;
                Check(result && result->cancelled && !result->timedOut);
            } else {
                Check(!completed->result);
            }
            P_JobRelease(completed);
        }
    }
    Check(atomic_load(&started) == 1);
    atomic_store(&started, 0);
    blocker = TestCreateJob(&queue, S("shutdown cancellation"), 0, &started, &gate);
    P_JobSubmit(blocker);
    Check(TestWaitStarted(&started, 1));
    for (u32 i = 0; i < 64; ++i) P_JobSubmit(TestCreateJob(&queue, S("shutdown queued"), i, &started, NULL));
    P_Job *unsubmitted = TestCreateJob(&queue, S("shutdown owned"), 0, NULL, NULL);
    u64 start = PlatformNowMicroseconds();
    P_JobsShutdown(&queue);
    Check(!queue.initialized && atomic_load(&started) == 1 && PlatformNowMicroseconds() - start < Millions(5));
    Check(ArenaPos(&blocker->arena) == 0 && ArenaPos(&unsubmitted->arena) == 0);
finish:
    P_JobsShutdown(&queue);
    ArenaRelease(&permanent);
}

function b32 TestEndsWith(String text, String suffix)
{
    return text.length >= suffix.length && StrIsEqual(StrFrom(text, text.length - suffix.length), suffix);
}

function u64 TestEntryIndex(Prog_State *state, String name)
{
    for (u64 i = 0; i < state->directory.count; ++i) {
        if (StrIsEqual(state->directory.entries[i].file.name, name)) return i;
    }
    return UINT64_MAX;
}

function u64 TestSelectionCount(Prog_State *state)
{
    u64 result = 0;
    for (u64 i = 0; i < state->directory.count; ++i) result += (u64)state->directory.entries[i].selected;
    Check(result == state->selectedCount);
    return result;
}

function b32 TestWaitDirectory(Prog_State *state)
{
    u64 start = PlatformNowMicroseconds();
    while (state->directoryJob && PlatformNowMicroseconds() - start < Millions(5)) {
        Prog_TakeCompletions(state);
        if (state->directoryJob) P__TestPause();
    }
    return !state->directoryJob;
}

function void TestBrowsingCommand(Prog_State *state, Prog_CommandKind kind, u64 row)
{
    ArenaClear(&state->frameArena);
    Prog_QueueCommand(state, kind, row, 1, 0);
    Check(Prog_ProcessCommands(state));
    Check(TestWaitDirectory(state));
}

// One box selection over view positions [from, to) of a one-item-per-line list with 10-pixel rows; an empty range draws no box.
function void TestMarquee(Prog_State *state, KeyMods mods, u64 from, u64 to, u32 down)
{
    ArenaClear(&state->frameArena);
    state->gridColumns = 1;
    state->gridPitch = (UI_Vec2){ 100, 10 };
    state->gridItem = (R_Rect){ 0, 0, 100, 10 };
    state->marqueeBox = from < to ? (R_Rect){ 40, (f32)from * 10 + 1, 60, (f32)to * 10 - 1 } : (R_Rect){0};
    Prog_QueueCommand(state, Prog_CommandKind_MarqueeStart, 0, 0, 0)->mods = mods;
    Prog_QueueCommand(state, Prog_CommandKind_MarqueeSelect, 0, down, 0);
    Prog_ProcessCommands(state);
}

typedef struct TestStaleJobInput {
    Prog_JobInput program;
    _Atomic u32 *started;
    atomic_bool *gate;
} TestStaleJobInput;

function void TestStaleJobCallback(P_Job *job)
{
    TestStaleJobInput *input = job->input;
    atomic_fetch_add(input->started, 1);
    u64 start = PlatformNowMicroseconds();
    while (!atomic_load(input->gate) && !P_IsCancelled(&job->cancelToken) && PlatformNowMicroseconds() - start < Millions(5)) P__TestPause();
    Prog_Directory *result = ArenaPushStruct(&job->arena, Prog_Directory);
    result->path = StrCopy(&job->arena, S("stale directory"));
    job->result = result;
}

function void TestStaleCompletion(Prog_State *state, TestFileFixture *fixture)
{
    _Atomic u32 started = 0;
    atomic_bool gate = false;
    P_Job *stale = P_JobCreate(&state->jobs, TestStaleJobCallback);
    TestStaleJobInput *input = ArenaPushStruct(&stale->arena, TestStaleJobInput);
    input->program.generation = state->generation;
    input->started = &started;
    input->gate = &gate;
    stale->input = input;
    P_JobSubmit(stale);
    Check(TestWaitStarted(&started, 1));
    String empty = Prog_JoinPath(&state->frameArena, fixture->path, S("Empty"));
    Prog_RequestDirectory(state, empty, true, 0);
    Check(TestWaitDirectory(state));
    Check(StrIsEqual(state->directory.path, empty) && !state->directory.count && !state->error.length);
    atomic_store(&gate, true);
    u64 start = PlatformNowMicroseconds();
    b32 ready = false;
    while (!ready && PlatformNowMicroseconds() - start < Millions(5)) {
        P__MutexLock(&state->jobs.mutex);
        ready = state->jobs.completedFirst != NULL;
        P__MutexUnlock(&state->jobs.mutex);
        if (!ready) P__TestPause();
    }
    Check(ready);
    Prog_TakeCompletions(state);
    Check(StrIsEqual(state->directory.path, empty) && !state->directory.count && !state->error.length);
    if (!ready) P_JobsShutdown(&state->jobs);
}

function void TestProgramBrowsing(TestFileFixture *fixture)
{
    Check(StrIsEqual(Prog_ParentPath(S("/")), S("/")));
    Check(StrIsEqual(Prog_ParentPath(S("/alpha/beta/")), S("/alpha")));
    Check(StrIsEqual(Prog_ParentPath(S("C:/")), S("C:/")));
    Check(StrIsEqual(Prog_ParentPath(S("C:/alpha")), S("C:/")));
    Check(StrIsEqual(Prog_ParentPath(S("//host/share/")), S("//host/share/")));
    Check(StrIsEqual(Prog_ParentPath(S("//host/share/folder")), S("//host/share/")));
    Check(StrIsEqual(Prog_ParentPath(S("//?/C:/")), S("//?/C:/")));
    Check(StrIsEqual(Prog_ParentPath(S("//?/UNC/host/share/folder")), S("//?/UNC/host/share/")));
    Check(StrIsEqual(Prog_ParentPath(S("//?/uNc/host/share/folder")), S("//?/uNc/host/share/")));
    Check(!Prog_ParentPath((String){0}).length);
    Check(Prog_CompareNames(S("file2.txt"), S("file10.txt")) < 0);
    Check(Prog_CompareNames(S("file10.txt"), S("file2.txt")) > 0);
    Check(Prog_CompareNames(S("file9"), S("file1844674407370955161600")) < 0);
    Check(Prog_CompareNames(S("file1000000000000000000001"), S("file1000000000000000000002")) < 0);
    Check(Prog_CompareNames(S("same"), S("same")) == 0);
    Check(Prog_CompareNames(S("Item2"), S("item10")) < 0);
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (!state.jobs.initialized) return;
    Check(TestWaitDirectory(&state));
    Check(StrIsEqual(state.directory.path, fixture->path) && !state.error.length);
    Check(state.history.header.count == 1 && state.historyAt == 0);
    Check(TestEntryIndex(&state, S(".hidden")) == UINT64_MAX);
    Check(state.directory.count >= 7);
    if (state.directory.count < 7) goto finish;
    Check(StrIsEqual(state.directory.entries[0].file.name, S("Empty")));
    Check(StrIsEqual(state.directory.entries[1].file.name, S("Folder2")));
    Check(StrIsEqual(state.directory.entries[2].file.name, S("Folder10")));
    Check(TestEntryIndex(&state, S("file2.txt")) < TestEntryIndex(&state, S("file10.txt")));
    Prog_SelectRow(&state, 0, 0);
    Prog_SelectRow(&state, 2, KeyMod_Shift);
    Check(TestSelectionCount(&state) == 3 && state.anchorRow == 0 && state.focusedRow == 2);
    u64 last = state.directory.count - 1;
    Prog_SelectRow(&state, last, KeyMod_Control);
    Check(TestSelectionCount(&state) == 4 && state.directory.entries[last].selected);
    Prog_SelectRow(&state, 0, KeyMod_Control);
    Check(TestSelectionCount(&state) == 3 && !state.directory.entries[0].selected);
    Prog_SelectRow(&state, 1, KeyMod_Control | KeyMod_Shift);
    Check(TestSelectionCount(&state) == 4 && state.directory.entries[0].selected && state.anchorRow == 0 && state.focusedRow == 1);
    Prog_SelectRow(&state, 0, KeyMod_Control);
    Prog_SelectRow(&state, UINT64_MAX, 0);
    Check(TestSelectionCount(&state) == 3 && state.focusedRow == 0);
    // A box replaces the selection with the rows it covers; with Ctrl it toggles them against the earlier selection, with Shift it adds them.
    TestMarquee(&state, 0, 1, 3, true);
    Check(TestSelectionCount(&state) == 2 && state.directory.entries[state.view[1]].selected && state.directory.entries[state.view[2]].selected);
    Check(state.focusedRow == state.view[2] && state.anchorRow == state.view[1]);
    TestMarquee(&state, KeyMod_Control, 0, 2, false);
    Check(TestSelectionCount(&state) == 2 && state.directory.entries[state.view[0]].selected && state.directory.entries[state.view[2]].selected);
    Check(state.focusedRow == state.view[0] && state.anchorRow == state.view[1]);
    TestMarquee(&state, KeyMod_Shift, 3, 4, true);
    Check(TestSelectionCount(&state) == 3 && state.directory.entries[state.view[3]].selected);
    TestMarquee(&state, 0, 0, 0, true);
    Check(TestSelectionCount(&state) == 0);
    // In a grid only the items whose box the selection box touches: here the second and third of the first two lines of three.
    state.gridColumns = 3;
    state.gridPitch = (UI_Vec2){ 10, 10 };
    state.gridItem = (R_Rect){ 2, 0, 8, 8 };
    state.marqueeBox = (R_Rect){ 15, 5, 25, 12 };
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_MarqueeStart, 0, 0, 0);
    Prog_QueueCommand(&state, Prog_CommandKind_MarqueeSelect, 0, 1, 0);
    Prog_ProcessCommands(&state);
    Check(TestSelectionCount(&state) == 4 && state.directory.entries[state.view[1]].selected && state.directory.entries[state.view[2]].selected);
    Check(state.directory.entries[state.view[4]].selected && state.directory.entries[state.view[5]].selected && !state.directory.entries[state.view[3]].selected);
    state.gridColumns = 1;
    Prog_SelectRow(&state, TestEntryIndex(&state, S("file2.txt")), 0);
    Prog_SelectRow(&state, TestEntryIndex(&state, S("plain.txt")), KeyMod_Control);
    for (u64 sort = 0; sort < Prog_Sort_Count; ++sort) {
        for (u32 direction = 0; direction < 2; ++direction) {
            TestBrowsingCommand(&state, Prog_CommandKind_Sort, sort);
            Check(TestSelectionCount(&state) == 2);
            u64 file2 = TestEntryIndex(&state, S("file2.txt")), plain = TestEntryIndex(&state, S("plain.txt"));
            Check(file2 != UINT64_MAX && plain != UINT64_MAX);
            if (file2 != UINT64_MAX && plain != UINT64_MAX) {
                Check(state.directory.entries[file2].selected && state.directory.entries[plain].selected);
                Check(state.hasFocus && state.focusedRow == plain && state.hasAnchor && state.anchorRow == plain);
            }
            for (u64 i = 0; i < state.directory.count; ++i) Check((state.directory.entries[i].file.kind == P_FileKind_Directory) == (i < 3));
            if (sort == Prog_Sort_Name) Check((TestEntryIndex(&state, S("file2.txt")) < TestEntryIndex(&state, S("file10.txt"))) == !state.descending);
            if (sort == Prog_Sort_Size) {
                u64 previous = 0;
                b32 hasPrevious = false;
                for (u64 i = 0; i < state.directory.count; ++i) {
                    P_File file = state.directory.entries[i].file;
                    if (file.kind != P_FileKind_File) continue;
                    if (hasPrevious) Check(state.descending ? previous >= file.size : previous <= file.size);
                    previous = file.size;
                    hasPrevious = true;
                }
            }
        }
    }
    // The zoom popup sets a level, and leaves one out of range alone.
    {
        u32 before = state.zoom;
        TestBrowsingCommand(&state, Prog_CommandKind_ZoomSet, Prog_ZoomLarge);
        Check(state.zoom == Prog_ZoomLarge);
        TestBrowsingCommand(&state, Prog_CommandKind_ZoomSet, Prog_ZoomMax + 1);
        Check(state.zoom == Prog_ZoomLarge);
        // Steps of 5.5% through the icon sizes, a view at a time below them, and old files' levels read as zoom.
        TestBrowsingCommand(&state, Prog_CommandKind_ZoomOut, 0);
        Check(state.zoom == 595 && Prog_ZoomStepped(1000, true) == 1000 && Prog_ZoomStepped(330, false) == 300 && Prog_ZoomStepped(300, false) == 200);
        Check(Prog_ZoomStepped(200, true) == 300 && Prog_ZoomFromSaved(2) == 200 && Prog_ZoomFromSaved(10) == 685 && Prog_ZoomFromSaved(16) == 1000 && Prog_ZoomFromSaved(650) == 650);
        TestBrowsingCommand(&state, Prog_CommandKind_ZoomSet, before);
    }
    // A folder keeps its view: it opens with it and follows what changes in it, while a folder without one gets the panel's own back.
    {
        Arena kept = {0};
        String here = StrCopy(&kept, state.directory.path);
        u32 ownZoom = state.zoom;
        Prog_Sort ownSort = state.sort;
        b32 ownDescending = state.descending;
        Prog_ViewSettings view = {0};
        TestBrowsingCommand(&state, Prog_CommandKind_FolderViewToggle, 0);
        Check(Prog_FolderView(&state, here, &view) && view.zoom == ownZoom && view.sort == ownSort && view.descending == ownDescending);
        TestBrowsingCommand(&state, Prog_CommandKind_ZoomIn, 0);
        TestBrowsingCommand(&state, Prog_CommandKind_Sort, ownSort == Prog_Sort_Size ? Prog_Sort_Name : Prog_Sort_Size);
        Check(TestWaitDirectory(&state));
        Prog_Sort changed = state.sort;
        // Its columns too.
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnToggle, Prog_ColumnKind_Created);
        Check(Prog_FolderView(&state, here, &view) && view.zoom == Prog_ZoomStepped(ownZoom, true) && view.sort == changed && view.columns.count == 6);
        Prog_RequestDirectory(&state, Prog_JoinPath(&kept, here, S("Empty")), true, 0);
        Check(TestWaitDirectory(&state) && state.zoom == ownZoom && state.sort == ownSort && state.descending == ownDescending && state.columnSet.count == 0);
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, here) && state.zoom == Prog_ZoomStepped(ownZoom, true) && state.sort == changed && state.columnSet.count == 6);
        TestBrowsingCommand(&state, Prog_CommandKind_FolderViewToggle, 0);
        Check(TestWaitDirectory(&state) && !Prog_FolderView(&state, here, &view));
        Check(state.zoom == ownZoom && state.sort == ownSort && state.descending == ownDescending && state.columnSet.count == 0);
        // The columns read back as written; a line from before the columns keeps the defaults.
        Prog_ColumnSet written = { .count = 3, .kinds = { Prog_ColumnKind_Name, Prog_ColumnKind_Size, Prog_ColumnKind_Created }, .shares = { 0, 0.125f, 0 } }, read = {0};
        Check(Prog_ParseColumns(Prog_ColumnsText(&kept, written), &read) && read.count == 3 && read.kinds[2] == Prog_ColumnKind_Created && read.shares[1] == 0.125f);
        Check(!Prog_ParseColumns(S("size,type"), &read) && !Prog_ParseColumns(S("name,bogus"), &read));
        String old = S("2 3 1 C:/Old Folder");
        Prog__SetList(&state.folderViewArena, &state.folderViews, &old, 1);
        Check(Prog_FolderView(&state, S("C:/Old Folder"), &view) && view.zoom == Prog_ZoomColumns && view.columns.count == 0);
        Prog__SetList(&state.folderViewArena, &state.folderViews, NULL, 0);
        ArenaRelease(&kept);
        // Going away and back left nothing selected.
        Prog_SelectRow(&state, TestEntryIndex(&state, S("plain.txt")), 0);
    }
    // This PC lists the drives with their own columns; Up from a drive's root leads there and goes no further.
    {
        Arena kept = {0};
        String here = StrCopy(&kept, state.directory.path), trail = StrCopy(&kept, Prog_TrailText(&state));
        Prog_RequestDirectory(&state, Prog_ThisPC, true, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, Prog_ThisPC) && state.directory.count >= 1);
        Check(state.directory.count && state.directory.entries[0].drive && Prog_PanelColumns(&state).kinds[5] == Prog_ColumnKind_PercentFull);
        // A root among the drives, which on Linux need not be the first mount.
        b32 root = false;
        for (u64 i = 0; i < state.directory.count; ++i) root |= StrIsEqual(Prog_UpPath(state.directory.entries[i].file.name), Prog_ThisPC);
        Check(!Prog_UpPath(Prog_ThisPC).length && root);
        Check(StrIsEqual(Prog_JoinPath(&kept, Prog_ThisPC, state.directory.entries[0].file.name), state.directory.entries[0].file.name));
        // The trail of visited folders stays through This PC and a parent, and goes on past the parent's crumbs.
        Check(StrIsEqual(Prog_TrailText(&state), trail));
        Prog_RequestDirectory(&state, Prog_UpPath(here), true, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(Prog_TrailText(&state), trail));
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, Prog_ThisPC));
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, here) && StrIsEqual(Prog_TrailText(&state), trail));
        Prog_SelectRow(&state, TestEntryIndex(&state, S("plain.txt")), 0);
        ArenaRelease(&kept);
    }
    // A start set to the root shows This PC.
    {
        state.startup = Prog_Startup_Root;
        Prog_StartUp(&state);
        state.startup = Prog_Startup_LastSession;
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, Prog_ThisPC));
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, fixture->path));
    }
    // A recently visited folder is marked, and sorted before the rest when asked.
    {
        state.showRecents = state.sortRecents = true;
        Prog_RequestDirectory(&state, Prog_JoinPath(&state.frameArena, fixture->path, S("Folder2")), true, 0);
        Check(TestWaitDirectory(&state));
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, fixture->path) && state.directory.count);
        // Every recent before the rest.
        b32 ordered = true;
        for (u64 i = 1; i < state.directory.count; ++i) ordered &= state.directory.entries[i - 1].recent || !state.directory.entries[i].recent;
        Check(ordered && state.directory.entries[TestEntryIndex(&state, S("Folder2"))].recent && !state.directory.entries[TestEntryIndex(&state, S("plain.txt"))].recent);
        state.showRecents = state.sortRecents = false;
        Prog_Reload(&state);
        Check(TestWaitDirectory(&state) && !state.directory.entries[TestEntryIndex(&state, S("Folder2"))].recent);
        Prog_SelectRow(&state, TestEntryIndex(&state, S("plain.txt")), 0);
    }
    // Folders go first, then last, then in among the files, and first again.
    {
        TestBrowsingCommand(&state, Prog_CommandKind_CycleFolderSort, 0);
        Check(TestWaitDirectory(&state) && state.folderSort == Prog_FolderSort_Last && state.directory.entries[0].file.kind != P_FileKind_Directory);
        Check(state.directory.entries[state.directory.count - 1].file.kind == P_FileKind_Directory);
        TestBrowsingCommand(&state, Prog_CommandKind_CycleFolderSort, 0);
        Check(TestWaitDirectory(&state) && state.folderSort == Prog_FolderSort_Mixed);
        TestBrowsingCommand(&state, Prog_CommandKind_CycleFolderSort, 0);
        Check(TestWaitDirectory(&state) && state.folderSort == Prog_FolderSort_First && state.directory.entries[0].file.kind == P_FileKind_Directory);
        Prog_SelectRow(&state, TestEntryIndex(&state, S("plain.txt")), 0);
    }
    // The header's menu shows and hides columns, moves them, resizes them and puts the defaults back; Name stays.
    {
        Prog_ColumnSet set = Prog_PanelColumns(&state);
        Check(set.count == 5 && set.kinds[0] == Prog_ColumnKind_Name && set.kinds[3] == Prog_ColumnKind_Items);
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnToggle, Prog_ColumnKind_Created);
        set = Prog_PanelColumns(&state);
        Check(set.count == 6 && set.kinds[5] == Prog_ColumnKind_Created);
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnShiftLeft, 5);
        set = Prog_PanelColumns(&state);
        Check(set.kinds[4] == Prog_ColumnKind_Created && set.kinds[5] == Prog_ColumnKind_Size);
        ArenaClear(&state.frameArena);
        Prog_QueueCommand(&state, Prog_CommandKind_ColumnResize, 4, 0, 0)->target = 250000;
        Prog_ProcessCommands(&state);
        Check(Prog_PanelColumns(&state).shares[4] == 0.25f);
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnClose, 0);
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnClose, 1);
        set = Prog_PanelColumns(&state);
        Check(set.count == 5 && set.kinds[0] == Prog_ColumnKind_Name && set.kinds[1] == Prog_ColumnKind_Modified);
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnToggle, Prog_ColumnKind_Created);
        Check(Prog_PanelColumns(&state).count == 4);
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnsRestore, 0);
        Check(state.columnSet.count == 0 && Prog_PanelColumns(&state).count == 5);
    }
    Check(P__TestWriteFile(fixture, S("refresh.txt"), 4, false));
    Check(P__TestRemovePath(fixture, S("file2.txt"), false));
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
    Check(TestEntryIndex(&state, S("refresh.txt")) != UINT64_MAX && TestEntryIndex(&state, S("file2.txt")) == UINT64_MAX);
    Check(TestSelectionCount(&state) == 1 && state.hasFocus && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("plain.txt")));
    Check(P__TestWriteFile(fixture, S("file2.txt"), 2, false));
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleHidden, 0);
    Check(state.showHidden && TestEntryIndex(&state, S(".hidden")) != UINT64_MAX && TestSelectionCount(&state) == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleHidden, 0);
    Check(!state.showHidden && TestEntryIndex(&state, S(".hidden")) == UINT64_MAX);
    u64 folder = TestEntryIndex(&state, S("Folder2"));
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, folder, 2, 0);
    Check(Prog_ProcessCommands(&state) && state.loading);
    Check(TestWaitDirectory(&state));
    Check(state.directory.count == 1 && StrIsEqual(state.directory.entries[0].file.name, S("inside.txt")));
    // A new folder has its cursor on the first item, which selects nothing.
    Check(state.hasFocus && state.focusedRow == 0 && !state.hasAnchor && !TestSelectionCount(&state));
    Check(state.history.header.count == 2 && state.historyAt == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
    Check(StrIsEqual(state.directory.path, fixture->path) && state.historyAt == 0);
    TestBrowsingCommand(&state, Prog_CommandKind_Forward, 0);
    Check(state.directory.count == 1 && state.historyAt == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_Up, 0);
    Check(StrIsEqual(state.directory.path, fixture->path) && state.history.header.count == 3 && state.historyAt == 2);
    // Going up selects the folder it came from.
    Check(state.hasFocus && state.focusedRow == folder && state.directory.entries[folder].selected && TestSelectionCount(&state) == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
    String empty = Prog_JoinPath(&state.frameArena, fixture->path, S("Empty"));
    Prog_RequestDirectory(&state, empty, true, 0);
    Check(TestWaitDirectory(&state));
    Check(!state.directory.count && StrIsEqual(state.directory.path, empty) && state.historyAt == 2 && state.history.header.count == 3);
    u64 generation = state.generation;
    TestBrowsingCommand(&state, Prog_CommandKind_Forward, 0);
    Check(state.generation == generation && state.historyAt == 2);
    String missing = Prog_JoinPath(&state.frameArena, fixture->path, S("missing"));
    Prog_RequestDirectory(&state, missing, true, 0);
    Check(TestWaitDirectory(&state));
    Check(state.error.length && StrIsEqual(state.directory.path, state.history.v[2]) && state.history.header.count == 3 && state.historyAt == 2);
    Arena source = {0};
    String copiedPath = Prog_JoinPath(&source, fixture->path, S("Folder2"));
    Prog_RequestDirectory(&state, copiedPath, true, 0);
    MemZero((void *)copiedPath.value, copiedPath.length);
    ArenaRelease(&source);
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
    Check(!state.error.length && state.directory.count == 1 && StrIsEqual(state.directory.entries[0].file.name, S("inside.txt")));
    Check(state.history.header.count == 4 && state.historyAt == 3 && StrIsEqual(state.history.v[3], state.directory.path));
    Prog_SelectRow(&state, 0, 0);
    missing = Prog_JoinPath(&state.frameArena, fixture->path, S("missing"));
    Prog_RequestDirectory(&state, missing, true, 0);
    Check(TestWaitDirectory(&state));
    Check(state.error.length && state.directory.count == 1 && state.directory.entries[0].selected && state.hasFocus && state.hasAnchor);
    Check(state.history.header.count == 4 && state.historyAt == 3 && StrIsEqual(state.history.v[3], state.directory.path));
    TestStaleCompletion(&state, fixture);
finish:
    Prog_Release(&state);
    Check(!state.jobs.initialized && !state.directory.path.length && ArenaPos(&state.directoryArena) == 0);
}

function void TestFileIcons(TestFileFixture *fixture)
{
    Arena arena = {0};
    Check(StrIsEqual(P_FileIconKey(&arena, S("/a/b/Readme.TXT"), P_FileKind_File), S(".txt")));
    Check(StrIsEqual(P_FileIconKey(&arena, S("/a/b/LICENSE"), P_FileKind_File), S("/file")));
    Check(StrIsEqual(P_FileIconKey(&arena, S("/a/b/dir.d"), P_FileKind_Directory), S("/folder")));
#if defined(_WIN32)
    Check(StrIsEqual(P_FileIconKey(&arena, S("C:/a/.gitignore"), P_FileKind_File), S(".gitignore")));
    Check(StrIsEqual(P_FileIconKey(&arena, S("C:/a/Tool.EXE"), P_FileKind_File), S("C:/a/Tool.EXE")));
    Check(StrIsEqual(P_FileTypeKey(&arena, S("C:/a/Tool.EXE"), P_FileKind_File), S(".exe")));
#else
    Check(StrIsEqual(P_FileIconKey(&arena, S("/a/.profile"), P_FileKind_File), S("/file")));
#endif

    // 2x2 to 1x1: alpha-weighted color, mean alpha.
    u32 quad[4] = { 0xFF0000FFu, 0xFF0000FFu, 0x00FF0000u, 0x00FF0000u };
    u32 *one = Prog_ResizeIcon(&arena, (P_Image){ quad, 2, 2 }, 1);
    Check(one[0] == 0x800000FFu);
    // 2x1 into 4x4: fitted and centered, transparent around it.
    u32 wide[2] = { 0xFF00FF00u, 0xFF00FF00u };
    u32 *fit = Prog_ResizeIcon(&arena, (P_Image){ wide, 2, 1 }, 4);
    Check(fit[0] == 0 && fit[4] == 0xFF00FF00u && fit[11] == 0xFF00FF00u && fit[12] == 0);

    // System-language names, so only their presence is checked.
    Check(P_FileTypeName(&arena, S("plain.txt"), P_FileKind_File).length);
    Check(P_FileTypeName(&arena, S("Folder2"), P_FileKind_Directory).length);
    Check(P_FileTypeName(&arena, S("LICENSE"), P_FileKind_File).length);

    P_Image image = {0};
    String file = Prog_JoinPath(&arena, fixture->path, S("plain.txt"));
    b32 loaded = P_LoadFileIcon(&arena, file, P_FileKind_File, 16, 1, false, &image);
#if defined(_WIN32)
    Check(loaded);
#endif
    Check(!loaded || (image.width > 0 && image.height > 0 && image.pixels != NULL));
    loaded = P_LoadFileIcon(&arena, fixture->path, P_FileKind_Directory, 16, 1, true, &image);
    Check(!loaded || (image.width > 0 && image.height > 0));
    // A picture is its own thumbnail.
    local_persist const char pixel[] = "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\x0d\x49\x44\x41\x54\x78\xda\x63\xfc\xcf\xc0\x50\x0f\x00\x04\x85\x01\x80\x84\xa9\x8c\x21\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
    String picture = Prog_JoinPath(&arena, fixture->path, S("pixel.png"));
    Check(!P_WriteFile(&arena, picture, StrMake(pixel, sizeof(pixel) - 1)).code);
    image = (P_Image){0};
    Check(P_LoadFileThumbnail(&arena, picture, P_FileKind_File, 64, false, NULL, &image) && image.pixels != NULL);
#if defined(__linux__)
    Check(image.width == 1 && image.height == 1 && StrIsEqual(P_FileThumbnailKey(&arena, picture, P_FileKind_File), picture));
    // A 16 at 125% is looked up as a 16 at scale 2: the theme's 16@2 directory, then by pixels, where a scalable 32 and up beats a 22.
    P__IconDir doubled = { .size = 16, .scale = 2, .type = P__IconDirType_Fixed }, small = { .size = 16, .scale = 1, .type = P__IconDirType_Fixed };
    P__IconDir mono = { .size = 22, .scale = 1, .type = P__IconDirType_Fixed };
    P__IconDir scalable = { .size = 64, .minSize = 32, .maxSize = 512, .scale = 1, .type = P__IconDirType_Scalable };
    Check(P__IconDirDistance(&doubled, 16, 2) == 0 && P__IconDirDistance(&small, 16, 1) == 0 && P__IconDirDistance(&small, 16, 2) == 16);
    Check(P__IconDirDistance(&scalable, 16, 2) < P__IconDirDistance(&mono, 16, 2) && P__IconDirDistance(&mono, 22, 1) == 0);
    // An SVG that only wraps a PNG draws the PNG, split over lines as Inkscape writes it; one that draws nothing fails.
    String wrapped = Prog_JoinPath(&arena, fixture->path, S("wrapped.svg"));
    Check(!P_WriteFile(&arena, wrapped, S("<svg width=\"16\" height=\"16\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">"
                                         "<path d=\"M30 2h4v4h-4z\"/><image width=\"16\" height=\"16\" xlink:href=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB\n"
                                         "CAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\"/></svg>")).code);
    image = (P_Image){0};
    Check(P__DecodeIcon(&arena, wrapped, 16, &image) && image.width == 1 && image.height == 1 && (image.pixels[0] >> 24) != 0);
    Check(!P_WriteFile(&arena, wrapped, S("<svg width=\"16\" height=\"16\" xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M30 2h4v4h-4z\"/></svg>")).code);
    Check(!P__DecodeIcon(&arena, wrapped, 16, &(P_Image){0}));
    Check(P__TestRemovePath(fixture, S("wrapped.svg"), false));
    // The computer and the user's folders have their own icons, as GTK names them; the home and other folders are plain folders.
    ArrayStr placeIcons = {0};
    P__PlaceIconNames(&arena, P_ComputerIconPath(), &placeIcons);
    Check(placeIcons.header.count == 1 && StrIsEqual(placeIcons.v[0], S("computer")));
    placeIcons = (ArrayStr){0};
    P__PlaceIconNames(&arena, StrFromCStr(getenv("HOME")), &placeIcons);
    P__PlaceIconNames(&arena, fixture->path, &placeIcons);
    Check(placeIcons.header.count == 0);
    // A drive with Windows on it is marked with Windows' logo in the corner, and each drive keeps its own icon so that only it is.
    {
        u32 pixels[20 * 20] = {0};
        P__BadgeWindows(&(P_Image){ pixels, 20, 20 });
        Check(pixels[0] == 0xFFD47800u && pixels[19 * 20 + 19] == 0 && pixels[10 * 20 + 10] == 0);
        // The root drive's penguin covers the top left corner and leaves the rest.
        u32 root[20 * 20] = {0};
        P__BadgeTux(&arena, &(P_Image){ root, 20, 20 });
        Check((root[6 * 20 + 6] >> 24) != 0 && root[19 * 20 + 19] == 0);
        Check(!StrIsEqual(P_FileIconKey(&arena, S("/"), P_FileKind_Drive), P_FileIconKey(&arena, S("/mnt/c"), P_FileKind_Drive)));
    }
    // A gray symbol gives way to color for a place; a little tint is still gray.
    u32 grays[2] = { 0xFF808080u, 0x80848080u }, tinted[2] = { 0xFF808080u, 0xFFD07020u };
    Check(P__IsGray(&(P_Image){ grays, 2, 1 }) && !P__IsGray(&(P_Image){ tinted, 2, 1 }));
    String computer = P_ComputerIconPath();
    Check(!P_ContextMenuOpen(&arena, (String){0}, &computer, 1, &(P_ContextMenu){0}));
#endif
    // Without a thumbnail of its own, a file gets none rather than its icon when only a thumbnail will do.
    String plain = Prog_JoinPath(&arena, fixture->path, S("plain.bin"));
    Check(!P_WriteFile(&arena, plain, S("\x01\x02")).code);
    image = (P_Image){0};
    Check(!P_LoadFileThumbnail(&arena, plain, P_FileKind_File, 64, true, NULL, &image));
    // Its bytes then show as hex rows: offset, hex in two groups of eight, characters; fewer to a row where the view is narrow.
    String dump = Prog_HexRowText(&arena, (const u8 *)"AB\x00", 3, 0, 16, Prog_HexPlaces(3));
    Check(StrIsEqual(StrFromTo(dump, 0, 18), S("0000  41 42 00    ")) && TestEndsWith(dump, S("    AB.")) && dump.length == 4 + 2 + 49 + 1 + 3);
    String sixteen = Prog_HexRowText(&arena, (const u8 *)"0123456789abcdef", 16, 0x10, 16, Prog_HexPlaces(0x10000));
    Check(StrIsEqual(sixteen, S("0010  30 31 32 33 34 35 36 37  38 39 61 62 63 64 65 66  0123456789abcdef")) && Prog_HexPlaces(0x10001) == 5);
    Check(Prog_HexPerLine(4, 72) == 16 && Prog_HexPerLine(4, 71) == 8 && Prog_HexPerLine(4, 10) == 4);
    // A code file's lines are colored by what each byte is, and a comment left open goes on into the next line.
    u8 tokens[64];
    Check(Prog_SyntaxLine(Prog_Syntax_C, S("int x = 0x1F; // done"), 0, tokens) == 0);
    Check(tokens[0] == Prog_Token_Keyword && tokens[4] == Prog_Token_Plain && tokens[8] == Prog_Token_Number && tokens[14] == Prog_Token_Comment && tokens[20] == Prog_Token_Comment);
    Check(Prog_SyntaxLine(Prog_Syntax_C, S("a /* b"), 0, NULL) != 0);
    Check(Prog_SyntaxLine(Prog_Syntax_C, S("c */ \"s\" 'x'"), Prog_SyntaxLine(Prog_Syntax_C, S("a /* b"), 0, NULL), tokens) == 0);
    Check(tokens[0] == Prog_Token_Comment && tokens[3] == Prog_Token_Comment && tokens[4] == Prog_Token_Plain && tokens[5] == Prog_Token_String && tokens[9] == Prog_Token_String);
    Check(Prog_SyntaxLine(Prog_Syntax_Rust, S("fn f<'a>(x: &'a str)"), 0, tokens) == 0 && tokens[0] == Prog_Token_Keyword && tokens[6] == Prog_Token_Plain);
    Check(Prog_SyntaxLine(Prog_Syntax_Json, S("{ \"key\": \"value\", \"n\": 12 }"), 0, tokens) == 0 && tokens[2] == Prog_Token_Keyword && tokens[10] == Prog_Token_String);
    Check(Prog_SyntaxLine(Prog_Syntax_Python, S("x = '''doc"), 0, NULL) != 0 && Prog_SyntaxLine(Prog_Syntax_Markdown, S("# Title"), 0, tokens) == 0 && tokens[2] == Prog_Token_Heading);
    Check(Prog_SyntaxForPath(S("C:/x/Thing.CPP")) == Prog_Syntax_C && Prog_SyntaxForPath(S("/a/b.py")) == Prog_Syntax_Python && Prog_SyntaxForPath(S("/a/notes.txt")) == Prog_Syntax_None);
    // Batch files: keywords in any case, variables and arguments, quoted strings, and REM, :: and label lines.
    Check(Prog_SyntaxForPath(S("C:/x/Build.BAT")) == Prog_Syntax_Batch && Prog_SyntaxForPath(S("C:/x/run.cmd")) == Prog_Syntax_Batch && Prog_SyntaxForPath(S("/a/b.sh")) == Prog_Syntax_Shell);
    Check(Prog_SyntaxLine(Prog_Syntax_Batch, S("IF exist \"%ROOT%\" set X=%~dp0 & echo %%a 50%"), 0, tokens) == 0);
    Check(tokens[0] == Prog_Token_Keyword && tokens[3] == Prog_Token_Keyword && tokens[9] == Prog_Token_String && tokens[16] == Prog_Token_String && tokens[18] == Prog_Token_Keyword);
    Check(tokens[23] == Prog_Token_Plain && tokens[24] == Prog_Token_Number && tokens[28] == Prog_Token_Number && tokens[29] == Prog_Token_Plain && tokens[32] == Prog_Token_Keyword);
    Check(tokens[37] == Prog_Token_Number && tokens[39] == Prog_Token_Number && tokens[40] == Prog_Token_Plain && tokens[43] == Prog_Token_Plain);
    Check(Prog_SyntaxLine(Prog_Syntax_Batch, S("@rem off"), 0, tokens) == 0 && tokens[5] == Prog_Token_Comment);
    Check(Prog_SyntaxLine(Prog_Syntax_Batch, S(":: note"), 0, tokens) == 0 && tokens[3] == Prog_Token_Comment);
    Check(Prog_SyntaxLine(Prog_Syntax_Batch, S(":loop"), 0, tokens) == 0 && tokens[1] == Prog_Token_Heading);
    Check(Prog_SyntaxLine(Prog_Syntax_Batch, S("for %%A in (%*) do"), 0, tokens) == 0 && tokens[12] == Prog_Token_Number && tokens[13] == Prog_Token_Number && tokens[14] == Prog_Token_Plain);
    Check(Prog_SyntaxLine(Prog_Syntax_Batch, S("remove.exe"), 0, tokens) == 0 && tokens[0] == Prog_Token_Plain);
    // A repository's branch comes from its HEAD file; the counts from git, here a repository without an upstream.
    Check(StrIsEqual(Prog_GitBranch(S("ref: refs/heads/main\n")), S("main")) && StrIsEqual(Prog_GitBranch(S("0123456789abcdef\n")), S("0123456")));
    String made = {0};
    Check(!P_CreateItem(&arena, fixture->path, S(".git"), true, &made).code);
    Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, Prog_JoinPath(&arena, fixture->path, S(".git")), S("HEAD")), S("ref: refs/heads/topic\n")).code);
    Prog_GitStatus git = Prog_FindGitStatus(&arena, Prog_JoinPath(&arena, fixture->path, S("Folder2")));
    Check(StrIsEqual(git.branch, S("topic")) && git.ahead == -1 && git.behind == -1);
    Check(P__TestRemovePath(fixture, S(".git/HEAD"), false) && P__TestRemovePath(fixture, S(".git"), true));
    String part = {0};
    Check(P_ReadFileAt(&arena, plain, 1, 8, &part) && StrIsEqual(part, S("\x02")));
    // An archive lists its folders and files with their paths inside it; one made by Python's zipfile, a folder "docs" holding "a.txt".
    local_persist const char zipped[] = "\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x9a\x0d\x50\x5d\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x64\x6f\x63\x73\x2f\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x9a\x0d\x50\x5d\xac\x2a\x93\xd8\x02\x00\x00\x00\x02\x00\x00\x00\x0a\x00\x00\x00\x64\x6f\x63\x73\x2f\x61\x2e\x74\x78\x74\x68\x69\x50\x4b\x01\x02\x14\x00\x14\x00\x00\x00\x00\x00\x9a\x0d\x50\x5d\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x10\x00\xfd\x41\x00\x00\x00\x00\x64\x6f\x63\x73\x2f\x50\x4b\x01\x02\x14\x00\x14\x00\x00\x00\x00\x00\x9a\x0d\x50\x5d\xac\x2a\x93\xd8\x02\x00\x00\x00\x02\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01\x23\x00\x00\x00\x64\x6f\x63\x73\x2f\x61\x2e\x74\x78\x74\x50\x4b\x05\x06\x00\x00\x00\x00\x02\x00\x02\x00\x6b\x00\x00\x00\x4d\x00\x00\x00\x00\x00";
    String archive = Prog_JoinPath(&arena, fixture->path, S("pack.zip"));
    Check(!P_WriteFile(&arena, archive, StrMake(zipped, sizeof(zipped) - 1)).code);
    P_FileArray items = {0};
    Check(P_ListArchive(&arena, archive, 100, NULL, &items));
    b32 sawFolder = false, sawFile = false;
    for (u64 i = 0; i < items.header.count; ++i) {
        sawFolder |= items.v[i].kind == P_FileKind_Directory && StrIsEqual(items.v[i].name, S("docs"));
        sawFile |= items.v[i].kind == P_FileKind_File && StrIsEqual(items.v[i].name, S("docs/a.txt")) && items.v[i].size == 2;
    }
    Check(sawFolder && sawFile);
    String listing = Prog_ArchiveListing(&arena, &items, false);
    Check(StrIsEqual(StrFromTo(listing, 0, 23), S("1 folder, 1 file, 2 B\n\n")) && TestEndsWith(listing, S("       2 B  docs/a.txt\n")));
    // The inspector sums it up over its list; folders only in the paths of files count too.
    P_DirectoryTotals totals = Prog_ArchiveTotals(&items);
    Check(totals.folders == 1 && totals.files == 1 && totals.bytes == 2 && totals.complete);
    Check(StrIsEqual(Prog_ArchiveSummary(&arena, totals, 1), S("1 folder, 1 file, 2 B, packed to 1 B (50% smaller)")));
    P_FileArray implied = {0};
    ArrayPush(&arena, implied, ((P_File){ .name = S("a/b/c.txt"), .size = 10, .kind = P_FileKind_File }));
    ArrayPush(&arena, implied, ((P_File){ .name = S("a/d.txt"), .size = 10, .kind = P_FileKind_File }));
    totals = Prog_ArchiveTotals(&implied);
    Check(totals.folders == 2 && totals.files == 2 && totals.bytes == 20);
    Check(StrIsEqual(Prog_ArchiveSummary(&arena, totals, 30), S("2 folders, 2 files, 20 B, packed to 30 B (no smaller)")));
    totals.complete = false;
    Check(StrIsEqual(Prog_ArchiveSummary(&arena, totals, 30), S("Over 4 items, packed to 30 B")));
    Check(!P_ListArchive(&arena, plain, 100, NULL, &items) && !items.header.count);
    // The archive opens as a folder: a level of it at a time, files taken out of it, a copy to open or preview, and files added to it.
    {
        P_DirectoryResult top = P_EnumerateArchive(&arena, archive, (String){0}, NULL), docs = P_EnumerateArchive(&arena, archive, S("docs"), NULL);
        Check(!top.errorCode && top.files.header.count == 1 && StrIsEqual(top.files.v[0].name, S("docs")) && top.files.v[0].kind == P_FileKind_Directory);
        Check(!docs.errorCode && docs.files.header.count == 1 && StrIsEqual(docs.files.v[0].name, S("a.txt")) && docs.files.v[0].size == 2);
        String within = {0}, inside = {0}, folder = Prog_JoinPath(&arena, archive, S("docs"));
        Check(Prog_ArchiveSplit(folder, &within, &inside) && StrIsEqual(within, archive) && StrIsEqual(inside, S("docs")));
        Check(Prog_InArchive(archive, &within, &inside) && !inside.length && !Prog_ArchiveSplit(fixture->path, &within, &inside));
        String out = {0}, name = S("a.txt"), contents = {0};
        Check(!P_CreateItem(&arena, fixture->path, S("Out"), true, &out).code);
        out = Prog_JoinPath(&arena, fixture->path, S("Out"));
        Check(!P_ExtractArchive(&arena, archive, S("docs"), &name, 1, out, NULL, NULL).code);
        Check(P_ReadFile(&arena, Prog_JoinPath(&arena, out, S("a.txt")), &contents) && StrIsEqual(contents, S("hi")));
        String copy = Prog_ArchiveCopy(&arena, Prog_JoinPath(&arena, folder, S("a.txt")), NULL, NULL);
        Check(copy.length && P_ReadFile(&arena, copy, &contents) && StrIsEqual(contents, S("hi")));
        String note = Prog_JoinPath(&arena, out, S("note.txt"));
        Check(!P_WriteFile(&arena, note, S("new")).code);
        Check(!P_AddToArchive(&arena, archive, S("docs"), &note, 1, NULL, NULL).code);
        docs = P_EnumerateArchive(&arena, archive, S("docs"), NULL);
        Check(docs.files.header.count == 2 && P_EnumerateArchive(&arena, archive, (String){0}, NULL).files.header.count == 1);
        // The program lists the folder from the archive, and a transfer out of it takes the file out.
        Prog_State listed = {0};
        Check(Prog_Init(&listed, folder, NULL, NULL) && listed.jobs.initialized);
        Check(TestWaitDirectory(&listed) && listed.directory.archive && listed.directory.count == 2 && !listed.directory.error.code);
        Prog_Release(&listed);
        // A name the system's code page has no letters for goes in as it is.
        String unicode = Prog_JoinPath(&arena, out, S("ünï 日本.txt"));
        Check(!P_WriteFile(&arena, unicode, S("u")).code && !P_AddToArchive(&arena, archive, (String){0}, &unicode, 1, NULL, NULL).code);
        top = P_EnumerateArchive(&arena, archive, (String){0}, NULL);
        Check(top.files.header.count == 2 && (StrIsEqual(top.files.v[0].name, S("ünï 日本.txt")) || StrIsEqual(top.files.v[1].name, S("ünï 日本.txt"))));
        // An add counts its progress, and a cancelled one leaves the archive as it was, with nothing beside it.
        P_Progress counted = {0};
        String more = Prog_JoinPath(&arena, out, S("more.txt"));
        Check(!P_WriteFile(&arena, more, S("more")).code && !P_AddToArchive(&arena, archive, (String){0}, &more, 1, &counted, NULL).code);
        Check(atomic_load(&counted.doneItems) == 1 && atomic_load(&counted.totalBytes) > 4 && atomic_load(&counted.doneBytes) >= 4);
        P_CancelToken stop = {0};
        P_RequestCancel(&stop);
        Check(P_AddToArchive(&arena, archive, (String){0}, &note, 1, NULL, &stop).code && P_EnumerateArchive(&arena, archive, (String){0}, NULL).files.header.count == 3);
        Check(!P_PathExists(Prog_JoinPath(&arena, fixture->path, S(".pack.zip.part"))));
        String moved = {0}, from = Prog_JoinPath(&arena, folder, S("note.txt"));
        Check(!P_CreateItem(&arena, fixture->path, S("Out2"), true, &moved).code);
        Prog_JobInput transfer = { .kind = Prog_JobKind_Transfer, .paths = &from, .pathCount = 1, .path = Prog_JoinPath(&arena, fixture->path, S("Out2")) };
        ArrayStr placed = {0};
        Check(!Prog__Transfer(&arena, &transfer, &placed, NULL).code);
        Check(P_ReadFile(&arena, Prog_JoinPath(&arena, transfer.path, S("note.txt")), &contents) && StrIsEqual(contents, S("new")));
        Check(P__TestRemovePath(fixture, S("Out/a.txt"), false) && P__TestRemovePath(fixture, S("Out/note.txt"), false) && P__TestRemovePath(fixture, S("Out/ünï 日本.txt"), false) && P__TestRemovePath(fixture, S("Out/more.txt"), false) &&
              P__TestRemovePath(fixture, S("Out"), true));
        Check(P__TestRemovePath(fixture, S("Out2/note.txt"), false) && P__TestRemovePath(fixture, S("Out2"), true));
        // A tar.gz rewritten to add a file stays a gzipped tar, as tar itself reads it back.
        String output = {0}, tarred = Prog_JoinPath(&arena, fixture->path, S("box.tar.gz"));
        const String make[] = { S("tar"), S("-czf"), S("box.tar.gz"), S("inside-tar.txt") };
        Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, fixture->path, S("inside-tar.txt")), S("t")).code);
        Check(P_RunCommand(&arena, fixture->path, make, CArrayCount(make), 10000, &output));
        String added = Prog_JoinPath(&arena, fixture->path, S("pixel.png"));
        Check(!P_AddToArchive(&arena, tarred, (String){0}, &added, 1, NULL, NULL).code);
        const String list[] = { S("tar"), S("-tzf"), S("box.tar.gz") };
        Check(P_RunCommand(&arena, fixture->path, list, CArrayCount(list), 10000, &output) && StrIndexStr(output, S("inside-tar.txt"), SSF_None).exists &&
              StrIndexStr(output, S("pixel.png"), SSF_None).exists);
        Check(P_ArchiveWritable(tarred) && P_ArchiveWritable(S("a.7z")) && !P_ArchiveWritable(S("a.rar")) && !P_ArchiveWritable(S("a.cab")));
#if defined(_WIN32)
        // Windows' tar makes a 7z too, which then takes a file as well.
        String sevened = Prog_JoinPath(&arena, fixture->path, S("box.7z"));
        const String pack[] = { S("tar"), S("-a"), S("-cf"), S("box.7z"), S("inside-tar.txt") };
        Check(P_RunCommand(&arena, fixture->path, pack, CArrayCount(pack), 10000, &output));
        Check(!P_AddToArchive(&arena, sevened, (String){0}, &added, 1, NULL, NULL).code && P_EnumerateArchive(&arena, sevened, (String){0}, NULL).files.header.count == 2);
        Check(P__TestRemovePath(fixture, S("box.7z"), false));
#endif
        Check(P__TestRemovePath(fixture, S("box.tar.gz"), false) && P__TestRemovePath(fixture, S("inside-tar.txt"), false));
    }
    Check(P__TestRemovePath(fixture, S("pack.zip"), false));
    Check(P__TestRemovePath(fixture, S("pixel.png"), false));
    Check(P__TestRemovePath(fixture, S("plain.bin"), false));

    // Menus are only built and read here, never invoked.
    String names[] = { S("plain.txt") };
    P_ContextMenu menu = {0};
    b32 opened = P_ContextMenuOpen(&arena, fixture->path, names, 1, &menu);
    Check(opened && menu.rootCount && menu.rootCount <= menu.count);
    b32 hasOpen = false;
    for (u32 i = 0; opened && i < menu.count; ++i) {
        const P_MenuItem *item = &menu.items[i];
        Check(!item->childCount || item->firstChild + item->childCount <= menu.count);
        hasOpen |= StrIsEqual(item->verb, S("open"));
    }
    Check(hasOpen);
    P_ContextMenuClose(&menu);
    Check(menu.items == NULL && menu.native == NULL);
#if defined(_WIN32)
    opened = P_ContextMenuOpen(&arena, fixture->path, NULL, 0, &menu);
    Check(opened && menu.rootCount);
    P_ContextMenuClose(&menu);
    // A drive named by its path gets its own menu, with Properties, instead of its root's background.
    String drive = P_TrashFolder(&arena);
    drive.length = 3;
    opened = P_ContextMenuOpen(&arena, (String){0}, &drive, 1, &menu);
    b32 hasProperties = false;
    for (u32 i = 0; opened && i < menu.count; ++i) hasProperties |= StrIsEqual(menu.items[i].verb, S("properties"));
    Check(opened && hasProperties);
    P_ContextMenuClose(&menu);
#else
    // The trash's own menu only opens it, as the Recycle Bin's does; the program adds emptying it.
    String trash = P_TrashFolder(&arena);
    opened = P_ContextMenuOpen(&arena, (String){0}, &trash, 1, &menu);
    Check(opened && menu.count == 1 && StrIsEqual(menu.items[0].verb, S("open")));
    P_ContextMenuClose(&menu);
    // What is in it is only deleted for good, and its background offers nothing to paste or make; the program adds putting things back.
    opened = P_ContextMenuOpen(&arena, StrFormat(&arena, "%S/files", trash), names, 1, &menu);
    Check(opened && menu.count == 1 && menu.items[0].action == P_MenuAction_Delete);
    P_ContextMenuClose(&menu);
    opened = P_ContextMenuOpen(&arena, trash, NULL, 0, &menu);
    Check(opened && menu.count == 0);
    P_ContextMenuClose(&menu);
#endif
    ArenaRelease(&arena);
}

function void TestRenameInput(Prog_State *state, PlatformEvent *events, u64 count)
{
    for (u64 i = 0; i + 1 < count; ++i) events[i].next = &events[i + 1];
    ArenaClear(&state->frameArena);
    Prog_HandleInput(state, (PlatformEventList){ count ? events : NULL, count ? &events[count - 1] : NULL, count });
    Prog_ProcessCommands(state);
}

function void TestRenameType(Prog_State *state, String text)
{
    PlatformEvent events[64] = {0};
    u64 count = 0;
    for (u64 at = 0; at < text.length && count < CArrayCount(events);) {
        events[count].kind = PlatformEventKind_Text;
        events[count++].codepoint = StrDecodeUtf8(text, &at);
    }
    TestRenameInput(state, events, count);
}

function void TestRenameKey(Prog_State *state, Key key, KeyMods mods)
{
    PlatformEvent event = { .kind = PlatformEventKind_KeyDown, .key = key, .mods = mods };
    TestRenameInput(state, &event, 1);
}

function void TestRename(TestFileFixture *fixture)
{
    Arena arena = {0};
    Check(P_RenamePath(&arena, fixture->path, S("plain.txt"), S("file2.txt")).code);
    Check(P_RenamePath(&arena, fixture->path, S("plain.txt"), S("Folder2/plain.txt")).code);
    Check(P_RenamePath(&arena, fixture->path, S("plain.txt"), S("..")).code);
    Check(P_RenamePath(&arena, fixture->path, S("plain.txt"), (String){0}).code);
    Check(!P_RenamePath(&arena, fixture->path, S("plain.txt"), S("plain2.txt")).code);
    Check(!P_RenamePath(&arena, fixture->path, S("plain2.txt"), S("plain.txt")).code);

    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (!state.jobs.initialized) goto finish;
    Check(TestWaitDirectory(&state));
    u64 plain = TestEntryIndex(&state, S("plain.txt"));
    Check(plain != UINT64_MAX);
    if (plain == UINT64_MAX) goto finish;

    // Files start with the name before the extension selected; Escape keeps the old name.
    Prog_BeginRename(&state, plain);
    Check(state.renaming && state.rename.anchor == 0 && state.rename.cursor == 5);
    TestRenameType(&state, S("ż"));
    Check(StrIsEqual(Prog_EditText(&state.rename), S("ż.txt")) && state.rename.cursor == 2);
    TestRenameKey(&state, Key_Backspace, 0);
    TestRenameKey(&state, Key_End, KeyMod_Shift);
    Check(StrIsEqual(Prog_EditText(&state.rename), S(".txt")) && state.rename.anchor == 0 && state.rename.cursor == 4);
    TestRenameKey(&state, Key_Left, 0);
    TestRenameKey(&state, Key_Right, 0);
    TestRenameKey(&state, Key_Left, KeyMod_Shift);
    TestRenameKey(&state, Key_Delete, 0);
    Check(StrIsEqual(Prog_EditText(&state.rename), S("txt")));
    TestRenameKey(&state, Key_Escape, 0);
    Check(!state.renaming && TestEntryIndex(&state, S("plain.txt")) != UINT64_MAX);

    // Enter renames on a job, then the new name is selected.
    Prog_BeginRename(&state, TestEntryIndex(&state, S("plain.txt")));
    TestRenameKey(&state, Key_A, KeyMod_Control);
    TestRenameType(&state, S("moved.txt"));
    TestRenameKey(&state, Key_Enter, 0);
    Check(!state.renaming);
    u64 start = PlatformNowMicroseconds();
    while (!state.directoryJob && PlatformNowMicroseconds() - start < Millions(5)) {
        Prog_TakeCompletions(&state);
        if (!state.directoryJob) P__TestPause();
    }
    Check(TestWaitDirectory(&state) && !state.error.length);
    u64 moved = TestEntryIndex(&state, S("moved.txt"));
    Check(moved != UINT64_MAX && TestEntryIndex(&state, S("plain.txt")) == UINT64_MAX);
    Check(moved != UINT64_MAX && state.directory.entries[moved].selected && state.selectedCount == 1 && state.focusedRow == moved);
    Check(!P_RenamePath(&arena, fixture->path, S("moved.txt"), S("plain.txt")).code);
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);

    // An incremental ID, over the selected part, is the smallest number no other item's name has; Tab leaves a name unchanged and goes on to the next item.
    plain = TestEntryIndex(&state, S("plain.txt"));
    Prog_BeginRename(&state, plain);
    TestBrowsingCommand(&state, Prog_CommandKind_RenameInsertIncrementalID, 0);
    Check(state.renaming && StrIsEqual(Prog_EditText(&state.rename), S("1.txt")));
    TestRenameKey(&state, Key_Escape, 0);
    Prog_BeginRename(&state, plain);
    u64 position = Prog_ViewPosition(&state, plain);
    TestRenameKey(&state, Key_Tab, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
    String next = position + 1 < state.viewCount ? state.directory.entries[state.view[position + 1]].file.name : (String){0};
    Check(next.length ? state.renaming && StrIsEqual(StrMake((const char *)state.renameFrom, state.renameFromLength), next) : !state.renaming);
    TestRenameKey(&state, Key_Escape, 0);

    // The one item a shell command adds goes straight into rename.
    Check(P__TestWriteFile(fixture, S("created.txt"), 1, false));
    state.renameCreatedUntil = PlatformNowMicroseconds() + Millions(5);
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
    Check(state.renaming && StrIsEqual(Prog_EditText(&state.rename), S("created.txt")) && state.rename.cursor == 7);
    Check(state.hasFocus && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("created.txt")));
    TestRenameKey(&state, Key_Escape, 0);
    Check(!state.renaming && !state.renameCreatedUntil);
    {
        Prog_TextEdit edit = {0};
        u8 wide[2000];
        for (u32 i = 0; i < sizeof(wide); i += 2) { wide[i] = 0xC5; wide[i + 1] = 0xBC; }
        PlatformClipboardSetText(StrMake((const char *)wide, sizeof(wide)));
        Prog__EditPaste(&edit);
        Check(edit.length == 1022 && edit.text[edit.length - 2] == 0xC5);
    }
    Check(P__TestRemovePath(fixture, S("created.txt"), false));
finish:
    Prog_Release(&state);
    ArenaRelease(&arena);
}

// Every row the view shows matches the filter, and no hidden row stays selected.
function b32 TestFilterConsistent(Prog_State *state)
{
    b32 result = true;
    String filter = Prog_EditText(&state->filter);
    u64 shown = 0;
    for (u64 i = 0; i < state->directory.count; ++i) {
        b32 match = Prog_EntryScore(state->directory.entries[i].file.name, filter, NULL) > 0;
        b32 inView = Prog_ViewPosition(state, i) != UINT64_MAX;
        result &= match == inView && (inView || !state->directory.entries[i].selected);
        shown += inView != 0;
    }
    return result && shown == state->viewCount && TestSelectionCount(state) <= state->viewCount;
}

function void TestFilter(TestFileFixture *fixture)
{
    // Letters in order, Unicode case folded, accents kept.
    u8 marks[16];
    Check(Prog_FilterMatch(S("my_folder.txt"), S("mfd"), marks) && marks[0] && !marks[1] && marks[3] && marks[6] && !marks[7]);
    Check(Prog_FilterMatch(S("Élan.txt"), S("éLAN"), marks) && marks[0] && marks[1] && marks[4] && !marks[5]);
    Check(Prog_FilterMatch(S("ÄBC.txt"), S("äbc"), NULL) && Prog_FilterMatch(S("ŁÓDŹ"), S("łódź"), NULL) && Prog_FilterMatch(S("ПРИВЕТ"), S("привет"), NULL));
    Check(!Prog_FilterMatch(S("Élan.txt"), S("elan"), NULL) && !Prog_FilterMatch(S("elan2.txt"), S("élan"), NULL));
    Check(Prog_FilterMatch(S("plain.txt"), (String){0}, NULL) && !Prog_FilterMatch(S("plain.txt"), S("plainer"), NULL));

    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (!state.jobs.initialized) return;
    Check(TestWaitDirectory(&state));
    Check(state.viewCount == state.directory.count && state.directory.count >= 7);

    // Typing filters and focuses the first match; hidden rows leave the selection.
    TestBrowsingCommand(&state, Prog_CommandKind_SelectAll, 0);
    TestRenameType(&state, S("FOLDER"));
    Check(state.filtering && state.viewCount == 2 && TestFilterConsistent(&state) && TestSelectionCount(&state) == 2);
    Check(state.hasFocus && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("Folder2")));
    TestRenameKey(&state, Key_Down, 0);
    Check(StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("Folder10")) && TestSelectionCount(&state) == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_SelectAll, 0);
    Check(TestSelectionCount(&state) == 2);

    // Backspace edits the filter instead of going up, and applying it hands the keyboard back to the list.
    TestRenameKey(&state, Key_Backspace, 0);
    Check(StrIsEqual(Prog_EditText(&state.filter), S("FOLDE")) && StrIsEqual(state.directory.path, fixture->path));
    TestRenameType(&state, S("r1"));
    Check(state.viewCount == 1 && TestFilterConsistent(&state));
    TestBrowsingCommand(&state, Prog_CommandKind_ApplyFilter, 0);
    Check(!state.filtering && state.filter.length == 7 && state.viewCount == 1 && !state.loading);

    // A refresh keeps the filter; Escape removes it.
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
    Check(state.viewCount == 1 && TestFilterConsistent(&state));
    TestRenameKey(&state, Key_Escape, 0);
    Check(!state.filter.length && !state.filtering && state.viewCount == state.directory.count);

    // The counts toggles leave out folders or files; totals include hidden items.
    Check(state.directory.totalFolders == 3 && state.directory.totalFiles == state.directory.count - 3 + 1);
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleFolders, 0);
    Check(state.viewCount == state.directory.count - 3 && !state.viewFolders);
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleFolders, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleFiles, 0);
    Check(state.viewCount == 3 && state.viewFolders == 3);
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleFiles, 0);
    Check(state.viewCount == state.directory.count);

    TestRenameType(&state, S("nothing matches this"));
    Check(!state.viewCount && !state.hasFocus && !TestSelectionCount(&state));
    TestRenameKey(&state, Key_A, KeyMod_Control);
    TestRenameType(&state, S("folder2"));
    Check(state.viewCount == 1 && state.hasFocus);

    // The clipboard and clicks in the box.
    TestRenameKey(&state, Key_A, KeyMod_Control);
    TestRenameKey(&state, Key_X, KeyMod_Control);
    Check(StrIsEqual(TestClipboard, S("folder2")) && !state.filter.length);
    PlatformClipboardSetText(S("fol\r\nder2"));
    TestRenameKey(&state, Key_V, KeyMod_Control);
    Check(StrIsEqual(Prog_EditText(&state.filter), S("folder2")) && state.viewCount == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_FilterCaret, 3);
    Check(state.filtering && state.filter.cursor == 3 && state.filter.anchor == 3);
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_FilterCaret, 5, 1, 0)->mods = KeyMod_Shift;
    Prog_ProcessCommands(&state);
    Check(state.filter.anchor == 3 && state.filter.cursor == 5);
    TestRenameKey(&state, Key_C, KeyMod_Control);
    Check(StrIsEqual(TestClipboard, S("de")));
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_FilterCaret, 2, 2, 0);
    Prog_ProcessCommands(&state);
    Check(state.filter.anchor == 0 && state.filter.cursor == 7);

    // Another folder starts unfiltered unless the filter is pinned; Enter opens the focused match.
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleFilterPin, 0);
    TestRenameKey(&state, Key_Enter, 0);
    Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.entries[0].file.name, S("inside.txt")));
    Check(state.filter.length == 7 && !state.viewCount);
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleFilterPin, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_Up, 0);
    Check(StrIsEqual(state.directory.path, fixture->path) && !state.filter.length && !state.filtering && state.viewCount == state.directory.count);
    Prog_Release(&state);
}

// Waits until the folder dropdown has its listing.
function b32 TestWaitFilesPopup(Prog_State *state)
{
    for (u64 start = PlatformNowMicroseconds(); state->filesOpen && !state->filesListed && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) {
        Prog_TakeCompletions(state);
    }
    return state->filesOpen && state->filesListed;
}

// The folder dropdown of the path bar's crumbs: its hotkey opens the shown folder's, the keys beside move it along the crumbs with the folder
// the path goes on into lit, typing finds in it, and Enter goes to what it lights.
function void TestFilesPopup(TestFileFixture *fixture)
{
    Check(P__TestMakeDirectory(fixture, S("Drop")) && P__TestMakeDirectory(fixture, S("Drop/Inner")) && P__TestMakeDirectory(fixture, S("Drop/Other")));
    Check(P__TestWriteFile(fixture, S("Drop/zeta.txt"), 1, false) && P__TestWriteFile(fixture, S("Drop/Inner/deep.txt"), 1, false));
    Arena arena = {0};
    String drop = Prog_JoinPath(&arena, fixture->path, S("Drop")), inner = Prog_JoinPath(&arena, drop, S("Inner"));
    Prog_State state = {0};
    Check(Prog_Init(&state, inner, NULL, NULL) && state.jobs.initialized);
    Check(TestWaitDirectory(&state));
    TestRenameKey(&state, Key_Down, KeyMod_Alt);
    Check(TestWaitFilesPopup(&state) && StrIsEqual(state.filesFolder, inner) && state.filesCount == 1 && !state.filesLit && !state.filesChild.length);
    TestRenameKey(&state, Key_Left, KeyMod_Alt);
    Check(TestWaitFilesPopup(&state) && StrIsEqual(state.filesFolder, drop) && StrIsEqual(state.filesChild, S("Inner")) && StrIsEqual(state.directory.path, inner));
    Check(state.filesCount == 3 && state.filesLit && StrIsEqual(state.filesItems[state.filesHighlight].file.name, S("Inner")) && StrIsEqual(state.filesItems[2].file.name, S("zeta.txt")));
    TestRenameKey(&state, Key_Right, KeyMod_Alt);
    Check(TestWaitFilesPopup(&state) && StrIsEqual(state.filesFolder, inner));
    TestRenameKey(&state, Key_Escape, 0);
    Check(!state.filesOpen);
    TestRenameKey(&state, Key_Down, KeyMod_Alt);
    TestRenameKey(&state, Key_Left, KeyMod_Alt);
    Check(TestWaitFilesPopup(&state));
    TestRenameType(&state, S("oth"));
    u32 rows[4];
    Check(Prog_FilesMatches(&state, rows) == 1 && state.filesHighlight == 0 && state.filesLit);
    TestRenameKey(&state, Key_Enter, 0);
    Check(TestWaitDirectory(&state) && !state.filesOpen && StrIsEqual(state.directory.path, Prog_JoinPath(&arena, drop, S("Other"))));
    Prog_Release(&state);
    ArenaRelease(&arena);
    Check(P__TestRemovePath(fixture, S("Drop/Inner/deep.txt"), false) && P__TestRemovePath(fixture, S("Drop/Inner"), true) && P__TestRemovePath(fixture, S("Drop/Other"), true));
    Check(P__TestRemovePath(fixture, S("Drop/zeta.txt"), false) && P__TestRemovePath(fixture, S("Drop"), true));
}

// The Quick Access filter: its hotkey gives its box the keyboard, typing finds sidebar items by their names' letters in order, Enter keeps the
// filter and hands the keyboard back, and Escape clears it.
function void TestQuickAccessFilter(TestFileFixture *fixture)
{
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL) && state.jobs.initialized);
    Check(TestWaitDirectory(&state));
    state.sidebarHidden = true;
    TestRenameKey(&state, Key_F, KeyMod_Alt);
    Check(state.sidebarFiltering && !state.sidebarHidden);
    // Each character comes after its key, as typing sends them.
    TestRenameKey(&state, Key_D, 0);
    TestRenameType(&state, S("d"));
    TestRenameKey(&state, Key_L, 0);
    TestRenameType(&state, S("l"));
    Check(StrIsEqual(Prog_EditText(&state.sidebarFilter), S("dl")) && !state.filtering && !state.filter.length);
    Check(Prog__SidebarFinds(&state, S("Downloads")) && !Prog__SidebarFinds(&state, S("Desktop")));
    u64 focused = state.focusedRow;
    TestRenameKey(&state, Key_Enter, 0);
    Check(!state.sidebarFiltering && state.sidebarFilter.length == 2 && StrIsEqual(state.directory.path, fixture->path));
    TestRenameKey(&state, Key_Down, 0);
    Check(state.focusedRow != focused || state.directory.count < 2);
    TestBrowsingCommand(&state, Prog_CommandKind_StartQuickAccessFilter, 0);
    TestRenameKey(&state, Key_Escape, 0);
    Check(!state.sidebarFiltering && !state.sidebarFilter.length && Prog__SidebarFinds(&state, S("Desktop")));
    Prog_Release(&state);
}

// Evening panels: three split off one after another to the right share the row evenly, and a column split inside one keeps its own share
// until all panels are evened.
function void TestEvenPanels(TestFileFixture *fixture)
{
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL) && state.jobs.initialized);
    Check(TestWaitDirectory(&state));
    TestBrowsingCommand(&state, Prog_CommandKind_SplitRight, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_SplitRight, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_SplitBottom, 0);
    Check(state.panelCount == 4 && state.splits[0].kind == Prog_SplitKind_Horizontal);
    state.splits[0].ratio = 0.7f;
    TestBrowsingCommand(&state, Prog_CommandKind_PanelEvenSiblings, 0);
    // The active panel is in the column, which is evened alone.
    u32 row = state.splits[0].second, column = Prog__SplitParent(&state, Prog__SplitOf(&state, state.panels[state.activePanel].id));
    Check(state.splits[0].ratio == 0.7f && state.splits[column].kind == Prog_SplitKind_Vertical && state.splits[column].ratio == 0.5f);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectPrev, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectPrev, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelEvenSiblings, 0);
    Check(fabsf(state.splits[0].ratio - 1.0f / 3.0f) < 1e-5f && state.splits[row].kind == Prog_SplitKind_Horizontal && state.splits[row].ratio == 0.5f);
    state.splits[0].ratio = state.splits[column].ratio = 0.8f;
    TestBrowsingCommand(&state, Prog_CommandKind_PanelEvenAll, 0);
    Check(fabsf(state.splits[0].ratio - 1.0f / 3.0f) < 1e-5f && state.splits[column].ratio == 0.5f);
    Prog_Release(&state);
}

// Open Containing Path: an item's real location, where a link points; a file's is its own path, and a missing one has none.
function void TestContainingPath(TestFileFixture *fixture)
{
    Arena arena = {0};
    String real = {0}, file = Prog_JoinPath(&arena, fixture->path, S("Folder2/inside.txt"));
    Check(P_RealPath(&arena, file, &real) && TestEndsWith(real, S("/Folder2/inside.txt")));
    Check(!P_RealPath(&arena, Prog_JoinPath(&arena, fixture->path, S("missing.txt")), &real));
#if defined(__linux__)
    String link = Prog_JoinPath(&arena, fixture->path, S("inside-link"));
    Check(symlink(P__FilePathCString(&arena, file), P__FilePathCString(&arena, link)) == 0);
    Check(P_RealPath(&arena, link, &real) && TestEndsWith(real, S("/Folder2/inside.txt")));
    Check(P__TestRemovePath(fixture, S("inside-link"), false));
#endif
    // The command shows the focused item of an expanded listing selected in the folder it really is in.
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL) && state.jobs.initialized);
    Check(TestWaitDirectory(&state));
    TestBrowsingCommand(&state, Prog_CommandKind_StartExpandedSearch, 0);
    TestRenameType(&state, S("inside"));
    Check(state.directory.expanded && state.hasFocus && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("Folder2/inside.txt")));
    // Run while the filter is up: Enter would open the file in its program.
    TestBrowsingCommand(&state, Prog_CommandKind_OpenContainingPath, 0);
    b32 shown = false;
    for (u64 start = PlatformNowMicroseconds(); !shown && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) {
        Prog_TakeCompletions(&state);
        TestWaitDirectory(&state);
        shown = TestEndsWith(state.directory.path, S("/Folder2")) && state.hasFocus && state.focusedRow < state.directory.count &&
                StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("inside.txt"));
    }
    Check(shown);
    Prog_Release(&state);
    ArenaRelease(&arena);
}

function void TestExpand(TestFileFixture *fixture)
{
    Check(StrIsEqual(Prog_EntryLeaf(S("a/b/c.txt")), S("c.txt")) && StrIsEqual(Prog_EntryParent(S("a/b/c.txt")), S("a/b")));
    Check(StrIsEqual(Prog_EntryLeaf(S("c.txt")), S("c.txt")) && !Prog_EntryParent(S("c.txt")).length);

    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (!state.jobs.initialized) return;
    Check(TestWaitDirectory(&state));
    Check(TestEntryIndex(&state, S("Folder2/inside.txt")) == UINT64_MAX);

    // Expanded Search lists subfolders' items and starts the filter; folders get totals from the walk.
    TestBrowsingCommand(&state, Prog_CommandKind_StartExpandedSearch, 0);
    Check(state.expanded && state.filtering && state.directory.expanded);
    u64 inside = TestEntryIndex(&state, S("Folder2/inside.txt")), folder = TestEntryIndex(&state, S("Folder2"));
    Check(inside != UINT64_MAX && folder != UINT64_MAX && !state.measureJob);
    if (inside == UINT64_MAX || folder == UINT64_MAX) goto finish;
    Check(state.directory.entries[folder].measured && state.directory.entries[folder].files == 1 && state.directory.entries[folder].bytes == 5);

    // The filter matches names, and the folders they are in for less.
    TestRenameType(&state, S("inside"));
    Check(state.viewCount == 1 && state.view[0] == inside && state.focusedRow == inside);
    TestRenameKey(&state, Key_A, KeyMod_Control);
    TestRenameType(&state, S("folder2"));
    Check(state.viewCount == 2 && state.view[0] == folder && state.view[1] == inside);
    TestRenameKey(&state, Key_A, KeyMod_Control);
    TestRenameType(&state, S("inside"));

    // Renaming a nested item edits its name and renames it in its own folder; the filter gives way to the new name.
    Prog_BeginRename(&state, inside);
    Check(state.renaming && StrIsEqual(Prog_EditText(&state.rename), S("inside.txt")));
    TestRenameKey(&state, Key_A, KeyMod_Control);
    TestRenameType(&state, S("moved.txt"));
    TestRenameKey(&state, Key_Enter, 0);
    for (u64 start = PlatformNowMicroseconds(); !state.directoryJob && PlatformNowMicroseconds() - start < Millions(5);) {
        Prog_TakeCompletions(&state);
        if (!state.directoryJob) P__TestPause();
    }
    Check(TestWaitDirectory(&state) && !state.error.length);
    u64 moved = TestEntryIndex(&state, S("Folder2/moved.txt"));
    Check(moved != UINT64_MAX && state.directory.entries[moved].selected && state.focusedRow == moved && !state.filter.length);
    Check(!P_RenamePath(&state.frameArena, Prog_JoinPath(&state.frameArena, fixture->path, S("Folder2")), S("moved.txt"), S("inside.txt")).code);

    // Turning it off lists the folder flat again.
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleExpandFolder, 0);
    Check(!state.expanded && TestEntryIndex(&state, S("Folder2/inside.txt")) == UINT64_MAX && TestEntryIndex(&state, S("Folder2")) != UINT64_MAX);
finish:
    Prog_Release(&state);
}

function void TestWatch(TestFileFixture *fixture)
{
    P_DirectoryWatch watch = {0};
    Check(P_DirectoryWatchStart(&watch, fixture->path, NULL, NULL));
    Check(!P_DirectoryWatchTakeChanged(&watch));
    Check(P__TestWriteFile(fixture, S("watched.txt"), 1, false));
    b32 changed = false;
    for (u64 start = PlatformNowMicroseconds(); !changed && PlatformNowMicroseconds() - start < Millions(5);) {
        changed = P_DirectoryWatchTakeChanged(&watch);
        if (!changed) P__TestPause();
    }
    Check(changed);
    P_DirectoryWatchStop(&watch);
    Check(!watch.native && !P_DirectoryWatchTakeChanged(&watch));
    Arena arena = {0};
    Check(!P_DirectoryWatchStart(&watch, Prog_JoinPath(&arena, fixture->path, S("missing")), NULL, NULL) && !watch.native);
    ArenaRelease(&arena);

    // The shown folder follows changes by itself and stays usable while it refreshes.
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (state.jobs.initialized) {
        Check(TestWaitDirectory(&state) && state.watch.native);
        Check(TestEntryIndex(&state, S("watched.txt")) != UINT64_MAX);
        Check(P__TestRemovePath(fixture, S("watched.txt"), false));
        b32 gone = false, usable = true;
        for (u64 start = PlatformNowMicroseconds(); !gone && PlatformNowMicroseconds() - start < Millions(5);) {
            Prog_TakeCompletions(&state);
            usable &= !state.loading;
            gone = TestEntryIndex(&state, S("watched.txt")) == UINT64_MAX;
            if (!gone) P__TestPause();
        }
        Check(gone && usable);
    }
    Prog_Release(&state);
    Check(!state.watch.native);
    P__TestRemovePath(fixture, S("watched.txt"), false);
}

function void TestMeasure(TestFileFixture *fixture)
{
    Arena arena = {0};
    Check(StrIsEqual(Prog__FormatCount(&arena, 999), S("999")) && StrIsEqual(Prog__FormatCount(&arena, 1599), S("1.5K")));
    Check(StrIsEqual(Prog__FormatCount(&arena, 250000), S("250K")) && StrIsEqual(Prog__FormatCount(&arena, 3210000), S("3.2M")));
#if defined(__linux__)
    // A walk of the root counts /proc as a folder but stays out of it, a file system of its own; a plain folder it goes into.
    {
        P_DirectoryTotals counted = {0};
        ArrayStr below = {0};
        Check(P_CountFolder(&arena, S("/"), &counted, &below) && counted.folders > 0);
        b32 proc = false, usr = false;
        for (u64 i = 0; i < below.header.count; ++i) proc |= StrIsEqual(below.v[i], S("/proc")), usr |= StrIsEqual(below.v[i], S("/usr"));
        Check(!proc && usr);
    }
#endif
    // An image file is read at its own size or shrunk to fit, and a text file is not taken for a picture.
    {
        local_persist const u8 picture[] = "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A\x00\x00\x00\x0D\x49\x48\x44\x52\x00\x00\x00\x03\x00\x00\x00\x02\x08\x06\x00\x00\x00\x9D\x74\x66\x1A\x00\x00\x00\x11\x49\x44\x41\x54\x78\x9C\x63\xF8\xCF\xC0\xF0\x1F\x86\x19\x90\x39\x00\x9B\x7E\x0B\xF5\x72\xB0\xB9\x3C\x00\x00\x00\x00\x49\x45\x4E\x44\xAE\x42\x60\x82";
        String path = Prog_JoinPath(&arena, fixture->path, S("red.png"));
        Check(!P_WriteFile(&arena, path, StrMake((const char *)picture, sizeof(picture) - 1)).code);
        P_Image image = {0};
        i32 width = 0, height = 0;
        Check(P_LoadImageFile(&arena, path, 4096, &image, &width, &height) && width == 3 && height == 2 && image.width == 3 && image.pixels[0] == 0xFF0000FFu);
        Check(P_LoadImageFile(&arena, path, 2, &image, &width, &height) && width == 3 && image.width == 2);
        Check(!P_LoadImageFile(&arena, Prog_JoinPath(&arena, fixture->path, S("plain.txt")), 4096, &image, &width, &height));
        Check(P__TestRemovePath(fixture, S("red.png"), false));
    }
    Check(StrIsEqual(Prog_FormatSize(&arena, 0), S("0 B")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 1010), S("1010 B")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 1024), S("1 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 1200), S("1.1 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 1842), S("1.8 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 2047), S("1.9 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 9420), S("9.2 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 2097151), S("1.9 MB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 150000), S("146.4 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 1048575), S("1023.9 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 3200000), S("3 MB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, UINT64_C(12268339)), S("11.7 MB")));

    P_CancelToken token = {0};
    // Several trees measured at once each get their own totals; hidden items count, and a subfolder's files count toward its tree.
    {
        String paths[] = { Prog_JoinPath(&arena, fixture->path, S("Folder2")), fixture->path, Prog_JoinPath(&arena, fixture->path, S("missing")) };
        Prog_TreeTotals trees[CArrayCount(paths)] = {0};
        Check(Prog_MeasureTrees(paths, CArrayCount(paths), trees, &token));
        P_DirectoryTotals folder = Prog_TreeTotalsTaken(&trees[0], true), root = Prog_TreeTotalsTaken(&trees[1], true), missing = Prog_TreeTotalsTaken(&trees[2], true);
        Check(folder.complete && folder.folders == 0 && folder.files == 1 && folder.bytes == 5);
        Check(root.complete && root.folders == 3 && root.files >= 7 && root.bytes >= 22);
        Check(!missing.complete && !missing.files);
        for (u32 i = 0; i < CArrayCount(paths); ++i) Check(atomic_load(&trees[i].pending) == 0);
    }
    P_RequestCancel(&token);
    {
        Prog_TreeTotals tree = {0};
        Check(!Prog_MeasureTrees(&fixture->path, 1, &tree, &token));
    }

    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (state.jobs.initialized) {
        Check(TestWaitDirectory(&state));
        for (u64 start = PlatformNowMicroseconds(); state.measureJob && PlatformNowMicroseconds() - start < Millions(5);) {
            Prog_TakeCompletions(&state);
            if (state.measureJob) P__TestPause();
        }
        u64 folder = TestEntryIndex(&state, S("Folder2")), empty = TestEntryIndex(&state, S("Empty"));
        Check(folder != UINT64_MAX && empty != UINT64_MAX);
        if (folder != UINT64_MAX && empty != UINT64_MAX) {
            Prog_Entry *entry = &state.directory.entries[folder];
            Check(entry->measured && entry->folders == 0 && entry->files == 1 && entry->bytes == 5);
            entry = &state.directory.entries[empty];
            Check(entry->measured && !entry->folders && !entry->files && !entry->bytes);
            // A refresh keeps the totals of unchanged folders without walking them again.
            TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
            folder = TestEntryIndex(&state, S("Folder2"));
            Check(folder != UINT64_MAX && state.directory.entries[folder].measured && !state.measureJob);
            // Size, Files and Folders sort folders by their totals, keeping the focus on the same item.
            Prog_SelectRow(&state, folder, 0);
            // A new sort by size starts with the biggest.
            TestBrowsingCommand(&state, Prog_CommandKind_Sort, Prog_Sort_Size);
            Check(state.descending && TestEntryIndex(&state, S("Folder2")) == 0 && state.focusedRow == 0 && state.directory.entries[0].selected);
            TestBrowsingCommand(&state, Prog_CommandKind_Sort, Prog_Sort_Size);
            Check(!state.descending && TestEntryIndex(&state, S("Folder2")) == 2 && TestEntryIndex(&state, S("Empty")) == 0);
            Check(state.hasFocus && state.focusedRow == 2 && state.directory.entries[2].selected);
            TestBrowsingCommand(&state, Prog_CommandKind_SortByItemCount, 0);
            Check(state.sort == Prog_Sort_Items && state.descending && TestEntryIndex(&state, S("Folder2")) == 0);
            TestBrowsingCommand(&state, Prog_CommandKind_Sort, Prog_Sort_Files);
            TestBrowsingCommand(&state, Prog_CommandKind_Sort, Prog_Sort_Files);
            Check(!state.descending && TestEntryIndex(&state, S("Folder2")) == 2);
            // A folder measured after the listing moves into place.
            state.directory.entries[0].measured = true;
            state.directory.entries[0].files = 9;
            Prog__SortByTotals(&state);
            Check(state.directory.entries[2].files == 9 && state.directory.entries[state.focusedRow].selected);
        }
    }
    Prog_Release(&state);
    ArenaRelease(&arena);
}

// Names inside `relative` below the fixture, for checks and for cleanup.
function P_FileArray TestListing(Arena *arena, TestFileFixture *fixture, String relative)
{
    P_CancelToken token = {0};
    return P_EnumerateDirectory(arena, Prog_JoinPath(arena, fixture->path, relative), &token).files;
}

function b32 TestHasFile(Arena *arena, TestFileFixture *fixture, String relative, String name)
{
    P_FileArray files = TestListing(arena, fixture, relative);
    for (u64 i = 0; i < files.header.count; ++i) {
        if (StrIsEqual(files.v[i].name, name)) return true;
    }
    return false;
}

// Removes what a test made under `relative`, one item at a time, staying inside the fixture.
function void TestRemoveTree(Arena *arena, TestFileFixture *fixture, String relative)
{
    Assert(relative.length);
    P_FileArray files = TestListing(arena, fixture, relative);
    for (u64 i = 0; i < files.header.count; ++i) {
        String child = StrFormat(arena, "%S/%S", relative, files.v[i].name);
        if (files.v[i].kind == P_FileKind_Directory && !files.v[i].symbolicLink) TestRemoveTree(arena, fixture, child);
        else Check(P__TestRemovePath(fixture, child, false));
    }
    Check(P__TestRemovePath(fixture, relative, true));
}

#if defined(__linux__)
// A transfer started paused on one lane, which the other lets go on once it has seen it wait.
typedef struct TestPausedTransfer {
    Arena *arena;
    String path, target;
    P_Progress progress;
    P_CancelToken *token;
    b32 failed, early;
} TestPausedTransfer;

function void TestPausedTransferLane(void *user, u32 lane)
{
    TestPausedTransfer *run = user;
    if (lane == 1) {
        run->failed = P_TransferItems(run->arena, &run->path, 1, run->target, false, &run->progress, NULL, run->token).code != 0;
        return;
    }
    u64 start = PlatformNowMicroseconds();
    while (PlatformNowMicroseconds() - start < 150000) P__TestPause();
    run->early = atomic_load(&run->progress.doneItems) != 0;
    atomic_store(&run->progress.paused, false);
}
#endif

function void TestFileOperations(TestFileFixture *fixture)
{
    Arena arena = {0};
    P_CancelToken token = {0};
    String created = {0};
    Check(!P_CreateItem(&arena, fixture->path, S("Ops"), true, &created).code && StrIsEqual(created, S("Ops")));
    String ops = Prog_JoinPath(&arena, fixture->path, S("Ops"));

    // New items take a number when their name is taken.
    Check(!P_CreateItem(&arena, ops, S("New folder"), true, &created).code && StrIsEqual(created, S("New folder")));
    Check(!P_CreateItem(&arena, ops, S("New folder"), true, &created).code && StrIsEqual(created, S("New folder (2)")));
    Check(!P_CreateItem(&arena, ops, S("New file.txt"), false, &created).code && StrIsEqual(created, S("New file.txt")));
    Check(!P_CreateItem(&arena, ops, S("New file.txt"), false, &created).code && StrIsEqual(created, S("New file (2).txt")));
    Check(P_CreateItem(&arena, ops, S("a/b"), true, &created).code && !created.length);

    // Copies never replace what is there, and folders copy with their contents.
    String plain = Prog_JoinPath(&arena, fixture->path, S("plain.txt"));
    String folder = Prog_JoinPath(&arena, fixture->path, S("Folder2"));
    Check(!P_TransferItems(&arena, &plain, 1, ops, false, NULL, NULL, &token).code);
    Check(!P_TransferItems(&arena, &plain, 1, ops, false, NULL, NULL, &token).code);
    Check(TestHasFile(&arena, fixture, S("Ops"), S("plain.txt")) && TestHasFile(&arena, fixture, S(""), S("plain.txt")));
    Check(TestListing(&arena, fixture, S("Ops")).header.count == 6);
#if defined(__linux__)
    // A taken name can also be skipped, or replaced by the new file once it is complete; the progress counts the work.
    {
        P_Progress progress = { .policy = P_Conflict_Skip };
        Check(!P_TransferItems(&arena, &plain, 1, ops, false, &progress, NULL, &token).code && TestListing(&arena, fixture, S("Ops")).header.count == 6);
        Check(atomic_load(&progress.doneItems) == 1 && atomic_load(&progress.totalItems) == 1 && atomic_load(&progress.doneBytes) == 0);
        progress = (P_Progress){ .policy = P_Conflict_Replace };
        Check(!P_TransferItems(&arena, &plain, 1, ops, false, &progress, NULL, &token).code && TestListing(&arena, fixture, S("Ops")).header.count == 6);
        Check(atomic_load(&progress.doneItems) == 1 && atomic_load(&progress.doneBytes) == atomic_load(&progress.totalBytes));
        // It names the file it copied last, under the source's name rather than the free one it was copied to first.
        Check(StrIsEqual(Prog__TransferName(&arena, &progress), S("plain.txt")) && !(atomic_load(&progress.nameVersion) & 1));
        // A file is written under a hidden name, and the one an earlier copy cut off left is kept.
        Check(!P_CreateItem(&arena, fixture->path, S("Parts"), true, &created).code);
        String parts = Prog_JoinPath(&arena, fixture->path, S("Parts")), leftover = Prog_JoinPath(&arena, parts, S(".plain.txt.part")), contents = {0};
        Check(!P_WriteFile(&arena, leftover, S("cut off")).code);
        Check(!P_TransferItems(&arena, &plain, 1, parts, false, NULL, NULL, &token).code && TestHasFile(&arena, fixture, S("Parts"), S("plain.txt")));
        Check(P_ReadFile(&arena, leftover, &contents) && StrIsEqual(contents, S("cut off")) && TestListing(&arena, fixture, S("Parts")).header.count == 2);
        // A paused transfer starts nothing until it is let go on.
        TestPausedTransfer run = { .arena = &arena, .path = plain, .target = parts, .token = &token };
        atomic_store(&run.progress.policy, P_Conflict_KeepBoth);
        atomic_store(&run.progress.paused, true);
        P_RunLanes(2, TestPausedTransferLane, &run);
        Check(!run.early && !run.failed && atomic_load(&run.progress.doneItems) == 1 && TestListing(&arena, fixture, S("Parts")).header.count == 3);
        // A file too big for another file system fails before anything is written, as /dev/shm is its own.
        char shmDir[] = "/dev/shm/filey-tests-XXXXXX";
        struct statvfs shm;
        struct stat here, there;
        if (statvfs("/dev/shm", &shm) == 0 && mkdtemp(shmDir)) {
            String huge = Prog_JoinPath(&arena, parts, S("huge.bin"));
            Check(!P_WriteFile(&arena, huge, S("")).code && truncate(P__TestNativePath(&arena, fixture, S("Parts/huge.bin")), (off_t)(shm.f_blocks * shm.f_frsize + Megabytes(64))) == 0);
            if (stat(shmDir, &there) == 0 && stat(P__TestNativePath(&arena, fixture, S("Parts")), &here) == 0 && here.st_dev != there.st_dev) {
                u64 start = PlatformNowMicroseconds();
                Check(P_TransferItems(&arena, &huge, 1, StrFromCStr(shmDir), false, NULL, NULL, &token).code == ENOSPC && PlatformNowMicroseconds() - start < 1000000);
            }
            Check(rmdir(shmDir) == 0);
        }
        TestRemoveTree(&arena, fixture, S("Parts"));
    }
#endif
    Check(!P_TransferItems(&arena, &folder, 1, ops, false, NULL, NULL, &token).code);
    Check(TestHasFile(&arena, fixture, S("Ops/Folder2"), S("inside.txt")) && TestHasFile(&arena, fixture, S("Folder2"), S("inside.txt")));

    // A folder never goes inside itself.
    String copy = Prog_JoinPath(&arena, ops, S("Folder2"));
    Check(P_TransferItems(&arena, &copy, 1, copy, false, NULL, NULL, &token).code);
    Check(P_TransferItems(&arena, &copy, 1, Prog_JoinPath(&arena, copy, S("deeper")), true, NULL, NULL, &token).code);
    Check(TestListing(&arena, fixture, S("Ops/Folder2")).header.count == 1);

    // Moves take the item out of its folder; a move into its own folder does nothing.
    String moving = Prog_JoinPath(&arena, ops, S("plain.txt"));
    String target = Prog_JoinPath(&arena, ops, S("New folder"));
    Check(!P_TransferItems(&arena, &moving, 1, ops, true, NULL, NULL, &token).code && TestHasFile(&arena, fixture, S("Ops"), S("plain.txt")));
    Check(!P_TransferItems(&arena, &moving, 1, target, true, NULL, NULL, &token).code);
    Check(TestHasFile(&arena, fixture, S("Ops/New folder"), S("plain.txt")) && !TestHasFile(&arena, fixture, S("Ops"), S("plain.txt")));
    String cancelled = Prog_JoinPath(&arena, ops, S("New file.txt"));
    P_RequestCancel(&token);
    Check(P_TransferItems(&arena, &cancelled, 1, target, false, NULL, NULL, &token).code && !TestHasFile(&arena, fixture, S("Ops/New folder"), S("New file.txt")));
    token = (P_CancelToken){0};

#if defined(__linux__)
    // The trash test uses a trash inside the fixture, never the real one.
    const char *previous = getenv("XDG_DATA_HOME");
    String saved = previous ? StrCopy(&arena, StrFromCStr(previous)) : (String){0};
    String data = Prog_JoinPath(&arena, ops, S("data"));
    setenv("XDG_DATA_HOME", P__FilePathCString(&arena, data), 1);
    String trashed = Prog_JoinPath(&arena, ops, S("New file (2).txt"));
    Check(!P_TrashItems(&arena, &trashed, 1).code);
    if (saved.length) setenv("XDG_DATA_HOME", P__FilePathCString(&arena, saved), 1);
    else unsetenv("XDG_DATA_HOME");
    Check(!TestHasFile(&arena, fixture, S("Ops"), S("New file (2).txt")) && TestHasFile(&arena, fixture, S("Ops/data/Trash/files"), S("New file (2).txt")));
    Check(TestHasFile(&arena, fixture, S("Ops/data/Trash/info"), S("New file (2).txt.trashinfo")));

    // File lists travel as file:// URIs, percent-encoded; GNOME's first line says whether they were cut.
    Check(StrIsEqual(P__FileUri(&arena, S("/tmp/a b%.txt")), S("file:///tmp/a%20b%25.txt")));
    Check(StrIsEqual(P__FilePathFromUri(&arena, S("file:///tmp/a%20b%25.txt")), S("/tmp/a b%.txt")));
    Check(StrIsEqual(P__FilePathFromUri(&arena, S("file://localhost/x")), S("/x")) && !P__FilePathFromUri(&arena, S("file://host/x")).length);
    Check(!P__FilePathFromUri(&arena, S("https://x/y")).length && !P__FilePathFromUri(&arena, S("file:///a%00b")).length);
    ArrayStr parsed = {0};
    b32 parsedCut = false;
    P__ParseFileList(&arena, S("cut\nfile:///one\nfile:///two%20x"), true, &parsed, &parsedCut);
    Check(parsedCut && parsed.header.count == 2 && StrIsEqual(parsed.v[1], S("/two x")));
    P__ParseFileList(&arena, S("# comment\r\nfile:///one\r\n"), false, &parsed, &parsedCut);
    Check(!parsedCut && parsed.header.count == 1 && StrIsEqual(parsed.v[0], S("/one")));

    // Links copy as links.
    String link = Prog_JoinPath(&arena, fixture->path, S("broken-link"));
    Check(!P_TransferItems(&arena, &link, 1, ops, false, NULL, NULL, &token).code && TestHasFile(&arena, fixture, S("Ops"), S("broken-link")));
#endif

    TestRemoveTree(&arena, fixture, S("Ops"));
    Check(!TestHasFile(&arena, fixture, S(""), S("Ops")));
    ArenaRelease(&arena);
}

// Runs `kind`, then waits for the job it starts and the reload that follows.
function void TestFileCommand(Prog_State *state, Prog_CommandKind kind)
{
    ArenaClear(&state->frameArena);
    Prog_QueueCommand(state, kind, 0, 1, 0);
    Prog_ProcessCommands(state);
    for (u64 start = PlatformNowMicroseconds(); !state->directoryJob && PlatformNowMicroseconds() - start < Millions(5);) {
        Prog_TakeCompletions(state);
        if (!state->directoryJob) P__TestPause();
    }
    Check(TestWaitDirectory(state));
}

// A watch reload can land before the one a command starts, so checks on the result wait for it.
function b32 TestWaitCount(Prog_State *state, u64 count)
{
    for (u64 start = PlatformNowMicroseconds(); (state->directory.count != count || state->directoryJob) && PlatformNowMicroseconds() - start < Millions(5);) {
        Prog_TakeCompletions(state);
        P__TestPause();
    }
    return state->directory.count == count;
}

#if defined(__linux__)
// The transfer slot that waits on a question about a taken name, once one does, or UINT32_MAX after five seconds.
function u32 TestAskingTransfer(Prog_State *state)
{
    for (u64 start = PlatformNowMicroseconds(); PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) {
        for (u32 i = 0; i < Prog_MaxTransfers; ++i) if (state->transfers[i].job && atomic_load(&state->transfers[i].progress.asking)) return i;
    }
    return UINT32_MAX;
}

function void TestWaitTransfer(Prog_State *state, u32 slot)
{
    for (u64 start = PlatformNowMicroseconds(); state->transfers[slot].job && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) Prog_TakeCompletions(state);
}
#endif

function void TestProgramFileCommands(TestFileFixture *fixture)
{
    Arena arena = {0};
    String created = {0};
    Check(!P_CreateItem(&arena, fixture->path, S("Commands"), true, &created).code);
    String root = Prog_JoinPath(&arena, fixture->path, S("Commands"));
    Check(!P_CreateItem(&arena, root, S("a.txt"), false, &created).code);
    Prog_State state = {0};
    Check(Prog_Init(&state, root, NULL, NULL));
    if (!state.jobs.initialized) goto finish;
    Check(TestWaitDirectory(&state) && state.directory.count == 1);

    // A new folder is selected and renamed once it shows up.
    TestFileCommand(&state, Prog_CommandKind_CreateFolder);
    u64 folder = TestEntryIndex(&state, S("New folder"));
    Check(folder != UINT64_MAX && state.renaming && state.focusedRow == folder && StrIsEqual(Prog_EditText(&state.rename), S("New folder")));
    TestRenameKey(&state, Key_Escape, 0);

    // Copy and Paste in the same folder makes a numbered or "- Copy" duplicate, as does Duplicate.
    Prog_SelectRow(&state, TestEntryIndex(&state, S("a.txt")), 0);
    TestRenameKey(&state, Key_C, KeyMod_Control);
    Check(TestClipboardFiles.header.count == 1 && !TestClipboardCut && StrIsEqual(TestClipboardFiles.v[0], Prog_JoinPath(&arena, root, S("a.txt"))));
    TestFileCommand(&state, Prog_CommandKind_Paste);
    Check(TestWaitCount(&state, 3) && !state.error.length);
    Prog_SelectRow(&state, TestEntryIndex(&state, S("a.txt")), 0);
    TestFileCommand(&state, Prog_CommandKind_Duplicate);
    Check(TestWaitCount(&state, 4) && !state.error.length);

    // Cut and Paste in another folder moves.
    Prog_SelectRow(&state, TestEntryIndex(&state, S("a.txt")), 0);
    TestRenameKey(&state, Key_X, KeyMod_Control);
    Check(TestClipboardCut);
    TestBrowsingCommand(&state, Prog_CommandKind_SelectRow, TestEntryIndex(&state, S("New folder")));
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, TestEntryIndex(&state, S("New folder")), 2, 0);
    Prog_ProcessCommands(&state);
    Check(TestWaitDirectory(&state) && !state.directory.count);
    TestFileCommand(&state, Prog_CommandKind_Paste);
    Check(!state.error.length && TestEntryIndex(&state, S("a.txt")) != UINT64_MAX);
    Check(!TestHasFile(&arena, fixture, S("Commands"), S("a.txt")));

    // Pasting where the name is taken asks first, and the answer is carried out: here the file is replaced. Windows leaves taken names to the shell's own question.
#if defined(__linux__)
    {
        String other = Prog_JoinPath(&arena, root, S("a.txt")), inside = Prog_JoinPath(&arena, Prog_JoinPath(&arena, root, S("New folder")), S("a.txt")), contents = {0};
        Check(!P_WriteFile(&arena, other, S("newer")).code);
        PlatformClipboardSetFiles(&other, 1, false);
        TestBrowsingCommand(&state, Prog_CommandKind_Paste, 0);
        u32 asked = TestAskingTransfer(&state);
        Check(asked != UINT32_MAX && StrIsEqual(StrFromCStr(state.transfers[asked].progress.conflictName), S("a.txt")));
        if (asked != UINT32_MAX) {
            ArenaClear(&state.frameArena);
            Prog_QueueCommand(&state, Prog_CommandKind_TransferAnswer, asked, 0, 0)->target = P_Conflict_Replace;
            Prog_ProcessCommands(&state);
            TestWaitTransfer(&state, asked);
        }
        Check(TestWaitCount(&state, 1) && P_ReadFile(&arena, inside, &contents) && StrIsEqual(contents, S("newer")));
        // A drop goes the same way, and Escape cancels the copy waiting on its question.
        Check(!P_WriteFile(&arena, other, S("newest")).code);
        ArrayStr dropped = {0};
        ArrayPush(&arena, dropped, other);
        P__KeepDroppedFiles(&dropped, state.directory.path, false);
        ArenaClear(&state.frameArena);
        Prog_ProcessCommands(&state);
        asked = TestAskingTransfer(&state);
        Check(asked != UINT32_MAX);
        if (asked != UINT32_MAX) {
            TestRenameKey(&state, Key_Escape, 0);
            TestWaitTransfer(&state, asked);
        }
        Check(TestWaitCount(&state, 1) && P_ReadFile(&arena, inside, &contents) && StrIsEqual(contents, S("newer")) && !state.error.length);
        P_CancelToken token = {0};
        Check(!P_DeleteItems(&arena, &other, 1, false, &token).code);
        // The refresh the folder's watch defers for the files that changed comes before the next steps.
        u64 settle = PlatformNowMicroseconds();
        while (PlatformNowMicroseconds() - settle < 2 * Prog_WatchInterval) P__TestPause();
        Prog_TakeCompletions(&state);
        Check(TestWaitDirectory(&state));
    }
#endif

    // Copy As Path puts the system's form of the path on the clipboard.
    Prog_SelectRow(&state, 0, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_CopyAsPath, 0);
    Check(StrIsEqual(TestClipboard, P_NativePath(&arena, Prog_JoinPath(&arena, Prog_JoinPath(&arena, root, S("New folder")), S("a.txt")))));
    // With nothing selected it copies the shown folder's path.
    for (u64 i = 0; i < state.directory.count; ++i) state.directory.entries[i].selected = false;
    state.selectedCount = 0;
    TestBrowsingCommand(&state, Prog_CommandKind_CopyAsPath, 0);
    Check(StrIsEqual(TestClipboard, P_NativePath(&arena, state.directory.path)));

    // Pasting with nothing selected or on the clipboard does nothing.
    PlatformClipboardSetText(S("not files"));
    TestBrowsingCommand(&state, Prog_CommandKind_Paste, 0);
    Check(!state.error.length);

    // Shift+Delete deletes for good, only once Enter confirms; Escape keeps everything.
    String inside = Prog_JoinPath(&arena, root, S("New folder"));
    Check(!P_CreateItem(&arena, inside, S("Doomed"), true, &created).code);
    Check(!P_CreateItem(&arena, Prog_JoinPath(&arena, inside, S("Doomed")), S("content.txt"), false, &created).code);
    Check(TestWaitCount(&state, 2));
    Prog_SelectRow(&state, TestEntryIndex(&state, S("Doomed")), 0);
    TestRenameKey(&state, Key_Delete, KeyMod_Shift);
    Check(state.confirmingDelete && state.confirmCount == 1 && TestEndsWith(state.confirmPaths[0], S("/Doomed")));
    TestRenameKey(&state, Key_Escape, 0);
    Check(!state.confirmingDelete && TestHasFile(&arena, fixture, S("Commands/New folder"), S("Doomed")));
    TestRenameKey(&state, Key_Delete, KeyMod_Shift);
    TestRenameKey(&state, Key_Enter, 0);
    Check(!state.confirmingDelete && TestWaitCount(&state, 1) && !state.error.length);
    Check(!TestHasFile(&arena, fixture, S("Commands/New folder"), S("Doomed")) && TestHasFile(&arena, fixture, S("Commands/New folder"), S("a.txt")));

    // Ctrl+Space selects the focused item alone, without changing the rest; Ctrl+F6 copies the selection into the next panel's folder.
    TestBrowsingCommand(&state, Prog_CommandKind_SplitRight, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_Up, 0);
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_ScrollSelectFirst, 0, 0, 0)->panel = state.panels[0].id;
    Prog_ProcessCommands(&state);
    Check(state.activePanel == 0 && TestEndsWith(state.directory.path, S("/New folder")));
    for (u64 i = 0; i < state.directory.count; ++i) state.directory.entries[i].selected = false;
    state.selectedCount = 0;
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleCursorSelection, 0);
    Check(state.selectedCount == 1 && state.directory.entries[state.focusedRow].selected);
    TestBrowsingCommand(&state, Prog_CommandKind_SelectionCopyNextPanel, 0);
    for (u64 start = PlatformNowMicroseconds(); !TestHasFile(&arena, fixture, S("Commands"), S("a.txt")) && PlatformNowMicroseconds() - start < Millions(5);) {
        Prog_TakeCompletions(&state);
        P__TestPause();
    }
    Check(TestHasFile(&arena, fixture, S("Commands"), S("a.txt")) && TestHasFile(&arena, fixture, S("Commands/New folder"), S("a.txt")));
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleCursorSelection, 0);
    Check(state.selectedCount == 0);

#if defined(__linux__)
    // Trash goes to a trash inside the fixture, never the real one.
    String data = Prog_JoinPath(&arena, root, S("data"));
    const char *previous = getenv("XDG_DATA_HOME");
    String saved = previous ? StrCopy(&arena, StrFromCStr(previous)) : (String){0};
    setenv("XDG_DATA_HOME", P__FilePathCString(&arena, data), 1);
    Prog_SelectRow(&state, TestEntryIndex(&state, S("a.txt")), 0);
    TestFileCommand(&state, Prog_CommandKind_Trash);
    if (saved.length) setenv("XDG_DATA_HOME", P__FilePathCString(&arena, saved), 1);
    else unsetenv("XDG_DATA_HOME");
    Check(!state.error.length && TestEntryIndex(&state, S("a.txt")) == UINT64_MAX && TestHasFile(&arena, fixture, S("Commands/data/Trash/files"), S("a.txt")));

    // The context menu's items run the same commands.
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_ContextMenuAt, UINT64_MAX, 1, 0);
    Prog_ProcessCommands(&state);
    // The background menu ends with New, whatever comes before it.
    u32 newMenu = state.menuOpen ? state.menu.rootCount - 1 : 0;
    Check(state.menuOpen && StrIsEqual(state.menu.items[newMenu].label, S("New")) && state.menu.items[newMenu].childCount == 2);
    if (state.menuOpen) {
        u32 newFolder = state.menu.items[newMenu].firstChild;
        Check(state.menu.items[newFolder].action == P_MenuAction_NewFolder);
        ArenaClear(&state.frameArena);
        Prog_QueueCommand(&state, Prog_CommandKind_ContextMenuInvoke, newFolder, 1, 0);
        Prog_ProcessCommands(&state);
        for (u64 start = PlatformNowMicroseconds(); !state.directoryJob && PlatformNowMicroseconds() - start < Millions(5);) {
            Prog_TakeCompletions(&state);
            if (!state.directoryJob) P__TestPause();
        }
        Check(TestWaitDirectory(&state) && !state.menuOpen && state.renaming && TestEntryIndex(&state, S("New folder")) != UINT64_MAX);
        TestRenameKey(&state, Key_Escape, 0);
    }
#endif
finish:
    Prog_Release(&state);
    TestRemoveTree(&arena, fixture, S("Commands"));
    ArenaRelease(&arena);
}

// Tabs keep their own listing and selection, and closing, reopening and moving them keeps the shown one in place.
function void TestTabs(TestFileFixture *fixture)
{
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (!state.jobs.initialized) return;
    Check(TestWaitDirectory(&state));
    u64 plain = TestEntryIndex(&state, S("plain.txt")), empty = TestEntryIndex(&state, S("Empty"));
    Check(plain != UINT64_MAX && empty != UINT64_MAX && state.tabCount == 1);
    Prog_SelectRow(&state, plain, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_OpenRowInTab, empty);
    Check(state.tabCount == 2 && state.activeTab == 0 && TestEndsWith(Prog_TabPath(&state, 1), S("/Empty")));
    TestBrowsingCommand(&state, Prog_CommandKind_SelectTab, 1);
    Check(state.activeTab == 1 && TestEndsWith(state.directory.path, S("/Empty")) && state.directory.count == 0 && state.historyAt == 0);
    TestBrowsingCommand(&state, Prog_CommandKind_TabSelectNext, 0);
    Check(state.activeTab == 0 && StrIsEqual(state.directory.path, fixture->path));
    plain = TestEntryIndex(&state, S("plain.txt"));
    Check(plain != UINT64_MAX && state.directory.entries[plain].selected && TestSelectionCount(&state) == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_CloseTabAt, 1);
    Check(state.tabCount == 1 && state.closedTabs.header.count == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_TabReopen, 0);
    Check(state.tabCount == 2 && state.activeTab == 1 && TestEndsWith(state.directory.path, S("/Empty")) && !state.closedTabs.header.count);
    TestBrowsingCommand(&state, Prog_CommandKind_TabShiftLeft, 0);
    Check(state.activeTab == 0 && TestEndsWith(state.directory.path, S("/Empty")) && StrIsEqual(Prog_TabPath(&state, 1), fixture->path));
    // Search Tabs finds tabs by their folder's name first and shows the one picked.
    TestBrowsingCommand(&state, Prog_CommandKind_TabList, 0);
    u32 tabMatches[Prog_MaxTabs];
    Check(state.paletteOpen && state.paletteMode == Prog_PaletteMode_Tabs && Prog_TabMatches(&state, tabMatches) == 2);
    Prog__EditInsert(&state.palette, 'm');
    Prog__EditInsert(&state.palette, 'p');
    Check(Prog_TabMatches(&state, tabMatches) >= 1 && tabMatches[0] == 0);
    TestBrowsingCommand(&state, Prog_CommandKind_TabSelectNext, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_PaletteRun, UINT64_MAX);
    Check(!state.paletteOpen && state.activeTab == 0 && TestEndsWith(state.directory.path, S("/Empty")));
    TestBrowsingCommand(&state, Prog_CommandKind_TabClose, 0);
    Check(state.tabCount == 1 && state.activeTab == 0 && StrIsEqual(state.directory.path, fixture->path));
    // The last tab closes the window.
    u32 closes = TestWindowCalls[2];
    TestBrowsingCommand(&state, Prog_CommandKind_TabClose, 0);
    Check(state.tabCount == 1 && TestWindowCalls[2] == closes + 1);
    // A new tab shows This PC by default, or the current folder when the settings say so.
    TestBrowsingCommand(&state, Prog_CommandKind_TabNew, 0);
    Check(state.tabCount == 2 && state.activeTab == 1 && TestWaitDirectory(&state) && StrIsEqual(state.directory.path, Prog_ThisPC));
    TestBrowsingCommand(&state, Prog_CommandKind_TabClose, 0);
    state.newTab = Prog_NewTab_Current;
    TestBrowsingCommand(&state, Prog_CommandKind_TabNew, 0);
    state.newTab = Prog_NewTab_Root;
    Check(state.tabCount == 2 && state.activeTab == 1 && StrIsEqual(state.directory.path, fixture->path));
    // Tabs on a drive whose medium went away, such as an ejected disc, go where a new tab would, the hidden one in its slot.
    {
        P_Place lost = { .path = fixture->path, .totalBytes = 1 }, empty = { .path = fixture->path };
        Prog__LeaveLostDrives(&state, (P_ArrayPlace){ .header.count = 1, .v = &lost }, (P_ArrayPlace){0});
        Check(StrIsEqual(state.directory.path, fixture->path));
        Prog__LeaveLostDrives(&state, (P_ArrayPlace){ .header.count = 1, .v = &lost }, (P_ArrayPlace){ .header.count = 1, .v = &empty });
        Check(state.tabCount == 2 && state.activeTab == 1 && TestWaitDirectory(&state) && StrIsEqual(state.directory.path, Prog_ThisPC));
        Check(StrIsEqual(Prog_TabPath(&state, 0), Prog_ThisPC));
        Prog_RequestDirectory(&state, fixture->path, true, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, fixture->path));
    }
    // Edit Path starts Go To on the shown folder, its path selected and nothing highlighted.
    TestBrowsingCommand(&state, Prog_CommandKind_EditPath, 0);
    String shownPath = P_NativePath(&state.frameArena, Prog_JoinPath(&state.frameArena, state.directory.path, S("")));
    Check(state.paletteOpen && state.paletteMode == Prog_PaletteMode_GoTo && StrIsEqual(Prog_EditText(&state.palette), shownPath));
    Check(state.palette.anchor == 0 && state.palette.cursor == state.palette.length && state.paletteHighlight == UINT32_MAX);
    TestBrowsingCommand(&state, Prog_CommandKind_PaletteClose, 0);
    // Go To lists the folder typed, offers its subfolders starting with what follows it, and opens the one picked.
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Check(state.paletteOpen && state.paletteMode == Prog_PaletteMode_GoTo);
    String typed = StrFormat(&state.frameArena, "%S/em", fixture->path);
    Prog__EditReplace(&state.palette, (const u8 *)typed.value, (u32)typed.length);
    u32 offered;
    String folder;
    Prog_GoToMatches(&state, &offered, &folder);
    Check(offered == 0 && StrIsEqual(folder, fixture->path));
    u64 start = PlatformNowMicroseconds();
    while (!state.gotoListed && PlatformNowMicroseconds() - start < Millions(5)) {
        Prog_TakeCompletions(&state);
        P__TestPause();
    }
    Prog_GoToItem *found = Prog_GoToMatches(&state, &offered, &folder);
    Check(state.gotoListed && offered >= 1 && TestEndsWith(found[0].path, S("/Empty")) && !found[0].file);
    TestBrowsingCommand(&state, Prog_CommandKind_PaletteRun, UINT64_MAX);
    Check(!state.paletteOpen && TestEndsWith(state.directory.path, S("/Empty")));
    // Ctrl+Enter shows the folder picked in its parent, selected there.
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    typed = StrFormat(&state.frameArena, "%S/em", fixture->path);
    Prog__EditReplace(&state.palette, (const u8 *)typed.value, (u32)typed.length);
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_PaletteRun, UINT64_MAX, 1, 0)->mods = KeyMod_Control;
    Check(Prog_ProcessCommands(&state));
    Check(TestWaitDirectory(&state));
    u64 shownEmpty = TestEntryIndex(&state, S("Empty"));
    Check(StrIsEqual(state.directory.path, fixture->path) && shownEmpty != UINT64_MAX && state.directory.entries[shownEmpty].selected);
    // The drive index walks everything below the drives, writing itself to the settings folder as it goes, and Go To finds folders and files by name.
    Arena driveArena = {0};
    ArrayPush(&driveArena, state.drives, ((P_Place){ .path = fixture->path }));
    String created = {0};
    Check(!P_CreateItem(&driveArena, fixture->path, S("Cache"), true, &created).code);
    String cache = Prog_JoinPath(&driveArena, fixture->path, created);
    state.configDirectory = cache;
    start = PlatformNowMicroseconds();
    do {
        Prog_TakeCompletions(&state);
        P__TestPause();
    } while ((!state.indexDone || state.indexJob || state.indexLoadJob) && PlatformNowMicroseconds() - start < Millions(5));
    Check(state.indexStarted && state.indexDone && !state.indexJob && state.index.count >= 2);
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Prog__EditReplace(&state.palette, (const u8 *)"mpt", 3);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 1 && TestEndsWith(found[0].path, S("/Empty")) && !found[0].file);
    // A file is found as well, by its name.
    String fileName = {0};
    for (u64 i = 0; i < state.index.count && !fileName.length; ++i) {
        if (state.index.flags[i] & Prog_IndexFlag_File) fileName = Prog_IndexName(&state.index, i);
    }
    Check(fileName.length);
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Prog__EditReplace(&state.palette, (const u8 *)fileName.value, (u32)fileName.length);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 1 && found[0].file && TestEndsWith(found[0].path, fileName));
    // A drive is found by the name it is shown by, which its folder does not have.
    state.drives.v[0].name = S("Samsung Test Drive");
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Prog__EditReplace(&state.palette, (const u8 *)"samsung test", 12);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 1 && StrIsEqual(found[0].path, fixture->path) && StrIsEqual(found[0].name, S("Samsung Test Drive")));
    // And by its volume's UUID, pasted from a disk tool, which its folder is not named by here.
    state.drives.v[0].id = S("0008C21B08C21018");
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Prog__EditReplace(&state.palette, (const u8 *)"0008C21B08C21018", 16);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 1 && StrIsEqual(found[0].path, fixture->path) && StrIsEqual(found[0].name, S("Samsung Test Drive")) && found[0].drive == &state.drives.v[0]);
    // Ahead of a folder named after it, as GVfs keeps them.
    String named = Prog_JoinPath(&driveArena, fixture->path, S("uuid-0008C21B08C21018"));
    u64 marked = state.bookmarks.header.count;
    String *marks = ArenaPushArray(&driveArena, marked + 1, String);
    for (u64 i = 0; i < marked; ++i) marks[i] = StrCopy(&driveArena, state.bookmarks.v[i]);
    marks[marked] = named;
    Prog__SetList(&state.bookmarkArena, &state.bookmarks, marks, marked + 1);
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Prog__EditReplace(&state.palette, (const u8 *)"0008C21B08C21018", 16);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 2 && StrIsEqual(found[0].path, fixture->path) && StrIsEqual(found[1].path, named));
    // The rows found keep their paths when the bookmarks are written anew under the same count, as following a rename does.
    Prog__SetList(&state.bookmarkArena, &state.bookmarks, marks, marked + 1);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 2 && StrIsEqual(found[1].path, named));
    Prog__SetList(&state.bookmarkArena, &state.bookmarks, marks, marked);
    // What the walk wrote reads back as the same entries, once its last step has put it in place.
    String saved = Prog_JoinPath(&driveArena, cache, Prog_IndexFile), contents = {0};
    start = PlatformNowMicroseconds();
    while (!P_ReadFile(&driveArena, saved, &contents) && PlatformNowMicroseconds() - start < Millions(5)) P__TestPause();
    P_Job load = {0};
    Prog_JobInput loadInput = { .kind = Prog_JobKind_IndexLoad, .name = saved };
    load.input = &loadInput;
    Prog__DirectoryJob(&load);
    const Prog_Indexed *loaded = load.result;
    Check(loaded && loaded->count == state.index.count);
    for (u64 i = 0; loaded && i < loaded->count; ++i) {
        Check(StrIsEqual(Prog__PackedName(loaded->packed[i]), Prog_IndexName(&state.index, i)) && loaded->parents[i] == state.index.parents[i] &&
              (u8)loaded->flags[i] == state.index.flags[i]);
    }
    ArenaRelease(&load.arena);
    // The options write their lines into Settings.txt, over the commented-out defaults, and apply at once; each keeps the others.
    {
        Prog_SetOption(&state, Prog_Option_FontSize, 10);
        Prog_SetOption(&state, Prog_Option_Animations, 0);
        Prog_SetOption(&state, Prog_Option_InspectorLayout, Prog_InspectorLayout_Below);
        Prog_SetOption(&state, Prog_Option_PopupBlur, 500);
        // A slider being dragged shows its value without saving it, until it is let go.
        Prog_PreviewOption(&state, Prog_Option_FontSize, 12);
        Check(Prog_OptionValue(&state, Prog_Option_FontSize) == 12);
        Prog_ReloadSettingsFile(&state);
        Check(Prog_OptionValue(&state, Prog_Option_FontSize) == 10);
        Check(Prog_OptionValue(&state, Prog_Option_FontSize) == 10 && state.uiScale > 1.1f && state.animationsOff);
        Check(state.inspectorLayout == Prog_InspectorLayout_Below && Prog_OptionValue(&state, Prog_Option_PopupBlur) == 100);
        String settings = {0};
        Check(P_ReadFile(&driveArena, Prog_JoinPath(&driveArena, cache, S("Settings.txt")), &settings));
        ArrayStr lines = StrSplit(&driveArena, settings, '\n');
        u32 animations = 0, layouts = 0;
        for (u64 i = 0; i < lines.header.count; ++i) {
            animations += StrIsEqual(lines.v[i], S("animations = false")) ? 1 : 0;
            layouts += StrIsEqual(lines.v[i], S("inspector.layout = below")) ? 1 : 0;
        }
        Check(animations == 1 && layouts == 1);
        // The inspector's own layout buttons hold until the file changes the layout again.
        state.inspectorLayout = Prog_InspectorLayout_Right;
        Prog_SetOption(&state, Prog_Option_Animations, 1);
        Check(state.inspectorLayout == Prog_InspectorLayout_Right && !state.animationsOff && state.uiScale > 1.1f);
        // Options are a mode of the palette, found by their words.
        TestBrowsingCommand(&state, Prog_CommandKind_OpenOptions, 0);
        Prog__EditReplace(&state.palette, (const u8 *)"anim", 4);
        u8 options[Prog_Option_Count];
        Check(state.paletteOpen && state.paletteMode == Prog_PaletteMode_Options && Prog_OptionMatches(&state, options) == 1 && options[0] == Prog_Option_Animations);
        TestBrowsingCommand(&state, Prog_CommandKind_PaletteRun, UINT64_MAX);
        Check(state.animationsOff && state.paletteOpen);
        TestBrowsingCommand(&state, Prog_CommandKind_PaletteClose, 0);
        Prog_ApplySettings(&state, S(""));
    }
    state.configDirectory = (String){0};
    state.drives = (P_ArrayPlace){0};
    Prog_Release(&state);
    Check(!P_DeleteItems(&driveArena, &cache, 1, false, NULL).code);
    ArenaRelease(&driveArena);
}

// One frame of a 1200 by 800 window as the app runs it: `events` taken in, the commands they queued run, the UI built and laid out,
// and the commands it queued run.
function void TestWindowFrame(Prog_State *state, PlatformEvent *events, u32 count, u64 now)
{
    PlatformEventList list = count ? TestUIEvents(events, count) : (PlatformEventList){0};
    ArenaClear(&state->frameArena);
    state->ui.defaultFont = &TestUIFont;
    UI_BeginFrame(&state->ui, &state->frameArena, (R_Rect){ 0, 0, 1200, 800 }, list, now);
    Prog_HandleInput(state, list);
    Prog_ProcessCommands(state);
    Prog_BuildUI(state, &TestUIFont, &TestUIFont, 1);
    UI_EndFrame();
    Prog_ProcessCommands(state);
}

// One frame of the window, with the pointer moved to `x`, `y` when `move` is set.
function void TestPanelsFrame(Prog_State *state, b32 move, f32 x, f32 y, u64 now)
{
    PlatformEvent event = { .kind = PlatformEventKind_MouseMove, .x = x, .y = y };
    TestWindowFrame(state, &event, move ? 1 : 0, now);
}

// The inspector's text is selected with the pointer, a word with a double click and a line with a third, and Ctrl+C copies it rather than the file.
function void TestInspectorSelection(TestFileFixture *fixture)
{
    Arena arena = {0};
    String created = {0};
    Check(!P_CreateItem(&arena, fixture->path, S("Selecting"), true, &created).code);
    String folder = Prog_JoinPath(&arena, fixture->path, S("Selecting"));
    StrBuilder note = {0};
    StrBuildStr(&arena, &note, S("hello world\nsecond line\n"));
    for (u32 i = 0; i < 200; ++i) StrBuildStr(&arena, &note, StrFormat(&arena, "row %u\n", i));
    Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, folder, S("note.txt")), note.buffer).code);
    Prog_State state = {0};
    Check(Prog_Init(&state, folder, NULL, NULL));
    if (state.jobs.initialized && TestWaitDirectory(&state) && state.directory.count == 1) {
        Prog_SelectRow(&state, 0, 0);
        state.previewShown = true;
        state.inspectorHideNumbers = true;
        u64 start = PlatformNowMicroseconds(), now = 1;
        do {
            TestWindowFrame(&state, NULL, 0, now++);
            Prog_TakeCompletions(&state);
            P__TestPause();
        } while (!state.previewLines && PlatformNowMicroseconds() - start < Millions(5));
        // The text view is laid out in the first frame that has it, and its place is known from the next.
        TestWindowFrame(&state, NULL, 0, now++);
        TestWindowFrame(&state, NULL, 0, now++);
        R_Rect rect = state.inspectorTextRect;
        Check(state.previewLines && rect.x1 > rect.x0);
        f32 pitch = floorf((f32)Prog_InspectorFontSize + 6.8f + 0.5f), cell = R_TextWidth(&TestUIFont, S("M"));
        f32 x = rect.x0 + floorf(9.6f + 0.5f), y = rect.y0 + floorf(13.6f + 0.5f) + pitch * 0.5f;
        PlatformEvent press[] = { { .kind = PlatformEventKind_MouseMove, .x = x + 0.1f * cell, .y = y }, { .kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = x + 0.1f * cell, .y = y } };
        PlatformEvent drag[] = { { .kind = PlatformEventKind_MouseMove, .x = x + 5.1f * cell, .y = y } };
        PlatformEvent release[] = { { .kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = x + 5.1f * cell, .y = y } };
        TestWindowFrame(&state, press, 2, now++);
        TestWindowFrame(&state, drag, 1, now++);
        TestWindowFrame(&state, release, 1, now++);
        String selected = {0};
        Check(Prog_InspectorSelection(&state, &arena, &selected) && StrIsEqual(selected, S("hello")));
        PlatformEvent copy[] = { { .kind = PlatformEventKind_KeyDown, .key = Key_C, .mods = KeyMod_Control } };
        TestWindowFrame(&state, copy, 1, now++);
        Check(StrIsEqual(TestClipboard, S("hello")));
        // A double click on "world" selects the word, and a third click its line.
        PlatformEvent click[] = { { .kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = x + 8.1f * cell, .y = y }, { .kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = x + 8.1f * cell, .y = y } };
        TestWindowFrame(&state, click, 2, now++);
        TestWindowFrame(&state, click, 2, now++);
        Check(Prog_InspectorSelection(&state, &arena, &selected) && StrIsEqual(selected, S("world")));
        TestWindowFrame(&state, click, 2, now++);
        Check(Prog_InspectorSelection(&state, &arena, &selected) && StrIsEqual(selected, S("hello world\n")));
        // Ctrl+A after a press on the text selects all of it; with the filter on, only the lines it shows are copied.
        PlatformEvent all[] = { { .kind = PlatformEventKind_KeyDown, .key = Key_A, .mods = KeyMod_Control } };
        TestWindowFrame(&state, all, 1, now++);
        Check(Prog_InspectorSelection(&state, &arena, &selected) && StrIsEqual(selected, note.buffer));
        Prog__EditReplace(&state.inspectorFilter, (const u8 *)"second", 6);
        TestWindowFrame(&state, NULL, 0, now++);
        TestWindowFrame(&state, copy, 1, now++);
        Check(StrIsEqual(TestClipboard, S("second line\n")));
        TestBrowsingCommand(&state, Prog_CommandKind_InspectorRemoveFilter, 0);
        TestWindowFrame(&state, NULL, 0, now++);
        // Holding the pointer below the view while dragging scrolls it on, and the selection with it.
        PlatformEvent below[] = { { .kind = PlatformEventKind_MouseMove, .x = x, .y = rect.y1 + 200 } };
        TestWindowFrame(&state, press, 2, now++);
        TestWindowFrame(&state, below, 1, now++);
        for (u32 i = 0; i < 20; ++i) TestWindowFrame(&state, NULL, 0, now++);
        u32 inView = (u32)((rect.y1 - rect.y0) / pitch);
        Check(inView + 2 < state.previewLineCount && state.previewCaret > state.previewLines[inView + 2]);
        TestWindowFrame(&state, release, 1, now++);
        // A press outside the text drops it.
        PlatformEvent away[] = { { .kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = rect.x0 - 40, .y = y }, { .kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = rect.x0 - 40, .y = y } };
        TestWindowFrame(&state, away, 2, now++);
        Check(!Prog_InspectorSelection(&state, &arena, &selected));
    }
    Prog_Release(&state);
    TestRemoveTree(&arena, fixture, S("Selecting"));
    ArenaRelease(&arena);
}

// Splitting makes a second panel with its own listing; a command from a panel's UI makes it active, and closing a panel's only tab closes the panel.
function void TestPanels(TestFileFixture *fixture)
{
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (!state.jobs.initialized) return;
    Check(TestWaitDirectory(&state));
    u64 first = state.panels[0].id;
    TestBrowsingCommand(&state, Prog_CommandKind_SplitRight, 0);
    Check(state.panelCount == 2 && state.activePanel == 1 && state.currentPanel == 1 && StrIsEqual(state.directory.path, fixture->path));
    Check(state.splits[0].kind == Prog_SplitKind_Horizontal && Prog__FirstPanelOf(&state, 0) == first);
    u64 empty = TestEntryIndex(&state, S("Empty"));
    Check(empty != UINT64_MAX);
    Prog_OpenRow(&state, empty);
    Check(TestWaitDirectory(&state) && TestEndsWith(state.directory.path, S("/Empty")));
    // The first panel kept its folder.
    ArenaClear(&state.frameArena);
    Prog_Command *select = Prog_QueueCommand(&state, Prog_CommandKind_ScrollSelectFirst, 0, 0, 0);
    select->panel = first;
    Prog_ProcessCommands(&state);
    Check(state.activePanel == 0 && StrIsEqual(state.directory.path, fixture->path) && state.hasFocus);
    // The panel the pointer moves over takes the keyboard; one Tab switched to keeps it while the pointer rests, until it moves again.
    TestPanelsFrame(&state, true, 1100, 400, 1);
    Check(state.activePanel == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectNext, 0);
    Check(state.activePanel == 0);
    TestPanelsFrame(&state, false, 0, 0, 2);
    TestPanelsFrame(&state, true, 1100, 400, 3);
    Check(state.activePanel == 0);
    TestPanelsFrame(&state, true, 1101, 400, 4);
    Check(state.activePanel == 1);
    // Unless only clicks switch panels.
    Prog_ApplySettings(&state, S("hover.focus = off"));
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectNext, 0);
    TestPanelsFrame(&state, true, 1102, 400, 5);
    Check(state.activePanel == 0 && state.clickToFocus);
    Prog_ApplySettings(&state, S("hover.focus = on"));
    // The inspector lists the folder in focus in a panel of its own, which no split holds and the session leaves out.
    // A click in it gives it the keyboard; hiding the inspector closes it and gives the keyboard back.
    {
        state.previewShown = true;
        Prog_SelectRow(&state, TestEntryIndex(&state, S("Folder2")), 0);
        Prog_ProcessCommands(&state);
        u64 owner = state.panels[state.activePanel].id;
        u32 inner = Prog_InspectorPanel(&state, owner, false);
        Check(inner != UINT32_MAX && state.previewFolder);
        u64 start = PlatformNowMicroseconds();
        while (inner != UINT32_MAX && state.panels[inner].directoryJob && PlatformNowMicroseconds() - start < Millions(5)) {
            P__TestPause();
            Prog_TakeCompletions(&state);
        }
        Check(inner != UINT32_MAX && state.panels[inner].directory.count == 1 && TestEndsWith(state.panels[inner].directory.path, S("/Folder2")));
        if (inner != UINT32_MAX) {
            // Its contents size is its zoom.
            TestBrowsingCommand(&state, Prog_CommandKind_InspectorIncreaseContentsSize, 0);
            Check(state.panels[inner].zoom == Prog_ZoomList && state.zoom == 0);
            TestBrowsingCommand(&state, Prog_CommandKind_InspectorDecreaseContentsSize, 0);
            Check(state.panels[inner].zoom == 0);
            ArenaClear(&state.frameArena);
            Prog_QueueCommand(&state, Prog_CommandKind_ScrollSelectFirst, 0, 0, 0)->panel = state.panels[inner].id;
            Prog_ProcessCommands(&state);
            Check(state.activePanel == inner && state.hasFocus && StrIsEqual(state.previewPath, Prog_JoinPath(&state.frameArena, fixture->path, S("Folder2"))));
            String session = Prog_SessionText(&state.frameArena, &state);
            u32 panels = 0;
            for (u64 i = 0; i + 6 <= session.length; ++i) panels += StrIsEqual(StrFromTo(session, i, (i64)i + 6), S("panel ")) ? 1 : 0;
            Check(panels == state.panelCount - 1);
        }
        state.previewShown = false;
        Prog_ProcessCommands(&state);
        Check(Prog_InspectorPanel(&state, owner, false) == UINT32_MAX && state.panels[state.activePanel].id == owner);
    }
    // The palette lists its groups, opens one from its title, finds commands across them by their label and description, and runs the highlighted one.
    {
        Prog_PaletteRow matches[Prog_MaxPaletteRows];
        TestBrowsingCommand(&state, Prog_CommandKind_CommandPalette, 0);
        u32 groups = Prog_PaletteMatches(&state, matches);
        Check(state.paletteOpen && groups >= 5 && matches[0].group == Prog_CommandGroup_Quick && matches[0].kind == Prog_CommandKind_None);
        // Every command with a label that does anything here is in exactly one group.
        state.paletteExpanded = (u16)((1u << Prog_CommandGroup_Count) - 1);
        u32 all = Prog_PaletteMatches(&state, matches), seen[Prog_CommandKind_Count] = {0};
        for (u32 i = 0; i < all; ++i) ++seen[matches[i].kind];
        for (u32 kind = Prog_CommandKind_None + 1; kind < Prog_CommandKind_Count; ++kind) {
            Check(seen[kind] == (Prog_Commands[kind].label.length && Prog_CommandApplies((Prog_CommandKind)kind) ? 1u : 0u));
        }
        // A star moves a command into Quick, at its end, and out of its own group; taken out again, it goes back.
        TestBrowsingCommand(&state, Prog_CommandKind_PaletteQuickToggle, Prog_CommandKind_ToggleFullscreen);
        u32 starred = Prog_PaletteMatches(&state, matches), quickAt = UINT32_MAX, inQuick = 0;
        for (u32 i = 0; i < starred; ++i) {
            if (matches[i].kind == Prog_CommandKind_ToggleFullscreen) quickAt = matches[i].group == Prog_CommandGroup_Quick ? i : UINT32_MAX - 1;
            inQuick += matches[i].group == Prog_CommandGroup_Quick && matches[i].kind ? 1u : 0u;
        }
        Check(starred == all && quickAt == inQuick && Prog_IsQuick(&state, Prog_CommandKind_ToggleFullscreen));
        TestBrowsingCommand(&state, Prog_CommandKind_PaletteQuickToggle, Prog_CommandKind_ToggleFullscreen);
        Check(!Prog_IsQuick(&state, Prog_CommandKind_ToggleFullscreen) && Prog_PaletteMatches(&state, matches) == all);
        state.paletteExpanded = 0;
        TestBrowsingCommand(&state, Prog_CommandKind_PaletteRun, 0);
        Check(state.paletteOpen && Prog_PaletteMatches(&state, matches) > groups && matches[1].group == Prog_CommandGroup_Quick && matches[1].kind != Prog_CommandKind_None);
        TestBrowsingCommand(&state, Prog_CommandKind_PaletteRun, 0);
        Check(state.paletteOpen && Prog_PaletteMatches(&state, matches) == groups);
        for (const char *c = "split b"; *c; ++c) Prog__EditInsert(&state.palette, (rune)*c);
        Check(Prog_PaletteMatches(&state, matches) >= 1 && matches[0].kind == Prog_CommandKind_SplitBottom);
        state.palette = (Prog_TextEdit){0};
        for (const char *c = "split r"; *c; ++c) Prog__EditInsert(&state.palette, (rune)*c);
        u32 found = Prog_PaletteMatches(&state, matches);
        Check(found >= 1 && matches[0].kind == Prog_CommandKind_SplitRight);
        u32 panels = state.panelCount;
        TestBrowsingCommand(&state, Prog_CommandKind_PaletteRun, UINT64_MAX);
        Check(!state.paletteOpen && state.panelCount == panels + 1);
        TestBrowsingCommand(&state, Prog_CommandKind_TabClose, 0);
        Check(state.panelCount == panels && state.activePanel == 0);
    }
    // Folders measured once have their totals at once when shown again, while they are measured anew.
    {
        u64 start = PlatformNowMicroseconds();
        while ((state.measureJob || !state.sizes.header.count) && PlatformNowMicroseconds() - start < Millions(5)) {
            Prog_TakeCompletions(&state);
            P__TestPause();
        }
        Check(state.sizes.header.count > 0);
        u64 empty = TestEntryIndex(&state, S("Empty"));
        Prog_OpenRow(&state, empty);
        Check(TestWaitDirectory(&state));
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        u64 folder = TestEntryIndex(&state, S("Folder2"));
        Check(folder != UINT64_MAX && state.directory.entries[folder].measured);
        start = PlatformNowMicroseconds();
        while (state.measureJob && PlatformNowMicroseconds() - start < Millions(5)) {
            Prog_TakeCompletions(&state);
            P__TestPause();
        }
        Check(!state.directory.entries[TestEntryIndex(&state, S("Folder2"))].remeasure);
    }
    // The keyboard moves to the panel beside the active one in the last layout.
    state.panelRect = (R_Rect){ 0, 0, 500, 800 };
    state.panels[1].panelRect = (R_Rect){ 501, 0, 1000, 800 };
    TestBrowsingCommand(&state, Prog_CommandKind_PanelRight, 0);
    Check(state.activePanel == 1 && TestEndsWith(state.directory.path, S("/Empty")));
    TestBrowsingCommand(&state, Prog_CommandKind_PanelUp, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelLeft, 0);
    Check(state.activePanel == 0 && StrIsEqual(state.directory.path, fixture->path));
    TestBrowsingCommand(&state, Prog_CommandKind_SplitBottom, 0);
    Check(state.panelCount == 3 && state.activePanel == 2 && state.splits[0].kind == Prog_SplitKind_Horizontal);
    TestBrowsingCommand(&state, Prog_CommandKind_TabClose, 0);
    Check(state.panelCount == 2 && state.splits[0].kind == Prog_SplitKind_Horizontal && StrIsEqual(state.directory.path, fixture->path));
    TestBrowsingCommand(&state, Prog_CommandKind_TabClose, 0);
    Check(state.panelCount == 1 && state.splits[0].kind == Prog_SplitKind_Panel && TestEndsWith(state.directory.path, S("/Empty")));
    // The last panel's last tab closes the window.
    u32 closes = TestWindowCalls[2];
    TestBrowsingCommand(&state, Prog_CommandKind_TabClose, 0);
    Check(state.panelCount == 1 && state.tabCount == 1 && TestWindowCalls[2] == closes + 1);
    // Dragged tabs: within their strip they change place, into another panel they move there, and beside a panel they make a new one; Ctrl copies.
    u64 alone = state.panels[0].id;
    Prog_OpenTab(&state, fixture->path, 1, false);
    Check(state.tabCount == 2);
    Prog_DropTab(&state, (Prog_TabDropTarget){ .kind = Prog_TabDrop_Strip, .panel = alone, .slot = 2, .fromPanel = alone, .fromSlot = 0 });
    Check(state.tabCount == 2 && StrIsEqual(Prog_TabPath(&state, 0), fixture->path) && TestEndsWith(Prog_TabPath(&state, 1), S("/Empty")));
    Prog_DropTab(&state, (Prog_TabDropTarget){ .kind = Prog_TabDrop_Right, .panel = alone, .fromPanel = alone, .fromSlot = 1 });
    Check(TestWaitDirectory(&state) && state.panelCount == 2 && state.activePanel == 1 && TestEndsWith(state.directory.path, S("/Empty")));
    Check(state.panels[0].tabCount == 1);
    // The moved tab keeps its history, so Back still leads where it came from.
    Check(state.history.header.count == 2 && state.historyAt == 1 && StrIsEqual(state.history.v[0], fixture->path));
    u64 right = state.panels[1].id;
    Prog_DropTab(&state, (Prog_TabDropTarget){ .kind = Prog_TabDrop_Into, .panel = right, .fromPanel = alone, .fromSlot = 0, .copy = true });
    Check(TestWaitDirectory(&state) && state.panelCount == 2 && state.activePanel == 1 && state.tabCount == 2 && state.panels[0].tabCount == 1);
    // The session records the layout and tabs, and a fresh state rebuilds them; broken text changes nothing.
    {
        Arena sessionArena = {0};
        // With a panel's columns, its folders after its files and its inspector's own view, which come back too.
        Prog_EditColumns(&state, Prog_CommandKind_ColumnToggle, Prog_ColumnKind_Created, 0);
        state.folderSort = Prog_FolderSort_Last;
        state.inspectorView = (Prog_ViewSettings){ .zoom = Prog_ZoomColumns, .sort = Prog_Sort_Size, .descending = true, .columns = { .count = 2, .kinds = { Prog_ColumnKind_Name, Prog_ColumnKind_Size } } };
        // And its tabs' filters, which their first listings keep.
        MemCopy(state.filter.text, "al pha", 6);
        state.filter.length = 6;
        state.hideFiles = true;
        // And the sidebar's width, dragged past its widest, which it is held to.
        Prog_QueueCommand(&state, Prog_CommandKind_ResizeSidebar, 0, 0, 0)->target = 900000;
        Prog_ProcessCommands(&state);
        Check(state.sidebarWidth == Prog_SidebarMax);
        String text = Prog_SessionText(&sessionArena, &state);
        Prog_State restored = {0};
        Check(Prog_Init(&restored, fixture->path, NULL, NULL));
        Check(!Prog_RestoreSession(&restored, S("session 1\nsplit 0 2 5 6 0 0\n")) && restored.panelCount == 1);
        Check(Prog_RestoreSession(&restored, text) && TestWaitDirectory(&restored));
        Check(restored.panelCount == state.panelCount && restored.activePanel == state.activePanel && restored.splits[0].kind == state.splits[0].kind);
        Check(restored.sidebarWidth == Prog_SidebarMax);
        Check(StrIsEqual(Prog_SessionText(&sessionArena, &restored), text));
        Check(StrIsEqual(Prog_EditText(&restored.filter), S("al pha")) && restored.hideFiles && !restored.filterSticky);
        Check(restored.columnSet.count == 6 && restored.folderSort == Prog_FolderSort_Last && restored.inspectorView.zoom == Prog_ZoomColumns && restored.inspectorView.columns.count == 2);
        Prog_Release(&restored);
        ArenaRelease(&sessionArena);
    }
    // Moving a panel's only tab away closes that panel.
    Prog_DropTab(&state, (Prog_TabDropTarget){ .kind = Prog_TabDrop_Strip, .panel = right, .slot = 0, .fromPanel = alone, .fromSlot = 0 });
    Check(TestWaitDirectory(&state) && state.panelCount == 1 && state.tabCount == 3 && StrIsEqual(Prog_TabPath(&state, 0), fixture->path));
    Prog_Release(&state);
}

// Settings override colors, the scale and hotkeys; a rebound command loses its defaults and bad lines are skipped.
function void TestSettings(void)
{
    Prog_State state = {0};
    Prog_ApplySettings(&state, S("# comment\nscale = 1.25\ncolor.accent = #ff8000\ncolor.nothing = #000000\ncolor.text = blue\n"
                                 "hotkey.SplitRight = Ctrl+Alt+R, F6\nhotkey.Refresh = none\nhotkey.Nonsense = Ctrl+Q\n"
                                 "glass.popup = #10203080\nglass.blur = 12.5\nglass.window = nope\nglass.backdrop = Mica\n"));
    Check(state.uiScale > 1.24f && state.uiScale < 1.26f);
    Check(Prog_ThemeGlassUser.menu.b > 0.18f && Prog_ThemeGlassUser.menu.a > 0.5f && Prog_ThemeGlassUser.menu.a < 0.51f && Prog_GlassBlur == 12.5f);
    Check(Prog_ThemeGlassUser.window.a == Prog_ThemeGlass.window.a && state.glassBackdrop == PlatformBackdrop_Mica);
    Check(Prog_ThemeUser.accent.r == 1 && Prog_ThemeUser.accent.b == 0 && Prog_ThemeUser.text.r == Prog_ThemeCustom.text.r);
    // How the lists look, read back as the options show them; the defaults are {0}'s.
    {
        Prog_State look = {0};
        Check(Prog_OptionValue(&look, Prog_Option_Spacing) == 4 && Prog_OptionValue(&look, Prog_Option_InactiveOpacity) == 60 && Prog_OptionValue(&look, Prog_Option_RoundCorners));
        Prog_ApplySettings(&look, S("spacing = 7\ncorners = square\nrows.alternate = on\ninactive.opacity = 85\ntheme = light\nspacing = 99\n"));
        Check(Prog_OptionValue(&look, Prog_Option_Spacing) == 7 && !Prog_OptionValue(&look, Prog_Option_RoundCorners) && look.alternatingRows);
        Check(Prog_OptionValue(&look, Prog_Option_InactiveOpacity) == 85 && look.scheme == 2 && Prog_ThemeUser.background.r == Prog_ThemeLight.background.r);
        // The pointer's options and their defaults.
        Check(Prog_OptionValue(&look, Prog_Option_ActOnPress) && Prog_OptionValue(&look, Prog_Option_UnderlineOnHover) && Prog_OptionValue(&look, Prog_Option_Overscroll));
        Prog_ApplySettings(&look, S("click.onPress = off\nclick.single = on\nhover.underline = off\nscroll.over = off\nscroll.reverse = on\nclick.doubleToParent = on\n"));
        Check(look.actOnRelease && look.singleClick && look.underlineAlways && look.noOverscroll && look.reverseScroll && look.doubleClickToParent && !look.hoverToSelect);
        // Emptying the bin asks first unless turned off.
        Check(Prog_OptionValue(&look, Prog_Option_ConfirmEmptyTrash));
        Prog_ApplySettings(&look, S("trash.confirm = off\n"));
        Check(look.noTrashConfirm && !Prog_OptionValue(&look, Prog_Option_ConfirmEmptyTrash));
        Check(Prog_OptionValue(&look, Prog_Option_HighlightCode));
        Prog_ApplySettings(&look, S("inspector.highlight = false\n"));
        Check(look.noHighlight && !Prog_OptionValue(&look, Prog_Option_HighlightCode));
        // Names without their extensions, a folder's kept whole, and a dot file's too.
        Prog_ApplySettings(&look, S("files.extensions = off\nfiles.hidden = on\n"));
        Prog_Entry file = { .file = { .name = S("Report.final.pdf"), .kind = P_FileKind_File } }, folder = { .file = { .name = S("v1.2"), .kind = P_FileKind_Directory } };
        Prog_Entry dot = { .file = { .name = S(".gitignore"), .kind = P_FileKind_File } };
        Check(StrIsEqual(Prog_EntryName(&look, &file), S("Report.final")) && StrIsEqual(Prog_EntryName(&look, &folder), S("v1.2")) && StrIsEqual(Prog_EntryName(&look, &dot), S(".gitignore")));
        Check(look.showHidden && Prog_OptionValue(&look, Prog_Option_ShowHiddenFiles));
        // Thumbnails and folder sizes by the kind of drive: by default local thumbnails, and sizes on solid-state drives only.
        Check(Prog_ShowsThumbnails(&look) && Prog_MeasuresFolders(&look) && Prog_OptionValue(&look, Prog_Option_LoadThumbnails) == 1);
        look.directory.driveKind = P_DriveKind_Network;
        Check(!Prog_ShowsThumbnails(&look) && !Prog_MeasuresFolders(&look));
        Prog_ApplySettings(&look, S("thumbnails = always\nfolders.size.network = on\nfolders.size.ssd = off\ncheckboxes = on\n"));
        Check(look.showCheckboxes && Prog_OptionValue(&look, Prog_Option_ShowCheckboxes));
        Check(Prog_ShowsThumbnails(&look) && Prog_MeasuresFolders(&look) && Prog_OptionValue(&look, Prog_Option_LoadThumbnails) == 2);
        look.directory.driveKind = P_DriveKind_SolidState;
        Check(!Prog_MeasuresFolders(&look));
        Prog_ApplySettings(&look, S("font = Consolas\ninspector.font = Cascadia Mono\n"));
        Check(StrIsEqual(StrMake(look.font.text, look.font.length), S("Consolas")) && StrIsEqual(StrMake(look.inspectorFont.text, look.inspectorFont.length), S("Cascadia Mono")));
        Prog_ApplySettings(&look, S(""));
    }
    const Prog_Hotkey *hotkey = Prog_FindHotkey(&state, Prog_HotkeyScope_Browser, Key_None, Key_R, KeyMod_Control | KeyMod_Alt);
    Check(hotkey && hotkey->command == Prog_CommandKind_SplitRight);
    hotkey = Prog_FindHotkey(&state, Prog_HotkeyScope_Browser, Key_None, Key_F6, 0);
    Check(hotkey && hotkey->command == Prog_CommandKind_SplitRight);
    Check(!Prog_FindHotkey(&state, Prog_HotkeyScope_Browser, Key_None, Key_S, KeyMod_Control | KeyMod_Shift) && !Prog_FindHotkey(&state, Prog_HotkeyScope_Browser, Key_None, Key_F5, 0));
    hotkey = Prog_FindHotkey(&state, Prog_HotkeyScope_Browser, Key_None, Key_S, KeyMod_Control);
    Check(hotkey && hotkey->command == Prog_CommandKind_SplitBottom);
    Arena arena = {0};
    Check(StrIsEqual(Prog_HotkeyText(&arena, &state, Prog_CommandKind_SplitRight), S("Ctrl+Alt+R")));
    Check(Prog_SettingsTemplate(&arena, &state).length > 0);
    Prog_ApplySettings(&state, (String){0});
    Check(state.uiScale == 1 && Prog_ThemeUser.accent.r == Prog_ThemeCustom.accent.r && Prog_FindHotkey(&state, Prog_HotkeyScope_Browser, Key_None, Key_F5, 0));
    // The mouse's side buttons go back and forward.
    hotkey = Prog_FindHotkey(&state, Prog_HotkeyScope_Browser, Key_None, Key_MouseX1, 0);
    Check(hotkey && hotkey->command == Prog_CommandKind_Back);
    hotkey = Prog_FindHotkey(&state, Prog_HotkeyScope_Browser, Key_None, Key_MouseX2, 0);
    Check(hotkey && hotkey->command == Prog_CommandKind_Forward);
    Check(Prog_ThemeGlassUser.menu.a == Prog_ThemeGlass.menu.a && Prog_GlassBlur == 24 && state.glassBackdrop == PlatformBackdrop_Acrylic);
    ArenaRelease(&arena);
}

// A move is recorded, and undoing it brings the item back under its own name.
function void TestUndo(TestFileFixture *fixture)
{
    Arena arena = {0};
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (!state.jobs.initialized) return;
    Check(TestWaitDirectory(&state));
    String created = {0};
    Check(!P_CreateItem(&arena, fixture->path, S("UndoTarget"), true, &created).code);
    String plain = Prog_JoinPath(&arena, fixture->path, S("plain.txt"));
    Prog__FileJob(&state, Prog_JobKind_Transfer, &plain, 1, Prog_JoinPath(&arena, fixture->path, S("UndoTarget")), true, (String){0});
    u64 start = PlatformNowMicroseconds();
    while (!state.undo.header.count && PlatformNowMicroseconds() - start < Millions(5)) {
        Prog_TakeCompletions(&state);
        P__TestPause();
    }
    Check(state.undo.header.count == 1 && state.undo.v[0].kind == Prog_UndoKind_Move && TestHasFile(&arena, fixture, S("UndoTarget"), S("plain.txt")));
    Prog_UndoLast(&state);
    Check(state.undo.header.count == 0);
    start = PlatformNowMicroseconds();
    while (!TestHasFile(&arena, fixture, S(""), S("plain.txt")) && PlatformNowMicroseconds() - start < Millions(5)) {
        Prog_TakeCompletions(&state);
        P__TestPause();
    }
    Check(TestHasFile(&arena, fixture, S(""), S("plain.txt")) && !TestHasFile(&arena, fixture, S("UndoTarget"), S("plain.txt")));
    Prog_UndoLast(&state);
    Check(StrIsEqual(state.error, S("Nothing to undo")));
    // Redo moves it again, which Undo can then reverse once more.
    for (start = PlatformNowMicroseconds(); !state.redo.header.count && PlatformNowMicroseconds() - start < Millions(5);) {
        Prog_TakeCompletions(&state);
        P__TestPause();
    }
    Check(state.redo.header.count == 1);
    Prog_RedoLast(&state);
    for (start = PlatformNowMicroseconds(); !state.undo.header.count && PlatformNowMicroseconds() - start < Millions(5);) {
        Prog_TakeCompletions(&state);
        P__TestPause();
    }
    Check(state.undo.header.count == 1 && !state.redo.header.count && TestHasFile(&arena, fixture, S("UndoTarget"), S("plain.txt")));
    Prog_UndoLast(&state);
    for (start = PlatformNowMicroseconds(); !TestHasFile(&arena, fixture, S(""), S("plain.txt")) && PlatformNowMicroseconds() - start < Millions(5);) {
        Prog_TakeCompletions(&state);
        P__TestPause();
    }
    Check(TestHasFile(&arena, fixture, S(""), S("plain.txt")));
    Prog_UndoLast(&state);
    Check(StrIsEqual(state.error, S("Nothing to undo")));
    Check(TestWaitDirectory(&state));
    Prog_Release(&state);
    Check(P__TestRemovePath(fixture, S("UndoTarget"), true));
    ArenaRelease(&arena);
}

// Sidebar locations, and bookmarks and recents kept in a config folder inside the fixture.
function void TestSidebar(TestFileFixture *fixture)
{
    Arena arena = {0};
    P_ArrayPlace places = P_UserPlaces(&arena);
    Check(places.header.count >= 1 && places.v[0].home && StrIsEqual(places.v[0].name, Prog__FolderName(places.v[0].path)) && places.v[0].path.length);
    P_ArrayPlace drives = P_Drives(&arena);
    Check(drives.header.count >= 1);
    b32 sized = false;
    for (u64 i = 0; i < drives.header.count; ++i) sized |= drives.v[i].totalBytes > 0 && drives.v[i].freeBytes <= drives.v[i].totalBytes;
    Check(sized);
#if defined(__linux__)
    // A drive mounted under its UUID is named by its disk's size where the disk has no model; neither is made up for what is not a disk.
    Check(StrIsEqual(P__ShortSize(&arena, UINT64_C(3960000000000)), S("3.6 TB")) && StrIsEqual(P__ShortSize(&arena, 512), S("512.0 B")));
    Check(!P__NamedByUuid(&arena, S("not-a-uuid"), S("/dev/sda1")) && !P__DiskModel(&arena, S("/dev/not-a-disk")).length);
    // Options of Windows alone are not offered: files are never marked as the system's own here.
    Check(!Prog_OptionApplies(Prog_Option_ShowSystemFiles) && Prog_OptionApplies(Prog_Option_FontSize));
    // Nor the palette's command for them, and the bin is named as here: the Trash.
    Check(!Prog_CommandApplies(Prog_CommandKind_ToggleSystemFiles) && Prog_CommandApplies(Prog_CommandKind_EmptyTrash));
    Check(StrIsEqual(Prog_CommandLabel(Prog_CommandKind_EmptyTrash), S("Empty Trash")) && TestEndsWith(Prog_CommandTooltip(Prog_CommandKind_EmptyTrash), S("in the Trash.")));
    Check(StrIsEqual(Prog_CommandLabel(Prog_CommandKind_Undo), S("Undo")));
    // Only a drive on a local device is looked into for Windows; /proc's source is no device.
    Check(StrIsEqual(P__MountPoint(&arena, S("/mnt/My\\040Disk")), S("/mnt/My Disk")) && !P__LocalMount(&arena, S("/proc")));
#endif

    // Bookmarks and recents at or inside a renamed folder follow it; a name that only starts the same stays.
    {
        Prog_State moved = {0};
        String marks[] = { S("/a/Downloads"), S("/a/Other"), S("/a/DownloadsX") }, recent[] = { S("/a/Downloads/file.txt"), S("/a/Downloadsy") };
        Prog_RecentRank ranks[2] = { { .score = 2, .ordinal = 2 }, { .score = 1, .ordinal = 1 } };
        Prog__SetList(&moved.bookmarkArena, &moved.bookmarks, marks, 3);
        Prog__SetRecents(&moved, recent, ranks, 2);
        String from = S("/a/Downloads"), to = S("/a/Downloads1");
        Prog__FollowMoved(&moved, &from, &to, 1);
        Check(StrIsEqual(moved.bookmarks.v[0], S("/a/Downloads1")) && StrIsEqual(moved.bookmarks.v[1], S("/a/Other")) && StrIsEqual(moved.bookmarks.v[2], S("/a/DownloadsX")));
        Check(StrIsEqual(moved.recents.v[0], S("/a/Downloads1/file.txt")) && StrIsEqual(moved.recents.v[1], S("/a/Downloadsy")));
        ArenaRelease(&moved.bookmarkArena);
        ArenaRelease(&moved.recentArena);
    }

    String file = Prog_JoinPath(&arena, fixture->path, S("settings.txt"));
    String text = {0};
    Check(!P_ReadFile(&arena, file, &text));
    Check(!P_WriteFile(&arena, file, S("first\nsecond\n")).code);
    Check(!P_WriteFile(&arena, file, S("replaced\n")).code);
    Check(P_ReadFile(&arena, file, &text) && StrIsEqual(text, S("replaced\n")));
    Check(P__TestRemovePath(fixture, S("settings.txt"), false));

    Check(P__TestMakeDirectory(fixture, S("Config")));
    String config = Prog_JoinPath(&arena, fixture->path, S("Config"));
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (state.jobs.initialized) {
        Prog_LoadSettings(&state, config);
        Check(TestWaitDirectory(&state));
        // A first run starts with the user's folders bookmarked.
        u64 seeded = state.bookmarks.header.count;
        // The folder it starts on is not counted as opened.
        Check(seeded <= 4 && state.recents.header.count == 0);
        TestBrowsingCommand(&state, Prog_CommandKind_BookmarksToggle, 0);
        Check(state.bookmarks.header.count == seeded + 1 && Prog_IsBookmarked(&state, fixture->path));
        u64 empty = TestEntryIndex(&state, S("Empty"));
        Check(empty != UINT64_MAX);
        Prog_OpenRow(&state, empty);
        Check(TestWaitDirectory(&state));
        Check(state.recents.header.count == 1 && TestEndsWith(state.recents.v[0], S("/Empty")) && !state.recentRanks[0].file);
        ArenaClear(&state.frameArena);
        Prog_Command *open = Prog_QueueCommand(&state, Prog_CommandKind_SidebarOpen, Prog_Group_Bookmarks, 1, 0);
        open->target = seeded;
        Prog_ProcessCommands(&state);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, fixture->path));
        // Saves run in jobs; releasing the state waits for them.
        Prog_Release(&state);
        state = (Prog_State){0};
        Check(Prog_Init(&state, fixture->path, NULL, NULL));
        Prog_LoadSettings(&state, config);
        Check(state.bookmarks.header.count == seeded + 1 && Prog_IsBookmarked(&state, fixture->path));
        // Opened as often, the later ranks first.
        Check(state.recents.header.count == 2 && StrIsEqual(state.recents.v[0], fixture->path) && TestEndsWith(state.recents.v[1], S("/Empty")));
        TestBrowsingCommand(&state, Prog_CommandKind_RemoveBookmark, seeded);
        Check(state.bookmarks.header.count == seeded && !Prog_IsBookmarked(&state, fixture->path));
        // Dropped folders are pinned where the drop points; one already pinned moves there.
        String pins[2] = { S("/pinned/first"), fixture->path };
        ArrayStr dropped = { .header = { .count = 2 }, .v = pins };
        Prog_PinBookmarks(&state, &dropped, 0);
        Check(state.bookmarks.header.count == seeded + 2 && StrIsEqual(state.bookmarks.v[0], pins[0]) && StrIsEqual(state.bookmarks.v[1], pins[1]));
        dropped.header.count = 1;
        Prog_PinBookmarks(&state, &dropped, state.bookmarks.header.count);
        Check(state.bookmarks.header.count == seeded + 2 && StrIsEqual(state.bookmarks.v[seeded + 1], pins[0]) && StrIsEqual(state.bookmarks.v[0], pins[1]));
        Check(TestWaitDirectory(&state));
        // Up passes through the folder it goes to, which does not count as opening it.
        Prog_OpenRow(&state, TestEntryIndex(&state, S("Empty")));
        Check(TestWaitDirectory(&state));
        TestBrowsingCommand(&state, Prog_CommandKind_Up, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, fixture->path));
        Check(TestEndsWith(state.recents.v[0], S("/Empty")) && StrIsEqual(state.recents.v[1], fixture->path) && state.recentRanks[1].score == Prog_RecentScoreNew);
        Prog_Release(&state);
    }
    Check(P__TestRemovePath(fixture, S("Config/Bookmarks.txt"), false));
    Check(P__TestRemovePath(fixture, S("Config/Recents.txt"), false));
    Check(P__TestRemovePath(fixture, S("Config/Session.txt"), false));
    Check(P__TestRemovePath(fixture, S("Config"), true));
    ArenaRelease(&arena);
}

#if defined(__linux__)
function void TestWriteText(Arena *arena, TestFileFixture *fixture, String relative, String text, u32 mode)
{
    Check(!P_WriteFile(arena, Prog_JoinPath(arena, fixture->path, relative), text).code);
    Check(chmod(P__FilePathCString(arena, Prog_JoinPath(arena, fixture->path, relative)), mode) == 0);
}

// Reads `relative` once something has written it.
function String TestWaitText(Arena *arena, TestFileFixture *fixture, String relative)
{
    String text = {0};
    for (u64 start = PlatformNowMicroseconds(); PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) {
        if (P_ReadFile(arena, Prog_JoinPath(arena, fixture->path, relative), &text) && text.length && text.value[text.length - 1] == '\n') break;
    }
    return text;
}

// Desktop entries, mimeapps.list and launching, against entries, a terminal and an opener made in the fixture.
function void TestApps(TestFileFixture *fixture)
{
    Arena arena = {0};
    String root = Prog_JoinPath(&arena, fixture->path, S("Apps"));
    Check(P__TestMakeDirectory(fixture, S("Apps")));
    Check(P__TestMakeDirectory(fixture, S("Apps/data")));
    Check(P__TestMakeDirectory(fixture, S("Apps/data/applications")));
    Check(P__TestMakeDirectory(fixture, S("Apps/data/applications/vendor")));
    Check(P__TestMakeDirectory(fixture, S("Apps/config")));
    Check(P__TestMakeDirectory(fixture, S("Apps/bin")));
    String opener = Prog_JoinPath(&arena, root, S("bin/opener"));
    TestWriteText(&arena, fixture, S("Apps/data/applications/test.desktop"),
                  StrFormat(&arena, "[Desktop Entry]\nType=Application\nName=Test Opener\nExec=%S --flag \"two words\" 100%%%% %%F\nMimeType=inode/directory;text/plain;image/png;\n", opener), 0600);
    TestWriteText(&arena, fixture, S("Apps/data/applications/vendor/other.desktop"),
                  S("[Desktop Entry]\nType=Application\nName=Another\nExec=other %u\nMimeType=inode/directory;\n"), 0600);
    TestWriteText(&arena, fixture, S("Apps/data/applications/hidden.desktop"),
                  S("[Desktop Entry]\nType=Application\nName=Hidden\nExec=hidden %f\nHidden=true\nMimeType=inode/directory;\n"), 0600);
    TestWriteText(&arena, fixture, S("Apps/data/applications/helper.desktop"),
                  S("[Desktop Entry]\nType=Application\nName=Helper\nExec=helper %f\nNoDisplay=true\nMimeType=inode/directory;\n"), 0600);
    TestWriteText(&arena, fixture, S("Apps/config/mimeapps.list"), S("[Default Applications]\ninode/directory=missing.desktop;vendor-other.desktop;\ntext/plain=test.desktop;\n"), 0600);
    TestWriteText(&arena, fixture, S("Apps/bin/opener"), S("#!/bin/sh\n{ pwd; printf '%s|' \"$@\"; echo; } > \"${0%/*}/../opened.tmp\" && /bin/mv \"${0%/*}/../opened.tmp\" \"${0%/*}/../opened\"\n"), 0700);
    TestWriteText(&arena, fixture, S("Apps/bin/kitty"), S("#!/bin/sh\n{ pwd; } > \"${0%/*}/../terminal.tmp\" && /bin/mv \"${0%/*}/../terminal.tmp\" \"${0%/*}/../terminal\"\n"), 0700);

    const char *names[] = { "XDG_DATA_HOME", "XDG_DATA_DIRS", "XDG_CONFIG_HOME", "XDG_CONFIG_DIRS", "PATH", "TERMINAL" };
    String saved[CArrayCount(names)] = {0};
    b32 had[CArrayCount(names)] = {0};
    for (u32 i = 0; i < CArrayCount(names); ++i) {
        const char *value = getenv(names[i]);
        had[i] = value != NULL;
        if (value) saved[i] = StrCopy(&arena, StrFromCStr(value));
    }
    String values[CArrayCount(names)] = {
        Prog_JoinPath(&arena, root, S("data")), Prog_JoinPath(&arena, root, S("none")), Prog_JoinPath(&arena, root, S("config")),
        Prog_JoinPath(&arena, root, S("none")), Prog_JoinPath(&arena, root, S("bin")), {0},
    };
    for (u32 i = 0; i < CArrayCount(names); ++i) {
        if (values[i].length) Check(setenv(names[i], P__FilePathCString(&arena, values[i]), 1) == 0);
        else Check(unsetenv(names[i]) == 0);
    }

    P__AppData data = {0};
    P__LoadApps(&data);
    P__App *apps[8];
    u64 count = P__AppsFor(&data, S("inode/directory"), false, apps, CArrayCount(apps));
    // The default comes first, past the missing one; hidden and NoDisplay entries are left out.
    Check(count == 2 && StrIsEqual(apps[0]->id, S("vendor-other.desktop")) && StrIsEqual(apps[1]->name, S("Test Opener")));
    P__App *textApps[8];
    Check(P__AppsFor(&data, S("text/plain"), false, textApps, CArrayCount(textApps)) == 1);

    String paths[2] = { Prog_JoinPath(&arena, fixture->path, S("plain.txt")), Prog_JoinPath(&arena, fixture->path, S("Zażółć 1.txt")) };
    ArrayStr args = {0};
    u64 used = P__ExpandExec(&arena, apps[0], paths, 2, &args);
    Check(used == 1 && args.header.count == 2 && StrIsEqual(args.v[0], S("other")) && TestEndsWith(args.v[1], S("/plain.txt")));
    Check(args.header.count == 2 && StrIsEqual(StrFromTo(args.v[1], 0, 8), S("file:///")));
    P__App *test = P__FindApp(&data, S("test.desktop"));
    Check(test != NULL);
    if (test) {
        used = P__ExpandExec(&arena, test, paths, 2, &args);
        Check(used == 2 && args.header.count == 6 && StrIsEqual(args.v[1], S("--flag")) && StrIsEqual(args.v[2], S("two words")) && StrIsEqual(args.v[3], S("100%")));
        Check(StrIsEqual(args.v[4], paths[0]) && StrIsEqual(args.v[5], paths[1]));
        Check(!P__LaunchApp(&arena, test, paths, 2, root).code);
        String opened = TestWaitText(&arena, fixture, S("Apps/opened"));
        Check(StrIsEqual(opened, StrFormat(&arena, "%S\n--flag|two words|100%%|%S|%S|\n", root, paths[0], paths[1])));
        // Opening a file starts its default application in the file's folder.
        Check(P__TestRemovePath(fixture, S("Apps/opened"), false));
        Check(!P__OpenPath(&arena, &data, paths[0]).code);
        opened = TestWaitText(&arena, fixture, S("Apps/opened"));
        Check(StrIsEqual(opened, StrFormat(&arena, "%S\n--flag|two words|100%%|%S|\n", fixture->path, paths[0])));
        // A patch is a kind of text/plain, so the text application opens it too.
        Check(P__TestRemovePath(fixture, S("Apps/opened"), false));
        String patch = Prog_JoinPath(&arena, fixture->path, S("fix.patch"));
        Check(!P__OpenPath(&arena, &data, patch).code);
        opened = TestWaitText(&arena, fixture, S("Apps/opened"));
        Check(StrIsEqual(opened, StrFormat(&arena, "%S\n--flag|two words|100%%|%S|\n", fixture->path, patch)));
        // A type that mimeapps.list chooses nothing for goes to xdg-open, which PATH lacks here, even though the application lists it.
        Check(P__OpenPath(&arena, &data, Prog_JoinPath(&arena, fixture->path, S("picture.png"))).code == ENOENT);
    }
    Check(!P__OpenTerminal(&arena, Prog_JoinPath(&arena, fixture->path, S("Empty"))).code);
    Check(StrIsEqual(TestWaitText(&arena, fixture, S("Apps/terminal")), StrFormat(&arena, "%S/Empty\n", fixture->path)));

    for (u32 i = 0; i < CArrayCount(names); ++i) {
        if (had[i]) Check(setenv(names[i], P__FilePathCString(&arena, saved[i]), 1) == 0);
        else Check(unsetenv(names[i]) == 0);
    }
    ArenaRelease(&data.arena);
    TestRemoveTree(&arena, fixture, S("Apps"));
    ArenaRelease(&arena);
}
#endif

// A bin's records read back as its items, with their own names, where they came from and when they went; a bin built in the fixture stands in
// for the real one, which the tests leave alone.
function void TestTrash(TestFileFixture *fixture)
{
    Arena arena = {0};
    String bin = Prog_JoinPath(&arena, fixture->path, S("Bin"));
    Check(P__TestMakeDirectory(fixture, S("Bin")));
#if defined(_WIN32)
    // A $I record of version 2: the size, the deletion time, then the path's length and the path in UTF-16.
    Check(P__TestMakeDirectory(fixture, S("Bin/S-1")));
    local_persist const wchar original[] = L"C:\\Old\\note.txt";
    u8 record[28 + sizeof(original)] = {0};
    i64 version = 2, size = 5, deleted = 116444736000000000LL + 10000000LL * 1000;
    u32 units = (u32)CArrayCount(original);
    MemCopy(record, &version, 8);
    MemCopy(record + 8, &size, 8);
    MemCopy(record + 16, &deleted, 8);
    MemCopy(record + 24, &units, 4);
    MemCopy(record + 28, original, sizeof(original));
    Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, bin, S("S-1/$IAB12.txt")), StrMake((const char *)record, sizeof(record))).code);
    Check(P__TestWriteFile(fixture, S("Bin/S-1/$RAB12.txt"), 5, false));
    P_ArrayTrashed items = P_ListTrash(&arena, bin, NULL);
    Check(items.header.count == 1 && items.header.count && StrIsEqual(items.v[0].name, S("note.txt")) && StrIsEqual(items.v[0].original, S("C:/Old")));
    Check(items.header.count && items.v[0].deletedTimeUnix == 1000 && TestEndsWith(items.v[0].file.name, S("/$RAB12.txt")));
    Check(P__TestRemovePath(fixture, S("Bin/S-1/$IAB12.txt"), false) && P__TestRemovePath(fixture, S("Bin/S-1/$RAB12.txt"), false));
    // Restoring puts an item back where its record says, making its folder, and drops the record; one whose place is taken stays.
    String back = P_NativePath(&arena, Prog_JoinPath(&arena, fixture->path, S("Gone/note.txt")));
    for (u32 pass = 0; pass < 2; ++pass) {
        wchar *wide = P__FilePathWide(&arena, back);
        u64 length = 0;
        while (wide[length]) ++length;
        u32 count = (u32)length + 1;
        u8 *again = ArenaPushArray(&arena, 28 + count * 2, u8);
        MemCopy(again, &version, 8);
        MemCopy(again + 8, &size, 8);
        MemCopy(again + 16, &deleted, 8);
        MemCopy(again + 24, &count, 4);
        MemCopy(again + 28, wide, count * 2);
        Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, bin, S("S-1/$ICD34.txt")), StrMake((const char *)again, 28 + count * 2)).code);
        Check(P__TestWriteFile(fixture, S("Bin/S-1/$RCD34.txt"), 5, false));
        P_FileError restored = P_RestoreTrash(&arena, bin, NULL, 0);
        Check(pass == 0 ? !restored.code && TestHasFile(&arena, fixture, S("Gone"), S("note.txt")) && !TestHasFile(&arena, fixture, S("Bin/S-1"), S("$ICD34.txt"))
                        : restored.code && TestHasFile(&arena, fixture, S("Bin/S-1"), S("$RCD34.txt")));
    }
    Check(P__TestRemovePath(fixture, S("Bin/S-1/$ICD34.txt"), false) && P__TestRemovePath(fixture, S("Bin/S-1/$RCD34.txt"), false) && P__TestRemovePath(fixture, S("Bin/S-1"), true));
    Check(P__TestRemovePath(fixture, S("Gone/note.txt"), false) && P__TestRemovePath(fixture, S("Gone"), true));
    Check(P_IsTrashFolder(S("D:/$RECYCLE.BIN/")) && !P_IsTrashFolder(S("D:/Recycle")));
#elif defined(__linux__)
    Check(P__TestMakeDirectory(fixture, S("Bin/files")) && P__TestMakeDirectory(fixture, S("Bin/info")));
    Check(P__TestWriteFile(fixture, S("Bin/files/note.txt"), 5, false));
    Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, bin, S("info/note.txt.trashinfo")), S("[Trash Info]\nPath=/Old%20Folder/note.txt\nDeletionDate=2026-01-02T03:04:05\n")).code);
    P_ArrayTrashed items = P_ListTrash(&arena, bin, NULL);
    Check(items.header.count == 1 && items.header.count && StrIsEqual(items.v[0].name, S("note.txt")) && StrIsEqual(items.v[0].original, S("/Old Folder")));
    Check(items.header.count && items.v[0].deletedTimeUnix > 0 && TestEndsWith(items.v[0].file.name, S("/files/note.txt")));
    Check(P__TestRemovePath(fixture, S("Bin/files/note.txt"), false) && P__TestRemovePath(fixture, S("Bin/info/note.txt.trashinfo"), false));
    // Restoring puts an item back where its info says, making its folder, and drops the info; one whose place is taken stays.
    String back = Prog_JoinPath(&arena, fixture->path, S("Gone/note.txt"));
    for (u32 pass = 0; pass < 2; ++pass) {
        Check(P__TestWriteFile(fixture, S("Bin/files/note.txt"), 5, false));
        Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, bin, S("info/note.txt.trashinfo")), StrFormat(&arena, "[Trash Info]\nPath=%S\nDeletionDate=2026-01-02T03:04:05\n", back)).code);
        P_FileError restored = P_RestoreTrash(&arena, bin, NULL, 0);
        Check(pass == 0 ? !restored.code && TestHasFile(&arena, fixture, S("Gone"), S("note.txt")) && !TestHasFile(&arena, fixture, S("Bin/info"), S("note.txt.trashinfo"))
                        : restored.code && TestHasFile(&arena, fixture, S("Bin/files"), S("note.txt")));
    }
    Check(P__TestRemovePath(fixture, S("Bin/files/note.txt"), false) && P__TestRemovePath(fixture, S("Bin/info/note.txt.trashinfo"), false));
    Check(P__TestRemovePath(fixture, S("Gone/note.txt"), false) && P__TestRemovePath(fixture, S("Gone"), true));
    Check(P__TestRemovePath(fixture, S("Bin/files"), true) && P__TestRemovePath(fixture, S("Bin/info"), true));
#endif
    Check(P__TestRemovePath(fixture, S("Bin"), true));
    ArenaRelease(&arena);
}

#if defined(__linux__)
// Where stb_image_write's writers put what they write, for pictures the tests make.
typedef struct TestSink {
    Arena *arena;
    StrBuilder out;
} TestSink;

function void TestSinkWrite(void *context, void *data, int size)
{
    TestSink *sink = context;
    StrBuildStr(sink->arena, &sink->out, StrMake((const char *)data, (u64)size));
}

// Thumbnails that installed thumbnailers make, through the cache Nautilus and Nemo share, kept inside the fixture here.
function void TestThumbnailers(TestFileFixture *fixture)
{
    Arena arena = {0};
    // The cache's names are the MD5 of the file's URI, as the thumbnail spec's own example has it, and its URIs are escaped as GLib does.
    Check(StrIsEqual(P__ThumbnailName(&arena, S("")), S("d41d8cd98f00b204e9800998ecf8427e.png")));
    Check(StrIsEqual(P__ThumbnailName(&arena, S("file:///home/jens/photos/me.png")), S("c6ee772d9e49320e97ec29a7eb5b1697.png")));
    Check(StrIsEqual(P__FileUri(&arena, S("/a b/Gold & Co (1).mp4")), S("file:///a%20b/Gold%20&%20Co%20(1).mp4")));
    // A cached picture is tagged with the file's URI and modification time, which the PNG keeps readable.
    local_persist const u8 dot[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
        0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    };
    String png = StrMake((const char *)dot, sizeof(dot)), keywords[] = { S("Thumb::URI"), S("Thumb::MTime") }, texts[] = { S("file:///x"), S("42") };
    String tagged = P__PngWithText(&arena, png, keywords, texts, 2);
    Check(StrIsEqual(P__PngText(tagged, S("Thumb::MTime")), S("42")) && StrIsEqual(P__PngText(tagged, S("Thumb::URI")), S("file:///x")));
    i32 width = 0, height = 0, channels = 0;
    u8 *pixels = stbi_load_from_memory((const u8 *)tagged.value, (i32)tagged.length, &width, &height, &channels, 4);
    Check(pixels && width == 1 && height == 1);
    stbi_image_free(pixels);
    // A thumbnailer's picture goes into the cache, which answers the next look without it.
    String source = Prog_JoinPath(&arena, fixture->path, S("made.png")), video = Prog_JoinPath(&arena, fixture->path, S("clip.mp4"));
    String cache = Prog_JoinPath(&arena, fixture->path, S("cache"));
    Check(!P_WriteFile(&arena, source, png).code && !P_WriteFile(&arena, video, S("not really a video")).code);
    const char *previous = getenv("XDG_CACHE_HOME");
    String saved = previous ? StrCopy(&arena, StrFromCStr(previous)) : (String){0};
    setenv("XDG_CACHE_HOME", P__FilePathCString(&arena, cache), 1);
    P_Image image = {0};
    Check(P__LoadThumbnailerPicture(&arena, video, StrFormat(&arena, "cp %S %%o", source), 200, NULL, &image) && image.width == 1);
    String cached = StrFormat(&arena, "%S/thumbnails/large/%S", cache, P__ThumbnailName(&arena, P__FileUri(&arena, video))), contents = {0};
    Check(P_ReadFile(&arena, cached, &contents) && P__PngText(contents, S("Thumb::URI")).length && TestListing(&arena, fixture, S("cache/thumbnails/large")).header.count == 1);
    image = (P_Image){0};
    Check(P__LoadThumbnailerPicture(&arena, video, S("false"), 200, NULL, &image) && image.width == 1);
    // A picture larger than the cache's size is shrunk into it, with its PNG written as stb_image reads it, and the next look takes it from there.
    u32 *gradient = ArenaPushArray(&arena, 300 * 200, u32);
    for (u32 i = 0; i < 300 * 200; ++i) gradient[i] = 0xFF000000u | (i % 300) | (i / 300) << 8;
    String encoded = P__EncodePng(&arena, gradient, 300, 200), photo = Prog_JoinPath(&arena, fixture->path, S("photo.png"));
    pixels = stbi_load_from_memory((const u8 *)encoded.value, (i32)encoded.length, &width, &height, &channels, 4);
    Check(pixels && width == 300 && height == 200 && MemCompare(pixels, gradient, 300 * 200 * 4) == 0 && encoded.length < 300 * 200);
    stbi_image_free(pixels);
    Check(!P_WriteFile(&arena, photo, encoded).code);
    image = (P_Image){0};
    Check(P_LoadFileThumbnail(&arena, photo, P_FileKind_File, 100, true, NULL, &image) && image.width == 128 && image.height == 85);
    String shrunk = StrFormat(&arena, "%S/thumbnails/normal/%S", cache, P__ThumbnailName(&arena, P__FileUri(&arena, photo)));
    Check(P_ReadFile(&arena, shrunk, &contents) && P__PngText(contents, S("Thumb::MTime")).length);
    image = (P_Image){0};
    Check(P_LoadFileThumbnail(&arena, photo, P_FileKind_File, 100, true, NULL, &image) && image.width == 128);
    // Formats past PNG and JPEG that stb_image reads go the same way, as a TGA here.
    TestSink sink = { .arena = &arena };
    String targa = Prog_JoinPath(&arena, fixture->path, S("photo.tga"));
    Check(stbi_write_tga_to_func(TestSinkWrite, &sink, 300, 200, 4, gradient) && !P_WriteFile(&arena, targa, sink.out.buffer).code);
    image = (P_Image){0};
    Check(P_LoadFileThumbnail(&arena, targa, P_FileKind_File, 100, true, NULL, &image) && image.width == 128 && image.height == 85);
    // One still running when its job is cancelled is stopped rather than waited for.
    P_CancelToken stop = {0};
    P_RequestCancel(&stop);
    u32 error = 0;
    char *sleeper[] = { P__FindExecutable(&arena, S("sleep"), &error), "30", NULL };
    u64 started = PlatformNowMicroseconds();
    Check(sleeper[0] && !P__RunAndWait(sleeper, 15, &stop) && PlatformNowMicroseconds() - started < 1000000);
    // One that fails leaves nothing behind.
    Check(!P__LoadThumbnailerPicture(&arena, source, S("false %o"), 200, NULL, &image) && TestListing(&arena, fixture, S("cache/thumbnails/large")).header.count == 1);
    if (saved.length) setenv("XDG_CACHE_HOME", P__FilePathCString(&arena, saved), 1);
    else unsetenv("XDG_CACHE_HOME");
    TestRemoveTree(&arena, fixture, S("cache"));
    P_CancelToken token = {0};
    String made[] = { source, video, photo, targa };
    Check(!P_DeleteItems(&arena, made, 4, false, &token).code);
    ArenaRelease(&arena);
}
#endif

function void TestBrowsing(void)
{
    TestFileFixture fixture = {0};
    b32 created = P__TestCreateFixture(&fixture);
    Check(created);
    if (created) {
        TestDirectoryFiles(&fixture);
        TestFileIcons(&fixture);
#if defined(__linux__)
        P__TestOpenPath(&fixture);
        TestApps(&fixture);
        TestThumbnailers(&fixture);
#endif
        TestProgramBrowsing(&fixture);
        TestTabs(&fixture);
        TestPanels(&fixture);
        TestInspectorSelection(&fixture);
        TestUndo(&fixture);
        TestSettings();
        TestSidebar(&fixture);
        TestRename(&fixture);
        TestFilter(&fixture);
        TestQuickAccessFilter(&fixture);
        TestEvenPanels(&fixture);
        TestContainingPath(&fixture);
        TestExpand(&fixture);
        TestWatch(&fixture);
        TestMeasure(&fixture);
        TestFileOperations(&fixture);
        TestProgramFileCommands(&fixture);
        TestTrash(&fixture);
#if defined(_WIN32)
        P__TestDropForwarding(&fixture);
        P__TestNeighborQuery(&fixture);
#endif
        TestFilesPopup(&fixture);
    }
    P__TestRemoveFixture(&fixture);
}
