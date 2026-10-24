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

// The keyed sort orders random names, with digits, case, punctuation, labels and ties, as the entries' comparison does, and keeps ties in place;
// enough of them for the sort to run on several lanes.
function void TestSortKeys(void)
{
    Arena arena = {0};
    u64 count = 70000;
    Prog_Entry *entries = ArenaPushArray(&arena, count, Prog_Entry), *buffer = ArenaPushArray(&arena, count, Prog_Entry);
    const char alphabet[] = "aAbB0129 _.-z\xC3\xA9";
    const char *types[] = { "Text", "text", "PNG File", "PNG", "" };
    u64 seed = 7;
    for (u64 i = 0; i < count; ++i) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        u32 r = (u32)(seed >> 33);
        u64 length = r % 11;
        char *name = ArenaPushArray(&arena, length + 1, char);
        for (u64 k = 0; k < length; ++k) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            name[k] = alphabet[(seed >> 40) % (sizeof(alphabet) - 1)];
        }
        Prog_Entry *entry = &entries[i];
        entry->file.name = StrMake(name, length);
        if (r % 17 == 0) entry->label = S("label");
        entry->file.kind = r % 5 == 0 ? P_FileKind_Directory : P_FileKind_File;
        entry->file.size = r % 4;
        entry->file.modifiedTimeUnix = (i64)(r % 7) - 3;
        // Items saved within one second go by the ticks inside it.
        entry->file.modifiedTicks = (u32)((r * UINT64_C(7919)) % 10000000);
        entry->type = StrFromCStr(types[r % 5]);
        entry->recentFirst = r % 23 == 0;
        entry->shownFrame = i;
    }
    Prog_Sort sorts[] = { Prog_Sort_Name, Prog_Sort_Type, Prog_Sort_Size, Prog_Sort_Modified };
    for (u32 s = 0; s < CArrayCount(sorts); ++s) {
        for (u32 d = 0; d < 2; ++d) {
            Prog_FolderSort folders = d ? Prog_FolderSort_Mixed : Prog_FolderSort_First;
            Prog_Entry *sorted = Prog__SortEntries(entries, buffer, count, sorts[s], d != 0, folders, NULL);
            Check(sorted != NULL);
            b32 ordered = true;
            for (u64 i = 1; sorted && i < count; ++i) {
                i32 order = Prog__CompareEntries(&sorted[i - 1], &sorted[i], sorts[s], d != 0, folders);
                ordered &= order < 0 || (order == 0 && sorted[i - 1].shownFrame < sorted[i].shownFrame);
            }
            Check(ordered);
        }
    }
    ArenaRelease(&arena);
}

