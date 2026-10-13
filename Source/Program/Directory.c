#include <inttypes.h>
#include <math.h>

function String Prog_JoinPath(Arena *arena, String path, String name)
{
    return StrFormat(arena, "%S%s%S", path, path.length && path.value[path.length - 1] == '/' ? "" : "/", name);
}

// 1024-based with one decimal that is dropped when zero, as in the reference: "1010 B", "1 kB", "11.7 MB".
function String Prog_FormatSize(Arena *arena, u64 bytes)
{
    local_persist const char *units[] = { "kB", "MB", "GB", "TB", "PB" };
    if (bytes < 1024) return StrFormat(arena, "%" PRIu64 " B", bytes);
    f64 value = (f64)bytes / 1024.0;
    u32 unit = 0;
    while (value >= 1024.0 && unit + 1 < CArrayCount(units)) {
        value /= 1024.0;
        ++unit;
    }
    f64 rounded = floor(value * 10.0 + 0.5) / 10.0;
    if (rounded >= 1024.0 && unit + 1 < CArrayCount(units)) {
        rounded = 1;
        ++unit;
    }
    return rounded == floor(rounded) ? StrFormat(arena, "%.0f %s", rounded, units[unit]) : StrFormat(arena, "%.1f %s", rounded, units[unit]);
}

