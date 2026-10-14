#include <inttypes.h>
#include <math.h>

function String Prog_JoinPath(Arena *arena, String path, String name)
{
    return StrFormat(arena, "%S%s%S", path, path.length && path.value[path.length - 1] == '/' ? "" : "/", name);
}

// 1024-based with one decimal that is dropped when zero: "1010 B", "1 kB", "11.7 MB".
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

// The last component of an entry's name, which in an expanded listing is a path.
function String Prog_EntryLeaf(String name)
{
    Index slash = StrIndexChar(name, '/', SSF_SearchBackwards);
    return slash.exists ? StrFromTo(name, slash.value + 1, (i64)name.length) : name;
}

// The folder an entry is in, relative to the shown folder; empty for its own items.
function String Prog_EntryParent(String name)
{
    Index slash = StrIndexChar(name, '/', SSF_SearchBackwards);
    return slash.exists ? StrFromTo(name, 0, (i64)slash.value) : (String){0};
}

// The lowercase form of `c` for matching.
// Accents stay, so "e" does not match "é".
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

// Whether the letters of `filter` appear in `name` in order, ignoring case.
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
        if (!(folder ? state->hideFolders : state->hideFiles) && Prog_FilterMatch(Prog_EntryLeaf(entry->file.name), filter, NULL)) {
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

#define Prog__Order(a, b) ((a) == (b) ? 0 : (a) < (b) ? -1 : 1)

// Folders first, then by `sort`, then by name.
// Folders sort by their totals once measured, and by zero until then.
function i32 Prog__CompareEntries(const Prog_Entry *a, const Prog_Entry *b, Prog_Sort sort, b32 descending)
{
    b32 ad = a->file.kind == P_FileKind_Directory;
    b32 bd = b->file.kind == P_FileKind_Directory;
    if (ad != bd) return ad ? -1 : 1;
    i32 result = 0;
    switch (sort) {
        case Prog_Sort_Name: break;
        case Prog_Sort_Type: result = Prog_CompareNames(a->type, b->type); break;
        case Prog_Sort_Modified:
            result = a->file.modifiedTimeUnix == b->file.modifiedTimeUnix ? 0 : a->file.modifiedTimeUnix < b->file.modifiedTimeUnix ? -1 : 1;
            break;
        case Prog_Sort_Size: result = ad ? Prog__Order(a->bytes, b->bytes) : Prog__Order(a->file.size, b->file.size); break;
        case Prog_Sort_Folders: result = Prog__Order(a->folders, b->folders); break;
        case Prog_Sort_Files: result = Prog__Order(a->files, b->files); break;
        default: InvalidPath;
    }
    if (!result) result = Prog_CompareNames(a->file.name, b->file.name);
    return descending ? -result : result;
}

// Stable bottom-up merge sort that alternates between `entries` and `buffer`.
// Returns the one that ends up sorted, or NULL when cancelled.
function Prog_Entry *Prog__SortEntries(Prog_Entry *entries, Prog_Entry *buffer, u64 count, Prog_Sort sort, b32 descending, const P_CancelToken *cancel)
{
    for (u64 width = 1; width < count; width *= 2) {
        for (u64 start = 0; start < count; start += width * 2) {
            u64 middle = Min(start + width, count);
            u64 end = Min(start + width * 2, count);
            u64 left = start, right = middle;
            for (u64 out = start; out < end; ++out) {
                if (cancel && (out & 255) == 0 && P_IsCancelled(cancel)) return NULL;
                b32 takeLeft = right == end || (left < middle && Prog__CompareEntries(&entries[left], &entries[right], sort, descending) <= 0);
                buffer[out] = entries[takeLeft ? left++ : right++];
            }
        }
        Prog_Entry *swap = entries;
        entries = buffer;
        buffer = swap;
    }
    return entries;
}

// Sorts on folder totals are redone as the totals arrive.
// Focus, anchor and selection stay on the same items.
function void Prog__SortByTotals(Prog_State *state)
{
    Prog_Directory *directory = &state->directory;
    Prog_Sort sort = directory->sort;
    if (sort != Prog_Sort_Size && sort != Prog_Sort_Folders && sort != Prog_Sort_Files) return;
    // An open menu or a drag refers to rows by index.
    if (state->menuOpen || state->dragging) {
        state->sortPending = true;
        return;
    }
    state->sortPending = false;
    b32 sorted = true;
    for (u64 i = 1; i < directory->count && sorted; ++i) {
        sorted = Prog__CompareEntries(&directory->entries[i - 1], &directory->entries[i], sort, directory->descending) <= 0;
    }
    if (sorted) return;
    String focus = state->hasFocus ? directory->entries[state->focusedRow].file.name : (String){0};
    String anchor = state->hasAnchor ? directory->entries[state->anchorRow].file.name : (String){0};
    Scratch scratch = GetScratch();
    Prog_Entry *buffer = ArenaPushArrayNoZero(scratch.arena, directory->count, Prog_Entry);
    Prog_Entry *result = Prog__SortEntries(directory->entries, buffer, directory->count, sort, directory->descending, NULL);
    if (result != directory->entries) MemCopy(directory->entries, result, directory->count * sizeof(Prog_Entry));
    ReleaseScratch(scratch);
    for (u64 i = 0; i < directory->count; ++i) {
        String name = directory->entries[i].file.name;
        if (state->hasFocus && StrIsEqual(name, focus)) state->focusedRow = i;
        if (state->hasAnchor && StrIsEqual(name, anchor)) state->anchorRow = i;
    }
    state->hasHoverRow = false;
    // Measure results for the old order are matched by name.
    ++state->listing;
    Prog_ApplyFilter(state, false);
}

// Whether the start of a file reads as text: no NUL bytes, and nearly all of it UTF-8; the end may cut a character.
function b32 Prog__LooksLikeText(String start)
{
    if (!start.length) return false;
    u64 runes = 0, invalid = 0;
    u64 end = start.length > 4 ? start.length - 4 : start.length;
    for (u64 at = 0; at < end;) {
        if (start.value[at] == 0) return false;
        invalid += StrDecodeUtf8(start, &at) == 0xFFFD;
        ++runes;
    }
    return invalid * 50 <= runes;
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
    if (input->kind == Prog_JobKind_Transfer || input->kind == Prog_JobKind_Trash || input->kind == Prog_JobKind_Delete) {
        Prog_Transferred *result = ArenaPushStruct(&job->arena, Prog_Transferred);
        P_FileError *error = &result->error;
        if (P_IsCancelled(&job->cancelToken)) {
        } else if (input->kind == Prog_JobKind_Transfer) {
            ProfileScope("TransferItems") *error = P_TransferItems(&job->arena, input->paths, input->pathCount, input->path, input->move, input->progress, &result->placed, &job->cancelToken);
        } else if (input->kind == Prog_JobKind_Delete) {
            ProfileScope("DeleteItems") *error = P_DeleteItems(&job->arena, input->paths, input->pathCount, &job->cancelToken);
        } else {
            ProfileScope("TrashItems") *error = P_TrashItems(&job->arena, input->paths, input->pathCount);
        }
        job->result = error;
        return;
    }
    if (input->kind == Prog_JobKind_Undo) {
        // Each step is reversed with the operations that made it; a moved item that went under another name gets its old one back.
        P_FileError *error = ArenaPushStruct(&job->arena, P_FileError);
        const Prog_Undo *undo = &input->undo;
        if (undo->kind == Prog_UndoKind_Rename && undo->count == 1) {
            *error = P_RenamePath(&job->arena, Prog_ParentPath(undo->to[0]), Prog_EntryLeaf(undo->to[0]), Prog_EntryLeaf(undo->from[0]));
        } else if (undo->kind == Prog_UndoKind_Create || undo->kind == Prog_UndoKind_Copy) {
            String *paths = ArenaPushArray(&job->arena, Max(undo->count, (u64)1), String);
            u64 count = 0;
            for (u64 i = 0; i < undo->count; ++i) if (undo->to[i].length) paths[count++] = undo->to[i];
            if (count) *error = P_TrashItems(&job->arena, paths, count);
        } else if (undo->kind == Prog_UndoKind_Move) {
            for (u64 i = 0; i < undo->count && !error->code; ++i) {
                if (!undo->to[i].length || StrIsEqual(undo->to[i], undo->from[i])) continue;
                String folder = Prog_ParentPath(undo->from[i]);
                ArrayStr placed = {0};
                *error = P_TransferItems(&job->arena, &undo->to[i], 1, folder, true, NULL, &placed, &job->cancelToken);
                String back = placed.header.count ? Prog_EntryLeaf(placed.v[0]) : (String){0}, name = Prog_EntryLeaf(undo->from[i]);
                if (!error->code && back.length && !StrIsEqual(back, name)) *error = P_RenamePath(&job->arena, folder, back, name);
            }
        }
        job->result = error;
        return;
    }
    if (input->kind == Prog_JobKind_Preview) {
        // Text where the start of the file reads as text, else the system's picture of it.
        Prog_Preview *result = ArenaPushStruct(&job->arena, Prog_Preview);
        String start = {0};
        if (!input->folder && P_ReadFileStart(&job->arena, input->path, Prog_PreviewBytes, &start) && Prog__LooksLikeText(start)) {
            result->text = start;
        } else if (!P_IsCancelled(&job->cancelToken)) {
            Scratch scratch = GetScratchEx(&job->arena);
            P_Image image = {0};
            if (P_LoadFileThumbnail(scratch.arena, input->path, input->folder ? P_FileKind_Directory : P_FileKind_File, Prog_PreviewSize, &image)) {
                result->pixels = Prog_ResizeIcon(&job->arena, image, Prog_PreviewSize);
            }
            ReleaseScratch(scratch);
        }
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Create) {
        Prog_Created *result = ArenaPushStruct(&job->arena, Prog_Created);
        result->error = P_CreateItem(&job->arena, input->path, input->name, input->folder, &result->name);
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Places) {
        Prog_Places *result = ArenaPushStruct(&job->arena, Prog_Places);
        ProfileScope("ListPlaces") {
            result->places = P_UserPlaces(&job->arena);
            result->drives = P_Drives(&job->arena);
        }
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Save) {
        P_FileError *error = ArenaPushStruct(&job->arena, P_FileError);
        *error = P_WriteFile(&job->arena, input->path, input->name);
        job->result = error;
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
    result->expanded = input->expanded;
    P_DirectoryResult listing = {0};
    ProfileScope("EnumerateDirectory") listing = P_EnumerateDirectory(&job->arena, input->path, &job->cancelToken);
    result->error = (P_FileError){ listing.errorCode, listing.errorString };
    result->cancelled = listing.cancelled;
    if (result->error.code || result->cancelled) return;
    P_FileArray files = listing.files;
    if (input->expanded) {
        // Walks every subfolder breadth first; links are not followed and unreadable folders are skipped.
        // ponytail: hands back one result at the end; stream chunks like the measure job if big trees feel slow.
        ProfileScope("ExpandDirectory") {
            P_FileArray all = {0};
            for (u64 i = 0; i < files.header.count; ++i) ArrayPush(&job->arena, all, files.v[i]);
            for (u64 i = 0; i < all.header.count; ++i) {
                if (P_IsCancelled(&job->cancelToken)) { result->cancelled = true; return; }
                P_File folder = all.v[i];
                if (folder.kind != P_FileKind_Directory || folder.symbolicLink || (folder.hidden && !input->showHidden)) continue;
                P_DirectoryResult inside = P_EnumerateDirectory(&job->arena, Prog_JoinPath(&job->arena, input->path, folder.name), &job->cancelToken);
                for (u64 j = 0; j < inside.files.header.count; ++j) {
                    P_File file = inside.files.v[j];
                    file.name = StrFormat(&job->arena, "%S/%S", folder.name, file.name);
                    ArrayPush(&job->arena, all, file);
                }
            }
            files = all;
        }
    }
    result->entries = ArenaPushArray(&job->arena, files.header.count, Prog_Entry);
    // Type names repeat per type, so each is asked of the system once per listing.
    struct { MapHeader header; struct { String key; String value; } *v; } types = {0};
    for (u64 i = 0; i < files.header.count; ++i) {
        if (P_IsCancelled(&job->cancelToken)) { result->cancelled = true; return; }
        P_File file = files.v[i];
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
    if (input->expanded) {
        // The walk saw everything below each folder, so folder totals come from the listing instead of measure jobs.
        // Hidden items count only when they are shown.
        struct { MapHeader header; struct { String key; u64 value; } *v; } folders = {0};
        for (u64 i = 0; i < result->count; ++i) {
            Prog_Entry *entry = &result->entries[i];
            if (entry->file.kind != P_FileKind_Directory) continue;
            entry->measured = true;
            MapsPut(&job->arena, folders, entry->file.name, i + 1);
        }
        for (u64 i = 0; i < result->count; ++i) {
            const Prog_Entry *entry = &result->entries[i];
            for (String parent = Prog_EntryParent(entry->file.name); parent.length; parent = Prog_EntryParent(parent)) {
                u64 at = MapsGet(folders, u64, parent);
                if (!at) break;
                Prog_Entry *owner = &result->entries[at - 1];
                if (entry->file.kind == P_FileKind_Directory) {
                    ++owner->folders;
                } else {
                    ++owner->files;
                    owner->bytes += entry->file.size;
                }
            }
        }
    }
    Prog_Entry *buffer = ArenaPushArray(&job->arena, result->count, Prog_Entry);
    Prog_Entry *sorted = Prog__SortEntries(result->entries, buffer, result->count, input->sort, input->descending, &job->cancelToken);
    if (sorted) result->entries = sorted;
    else result->cancelled = true;
}

// Measures the shown folder's subfolders that have no totals yet, one job at a time.
function void Prog__MeasureFolders(Prog_State *state)
{
    if (state->measureJob) return;
    u64 count = 0;
    for (u64 i = 0; i < state->directory.count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        count += entry->file.kind == P_FileKind_Directory && (!entry->measured || entry->remeasure);
    }
    if (!count) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Measure;
    input->path = StrCopy(&job->arena, state->directory.path);
    input->listing = state->listing;
    input->names = ArenaPushArray(&job->arena, count, String);
    input->rows = ArenaPushArray(&job->arena, count, u64);
    for (u64 i = 0; i < state->directory.count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        if (entry->file.kind != P_FileKind_Directory || (entry->measured && !entry->remeasure)) continue;
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
    input->panel = Prog__PanelId(state);
    input->path = StrCopy(&job->arena, path);
    input->generation = ++state->generation;
    input->sort = state->sort;
    input->descending = state->descending;
    // Another folder starts flat unless the filter is pinned.
    if (!StrIsEqual(path, state->directory.path) && !state->filterSticky) state->expanded = false;
    input->showHidden = state->showHidden;
    input->expanded = state->expanded;
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

function void Prog__EnsureTab(Prog_State *state);
function void Prog__EnsurePanel(Prog_State *state);
function void Prog__EndRename(Prog_State *state, b32 commit);

// Bookmarks and recents, one path per line in the config folder.
#define Prog_BookmarksFile S("Bookmarks.txt")
#define Prog_RecentsFile   S("Recents.txt")
#define Prog_MaxRecents    20

// Replaces `list` with copies of the `count` strings at `items`, in a fresh arena.
function void Prog__SetList(Arena *arena, ArrayStr *list, const String *items, u64 count)
{
    Arena fresh = {0};
    ArrayStr result = {0};
    for (u64 i = 0; i < count; ++i) ArrayPush(&fresh, result, StrCopy(&fresh, items[i]));
    ArenaRelease(arena);
    *arena = fresh;
    *list = result;
}

function String Prog__ListText(Arena *arena, ArrayStr list)
{
    StrBuilder text = {0};
    for (u64 i = 0; i < list.header.count; ++i) {
        StrBuildStr(arena, &text, list.v[i]);
        StrBuildChar(arena, &text, '\n');
    }
    return text.buffer;
}

function void Prog__SaveList(Prog_State *state, String file, ArrayStr list)
{
    if (!state->configDirectory.length) return;
    u32 which = StrIsEqual(file, Prog_RecentsFile) ? 1 : 0;
    if (state->saving[which]) {
        state->saveAgain[which] = true;
        return;
    }
    state->saving[which] = true;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Save;
    input->path = Prog_JoinPath(&job->arena, state->configDirectory, file);
    input->name = Prog__ListText(&job->arena, list);
    job->input = input;
    P_JobSubmit(job);
}

function void Prog__LoadList(Prog_State *state, String file, Arena *arena, ArrayStr *list, b32 *found)
{
    Scratch scratch = GetScratch();
    String text = {0};
    *found = P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, file), &text);
    ArrayStr lines = StrSplit(scratch.arena, text, '\n');
    u64 kept = 0;
    for (u64 i = 0; i < lines.header.count; ++i) {
        String line = lines.v[i];
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        if (line.length) lines.v[kept++] = line;
    }
    Prog__SetList(arena, list, lines.v, kept);
    ReleaseScratch(scratch);
}

// Loads the bookmarks and recents saved in `configDirectory`, which keeps them from then on.
// Without a bookmarks file, the user's desktop, documents, downloads and pictures start as bookmarks.
function void Prog_LoadSettings(Prog_State *state, String configDirectory)
{
    ArenaClear(&state->settingsArena);
    state->configDirectory = StrCopy(&state->settingsArena, configDirectory);
    if (!configDirectory.length) return;
    b32 found = false;
    Prog__LoadList(state, Prog_BookmarksFile, &state->bookmarkArena, &state->bookmarks, &found);
    if (!found) {
        Scratch scratch = GetScratch();
        P_ArrayPlace places = P_UserPlaces(scratch.arena);
        String *paths = ArenaPushArray(scratch.arena, places.header.count + 1, String);
        u64 count = 0;
        for (u64 i = 0; i < places.header.count; ++i) {
            String name = places.v[i].name;
            if (StrIsEqual(name, S("Desktop")) || StrIsEqual(name, S("Documents")) || StrIsEqual(name, S("Downloads")) || StrIsEqual(name, S("Pictures"))) paths[count++] = places.v[i].path;
        }
        Prog__SetList(&state->bookmarkArena, &state->bookmarks, paths, count);
        ReleaseScratch(scratch);
    }
    Prog__LoadList(state, Prog_RecentsFile, &state->recentArena, &state->recents, &found);
    Prog_ReloadSettingsFile(state);
    P_DirectoryWatchStop(&state->configWatch);
    P_DirectoryWatchStart(&state->configWatch, configDirectory, state->jobs.wake, state->jobs.wakeUser);
}

// Settings.txt, or the defaults without it.
function void Prog_ReloadSettingsFile(Prog_State *state)
{
    Scratch scratch = GetScratch();
    String text = {0};
    if (state->configDirectory.length) P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, S("Settings.txt")), &text);
    Prog_ApplySettings(state, text);
    ReleaseScratch(scratch);
}

function b32 Prog_IsBookmarked(const Prog_State *state, String path)
{
    for (u64 i = 0; i < state->bookmarks.header.count; ++i) {
        if (StrIsEqual(state->bookmarks.v[i], path)) return true;
    }
    return false;
}

// Adds `path` at the end of the bookmarks, or removes it when it is one.
function void Prog_ToggleBookmark(Prog_State *state, String path)
{
    if (!path.length) return;
    Scratch scratch = GetScratch();
    u64 count = state->bookmarks.header.count;
    String *paths = ArenaPushArray(scratch.arena, count + 1, String);
    u64 kept = 0;
    for (u64 i = 0; i < count; ++i) {
        if (!StrIsEqual(state->bookmarks.v[i], path)) paths[kept++] = state->bookmarks.v[i];
    }
    if (kept == count) paths[kept++] = path;
    Prog__SetList(&state->bookmarkArena, &state->bookmarks, paths, kept);
    ReleaseScratch(scratch);
    Prog__SaveList(state, Prog_BookmarksFile, state->bookmarks);
}

// Puts `paths` into the bookmarks before position `at`; ones already there move.
function void Prog_PinBookmarks(Prog_State *state, const ArrayStr *paths, u64 at)
{
    if (!paths->header.count) return;
    Scratch scratch = GetScratch();
    u64 count = state->bookmarks.header.count;
    String *result = ArenaPushArray(scratch.arena, count + paths->header.count, String);
    u64 kept = 0;
    at = Min(at, count);
    for (u64 i = 0; i <= count; ++i) {
        if (i == at) {
            for (u64 j = 0; j < paths->header.count; ++j) result[kept++] = paths->v[j];
        }
        if (i == count) break;
        b32 moving = false;
        for (u64 j = 0; j < paths->header.count && !moving; ++j) moving = StrIsEqual(state->bookmarks.v[i], paths->v[j]);
        if (!moving) result[kept++] = state->bookmarks.v[i];
    }
    Prog__SetList(&state->bookmarkArena, &state->bookmarks, result, kept);
    ReleaseScratch(scratch);
    Prog__SaveList(state, Prog_BookmarksFile, state->bookmarks);
}

// Puts `path` first among the recents.
function void Prog__AddRecent(Prog_State *state, String path)
{
    if (state->recents.header.count && StrIsEqual(state->recents.v[0], path)) return;
    Scratch scratch = GetScratch();
    String *paths = ArenaPushArray(scratch.arena, Prog_MaxRecents, String);
    u64 count = 0;
    paths[count++] = path;
    for (u64 i = 0; i < state->recents.header.count && count < Prog_MaxRecents; ++i) {
        if (!StrIsEqual(state->recents.v[i], path)) paths[count++] = state->recents.v[i];
    }
    Prog__SetList(&state->recentArena, &state->recents, paths, count);
    ReleaseScratch(scratch);
    Prog__SaveList(state, Prog_RecentsFile, state->recents);
}

function b32 Prog_GroupOpen(const Prog_State *state, Prog_Group group)
{
    b32 open = group == Prog_Group_Bookmarks || group == Prog_Group_Storage;
    return open != (state->groupToggled[group] != 0);
}

function b32 Prog_Init(Prog_State *state, String path, P_JobWakeCallback *wake, void *user)
{
    if (!P_JobsInit(&state->jobs, &state->jobsArena, 0, wake, user)) {
        ArenaRelease(&state->jobsArena);
        return false;
    }
    Prog__EnsureTab(state);
    Arena temporary = {0};
    String absolute = P_AbsolutePath(&temporary, path);
    if (absolute.length) Prog_RequestDirectory(state, absolute, true, 0);
    else state->error = S("Cannot resolve the initial directory");
    ArenaRelease(&temporary);
    return true;
}

// `{0}` state has one tab: the shown one.
function void Prog__EnsureTab(Prog_State *state)
{
    Prog__EnsurePanel(state);
    if (!state->tabs) state->tabs = ArenaPushArray(&state->tabArena, Prog_MaxTabs, Prog_Tab);
    if (state->tabCount) return;
    state->tabCount = 1;
    state->tabs[0].id = ++state->nextTabId;
}

// `{0}` state has one panel, current and active, filling the whole layout.
function void Prog__EnsurePanel(Prog_State *state)
{
    if (state->panelCount) return;
    state->panelCount = 1;
    state->panels[0].id = ++state->nextPanelId;
    state->currentPanel = state->activePanel = 0;
    state->splits[0] = (Prog_Split){ .kind = Prog_SplitKind_Panel, .panel = state->panels[0].id };
}

function u64 Prog__PanelId(const Prog_State *state)
{
    return state->panelCount ? state->panels[state->currentPanel].id : 0;
}

function u32 Prog_PanelIndex(const Prog_State *state, u64 id)
{
    for (u32 i = 0; i < state->panelCount; ++i) {
        if (state->panels[i].id == id) return i;
    }
    return UINT32_MAX;
}

function void Prog__SwapPanel(Prog_State *state, Prog_Panel *panel)
{
#define Prog__SwapField(type, name) { type swap = state->name; state->name = panel->name; panel->name = swap; }
    Prog_TabFields(Prog__SwapField)
    Prog_PanelFields(Prog__SwapField)
#undef Prog__SwapField
}

// Makes panel `index` current: its fields move into Prog_State and the current one's go back to its slot.
// Other panels are current only briefly, to build their UI or take their jobs' results; the active one is current between frames.
function void Prog__UsePanel(Prog_State *state, u32 index)
{
    Prog__EnsurePanel(state);
    if (index >= state->panelCount || index == state->currentPanel) return;
    Prog__SwapPanel(state, &state->panels[state->currentPanel]);
    Prog__SwapPanel(state, &state->panels[index]);
    state->currentPanel = index;
}

// Gives panel `index` the keyboard: a rename commits and a menu closes, since both belong to the panel they were in.
function void Prog__ActivatePanel(Prog_State *state, u32 index)
{
    Prog__EnsurePanel(state);
    if (index >= state->panelCount) return;
    if (index != state->activePanel) {
        if (state->renaming) Prog__EndRename(state, true);
        Prog_CloseMenu(state);
        state->hasHoverRow = false;
    }
    Prog__UsePanel(state, index);
    state->activePanel = index;
}

// Stops and frees what the current panel holds, leaving it empty.
function void Prog__ReleaseCurrentPanel(Prog_State *state)
{
    if (state->directoryJob) P_RequestCancel(&state->directoryJob->cancelToken);
    if (state->measureJob) P_RequestCancel(&state->measureJob->cancelToken);
    P_DirectoryWatchStop(&state->watch);
    ArenaRelease(&state->watch.arena);
    ArenaRelease(&state->directoryArena);
    ArenaRelease(&state->historyArena);
    ArenaRelease(&state->viewArena);
    ArenaRelease(&state->errorArena);
    for (u32 i = 0; i < state->tabCount; ++i) {
        if (i != state->activeTab) Prog__ReleaseTab(&state->tabs[i]);
    }
    ArenaRelease(&state->tabArena);
#define Prog__ZeroField(type, name) state->name = (type){0};
    Prog_TabFields(Prog__ZeroField)
    Prog_PanelFields(Prog__ZeroField)
#undef Prog__ZeroField
}

function u32 Prog__SplitOf(const Prog_State *state, u64 panel)
{
    for (u32 i = 0; i < Prog_MaxSplits; ++i) {
        if (state->splits[i].kind == Prog_SplitKind_Panel && state->splits[i].panel == panel) return i;
    }
    return UINT32_MAX;
}

function u32 Prog__FreeSplit(const Prog_State *state)
{
    for (u32 i = 1; i < Prog_MaxSplits; ++i) {
        if (state->splits[i].kind == Prog_SplitKind_None) return i;
    }
    return UINT32_MAX;
}

// The first panel of the subtree at `node`, in reading order.
function u64 Prog__FirstPanelOf(const Prog_State *state, u32 node)
{
    while (node < Prog_MaxSplits && state->splits[node].kind != Prog_SplitKind_Panel && state->splits[node].kind != Prog_SplitKind_None) node = state->splits[node].first;
    return node < Prog_MaxSplits ? state->splits[node].panel : 0;
}

// Splits panel `index` in two along `kind`, the new panel `before` it or after it, and makes the new one, which shows `path`, active.
// Returns the new panel's index, or UINT32_MAX when there is no room.
function u32 Prog_SplitPanel(Prog_State *state, u32 index, Prog_SplitKind kind, b32 before, String path)
{
    Prog__EnsureTab(state);
    if (state->panelCount >= Prog_MaxPanels || index >= state->panelCount || !path.length) return UINT32_MAX;
    u32 leaf = Prog__SplitOf(state, state->panels[index].id);
    u32 kept = Prog__FreeSplit(state);
    if (leaf == UINT32_MAX || kept == UINT32_MAX) return UINT32_MAX;
    state->splits[kept] = state->splits[leaf];
    u32 added = Prog__FreeSplit(state);
    if (added == UINT32_MAX) {
        state->splits[kept] = (Prog_Split){0};
        return UINT32_MAX;
    }
    u32 at = state->panelCount++;
    // The new panel starts with the order and hidden-item choice of the one it splits.
    b32 current = index == state->currentPanel;
    state->panels[at] = (Prog_Panel){
        .id = ++state->nextPanelId,
        .sort = current ? state->sort : state->panels[index].sort,
        .descending = current ? state->descending : state->panels[index].descending,
        .showHidden = current ? state->showHidden : state->panels[index].showHidden,
        .zoom = current ? state->zoom : state->panels[index].zoom,
    };
    state->splits[added] = (Prog_Split){ .kind = Prog_SplitKind_Panel, .panel = state->panels[at].id };
    state->splits[leaf] = (Prog_Split){ .kind = (u8)kind, .first = (u8)(before ? added : kept), .second = (u8)(before ? kept : added), .ratio = 0.5f };
    Prog__ActivatePanel(state, at);
    Prog__EnsureTab(state);
    Prog_RequestDirectory(state, path, true, 0);
    return at;
}

// Closes panel `index`, whose neighbor in the layout takes its space; the last panel stays.
function void Prog_ClosePanel(Prog_State *state, u32 index)
{
    Prog__EnsurePanel(state);
    if (state->panelCount <= 1 || index >= state->panelCount) return;
    u64 id = state->panels[index].id;
    u32 leaf = Prog__SplitOf(state, id), parent = UINT32_MAX;
    for (u32 i = 0; i < Prog_MaxSplits && leaf != UINT32_MAX; ++i) {
        Prog_Split *split = &state->splits[i];
        if ((split->kind == Prog_SplitKind_Horizontal || split->kind == Prog_SplitKind_Vertical) && (split->first == leaf || split->second == leaf)) parent = i;
    }
    if (parent == UINT32_MAX) return;
    // The sibling's subtree moves up into the parent's node.
    u32 sibling = state->splits[parent].first == leaf ? state->splits[parent].second : state->splits[parent].first;
    state->splits[parent] = state->splits[sibling];
    state->splits[sibling] = state->splits[leaf] = (Prog_Split){0};
    u32 next = Prog_PanelIndex(state, Prog__FirstPanelOf(state, parent));
    // Released while current, so that its fields are live; the slot left behind is empty.
    if (state->renaming && index == state->activePanel) Prog__EndRename(state, false);
    Prog_CloseMenu(state);
    Prog__UsePanel(state, index);
    Prog__ReleaseCurrentPanel(state);
    Prog__UsePanel(state, next);
    MemMove(&state->panels[index], &state->panels[index + 1], (state->panelCount - index - 1) * sizeof(Prog_Panel));
    --state->panelCount;
    state->panels[state->panelCount] = (Prog_Panel){0};
    if (state->currentPanel > index) --state->currentPanel;
    state->activePanel = state->currentPanel;
}

// Moves, or with `copy` copies, a tab to where it was dropped; the tab takes its folder and history there, and the place it lands becomes active.
// ponytail: the selection and scroll start over; carry the whole Prog_Tab across when that matters.
function void Prog_DropTab(Prog_State *state, Prog_TabDropTarget drop)
{
    u32 from = Prog_PanelIndex(state, drop.fromPanel), into = Prog_PanelIndex(state, drop.panel);
    if (from == UINT32_MAX) return;
    Prog__ActivatePanel(state, from);
    if (drop.fromSlot >= state->tabCount) return;
    String path = StrCopy(&state->frameArena, Prog_TabPath(state, drop.fromSlot));
    if (!path.length) return;
    b32 shown = drop.fromSlot == state->activeTab;
    ArrayStr history = shown ? state->history : state->tabs[drop.fromSlot].history;
    u64 historyAt = shown ? state->historyAt : state->tabs[drop.fromSlot].historyAt;
    String *saved = ArenaPushArray(&state->frameArena, history.header.count + 1, String);
    for (u64 i = 0; i < history.header.count; ++i) saved[i] = StrCopy(&state->frameArena, history.v[i]);
    // A tab that has not listed its folder yet has no history of it.
    if (historyAt >= history.header.count || !StrIsEqual(saved[historyAt], path)) {
        saved[0] = path;
        history.header.count = 1;
        historyAt = 0;
    }
    b32 alone = state->tabCount == 1;
    switch (drop.kind) {
        case Prog_TabDrop_Strip:
        case Prog_TabDrop_Into: {
            if (into == UINT32_MAX) return;
            if (into == from && !drop.copy) {
                // Within its strip a tab only changes place.
                if (drop.kind == Prog_TabDrop_Strip) Prog_MoveTab(state, drop.fromSlot, drop.slot > drop.fromSlot ? drop.slot - 1 : drop.slot);
                return;
            }
            Prog__ActivatePanel(state, into);
            u32 at = Min(drop.kind == Prog_TabDrop_Into ? state->tabCount : drop.slot, state->tabCount);
            u32 count = state->tabCount;
            Prog_OpenTab(state, path, at, false);
            if (state->tabCount == count) return;
            Prog_Tab *tab = &state->tabs[at];
            Prog__SetList(&tab->historyArena, &tab->history, saved, history.header.count);
            tab->historyAt = historyAt;
            Prog_SelectTab(state, at);
        } break;
        case Prog_TabDrop_Left:
        case Prog_TabDrop_Right:
        case Prog_TabDrop_Top:
        case Prog_TabDrop_Bottom: {
            // A panel's only tab moving beside the panel would only leave it where it is.
            if (into == UINT32_MAX || (into == from && alone && !drop.copy)) return;
            b32 across = drop.kind == Prog_TabDrop_Left || drop.kind == Prog_TabDrop_Right;
            b32 before = drop.kind == Prog_TabDrop_Left || drop.kind == Prog_TabDrop_Top;
            if (Prog_SplitPanel(state, into, across ? Prog_SplitKind_Horizontal : Prog_SplitKind_Vertical, before, path) == UINT32_MAX) return;
            // The listing on its way finds `path` already in the history, where it adds nothing.
            Prog__SetList(&state->historyArena, &state->history, saved, history.header.count);
            state->historyAt = historyAt;
        } break;
        case Prog_TabDrop_Window: {
            P_FileError error = P_OpenWindow(&state->frameArena, path);
            if (error.code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "New window: %S", error.message);
                return;
            }
        } break;
        default: return;
    }
    if (drop.copy) return;
    // The tab leaves where it was; its panel closes when it was the only one there.
    u64 landed = Prog__PanelId(state);
    from = Prog_PanelIndex(state, drop.fromPanel);
    if (from == UINT32_MAX) return;
    Prog__ActivatePanel(state, from);
    Prog_CloseTab(state, drop.fromSlot);
    u32 back = Prog_PanelIndex(state, landed);
    if (back != UINT32_MAX) Prog__ActivatePanel(state, back);
}