function void TestProgramBrowsing(TestFileFixture *fixture)
{
    TestSortKeys();
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
    // A path on a drive that is not there now is offline; a share, or one before any drive is listed, never is.
    {
        Arena arena = {0};
        Prog_State offline = {0};
        Check(Prog_PathOnline(&offline, S("D:/Media")));
        ArrayPush(&arena, offline.drives, ((P_Place){ .path = S("C:/") }));
        ArrayPush(&arena, offline.drives, ((P_Place){ .path = S("/run/media/me/Stick") }));
        Check(Prog_PathOnline(&offline, S("c:/Work")) && Prog_PathOnline(&offline, S("C:")) && !Prog_PathOnline(&offline, S("D:/Media")) && !Prog_PathOnline(&offline, S("D:/")));
        Check(Prog_PathOnline(&offline, S("//server/share")) && Prog_PathOnline(&offline, S("/home/me")) && Prog_PathOnline(&offline, S("/run/media/me/Stick/a")));
        Check(!Prog_PathOnline(&offline, S("/run/media/me/Other")) && !Prog_PathOnline(&offline, S("/run/media/me/StickTwo")));
        ArenaRelease(&arena);
    }
    // The same notice again counts up and comes to the front; a sixth pushes the oldest out; an error stays, an info fades.
    {
        Prog_State noticed = {0};
        Prog_Notify(&noticed, Prog_NoticeKind_Error, S("copy.failed"), S("Copy failed"), S("Access is denied."));
        Prog_Notify(&noticed, Prog_NoticeKind_Info, S("undo.nothing"), S("Nothing to undo"), (String){0});
        Prog_Notify(&noticed, Prog_NoticeKind_Error, S("copy.failed"), S("Copy failed"), S("Access is denied."));
        Check(noticed.noticeCount == 2 && noticed.notices[1].count == 2 && noticed.notices[1].kind == Prog_NoticeKind_Error && noticed.notices[0].count == 1);
        for (u32 i = 0; i < 5; ++i) Prog_Notify(&noticed, Prog_NoticeKind_Warning, S("test.warned"), S("Warned"), StrFormat(&noticed.frameArena, "%u", i));
        Check(noticed.noticeCount == Prog_MaxNotices && noticed.notices[0].kind == Prog_NoticeKind_Warning);
        Check(Prog_NoticeLeft(&(Prog_Notice){ .kind = Prog_NoticeKind_Error, .since = 0 }, UINT64_C(1) << 40) == UINT64_MAX);
        Check(Prog_NoticeLeft(&(Prog_Notice){ .kind = Prog_NoticeKind_Info, .since = 0 }, UINT64_C(1) << 40) == 0);
        // Set to errors only, a warning does not show; set to none, nothing does.
        Prog_State quiet = {0};
        Prog_ApplySettings(&quiet, S("notices = errors\n"));
        Prog_Notify(&quiet, Prog_NoticeKind_Warning, S("test.warned"), S("Warned"), (String){0});
        Prog_Notify(&quiet, Prog_NoticeKind_Error, S("test.failed"), S("Failed"), (String){0});
        Check(quiet.noticeCount == 1 && quiet.notices[0].kind == Prog_NoticeKind_Error && Prog_OptionValue(&quiet, Prog_Option_Notices) == Prog_NoticeKind_Error);
        Check(quiet.noticesInSidebar);
        Prog_ApplySettings(&quiet, S("notices = off\nnotices.sidebar = false\n"));
        Check(!quiet.noticesInSidebar && Prog_OptionValue(&quiet, Prog_Option_NoticesInSidebar) == 0);
        Prog_Notify(&quiet, Prog_NoticeKind_Error, S("test.failed"), S("Failed again"), (String){0});
        Check(quiet.noticeCount == 1);
        // Without a settings file, every option is at its default; one set away from it goes back with a restore.
        Prog_State fresh = {0};
        Prog_ApplySettings(&fresh, (String){0});
        for (u32 o = 0; o < Prog_Option_Count; ++o) Check(Prog_OptionIsDefault(&fresh, (Prog_Option)o));
        Check(!Prog_OptionIsDefault(&quiet, Prog_Option_Notices));
        Prog_RestoreOption(&quiet, Prog_Option_Notices);
        Check(Prog_OptionIsDefault(&quiet, Prog_Option_Notices) && quiet.noticeLevel == Prog_NoticeKind_Info);
        Prog_ApplySettings(&quiet, S("font = Consolas\n"));
        Check(!Prog_OptionIsDefault(&quiet, Prog_Option_Font));
        Prog_RestoreOption(&quiet, Prog_Option_Font);
        Check(Prog_OptionIsDefault(&quiet, Prog_Option_Font));
        // ffprobe's compact lines read by field, and its numbers, ratios too; media files by their extensions.
        {
            String line = S("codec_name=h264|codec_type=video|width=640|avg_frame_rate=30000/1001");
            Check(StrIsEqual(Prog__MediaField(line, S("codec_type")), S("video")) && !Prog__MediaField(line, S("codec")).length);
            f64 fps = Prog__MediaNumber(Prog__MediaField(line, S("avg_frame_rate")));
            Check(fps > 29.96 && fps < 29.98 && Prog__MediaNumber(S("5.250000")) == 5.25 && Prog__MediaNumber(S("0/0")) == 0);
            Check(Prog_IsAudio(S("a/Song.FLAC")) && Prog_IsVideo(S("clip.mkv")) && !Prog_IsAudio(S("clip.mkv")) && !Prog_IsVideo(S("notes")));
            Check(!Prog_IsVideo(S("src/index.ts")) && Prog_IsVideo(S("00001.MTS")) && !Prog_IsAudio(S("board.dts")));
            // ffmpeg's -progress lines, however they are cut, move a compression's milliseconds on after those of the videos before, and
            // a line of its own is its message.
            P_Progress progress = {0};
            atomic_store(&progress.totalBytes, 10000);
            Prog__Encoding encoding = { .progress = &progress, .base = 4000, .name = S("clip.mkv") };
            Prog__EncodingOutput(&encoding, S("frame=10\nout_time_us=15"));
            Prog__EncodingOutput(&encoding, S("00000\r\nspeed= 2.5x\nNo such filter\n"));
            Check(atomic_load(&progress.doneBytes) == 5500 && StrIsEqual(Prog__TransferName(&quiet.frameArena, &progress), S("clip.mkv \u00B7 2.5x")));
            Check(StrIsEqual(StrMake(encoding.message, encoding.messageLength), S("No such filter")));
            // yt-dlp's marked lines: a playlist's count, the title, how far a part is with its name holding spaces, the finished files and
            // errors; only web addresses are downloaded.
            P_Progress fetched = {0};
            Arena fetchArena = {0};
            ArrayStr fetchedFiles = {0};
            Prog__Fetch fetch = { .progress = &fetched, .arena = &fetchArena, .placed = &fetchedFiles };
            Prog__FetchOutput(&fetch, S("FILEYITEM 3\nFILEYTITLE Me at the zoo\nFILEY 1024 NA 4096 2048.5 3 Me at the zoo.f1.mp4.part\n[info] noise\nERROR: Gone\n"));
            Check(atomic_load(&fetched.totalBytes) == 4096 && atomic_load(&fetched.doneBytes) == 1024 && StrIsEqual(StrMake(fetch.part, fetch.partLength), S("Me at the zoo.f1.mp4.part")));
            Check(atomic_load(&fetched.totalItems) == 3 && StrIsEqual(StrMake(fetch.message, fetch.messageLength), S("Gone")));
            Prog__FetchOutput(&fetch, S("FILEYDONE C:/v/Me.mp4\nFILEYITEM NA\nFILEYDONE C:/v/Next.mp4\n"));
            Check(fetchedFiles.header.count == 2 && StrIsEqual(fetchedFiles.v[0], S("C:/v/Me.mp4")) && StrIsEqual(fetchedFiles.v[1], S("C:/v/Next.mp4")));
            Check(atomic_load(&fetched.doneItems) == 2 && atomic_load(&fetched.totalItems) == 3 && !fetch.partLength);
            ArenaRelease(&fetchArena);
            // aria2c's readout, rewritten in place with carriage returns, its finished path and errors; torrents by link or name.
            P_Progress seeded = {0};
            Prog__Torrent torrent = { .progress = &seeded, .name = S("bbb") };
            Prog__TorrentOutput(&torrent, S("[#9cb939 30MiB/263MiB(11%) CN:31 SD:3 DL:22MiB ETA:1m10s]\r   \r\n10/23 [NOTICE] Download complete: [MEMORY][METADATA]x\n"
                                            "10/23 [NOTICE] Download complete: C:/t/Big Buck Bunny\n10/23 [ERROR] Bad\n"));
            Check(atomic_load(&seeded.totalBytes) == (UINT64_C(263) << 20) && atomic_load(&seeded.doneBytes) == (UINT64_C(30) << 20));
            Check(StrIsEqual(Prog__TransferName(&quiet.frameArena, &seeded), S("bbb \u00B7 22 MB/s \u00B7 1:10 \u00B7 31 peers")));
            Check(StrIsEqual(StrMake(torrent.done, torrent.doneLength), S("C:/t/Big Buck Bunny")) && StrIsEqual(StrMake(torrent.message, torrent.messageLength), S("Bad")));
            Check(Prog_IsTorrent(S("magnet:?xt=urn:btih:dd82")) && Prog_IsTorrent(S("C:/a/b.TORRENT")) && !Prog_IsTorrent(S("b.torrent.txt")));
            Check(Prog_IsMagnet(S("Magnet:?xt=urn:btih:dd82")) && !Prog_IsMagnet(S("movie.torrent")) && !Prog_IsMagnet(S("magnet")));
            // A .torrent's files in aria2c's order, a padding one among them, by their UTF-8 names; one of a single file is its name, and bytes
            // cut short are none.
            Prog_TorrentInfo info = {0};
            Check(Prog_ReadTorrent(&quiet.frameArena, S("d4:infod5:filesld6:lengthi40e4:pathl6:Season5:a.mkveed4:attr1:p6:lengthi24e4:pathl4:.pad2:24eed6:lengthi50e4:pathl5:b.txteee"
                                                        "4:name3:old10:name.utf-83:Newee"), &info));
            Check(StrIsEqual(info.name, S("New")) && info.fileCount == 3 && info.size == 114 && StrIsEqual(info.files[0].path, S("Season/a.mkv")) && info.files[0].size == 40);
            Check(info.files[1].pad && !info.files[0].pad && StrIsEqual(info.files[2].path, S("b.txt")) && info.files[2].size == 50);
            // The files chosen, as --select-file takes them, never the padding; empty chooses all of them.
            b8 skip[5] = {0};
            Prog_TorrentSkip((String){0}, &info, skip);
            Check(!skip[0] && skip[1] && !skip[2] && StrIsEqual(Prog_TorrentSelection(&quiet.frameArena, &info, skip), S("1,3")));
            Prog_TorrentSkip(S("3"), &info, skip);
            Check(skip[0] && skip[1] && !skip[2] && StrIsEqual(Prog_TorrentSelection(&quiet.frameArena, &info, skip), S("3")));
            Prog_TorrentInfo single = {0};
            Check(Prog_ReadTorrent(&quiet.frameArena, S("d4:infod6:lengthi7e4:name5:a.isoee"), &single) && single.fileCount == 1 && StrIsEqual(single.files[0].path, S("a.iso")));
            Check(!Prog_ReadTorrent(&quiet.frameArena, S("d4:infod6:lengthi7e4:name5:a.is"), &single) && !Prog_ReadTorrent(&quiet.frameArena, S("i3e"), &single));
            Prog_TorrentFile five[5] = {0};
            Prog_TorrentInfo ranges = { .files = five, .fileCount = 5 };
            b8 picked[5] = { 0, 0, 1, 0, 0 };
            Check(StrIsEqual(Prog_TorrentSelection(&quiet.frameArena, &ranges, picked), S("1-2,4-5")));
            Prog_TorrentSkip(S("2-3,5"), &ranges, picked);
            Check(picked[0] && !picked[1] && !picked[2] && picked[3] && !picked[4]);
            picked[0] = picked[3] = 0;
            Check(!Prog_TorrentSelection(&quiet.frameArena, &ranges, picked).length);
            // Each file's bytes done by the pieces aria2c's control file marks, 16 KiB pieces of which the first three are done; one of another
            // torrent's length, or of a version with the host's own byte order, says nothing.
            String control = S("\x00\x01" "\x00\x00\x00\x00" "\x00\x00\x00\x14" "AAAAAAAAAAAAAAAAAAAA" "\x00\x00\x40\x00" "\x00\x00\x00\x00\x00\x01\xBD\x50"
                               "\x00\x00\x00\x00\x00\x00\x00\x00" "\x00\x00\x00\x01" "\xE0" "\x00\x00\x00\x00");
            u64 sizes[3] = { 40000, 24000, 50000 }, done[3] = {0}, wrong[3] = { 1, 2, 3 };
            Check(Prog_TorrentControlDone(control, sizes, 3, done) && done[0] == 40000 && done[1] == 49152 - 40000 && done[2] == 0);
            Check(!Prog_TorrentControlDone(control, wrong, 3, done) && !Prog_TorrentControlDone(StrFrom(control, 1), sizes, 3, done) && !Prog_TorrentControlDone(StrFromTo(control, 0, 40), sizes, 3, done));
            // A magnet link's info hash in hex, from hex or base32, as aria2c names the .torrent it saves; and a torrent's name as a file's.
            String hex = S("c12fe1c06bba254a9dc9f519b335aa7c1367a88a");
            Check(StrIsEqual(Prog_MagnetHash(&quiet.frameArena, S("magnet:?xt=urn:btih:C12FE1C06BBA254A9DC9F519B335AA7C1367A88A&dn=x")), hex));
            Check(StrIsEqual(Prog_MagnetHash(&quiet.frameArena, S("magnet:?xt=urn:btih:YEX6DQDLXISUVHOJ6UM3GNNKPQJWPKEK")), hex));
            Check(!Prog_MagnetHash(&quiet.frameArena, S("magnet:?xt=urn:btih:xyz")).length && !Prog_MagnetHash(&quiet.frameArena, S("magnet:?dn=x")).length);
            Check(StrIsEqual(Prog__TorrentFileName(&quiet.frameArena, S("a:b?. ")), S("a_b_")) && StrIsEqual(Prog__TorrentFileName(&quiet.frameArena, S(" .")), S("Torrent")));
            // The downloads noted to be started again, a line each with the files they chose, replaced when noted again and dropped once over.
            Prog_State notes = {0};
            Prog__NoteTorrent(&notes, S("C:/t/a.torrent"), S("1,3"), true);
            Prog__NoteTorrent(&notes, S("C:/t/b.torrent"), (String){0}, true);
            Prog__NoteTorrent(&notes, S("C:/t/a.torrent"), S("2"), true);
            Check(notes.torrents.header.count == 2 && StrIsEqual(Prog__SavedSelection(&notes, S("C:/t/a.torrent")), S("2")));
            Prog__NoteTorrent(&notes, S("C:/t/b.torrent"), (String){0}, false);
            Check(notes.torrents.header.count == 1 && !Prog__SavedSelection(&notes, S("C:/t/b.torrent")).length);
            // A torrent downloads in one of the torrents' own slots, after the others', and one noted without a slot waits at its place in the list.
            String a = S("C:/t/a.torrent"), c = S("C:/t/c.torrent");
            Prog__NoteTorrent(&notes, c, S("2"), true);
            notes.transfers[Prog_MaxTransfers + 1] = (Prog_Transfer){ .job = (P_Job *)&notes, .sourceHash = MapHashBytes(a.value, a.length) };
            notes.transfers[0] = (Prog_Transfer){ .job = (P_Job *)&notes, .sourceHash = MapHashBytes(c.value, c.length) };
            Check(Prog_TorrentSlot(&notes, a) == Prog_MaxTransfers + 1 && Prog_TorrentWaiting(&notes, a) == UINT64_MAX);
            Check(Prog_TorrentSlot(&notes, c) == UINT32_MAX && Prog_TorrentWaiting(&notes, c) == 1 && Prog_TorrentWaiting(&notes, S("C:/t/d.torrent")) == UINT64_MAX);
            ArenaRelease(&notes.torrentArena);
            // The decoded sound comes in pieces that may cut a frame, which waits for the next, and plays at the volume set as it comes.
            {
                atomic_uint volume = 50;
                Prog__Player player = { .volume = &volume };
                i16 first[3] = { 1000, -1000, 400 }, second[3] = { -400, 2000, 2000 };
                u64 frames = 0;
                i16 *heard = Prog__PlayerFrames(&player, &notes.frameArena, StrMake((const char *)first, 6), &frames);
                Check(frames == 1 && heard[0] == 250 && heard[1] == -250 && player.carried == 2);
                atomic_store(&volume, 100u);
                heard = Prog__PlayerFrames(&player, &notes.frameArena, StrMake((const char *)second, 6), &frames);
                Check(frames == 2 && heard[0] == 400 && heard[1] == -400 && heard[2] == 2000 && heard[3] == 2000 && player.carried == 0);
                heard = Prog__PlayerFrames(&player, &notes.frameArena, StrMake((const char *)first, 2), &frames);
                Check(frames == 0 && player.carried == 2);
            }
            Check(Prog_IsWebUrl(S("https://youtu.be/x")) && Prog_IsWebUrl(S("HTTP://a.b")) && !Prog_IsWebUrl(S("ftp://a")) && !Prog_IsWebUrl(S("https://a b")));
        }
        // A language file translates by the English text, keeps a translation's conversions only where they match, and a custom file of a
        // built-in language's name adds to it; English again takes them all away.
        {
            Check(Prog__SameConversions(S("%u of %S"), S("%S? no %u")) == false && Prog__SameConversions(S("%u files, 100%%"), S("%u fichiers")));
            Prog_UseLanguage(&quiet, S("English"));
            Prog_AddLanguageText(S("# comment\nOpen\n= Ouvrir\n\n%u muted\n= %S muet\nEmpty\n= \nDone\n= Fini\n"));
            Check(StrIsEqual(Tr("Open"), S("Ouvrir")) && StrIsEqual(Tr("Done"), S("Fini")) && StrIsEqual(Tr("Empty"), S("Empty")));
            Check(!strcmp(TrF("%u muted"), "%u muted"));
            // Counted texts take the form for their count, English's without a translation, and a line break is written as one.
            Check(!strcmp(TrN(1, "%u file", "%u files"), "%u file") && !strcmp(TrN(2, "%u file", "%u files"), "%u files"));
            Prog_AddLanguageText(S("%u files\n= %u f1 | %u f2 | %u f5\n%u rows\n= %u r1 | %S r2\nTwo\\nlines\n= Deux\\nlignes\n"));
            Check(!strcmp(TrN(1, "%u file", "%u files"), "%u f1") && !strcmp(TrN(22, "%u file", "%u files"), "%u f2") && !strcmp(TrN(12, "%u file", "%u files"), "%u f5"));
            Check(!strcmp(TrN(21, "%u file", "%u files"), "%u f1") && !strcmp(TrN(3, "%u row", "%u rows"), "%u rows"));
            Check(StrIsEqual(Tr("Two\nlines"), S("Deux\nlignes")));
            Prog_UseLanguage(&quiet, (String){0});
            Check(StrIsEqual(Tr("Open"), S("Open")));
            // The menu items kept by their text follow the language shown, and are saved in English.
            {
                String pins[] = { S("Open"), S("Unknown Shell Item") };
                Prog__SetList(&quiet.menuPinArena, &quiet.menuPins, pins, CArrayCount(pins));
                Prog_UseLanguage(&quiet, S("\u0420\u0443\u0441\u0441\u043A\u0438\u0439"));
                Check(StrIsEqual(quiet.menuPins.v[0], Tr("Open")) && !StrIsEqual(quiet.menuPins.v[0], S("Open")) && StrIsEqual(quiet.menuPins.v[1], S("Unknown Shell Item")));
                Check(StrIsEqual(Prog_Untr(quiet.menuPins.v[0]), S("Open")));
                // Sizes take the language's units and decimal mark.
                Check(StrIsEqual(Prog_FormatSize(&quiet.frameArena, 1536), S("1,5 \u041A\u0411")));
                Prog_UseLanguage(&quiet, (String){0});
                Check(StrIsEqual(quiet.menuPins.v[0], S("Open")) && StrIsEqual(Prog_FormatSize(&quiet.frameArena, 1536), S("1.5 kB")));
                Prog__SetList(&quiet.menuPinArena, &quiet.menuPins, NULL, 0);
            }
#if !defined(_WIN32)
            // The system's language is the first of the variables gettext reads, by its two letters.
            {
                const char *was = getenv("LANGUAGE");
                setenv("LANGUAGE", "ru_RU:en", 1);
                Check(StrIsEqual(P_SystemLanguage(&quiet.frameArena), S("ru")));
                setenv("LANGUAGE", "C", 1);
                Check(P_SystemLanguage(&quiet.frameArena).length == 0);
                if (was) setenv("LANGUAGE", was, 1);
                else unsetenv("LANGUAGE");
            }
#endif
            Check(Prog_LanguageIsCustom(S("Mine")) && !Prog_LanguageIsCustom(S("english")) && !Prog_LanguageIsCustom((String){0}) && !Prog_LanguageIsCustom(S("Русский")));
            Prog_UseLanguage(&quiet, S("\u0420\u0443\u0441\u0441\u043A\u0438\u0439"));
            Check(StrIsEqual(Tr("Done"), S("\u0413\u043E\u0442\u043E\u0432\u043E")) && !StrIsEqual(Tr("Language"), S("Language")));
            String made = Prog_LanguageTemplate(&quiet.frameArena, S("Mine"));
            Check(TestEndsWith(StrFromTo(made, 0, (i64)StrIndexChar(made, '\n', SSF_None).value), S("Mine")) && Prog__ContainsFolded(made, S("Done\n= \u0413\u043E\u0442\u043E\u0432\u043E\n")));
            Prog_UseLanguage(&quiet, (String){0});
        }
        // A slider takes arithmetic typed into it, worked out and rounded; what does not read keeps it typing.
        f64 worked = 0;
        Check(Prog_EvaluateExpression(S("2*2"), &worked) && worked == 4);
        Check(Prog_EvaluateExpression(S(" 1 + 2 * (3 - 1) / 4 "), &worked) && worked == 2);
        Check(Prog_EvaluateExpression(S("-(2.5+0,5)"), &worked) && worked == -3);
        Check(Prog_EvaluateExpression(S("--3"), &worked) && worked == 3);
        Check(!Prog_EvaluateExpression(S(""), &worked) && !Prog_EvaluateExpression(S("1/0"), &worked) && !Prog_EvaluateExpression(S("2*"), &worked));
        Check(!Prog_EvaluateExpression(S("(1+2"), &worked) && !Prog_EvaluateExpression(S("abc"), &worked) && !Prog_EvaluateExpression(S("."), &worked));
        Prog_BeginSliderEdit(&quiet, Prog_Option_Spacing);
        Check(quiet.sliderOption == Prog_Option_Spacing + 1 && StrIsEqual(Prog_EditText(&quiet.sliderEdit), S("4")));
        quiet.sliderEdit = (Prog_TextEdit){0};
        Prog__EditReplace(&quiet.sliderEdit, (const u8 *)"oops", 4);
        Check(!Prog_EndSliderEdit(&quiet, true) && quiet.sliderInvalid && quiet.sliderOption);
        quiet.sliderEdit = (Prog_TextEdit){0};
        Prog__EditReplace(&quiet.sliderEdit, (const u8 *)"3*2.4", 5);
        Check(Prog_EndSliderEdit(&quiet, true) && !quiet.sliderOption && Prog_OptionValue(&quiet, Prog_Option_Spacing) == 7);
        // A muted kind does not show, whatever its text; an error too, once its kind is in the list by hand.
        Prog_State muted = {0};
        String names[] = { S("undo.nothing"), S("copy.failed") };
        Prog__SetList(&muted.mutedArena, &muted.mutedNotices, names, CArrayCount(names));
        Prog_Notify(&muted, Prog_NoticeKind_Info, S("undo.nothing"), S("Nothing to redo"), (String){0});
        Prog_Notify(&muted, Prog_NoticeKind_Error, S("copy.failed"), S("Copy failed"), S("Access is denied."));
        Prog_Notify(&muted, Prog_NoticeKind_Error, S("move.failed"), S("Move failed"), S("Access is denied."));
        Check(muted.noticeCount == 1 && StrIsEqual(StrMake((const char *)muted.notices[0].key, muted.notices[0].keyLength), S("move.failed")));
        Check(StrIsEqual(Prog__NoticeKey(&muted.frameArena, S("Paste shortcut")), S("pasteshortcut.failed")));
        Check(StrIsEqual(Prog_NoticeKindName(&muted.frameArena, S("rename.failed")), S("Rename failed")) && StrIsEqual(Prog_NoticeKindName(&muted.frameArena, S("undo.nothing")), S("Nothing to undo or redo")));
        Check(StrIsEqual(Prog_NoticeKindName(&muted.frameArena, S("custom")), S("custom")));
        ArenaRelease(&muted.frameArena);
        ArenaRelease(&muted.mutedArena);
        ArenaRelease(&noticed.frameArena);
    }
    // git status, read for a folder inside the repository and for its top: each item's mark, and its folders' the most telling inside.
    {
        Arena arena = {0};
        String text = S(" M sub/b.txt\0A  sub/c.txt\0?? sub/deep/\0 M a.txt\0R  sub/n.txt\0sub/o.txt\0 D sub/deep2/gone.txt\0UU sub/x\0");
        Prog_GitStatus inside = {0}, top = {0};
        Prog_ReadGitChanges(&arena, S("C:/r"), S("C:/r/sub"), text, &inside);
        Check(inside.counts[Prog_GitMark_Modified] == 3 && inside.counts[Prog_GitMark_Added] == 2 && inside.counts[Prog_GitMark_Conflict] == 1);
        Check(inside.changes == 7 && MapsGet(inside.marks, u8, S("b.txt")) == Prog_GitMark_Modified && MapsGet(inside.marks, u8, S("c.txt")) == Prog_GitMark_Added);
        Check(MapsGet(inside.marks, u8, S("deep")) == Prog_GitMark_Untracked && MapsGet(inside.marks, u8, S("n.txt")) == Prog_GitMark_Added);
        Check(MapsGet(inside.marks, u8, S("deep2")) == Prog_GitMark_Modified && MapsGet(inside.marks, u8, S("x")) == Prog_GitMark_Conflict);
        Check(!MapsGet(inside.marks, u8, S("a.txt")) && !MapsGet(inside.marks, u8, S("o.txt")));
        Prog_ReadGitChanges(&arena, S("C:/r"), S("C:/r"), text, &top);
        Check(MapsGet(top.marks, u8, S("sub")) == Prog_GitMark_Conflict && MapsGet(top.marks, u8, S("a.txt")) == Prog_GitMark_Modified);
        Check(MapsGet(top.marks, u8, S("sub/deep")) == Prog_GitMark_Untracked);
        ArenaRelease(&arena);
    }
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
    // The drives' and the bin's own orders: by free space, fullness, file system, original folder and when deleted.
    {
        Prog_Entry made[3] = {
            { .file = { .name = S("a"), .size = 100 }, .freeBytes = 10, .fileSystem = S("NTFS"), .original = S("C:/b"), .deletedTimeUnix = 30 },
            { .file = { .name = S("b"), .size = 100 }, .freeBytes = 90, .fileSystem = S("exFAT"), .original = S("C:/c"), .deletedTimeUnix = 10 },
            { .file = { .name = S("c"), .size = 1000 }, .freeBytes = 500, .fileSystem = S("FAT32"), .original = S("C:/a"), .deletedTimeUnix = 20 },
        };
        Prog_Entry buffer[3];
        struct { Prog_Sort sort; const char *order; } orders[] = {
            { Prog_Sort_FreeSpace, "abc" }, { Prog_Sort_PercentFull, "bca" }, { Prog_Sort_FileSystem, "bca" }, { Prog_Sort_Original, "cab" }, { Prog_Sort_Deleted, "bca" },
        };
        for (u32 o = 0; o < CArrayCount(orders); ++o) {
            Prog_Entry copy[3] = { made[0], made[1], made[2] };
            Prog_Entry *sorted = Prog__SortEntries(copy, buffer, 3, orders[o].sort, false, Prog_FolderSort_Mixed, NULL);
            Check(sorted && sorted[0].file.name.value[0] == orders[o].order[0] && sorted[1].file.name.value[0] == orders[o].order[1] && sorted[2].file.name.value[0] == orders[o].order[2]);
        }
    }
    // What follows goes by name, from A.
    state.sort = Prog_Sort_Name;
    state.descending = false;
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
    Check(TestWaitDirectory(&state));
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
    // A locked folder opens with its view, while a folder without one gets the panel's own back; what changes while it is shown is kept in
    // the lock at once.
    {
        Arena kept = {0};
        String here = StrCopy(&kept, state.directory.path), empty = Prog_JoinPath(&kept, here, S("Empty"));
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
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnToggle, Prog_ColumnKind_Created);
        Check(Prog_FolderView(&state, here, &view) && view.zoom == Prog_ZoomStepped(ownZoom, true) && view.sort == changed && view.columns.count == 6);
        // Unlocked and locked again, it keeps what is shown.
        TestBrowsingCommand(&state, Prog_CommandKind_FolderViewToggle, 0);
        Check(!Prog_FolderView(&state, here, &view) && state.zoom == Prog_ZoomStepped(ownZoom, true) && state.sort == changed);
        TestBrowsingCommand(&state, Prog_CommandKind_FolderViewToggle, 0);
        Check(Prog_FolderView(&state, here, &view) && view.zoom == Prog_ZoomStepped(ownZoom, true) && view.sort == changed && view.columns.count == 6);
        Prog_RequestDirectory(&state, empty, true, 0);
        Check(TestWaitDirectory(&state) && state.zoom == ownZoom && state.sort == ownSort && state.descending == ownDescending && state.columnSet.count == 0);
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, here) && state.zoom == Prog_ZoomStepped(ownZoom, true) && state.sort == changed && state.columnSet.count == 6);
        // A change while locked outlasts leaving the folder.
        TestBrowsingCommand(&state, Prog_CommandKind_ZoomIn, 0);
        u32 zoomed = state.zoom;
        Prog_RequestDirectory(&state, empty, true, 0);
        Check(TestWaitDirectory(&state) && state.zoom == ownZoom);
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state) && state.zoom == zoomed);
        // And the grouping, which a folder without a lock leaves out.
        TestBrowsingCommand(&state, Prog_CommandKind_GroupBySize, 0);
        Check(Prog_FolderView(&state, here, &view) && view.groupBy == Prog_GroupBy_Size);
        Prog_RequestDirectory(&state, empty, true, 0);
        Check(TestWaitDirectory(&state) && state.groupBy == Prog_GroupBy_None && !state.groupCount);
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state) && state.groupBy == Prog_GroupBy_Size && state.groupCount);
        TestBrowsingCommand(&state, Prog_CommandKind_FolderViewToggle, 0);
        Prog_RequestDirectory(&state, empty, true, 0);
        Check(TestWaitDirectory(&state) && !Prog_FolderView(&state, here, &view));
        Check(state.zoom == ownZoom && state.sort == ownSort && state.descending == ownDescending && state.columnSet.count == 0 && state.groupBy == Prog_GroupBy_None);
        TestBrowsingCommand(&state, Prog_CommandKind_Back, 0);
        Check(TestWaitDirectory(&state));
        // The columns read back as written; a line from before the columns keeps the defaults.
        Prog_ColumnSet written = { .count = 3, .kinds = { Prog_ColumnKind_Name, Prog_ColumnKind_Size, Prog_ColumnKind_Created }, .shares = { 0, 0.125f, 0 } }, read = {0};
        Check(Prog_ParseColumns(Prog_ColumnsText(&kept, written), &read) && read.count == 3 && read.kinds[2] == Prog_ColumnKind_Created && read.shares[1] == 0.125f);
        Check(!Prog_ParseColumns(S("size,type"), &read) && !Prog_ParseColumns(S("name,bogus"), &read));
        String old = S("2 3 1 C:/Old Folder");
        Prog__SetList(&state.folderViewArena, &state.folderViews, &old, 1);
        Check(Prog_FolderView(&state, S("C:/Old Folder"), &view) && view.zoom == Prog_ZoomColumns && view.columns.count == 0 && !view.groupBy);
        // A grouping is written before the columns only when there is one.
        String grouped = S("2 3 1 1 3 name C:/Grouped Folder"), plainLine = S("2 3 1 1 name C:/Plain Folder");
        Prog__SetList(&state.folderViewArena, &state.folderViews, &grouped, 1);
        Check(Prog_FolderView(&state, S("C:/Grouped Folder"), &view) && view.groupBy == Prog_GroupBy_Size && view.folders == Prog_FolderSort_Last && view.columns.count == 1);
        Prog__SetList(&state.folderViewArena, &state.folderViews, &plainLine, 1);
        Check(Prog_FolderView(&state, S("C:/Plain Folder"), &view) && !view.groupBy && view.columns.count == 1);
        Prog__SetList(&state.folderViewArena, &state.folderViews, NULL, 0);
        // A column added from the menu in a folder without a lock locks it with that column, and the panel's own view goes without it; one
        // taken away again stays away in the lock.
        Prog_ColumnSet ownColumns = state.columnSet;
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnToggle, Prog_ColumnKind_Git);
        Check(Prog_FolderView(&state, here, &view) && view.columns.kinds[view.columns.count - 1] == Prog_ColumnKind_Git && Prog_ShowsGit(&state));
        Check(state.ownView && state.ownSettings.columns.count == ownColumns.count);
        // A column dragged to another place goes there, which the lock keeps.
        Prog_ColumnSet before = Prog_PanelColumns(&state);
        Prog_QueueCommand(&state, Prog_CommandKind_ColumnMove, before.count - 1, 0, 0)->target = 1;
        Prog_ProcessCommands(&state);
        Check(Prog_FolderView(&state, here, &view) && view.columns.count == before.count && view.columns.kinds[1] == Prog_ColumnKind_Git && view.columns.kinds[2] == before.kinds[1]);
        TestBrowsingCommand(&state, Prog_CommandKind_ColumnToggle, Prog_ColumnKind_Git);
        Check(Prog_FolderView(&state, here, &view) && !Prog_ShowsGit(&state));
        TestBrowsingCommand(&state, Prog_CommandKind_FolderViewToggle, 0);
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
        // Up from a share's root goes to its server, which lists its shares, and from the server to This PC.
        Check(StrIsEqual(Prog_PathServer(S("//nas/media/films")), S("//nas")) && StrIsEqual(Prog_PathServer(S("//nas")), S("//nas")));
        Check(!Prog_PathServer(S("//?/UNC/nas/media")).length && !Prog_PathServer(S("C:/nas")).length && !Prog_PathServer(S("/home")).length);
        Check(StrIsEqual(Prog_UpPath(S("//nas/media/films")), S("//nas/media/")) && StrIsEqual(Prog_UpPath(S("//nas/media/")), S("//nas")));
        Check(StrIsEqual(Prog_UpPath(S("//nas/media")), S("//nas")) && StrIsEqual(Prog_UpPath(S("//nas")), Prog_ThisPC));
        // A server's shares leave out the hidden ones, as C$, whether or not this one answers.
        {
            P_ArrayPlace shares = P_ServerShares(&kept, S("//localhost"), NULL);
            P_DirectoryResult listed = P_EnumerateDirectory(&kept, S("//localhost"), NULL);
            b32 shown = true;
            for (u64 i = 0; i < shares.header.count; ++i) shown &= !TestEndsWith(shares.v[i].path, S("$")) && StrIsEqual(Prog_PathServer(shares.v[i].path), S("//localhost"));
            for (u64 i = 0; i < listed.files.header.count; ++i) shown &= !TestEndsWith(listed.files.v[i].name, S("$")) && listed.files.v[i].kind == P_FileKind_Directory;
            Check(shown);
        }
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
    // A crash report the last run left is offered once, with a button to report it, and marked as offered; the issue's text goes into its link
    // encoded.
    {
        Check(P__TestMakeDirectory(fixture, S("CrashCache")) && P__TestMakeDirectory(fixture, S("CrashCache/Crashes")) &&
              P__TestWriteFile(fixture, S("CrashCache/Crashes/Filey-0.1.0-1.dmp"), 4, false));
        Prog_State crashed = { .cacheDirectory = Prog_JoinPath(&state.frameArena, fixture->path, S("CrashCache")) };
        Prog_CheckCrashes(&crashed);
        Check(crashed.noticeCount == 1 && crashed.notices[0].action == Prog_CommandKind_CrashReport && StrIsEqual(crashed.crashReport, S("Filey-0.1.0-1.seen.dmp")));
        Check(P_PathExists(Prog_JoinPath(&state.frameArena, fixture->path, S("CrashCache/Crashes/Filey-0.1.0-1.seen.dmp"))));
        Prog_CheckCrashes(&crashed);
        Check(crashed.noticeCount == 1);
        Check(StrIsEqual(Prog_UrlEncode(&state.frameArena, S("a b/c\n~")), S("a%20b%2Fc%0A~")));
        Check(P__TestRemovePath(fixture, S("CrashCache/Crashes/Filey-0.1.0-1.seen.dmp"), false) && P__TestRemovePath(fixture, S("CrashCache/Crashes"), true) &&
              P__TestRemovePath(fixture, S("CrashCache"), true));
    }
    // A folder shown that is then deleted lists the folder above it when listed again, as a stale tab does, in its place in the history.
    {
        Check(P__TestMakeDirectory(fixture, S("Doomed")));
        String doomed = Prog_JoinPath(&state.frameArena, fixture->path, S("Doomed"));
        Prog_RequestDirectory(&state, doomed, true, 0);
        Check(TestWaitDirectory(&state) && StrIsEqual(state.directory.path, doomed));
        Check(P__TestRemovePath(fixture, S("Doomed"), true));
        TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
        Check(TestWaitDirectory(&state) && !state.error.length && StrIsEqual(state.directory.path, fixture->path));
        Check(StrIsEqual(state.history.v[state.historyAt], fixture->path));
        // The focused item deleted, the next one takes the focus where it was, rather than the first row.
        Check(P__TestWriteFile(fixture, S("zz1.txt"), 1, false) && P__TestWriteFile(fixture, S("zz2.txt"), 1, false));
        TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
        Check(TestWaitDirectory(&state));
        u64 doomedRow = TestEntryIndex(&state, S("zz1.txt"));
        Prog_SelectRow(&state, doomedRow, 0);
        Check(P__TestRemovePath(fixture, S("zz1.txt"), false));
        TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
        Check(TestWaitDirectory(&state) && state.hasFocus && state.focusedRow == doomedRow && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("zz2.txt")));
        Check(P__TestRemovePath(fixture, S("zz2.txt"), false));
        TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
        Check(TestWaitDirectory(&state));
    }
