#include <inttypes.h>

function String Prog_JoinPath(Arena *arena, String path, String name)
{
    return StrFormat(arena, "%S%s%S", path, path.length && path.value[path.length - 1] == '/' ? "" : "/", name);
}

function u8 Prog__FoldAscii(u8 c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
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
    if (input->kind == Prog_JobKind_Open) {
        P_FileError *error = ArenaPushStruct(&job->arena, P_FileError);
        if (!P_IsCancelled(&job->cancelToken)) *error = P_OpenPath(&job->arena, input->path);
        job->result = error;
        return;
    }
    Prog_Directory *result = ArenaPushStruct(&job->arena, Prog_Directory);
    job->result = result;
    result->path = input->path;
    result->sort = input->sort;
    result->descending = input->descending;
    result->showHidden = input->showHidden;
    P_DirectoryResult listing = P_EnumerateDirectory(&job->arena, input->path, &job->cancelToken);
    result->error = (P_FileError){ listing.errorCode, listing.errorString };
    result->cancelled = listing.cancelled;
    if (result->error.code || result->cancelled) return;
    result->entries = ArenaPushArray(&job->arena, listing.files.header.count, Prog_Entry);
    for (u64 i = 0; i < listing.files.header.count; ++i) {
        if (P_IsCancelled(&job->cancelToken)) { result->cancelled = true; return; }
        P_File file = listing.files.v[i];
        if (file.hidden && !input->showHidden) continue;
        Prog_Entry *entry = &result->entries[result->count++];
        entry->file = file;
        result->folderCount += file.kind == P_FileKind_Directory;
        entry->type = file.kind == P_FileKind_Directory ? S("Folder") : S("File");
        if (file.kind != P_FileKind_Directory) {
            Index dot = StrIndexChar(file.name, '.', SSF_SearchBackwards);
            if (dot.exists && dot.value && dot.value + 1 < file.name.length) {
                entry->type = StrFormat(&job->arena, "%S file", StrFrom(file.name, dot.value + 1));
            }
        }
        if (file.symbolicLink) entry->type = file.kind == P_FileKind_Directory ? S("Folder link") : S("Link");
        if (file.metadataValid) {
            entry->modified = P_FormatFileTime(&job->arena, file.modifiedTimeUnix);
            if (file.kind != P_FileKind_Directory) {
                entry->size = file.size < 1024 ? StrFormat(&job->arena, "%" PRIu64 " B", file.size)
                    : file.size < UINT64_C(1048576) ? StrFormat(&job->arena, "%.1f KB", (f64)file.size / 1024.0)
                    : file.size < UINT64_C(1073741824) ? StrFormat(&job->arena, "%.1f MB", (f64)file.size / 1048576.0)
                    : StrFormat(&job->arena, "%.1f GB", (f64)file.size / 1073741824.0);
            }
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
    state->loading = true;
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

function b32 Prog_TakeCompletions(Prog_State *state)
{
    b32 changed = false;
    for (P_Job *job; (job = P_JobTakeCompleted(&state->jobs)) != NULL;) {
        Prog_JobInput *input = job->input;
        if (input->kind == Prog_JobKind_Open) {
            P_FileError *error = job->result;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrCopy(&state->errorArena, error->message);
                changed = true;
            }
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
                    state->hasFocus = state->hasAnchor = false;
                    state->selectedCount = 0;
                    for (u64 i = 0; i < result->count; ++i) {
                        Prog_Entry *entry = &result->entries[i];
                        entry->selected = samePath && MapsGet(selected, b32, entry->file.name);
                        state->selectedCount += entry->selected != 0;
                        if (samePath && StrIsEqual(focus, entry->file.name)) { state->hasFocus = true; state->focusedRow = i; }
                        if (samePath && StrIsEqual(anchor, entry->file.name)) { state->hasAnchor = true; state->anchorRow = i; }
                    }
                    if (!samePath) state->resetScroll = true;
                    if (input->addHistory && (!state->history.header.count || !StrIsEqual(state->history.v[state->historyAt], input->path))) {
                        if (state->history.header.count) state->history.header.count = state->historyAt + 1;
                        ArrayPush(&state->historyArena, state->history, StrCopy(&state->historyArena, input->path));
                        state->historyAt = state->history.header.count - 1;
                    } else if (!input->addHistory && state->history.header.count) {
                        state->historyAt = input->historyAt;
                    }
                    ArenaRelease(&state->directoryArena);
                    state->directoryArena = job->arena;
                    state->directory = *result;
                    job->arena = (Arena){0};
                }
            }
        }
        P_JobRelease(job);
    }
    return changed;
}

function void Prog_SelectRow(Prog_State *state, u64 row, KeyMods mods)
{
    if (row >= state->directory.count) return;
    if (!(mods & KeyMod_Control)) {
        for (u64 i = 0; i < state->directory.count; ++i) state->directory.entries[i].selected = false;
        state->selectedCount = 0;
    }
    if ((mods & KeyMod_Shift) && state->hasAnchor) {
        u64 first = Min(row, state->anchorRow), last = Max(row, state->anchorRow);
        for (u64 i = first; i <= last; ++i) {
            if (!state->directory.entries[i].selected) ++state->selectedCount;
            state->directory.entries[i].selected = true;
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
        input->path = StrCopy(&job->arena, path);
        job->input = input;
        P_JobSubmit(job);
    }
}

function void Prog_Release(Prog_State *state)
{
    P_JobsShutdown(&state->jobs);
    UI_Release(&state->ui);
    ArenaRelease(&state->frameArena);
    ArenaRelease(&state->directoryArena);
    ArenaRelease(&state->historyArena);
    ArenaRelease(&state->errorArena);
    ArenaRelease(&state->jobsArena);
    *state = (Prog_State){0};
}