// Activates the panel beside the active one toward (`dx`, `dy`) in the last layout: the nearest one past that edge that overlaps it across, the most overlapping among equally near ones.
function void Prog_FocusPanelToward(Prog_State *state, i32 dx, i32 dy)
{
    Prog__ActivatePanel(state, state->activePanel);
    R_Rect from = state->panelRect;
    u32 best = UINT32_MAX;
    f32 bestGap = 0, bestOverlap = 0;
    for (u32 i = 0; i < state->panelCount; ++i) {
        if (i == state->currentPanel) continue;
        R_Rect r = state->panels[i].panelRect;
        f32 gap = dx > 0 ? r.x0 - from.x1 : dx < 0 ? from.x0 - r.x1 : dy > 0 ? r.y0 - from.y1 : from.y0 - r.y1;
        f32 overlap = dx ? MinF32(r.y1, from.y1) - MaxF32(r.y0, from.y0) : MinF32(r.x1, from.x1) - MaxF32(r.x0, from.x0);
        if (gap < -1 || overlap <= 0) continue;
        if (best == UINT32_MAX || gap < bestGap - 1 || (gap <= bestGap + 1 && overlap > bestOverlap)) {
            best = i;
            bestGap = gap;
            bestOverlap = overlap;
        }
    }
    if (best != UINT32_MAX) Prog__ActivatePanel(state, best);
}