#if defined(_WIN32)
    // A tab on a drive that is gone, with nothing above its folder to show, goes where a new tab opens.
    {
        char letter = 0;
        for (char c = 'Z'; c >= 'G' && !letter; --c) if (!P_PathExists(StrFormat(&state.frameArena, "%c:/", c))) letter = c;
        if (letter) {
            Prog_RequestDirectory(&state, StrFormat(&state.frameArena, "%c:/Gone/Folder", letter), false, state.historyAt);
            Check(TestWaitDirectory(&state) && TestWaitDirectory(&state) && !state.error.length && StrIsEqual(state.directory.path, Prog_ThisPC));
        }
    }
#endif
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
    // A bare name, as a file only in the cloud is asked by, gets its type's icon though no such file is there to read.
    loaded = P_LoadFileIcon(&arena, S("nowhere.exe"), P_FileKind_File, 16, 1, false, &image);
#if defined(_WIN32)
    Check(loaded && image.width > 0);
#endif
    // A picture is its own thumbnail.
    local_persist const char pixel[] = "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\x0d\x49\x44\x41\x54\x78\xda\x63\xfc\xcf\xc0\x50\x0f\x00\x04\x85\x01\x80\x84\xa9\x8c\x21\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
    String picture = Prog_JoinPath(&arena, fixture->path, S("pixel.png"));
    Check(!P_WriteFile(&arena, picture, StrMake(pixel, sizeof(pixel) - 1)).code);
    image = (P_Image){0};
    Check(P_LoadFileThumbnail(&arena, picture, P_FileKind_File, 64, false, NULL, &image) && image.pixels != NULL);
    // A torrent's download takes it whole when its selection chooses every file but the padding.
    String torrent = Prog_JoinPath(&arena, fixture->path, S("set.torrent"));
    Check(!P_WriteFile(&arena, torrent, S("d4:infod5:filesld6:lengthi40e4:pathl6:Season5:a.mkveed4:attr1:p6:lengthi24e4:pathl4:.pad2:24eed6:lengthi50e4:pathl5:b.txteee4:name3:Setee")).code);
    Check(Prog__TorrentWhole(torrent, (String){0}) && Prog__TorrentWhole(torrent, S("1,3")) && !Prog__TorrentWhole(torrent, S("3")) && !Prog__TorrentWhole(S("nowhere.torrent"), (String){0}));
    // One stopped from its card takes its files, its control file and their emptied folders with it, leaving what else is there; finished, with
    // no control file, it keeps them.
    {
        String set = Prog_JoinPath(&arena, fixture->path, S("Set")), season = Prog_JoinPath(&arena, set, S("Season")), created = {0};
        Check(!P_CreateItem(&arena, fixture->path, S("Set"), true, &created).code && !P_CreateItem(&arena, set, S("Season"), true, &created).code);
        String video = Prog_JoinPath(&arena, season, S("a.mkv")), text = Prog_JoinPath(&arena, set, S("b.txt")), mine = Prog_JoinPath(&arena, set, S("mine.txt"));
        String control = Prog_JoinPath(&arena, fixture->path, S("Set.aria2"));
        Check(!P_WriteFile(&arena, video, S("v")).code && !P_WriteFile(&arena, text, S("t")).code && !P_WriteFile(&arena, mine, S("m")).code);
        Prog_DiscardTorrent(&arena, torrent, fixture->path);
        Check(P_PathExists(video) && P_PathExists(text));
        Check(!P_WriteFile(&arena, control, S("c")).code);
        Prog_DiscardTorrent(&arena, torrent, fixture->path);
        Check(!P_PathExists(control) && !P_PathExists(video) && !P_PathExists(season) && !P_PathExists(text) && P_PathExists(mine));
        Check(P__TestRemovePath(fixture, S("Set/mine.txt"), false) && P__TestRemovePath(fixture, S("Set"), true));
    }
    Check(!P_DeleteItems(&arena, &torrent, 1, false, NULL).code);
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
    // A magnet link's .torrent, saved by its info hash, takes the torrent's name, or gives way to the same one already there.
    {
        String fetched = Prog_JoinPath(&arena, fixture->path, S("Fetched")), created = {0};
        String bytes = S("d4:infod6:lengthi7e4:name7:a?b.isoee");
        Check(!P_CreateItem(&arena, fixture->path, S("Fetched"), true, &created).code);
        Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, fetched, S("00ff.torrent")), bytes).code);
        Check(StrIsEqual(Prog__NameFetchedTorrent(&arena, fetched, S("00ff")), Prog_JoinPath(&arena, fetched, S("a_b.iso.torrent"))));
        Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, fetched, S("00ee.torrent")), bytes).code);
        Check(StrIsEqual(Prog__NameFetchedTorrent(&arena, fetched, S("00ee")), Prog_JoinPath(&arena, fetched, S("a_b.iso.torrent"))));
        Check(!P_PathExists(Prog_JoinPath(&arena, fetched, S("00ee.torrent"))) && !Prog__NameFetchedTorrent(&arena, fetched, S("00dd")).length);
        Check(P__TestRemovePath(fixture, S("Fetched/a_b.iso.torrent"), false) && P__TestRemovePath(fixture, S("Fetched"), true));
    }
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
    // A press there takes the byte of the hex column or the character it is on, the gaps going to the byte before them.
    Check(Prog_HexByteAt(4, 16, 6) == 0 && Prog_HexByteAt(4, 16, 7.5f) == 0 && Prog_HexByteAt(4, 16, 8.6f) == 1 && Prog_HexByteAt(4, 16, 31) == 8);
    Check(Prog_HexByteAt(4, 16, 56) == 0 && Prog_HexByteAt(4, 16, 60.5f) == 4 && Prog_HexByteAt(4, 16, 100) == 15);
    // Its find takes hex, two digits a byte with or without spaces between bytes, and anything else as text.
    Check(StrIsEqual(Prog_HexQuery(&arena, S("4D 5a")), S("MZ")) && StrIsEqual(Prog_HexQuery(&arena, S("4d5a90")), S("MZ\x90")));
    Check(StrIsEqual(Prog_HexQuery(&arena, S("MZ")), S("MZ")) && StrIsEqual(Prog_HexQuery(&arena, S("abc")), S("abc")) && StrIsEqual(Prog_HexQuery(&arena, S("a bc")), S("a bc")));
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
    // Markup: tags as headings, attributes as keywords, values as strings, entities as numbers, and comments and tags that go on over lines.
    u8 inside = Prog_SyntaxLine(Prog_Syntax_Markup, S("<a href=\"x\">&amp; <!-- c"), 0, tokens);
    Check(inside && tokens[0] == Prog_Token_Heading && tokens[1] == Prog_Token_Heading && tokens[3] == Prog_Token_Keyword && tokens[8] == Prog_Token_String && tokens[11] == Prog_Token_Heading);
    Check(tokens[12] == Prog_Token_Number && tokens[16] == Prog_Token_Number && tokens[17] == Prog_Token_Plain && tokens[18] == Prog_Token_Comment && tokens[23] == Prog_Token_Comment);
    u8 tag = Prog_SyntaxLine(Prog_Syntax_Markup, S("--> <b"), inside, tokens);
    Check(tag && tag != inside && tokens[0] == Prog_Token_Comment && tokens[3] == Prog_Token_Plain && tokens[4] == Prog_Token_Heading);
    Check(Prog_SyntaxLine(Prog_Syntax_Markup, S(" id=\"y\" />"), tag, tokens) == 0 && tokens[1] == Prog_Token_Keyword && tokens[4] == Prog_Token_String && tokens[9] == Prog_Token_Heading);
    // CMake's commands and arguments, its variables and comments; a Makefile's targets, variables and directives.
    Check(Prog_SyntaxLine(Prog_Syntax_CMake, S("target_link_libraries(app PUBLIC ${LIBS}) # c"), 0, tokens) == 0);
    Check(tokens[0] == Prog_Token_Keyword && tokens[20] == Prog_Token_Keyword && tokens[22] == Prog_Token_Plain && tokens[26] == Prog_Token_Keyword && tokens[33] == Prog_Token_Number && tokens[42] == Prog_Token_Comment);
    Check(Prog_SyntaxLine(Prog_Syntax_Make, S("all: main.o $(OBJ)"), 0, tokens) == 0 && tokens[0] == Prog_Token_Heading && tokens[2] == Prog_Token_Heading && tokens[5] == Prog_Token_Plain);
    Check(tokens[12] == Prog_Token_Number && tokens[17] == Prog_Token_Number);
    Check(Prog_SyntaxLine(Prog_Syntax_Make, S("CC := gcc"), 0, tokens) == 0 && tokens[0] == Prog_Token_Plain);
    Check(Prog_SyntaxLine(Prog_Syntax_Make, S("ifeq ($(A),1)"), 0, tokens) == 0 && tokens[0] == Prog_Token_Keyword && tokens[6] == Prog_Token_Number);
    Check(Prog_SyntaxForPath(S("/a/CMakeLists.txt")) == Prog_Syntax_CMake && Prog_SyntaxForPath(S("/a/Makefile")) == Prog_Syntax_Make && Prog_SyntaxForPath(S("C:/a/App.vcxproj")) == Prog_Syntax_Markup);
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
    Check(StrIsEqual(git.branch, S("topic")) && git.ahead == -1 && git.behind == -1 && StrIsEqual(git.root, fixture->path));
    // A command's own environment reaches it, over the program's, as git's quieting does; git var prints the editor it was given.
    {
        String editor[] = { S("GIT_EDITOR=filey-test-editor") }, asked[] = { S("git"), S("var"), S("GIT_EDITOR") }, said = {0};
        Check(P_RunCommandWith(&arena, fixture->path, asked, CArrayCount(asked), editor, CArrayCount(editor), 10000, &said, false, NULL));
        Check(StrIndexStr(said, S("filey-test-editor"), SSF_None).exists);
        // Git runs without asking: ssh in BatchMode, unless a program of the user's own takes no ssh options.
        String quiet[Prog_GitQuietCount], batch = S(" -o BatchMode=yes");
        Check(Prog__GitQuiet(&arena, fixture->path, false, quiet) == 3 && StrIsEqual(quiet[2], S("GIT_ASKPASS=")));
        u32 quietCount = Prog__GitQuiet(&arena, fixture->path, true, quiet);
        Check(quietCount >= 3 && StrIsEqual(quiet[0], S("GIT_TERMINAL_PROMPT=0")));
        Check(quietCount == 3 || (quiet[3].length > batch.length && StrIsEqual(StrFrom(quiet[3], quiet[3].length - batch.length), batch)));
        Check(P_EnvironmentValue(&arena, S("PATH")).length && !P_EnvironmentValue(&arena, S("FILEY_TEST_UNSET_VARIABLE")).length);
    }
    Check(P__TestRemovePath(fixture, S(".git/HEAD"), false) && P__TestRemovePath(fixture, S(".git"), true));
    String part = {0};
    Check(P_ReadFileAt(&arena, plain, 1, 8, &part) && StrIsEqual(part, S("\x02")));
    u64 plainSize = 0;
    Check(P_FileSize(plain, &plainSize) && plainSize == 2 && !P_FileSize(fixture->path, &plainSize) && !P_FileSize(Prog_JoinPath(&arena, fixture->path, S("missing.bin")), &plainSize));
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
#if defined(_WIN32)
        // A drag of the archive's items, which the shell drops, is kept by the names they take where they land, to select them there.
        {
            P__EnsureCom();
            IShellFolder *shell = P__ShellFolder(P__FilePathWide(&arena, folder));
            IDataObject *data = NULL;
            Check(shell && P__ShellChildren(shell, &name, 1, &P__IID_IDataObject, (void **)&data));
            if (data) {
                P__drop.bound = out;
                P__DropKeepNames(data);
                String landedIn = {0};
                ArrayStr landed = P_TakeShellDropped(&arena, &landedIn);
                Check(landed.header.count == 1 && StrIsEqual(landed.v[0], S("a.txt")) && StrIsEqual(landedIn, out));
                data->lpVtbl->Release(data);
                P__drop.bound = (String){0};
            }
            if (shell) shell->lpVtbl->Release(shell);
        }
#endif
        String copy = Prog_ArchiveCopy(&arena, Prog_JoinPath(&arena, folder, S("a.txt")), NULL, NULL);
        Check(copy.length && P_ReadFile(&arena, copy, &contents) && StrIsEqual(contents, S("hi")));
        String note = Prog_JoinPath(&arena, out, S("note.txt"));
        Check(!P_WriteFile(&arena, note, S("new")).code);
        Check(!P_AddToArchive(&arena, archive, S("docs"), &note, 1, 9, NULL, NULL).code);
        docs = P_EnumerateArchive(&arena, archive, S("docs"), NULL);
        Check(docs.files.header.count == 2 && P_EnumerateArchive(&arena, archive, (String){0}, NULL).files.header.count == 1);
        // The program lists the folder from the archive, and a transfer out of it takes the file out.
        Prog_State listed = {0};
        Check(Prog_Init(&listed, folder, NULL, NULL) && listed.jobs.initialized);
        Check(TestWaitDirectory(&listed) && listed.directory.archive && listed.directory.count == 2 && !listed.directory.error.code);
        Prog_Release(&listed);
        // A name the system's code page has no letters for goes in as it is.
        String unicode = Prog_JoinPath(&arena, out, S("ünï 日本.txt"));
        Check(!P_WriteFile(&arena, unicode, S("u")).code && !P_AddToArchive(&arena, archive, (String){0}, &unicode, 1, 9, NULL, NULL).code);
        top = P_EnumerateArchive(&arena, archive, (String){0}, NULL);
        Check(top.files.header.count == 2 && (StrIsEqual(top.files.v[0].name, S("ünï 日本.txt")) || StrIsEqual(top.files.v[1].name, S("ünï 日本.txt"))));
        // An add counts its progress, and a cancelled one leaves the archive as it was, with nothing beside it.
        P_Progress counted = {0};
        String more = Prog_JoinPath(&arena, out, S("more.txt"));
        Check(!P_WriteFile(&arena, more, S("more")).code && !P_AddToArchive(&arena, archive, (String){0}, &more, 1, 9, &counted, NULL).code);
        Check(atomic_load(&counted.doneItems) == 1 && atomic_load(&counted.totalBytes) > 4 && atomic_load(&counted.doneBytes) >= 4);
        P_CancelToken stop = {0};
        P_RequestCancel(&stop);
        Check(P_AddToArchive(&arena, archive, (String){0}, &note, 1, 9, NULL, &stop).code && P_EnumerateArchive(&arena, archive, (String){0}, NULL).files.header.count == 3);
        Check(!P_PathExists(Prog_JoinPath(&arena, fixture->path, S(".pack.zip.part"))));
        String moved = {0}, from = Prog_JoinPath(&arena, folder, S("note.txt"));
        Check(!P_CreateItem(&arena, fixture->path, S("Out2"), true, &moved).code);
        Prog_JobInput transfer = { .kind = Prog_JobKind_Transfer, .paths = &from, .pathCount = 1, .path = Prog_JoinPath(&arena, fixture->path, S("Out2")) };
        ArrayStr placed = {0};
        Check(!Prog__Transfer(&arena, &transfer, &placed, NULL).code);
        Check(P_ReadFile(&arena, Prog_JoinPath(&arena, transfer.path, S("note.txt")), &contents) && StrIsEqual(contents, S("new")));
        // What it took out is where it was placed, which is selected afterwards.
        Check(placed.header.count == 1 && StrIsEqual(placed.v[0], Prog_JoinPath(&arena, transfer.path, S("note.txt"))));
        // Packing makes a new archive beside the items, and taking it out into a folder of its own puts its items there.
        {
            String packed[2] = { Prog_JoinPath(&arena, out, S("a.txt")), Prog_JoinPath(&arena, out, S("note.txt")) };
            Prog_JobInput pack = { .kind = Prog_JobKind_Transfer, .transferOp = Prog_TransferOp_Pack, .paths = packed, .pathCount = 2, .path = transfer.path, .name = S("pack2.zip") };
            ArrayStr made = {0};
            Check(!Prog__Transfer(&arena, &pack, &made, NULL).code && made.header.count == 1);
            String archive2 = made.header.count ? made.v[0] : (String){0};
            Check(P_EnumerateArchive(&arena, archive2, (String){0}, NULL).files.header.count == 2);
            Prog_JobInput unpack = { .kind = Prog_JobKind_Transfer, .transferOp = Prog_TransferOp_ExtractToFolder, .paths = &archive2, .pathCount = 1, .path = transfer.path, .name = S("pack2") };
            ArrayStr taken = {0};
            Check(!Prog__Transfer(&arena, &unpack, &taken, NULL).code && taken.header.count == 1);
            Check(taken.header.count && P_ReadFile(&arena, Prog_JoinPath(&arena, taken.v[0], S("note.txt")), &contents) && StrIsEqual(contents, S("new")));
            Check(Prog__NameStem(S("box.tar.gz")).length == 3 && StrIsEqual(Prog__NameStem(S("notes.txt")), S("notes")) && StrIsEqual(Prog__NameStem(S("README")), S("README")));
            Check(P__TestRemovePath(fixture, S("Out2/pack2/a.txt"), false) && P__TestRemovePath(fixture, S("Out2/pack2/note.txt"), false) && P__TestRemovePath(fixture, S("Out2/pack2"), true));
            Check(P__TestRemovePath(fixture, S("Out2/pack2.zip"), false));
            // Level 0 stores; 9 squeezes a text that repeats into far less.
            StrBuilder repeated = {0};
            for (u32 i = 0; i < 4000; ++i) StrBuildStr(&arena, &repeated, S("hello "));
            String text = Prog_JoinPath(&arena, transfer.path, S("text.txt")), stored = {0}, squeezed = {0};
            Check(!P_WriteFile(&arena, text, repeated.buffer).code);
            Prog_JobInput loose = { .kind = Prog_JobKind_Transfer, .transferOp = Prog_TransferOp_Pack, .archiveLevel = 0, .paths = &text, .pathCount = 1, .path = transfer.path, .name = S("stored.zip") };
            Prog_JobInput tight = loose;
            tight.archiveLevel = 9, tight.name = S("squeezed.zip");
            ArrayStr unused = {0};
            Check(!Prog__Transfer(&arena, &loose, &unused, NULL).code && !Prog__Transfer(&arena, &tight, &unused, NULL).code);
            Check(P_ReadFile(&arena, Prog_JoinPath(&arena, transfer.path, S("stored.zip")), &stored) && P_ReadFile(&arena, Prog_JoinPath(&arena, transfer.path, S("squeezed.zip")), &squeezed));
            Check(stored.length > repeated.buffer.length && squeezed.length * 10 < stored.length);
            Check(P__TestRemovePath(fixture, S("Out2/text.txt"), false) && P__TestRemovePath(fixture, S("Out2/stored.zip"), false) && P__TestRemovePath(fixture, S("Out2/squeezed.zip"), false));
        }
        Check(P__TestRemovePath(fixture, S("Out/a.txt"), false) && P__TestRemovePath(fixture, S("Out/note.txt"), false) && P__TestRemovePath(fixture, S("Out/ünï 日本.txt"), false) && P__TestRemovePath(fixture, S("Out/more.txt"), false) &&
              P__TestRemovePath(fixture, S("Out"), true));
        Check(P__TestRemovePath(fixture, S("Out2/note.txt"), false) && P__TestRemovePath(fixture, S("Out2"), true));
        // A tar.gz rewritten to add a file stays a gzipped tar, as tar itself reads it back.
        String output = {0}, tarred = Prog_JoinPath(&arena, fixture->path, S("box.tar.gz"));
        const String make[] = { S("tar"), S("-czf"), S("box.tar.gz"), S("inside-tar.txt") };
        Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, fixture->path, S("inside-tar.txt")), S("t")).code);
        Check(P_RunCommand(&arena, fixture->path, make, CArrayCount(make), 10000, &output, false));
        String added = Prog_JoinPath(&arena, fixture->path, S("pixel.png"));
        Check(!P_AddToArchive(&arena, tarred, (String){0}, &added, 1, 9, NULL, NULL).code);
        const String list[] = { S("tar"), S("-tzf"), S("box.tar.gz") };
        Check(P_RunCommand(&arena, fixture->path, list, CArrayCount(list), 10000, &output, false) && StrIndexStr(output, S("inside-tar.txt"), SSF_None).exists &&
              StrIndexStr(output, S("pixel.png"), SSF_None).exists);
        Check(P_ArchiveWritable(tarred) && P_ArchiveWritable(S("a.7z")) && !P_ArchiveWritable(S("a.rar")) && !P_ArchiveWritable(S("a.cab")));
