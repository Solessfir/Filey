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
    {.name = S("Folder10"), .directory = true},
    {.name = S("Folder2"), .directory = true},
    {.name = S("Empty"), .directory = true},
    {.name = S("file10.txt"), .size = 10},
    {.name = S("file2.txt"), .size = 2},
    {.name = S("plain.txt"), .size = 1},
    {.name = S(".hidden"), .hidden = true},
    {.name = S("Zażółć-🙂.txt"), .size = 3},
    {.name = S("Folder2/inside.txt"), .size = 5},
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
            Check(file->modifiedTimeUnix > 0);
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
    Check(P_FormatFileTime(&arena, 0).length == 16);
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
    Check(!state.hasFocus && !state.hasAnchor && !TestSelectionCount(&state));
    Check(state.history.header.count == 2 && state.historyAt == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
    Check(StrIsEqual(state.directory.path, fixture->path) && state.historyAt == 0);
    TestBrowsingCommand(&state, Prog_CommandKind_Forward, 0);
    Check(state.directory.count == 1 && state.historyAt == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_Up, 0);
    Check(StrIsEqual(state.directory.path, fixture->path) && state.history.header.count == 3 && state.historyAt == 2);
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
    b32 loaded = P_LoadFileIcon(&arena, file, P_FileKind_File, 16, &image);
#if defined(_WIN32)
    Check(loaded);
#endif
    Check(!loaded || (image.width > 0 && image.height > 0 && image.pixels != NULL));
    loaded = P_LoadFileIcon(&arena, fixture->path, P_FileKind_Directory, 16, &image);
    Check(!loaded || (image.width > 0 && image.height > 0));

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
        b32 match = Prog_FilterMatch(Prog_EntryLeaf(state->directory.entries[i].file.name), filter, NULL);
        b32 inView = Prog_ViewPosition(state, i) != UINT64_MAX;
        result &= match == inView && (inView || !state->directory.entries[i].selected);
        shown += inView != 0;
    }
    return result && shown == state->viewCount && TestSelectionCount(state) <= state->viewCount;
}