// Records a file operation for Undo; strings are copied.
function void Prog_PushUndo(Prog_State *state, Prog_UndoKind kind, const String *from, const String *to, u64 count)
{
    if (!count) return;
    if (state->undo.header.count >= Prog_MaxUndo) {
        MemMove(&state->undo.v[0], &state->undo.v[1], (state->undo.header.count - 1) * sizeof(Prog_Undo));
        --state->undo.header.count;
    }
    Prog_Undo undo = { .kind = (u8)kind, .count = count, .to = ArenaPushArray(&state->undoArena, count, String) };
    if (from) undo.from = ArenaPushArray(&state->undoArena, count, String);
    for (u64 i = 0; i < count; ++i) {
        undo.to[i] = StrCopy(&state->undoArena, to[i]);
        if (from) undo.from[i] = StrCopy(&state->undoArena, from[i]);
    }
    ArrayPush(&state->undoArena, state->undo, undo);
}

// Reverses the newest recorded file operation in a job.
function void Prog_UndoLast(Prog_State *state)
{
    ArenaClear(&state->errorArena);
    state->error = (String){0};
    if (!state->undo.header.count) {
        state->error = S("Nothing to undo");
        return;
    }
    Prog_Undo undo = state->undo.v[--state->undo.header.count];
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Undo;
    input->panel = Prog__PanelId(state);
    input->undo = (Prog_Undo){ .kind = undo.kind, .count = undo.count, .to = ArenaPushArray(&job->arena, undo.count, String) };
    if (undo.from) input->undo.from = ArenaPushArray(&job->arena, undo.count, String);
    for (u64 i = 0; i < undo.count; ++i) {
        input->undo.to[i] = StrCopy(&job->arena, undo.to[i]);
        if (undo.from) input->undo.from[i] = StrCopy(&job->arena, undo.from[i]);
    }
    if (!state->undo.header.count) {
        ArenaClear(&state->undoArena);
        state->undo = (Prog_ArrayUndo){0};
    }
    job->input = input;
    P_JobSubmit(job);
}