#if defined(_WIN32)
        // Windows' tar makes a 7z too, which then takes a file as well.
        String sevened = Prog_JoinPath(&arena, fixture->path, S("box.7z"));
        const String pack[] = { S("tar"), S("-a"), S("-cf"), S("box.7z"), S("inside-tar.txt") };
        Check(P_RunCommand(&arena, fixture->path, pack, CArrayCount(pack), 10000, &output, false));
        Check(!P_AddToArchive(&arena, sevened, (String){0}, &added, 1, 9, NULL, NULL).code && P_EnumerateArchive(&arena, sevened, (String){0}, NULL).files.header.count == 2);
        Check(P__TestRemovePath(fixture, S("box.7z"), false));
#endif
        Check(P__TestRemovePath(fixture, S("box.tar.gz"), false) && P__TestRemovePath(fixture, S("inside-tar.txt"), false));
    }
    Check(P__TestRemovePath(fixture, S("pack.zip"), false));
    // A zip whose entry is encrypted lists as locked, and only its password opens it; once given, the entry comes out with it. Written
    // with AES, whose check a wrong password passes once in 65536 tries, where the old zip encryption's passes once in 256.
    {
        P__LoadArchive();
        P__Archive *a = &P__archive;
        i32 (*setPassphrase)(void *, const char *) = NULL;
        *(void **)&setPassphrase = P__ArchiveSymbol("archive_write_set_passphrase");
        String locked = Prog_JoinPath(&arena, fixture->path, S("locked.zip")), content = S("behind a password");
        const void *native = P__ArchiveNativePath(&arena, locked);
        void *writer = setPassphrase && a->writable ? a->writeNew() : NULL, *entry = NULL;
        b32 written = writer && a->writeFormat(writer, P__ArchiveZip) == 0 && a->writeOptions(writer, "zip:encryption=aes256") == 0 &&
                      setPassphrase(writer, "secret") == 0 && a->writeOpen(writer, native) == 0 && (entry = a->entryNew()) != NULL;
        if (written) {
            a->entrySetPath(entry, "inside.txt");
            a->entrySetFiletype(entry, 0100000);
            a->entrySetPerm(entry, 0644);
            a->entrySetSize(entry, (i64)content.length);
            written = a->writeHeader(writer, entry) == 0 && a->writeData(writer, content.value, content.length) == (i64)content.length;
            a->entryFree(entry);
        }
        if (writer) {
            written = a->writeClose(writer) == 0 && written;
            a->writeFree(writer);
        }
        // Only where libarchive was built with the means to encrypt, as Windows' is.
#if defined(_WIN32)
        Check(written);
#endif
        if (written) {
            P_DirectoryResult listed = P_EnumerateArchive(&arena, locked, (String){0}, NULL);
            Check(listed.locked && listed.files.header.count == 1 && !P_ArchiveHasPassword(locked));
            Check(!P_ArchiveCheckPassword(locked, S("wrong"), NULL) && P_ArchiveCheckPassword(locked, S("secret"), NULL));
            String name = S("inside.txt"), out = Prog_JoinPath(&arena, fixture->path, name), read = {0};
            P_FileError failed = P_ExtractArchive(&arena, locked, (String){0}, &name, 1, fixture->path, NULL, NULL);
            Check(failed.code && StrIndexStr(failed.message, S("password"), 0).exists && !P_PathExists(out));
            P_ArchiveSetPassword(locked, S("secret"));
            Check(P_ArchiveHasPassword(locked) && !P_ExtractArchive(&arena, locked, (String){0}, &name, 1, fixture->path, NULL, NULL).code);
            Check(P_ReadFile(&arena, out, &read) && StrIsEqual(read, content));
            Check(P__TestRemovePath(fixture, name, false));
        }
        if (P_PathExists(locked)) Check(P__TestRemovePath(fixture, S("locked.zip"), false));
    }
    Check(P__TestRemovePath(fixture, S("pixel.png"), false));
    Check(P__TestRemovePath(fixture, S("plain.bin"), false));

    // Menus are built and read here; only New > Folder is invoked, in the test's own folder.
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
    // A menu prepared is built on the menu thread a moment later, and the open with the same arguments takes it, its items copied out of the menu thread's arena.
    P_ContextMenuPrepare(fixture->path, names, 1);
    u64 start = PlatformNowMicroseconds();
    b32 ready = false;
    while (!ready && PlatformNowMicroseconds() - start < Millions(10)) {
        AcquireSRWLockExclusive(&P__menus.lock);
        ready = P__menus.readySerial && P__menus.readySerial == P__menus.wantSerial;
        ReleaseSRWLockExclusive(&P__menus.lock);
        if (!ready) Sleep(10);
    }
    Check(ready);
    opened = P_ContextMenuOpen(&arena, fixture->path, names, 1, &menu);
    Check(opened && menu.rootCount && !P__menus.readySerial && !P__menus.ready.native);
    P_ContextMenuClose(&menu);
    // Taken, the same arguments are wanted again.
    u64 wanted = P__menus.wantSerial;
    P_ContextMenuPrepare(fixture->path, names, 1);
    Check(P__menus.wantSerial == wanted + 1);
    opened = P_ContextMenuOpen(&arena, fixture->path, NULL, 0, &menu);
    Check(opened && menu.rootCount);
    // Its New > Folder, run on the menu thread, makes a folder here, which is taken away again.
    u32 newFolder = 0;
    for (u32 i = 0; opened && i < menu.count; ++i) if (StrIsEqual(menu.items[i].verb, S("newfolder"))) newFolder = menu.items[i].id;
    P_FileArray before = P_EnumerateDirectory(&arena, fixture->path, NULL).files, after = before;
    Check(newFolder && !P_ContextMenuInvoke(&arena, &menu, newFolder).code);
    for (u64 start = PlatformNowMicroseconds(); after.header.count == before.header.count && PlatformNowMicroseconds() - start < Millions(5); Sleep(10)) {
        after = P_EnumerateDirectory(&arena, fixture->path, NULL).files;
    }
    Check(after.header.count == before.header.count + 1);
    for (u64 i = 0; i < after.header.count; ++i) {
        b32 seen = false;
        for (u64 j = 0; j < before.header.count && !seen; ++j) seen = StrIsEqual(after.v[i].name, before.v[j].name);
        if (!seen) Check(after.v[i].kind == P_FileKind_Directory && P__TestRemovePath(fixture, after.v[i].name, true));
    }
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
    // Where the system has permissions, the listing has them, and they can be set; Windows has none to set.
    {
        String plainPath = Prog_JoinPath(&arena, fixture->path, S("plain.txt"));
#if defined(_WIN32)
        Check(P_SetMode(&arena, plainPath, 0755).code);
#else
        Check(!P_SetMode(&arena, plainPath, 0755).code);
        P_DirectoryResult listed = P_EnumerateDirectory(&arena, fixture->path, NULL);
        u16 mode = 0;
        for (u64 i = 0; i < listed.files.header.count; ++i) if (StrIsEqual(listed.files.v[i].name, S("plain.txt"))) mode = listed.files.v[i].mode;
        Check(mode == 0755);
        Check(!P_SetMode(&arena, plainPath, 0644).code);
#endif
    }

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
    // The mouse's back button cancels it too, on its way back.
    Prog_BeginRename(&state, TestEntryIndex(&state, S("plain.txt")));
    TestRenameType(&state, S("gone"));
    TestRenameKey(&state, Key_MouseX1, 0);
    Check(!state.renaming && TestWaitDirectory(&state) && TestEntryIndex(&state, S("plain.txt")) != UINT64_MAX && TestEntryIndex(&state, S("gone.txt")) == UINT64_MAX);

    // Enter renames on a job, then the new name is selected.
    Prog_BeginRename(&state, TestEntryIndex(&state, S("plain.txt")));
    TestRenameKey(&state, Key_A, KeyMod_Control | KeyMod_Shift);
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

    // The one item a shell command adds is selected, as an archiver's new archive is, and goes into rename after one of New's.
    Check(P__TestWriteFile(fixture, S("archived.zip"), 1, false));
    state.renameCreatedUntil = PlatformNowMicroseconds() + Millions(5);
    state.createdRenames = false;
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
    Check(!state.renaming && state.selectedCount == 1 && state.hasFocus && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("archived.zip")));
    Check(P__TestRemovePath(fixture, S("archived.zip"), false));
    TestBrowsingCommand(&state, Prog_CommandKind_Refresh, 0);
    Check(P__TestWriteFile(fixture, S("created.txt"), 1, false));
    state.renameCreatedUntil = PlatformNowMicroseconds() + Millions(5);
    state.createdRenames = true;
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
    TestRenameKey(&state, Key_A, KeyMod_Control | KeyMod_Shift);
    TestRenameType(&state, S("folder2"));
    Check(state.viewCount == 1 && state.hasFocus);
    // Under a filter the view is in the order of the matches, and each row still finds its place in it, so that several can be selected and
    // a delete takes those and no others.
    {
        TestRenameKey(&state, Key_A, KeyMod_Control | KeyMod_Shift);
        TestRenameType(&state, S("txt"));
        Check(state.viewCount >= 3);
        for (u64 p = 0; p < state.viewCount; ++p) Check(Prog_ViewPosition(&state, state.view[p]) == p);
        u64 first = state.view[0], third = state.view[2];
        ArenaClear(&state.frameArena);
        Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, first, 1, 0);
        Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, third, 1, 0)->mods = KeyMod_Control;
        Prog_ProcessCommands(&state);
        String *paths = NULL;
        u64 picked = Prog__SelectedPaths(&state, &state.frameArena, &paths);
        Check(state.selectedCount == 2 && picked == 2 && state.directory.entries[first].selected && state.directory.entries[third].selected);
        TestRenameKey(&state, Key_A, KeyMod_Control | KeyMod_Shift);
        TestRenameType(&state, S("folder2"));
        Check(state.viewCount == 1 && state.hasFocus);
    }

    // The clipboard and clicks in the box.
    TestRenameKey(&state, Key_A, KeyMod_Control | KeyMod_Shift);
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

// A custom scheme is a file of its own in the settings folder, as Matugen renders one, read in the order of the names, its comments and
// carriage returns aside. One made in the options is written as such a file, and one taken out is deleted.
function void TestSchemeFiles(TestFileFixture *fixture)
{
    Prog_State state = { .configDirectory = fixture->path };
    Arena arena = {0};
    Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, fixture->path, S("Wallpaper.scheme")), S("# Filey's colors from the wallpaper\r\nBackground = #102030\r\nText = #e2e2e9\r\n")).code);
    Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, fixture->path, S("autumn.scheme")), S("Background = #302010\n")).code);
    Prog_LoadSchemes(&state);
    Check(state.schemeNames.header.count == 2 && StrIsEqual(state.schemeNames.v[0], S("autumn")) && StrIsEqual(state.schemeNames.v[1], S("Wallpaper")));
    Prog_ApplySettings(&state, S("theme = wallpaper\n"));
    Check(state.scheme == Prog_BuiltInSchemes + 1 && state.schemeGroups[Prog_Option_ColorBackground - Prog_Option_ColorClear] == 0x102030 &&
          state.schemeGroups[Prog_Option_ColorText - Prog_Option_ColorClear] == 0xE2E2E9);
    Prog_SetScheme(&state, S("Mine"), S("Background = #000000\n"), false);
    String saved = {0};
    Check(P_ReadFile(&arena, Prog_JoinPath(&arena, fixture->path, S("Mine.scheme")), &saved) && StrIsEqual(saved, S("Background = #000000\n")));
    Check(state.schemeNames.header.count == 3 && StrIsEqual(state.schemeNames.v[1], S("Mine")));
    Prog_SetScheme(&state, S("Mine"), (String){0}, true);
    Check(!P_PathExists(Prog_JoinPath(&arena, fixture->path, S("Mine.scheme"))) && state.schemeNames.header.count == 2);
    Check(P__TestRemovePath(fixture, S("Wallpaper.scheme"), false) && P__TestRemovePath(fixture, S("autumn.scheme"), false));
    ArenaRelease(&state.schemesArena);
    ArenaRelease(&arena);
}

// The window opens as its settings make it look: in the default scheme's color without any, their scheme's, a custom one's by its file, or
// their own color.window, which a theme line after it replaces; or see-through over the backdrop they choose.
function u32 TestStartupColor(String folder)
{
    PlatformWindowDesc desc = {0};
    Prog_StartupWindow(folder, &desc);
    return desc.background;
}

function void TestStartupWindowColor(TestFileFixture *fixture)
{
    Arena arena = {0};
    String settings = Prog_JoinPath(&arena, fixture->path, S("Settings.conf"));
    Check(TestStartupColor(fixture->path) == Prog_SchemeColors[0][0]);
    Check(!P_WriteFile(&arena, settings, S("theme = filey-dark\n")).code && TestStartupColor(fixture->path) == 0x191919);
    Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, fixture->path, S("Night.scheme")), S("Clear = #0a0b0c\r\n")).code);
    Check(!P_WriteFile(&arena, settings, S("theme = night\n")).code && TestStartupColor(fixture->path) == 0x0A0B0C);
    Check(!P_WriteFile(&arena, settings, S("color.window = #123456\ntheme = night\n")).code && TestStartupColor(fixture->path) == 0x0A0B0C);
    Check(!P_WriteFile(&arena, settings, S("theme = night\ncolor.window = #123456\n")).code && TestStartupColor(fixture->path) == 0x123456);
    PlatformWindowDesc glass = {0};
    Check(!P_WriteFile(&arena, settings, S("theme = night\ntransparent = true\nglass.backdrop = Mica\n")).code);
    Prog_StartupWindow(fixture->path, &glass);
    Check(glass.transparent && glass.backdrop == PlatformBackdrop_Mica);
    Check(P__TestRemovePath(fixture, S("Night.scheme"), false) && P__TestRemovePath(fixture, S("Settings.conf"), false));
    ArenaRelease(&arena);
}

// git.fetch fetches a repository at most every few minutes and one at a time, in folders that are not there, where git cannot even start.
// The panel shows This PC, in no repository, so that no real one around the fixture is fetched after.
function void TestGitFetchLimit(TestFileFixture *fixture)
{
    Prog_State state = {0};
    Check(Prog_Init(&state, Prog_ThisPC, NULL, NULL) && state.jobs.initialized);
    Arena arena = {0};
    String first = Prog_JoinPath(&arena, fixture->path, S("No repository")), second = Prog_JoinPath(&arena, fixture->path, S("No other repository"));
    Prog__FetchGit(&state, first);
    Check(!state.gitFetchJob);
    state.gitFetch = true;
    Prog__FetchGit(&state, first);
    P_Job *fetching = state.gitFetchJob;
    Prog__FetchGit(&state, second);
    Check(fetching && state.gitFetchJob == fetching);
    for (u64 start = PlatformNowMicroseconds(); state.gitFetchJob && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) Prog_TakeCompletions(&state);
    Check(!state.gitFetchJob);
    Prog__FetchGit(&state, first);
    Check(!state.gitFetchJob);
    Prog__FetchGit(&state, second);
    Check(state.gitFetchJob != NULL);
    for (u64 start = PlatformNowMicroseconds(); state.gitFetchJob && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) Prog_TakeCompletions(&state);
    ArenaRelease(&arena);
    Prog_Release(&state);
}

// Run folders of runs that are over go, with what is in them; this run's own and folders of other names stay.
// Here in a folder of the test's own rather than the real one beside the run folder.
function void TestStaleRunFolders(TestFileFixture *fixture)
{
    Arena arena = {0};
    String root = Prog_JoinPath(&arena, fixture->path, S("Runs")), made = {0};
    Check(!P_CreateItem(&arena, fixture->path, S("Runs"), true, &made).code);
    // A number no process has, odd as no Windows process's is and past where Linux numbers them, one taken for this run's, and a folder of
    // another name.
    String gone = S("2000000001"), own = S("123"), other = S("keep");
    String names[] = { gone, own, other };
    for (u32 i = 0; i < CArrayCount(names); ++i) {
        Check(!P_CreateItem(&arena, root, names[i], true, &made).code);
        String folder = Prog_JoinPath(&arena, root, names[i]);
        Check(!P_CreateItem(&arena, folder, S("inner"), true, &made).code);
        Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, folder, S("inner/copy.txt")), S("taken out")).code);
    }
    P__RemoveStaleRunFolders(root, 123);
    Check(!P_PathExists(Prog_JoinPath(&arena, root, gone)) && P_PathExists(Prog_JoinPath(&arena, root, own)) && P_PathExists(Prog_JoinPath(&arena, root, other)));
    Check(!P_DeleteItems(&arena, &root, 1, false, NULL).code);
    ArenaRelease(&arena);
}

// A big text file's lines counted in steps: each step's first line, its number and the highlighter's state there, across a block comment
// that spans a step's start; and a window's lines from a state.
function void TestTextIndex(TestFileFixture *fixture)
{
    Arena arena = {0};
    u32 *starts = NULL;
    u8 *states = NULL;
    Check(Prog_TextLines(&arena, S("a\n/* b\nc */\nd"), Prog_Syntax_C, 0, &starts, &states) == 4 && starts[1] == 2 && starts[4] == 13 && !states[1] && states[2] && !states[3]);
    Check(StrIsEqual(Prog__WholeLines(S("one\ntwo\nthr"), false), S("one\ntwo\n")) && StrIsEqual(Prog__WholeLines(S("one\ntwo"), true), S("one\ntwo")));
    // 40 bytes a line: a comment opens at line 70000, past the third step's start, and closes at 90000.
    StrBuilder text = {0};
    for (u32 i = 0; i < 100000; ++i) {
        String body = i == 70000 ? S("/* a comment starts") : i == 90000 ? S("   and it ends */") : S("int value = 1; // a line");
        char line[40];
        for (u32 c = 0; c < sizeof(line); ++c) line[c] = c < body.length ? body.value[c] : ' ';
        line[39] = '\n';
        StrBuildStr(&arena, &text, StrMake(line, sizeof(line)));
    }
    String path = Prog_JoinPath(&arena, fixture->path, S("big.c"));
    Check(text.buffer.length == 100000 * 40 && !P_WriteFile(&arena, path, text.buffer).code);
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL) && state.jobs.initialized && TestWaitDirectory(&state));
    state.previewWindowed = true;
    state.previewPath = state.textReadPath = path;
    state.previewSize = text.buffer.length;
    Prog__RequestTextIndex(&state);
    for (u64 start = PlatformNowMicroseconds(); state.textIndexJob && PlatformNowMicroseconds() - start < Millions(20); P__TestPause()) Prog_TakeCompletions(&state);
    const Prog_TextIndex *index = &state.textIndex;
    Check(index->done && index->lines == 100000 && index->count == 4);
    // Each step's first line starts at or after the step's start, on the 40 bytes of each line.
    for (u64 step = 0; index->done && step < index->count; ++step) {
        u64 number = (step * Prog_TextStep + 39) / 40;
        Check(index->numbers[step] == number && index->offsets[step] == number * 40);
    }
    // The third step starts at line 52429, before the comment; the fourth at 78644, inside it.
    Check(index->count == 4 && !index->states[2] && index->states[3]);
    // Its filter finds the lines it keeps through the whole file, numbered, with where the highlighter is at each.
    String needles[] = { S("comment"), S("ends") };
    u64 numbers[] = { 70000, 90000 };
    for (u32 n = 0; n < CArrayCount(needles); ++n) {
        MemCopy(state.inspectorFilter.text, needles[n].value, needles[n].length);
        state.inspectorFilter.length = (u32)needles[n].length;
        ++state.inspectorFilter.version;
        Prog_RequestTextFind(&state);
        for (u64 start = PlatformNowMicroseconds(); state.textFindJob && PlatformNowMicroseconds() - start < Millions(20); P__TestPause()) Prog_TakeCompletions(&state);
        const Prog_FoundLines *found = &state.textFind;
        Check(found->done && found->version == state.inspectorFilter.version && found->count == 1 && !found->cut && found->numbers[0] == numbers[n]);
        Check(found->count == 1 && found->lines[1] == 40 && found->text.length == 40 && (found->states[0] != 0) == (n == 1));
    }
    Prog_Release(&state);
    Check(P__TestRemovePath(fixture, S("big.c"), false));
    ArenaRelease(&arena);
}