function u8 Prog__FoldAscii(u8 c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

// The lowercase form of `c` for matching.
// Accents stay, so "e" does not match "é", as in the reference.
// ponytail: Latin, Greek and Cyrillic only; add a generated case table if other scripts need it.
function rune Prog__FoldRune(rune c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0xC0) return c;
    if (c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x137 && c != 0x130) return c | 1;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c + (c & 1);
    if (c >= 0x14A && c <= 0x177) return c | 1;
    if (c == 0x178) return 0xFF;
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

// Whether the letters of `filter` appear in `name` in order, ignoring case, as the reference matches.
// Each letter takes its first occurrence after the previous one.
// `marks`, when not NULL, flags the bytes of `name` that matched.
function b32 Prog_FilterMatch(String name, String filter, u8 *marks)
{
    if (marks) MemZero(marks, name.length);
    u64 at = 0;
    for (u64 from = 0; from < filter.length;) {
        rune want = Prog__FoldRune(StrDecodeUtf8(filter, &from));
        for (;;) {
            if (at >= name.length) return false;
            u64 start = at;
            if (Prog__FoldRune(StrDecodeUtf8(name, &at)) == want) {
                if (marks) for (u64 i = start; i < at; ++i) marks[i] = 1;
                break;
            }
        }
    }
    return true;
}

// Rebuilds the view from the filter and the folder and file toggles, and drops hidden rows from the selection.
// After an edit the focus moves to the first match, so that Enter opens it.
function void Prog_ApplyFilter(Prog_State *state, b32 edited)
{
    ArenaClear(&state->viewArena);
    String filter = Prog_EditText(&state->filter);
    state->view = ArenaPushArrayNoZero(&state->viewArena, Max(state->directory.count, (u64)1), u64);
    state->viewCount = state->viewFolders = 0;
    state->selectedCount = 0;
    for (u64 i = 0; i < state->directory.count; ++i) {
        Prog_Entry *entry = &state->directory.entries[i];
        b32 folder = entry->file.kind == P_FileKind_Directory;
        if (!(folder ? state->hideFolders : state->hideFiles) && Prog_FilterMatch(entry->file.name, filter, NULL)) {
            state->view[state->viewCount++] = i;
            state->viewFolders += folder != 0;
        } else {
            entry->selected = false;
        }
        state->selectedCount += entry->selected != 0;
    }
    if (edited || (state->hasFocus && Prog_ViewPosition(state, state->focusedRow) == UINT64_MAX)) {
        state->hasFocus = state->viewCount != 0;
        state->focusedRow = state->viewCount ? state->view[0] : 0;
        state->revealFocus = true;
    }
    if (state->hasAnchor && Prog_ViewPosition(state, state->anchorRow) == UINT64_MAX) {
        state->hasAnchor = state->hasFocus;
        state->anchorRow = state->focusedRow;
    }
}

function void Prog_ClearFilter(Prog_State *state)
{
    if (state->filter.length) ++state->filter.version;
    state->filter.length = state->filter.cursor = state->filter.anchor = 0;
    state->filtering = false;
}

// Where listing row `row` is in the view, or UINT64_MAX when the filter hides it.
function u64 Prog_ViewPosition(const Prog_State *state, u64 row)
{
    u64 low = 0, high = state->viewCount;
    while (low < high) {
        u64 middle = low + (high - low) / 2;
        if (state->view[middle] < row) low = middle + 1;
        else high = middle;
    }
    return low < state->viewCount && state->view[low] == row ? low : UINT64_MAX;
}

function String Prog_ParentPath(String path)
{
    u64 root = 1;
    if (path.length >= 3 && path.value[1] == ':' && path.value[2] == '/') {
        root = 3;
    } else if (path.length >= 2 && path.value[0] == '/' && path.value[1] == '/') {
        u64 start = 2;
        u32 components = 2;
        if (path.length >= 4 && path.value[2] == '?' && path.value[3] == '/') {
            start = 4;
            components = 1;
            if (path.length >= 8 && Prog__FoldAscii((u8)path.value[4]) == 'u' && Prog__FoldAscii((u8)path.value[5]) == 'n' && Prog__FoldAscii((u8)path.value[6]) == 'c' && path.value[7] == '/') {
                start = 8;
                components = 2;
            }
        }
        root = path.length;
        for (u64 i = start; i < path.length; ++i) {
            if (path.value[i] == '/' && --components == 0) {
                root = i + 1;
                break;
            }
        }
    }
    u64 end = path.length;
    while (end > root && path.value[end - 1] == '/') --end;
    while (end > root && path.value[end - 1] != '/') --end;
    if (end > root) --end;
    return StrMake(path.value, end);
}

function i32 Prog_CompareNames(String a, String b)
{
    u64 ai = 0, bi = 0;
    while (ai < a.length && bi < b.length) {
        u8 ac = Prog__FoldAscii((u8)a.value[ai]);
        u8 bc = Prog__FoldAscii((u8)b.value[bi]);
        if (ac >= '0' && ac <= '9' && bc >= '0' && bc <= '9') {
            u64 ae = ai, be = bi;
            while (ae < a.length && a.value[ae] >= '0' && a.value[ae] <= '9') ++ae;
            while (be < b.length && b.value[be] >= '0' && b.value[be] <= '9') ++be;
            u64 az = ai, bz = bi;
            while (az < ae && a.value[az] == '0') ++az;
            while (bz < be && b.value[bz] == '0') ++bz;
            if (ae - az != be - bz) return ae - az < be - bz ? -1 : 1;
            for (u64 i = 0; i < ae - az; ++i) {
                if (a.value[az + i] != b.value[bz + i]) return a.value[az + i] < b.value[bz + i] ? -1 : 1;
            }
            ai = ae;
            bi = be;
        } else {
            if (ac != bc) return ac < bc ? -1 : 1;
            ++ai;
            ++bi;
        }
    }
    if (ai != a.length || bi != b.length) return ai == a.length ? -1 : 1;
    // shortcut: Unicode names use UTF-8 byte order; add locale collation when its behavior is specified.
    u64 count = Min(a.length, b.length);
    for (u64 i = 0; i < count; ++i) {
        if ((u8)a.value[i] != (u8)b.value[i]) return (u8)a.value[i] < (u8)b.value[i] ? -1 : 1;
    }
    return a.length == b.length ? 0 : a.length < b.length ? -1 : 1;
}

function i32 Prog__CompareEntries(const Prog_Entry *a, const Prog_Entry *b, const Prog_JobInput *input)
{
    b32 ad = a->file.kind == P_FileKind_Directory;
    b32 bd = b->file.kind == P_FileKind_Directory;
    if (ad != bd) return ad ? -1 : 1;
    i32 result = 0;
    switch (input->sort) {
        case Prog_Sort_Name: break;
        case Prog_Sort_Type: result = Prog_CompareNames(a->type, b->type); break;
        case Prog_Sort_Modified:
            result = a->file.modifiedTimeUnix == b->file.modifiedTimeUnix ? 0 : a->file.modifiedTimeUnix < b->file.modifiedTimeUnix ? -1 : 1;
            break;
        case Prog_Sort_Size: result = a->file.size == b->file.size ? 0 : a->file.size < b->file.size ? -1 : 1; break;
        default: InvalidPath;
    }
    if (!result) result = Prog_CompareNames(a->file.name, b->file.name);
    return input->descending ? -result : result;
}

function void Prog__DirectoryJob(P_Job *job)
{
    Prog_JobInput *input = job->input;
    if (input->kind == Prog_JobKind_Open || input->kind == Prog_JobKind_Rename) {
        P_FileError *error = ArenaPushStruct(&job->arena, P_FileError);
        if (!P_IsCancelled(&job->cancelToken)) {
            *error = input->kind == Prog_JobKind_Open ? P_OpenPath(&job->arena, input->path, (P_FileOrder){ input->sort, (b8)input->descending })
                                                      : P_RenamePath(&job->arena, input->path, input->name, input->newName);
        }
        job->result = error;
        return;
    }
    if (input->kind == Prog_JobKind_Measure) {
        Prog_Measured *result = ArenaPushStruct(&job->arena, Prog_Measured);
        result->totals = ArenaPushArray(&job->arena, Max(input->rowCount, (u64)1), P_DirectoryTotals);
        job->result = result;
        // Hands back what it has every few dozen milliseconds, so rows fill in while big trees are walked.
        u64 start = PlatformNowMicroseconds();
        while (result->count < input->rowCount && PlatformNowMicroseconds() - start < UINT64_C(50000)) {
            String path = Prog_JoinPath(&job->arena, input->path, input->names[result->count]);
            P_DirectoryTotals totals = {0};
            ProfileScope("MeasureDirectory") totals = P_MeasureDirectory(path, &job->cancelToken);
            if (P_IsCancelled(&job->cancelToken)) break;
            result->totals[result->count++] = totals;
        }
        return;
    }
    if (input->kind == Prog_JobKind_WarmMenu) {
        ProfileScope("WarmMenu") P_ContextMenuWarm(input->path, input->name);
        return;
    }
    Prog_Directory *result = ArenaPushStruct(&job->arena, Prog_Directory);
    job->result = result;
    result->path = input->path;
    result->sort = input->sort;
    result->descending = input->descending;
    result->showHidden = input->showHidden;
    P_DirectoryResult listing = {0};
    ProfileScope("EnumerateDirectory") listing = P_EnumerateDirectory(&job->arena, input->path, &job->cancelToken);
    result->error = (P_FileError){ listing.errorCode, listing.errorString };
    result->cancelled = listing.cancelled;
    if (result->error.code || result->cancelled) return;
    result->entries = ArenaPushArray(&job->arena, listing.files.header.count, Prog_Entry);
    // Type names repeat per type, so each is asked of the system once per listing.
    struct { MapHeader header; struct { String key; String value; } *v; } types = {0};
    for (u64 i = 0; i < listing.files.header.count; ++i) {
        if (P_IsCancelled(&job->cancelToken)) { result->cancelled = true; return; }
        P_File file = listing.files.v[i];
        if (file.kind == P_FileKind_Directory) ++result->totalFolders;
        else ++result->totalFiles;
        if (file.hidden && !input->showHidden) continue;
        Prog_Entry *entry = &result->entries[result->count++];
        entry->file = file;
        result->folderCount += file.kind == P_FileKind_Directory;
        String typeKey = P_FileTypeKey(&job->arena, file.name, file.kind);
        String *type = MapsGetPtrNull(types, typeKey);
        if (type) {
            entry->type = *type;
        } else {
            entry->type = P_FileTypeName(&job->arena, file.name, file.kind);
            MapsPut(&job->arena, types, typeKey, entry->type);
        }
        if (file.symbolicLink) entry->type = file.kind == P_FileKind_Directory ? S("Folder link") : S("Link");
        if (file.metadataValid) {
            entry->modified = P_FormatFileTime(&job->arena, file.modifiedTimeUnix);
        }
    }
    Prog_Entry *buffer = ArenaPushArray(&job->arena, result->count, Prog_Entry);
    for (u64 width = 1; width < result->count; width *= 2) {
        for (u64 start = 0; start < result->count; start += width * 2) {
            u64 middle = Min(start + width, result->count);
            u64 end = Min(start + width * 2, result->count);
            u64 left = start, right = middle;
            for (u64 out = start; out < end; ++out) {
                if ((out & 255) == 0 && P_IsCancelled(&job->cancelToken)) { result->cancelled = true; return; }
                b32 takeLeft = right == end || (left < middle && Prog__CompareEntries(&result->entries[left], &result->entries[right], input) <= 0);
                buffer[out] = result->entries[takeLeft ? left++ : right++];
            }
        }
        Prog_Entry *swap = result->entries;
        result->entries = buffer;
        buffer = swap;
    }
}

// Measures the shown folder's subfolders that have no totals yet, one job at a time.
function void Prog__MeasureFolders(Prog_State *state)
{
    if (state->measureJob) return;
    u64 count = 0;
    for (u64 i = 0; i < state->directory.count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        count += entry->file.kind == P_FileKind_Directory && !entry->measured;
    }
    if (!count) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Measure;
    input->path = StrCopy(&job->arena, state->directory.path);
    input->listing = state->listing;
    input->names = ArenaPushArray(&job->arena, count, String);
    input->rows = ArenaPushArray(&job->arena, count, u64);
    for (u64 i = 0; i < state->directory.count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        if (entry->file.kind != P_FileKind_Directory || entry->measured) continue;
        input->names[input->rowCount] = StrCopy(&job->arena, entry->file.name);
        input->rows[input->rowCount++] = i;
    }
    job->input = input;
    state->measureJob = job;
    P_JobSubmit(job);
}

function void Prog_RequestDirectory(Prog_State *state, String path, b32 addHistory, u64 historyAt)
{
    if (state->directoryJob) P_RequestCancel(&state->directoryJob->cancelToken);
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->path = StrCopy(&job->arena, path);
    input->generation = ++state->generation;
    input->sort = state->sort;
    input->descending = state->descending;
    input->showHidden = state->showHidden;
    input->addHistory = addHistory;
    input->historyAt = historyAt;
    job->input = input;
    state->directoryJob = job;
    // Refreshing the shown folder keeps its rows usable until the new listing replaces them.
    state->loading = !StrIsEqual(path, state->directory.path);
    ArenaClear(&state->errorArena);
    state->error = (String){0};
    P_JobSubmit(job);
}

function b32 Prog_Init(Prog_State *state, String path, P_JobWakeCallback *wake, void *user)
{
    if (!P_JobsInit(&state->jobs, &state->jobsArena, 0, wake, user)) {
        ArenaRelease(&state->jobsArena);
        return false;
    }
    Arena temporary = {0};
    String absolute = P_AbsolutePath(&temporary, path);
    if (absolute.length) Prog_RequestDirectory(state, absolute, true, 0);
    else state->error = S("Cannot resolve the initial directory");
    ArenaRelease(&temporary);
    return true;
}

// Coalesces bursts such as a copy or a build into a few reloads.
#define Prog_WatchInterval UINT64_C(100000)

function b32 Prog_TakeCompletions(Prog_State *state)
{
    b32 changed = false;
    u64 now = PlatformNowMicroseconds();
    if (P_DirectoryWatchTakeChanged(&state->watch)) state->watchDirty = true;
    if (state->watchDirty && !state->directoryJob && now - state->watchReloadTime >= Prog_WatchInterval) {
        state->watchDirty = false;
        state->watchReloadTime = now;
        Prog_Reload(state);
    }
    // A deferred reload needs frames until it runs.
    changed |= state->watchDirty;
    for (P_Job *job; (job = P_JobTakeCompleted(&state->jobs)) != NULL;) {
        Prog_JobInput *input = job->input;
        if (input->kind == Prog_JobKind_Icons) {
            Prog__TakeIcons(state, job);
            changed = true;
        } else if (input->kind == Prog_JobKind_Open) {
            P_FileError *error = job->result;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrCopy(&state->errorArena, error->message);
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_Rename) {
            P_FileError *error = job->result;
            changed = true;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "Rename %S: %S", input->name, error->message);
            } else if (StrIsEqual(state->directory.path, input->path)) {
                // Reloading now, rather than on the watch, selects the new name without waiting.
                MemCopy(state->selectText, input->newName.value, input->newName.length);
                state->selectLength = (u32)input->newName.length;
                Prog_Reload(state);
            }
        } else if (input->kind == Prog_JobKind_WarmMenu) {
        } else if (input->kind == Prog_JobKind_Measure) {
            Prog_Measured *result = job->result;
            if (state->measureJob == job) state->measureJob = NULL;
            if (result && StrIsEqual(input->path, state->directory.path) && !P_IsCancelled(&job->cancelToken)) {
                // A refresh may have replaced the listing while the job ran.
                b32 sameListing = input->listing == state->listing;
                struct { MapHeader header; struct { String key; u64 value; } *v; } rows = {0};
                if (!sameListing) {
                    for (u64 i = 0; i < state->directory.count; ++i) MapsPut(&state->frameArena, rows, state->directory.entries[i].file.name, i + 1);
                }
                for (u64 i = 0; i < result->count; ++i) {
                    u64 row = sameListing ? input->rows[i] : MapsGet(rows, u64, input->names[i]) - 1;
                    if (row >= state->directory.count || state->directory.entries[row].file.kind != P_FileKind_Directory) continue;
                    Prog_Entry *entry = &state->directory.entries[row];
                    entry->measured = true;
                    entry->folders = result->totals[i].folders;
                    entry->files = result->totals[i].files;
                    entry->bytes = result->totals[i].bytes;
                }
                changed = true;
            }
            Prog__MeasureFolders(state);
        } else if (input->generation == state->generation) {
            state->directoryJob = NULL;
            state->loading = false;
            changed = true;
            Prog_Directory *result = job->result;
            if (result && !result->cancelled && !P_IsCancelled(&job->cancelToken)) {
                ArenaClear(&state->errorArena);
                state->error = (String){0};
                if (result->error.code) {
                    state->error = StrFormat(&state->errorArena, "%S: %S", input->path, result->error.message);
                    state->sort = state->directory.sort;
                    state->descending = state->directory.descending;
                    state->showHidden = state->directory.showHidden;
                } else {
                    b32 samePath = StrIsEqual(state->directory.path, result->path);
                    String focus = state->hasFocus ? state->directory.entries[state->focusedRow].file.name : (String){0};
                    String anchor = state->hasAnchor ? state->directory.entries[state->anchorRow].file.name : (String){0};
                    struct { MapHeader header; struct { String key; b32 value; } *v; } selected = {0};
                    if (samePath) {
                        for (u64 i = 0; i < state->directory.count; ++i) {
                            if (state->directory.entries[i].selected) MapsPut(&state->frameArena, selected, state->directory.entries[i].file.name, true);
                        }
                    }
                    // After a shell command, the one item it created is renamed, as in Explorer.
                    // Commands such as Send to a zip may finish after this listing, so the wait spans a few.
                    b32 renameCreated = samePath && now < state->renameCreatedUntil;
                    if (!renameCreated) state->renameCreatedUntil = 0;
                    struct { MapHeader header; struct { String key; b32 value; } *v; } known = {0};
                    if (renameCreated) {
                        for (u64 i = 0; i < state->directory.count; ++i) MapsPut(&state->frameArena, known, state->directory.entries[i].file.name, true);
                    }
                    // Refreshes keep the totals of folders that were not modified, so a busy folder is not walked again and again.
                    struct { MapHeader header; struct { String key; Prog_Entry *value; } *v; } totals = {0};
                    if (samePath) {
                        for (u64 i = 0; i < state->directory.count; ++i) {
                            Prog_Entry *old = &state->directory.entries[i];
                            if (old->measured) MapsPut(&state->frameArena, totals, old->file.name, old);
                        }
                    }
                    String pick = samePath ? StrMake((const char *)state->selectText, state->selectLength) : (String){0};
                    u64 created = 0, pickRow = UINT64_MAX;
                    b32 renameStillThere = false;
                    String renameFrom = StrMake((const char *)state->renameFrom, state->renameFromLength);
                    state->hasFocus = state->hasAnchor = false;
                    state->selectedCount = 0;
                    for (u64 i = 0; i < result->count; ++i) {
                        Prog_Entry *entry = &result->entries[i];
                        entry->selected = samePath && MapsGet(selected, b32, entry->file.name);
                        state->selectedCount += entry->selected != 0;
                        if (samePath && StrIsEqual(focus, entry->file.name)) { state->hasFocus = true; state->focusedRow = i; }
                        if (samePath && StrIsEqual(anchor, entry->file.name)) { state->hasAnchor = true; state->anchorRow = i; }
                        if (renameCreated && !MapsGet(known, b32, entry->file.name) && ++created == 1) pickRow = i;
                        if (pick.length && StrIsEqual(pick, entry->file.name)) pickRow = i;
                        renameStillThere |= samePath && StrIsEqual(renameFrom, entry->file.name);
                        Prog_Entry **old = samePath ? MapsGetPtrNull(totals, entry->file.name) : NULL;
                        if (old && (*old)->file.kind == entry->file.kind && (*old)->file.modifiedTimeUnix == entry->file.modifiedTimeUnix) {
                            entry->measured = true;
                            entry->folders = (*old)->folders;
                            entry->files = (*old)->files;
                            entry->bytes = (*old)->bytes;
                        }
                    }
                    if (renameCreated && created != 1 && !pick.length) pickRow = UINT64_MAX;
                    // The filter resets in another folder unless pinned, and gives way to an item that was just created or renamed.
                    if ((!samePath && !state->filterSticky) || (pickRow < result->count && !Prog_FilterMatch(result->entries[pickRow].file.name, Prog_EditText(&state->filter), NULL))) {
                        Prog_ClearFilter(state);
                    }
                    b32 renamePicked = renameCreated && created == 1 && !pick.length;
                    if (created) state->renameCreatedUntil = 0;
                    state->selectLength = 0;
                    if (!renameStillThere) state->renaming = false;
                    if (pickRow < result->count) {
                        for (u64 i = 0; i < result->count; ++i) result->entries[i].selected = i == pickRow;
                        state->selectedCount = 1;
                        state->hasFocus = state->hasAnchor = true;
                        state->focusedRow = state->anchorRow = pickRow;
                        state->revealFocus = true;
                    }
                    if (!samePath) state->resetScroll = true;
                    if (input->addHistory && (!state->history.header.count || !StrIsEqual(state->history.v[state->historyAt], input->path))) {
                        if (state->history.header.count) state->history.header.count = state->historyAt + 1;
                        ArrayPush(&state->historyArena, state->history, StrCopy(&state->historyArena, input->path));
                        state->historyAt = state->history.header.count - 1;
                    } else if (!input->addHistory && state->history.header.count) {
                        state->historyAt = input->historyAt;
                    }
                    // The menu's rows index the listing being replaced.
                    Prog_CloseMenu(state);
                    ArenaRelease(&state->directoryArena);
                    state->directoryArena = job->arena;
                    state->directory = *result;
                    job->arena = (Arena){0};
                    ++state->listing;
                    Prog_ApplyFilter(state, false);
                    // A walk under way in this folder carries on; refreshes would otherwise restart it forever.
                    if (!samePath && state->measureJob) {
                        P_RequestCancel(&state->measureJob->cancelToken);
                        state->measureJob = NULL;
                    }
                    Prog__MeasureFolders(state);
                    // ponytail: changes between enumerating and starting the watch are missed; start it before the listing if that shows.
                    if (!samePath || !state->watch.native) {
                        P_DirectoryWatchStart(&state->watch, state->directory.path, state->jobs.wake, state->jobs.wakeUser);
                        state->watchDirty = false;
                    }
                    if (renamePicked) Prog_BeginRename(state, pickRow);
                    if (state->warmMenus && state->directory.count) {
                        state->warmMenus = false;
                        u64 file = 0;
                        while (file + 1 < state->directory.count && state->directory.entries[file].file.kind == P_FileKind_Directory) ++file;
                        P_Job *warm = P_JobCreate(&state->jobs, Prog__DirectoryJob);
                        Prog_JobInput *warmInput = ArenaPushStruct(&warm->arena, Prog_JobInput);
                        warmInput->kind = Prog_JobKind_WarmMenu;
                        warmInput->path = StrCopy(&warm->arena, state->directory.path);
                        warmInput->name = StrCopy(&warm->arena, state->directory.entries[file].file.name);
                        warm->input = warmInput;
                        P_JobSubmit(warm);
                    }
                }
            }
        }
        P_JobRelease(job);
    }
    return changed;
}