function void TestFilter(TestFileFixture *fixture)
{
    // the reference's rules: letters in order, Unicode case folded, accents kept.
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

    // The filter matches names, not the folders they are in.
    TestRenameType(&state, S("inside"));
    Check(state.viewCount == 1 && state.view[0] == inside && state.focusedRow == inside);
    TestRenameKey(&state, Key_A, KeyMod_Control);
    TestRenameType(&state, S("folder2"));
    Check(state.viewCount == 1 && state.view[0] == folder);
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
    Check(StrIsEqual(Prog_FormatSize(&arena, 0), S("0 B")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 1010), S("1010 B")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 1024), S("1 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 9420), S("9.2 kB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, 1048575), S("1 MB")));
    Check(StrIsEqual(Prog_FormatSize(&arena, UINT64_C(12268339)), S("11.7 MB")));

    P_CancelToken token = {0};
    P_DirectoryTotals totals = P_MeasureDirectory(Prog_JoinPath(&arena, fixture->path, S("Folder2")), &token);
    Check(totals.complete && totals.folders == 0 && totals.files == 1 && totals.bytes == 5);
    // Hidden items count; the subfolder's file counts toward the root.
    totals = P_MeasureDirectory(fixture->path, &token);
    Check(totals.complete && totals.folders == 3 && totals.files >= 7 && totals.bytes >= 22);
    totals = P_MeasureDirectory(Prog_JoinPath(&arena, fixture->path, S("missing")), &token);
    Check(!totals.complete && !totals.files);
    P_RequestCancel(&token);
    Check(!P_MeasureDirectory(fixture->path, &token).complete);

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
            TestBrowsingCommand(&state, Prog_CommandKind_Sort, Prog_Sort_Size);
            Check(TestEntryIndex(&state, S("Folder2")) == 2 && TestEntryIndex(&state, S("Empty")) == 0);
            Check(state.hasFocus && state.focusedRow == 2 && state.directory.entries[2].selected);
            TestBrowsingCommand(&state, Prog_CommandKind_Sort, Prog_Sort_Size);
            Check(state.descending && TestEntryIndex(&state, S("Folder2")) == 0 && state.focusedRow == 0);
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
    Check(!P_TransferItems(&arena, &plain, 1, ops, false, &token).code);
    Check(!P_TransferItems(&arena, &plain, 1, ops, false, &token).code);
    Check(TestHasFile(&arena, fixture, S("Ops"), S("plain.txt")) && TestHasFile(&arena, fixture, S(""), S("plain.txt")));
    Check(TestListing(&arena, fixture, S("Ops")).header.count == 6);
    Check(!P_TransferItems(&arena, &folder, 1, ops, false, &token).code);
    Check(TestHasFile(&arena, fixture, S("Ops/Folder2"), S("inside.txt")) && TestHasFile(&arena, fixture, S("Folder2"), S("inside.txt")));

    // A folder never goes inside itself.
    String copy = Prog_JoinPath(&arena, ops, S("Folder2"));
    Check(P_TransferItems(&arena, &copy, 1, copy, false, &token).code);
    Check(P_TransferItems(&arena, &copy, 1, Prog_JoinPath(&arena, copy, S("deeper")), true, &token).code);
    Check(TestListing(&arena, fixture, S("Ops/Folder2")).header.count == 1);

    // Moves take the item out of its folder; a move into its own folder does nothing.
    String moving = Prog_JoinPath(&arena, ops, S("plain.txt"));
    String target = Prog_JoinPath(&arena, ops, S("New folder"));
    Check(!P_TransferItems(&arena, &moving, 1, ops, true, &token).code && TestHasFile(&arena, fixture, S("Ops"), S("plain.txt")));
    Check(!P_TransferItems(&arena, &moving, 1, target, true, &token).code);
    Check(TestHasFile(&arena, fixture, S("Ops/New folder"), S("plain.txt")) && !TestHasFile(&arena, fixture, S("Ops"), S("plain.txt")));
    String cancelled = Prog_JoinPath(&arena, ops, S("New file.txt"));
    P_RequestCancel(&token);
    Check(P_TransferItems(&arena, &cancelled, 1, target, false, &token).code && !TestHasFile(&arena, fixture, S("Ops/New folder"), S("New file.txt")));
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

    // Links copy as links.
    String link = Prog_JoinPath(&arena, fixture->path, S("broken-link"));
    Check(!P_TransferItems(&arena, &link, 1, ops, false, &token).code && TestHasFile(&arena, fixture, S("Ops"), S("broken-link")));
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

    // Copy As Path puts the system's form of the path on the clipboard.
    Prog_SelectRow(&state, 0, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_CopyAsPath, 0);
    Check(StrIsEqual(TestClipboard, P_NativePath(&arena, Prog_JoinPath(&arena, Prog_JoinPath(&arena, root, S("New folder")), S("a.txt")))));

    // Pasting with nothing selected or on the clipboard does nothing.
    PlatformClipboardSetText(S("not files"));
    TestBrowsingCommand(&state, Prog_CommandKind_Paste, 0);
    Check(!state.error.length);

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
    Check(state.menuOpen && state.menu.rootCount == 3 && state.menu.items[2].childCount == 2);
    if (state.menuOpen) {
        u32 newFolder = state.menu.items[2].firstChild;
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
#endif
        TestProgramBrowsing(&fixture);
        TestRename(&fixture);
        TestFilter(&fixture);
        TestExpand(&fixture);
        TestWatch(&fixture);
        TestMeasure(&fixture);
        TestFileOperations(&fixture);
        TestProgramFileCommands(&fixture);
#if defined(_WIN32)
        P__TestDropForwarding(&fixture);
        P__TestNeighborQuery(&fixture);
#endif
    }
    P__TestRemoveFixture(&fixture);
}