// The window's panels and their tabs, kept in the config folder between runs.
#define Prog_SessionFile S("Session.txt")

// The digits at the start of `*text`, which moves past them and one space.
function b32 Prog__TakeNumber(String *text, u64 *value)
{
    u64 at = 0, result = 0;
    while (at < text->length && text->value[at] >= '0' && text->value[at] <= '9' && at < 19) result = result * 10 + (u64)(text->value[at++] - '0');
    if (!at) return false;
    *value = result;
    *text = StrFrom(*text, Min(at + 1, text->length));
    return true;
}

// One record per line: the layout's split nodes, then each panel and its tabs; a tab's path ends its line, so it may hold spaces.
function String Prog_SessionText(Arena *arena, Prog_State *state)
{
    Prog__EnsureTab(state);
    StrBuilder text = {0};
    u32 active = state->activePanel;
    StrBuildStr(arena, &text, StrFormat(arena, "session 1\nactive %u\nsidebar %u\npreview %u\n", active, state->sidebarHidden ? 1u : 0u, state->previewShown ? 1u : 0u));
    for (u32 i = 0; i < Prog_MaxSplits; ++i) {
        const Prog_Split *split = &state->splits[i];
        if (split->kind == Prog_SplitKind_None) continue;
        u32 panel = split->kind == Prog_SplitKind_Panel ? Prog_PanelIndex(state, split->panel) : 0;
        StrBuildStr(arena, &text, StrFormat(arena, "split %u %u %u %u %u %u\n", i, (u32)split->kind, (u32)split->first, (u32)split->second, panel, (u32)(split->ratio * 1e6f)));
    }
    for (u32 p = 0; p < state->panelCount; ++p) {
        Prog__UsePanel(state, p);
        StrBuildStr(arena, &text, StrFormat(arena, "panel %u %u %u\n", p, state->zoom, state->activeTab));
        for (u32 t = 0; t < state->tabCount; ++t) StrBuildStr(arena, &text, StrFormat(arena, "tab %u %S\n", p, Prog_TabPath(state, t)));
    }
    Prog__UsePanel(state, active);
    return text.buffer;
}