function void Prog_SelectRow(Prog_State *state, u64 row, KeyMods mods)
{
    u64 at = Prog_ViewPosition(state, row), anchorAt = state->hasAnchor ? Prog_ViewPosition(state, state->anchorRow) : UINT64_MAX;
    if (at == UINT64_MAX) return;
    if (!(mods & KeyMod_Control)) {
        for (u64 i = 0; i < state->directory.count; ++i) state->directory.entries[i].selected = false;
        state->selectedCount = 0;
    }
    if ((mods & KeyMod_Shift) && anchorAt != UINT64_MAX) {
        // The range runs over the rows the filter shows.
        for (u64 i = Min(at, anchorAt); i <= Max(at, anchorAt); ++i) {
            Prog_Entry *entry = &state->directory.entries[state->view[i]];
            if (!entry->selected) ++state->selectedCount;
            entry->selected = true;
        }
    } else {
        state->directory.entries[row].selected = (mods & KeyMod_Control) ? !state->directory.entries[row].selected : true;
        if (state->directory.entries[row].selected) ++state->selectedCount;
        else --state->selectedCount;
        state->anchorRow = row;
        state->hasAnchor = true;
    }
    state->focusedRow = row;
    state->hasFocus = true;
}

function void Prog_OpenRow(Prog_State *state, u64 row)
{
    if (state->loading || row >= state->directory.count) return;
    Prog_Entry *entry = &state->directory.entries[row];
    String path = Prog_JoinPath(&state->frameArena, state->directory.path, entry->file.name);
    if (entry->file.kind == P_FileKind_Directory) {
        Prog_RequestDirectory(state, path, true, 0);
    } else {
        P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
        input->kind = Prog_JobKind_Open;
        input->generation = state->generation;
        input->sort = state->directory.sort;
        input->descending = state->directory.descending;
        input->path = StrCopy(&job->arena, path);
        job->input = input;
        P_JobSubmit(job);
    }
}

function void Prog_Release(Prog_State *state)
{
    // Its thread wakes the window, like the workers.
    P_DirectoryWatchStop(&state->watch);
    Prog_CloseMenu(state);
    ArenaRelease(&state->menuArena);
    P_JobsShutdown(&state->jobs);
    UI_Release(&state->ui);
    ArenaRelease(&state->frameArena);
    ArenaRelease(&state->directoryArena);
    ArenaRelease(&state->viewArena);
    ArenaRelease(&state->historyArena);
    ArenaRelease(&state->errorArena);
    ArenaRelease(&state->iconArena);
    ArenaRelease(&state->jobsArena);
    *state = (Prog_State){0};
}