// The Git column's repositories show their commits to push and to pull: a clone one commit ahead of the repository it came from, both made
// here with git, which the tests need on the PATH.
function void TestRepoCommits(TestFileFixture *fixture)
{
    Arena arena = {0};
    String repos = Prog_JoinPath(&arena, fixture->path, S("Repos")), clone = Prog_JoinPath(&arena, repos, S("clone")), made = {0}, output = {0};
    Check(!P_CreateItem(&arena, fixture->path, S("Repos"), true, &made).code);
    String init[] = { S("git"), S("init"), S("-q"), S("--bare"), S("origin.git") }, copy[] = { S("git"), S("clone"), S("-q"), S("origin.git"), S("clone") };
    String commit[] = { S("git"), S("-c"), S("user.name=Test"), S("-c"), S("user.email=test@example.com"), S("-c"), S("commit.gpgsign=false"), S("commit"), S("-q"), S("--allow-empty"), S("-m"), S("one") };
    String push[] = { S("git"), S("push"), S("-q"), S("-u"), S("origin"), S("HEAD") };
    b32 ready = P_RunCommand(&arena, repos, init, CArrayCount(init), 10000, &output, false) && P_RunCommand(&arena, repos, copy, CArrayCount(copy), 10000, &output, false) &&
                P_RunCommand(&arena, clone, commit, CArrayCount(commit), 10000, &output, false) && P_RunCommand(&arena, clone, push, CArrayCount(push), 10000, &output, false) &&
                P_RunCommand(&arena, clone, commit, CArrayCount(commit), 10000, &output, false);
    Check(ready);
    if (ready) {
        Prog_State state = {0};
        Check(Prog_Init(&state, repos, NULL, NULL) && state.jobs.initialized && TestWaitDirectory(&state));
        String name = S("clone");
        for (u32 pass = 0; pass < 2; ++pass) {
            Prog__RepoJob(&state, &name, 1, pass == 1);
            for (u64 start = PlatformNowMicroseconds(); state.reposJob && PlatformNowMicroseconds() - start < Millions(20); P__TestPause()) Prog_TakeCompletions(&state);
        }
        const Prog_Repo *repo = MapsGetPtrNull(state.repos, name);
        Check(repo && repo->branch.length && repo->changes == 0 && repo->ahead == 1 && repo->behind == 0);
        Prog_Release(&state);
    }
    Check(!P_DeleteItems(&arena, &repos, 1, false, NULL).code && !P_PathExists(repos));
    ArenaRelease(&arena);
}

// The Quick Access filter: its hotkey gives its box the keyboard, typing finds sidebar items by their names' letters in order, Enter keeps the
// filter and hands the keyboard back, Escape takes back what was typed since the box got the keyboard, and the clear button clears it.
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
    TestRenameKey(&state, Key_O, 0);
    TestRenameType(&state, S("o"));
    Check(StrIsEqual(Prog_EditText(&state.sidebarFilter), S("dlo")));
    TestRenameKey(&state, Key_Escape, 0);
    Check(!state.sidebarFiltering && StrIsEqual(Prog_EditText(&state.sidebarFilter), S("dl")));
    TestBrowsingCommand(&state, Prog_CommandKind_QuickAccessFilterCancel, 0);
    Check(!state.sidebarFiltering && !state.sidebarFilter.length && Prog__SidebarFinds(&state, S("Desktop")));
    Prog_Release(&state);
}

// Copy As Path (Smart) quotes only the paths a command line would split; Quick Commands lists the starred commands alone; Paste Shortcut's
// links are numbered when their names are taken.
function void TestPasteLinkAndQuick(TestFileFixture *fixture)
{
    Arena arena = {0};
    String paths[] = { S("C:/plain/file.txt"), S("C:/with space/a.txt"), S("C:/x&y") };
    String quoted = Prog_QuotedPaths(&arena, paths, CArrayCount(paths));
    Check(StrIsEqual(quoted, StrFormat(&arena, "%S\n\"%S\"\n\"%S\"", P_NativePath(&arena, paths[0]), P_NativePath(&arena, paths[1]), P_NativePath(&arena, paths[2]))));
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    String quick[] = { S("Refresh"), S("TabNew") };
    Prog__SetList(&state.quickArena, &state.quickCommands, quick, CArrayCount(quick));
    state.paletteQuick = true;
    Prog_PaletteRow rows[Prog_MaxPaletteRows];
    u32 count = Prog_PaletteMatches(&state, rows);
    Check(count == 2 && rows[0].kind == Prog_CommandKind_Refresh && rows[1].kind == Prog_CommandKind_TabNew);
    String target = Prog_JoinPath(&arena, fixture->path, S("Folder10")), first = {0}, second = {0};
    Check(!P_CreateLink(&arena, fixture->path, target, &first).code && !P_CreateLink(&arena, fixture->path, target, &second).code);
    Check(first.length && second.length && !StrIsEqual(first, second));
    Check(P_PathExists(Prog_JoinPath(&arena, fixture->path, first)) && P_PathExists(Prog_JoinPath(&arena, fixture->path, second)));
    P_DeleteItems(&arena, (String[]){ Prog_JoinPath(&arena, fixture->path, first), Prog_JoinPath(&arena, fixture->path, second) }, 2, false, NULL);
    Prog_Release(&state);
    ArenaRelease(&arena);
}

// Evening panels: three split off one after another to the right share the row evenly, and a column split inside one keeps its own share
// until all panels are evened. Swapping trades the active panel's split's sides at their sizes, and resetting the layout leaves the
// active panel alone, the others' tabs reopenable.
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
    // The current panel's share of its split grows a step and shrinks back, and the sidebar widens a step.
    u32 leaf = Prog__SplitOf(&state, state.panels[state.activePanel].id), parent = Prog__SplitParent(&state, leaf);
    f32 share = state.splits[parent].ratio > 0 ? state.splits[parent].ratio : 0.5f;
    TestBrowsingCommand(&state, Prog_CommandKind_PanelIncreaseViewport, 0);
    Check(fabsf((state.splits[parent].first == leaf ? state.splits[parent].ratio - share : share - state.splits[parent].ratio) - 0.05f) < 1e-5f);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelDecreaseViewport, 0);
    Check(fabsf(state.splits[parent].ratio - share) < 1e-5f);
    TestBrowsingCommand(&state, Prog_CommandKind_QuickAccessIncreaseViewport, 0);
    Check(fabsf(state.sidebarWidth - (Prog_SidebarDefault + 24)) < 1e-4f);
    // The second panel is active, first in the row it shares with the column.
    u64 active = state.panels[state.activePanel].id;
    Check(state.splits[state.splits[row].first].panel == active && state.splits[row].second == column);
    state.splits[row].ratio = 0.3f;
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSwap, 0);
    Check(state.splits[row].first == column && state.splits[state.splits[row].second].panel == active && fabsf(state.splits[row].ratio - 0.7f) < 1e-5f);
    Check(state.panels[state.activePanel].id == active && fabsf(state.splits[0].ratio - 1.0f / 3.0f) < 1e-5f);
    u64 closed = state.closedTabs.header.count;
    TestBrowsingCommand(&state, Prog_CommandKind_PanelResetLayout, 0);
    Check(state.panelCount == 1 && state.panels[0].id == active && state.splits[0].kind == Prog_SplitKind_Panel && state.closedTabs.header.count == closed + 3);
    Check(!Prog_CommandAvailable(&state, Prog_CommandKind_PanelResetLayout) && !Prog_CommandAvailable(&state, Prog_CommandKind_PanelSwap));
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
    TestRenameKey(&state, Key_A, KeyMod_Control | KeyMod_Shift);
    TestRenameType(&state, S("folder2"));
    Check(state.viewCount == 2 && state.view[0] == folder && state.view[1] == inside);
    TestRenameKey(&state, Key_A, KeyMod_Control | KeyMod_Shift);
    TestRenameType(&state, S("inside"));

    // Renaming a nested item edits its name and renames it in its own folder; the filter gives way to the new name.
    Prog_BeginRename(&state, inside);
    Check(state.renaming && StrIsEqual(Prog_EditText(&state.rename), S("inside.txt")));
    TestRenameKey(&state, Key_A, KeyMod_Control | KeyMod_Shift);
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
#if defined(_WIN32)
        // A PDF is its first page, the longer side fitted: a 200 x 100 point page with a red square, on white.
        String pdf = Prog_JoinPath(&arena, fixture->path, S("square.pdf"));
        Check(!P_WriteFile(&arena, pdf, S("%PDF-1.4\n1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
                                          "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents 4 0 R >>\nendobj\n"
                                          "4 0 obj\n<< /Length 25 >>\nstream\n1 0 0 rg 20 20 60 60 re f\nendstream\nendobj\n"
                                          "xref\n0 5\n0000000000 65535 f \n0000000009 00000 n \n0000000058 00000 n \n0000000115 00000 n \n0000000202 00000 n \n"
                                          "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n277\n%%EOF\n")).code);
        Check(P_LoadImageFile(&arena, pdf, 400, &image, &width, &height) && image.width == 400 && image.height == 200);
        Check(image.width == 400 && image.pixels[0] == 0xFFFFFFFFu && image.pixels[100 * 400 + 100] == 0xFF0000FFu);
        Check(P__TestRemovePath(fixture, S("square.pdf"), false));
#endif
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
    // One stopped is a cancel, which is no failure to report.
    P_CancelToken stopped = {0};
    P_RequestCancel(&stopped);
    Check(P_FileErrorIsCancel(P_TransferItems(&arena, &plain, 1, ops, false, NULL, NULL, &stopped)) && !P_FileErrorIsCancel((P_FileError){0}));
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
// Waits for the listing to hold `count` items, the copies and moves under way to be done, and the listing they ask for after.
function b32 TestWaitCount(Prog_State *state, u64 count)
{
    for (u64 start = PlatformNowMicroseconds(); PlatformNowMicroseconds() - start < Millions(5);) {
        b32 transferring = false;
        for (u32 i = 0; i < Prog_MaxTransfers; ++i) transferring |= state->transfers[i].job != NULL;
        if (state->directory.count == count && !state->directoryJob && !transferring) break;
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
    // What was pasted is selected, and only that.
    Check(TestWaitDirectory(&state));
    Check(state.selectedCount == 1 && !state.directory.entries[TestEntryIndex(&state, S("a.txt"))].selected);
    // The keyboard goes with it, off the item copied.
    Check(state.hasFocus && state.directory.entries[state.focusedRow].selected);
    Prog_SelectRow(&state, TestEntryIndex(&state, S("a.txt")), 0);
    TestFileCommand(&state, Prog_CommandKind_Duplicate);
    Check(TestWaitCount(&state, 4) && !state.error.length);

    // Cut and Paste in another folder moves.
    Prog_SelectRow(&state, TestEntryIndex(&state, S("a.txt")), 0);
    TestRenameKey(&state, Key_X, KeyMod_Control);
    Check(TestClipboardCut && Prog__EntryCut(&state, &state.directory.entries[TestEntryIndex(&state, S("a.txt"))]));
    // Copying something else over it leaves it undimmed, and cutting it again dims it.
    TestRenameKey(&state, Key_C, KeyMod_Control);
    Check(!Prog__EntryCut(&state, &state.directory.entries[TestEntryIndex(&state, S("a.txt"))]));
    TestRenameKey(&state, Key_X, KeyMod_Control);
    Check(Prog__EntryCut(&state, &state.directory.entries[TestEntryIndex(&state, S("a.txt"))]));
    TestBrowsingCommand(&state, Prog_CommandKind_SelectRow, TestEntryIndex(&state, S("New folder")));
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, TestEntryIndex(&state, S("New folder")), 2, 0);
    Prog_ProcessCommands(&state);
    Check(TestWaitDirectory(&state) && !state.directory.count);
    TestFileCommand(&state, Prog_CommandKind_Paste);
    Check(!state.error.length && TestEntryIndex(&state, S("a.txt")) != UINT64_MAX);
    Check(!TestHasFile(&arena, fixture, S("Commands"), S("a.txt")) && !state.cutPaths.header.count);

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
        while (PlatformNowMicroseconds() - settle < UINT64_C(2000) * P_WatchPause) P__TestPause();
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
    // The path menu's Copy Path copies the crumb it opened on, a folder above the shown one too; This PC has no path to copy.
    {
        u64 shownEnd = 0;
        String crumbs = Prog__CrumbsPath(&state, &shownEnd), parent = Prog_ParentPath(state.directory.path);
        Check(StrIsEqual(Prog__CrumbTarget(&state, shownEnd), state.directory.path) && StrIsEqual(Prog__CrumbTarget(&state, 0), Prog_ThisPC));
        TestBrowsingCommand(&state, Prog_CommandKind_PathCopy, parent.length);
        Check(crumbs.length && StrIsEqual(TestClipboard, P_NativePath(&arena, parent)));
        PlatformClipboardSetText(S("kept"));
        TestBrowsingCommand(&state, Prog_CommandKind_PathCopy, 0);
        Check(StrIsEqual(TestClipboard, S("kept")));
    }

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
    Check(!state.error.length && TestEntryIndex(&state, S("a.txt")) == UINT64_MAX && TestHasFile(&arena, fixture, S("Commands/data/Trash/files"), S("a.txt")));
    // Undo takes it back out of the trash, selected, and Redo sends it there again.
    TestBrowsingCommand(&state, Prog_CommandKind_Undo, 0);
    for (u64 start = PlatformNowMicroseconds(); TestEntryIndex(&state, S("a.txt")) == UINT64_MAX && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) Prog_TakeCompletions(&state);
    Check(TestWaitDirectory(&state) && !state.error.length && TestEntryIndex(&state, S("a.txt")) != UINT64_MAX && !TestHasFile(&arena, fixture, S("Commands/data/Trash/files"), S("a.txt")));
    Check(state.directory.entries[TestEntryIndex(&state, S("a.txt"))].selected && state.redo.header.count == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_Redo, 0);
    for (u64 start = PlatformNowMicroseconds(); TestEntryIndex(&state, S("a.txt")) != UINT64_MAX && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) Prog_TakeCompletions(&state);
    Check(TestEntryIndex(&state, S("a.txt")) == UINT64_MAX && TestHasFile(&arena, fixture, S("Commands/data/Trash/files"), S("a.txt")));
    if (saved.length) setenv("XDG_DATA_HOME", P__FilePathCString(&arena, saved), 1);
    else unsetenv("XDG_DATA_HOME");

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

// A newer release is told from this build's version, found in GitHub's answer with this system's archive, kept in Update.conf between runs, and
// put in place of an executable as the update does.
function void TestUpdates(TestFileFixture *fixture)
{
    Arena arena = {0};
    Check(Prog_VersionNewer(S("1.2.0"), S("1.1.9")) && Prog_VersionNewer(S("v1.10.0"), S("1.9.5")) && Prog_VersionNewer(S("0.2.0"), S("0.1.0+abc1234")));
    Check(!Prog_VersionNewer(S("1.2.0"), S("1.2.0")) && !Prog_VersionNewer(S("1.2.0"), S("1.2.0-3-gabc1234-dirty")) && !Prog_VersionNewer(S("1.2"), S("1.2.0")));
    Check(!Prog_VersionNewer((String){0}, S("0.1.0")) && !Prog_VersionNewer(S("1.1.9"), S("1.2.0")));
    String answer = S("{\"url\": \"x\", \"html_url\": \"https://github.com/Solessfir/Filey/releases/tag/v1.3.0\", \"tag_name\": \"v1.3.0\", \"assets\": ["
                      "{\"name\": \"Filey-1.3.0-windows-x64.zip\", \"browser_download_url\": \"https:\\/\\/example.com/Filey-1.3.0-windows-x64.zip\"},"
                      "{\"name\": \"filey-1.3.0-linux-x64.tar.gz\", \"browser_download_url\" : \"https://example.com/filey-1.3.0-linux-x64.tar.gz\"}]}");
    Prog_LatestRelease release = Prog_ParseRelease(&arena, answer);
    Check(release.fetched && StrIsEqual(release.version, S("1.3.0")) && StrIsEqual(release.page, S("https://github.com/Solessfir/Filey/releases/tag/v1.3.0")));
    Check(TestEndsWith(release.asset, Prog_ReleaseArchive) && StrIsEqual(StrFromTo(release.asset, 0, 20), S("https://example.com/")));
    Check(!Prog_ParseRelease(&arena, S("{\"message\": \"Not Found\"}")).fetched);
    // Kept while it is newer than this build, and the version offered last either way.
    Prog_State state = {0};
    state.cacheDirectory = fixture->path;
    String file = Prog_JoinPath(&arena, fixture->path, Prog_UpdateFile);
    Prog_LatestRelease later = { S("999.0.0"), S("https://example.com/page"), S("https://example.com/a.zip"), true, false };
    Check(!P_WriteFile(&arena, file, Prog__UpdateText(&arena, 1234, &later, S("999.0.0"))).code);
    Prog__ReadUpdate(&state);
    Check(state.updateChecked == 1234 && StrIsEqual(state.update.version, S("999.0.0")) && StrIsEqual(state.update.asset, S("https://example.com/a.zip")) && StrIsEqual(state.updateShown, S("999.0.0")));
    Prog_LatestRelease old = { S("0.0.1"), S("p"), S("a"), true, false };
    Check(!P_WriteFile(&arena, file, Prog__UpdateText(&arena, 5, &old, S("0.0.1"))).code);
    Prog__ReadUpdate(&state);
    Check(!state.update.version.length && StrIsEqual(state.updateShown, S("0.0.1")));
    ArenaRelease(&state.updateArena);
    P__TestRemovePath(fixture, Prog_UpdateFile, false);
    // The new file takes the old one's place; on Windows the old one moves aside, as the running one has to.
    String executable = Prog_JoinPath(&arena, fixture->path, S("app.exe")), replacement = Prog_JoinPath(&arena, fixture->path, S("new.bin")), contents = {0};
    Check(!P_WriteFile(&arena, executable, S("old")).code && !P_WriteFile(&arena, replacement, S("new")).code);
    Check(!P_ReplaceExecutable(&arena, executable, replacement).code && P_ReadFile(&arena, executable, &contents) && StrIsEqual(contents, S("new")) && !P_PathExists(replacement));
#if defined(_WIN32)
    Check(P_ReadFile(&arena, Prog_JoinPath(&arena, fixture->path, S("app.old.exe")), &contents) && StrIsEqual(contents, S("old")));
    // While an older copy still runs from that name, held open here as a running one is, the next one moved aside takes a name of its own;
    // both go once nothing holds them.
    {
        wchar *held = P__FilePathWide(&arena, P_NativePath(&arena, Prog_JoinPath(&arena, fixture->path, S("app.old.exe"))));
        HANDLE handle = CreateFileW(held, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        Check(handle != INVALID_HANDLE_VALUE && !P_WriteFile(&arena, replacement, S("newer")).code);
        Check(!P_ReplaceExecutable(&arena, executable, replacement).code && P_ReadFile(&arena, executable, &contents) && StrIsEqual(contents, S("newer")));
        String second = StrFormat(&arena, "app.old-%u.exe", (u32)GetCurrentProcessId());
        Check(P_ReadFile(&arena, Prog_JoinPath(&arena, fixture->path, second), &contents) && StrIsEqual(contents, S("new")));
        P_RemoveReplaced(executable);
        Check(P_PathExists(Prog_JoinPath(&arena, fixture->path, S("app.old.exe"))) && !P_PathExists(Prog_JoinPath(&arena, fixture->path, second)));
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        P_RemoveReplaced(executable);
        Check(!P_PathExists(Prog_JoinPath(&arena, fixture->path, S("app.old.exe"))));
    }
#endif
    P__TestRemovePath(fixture, S("app.exe"), false);
    Check(P_ExecutablePath(&arena).length);
    ArenaRelease(&arena);
}

// The finished drive index brought up to date by a folder listed again: its folders found by path whatever their case, an item no longer
// there marked gone with what was inside it, and a new one added in its folder, where the next listing finds it.
function void TestIndexPatch(void)
{
    Prog_Index index = {0};
    Prog_IndexAdd(&index, S("C:/"), UINT32_MAX, 0);
    Prog_IndexAdd(&index, S("Work"), 0, 0);
    Prog_IndexAdd(&index, S("notes.txt"), 1, Prog_IndexFlag_File);
    Prog_IndexAdd(&index, S("Old"), 1, 0);
    Prog_IndexAdd(&index, S("inside.txt"), 3, Prog_IndexFlag_File);
    Prog_IndexLink(&index);
    Check(Prog__IndexFind(&index, S("C:/")) == 0 && Prog__IndexFind(&index, S("c:/work")) == 1 && Prog__IndexFind(&index, S("C:/Work/Old")) == 3);
    Check(Prog__IndexFind(&index, S("C:/Work/notes.txt")) == UINT32_MAX && Prog__IndexFind(&index, S("C:/Missing")) == UINT32_MAX);
    P_File listed[] = { { .name = SInit("NOTES.TXT"), .kind = P_FileKind_File }, { .name = SInit("New"), .kind = P_FileKind_Directory, .hidden = true } };
    Prog_IndexApply(&index, 1, listed, CArrayCount(listed));
    Check(index.count == 6 && StrIsEqual(Prog_IndexName(&index, 5), S("New")) && index.parents[5] == 1 && index.flags[5] == Prog_IndexFlag_Hidden);
    Check(!Prog_IndexGone(&index, 2) && Prog_IndexGone(&index, 3) && Prog_IndexGone(&index, 4) && !Prog_IndexGone(&index, 5));
    Check(Prog__IndexFind(&index, S("C:/Work/Old")) == UINT32_MAX && Prog__IndexFind(&index, S("C:/Work/New")) == 5);
    // Listed again as it is, nothing changes.
    Prog_IndexApply(&index, 1, listed, CArrayCount(listed));
    Check(index.count == 6);
    Prog_IndexRelease(&index);
}

// The user's own groups: made, named, their folders added, moved and taken out, folded and deleted, the groups' order keeping up, and read
// back from Groups.conf.
function void TestUserGroups(TestFileFixture *fixture)
{
    Arena arena = {0};
    Prog_State state = {0};
    Prog_UserGroupNew(&state, S("Work"));
    Prog_UserGroupNew(&state, S("Games"));
    Check(state.userGroupCount == 2 && Prog_SidebarGroupCount(&state) == Prog_Group_Count + 2 && StrIsEqual(state.userGroups[1].name, S("Games")));
    ArrayStr folders = {0};
    ArrayPush(&arena, folders, S("/a"));
    ArrayPush(&arena, folders, S("/b"));
    Prog_UserGroupAdd(&state, 0, &folders, UINT64_MAX);
    ArrayStr more = {0};
    ArrayPush(&arena, more, S("/c"));
    ArrayPush(&arena, more, S("/a"));
    Prog_UserGroupAdd(&state, 0, &more, 0);
    const ArrayStr *work = &state.userGroups[0].folders;
    Check(work->header.count == 3 && StrIsEqual(work->v[0], S("/c")) && StrIsEqual(work->v[1], S("/a")) && StrIsEqual(work->v[2], S("/b")));
    Prog_UserGroupMove(&state, 0, 0, 3);
    Check(StrIsEqual(state.userGroups[0].folders.v[2], S("/c")) && StrIsEqual(state.userGroups[0].folders.v[0], S("/a")));
    Prog_UserGroupRemove(&state, 0, 1);
    Check(state.userGroups[0].folders.header.count == 2 && StrIsEqual(state.userGroups[0].folders.v[1], S("/c")));
    Prog_UserGroupRename(&state, 1, S("Play"));
    Prog_UserGroupFold(&state, 1);
    Check(StrIsEqual(state.userGroups[1].name, S("Play")) && state.userGroups[1].closed && !Prog_GroupOpen(&state, (Prog_Group)(Prog_Group_Count + 1)) && Prog_GroupOpen(&state, (Prog_Group)Prog_Group_Count));
    Check(StrIsEqual(Prog__SidebarPath(&state, (Prog_Group)Prog_Group_Count, 1), S("/c")));
    // A group moved up keeps its place when another is deleted, and the numbers after the deleted one move down.
    Prog_MoveGroup(&state, Prog_Group_Count + 1, Prog_Group_Count);
    Check(state.groupsOrdered && Prog_SidebarGroupAt(&state, Prog_Group_Count) == Prog_Group_Count + 1);
    // Go To's fold of the group after it moves down with it.
    state.gotoGroupsFolded = 2;
    Prog_UserGroupDelete(&state, 0);
    Check(state.userGroupCount == 1 && StrIsEqual(state.userGroups[0].name, S("Play")) && Prog_SidebarGroupAt(&state, Prog_Group_Count) == Prog_Group_Count);
    Check(state.gotoGroupsFolded == 1);
    state.gotoGroupsFolded = 0;
    // Read back as written, a line each: folded, its name and its folders, a tab apart; a line without a name is left out.
    String file = Prog_JoinPath(&arena, fixture->path, S("Groups.conf"));
    Check(!P_WriteFile(&arena, file, S("0\tWork\t/a\t/b\n1\tEmpty\nLonely\n")).code);
    state.configDirectory = fixture->path;
    Prog__LoadUserGroups(&state);
    Check(state.userGroupCount == 2 && StrIsEqual(state.userGroups[0].name, S("Work")) && state.userGroups[0].folders.header.count == 2 && state.userGroups[1].closed);
    // The order kept from before has each group once, the one more read in last.
    Check(Prog_SidebarGroupAt(&state, Prog_Group_Count) == Prog_Group_Count && Prog_SidebarGroupAt(&state, Prog_Group_Count + 1) == Prog_Group_Count + 1);
    Check(!state.userGroups[1].folders.header.count && StrIsEqual(state.userGroups[1].name, S("Empty")));
    // Go To lists a group's folders as a section of its own, which folds as the others do, and searches them.
    u32 offered = 0;
    String folder = {0};
    Prog_GoToItem *found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered == 2 && found[0].section == Prog_GoToSection_Count && StrIsEqual(found[1].path, S("/b")));
    state.gotoGroupsFolded = 1;
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered == 0 && Prog_GoToFolded(&state, Prog_GoToSection_Count));
    Prog__EditReplace(&state.palette, (const u8 *)"b", 1);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 1 && StrIsEqual(found[0].path, S("/b")));
    P__TestRemovePath(fixture, S("Groups.conf"), false);
    ArenaRelease(&state.userGroupsArena);
    ArenaRelease(&arena);
}