// Rebuilds the panels and tabs of `text` in a state that has just started with one panel and one tab; returns false, changing nothing, when `text` does not describe a whole layout.
function b32 Prog_RestoreSession(Prog_State *state, String text)
{
    Prog__EnsureTab(state);
    if (state->panelCount != 1 || state->tabCount != 1) return false;
    Scratch scratch = GetScratch();
    Prog_Split splits[Prog_MaxSplits] = {0};
    u32 splitPanel[Prog_MaxSplits] = {0};
    struct { u32 zoom, activeTab, tabCount; String tabs[Prog_MaxTabs]; } *panels = ArenaPushArray(scratch.arena, Prog_MaxPanels, typeof(*panels));
    u64 panelCount = 0, active = 0, sidebarHidden = 0, previewShown = 0;
    b32 valid = false;
    ArrayStr lines = StrSplit(scratch.arena, text, '\n');
    for (u64 l = 0; l < lines.header.count; ++l) {
        String line = lines.v[l];
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        Index space = StrIndexChar(line, ' ', SSF_CountOnFail);
        String word = StrFromTo(line, 0, (i64)space.value), rest = StrFrom(line, Min(space.value + 1, line.length));
        u64 a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        if (StrIsEqual(word, S("session"))) valid = Prog__TakeNumber(&rest, &a) && a == 1;
        else if (StrIsEqual(word, S("active"))) Prog__TakeNumber(&rest, &active);
        else if (StrIsEqual(word, S("sidebar"))) Prog__TakeNumber(&rest, &sidebarHidden);
        else if (StrIsEqual(word, S("preview"))) Prog__TakeNumber(&rest, &previewShown);
        else if (StrIsEqual(word, S("split"))) {
            if (!(Prog__TakeNumber(&rest, &a) && Prog__TakeNumber(&rest, &b) && Prog__TakeNumber(&rest, &c) && Prog__TakeNumber(&rest, &d) && Prog__TakeNumber(&rest, &e) && Prog__TakeNumber(&rest, &f))
                || a >= Prog_MaxSplits || b >= Prog_SplitKind_Count || c >= Prog_MaxSplits || d >= Prog_MaxSplits || e >= Prog_MaxPanels) valid = false;
            else {
                splits[a] = (Prog_Split){ .kind = (u8)b, .first = (u8)c, .second = (u8)d, .ratio = (f32)f / 1e6f };
                splitPanel[a] = (u32)e;
            }
        } else if (StrIsEqual(word, S("panel"))) {
            if (!(Prog__TakeNumber(&rest, &a) && Prog__TakeNumber(&rest, &b) && Prog__TakeNumber(&rest, &c)) || a != panelCount || a >= Prog_MaxPanels) valid = false;
            else panels[panelCount++] = (typeof(*panels)){ .zoom = (u32)Min(b, (u64)Prog_ZoomLevels - 1), .activeTab = (u32)c };
        } else if (StrIsEqual(word, S("tab"))) {
            if (!Prog__TakeNumber(&rest, &a) || a + 1 != panelCount) valid = false;
            else if (rest.length && panels[a].tabCount < Prog_MaxTabs) panels[a].tabs[panels[a].tabCount++] = rest;
        }
    }
    // The tree from the root must reach every panel once, each with a tab.
    u32 reached[Prog_MaxPanels] = {0};
    u32 stack[Prog_MaxSplits * 2], depth = 0, visits = 0;
    if (valid && splits[0].kind != Prog_SplitKind_None) stack[depth++] = 0;
    else valid = false;
    while (valid && depth) {
        u32 node = stack[--depth];
        const Prog_Split *split = &splits[node];
        if (++visits > Prog_MaxSplits) valid = false;
        else if (split->kind == Prog_SplitKind_Panel) valid = splitPanel[node] < panelCount && !reached[splitPanel[node]]++;
        else if (split->kind == Prog_SplitKind_None || split->first == node || split->second == node) valid = false;
        else {
            stack[depth++] = split->first;
            stack[depth++] = split->second;
        }
    }
    for (u32 p = 0; valid && p < panelCount; ++p) valid = reached[p] == 1 && panels[p].tabCount;
    if (valid) {
        state->sidebarHidden = sidebarHidden != 0;
        state->previewShown = previewShown != 0;
        for (u32 p = 0; p < panelCount; ++p) {
            if (p) {
                state->panels[p] = (Prog_Panel){ .id = ++state->nextPanelId };
                state->panelCount = p + 1;
            }
            Prog__ActivatePanel(state, p);
            Prog__EnsureTab(state);
            state->zoom = panels[p].zoom;
            // The first tab drops whatever it was about to list and starts over in its folder.
            ArenaClear(&state->directoryArena);
            state->directory = (Prog_Directory){0};
            ArenaClear(&state->historyArena);
            state->history = (ArrayStr){0};
            ArrayPush(&state->historyArena, state->history, StrCopy(&state->historyArena, panels[p].tabs[0]));
            state->historyAt = 0;
            Prog_RequestDirectory(state, panels[p].tabs[0], false, 0);
            for (u32 t = 1; t < panels[p].tabCount; ++t) Prog_OpenTab(state, panels[p].tabs[t], t, false);
            if (panels[p].activeTab < state->tabCount) Prog_SelectTab(state, panels[p].activeTab);
        }
        for (u32 i = 0; i < Prog_MaxSplits; ++i) {
            state->splits[i] = splits[i];
            if (splits[i].kind == Prog_SplitKind_Panel) state->splits[i].panel = state->panels[splitPanel[i]].id;
        }
        Prog__ActivatePanel(state, active < panelCount ? (u32)active : 0);
    }
    ReleaseScratch(scratch);
    return valid;
}