// Names edited in their sidebar rows: a new group made at once as "New group", numbered past one taken, with its name selected to be typed
// over, which Escape keeps; a folder of a group renamed on disk, the group following it; and a place, which is not renamed from there.
function void TestSidebarNames(TestFileFixture *fixture)
{
    Arena arena = {0};
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL) && state.jobs.initialized);
    Check(TestWaitDirectory(&state));
    TestBrowsingCommand(&state, Prog_CommandKind_GroupNew, 0);
    Check(state.userGroupCount == 1 && StrIsEqual(state.userGroups[0].name, S("New group")) && state.layoutSaving && state.nameFor == 1 && state.nameGroup == 0);
    Check(StrIsEqual(Prog_EditText(&state.layoutName), S("New group")) && state.layoutName.anchor == 0 && state.layoutName.cursor == state.layoutName.length);
    TestBrowsingCommand(&state, Prog_CommandKind_LayoutSaveButton, 2);
    Check(!state.layoutSaving && !state.nameFor && StrIsEqual(state.userGroups[0].name, S("New group")));
    TestBrowsingCommand(&state, Prog_CommandKind_GroupNew, 0);
    Check(state.userGroupCount == 2 && StrIsEqual(state.userGroups[1].name, S("New group (2)")) && state.nameGroup == 1);
    Prog__EditReplace(&state.layoutName, (const u8 *)"Work", 4);
    TestBrowsingCommand(&state, Prog_CommandKind_LayoutSaveButton, 0);
    Check(!state.layoutSaving && StrIsEqual(state.userGroups[1].name, S("Work")));
    // Its title's Rename edits it again, and an empty name leaves it as it was.
    TestBrowsingCommand(&state, Prog_CommandKind_GroupRename, Prog_Group_Count + 1);
    Check(state.layoutSaving && state.nameFor == 1 && StrIsEqual(Prog_EditText(&state.layoutName), S("Work")));
    Prog__EditReplace(&state.layoutName, (const u8 *)"", 0);
    TestBrowsingCommand(&state, Prog_CommandKind_LayoutSaveButton, 0);
    Check(StrIsEqual(state.userGroups[1].name, S("Work")));
    String made = {0};
    Check(!P_CreateItem(&arena, fixture->path, S("Renamable"), true, &made).code && StrIsEqual(made, S("Renamable")));
    ArrayStr folders = {0};
    ArrayPush(&arena, folders, Prog_JoinPath(&arena, fixture->path, made));
    Prog_UserGroupAdd(&state, 1, &folders, UINT64_MAX);
    TestBrowsingCommand(&state, Prog_CommandKind_SidebarRename, Prog_Group_Count + 1);
    Check(state.layoutSaving && state.nameFor == 2 && StrIsEqual(Prog_EditText(&state.layoutName), S("Renamable")) && state.layoutName.cursor == 9 && !state.layoutName.anchor);
    Prog__EditReplace(&state.layoutName, (const u8 *)"Renamed", 7);
    TestBrowsingCommand(&state, Prog_CommandKind_LayoutSaveButton, 0);
    String renamed = Prog_JoinPath(&arena, fixture->path, S("Renamed"));
    b32 followed = false;
    for (u64 start = PlatformNowMicroseconds(); !followed && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) {
        Prog_TakeCompletions(&state);
        followed = state.userGroups[1].folders.header.count == 1 && StrIsEqual(state.userGroups[1].folders.v[0], renamed);
    }
    Check(followed && P_IsDirectory(renamed) && !P_PathExists(Prog_JoinPath(&arena, fixture->path, S("Renamable"))));
    Check(P__TestRemovePath(fixture, S("Renamed"), true));
    // A place is not renamed from the sidebar.
    if (state.places.header.count) {
        TestBrowsingCommand(&state, Prog_CommandKind_SidebarRename, Prog_Group_Places);
        Check(!state.layoutSaving);
    }
    Prog_Release(&state);
    ArenaRelease(&arena);
}

#if defined(_WIN32)
// A drive read from its file table becomes the walk's tree, breadth first: the root's folders and files in the table's order, then each
// folder's; a link is not gone into, a record whose folder is not in the table is left out, and the room cuts it short.
function void TestVolumeTree(void)
{
    Arena arena = {0};
    u64 root = UINT64_C(0x0005000000000005);
    P__VolumeRecord records[] = {
        { 100, root, FILE_ATTRIBUTE_DIRECTORY, SInit("Alpha") },
        { 101, root, FILE_ATTRIBUTE_HIDDEN, SInit("b.txt") },
        { 102, 100, 0, SInit("c.txt") },
        { 103, root, FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT, SInit("Link") },
        { 104, 103, 0, SInit("d.txt") },
        { 105, 999, 0, SInit("lost.txt") },
        { 106, 100, FILE_ATTRIBUTE_DIRECTORY, SInit("Inner") },
        { 107, 106, 0, SInit("e.txt") },
    };
    P_VolumeTree tree = {0};
    Check(P__VolumeTreeFrom(&arena, records, CArrayCount(records), root, 100, &tree) && tree.count == 6);
    const char *names[] = { "Alpha", "b.txt", "Link", "c.txt", "Inner", "e.txt" };
    u32 parents[] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, 0, 0, 4 };
    for (u32 i = 0; i < 6 && i < tree.count; ++i) Check(StrIsEqual(tree.entries[i].name, StrFromCStr(names[i])) && tree.entries[i].parent == parents[i]);
    Check(tree.entries[0].folder && tree.entries[1].hidden && !tree.entries[1].folder && tree.entries[2].link && tree.entries[4].folder);
    Check(P__VolumeTreeFrom(&arena, records, CArrayCount(records), root, 4, &tree) && tree.count == 4 && StrIsEqual(tree.entries[3].name, S("c.txt")));
    ArenaRelease(&arena);
}

// A drag out of another program that only describes its files, as a browser's picture or an archiver's files are: a picture, a folder with a
// file in it, given as a stream, and a name that would climb out of where they are written.
global const wchar *const TestDescribedNames[] = { L"picture.png", L"folder", L"folder\\inner.txt", L"..\\escape.txt" };
global const char *const TestDescribedContents[] = { "PNGDATA", "", "inner", "out" };

function HRESULT STDMETHODCALLTYPE TestDescribed__QueryInterface(IDataObject *self, REFIID iid, void **object) { *object = self; return S_OK; }
function ULONG STDMETHODCALLTYPE TestDescribed__AddRef(IDataObject *self) { return 1; }
function ULONG STDMETHODCALLTYPE TestDescribed__Release(IDataObject *self) { return 1; }
function HRESULT STDMETHODCALLTYPE TestDescribed__GetData(IDataObject *self, FORMATETC *format, STGMEDIUM *medium)
{
    *medium = (STGMEDIUM){0};
    u32 count = CArrayCount(TestDescribedNames);
    if (format->cfFormat == RegisterClipboardFormatW(L"FileGroupDescriptorW")) {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(FILEGROUPDESCRIPTORW) + (count - 1) * sizeof(FILEDESCRIPTORW));
        FILEGROUPDESCRIPTORW *group = GlobalLock(memory);
        group->cItems = count;
        for (u32 i = 0; i < count; ++i) {
            lstrcpynW(group->fgd[i].cFileName, TestDescribedNames[i], MAX_PATH);
            group->fgd[i].dwFlags = FD_ATTRIBUTES;
            group->fgd[i].dwFileAttributes = i == 1 ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        }
        GlobalUnlock(memory);
        *medium = (STGMEDIUM){ .tymed = TYMED_HGLOBAL, .hGlobal = memory };
        return S_OK;
    }
    if (format->cfFormat == RegisterClipboardFormatW(L"FileContents") && format->lindex >= 0 && (u32)format->lindex < count) {
        const char *text = TestDescribedContents[format->lindex];
        u64 length = StrFromCStr(text).length;
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, Max(length, (u64)1));
        MemCopy(GlobalLock(memory), text, length);
        GlobalUnlock(memory);
        if (format->lindex != 2) {
            *medium = (STGMEDIUM){ .tymed = TYMED_HGLOBAL, .hGlobal = memory };
            return S_OK;
        }
        IStream *stream = NULL;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) return E_FAIL;
        ULARGE_INTEGER size = { .QuadPart = length };
        stream->lpVtbl->SetSize(stream, size);
        *medium = (STGMEDIUM){ .tymed = TYMED_ISTREAM, .pstm = stream };
        return S_OK;
    }
    return DV_E_FORMATETC;
}
function HRESULT STDMETHODCALLTYPE TestDescribed__GetDataHere(IDataObject *self, FORMATETC *format, STGMEDIUM *medium) { return E_NOTIMPL; }
function HRESULT STDMETHODCALLTYPE TestDescribed__QueryGetData(IDataObject *self, FORMATETC *format)
{
    return format->cfFormat == RegisterClipboardFormatW(L"FileGroupDescriptorW") || format->cfFormat == RegisterClipboardFormatW(L"FileContents") ? S_OK : DV_E_FORMATETC;
}
function HRESULT STDMETHODCALLTYPE TestDescribed__GetCanonicalFormatEtc(IDataObject *self, FORMATETC *in, FORMATETC *out) { return E_NOTIMPL; }
function HRESULT STDMETHODCALLTYPE TestDescribed__SetData(IDataObject *self, FORMATETC *format, STGMEDIUM *medium, BOOL release) { return E_NOTIMPL; }
function HRESULT STDMETHODCALLTYPE TestDescribed__EnumFormatEtc(IDataObject *self, DWORD direction, IEnumFORMATETC **formats) { return E_NOTIMPL; }
function HRESULT STDMETHODCALLTYPE TestDescribed__DAdvise(IDataObject *self, FORMATETC *format, DWORD flags, IAdviseSink *sink, DWORD *connection) { return E_NOTIMPL; }
function HRESULT STDMETHODCALLTYPE TestDescribed__DUnadvise(IDataObject *self, DWORD connection) { return E_NOTIMPL; }
function HRESULT STDMETHODCALLTYPE TestDescribed__EnumDAdvise(IDataObject *self, IEnumSTATDATA **advises) { return E_NOTIMPL; }

// Described files dropped into a folder inside an archive are written out, and their top level kept to go into it.
function void TestDescribedDrop(TestFileFixture *fixture)
{
    Arena arena = {0};
    IDataObjectVtbl table = {
        TestDescribed__QueryInterface, TestDescribed__AddRef, TestDescribed__Release, TestDescribed__GetData, TestDescribed__GetDataHere, TestDescribed__QueryGetData,
        TestDescribed__GetCanonicalFormatEtc, TestDescribed__SetData, TestDescribed__EnumFormatEtc, TestDescribed__DAdvise, TestDescribed__DUnadvise, TestDescribed__EnumDAdvise,
    };
    IDataObject data = { .lpVtbl = &table };
    String inside = Prog_JoinPath(&arena, fixture->path, S("archive.zip/inner"));
    ArenaClear(&P__drop.boundArena);
    P__drop.bound = StrCopy(&P__drop.boundArena, inside);
    Check(P__DropHasDescribed(&data) && !P__DropHasFiles(&data));
    P__DropKeepFiles(&data);
    String directory = {0}, contents = {0};
    b32 move = true;
    ArrayStr files = P_TakeDroppedFiles(&arena, &directory, &move);
    Check(StrIsEqual(directory, inside) && !move && files.header.count == 2 && TestEndsWith(files.v[0], S("/picture.png")) && TestEndsWith(files.v[1], S("/folder")));
    if (files.header.count == 2) {
        Check(P_ReadFile(&arena, files.v[0], &contents) && StrIsEqual(contents, S("PNGDATA")));
        Check(P_ReadFile(&arena, Prog_JoinPath(&arena, files.v[1], S("inner.txt")), &contents) && StrIsEqual(contents, S("inner")));
        String written = Prog_ParentPath(files.v[0]);
        Check(!P_PathExists(Prog_JoinPath(&arena, Prog_ParentPath(written), S("escape.txt"))));
    }
    ArenaClear(&P__drop.boundArena);
    P__drop.bound = (String){0};
    ArenaRelease(&arena);
}
#endif

// Places hidden and moved keep to it, by path, and on Linux the folders GTK's bookmarks name are places too.
function void TestPlaces(TestFileFixture *fixture)
{
    Arena arena = {0};
    Prog_State state = {0};
    String paths[] = { S("This PC"), S("/a"), S("/b"), S("/c") };
    for (u32 i = 0; i < CArrayCount(paths); ++i) ArrayPush(&state.placesArena, state.places, ((P_Place){ .name = paths[i], .path = paths[i] }));
    Prog_MovePlace(&state, 3, 1);
    Check(StrIsEqual(state.places.v[1].path, S("/c")) && StrIsEqual(state.places.v[3].path, S("/b")) && state.placesOrder.header.count == 4);
    Prog_MovePlace(&state, 1, 4);
    Check(StrIsEqual(state.places.v[3].path, S("/c")) && StrIsEqual(state.places.v[1].path, S("/a")));
    // A new listing comes in the default order, which the kept one sorts, a folder it does not name after the others.
    state.places = (P_ArrayPlace){0};
    String listed[] = { S("This PC"), S("/new"), S("/a"), S("/b"), S("/c") };
    for (u32 i = 0; i < CArrayCount(listed); ++i) ArrayPush(&state.placesArena, state.places, ((P_Place){ .name = listed[i], .path = listed[i] }));
    Prog__OrderPlaces(&state);
    Check(StrIsEqual(state.places.v[3].path, S("/c")) && StrIsEqual(state.places.v[4].path, S("/new")));
    Prog_TogglePlaceHidden(&state, 1);
    Check(Prog_PlaceHidden(&state, state.places.v[1].path) && state.placesHidden.header.count == 1);
    Prog_TogglePlaceHidden(&state, 1);
    Check(!state.placesHidden.header.count);
    // A group moves before another, and back in their own order the groups are not kept.
    Prog_MoveGroup(&state, Prog_Group_Places, 2);
    Check(state.groupsOrdered && Prog_SidebarGroupAt(&state, 2) == Prog_Group_Places && Prog_SidebarGroupAt(&state, 3) == Prog_Group_Storage);
    Prog_MoveGroup(&state, Prog_Group_Places, 4);
    Check(!state.groupsOrdered && Prog_SidebarGroupAt(&state, 3) == Prog_Group_Places);
    Prog_MoveGroup(&state, Prog_Group_Recents, 0);
    Check(!state.groupsOrdered);
    ArenaRelease(&state.placesArena);
    ArenaRelease(&state.placesOrderArena);
    ArenaRelease(&state.placesHiddenArena);
    // A program or a script takes the files dropped on it on Windows, whatever its name's case; a folder named so, or a document, does not.
    Prog_Entry program = { .file = { .name = S("Setup.EXE"), .kind = P_FileKind_File } }, notes = { .file = { .name = S("notes.txt"), .kind = P_FileKind_File } };
    Prog_Entry named = { .file = { .name = S("tools.cmd"), .kind = P_FileKind_Directory } };
#if defined(_WIN32)
    Check(Prog__RunsDropped(&program) && !Prog__RunsDropped(&notes) && !Prog__RunsDropped(&named));
#else
    Check(!Prog__RunsDropped(&program) && !Prog__RunsDropped(&notes) && !Prog__RunsDropped(&named));
#endif
#if defined(__linux__)
    {
        String config = Prog_JoinPath(&arena, fixture->path, S("gtk-config")), created = {0};
        Check(!P_CreateItem(&arena, fixture->path, S("gtk-config"), true, &created).code && !P_CreateItem(&arena, config, S("gtk-3.0"), true, &created).code);
        String folder = Prog_JoinPath(&arena, fixture->path, S("Folder2"));
        Check(!P_WriteFile(&arena, Prog_JoinPath(&arena, config, S("gtk-3.0/bookmarks")), StrFormat(&arena, "file://%S Second\nfile:///nowhere/gone\nsftp://host/x\n", folder)).code);
        const char *previous = getenv("XDG_CONFIG_HOME");
        String saved = previous ? StrCopy(&arena, StrFromCStr(previous)) : (String){0};
        setenv("XDG_CONFIG_HOME", P__FilePathCString(&arena, config), 1);
        P_ArrayPlace places = P_UserPlaces(&arena);
        b32 found = false;
        for (u64 i = 0; i < places.header.count; ++i) found |= StrIsEqual(places.v[i].path, folder) && StrIsEqual(places.v[i].name, S("Second"));
        Check(found);
        if (saved.length) setenv("XDG_CONFIG_HOME", P__FilePathCString(&arena, saved), 1);
        else unsetenv("XDG_CONFIG_HOME");
        TestRemoveTree(&arena, fixture, S("gtk-config"));
    }
#endif
    (void)fixture;
    ArenaRelease(&arena);
}