function void Prog_LoadSession(Prog_State *state)
{
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    String text = {0};
    if (P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, Prog_SessionFile), &text)) Prog_RestoreSession(state, text);
    ReleaseScratch(scratch);
}

function b32 Prog__AnyMeasuring(const Prog_State *state)
{
    b32 result = state->measureJob != NULL;
    for (u32 i = 0; i < state->panelCount; ++i) result |= i != state->currentPanel && state->panels[i].measureJob != NULL;
    return result;
}

function void Prog__SwapTab(Prog_State *state, Prog_Tab *tab)
{
#define Prog__SwapField(type, name) { type swap = state->name; state->name = tab->name; tab->name = swap; }
    Prog_TabFields(Prog__SwapField)
#undef Prog__SwapField
}

function void Prog__ReleaseTab(Prog_Tab *tab)
{
    ArenaRelease(&tab->directoryArena);
    ArenaRelease(&tab->historyArena);
    ArenaRelease(&tab->viewArena);
    *tab = (Prog_Tab){0};
}

// The folder tab `index` shows, or is about to show when it has not listed one yet.
function String Prog_TabPath(const Prog_State *state, u32 index)
{
    b32 shown = index == state->activeTab;
    const Prog_Directory *directory = shown ? &state->directory : &state->tabs[index].directory;
    const ArrayStr *history = shown ? &state->history : &state->tabs[index].history;
    u64 at = shown ? state->historyAt : state->tabs[index].historyAt;
    if (directory->path.length) return directory->path;
    if (shown && state->directoryJob) return ((Prog_JobInput *)state->directoryJob->input)->path;
    return at < history->header.count ? history->v[at] : (String){0};
}

// Opens `path` in a new tab at slot `at`.
function void Prog_OpenTab(Prog_State *state, String path, u32 at, b32 activate)
{
    Prog__EnsureTab(state);
    if (state->tabCount >= Prog_MaxTabs || !path.length) return;
    at = Min(at, state->tabCount);
    MemMove(&state->tabs[at + 1], &state->tabs[at], (state->tabCount - at) * sizeof(Prog_Tab));
    ++state->tabCount;
    if (at <= state->activeTab) ++state->activeTab;
    Prog_Tab *tab = &state->tabs[at];
    *tab = (Prog_Tab){ .id = ++state->nextTabId, .sort = state->sort, .descending = state->descending, .showHidden = state->showHidden };
    // The tab lists its folder when it is first shown.
    ArrayPush(&tab->historyArena, tab->history, StrCopy(&tab->historyArena, path));
    if (activate) Prog_SelectTab(state, at);
}

function void Prog_SelectTab(Prog_State *state, u32 index)
{
    Prog__EnsureTab(state);
    if (index >= state->tabCount || index == state->activeTab) return;
    // Work tied to the rows of the tab being left stops; a listing on its way is dropped, so that tab keeps the folder it showed.
    Prog_CloseMenu(state);
    state->renaming = state->filtering = false;
    state->hasHoverRow = false;
    if (state->directoryJob) {
        P_RequestCancel(&state->directoryJob->cancelToken);
        state->directoryJob = NULL;
        ++state->generation;
    }
    state->loading = false;
    if (state->measureJob) {
        P_RequestCancel(&state->measureJob->cancelToken);
        state->measureJob = NULL;
    }
    P_DirectoryWatchStop(&state->watch);
    state->watchDirty = false;
    ArenaClear(&state->errorArena);
    state->error = (String){0};

    Prog__SwapTab(state, &state->tabs[state->activeTab]);
    state->activeTab = index;
    Prog__SwapTab(state, &state->tabs[index]);

    // Totals of the old listing are matched by name, as after a refresh.
    ++state->listing;
    state->restoreScroll = true;
    if (state->directory.path.length) {
        // The tab's folder may have changed while it was hidden.
        P_DirectoryWatchStart(&state->watch, state->directory.path, state->jobs.wake, state->jobs.wakeUser);
        Prog_Reload(state);
    } else if (state->historyAt < state->history.header.count) {
        Prog_RequestDirectory(state, state->history.v[state->historyAt], false, state->historyAt);
    }
}

function void Prog_CloseTab(Prog_State *state, u32 index)
{
    Prog__EnsureTab(state);
    // A panel's only tab closes the panel; the last panel's last tab stays, since closing it would close the window.
    if (index < state->tabCount && state->tabCount == 1 && state->panelCount > 1) {
        String path = Prog_TabPath(state, index);
        if (path.length) ArrayPush(&state->closedArena, state->closedTabs, StrCopy(&state->closedArena, path));
        Prog_ClosePanel(state, state->currentPanel);
        return;
    }
    if (index >= state->tabCount || state->tabCount == 1) return;
    String path = Prog_TabPath(state, index);
    if (path.length) ArrayPush(&state->closedArena, state->closedTabs, StrCopy(&state->closedArena, path));
    // The neighbor to the right takes over a closed shown tab, else the one to the left.
    if (index == state->activeTab) Prog_SelectTab(state, index + 1 < state->tabCount ? index + 1 : index - 1);
    Prog__ReleaseTab(&state->tabs[index]);
    MemMove(&state->tabs[index], &state->tabs[index + 1], (state->tabCount - index - 1) * sizeof(Prog_Tab));
    --state->tabCount;
    state->tabs[state->tabCount] = (Prog_Tab){0};
    if (state->activeTab > index) --state->activeTab;
}

function void Prog_MoveTab(Prog_State *state, u32 from, u32 to)
{
    Prog__EnsureTab(state);
    if (from >= state->tabCount || to >= state->tabCount || from == to) return;
    Prog_Tab moved = state->tabs[from];
    if (from < to) MemMove(&state->tabs[from], &state->tabs[from + 1], (to - from) * sizeof(Prog_Tab));
    else MemMove(&state->tabs[to + 1], &state->tabs[to], (from - to) * sizeof(Prog_Tab));
    state->tabs[to] = moved;
    u32 active = state->activeTab;
    if (active == from) active = to;
    else if (from < active && active <= to) --active;
    else if (to <= active && active < from) ++active;
    state->activeTab = active;
}

// Coalesces bursts such as a copy or a build into a few reloads.
#define Prog_WatchInterval UINT64_C(100000)