// Flags a run passes before its folders say how they open: --replace in the shown tab, which stays shown, the next one in a tab behind it;
// --split in a new panel beside, a file there selected in its folder, with --inspect showing the inspector.
function void TestOpenFlags(TestFileFixture *fixture)
{
    Prog_State state = {0};
    Check(Prog_Init(&state, fixture->path, NULL, NULL));
    if (state.jobs.initialized) {
        Check(TestWaitDirectory(&state));
        Prog_OpenPaths(&state, StrFormat(&state.frameArena, "--replace\n%S/Empty\n%S/Folder2", fixture->path, fixture->path));
        Check(TestWaitDirectory(&state) && state.tabCount == 2 && state.activeTab == 0 && TestEndsWith(Prog_TabPath(&state, 0), S("/Empty")) && TestEndsWith(Prog_TabPath(&state, 1), S("/Folder2")));
        u32 panels = state.panelCount;
        Prog_OpenPaths(&state, StrFormat(&state.frameArena, "--split\n--inspect\n%S/plain.txt", fixture->path));
        Check(TestWaitDirectory(&state) && state.panelCount == panels + 1 && StrIsEqual(state.directory.path, fixture->path) && state.previewShown);
        Check(state.hasFocus && state.focusedRow < state.directory.count && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("plain.txt")));
    }
    Prog_Release(&state);
}

// In a tree, a new item goes into the unfolded folder in focus, and a change inside one shows by itself.
function void TestTreeFileCommands(TestFileFixture *fixture)
{
    Arena arena = {0};
    String created = {0};
    Check(!P_CreateItem(&arena, fixture->path, S("TreeOps"), true, &created).code);
    String root = Prog_JoinPath(&arena, fixture->path, S("TreeOps"));
    Check(!P_CreateItem(&arena, root, S("Sub"), true, &created).code);
    Prog_State state = {0};
    Check(Prog_Init(&state, root, NULL, NULL));
    if (!state.jobs.initialized) goto finish;
    Check(TestWaitDirectory(&state));
    TestBrowsingCommand(&state, Prog_CommandKind_ToggleTreeView, 0);
    Check(TestWaitDirectory(&state));
    Prog_TreeToggle(&state, S("Sub"));
    Check(TestWaitDirectory(&state) && state.treeWatches.count == 1);
    Prog_SelectRow(&state, TestEntryIndex(&state, S("Sub")), 0);
    TestFileCommand(&state, Prog_CommandKind_CreateFolder);
    for (u64 start = PlatformNowMicroseconds(); TestEntryIndex(&state, S("Sub/New folder")) == UINT64_MAX && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) {
        Prog_TakeCompletions(&state);
    }
    Check(TestHasFile(&arena, fixture, S("TreeOps/Sub"), S("New folder")) && !TestHasFile(&arena, fixture, S("TreeOps"), S("New folder")));
    Check(state.renaming && state.focusedRow == TestEntryIndex(&state, S("Sub/New folder")));
    TestRenameKey(&state, Key_Escape, 0);
    Check(!P_CreateItem(&arena, Prog_JoinPath(&arena, root, S("Sub")), S("outside.txt"), false, &created).code);
    for (u64 start = PlatformNowMicroseconds(); TestEntryIndex(&state, S("Sub/outside.txt")) == UINT64_MAX && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) {
        Prog_TakeCompletions(&state);
    }
    Check(TestEntryIndex(&state, S("Sub/outside.txt")) != UINT64_MAX);
finish:
    Prog_Release(&state);
    Check(!state.treeWatches.count);
    TestRemoveTree(&arena, fixture, S("TreeOps"));
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
    // It starts in the default view, whatever the shown tab's zoom and sort.
    {
        u32 zoom = state.zoom;
        Prog_Sort sort = state.sort;
        state.zoom = Prog_ZoomIcons, state.sort = Prog_Sort_Size, state.descending = true;
        TestBrowsingCommand(&state, Prog_CommandKind_TabNew, 0);
        Check(TestWaitDirectory(&state) && state.zoom == 0 && state.sort == Prog_Sort_Name && !state.descending);
        TestBrowsingCommand(&state, Prog_CommandKind_TabClose, 0);
        state.zoom = zoom, state.sort = (u8)sort, state.descending = false;
    }
    state.newTab = Prog_NewTab_Current;
    TestBrowsingCommand(&state, Prog_CommandKind_TabNew, 0);
    state.newTab = Prog_NewTab_Root;
    Check(state.tabCount == 2 && state.activeTab == 1 && StrIsEqual(state.directory.path, fixture->path));
    // Each tab keeps its own zoom.
    {
        Check(TestWaitDirectory(&state));
        u32 shared = state.zoom;
        TestBrowsingCommand(&state, Prog_CommandKind_ZoomIn, 0);
        u32 zoomed = state.zoom;
        Prog_SelectTab(&state, 0);
        Check(zoomed != shared && state.zoom == shared);
        Prog_SelectTab(&state, 1);
        Check(state.zoom == zoomed);
        TestBrowsingCommand(&state, Prog_CommandKind_ZoomSet, shared);
    }
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
    // Escape closes the palette at once, with its text, rather than clearing it first.
    TestRenameKey(&state, Key_Escape, 0);
    Check(!state.paletteOpen);
    // Go To lists the folder typed, offers its subfolders starting with what follows it, and opens the one picked.
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Check(state.paletteOpen && state.paletteMode == Prog_PaletteMode_GoTo);
    // An environment variable typed whole is put in at once, so that its folder lists.
    {
#if defined(_WIN32)
        String variable = S("%SYSTEMROOT%");
#else
        String variable = S("${HOME}");
#endif
        String expected = P_ExpandVariables(&state.frameArena, variable, true);
        Prog__EditReplace(&state.palette, (const u8 *)variable.value, (u32)variable.length);
        Prog__GoToExpandVariables(&state);
        Check(StrIsEqual(Prog_EditText(&state.palette), expected) && expected.length && !StrIsEqual(expected, variable));
        state.palette.anchor = 0;
        state.palette.cursor = state.palette.length;
        Prog__EditReplace(&state.palette, NULL, 0);
    }
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
    // A folder's path pasted whole, ending in a separator, highlights nothing, so that Enter goes to that folder rather than into the first in it.
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    typed = P_NativePath(&state.frameArena, Prog_JoinPath(&state.frameArena, fixture->path, S("Folder2/")));
    Prog__EditReplace(&state.palette, (const u8 *)typed.value, (u32)typed.length);
    Prog__KeepHighlight(&state, Prog_Option_Count);
    Check(state.paletteHighlight == UINT32_MAX);
    TestBrowsingCommand(&state, Prog_CommandKind_PaletteRun, UINT64_MAX);
    Check(TestWaitDirectory(&state) && TestEndsWith(state.directory.path, S("/Folder2")));
    Prog_RequestDirectory(&state, fixture->path, true, 0);
    Check(TestWaitDirectory(&state));
    // A tree unfolds a folder in place, its items right after it, named by their path from the folder shown; folded, they go again, and
    // another folder starts folded.
    {
        TestBrowsingCommand(&state, Prog_CommandKind_ToggleTreeView, 0);
        Check(TestWaitDirectory(&state) && state.tree && state.directory.tree);
        u64 before = state.directory.count;
        Prog_TreeToggle(&state, S("Folder2"));
        Check(TestWaitDirectory(&state) && Prog_TreeIsOpen(&state, S("Folder2")) && state.directory.count > before);
        u64 at = TestEntryIndex(&state, S("Folder2"));
        Check(at + 1 < state.directory.count && StrIsEqual(Prog_EntryParent(state.directory.entries[at + 1].file.name), S("Folder2")));
        Prog_TreeToggle(&state, S("Folder2"));
        Check(TestWaitDirectory(&state) && !Prog_TreeIsOpen(&state, S("Folder2")) && state.directory.count == before);
        Prog_TreeToggle(&state, S("Folder2"));
        Check(TestWaitDirectory(&state) && state.treeOpen.header.count == 1);
        Prog_RequestDirectory(&state, Prog_JoinPath(&state.frameArena, fixture->path, S("Folder2")), true, 0);
        Check(TestWaitDirectory(&state) && !state.treeOpen.header.count);
        TestBrowsingCommand(&state, Prog_CommandKind_ToggleTreeView, 0);
        Prog_RequestDirectory(&state, fixture->path, true, 0);
        Check(TestWaitDirectory(&state) && !state.tree && !state.directory.tree);
    }
    // Grouped by type, the view runs group after group, each holding the items of its type; a folded group keeps its heading and count and
    // leaves its items out until it is shown again.
    {
        u64 all = state.viewCount;
        TestBrowsingCommand(&state, Prog_CommandKind_GroupByType, 0);
        Check(state.groupBy == Prog_GroupBy_Type && state.groupCount >= 2 && state.viewCount == all);
        u64 next = 0;
        b32 runs = true;
        for (u32 g = 0; g < state.groupCount; ++g) {
            const Prog_ListGroup *group = &state.groups[g];
            runs &= group->start == next && group->count == group->total && group->count;
            for (u64 p = group->start; p < group->start + group->count; ++p) runs &= StrIsEqual(state.directory.entries[state.view[p]].type, group->label);
            next += group->count;
        }
        Check(runs && next == all && state.directory.entries[state.view[0]].file.kind == P_FileKind_Directory);
        u64 folded = state.groups[0].total;
        TestBrowsingCommand(&state, Prog_CommandKind_GroupToggle, 0);
        Check(!state.groups[0].count && state.groups[0].total == folded && state.viewCount == all - folded && state.groups[1].start == 0);
        Check(Prog_GroupAt(&state, 0) == 1);
        TestBrowsingCommand(&state, Prog_CommandKind_GroupToggle, 0);
        Check(state.viewCount == all && state.groups[0].count == folded);
        // By size the folders stand apart, first as they are in the list, and the small files are tiny.
        TestBrowsingCommand(&state, Prog_CommandKind_GroupBySize, 0);
        u64 plain = TestEntryIndex(&state, S("plain.txt"));
        Check(state.groupCount >= 2 && StrIsEqual(state.groups[0].label, S("Folders")) && state.groups[0].count == state.viewFolders);
        Check(StrIsEqual(state.groups[Prog_GroupAt(&state, Prog_ViewPosition(&state, plain))].label, S("Tiny (under 16 kB)")));
        // By date, what was made just now is today's, which comes first.
        TestBrowsingCommand(&state, Prog_CommandKind_GroupByDateModified, 0);
        Check(Prog_GroupAt(&state, Prog_ViewPosition(&state, plain)) == 0 && StrIsEqual(state.groups[0].label, S("Today")));
        // In a grid two across, a line down from a group's last line goes to the next group's first, and up from there to the last item above
        // when that line is short.
        TestBrowsingCommand(&state, Prog_CommandKind_GroupBySize, 0);
        Check(state.groups[0].count == 3 && state.groups[1].count >= 2);
        Check(Prog__GroupedLineStep(&state, 1, 2, true) == 2 && Prog__GroupedLineStep(&state, 2, 2, true) == 3 && Prog__GroupedLineStep(&state, 4, 2, false) == 2);
        // The grouping and the sort shown are checked, and only they.
        Check(Prog_CommandChecked(&state, Prog_CommandKind_GroupBySize) && !Prog_CommandChecked(&state, Prog_CommandKind_GroupByType));
        Check(Prog_CommandChecked(&state, Prog_CommandKind_SortByName) == (state.sort == Prog_Sort_Name) && !Prog_CommandChecked(&state, Prog_CommandKind_Refresh));
        TestBrowsingCommand(&state, Prog_CommandKind_GroupByNone, 0);
        Check(!state.groupCount && state.viewCount == all && Prog_CommandChecked(&state, Prog_CommandKind_GroupByNone));
    }
    // The drive index walks everything below the drives, writing itself to the cache folder as it goes, and Go To finds folders and files by name.
    Arena driveArena = {0};
    ArrayPush(&driveArena, state.drives, ((P_Place){ .path = fixture->path }));
    String created = {0};
    Check(!P_CreateItem(&driveArena, fixture->path, S("Cache"), true, &created).code);
    String cache = Prog_JoinPath(&driveArena, fixture->path, created);
    state.configDirectory = state.cacheDirectory = cache;
    // It waits for Go To to search the drives.
    Prog_TakeCompletions(&state);
    Check(!state.indexStarted);
    state.gotoDrives = true;
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
    // The drive itself is not searched: a file the walk found is offered only once it is among the recents, files being kept there too.
    String fileName = {0}, filePath = {0};
    for (u64 i = 0; i < state.index.count && !fileName.length; ++i) {
        String name = Prog_IndexName(&state.index, i);
        if (!(state.index.flags[i] & Prog_IndexFlag_File) || (state.index.flags[i] & Prog_IndexFlag_Hidden) || (name.length && name.value[0] == '.')) continue;
        char made[1024];
        fileName = name;
        filePath = StrCopy(&driveArena, Prog__IndexPathTo(made, sizeof(made), &state.index, (u32)i));
    }
    Check(fileName.length);
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Prog__EditReplace(&state.palette, (const u8 *)fileName.value, (u32)fileName.length);
    found = Prog_GoToMatches(&state, &offered, &folder);
    b32 offeredFile = false;
    for (u32 i = 0; i < offered; ++i) offeredFile |= StrIsEqual(found[i].path, filePath);
    Check(!offeredFile);
    // Alt+E searches the drive index too, which finds it after the short list.
    state.gotoDrives = true;
    for (u32 tries = 0; tries < 200; ++tries) {
        ++state.ui.frameIndex;
        found = Prog_GoToMatches(&state, &offered, &folder);
        if (!state.gotoSearching) break;
    }
    for (u32 i = 0; i < offered; ++i) offeredFile |= StrIsEqual(found[i].path, filePath);
    Check(offeredFile && !state.gotoSearching);
    state.gotoDrives = false;
    Prog__AddRecent(&state, filePath, true, false);
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Prog__EditReplace(&state.palette, (const u8 *)fileName.value, (u32)fileName.length);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 1 && found[0].file && StrIsEqual(found[0].path, filePath));
    // A folder the system names, such as AppData's, is searched though the places do not show it.
    ArrayPush(&driveArena, state.searchPlaces, cache);
    TestBrowsingCommand(&state, Prog_CommandKind_GoTo, 0);
    Prog__EditReplace(&state.palette, (const u8 *)created.value, (u32)created.length);
    found = Prog_GoToMatches(&state, &offered, &folder);
    Check(offered >= 1 && StrIsEqual(found[0].path, cache));
    state.searchPlaces = (ArrayStr){0};
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
    // Just written whole, it serves as the index without another walk.
    Check(loaded && loaded->count == state.index.count && loaded->fresh);
    for (u64 i = 0; loaded && i < loaded->count; ++i) {
        Check(StrIsEqual(Prog__PackedName(loaded->packed[i]), Prog_IndexName(&state.index, i)) && loaded->parents[i] == state.index.parents[i] &&
              (u8)loaded->flags[i] == state.index.flags[i]);
    }
    // And the index it makes, whole and linked, which the app takes as its own.
    Prog_Index whole = loaded ? loaded->whole : (Prog_Index){0};
    Check(whole.count == state.index.count && whole.firstChild && Prog__IndexFind(&whole, Prog_IndexName(&whole, 0)) == 0);
    Prog_IndexRelease(&whole);
    ArenaRelease(&load.arena);
    // The options write their lines into Settings.conf, over the commented-out defaults, and apply at once; each keeps the others.
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
        Check(P_ReadFile(&driveArena, Prog_JoinPath(&driveArena, cache, S("Settings.conf")), &settings));
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
        // A key alone that types nothing, such as the Windows key, leaves the list where the wheel scrolled it, and typing brings it back.
        state.paletteWheeled = true;
        PlatformEvent alone[] = { { .kind = PlatformEventKind_KeyDown, .key = Key_Super }, { .kind = PlatformEventKind_KeyDown, .key = Key_None } };
        TestRenameInput(&state, alone, CArrayCount(alone));
        Check(state.paletteWheeled);
        PlatformEvent typed[] = { { .kind = PlatformEventKind_Text, .codepoint = 'x' } };
        TestRenameInput(&state, typed, CArrayCount(typed));
        Check(!state.paletteWheeled);
        TestBrowsingCommand(&state, Prog_CommandKind_PaletteClose, 0);
        Prog_ApplySettings(&state, S(""));
    }
    state.configDirectory = state.cacheDirectory = (String){0};
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
    // With hover.focus on, the panel the pointer moves over takes the keyboard; one Tab switched to keeps it while the pointer rests, until it moves again.
    Prog_ApplySettings(&state, S("hover.focus = on"));
    TestPanelsFrame(&state, true, 1100, 400, 1);
    Check(state.activePanel == 1);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectNext, 0);
    Check(state.activePanel == 0);
    TestPanelsFrame(&state, false, 0, 0, 2);
    TestPanelsFrame(&state, true, 1100, 400, 3);
    Check(state.activePanel == 0);
    TestPanelsFrame(&state, true, 1101, 400, 4);
    Check(state.activePanel == 1);
    // By default only clicks switch panels.
    Prog_ApplySettings(&state, S(""));
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectNext, 0);
    TestPanelsFrame(&state, true, 1102, 400, 5);
    Check(state.activePanel == 0 && state.clickToFocus);
    // Each panel's tabs have inspectors of their own: the first panel's stays open as the keyboard goes to the second, which opens its own.
    TestBrowsingCommand(&state, Prog_CommandKind_TogglePreview, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectNext, 0);
    Check(state.activePanel == 1 && !state.previewShown && state.panels[0].previewShown);
    TestBrowsingCommand(&state, Prog_CommandKind_TogglePreview, 0);
    Check(state.previewShown && state.panels[0].previewShown);
    // A press in the first panel's inspector gives it the keyboard.
    {
        state.panels[0].inspectorRect = (R_Rect){ 500, 100, 900, 700 };
        PlatformEvent press[] = { { .kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = 600, .y = 300 } };
        ArenaClear(&state.frameArena);
        Prog_HandleInput(&state, TestUIEvents(press, CArrayCount(press)));
        Prog_ProcessCommands(&state);
        state.panels[0].inspectorRect = (R_Rect){0};
        Check(state.activePanel == 0);
        TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectNext, 0);
        Check(state.activePanel == 1);
    }
    TestBrowsingCommand(&state, Prog_CommandKind_TogglePreview, 0);
    TestBrowsingCommand(&state, Prog_CommandKind_PanelSelectNext, 0);
    Check(state.activePanel == 0 && state.previewShown);
    // A new tab opens with the inspector its tab had, and closing it there leaves the other tab's.
    TestBrowsingCommand(&state, Prog_CommandKind_TabNew, 0);
    Check(state.activeTab == 1 && state.previewShown);
    TestBrowsingCommand(&state, Prog_CommandKind_TogglePreview, 0);
    Prog_SelectTab(&state, 0);
    Check(state.previewShown && !state.tabs[1].previewShown);
    Prog_CloseTab(&state, 1);
    TestBrowsingCommand(&state, Prog_CommandKind_TogglePreview, 0);
    Check(!state.previewShown && state.tabCount == 1);
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
            // The inspector is still its owner's tab's.
            const Prog_Panel *owning = &state.panels[Prog_PanelIndex(&state, owner)];
            Check(state.activePanel == inner && state.hasFocus && StrIsEqual(owning->previewPath, Prog_JoinPath(&state.frameArena, fixture->path, S("Folder2"))));
            String session = Prog_SessionText(&state.frameArena, &state);
            u32 panels = 0;
            for (u64 i = 0; i + 6 <= session.length; ++i) panels += StrIsEqual(StrFromTo(session, i, (i64)i + 6), S("panel ")) ? 1 : 0;
            Check(panels == state.panelCount - 1);
        }
        // Hidden from inside its list, it is its owner's that closes.
        TestBrowsingCommand(&state, Prog_CommandKind_TogglePreview, 0);
        Prog_ProcessCommands(&state);
        Check(Prog_InspectorPanel(&state, owner, false) == UINT32_MAX && state.panels[state.activePanel].id == owner && !state.previewShown);
    }
    // A name to select waits for its own panel's next listing, which an inspector's folder list listed first leaves alone.
    {
        state.previewShown = true;
        Prog_SelectRow(&state, TestEntryIndex(&state, S("plain.txt")), 0);
        Prog_ProcessCommands(&state);
        Prog_SelectRow(&state, TestEntryIndex(&state, S("Folder2")), 0);
        Prog_ProcessCommands(&state);
        u32 inner = Prog_InspectorPanel(&state, state.panels[state.activePanel].id, false);
        Check(Prog_SelectNext(&state, S("plain.txt")) && inner != UINT32_MAX);
        u64 start = PlatformNowMicroseconds();
        while (inner != UINT32_MAX && state.panels[inner].directoryJob && PlatformNowMicroseconds() - start < Millions(5)) {
            P__TestPause();
            Prog_TakeCompletions(&state);
        }
        Check(state.selectLength == 9);
        Prog_Reload(&state);
        start = PlatformNowMicroseconds();
        while (state.directoryJob && PlatformNowMicroseconds() - start < Millions(5)) {
            P__TestPause();
            Prog_TakeCompletions(&state);
        }
        Check(!state.selectLength && state.hasFocus && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("plain.txt")));
        TestBrowsingCommand(&state, Prog_CommandKind_TogglePreview, 0);
        Prog_ProcessCommands(&state);
        // An item shown in its folder scrolls into sight, though the tab would start scrolled where a session left it.
        state.pickScrollSet = true;
        state.revealFocus = false;
        Prog_ShowInFolder(&state, Prog_JoinPath(&state.frameArena, fixture->path, S("Folder2")), false);
        Check(TestWaitDirectory(&state) && state.revealFocus && StrIsEqual(state.directory.entries[state.focusedRow].file.name, S("Folder2")));
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
    // Folders a later run hands over open in new tabs after the shown one, the last of them shown; none only brings the window forward.
    {
        String handed = StrFormat(&state.frameArena, "%S\n%S/Empty\n", fixture->path, fixture->path);
        Prog_OpenPaths(&state, handed);
        Check(state.tabCount == 3 && state.activeTab == 2 && TestEndsWith(Prog_TabPath(&state, 2), S("/Empty")) && StrIsEqual(Prog_TabPath(&state, 1), fixture->path));
        Prog_OpenPaths(&state, S(""));
        Check(state.tabCount == 3);
        Prog_SelectTab(&state, 0);
        TestBrowsingCommand(&state, Prog_CommandKind_TabCloseOthers, 0);
        Check(state.tabCount == 1 && TestWaitDirectory(&state));
    }
    // Dragged tabs: within their strip they change place, into another panel they move there, and beside a panel they make a new one; Ctrl copies.
    u64 alone = state.panels[0].id;
    Prog_OpenTab(&state, fixture->path, 1, false);
    Check(state.tabCount == 2);
    Prog_DropTab(&state, (Prog_TabDropTarget){ .kind = Prog_TabDrop_Strip, .panel = alone, .slot = 2, .fromPanel = alone, .fromSlot = 0 });
    Check(state.tabCount == 2 && StrIsEqual(Prog_TabPath(&state, 0), fixture->path) && TestEndsWith(Prog_TabPath(&state, 1), S("/Empty")));
    // The moved tab keeps its zoom, from its own fields or the panel's while it is shown.
    if (state.activeTab == 1) state.zoom = Prog_ZoomIcons;
    else state.tabs[1].zoom = Prog_ZoomIcons;
    Prog_DropTab(&state, (Prog_TabDropTarget){ .kind = Prog_TabDrop_Right, .panel = alone, .fromPanel = alone, .fromSlot = 1 });
    Check(TestWaitDirectory(&state) && state.panelCount == 2 && state.activePanel == 1 && TestEndsWith(state.directory.path, S("/Empty")));
    Check(state.zoom == Prog_ZoomIcons);
    state.zoom = 0;
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
        state.columnSet = Prog_PanelColumns(&state);
        state.columnSet.kinds[state.columnSet.count++] = Prog_ColumnKind_Created;
        state.folderSort = Prog_FolderSort_Last;
        state.inspectorView = (Prog_ViewSettings){ .zoom = Prog_ZoomColumns, .sort = Prog_Sort_Size, .descending = true, .columns = { .count = 2, .kinds = { Prog_ColumnKind_Name, Prog_ColumnKind_Size } } };
        // And its tabs' filters, which their first listings keep.
        MemCopy(state.filter.text, "al pha", 6);
        state.filter.length = 6;
        state.hideFiles = true;
        // And the sidebar's width, dragged past its widest, which it is held to.
        Prog_QueueCommand(&state, Prog_CommandKind_ResizeSidebar, 0, 0, 0)->target = 900000;
        // And its Miller columns.
        Prog_QueueCommand(&state, Prog_CommandKind_ToggleMillerColumns, 0, 0, 0);
        Prog_ProcessCommands(&state);
        Check(state.sidebarWidth == Prog_SidebarMax && state.miller);
        // And a tab's tree, with its unfolded folders.
        state.tree = true;
        state.groupBy = Prog_GroupBy_Size;
        String unfolded[] = { S("Folder2"), S("Folder2/a b") };
        Prog__SetList(&state.treeArena, &state.treeOpen, unfolded, CArrayCount(unfolded));
        // And the inspector, open in the shown tab and not in the other.
        state.previewShown = true;
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
        Check(restored.miller && restored.tree && restored.treeOpen.header.count == 2 && StrIsEqual(restored.treeOpen.v[1], S("Folder2/a b")));
        Check(restored.groupBy == Prog_GroupBy_Size);
        Check(restored.previewShown && !restored.tabs[restored.activeTab ? 0 : 1].previewShown);
        // A layout keeps the same, and puts a window that has other tabs back the way it was; a deleted one no longer applies.
        Prog_SetLayout(&restored, S("Two"), Prog_LayoutText(&sessionArena, &state), false);
        Prog_OpenTab(&restored, fixture->path, restored.tabCount, true);
        Check(restored.layoutNames.header.count == 1 && Prog_ApplyLayout(&restored, S("two")) && TestWaitDirectory(&restored) && restored.panelCount == state.panelCount);
        Check(StrIsEqual(Prog_SessionText(&sessionArena, &restored), text));
        Prog_SetLayout(&restored, S("TWO"), (String){0}, true);
        Check(!restored.layoutNames.header.count && !Prog_ApplyLayout(&restored, S("Two")));
        Prog_Release(&restored);
        ArenaRelease(&sessionArena);
        state.miller = false;
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
    Check(Prog_ThemeGlassUser.window.a == Prog_DefaultGlassOpacity && state.glassBackdrop == PlatformBackdrop_Mica);
    Check(Prog_ThemeUser.accent.r == 1 && Prog_ThemeUser.accent.b == 0 && Prog_ThemeUser.text.r == Prog_ThemeCustom.text.r);
    // An option that narrows the one above it shows only while that one is on.
    {
        Prog_State options = {0};
        u8 found[Prog_Option_Count];
        u32 shown = Prog_OptionMatches(&options, found);
        options.noFolderSizes = true;
        Check(Prog_OptionMatches(&options, found) == shown - 3);
        options.singleClick = true;
        Check(Prog_OptionMatches(&options, found) == shown - 2);
        String muted[] = { S("undo.nothing") };
        Prog__SetList(&options.mutedArena, &options.mutedNotices, muted, 1);
        Check(Prog_OptionMatches(&options, found) == shown - 1);
        ArenaRelease(&options.mutedArena);
    }
    // How the lists look, read back as the options show them; the defaults are {0}'s.
    {
        Prog_State look = {0};
        Check(Prog_OptionValue(&look, Prog_Option_Spacing) == 4 && Prog_OptionValue(&look, Prog_Option_InactiveOpacity) == 60 && Prog_OptionValue(&look, Prog_Option_RoundCorners));
        // The schemes are checked as chosen, whatever the system's mode, which a theme.system off leaves out.
        Prog_ApplySettings(&look, S("theme.system = off\nspacing = 7\ncorners = square\nrows.alternate = on\ninactive.opacity = 85\ntheme = light\nspacing = 99\n"));
        Check(Prog_OptionValue(&look, Prog_Option_Spacing) == 7 && !Prog_OptionValue(&look, Prog_Option_RoundCorners) && look.alternatingRows);
        // "light" is the light scheme, Catppuccin Latte.
        Check(Prog_OptionValue(&look, Prog_Option_InactiveOpacity) == 85 && look.scheme == Prog_LightScheme && (u32)(Prog_ThemeUser.background.r * 255 + 0.5f) == 0xEF &&
              look.schemeGroups[Prog_Option_ColorText - Prog_Option_ColorClear] == 0x4C4F69);
        // The default theme is the one its colors make. The full moon scheme, which the choices leave out, shows the default among them, and its
        // popups and disabled icons come from its surface, background, icon and disabled colors.
        Prog_Theme made = Prog__ThemeFromScheme(Prog_SchemeColors[0]);
        Check(!MemCompare(&Prog_ThemeCustom, &made, sizeof(made)));
        Prog_ApplySettings(&look, S("theme.system = off\ntheme = FullMoon\n"));
        Check(look.scheme == 0 && Prog_ThemeUser.menu.r == Prog_ThemeUser.panel.r && (u32)(Prog_ThemeUser.iconDisabled.b * 255 + 0.5f) == 0x49 && (u32)(Prog_ThemeUser.textHeading.r * 255 + 0.5f) == 0xA2);
        // A custom scheme by its name, its colors over the default scheme's, among the choices after the built-in ones; a fork of it is named
        // as the first free custom one and shows in its place by name, and the picker changes its colors.
        Prog_SetScheme(&look, S("Mine"), S("Background = #102030\nnonsense = #ffffff\n"), false);
        Prog_ApplySettings(&look, S("theme.system = off\ntheme = mine\n"));
        Check(look.scheme == Prog_BuiltInSchemes && Prog_ThemeUser.background.b > 0.18f && Prog_ThemeUser.text.r == Prog_ThemeCustom.text.r);
        u32 schemeChoices = 0;
        Prog_OptionChoices(&look, Prog_Option_ColorScheme, &look.frameArena, &schemeChoices);
        // The built-in ones show their themes' own names, and a custom one its.
        Check(StrIsEqual(Prog__OptionName(&look.frameArena, Prog_Option_ColorScheme, S("filey-dark")), S("Filey Dark")) &&
              StrIsEqual(Prog__OptionName(&look.frameArena, Prog_Option_LightScheme, S("catppuccin-latte")), S("Catppuccin Latte")) &&
              StrIsEqual(Prog__OptionName(&look.frameArena, Prog_Option_ColorScheme, S("Mine")), S("Mine")));
        Check(schemeChoices == Prog_BuiltInSchemes + 1 && Prog_ForkScheme(&look, Prog_BuiltInSchemes, Prog_Option_ColorScheme) && look.schemeNames.header.count == 2);
        Check(StrIsEqual(look.schemeNames.v[0], S("Custom 1")) && look.scheme == Prog_BuiltInSchemes && Prog_ThemeUser.background.b > 0.18f);
        // A fork of a built-in scheme takes the next number.
        Check(Prog_ForkScheme(&look, 0, Prog_Option_ColorScheme) && StrIsEqual(look.schemeNames.v[1], S("Custom 2")) && StrIsEqual(look.schemeNames.v[2], S("Mine")));
        Check(look.schemeGroups[Prog_Option_ColorBackground - Prog_Option_ColorClear] == 0x191919);
        look.colorPicking = true;
        look.colorOption = Prog_Option_ColorBackground;
        Prog_PickColor(&look, 3, 1);
        Check(Prog_OptionColor(&look, Prog_Option_ColorBackground).r == 1 && Prog_ThemeUser.background.r == 1);
        Prog_PickColor(&look, 0, 0);
        Prog_PickColor(&look, 1, 1);
        Prog_PickColor(&look, 2, 1);
        Check(look.schemeGroups[Prog_Option_ColorBackground - Prog_Option_ColorClear] == 0xFF0000);
        // The number boxes: a hue typed in changes the color at once, a red past 255 stops there, and six hex digits set it all.
        look.pickerField = 1;
        look.pickerEdit = (Prog_TextEdit){0};
        Prog__EditReplace(&look.pickerEdit, (const u8 *)"120", 3);
        Prog_PickerTyped(&look);
        Check(look.schemeGroups[Prog_Option_ColorBackground - Prog_Option_ColorClear] == 0x00FF00 && StrIsEqual(Prog_PickerFieldText(&look, &look.frameArena, 1), S("120")));
        look.pickerField = 4;
        look.pickerEdit = (Prog_TextEdit){0};
        Prog__EditReplace(&look.pickerEdit, (const u8 *)"999", 3);
        Prog_PickerTyped(&look);
        Check(look.schemeGroups[Prog_Option_ColorBackground - Prog_Option_ColorClear] == 0xFFFF00);
        look.pickerField = 7;
        look.pickerEdit = (Prog_TextEdit){0};
        Prog__EditReplace(&look.pickerEdit, (const u8 *)"4A9C", 4);
        Prog_PickerTyped(&look);
        Check(look.schemeGroups[Prog_Option_ColorBackground - Prog_Option_ColorClear] == 0xFFFF00);
        Prog__EditReplace(&look.pickerEdit, (const u8 *)"C4", 2);
        Prog_PickerTyped(&look);
        Check(look.schemeGroups[Prog_Option_ColorBackground - Prog_Option_ColorClear] == 0x4A9CC4 && StrIsEqual(Prog_PickerFieldText(&look, &look.frameArena, 1), S("200")));
        Prog_PickHex(&look, 0xFF0000);
        // Closing the picker keeps the colors in the scheme, by the groups' names.
        Prog_CloseColorPicker(&look);
        Check(StrIndexStr(look.schemeTexts.v[1], S("Background = #FF0000\n"), SSF_None).exists);
        // A group's hex box sets the group, saved too, from six digits only; fewer leave it as it was.
        look.hexOption = Prog_Option_ColorText;
        look.hexEdit = (Prog_TextEdit){0};
        Prog__EditReplace(&look.hexEdit, (const u8 *)"12AB", 4);
        Prog_EndHexEdit(&look, true);
        Check(!look.hexOption && look.schemeGroups[Prog_Option_ColorText - Prog_Option_ColorClear] != 0x12AB);
        look.hexOption = Prog_Option_ColorText;
        look.hexEdit = (Prog_TextEdit){0};
        Prog__EditReplace(&look.hexEdit, (const u8 *)"12AB34", 6);
        Prog_EndHexEdit(&look, true);
        Check(look.schemeGroups[Prog_Option_ColorText - Prog_Option_ColorClear] == 0x12AB34 && StrIndexStr(look.schemeTexts.v[1], S("#12AB34"), SSF_None).exists);
        // The pointer's options and their defaults.
        Check(Prog_OptionValue(&look, Prog_Option_ActOnPress) && Prog_OptionValue(&look, Prog_Option_UnderlineOnHover) && Prog_OptionValue(&look, Prog_Option_Overscroll));
        Prog_ApplySettings(&look, S("click.onPress = off\nclick.single = on\nhover.underline = off\nscroll.over = off\nscroll.reverse = on\nclick.doubleToParent = on\n"));
        Check(look.actOnRelease && look.singleClick && look.underlineAlways && look.noOverscroll && look.reverseScroll && look.doubleClickToParent && !look.hoverToSelect);
        // Emptying the bin asks first unless turned off.
        Check(Prog_OptionValue(&look, Prog_Option_ConfirmEmptyTrash));
        Prog_ApplySettings(&look, S("trash.confirm = off\n"));
        Check(look.noTrashConfirm && !Prog_OptionValue(&look, Prog_Option_ConfirmEmptyTrash));
        // Borders are 1 pixel unless set in half pixels up to 3, and back to it for anything else, as the old "default".
        Check(Prog_OptionValue(&look, Prog_Option_Borders) == 0);
        Prog_ApplySettings(&look, S("borders = 2.5\n"));
        Check(look.borderSize == 3 && Prog_OptionValue(&look, Prog_Option_Borders) == 3);
        Prog_ApplySettings(&look, S("borders = default\n"));
        Check(look.borderSize == 0);
        // One window for every run, unless turned off.
        Check(Prog_OptionValue(&look, Prog_Option_SingleWindow));
        Prog_ApplySettings(&look, S("instances.single = off\n"));
        Check(look.separateInstances && !Prog_OptionValue(&look, Prog_Option_SingleWindow));
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
    Check(Prog_ThemeGlassUser.menu.a == Prog_DefaultPopupOpacity && Prog_GlassBlur == 24 && state.glassBackdrop == PlatformBackdrop_Acrylic);
    // The transparent theme is the scheme's, its window and surfaces see-through, unless the settings give tints of their own.
    Prog_ApplySettings(&state, S("theme = nord\n"));
    Check(Prog_ThemeGlassUser.text.r == Prog_ThemeUser.text.r && Prog_ThemeGlassUser.menu.b == Prog_ThemeUser.menu.b && Prog_ThemeGlassUser.menu.a == Prog_DefaultPopupOpacity);
    Check(Prog_ThemeGlassUser.window.r == Prog_ThemeUser.window.r && Prog_ThemeGlassUser.window.a == Prog_DefaultGlassOpacity && Prog_ThemeGlassUser.background.a == 0);
    Check(Prog_ThemeGlassUser.selection.g == Prog_ThemeUser.selection.g && Prog_ThemeGlassUser.selection.a < 1);
    Prog_ApplySettings(&state, S("theme = nord\nglass.popup = #102030\nglass.popupOpacity = 40\n"));
    Check(Prog_ThemeGlassUser.menu.b > 0.18f && Prog_ThemeGlassUser.menu.b < 0.19f && Prog_ThemeGlassUser.menu.a == 0.4f);
    Prog_ApplySettings(&state, S("glass.popupOpacity = 80\n"));
    Check(Prog_OptionValue(&state, Prog_Option_PopupOpacity) == 80 && !Prog_OptionIsDefault(&state, Prog_Option_PopupOpacity));
    Prog_ApplySettings(&state, (String){0});
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
    Check(state.noticeCount && StrIsEqual(StrMake((const char *)state.notices[state.noticeCount - 1].title, state.notices[state.noticeCount - 1].titleLength), S("Nothing to undo")));
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
    Check(state.noticeCount && StrIsEqual(StrMake((const char *)state.notices[state.noticeCount - 1].title, state.notices[state.noticeCount - 1].titleLength), S("Nothing to undo")));
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
        Prog_LoadSettings(&state, config, config);
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
        Prog_LoadSettings(&state, config, config);
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
        // A recent folder that is gone is dropped by the next check, and the others stay.
        Prog__AddRecent(&state, Prog_JoinPath(&arena, fixture->path, S("Gone")), false, false);
        u64 before = state.recents.header.count;
        Prog_CheckRecents(&state);
        for (u64 start = PlatformNowMicroseconds(); state.recentsCheckJob && PlatformNowMicroseconds() - start < Millions(5); P__TestPause()) Prog_TakeCompletions(&state);
        Check(!state.recentsCheckJob && state.recents.header.count == before - 1 && TestEndsWith(state.recents.v[0], S("/Empty")));
        Prog_Release(&state);
    }
    Check(P__TestRemovePath(fixture, S("Config/Bookmarks.conf"), false));
    Check(P__TestRemovePath(fixture, S("Config/Recents.conf"), false));
    Check(P__TestRemovePath(fixture, S("Config/Session.conf"), false));
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
        TestGitFetchLimit(&fixture);
        TestRepoCommits(&fixture);
        TestTextIndex(&fixture);
        TestStaleRunFolders(&fixture);
        TestSchemeFiles(&fixture);
        TestStartupWindowColor(&fixture);
    TestPasteLinkAndQuick(&fixture);
        TestEvenPanels(&fixture);
        TestContainingPath(&fixture);
        TestExpand(&fixture);
        TestWatch(&fixture);
        TestMeasure(&fixture);
        TestFileOperations(&fixture);
        TestProgramFileCommands(&fixture);
        TestTreeFileCommands(&fixture);
        TestUpdates(&fixture);
        TestPlaces(&fixture);
        TestUserGroups(&fixture);
        TestIndexPatch();
        TestSidebarNames(&fixture);
        TestOpenFlags(&fixture);
#if defined(_WIN32)
        TestDescribedDrop(&fixture);
        TestVolumeTree();
#endif
        TestTrash(&fixture);
#if defined(_WIN32)
        P__TestDropForwarding(&fixture);
        P__TestNeighborQuery(&fixture);
#endif
        TestFilesPopup(&fixture);
    }
    P__TestRemoveFixture(&fixture);
}