function b32 Prog_TakeCompletions(Prog_State *state)
{
    b32 changed = false;
    u64 now = PlatformNowMicroseconds();
    Prog__EnsurePanel(state);
    u32 active = state->activePanel;
    // Saved settings apply at once.
    if (P_DirectoryWatchTakeChanged(&state->configWatch)) {
        Prog_ReloadSettingsFile(state);
        changed = true;
    }
    // Every panel watches its folder.
    for (u32 panel = 0; panel < state->panelCount; ++panel) {
        Prog__UsePanel(state, panel);
        if (P_DirectoryWatchTakeChanged(&state->watch)) state->watchDirty = true;
        if (state->watchDirty && !state->directoryJob && now - state->watchReloadTime >= Prog_WatchInterval) {
            state->watchDirty = false;
            state->watchReloadTime = now;
            Prog_Reload(state);
        }
        // A deferred reload needs frames until it runs.
        changed |= state->watchDirty;
    }
    Prog__UsePanel(state, active);
    // ponytail: drives that come and go show at the next refresh after some input; watch device changes if that feels slow.
    if (state->placesWanted && !state->placesJob && (!state->placesTime || now - state->placesTime >= Millions(5))) {
        state->placesTime = now;
        P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
        input->panel = Prog__PanelId(state);
        input->kind = Prog_JobKind_Places;
        job->input = input;
        state->placesJob = job;
        P_JobSubmit(job);
    }
    for (P_Job *job; (job = P_JobTakeCompleted(&state->jobs)) != NULL;) {
        Prog_JobInput *input = job->input;
        // Results about a panel's folder go to that panel, and are dropped once it is closed.
        b32 windowWide = input->kind == Prog_JobKind_Icons || input->kind == Prog_JobKind_Places || input->kind == Prog_JobKind_Save || input->kind == Prog_JobKind_WarmMenu || input->kind == Prog_JobKind_Preview;
        u32 owner = windowWide || !input->panel ? state->activePanel : Prog_PanelIndex(state, input->panel);
        if (owner == UINT32_MAX) {
            P_JobRelease(job);
            continue;
        }
        Prog__UsePanel(state, owner);
        if (input->kind == Prog_JobKind_Icons) {
            Prog__TakeIcons(state, job);
            changed = true;
        } else if (input->kind == Prog_JobKind_Preview) {
            // Only the latest one is shown; the texture is made on first use and refilled after.
            const Prog_Preview *preview = job->result;
            if (job == state->previewJob && preview) {
                state->previewJob = NULL;
                state->previewText = StrCopy(&state->previewArena, preview->text);
                if (preview->pixels) {
                    if (!state->previewTexture.u64[0]) state->previewTexture = R_TextureCreateRGBA(Prog_PreviewSize, Prog_PreviewSize, preview->pixels);
                    else R_TextureUpdateRGBA(state->previewTexture, 0, 0, Prog_PreviewSize, Prog_PreviewSize, preview->pixels);
                    state->previewPicture = state->previewTexture.u64[0] != 0;
                }
                changed = true;
            }
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
            } else {
                String from = Prog_JoinPath(&state->frameArena, input->path, input->name), to = Prog_JoinPath(&state->frameArena, input->path, input->newName);
                Prog_PushUndo(state, Prog_UndoKind_Rename, &from, &to, 1);
                // Reloading now, rather than on the watch, selects the new name without waiting.
                String listed = input->prefix.length ? StrFormat(&state->frameArena, "%S/%S", input->prefix, input->newName) : input->newName;
                String folder = input->prefix.length ? Prog_JoinPath(&state->frameArena, state->directory.path, input->prefix) : state->directory.path;
                if (StrIsEqual(folder, input->path) && listed.length <= sizeof(state->selectText)) {
                    MemCopy(state->selectText, listed.value, listed.length);
                    state->selectLength = (u32)listed.length;
                    Prog_Reload(state);
                }
            }
        } else if (input->kind == Prog_JobKind_Transfer || input->kind == Prog_JobKind_Trash || input->kind == Prog_JobKind_Delete || input->kind == Prog_JobKind_Create) {
            changed = true;
            for (u32 i = 0; i < Prog_MaxTransfers; ++i) {
                if (state->transfers[i].job == job) state->transfers[i].job = NULL;
            }
            P_FileError error = input->kind == Prog_JobKind_Create ? (job->result ? ((Prog_Created *)job->result)->error : (P_FileError){0})
                              : job->result ? *(P_FileError *)job->result : (P_FileError){0};
            String verb = input->kind == Prog_JobKind_Trash ? S("Trash") : input->kind == Prog_JobKind_Delete ? S("Delete") : input->kind == Prog_JobKind_Create ? S("Create") : input->move ? S("Move") : S("Copy");
            if (error.code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "%S: %S", verb, error.message);
            } else if (input->kind == Prog_JobKind_Create && StrIsEqual(state->directory.path, input->path)) {
                // The new item is selected and renamed once the listing shows it.
                String name = ((Prog_Created *)job->result)->name;
                if (name.length <= sizeof(state->selectText)) {
                    MemCopy(state->selectText, name.value, name.length);
                    state->selectLength = (u32)name.length;
                    state->selectRename = true;
                }
            }
            // Copies, moves and new items that went through can be undone.
            if (!error.code && input->kind == Prog_JobKind_Transfer && job->result) {
                const Prog_Transferred *result = job->result;
                if (result->placed.header.count == input->pathCount) {
                    Prog_PushUndo(state, input->move ? Prog_UndoKind_Move : Prog_UndoKind_Copy, input->move ? input->paths : NULL, result->placed.v, input->pathCount);
                }
            }
            if (!error.code && input->kind == Prog_JobKind_Create && job->result) {
                String created = Prog_JoinPath(&state->frameArena, input->path, ((Prog_Created *)job->result)->name);
                Prog_PushUndo(state, Prog_UndoKind_Create, NULL, &created, 1);
            }
            // Reloading now shows the result without waiting for the watch.
            if (!error.code) Prog_Reload(state);
        } else if (input->kind == Prog_JobKind_Undo) {
            P_FileError *error = job->result;
            changed = true;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "Undo: %S", error->message);
            }
            Prog_Reload(state);
        } else if (input->kind == Prog_JobKind_Places) {
            if (state->placesJob == job) state->placesJob = NULL;
            if (job->result) {
                Prog_Places *result = job->result;
                ArenaRelease(&state->placesArena);
                state->placesArena = job->arena;
                job->arena = (Arena){0};
                state->places = result->places;
                state->drives = result->drives;
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_Save) {
            P_FileError *error = job->result;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "Save %S: %S", input->path, error->message);
                changed = true;
            }
            b32 recents = StrIsEqual(Prog_EntryLeaf(input->path), Prog_RecentsFile);
            state->saving[recents] = false;
            if (state->saveAgain[recents]) {
                state->saveAgain[recents] = false;
                Prog__SaveList(state, recents ? Prog_RecentsFile : Prog_BookmarksFile, recents ? state->recents : state->bookmarks);
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
                    entry->remeasure = false;
                    entry->folders = result->totals[i].folders;
                    entry->files = result->totals[i].files;
                    entry->bytes = result->totals[i].bytes;
                    // Kept for the next time the folder is shown.
                    if (state->sizes.header.count >= Prog_MaxCachedSizes) {
                        ArenaClear(&state->sizeArena);
                        state->sizes = (Prog_SizeMap){0};
                    }
                    String path = Prog_JoinPath(&state->frameArena, input->path, input->names[i]);
                    P_DirectoryTotals *known = MapsGetPtrNull(state->sizes, path);
                    if (known) *known = result->totals[i];
                    else MapsPut(&state->sizeArena, state->sizes, StrCopy(&state->sizeArena, path), result->totals[i]);
                }
                changed = true;
                Prog__SortByTotals(state);
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
                        } else if (!samePath && entry->file.kind == P_FileKind_Directory && !entry->measured && state->sizes.header.count) {
                            P_DirectoryTotals *known = MapsGetPtrNull(state->sizes, Prog_JoinPath(&state->frameArena, result->path, entry->file.name));
                            if (known) {
                                entry->measured = entry->remeasure = true;
                                entry->folders = known->folders;
                                entry->files = known->files;
                                entry->bytes = known->bytes;
                            }
                        }
                    }
                    if (renameCreated && created != 1 && !pick.length) pickRow = UINT64_MAX;
                    // The filter resets in another folder unless pinned, and gives way to an item that was just created or renamed.
                    if ((!samePath && !state->filterSticky) || (pickRow < result->count && !Prog_FilterMatch(result->entries[pickRow].file.name, Prog_EditText(&state->filter), NULL))) {
                        Prog_ClearFilter(state);
                    }
                    b32 renamePicked = (renameCreated && created == 1 && !pick.length) || (state->selectRename && pick.length && pickRow < result->count);
                    state->selectRename = false;
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
                    if (!samePath) {
                        state->resetScroll = true;
                        Prog__AddRecent(state, result->path);
                    }
                    if (input->addHistory && (!state->history.header.count || !StrIsEqual(state->history.v[state->historyAt], input->path))) {
                        if (state->history.header.count) state->history.header.count = state->historyAt + 1;
                        ArrayPush(&state->historyArena, state->history, StrCopy(&state->historyArena, input->path));
                        state->historyAt = state->history.header.count - 1;
                    } else if (!input->addHistory && state->history.header.count) {
                        state->historyAt = input->historyAt;
                    }
                    // The menu's rows index the listing being replaced; Filey's own menus have none.
                    if (!state->menuCommands) Prog_CloseMenu(state);
                    ArenaRelease(&state->directoryArena);
                    state->directoryArena = job->arena;
                    state->directory = *result;
                    job->arena = (Arena){0};
                    ++state->listing;
                    Prog_ApplyFilter(state, false);
                    Prog__SortByTotals(state);
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
    Prog__UsePanel(state, state->activePanel);
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
        input->panel = Prog__PanelId(state);
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
    Prog_CloseMenu(state);
    // The session is written while the panels still hold it.
    if (state->configDirectory.length && state->panelCount) {
        Scratch scratch = GetScratch();
        P_WriteFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, Prog_SessionFile), Prog_SessionText(scratch.arena, state));
        ReleaseScratch(scratch);
    }
    // Watch threads wake the window, like the workers, so they stop first.
    for (u32 panel = 0; panel < state->panelCount; ++panel) {
        Prog__UsePanel(state, panel);
        Prog__ReleaseCurrentPanel(state);
    }
    ArenaRelease(&state->menuArena);
    P_DirectoryWatchStop(&state->configWatch);
    ArenaRelease(&state->configWatch.arena);
    P_JobsShutdown(&state->jobs);
    // Shutting down drops saves still waiting in the queue, and the workers are done, so the lists are written once more here.
    if (state->configDirectory.length) {
        Scratch scratch = GetScratch();
        ArrayStr lists[2] = { state->bookmarks, state->recents };
        String files[2] = { Prog_BookmarksFile, Prog_RecentsFile };
        for (u32 i = 0; i < 2; ++i) P_WriteFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, files[i]), Prog__ListText(scratch.arena, lists[i]));
        ReleaseScratch(scratch);
    }
    UI_Release(&state->ui);
    ArenaRelease(&state->frameArena);
    ArenaRelease(&state->iconArena);
    ArenaRelease(&state->thumbnailArena);
    ArenaRelease(&state->jobsArena);
    ArenaRelease(&state->closedArena);
    ArenaRelease(&state->confirmArena);
    ArenaRelease(&state->placesArena);
    ArenaRelease(&state->settingsArena);
    ArenaRelease(&state->bookmarkArena);
    ArenaRelease(&state->recentArena);
    ArenaRelease(&state->undoArena);
    ArenaRelease(&state->sizeArena);
    ArenaRelease(&state->previewArena);
    *state = (Prog_State){0};
}
