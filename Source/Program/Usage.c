// The disk usage of a folder: a walk of everything below it in lanes, which keeps each folder's largest files and counts the rest, and the squarified treemap the inspector draws of it.

typedef struct Prog__UsageFolders { ArrayHeader header; Prog_UsageFolder *v; } Prog__UsageFolders;
typedef struct Prog__UsageFiles { ArrayHeader header; Prog_UsageFile *v; } Prog__UsageFiles;
typedef struct Prog__UsageTypes { ArrayHeader header; Prog_UsageType *v; } Prog__UsageTypes;
typedef struct Prog__UsageTypeMap { MapHeader header; struct { String key; u32 value; } *v; } Prog__UsageTypeMap;
typedef struct Prog__UsagePending { String path; u32 folder; } Prog__UsagePending;
typedef struct Prog__UsageStack { ArrayHeader header; Prog__UsagePending *v; } Prog__UsageStack;

typedef struct Prog__UsageWalk {
    Arena *names;  // the result's, which keeps the names
    Arena walk;    // the growing arrays and the paths waiting to be listed, let go once the walk is done
    const P_CancelToken *cancel;
    Prog_UsageProgress *progress;
    P_Mutex mutex;
    P_Condition condition;
    Prog__UsageStack stack;
    u32 busy;
    Prog__UsageFolders folders;
    Prog__UsageFiles files;
    Prog__UsageTypes types;
    Prog__UsageTypeMap typeOf;
} Prog__UsageWalk;

// Sorts `order`, `count` indices, by their `keys`, largest first, keeping the order of equal ones; `spare` holds as many.
function void Prog__UsageSort(u32 *order, u32 count, const u64 *keys, u32 *spare)
{
    u32 *from = order, *to = spare;
    for (u32 width = 1; width < count; width *= 2) {
        for (u32 start = 0; start < count; start += 2 * width) {
            u32 middle = Min(start + width, count), end = Min(start + 2 * width, count), a = start, b = middle, at = start;
            while (a < middle && b < end) to[at++] = keys[from[b]] > keys[from[a]] ? from[b++] : from[a++];
            while (a < middle) to[at++] = from[a++];
            while (b < end) to[at++] = from[b++];
        }
        u32 *swap = from;
        from = to, to = swap;
    }
    if (from != order) MemCopy(order, from, count * sizeof(u32));
}

// A file's extension, lowercase and without its dot; empty for none, and for a name that only starts with one, as ".bashrc".
function String Prog__UsageExtension(Arena *arena, String name)
{
    u64 dot = name.length;
    while (dot > 0 && name.value[dot - 1] != '.') --dot;
    if (dot <= 1 || dot >= name.length) return (String){0};
    String extension = StrFrom(name, dot);
    char *lower = ArenaPushArray(arena, extension.length, char);
    for (u64 i = 0; i < extension.length; ++i) lower[i] = extension.value[i] >= 'A' && extension.value[i] <= 'Z' ? (char)(extension.value[i] + 32) : extension.value[i];
    return (String){ lower, extension.length };
}

// The type of `extension`, made the first time it comes; the walk's mutex is held.
function u32 Prog__UsageType(Prog__UsageWalk *walk, String extension)
{
    u32 *found = MapsGetPtrNull(walk->typeOf, extension);
    if (found) return *found;
    u32 index = (u32)walk->types.header.count;
    String kept = StrCopy(walk->names, extension);
    ArrayPush(&walk->walk, walk->types, ((Prog_UsageType){ .extension = kept }));
    MapsPut(&walk->walk, walk->typeOf, kept, index);
    return index;
}

function void Prog__UsageLane(void *user, u32 lane)
{
    (void)lane;
    Prog__UsageWalk *walk = user;
    Arena local = {0};
    for (;;) {
        P_MutexLock(&walk->mutex);
        while (!walk->stack.header.count && walk->busy && !P_IsCancelled(walk->cancel)) P_ConditionWait(&walk->condition, &walk->mutex);
        if (!walk->stack.header.count || P_IsCancelled(walk->cancel)) {
            P_ConditionBroadcast(&walk->condition);
            P_MutexUnlock(&walk->mutex);
            break;
        }
        Prog__UsagePending next = walk->stack.v[--walk->stack.header.count];
        ++walk->busy;
        P_MutexUnlock(&walk->mutex);

        ArenaClear(&local);
        P_DirectoryResult listed = P_EnumerateDirectory(&local, next.path, walk->cancel);
        u32 count = (u32)listed.files.header.count, fileCount = 0, folderCount = 0;
        u32 *files = ArenaPushArray(&local, count + 1, u32), *spare = ArenaPushArray(&local, count + 1, u32), *folders = ArenaPushArray(&local, count + 1, u32);
        u64 *keys = ArenaPushArray(&local, count + 1, u64);
        b8 *walked = ArenaPushArray(&local, count + 1, b8);
        u64 ownBytes = 0;
        i64 newest = 0;
        for (u32 i = 0; i < count; ++i) {
            const P_File *file = &listed.files.v[i];
            // A link or junction counts as a file of no size, as what it leads to is counted where it is; a folder the system makes up, as /proc, counts without going into it.
            if (file->kind == P_FileKind_Directory && !file->symbolicLink) {
                walked[i] = !P_IsVirtualFolder(Prog_JoinPath(&local, next.path, file->name));
                folders[folderCount++] = i;
            } else {
                keys[i] = file->symbolicLink ? 0 : file->size;
                files[fileCount++] = i;
                ownBytes += keys[i];
                newest = Max(newest, file->modifiedTimeUnix);
            }
        }
        Prog__UsageSort(files, fileCount, keys, spare);
        // The listing's extensions, counted here so that the lock is held once for them.
        String *extensions = ArenaPushArray(&local, fileCount + 1, String);
        u64 *extensionBytes = ArenaPushArray(&local, fileCount + 1, u64), *extensionFiles = ArenaPushArray(&local, fileCount + 1, u64);
        u32 *fileExtension = ArenaPushArray(&local, fileCount + 1, u32), extensionCount = 0;
        for (u32 k = 0; k < fileCount; ++k) {
            const P_File *file = &listed.files.v[files[k]];
            String extension = Prog__UsageExtension(&local, file->name);
            u32 at = 0;
            while (at < extensionCount && !StrIsEqual(extensions[at], extension)) ++at;
            if (at == extensionCount) extensions[extensionCount] = extension, extensionBytes[extensionCount] = extensionFiles[extensionCount] = 0, ++extensionCount;
            extensionBytes[at] += keys[files[k]];
            ++extensionFiles[at];
            fileExtension[k] = at;
        }

        P_MutexLock(&walk->mutex);
        u32 *types = ArenaPushArray(&local, extensionCount + 1, u32);
        for (u32 e = 0; e < extensionCount; ++e) {
            types[e] = Prog__UsageType(walk, extensions[e]);
            walk->types.v[types[e]].bytes += extensionBytes[e];
            walk->types.v[types[e]].files += extensionFiles[e];
        }
        u32 kept = Min(fileCount, (u32)Prog_UsageKeptFiles), first = (u32)walk->files.header.count;
        u64 restBytes = 0;
        for (u32 k = 0; k < fileCount; ++k) {
            const P_File *file = &listed.files.v[files[k]];
            if (k < kept) ArrayPush(&walk->walk, walk->files, ((Prog_UsageFile){ .name = StrCopy(walk->names, file->name), .bytes = keys[files[k]], .type = types[fileExtension[k]] }));
            else restBytes += keys[files[k]];
        }
        Prog_UsageFolder *folder = &walk->folders.v[next.folder];
        folder->unreadable = listed.errorCode != 0 && !listed.cancelled;
        folder->bytes += ownBytes;
        folder->files += fileCount;
        folder->modified = Max(folder->modified, newest);
        folder->firstFile = first;
        folder->fileCount = kept;
        folder->restFiles = fileCount - kept;
        folder->restBytes = restBytes;
        for (u32 k = 0; k < folderCount; ++k) {
            const P_File *file = &listed.files.v[folders[k]];
            u32 index = (u32)walk->folders.header.count;
            ArrayPush(&walk->walk, walk->folders, ((Prog_UsageFolder){ .name = StrCopy(walk->names, file->name), .parent = next.folder, .firstChild = UINT32_MAX,
                                                                        .nextSibling = UINT32_MAX, .modified = file->modifiedTimeUnix, .hidden = file->hidden,
                                                                        .system = file->system }));
            if (walked[folders[k]]) ArrayPush(&walk->walk, walk->stack, ((Prog__UsagePending){ Prog_JoinPath(&walk->walk, next.path, file->name), index }));
        }
        atomic_fetch_add(&walk->progress->folders, folderCount);
        atomic_fetch_add(&walk->progress->files, fileCount);
        atomic_fetch_add(&walk->progress->bytes, ownBytes);
        --walk->busy;
        P_ConditionBroadcast(&walk->condition);
        P_MutexUnlock(&walk->mutex);
    }
    ArenaRelease(&local);
}

// Walks everything below `path` into `usage`, in `arena`, with what it finds so far in `progress`; false when cancelled.
// Touches the disk, so run it in a job.
function b32 Prog_ScanUsage(Arena *arena, String path, Prog_UsageProgress *progress, const P_CancelToken *cancel, Prog_Usage *usage)
{
    u64 start = PlatformNowMicroseconds();
    Prog__UsageWalk walk = { .names = arena, .cancel = cancel, .progress = progress };
    if (!P_MutexInit(&walk.mutex)) return false;
    if (!P_ConditionInit(&walk.condition)) {
        P_MutexRelease(&walk.mutex);
        return false;
    }
    String root = StrCopy(arena, path);
    ArrayPush(&walk.walk, walk.folders, ((Prog_UsageFolder){ .name = root, .parent = UINT32_MAX, .firstChild = UINT32_MAX, .nextSibling = UINT32_MAX }));
    ArrayPush(&walk.walk, walk.stack, ((Prog__UsagePending){ root, 0 }));
    P_RunLanes(Max(1u, Min(P_ProcessorCount(), (u32)Prog_WalkLanes)), Prog__UsageLane, &walk);
    P_ConditionRelease(&walk.condition);
    P_MutexRelease(&walk.mutex);
    b32 result = !P_IsCancelled(cancel);
    if (result) {
        u32 count = (u32)walk.folders.header.count;
        Prog_UsageFolder *folders = walk.folders.v;
        // Each folder's totals take in its folders', which come after it.
        for (u32 i = count; i-- > 1;) {
            Prog_UsageFolder *child = &folders[i], *parent = &folders[child->parent];
            parent->bytes += child->bytes;
            parent->files += child->files;
            parent->folders += child->folders + 1;
            parent->modified = Max(parent->modified, child->modified);
        }
        // Each folder's own, largest first: counted by parent, placed, sorted and linked.
        u32 *offsets = ArenaPushArray(&walk.walk, count + 1, u32), *children = ArenaPushArray(&walk.walk, count + 1, u32);
        u32 *spare = ArenaPushArray(&walk.walk, count + 1, u32), *placed = ArenaPushArray(&walk.walk, count + 1, u32);
        u64 *keys = ArenaPushArray(&walk.walk, count + 1, u64);
        for (u32 i = 0; i < count; ++i) keys[i] = folders[i].bytes;
        for (u32 i = 1; i < count; ++i) ++offsets[folders[i].parent + 1];
        for (u32 i = 0; i < count; ++i) offsets[i + 1] += offsets[i];
        for (u32 i = 1; i < count; ++i) children[offsets[folders[i].parent] + placed[folders[i].parent]++] = i;
        for (u32 p = 0; p < count; ++p) {
            u32 from = offsets[p], many = offsets[p + 1] - from;
            if (!many) continue;
            Prog__UsageSort(children + from, many, keys, spare);
            folders[p].firstChild = children[from];
            for (u32 k = 0; k + 1 < many; ++k) folders[children[from + k]].nextSibling = children[from + k + 1];
        }
        // The types largest first, the files following them there, and the larger ones named as the system names them.
        u32 typeCount = (u32)walk.types.header.count;
        u32 *order = ArenaPushArray(&walk.walk, typeCount + 1, u32), *moved = ArenaPushArray(&walk.walk, typeCount + 1, u32);
        u32 *typeSpare = ArenaPushArray(&walk.walk, typeCount + 1, u32);
        u64 *typeKeys = ArenaPushArray(&walk.walk, typeCount + 1, u64);
        for (u32 t = 0; t < typeCount; ++t) order[t] = t, typeKeys[t] = walk.types.v[t].bytes;
        Prog__UsageSort(order, typeCount, typeKeys, typeSpare);
        usage->types = ArenaPushArray(arena, typeCount + 1, Prog_UsageType);
        for (u32 t = 0; t < typeCount; ++t) {
            usage->types[t] = walk.types.v[order[t]];
            moved[order[t]] = t;
            if (t < 200 && usage->types[t].extension.length) {
                Scratch scratch = GetScratchEx(arena);
                usage->types[t].name = P_FileTypeName(arena, StrFormat(scratch.arena, "file.%S", usage->types[t].extension), P_FileKind_File);
                ReleaseScratch(scratch);
            }
        }
        usage->typeCount = typeCount;
        usage->fileCount = (u32)walk.files.header.count;
        usage->files = ArenaPushArray(arena, usage->fileCount + 1, Prog_UsageFile);
        for (u32 f = 0; f < usage->fileCount; ++f) {
            usage->files[f] = walk.files.v[f];
            usage->files[f].type = moved[usage->files[f].type];
        }
        usage->folderCount = count;
        usage->folders = ArenaPushArray(arena, count + 1, Prog_UsageFolder);
        MemCopy(usage->folders, folders, count * sizeof(Prog_UsageFolder));
        usage->path = root;
        usage->microseconds = PlatformNowMicroseconds() - start;
    }
    ArenaRelease(&walk.walk);
    return result;
}

// The path of the usage's folder `index`, from its root's.
function String Prog_UsageFolderPath(Arena *arena, const Prog_Usage *usage, u32 index)
{
    u32 depth = 0;
    for (u32 at = index; at != UINT32_MAX; at = usage->folders[at].parent) ++depth;
    Scratch scratch = GetScratchEx(arena);
    u32 *chain = ArenaPushArray(scratch.arena, depth + 1, u32), n = 0;
    for (u32 at = index; at != UINT32_MAX; at = usage->folders[at].parent) chain[n++] = at;
    String path = usage->folders[chain[n - 1]].name;
    for (u32 i = n - 1; i-- > 0;) path = Prog_JoinPath(scratch.arena, path, usage->folders[chain[i]].name);
    path = StrCopy(arena, path);
    ReleaseScratch(scratch);
    return path;
}

// What a folder's treemap lays out: its folders, its kept files and its smaller files together.
typedef struct Prog__UsageItem {
    u64 bytes;
    u32 folder;  // a folder of it, or the folder whose files these are
    u32 file;    // UINT32_MAX for a folder, or for the smaller files
    b8 isFolder;
} Prog__UsageItem;
typedef struct Prog__UsageCells { ArrayHeader header; Prog_UsageCell *v; } Prog__UsageCells;

// The most cells a treemap has, enough for a large screen's worth of the smallest ones.
#define Prog_UsageMaxCells 200000

function void Prog__UsageLayoutFolder(Arena *arena, const Prog_Usage *usage, u32 index, R_Rect rect, Prog__UsageCells *cells);

// Places what one item takes of the rectangle; a folder big enough has its frame and its contents inside it, a pixel in.
function void Prog__UsagePlace(Arena *arena, const Prog_Usage *usage, const Prog__UsageItem *item, R_Rect rect, Prog__UsageCells *cells)
{
    rect = (R_Rect){ floorf(rect.x0 + 0.5f), floorf(rect.y0 + 0.5f), floorf(rect.x1 + 0.5f), floorf(rect.y1 + 0.5f) };
    if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0 || cells->header.count >= Prog_UsageMaxCells) return;
    if (item->isFolder && rect.x1 - rect.x0 >= 6 && rect.y1 - rect.y0 >= 6) {
        Prog__UsageLayoutFolder(arena, usage, item->folder, rect, cells);
        return;
    }
    u32 type = item->file != UINT32_MAX ? usage->files[item->file].type : UINT32_MAX;
    ArrayPush(arena, *cells, ((Prog_UsageCell){ .rect = rect, .folder = item->folder, .file = item->file, .type = type, .rest = !item->isFolder && item->file == UINT32_MAX }));
}

// The squarified treemap of folder `index` in `rect`: rows of items, largest first, along the rectangle's shorter side, each row as long as keeps its items closest to square.
function void Prog__UsageLayoutFolder(Arena *arena, const Prog_Usage *usage, u32 index, R_Rect rect, Prog__UsageCells *cells)
{
    const Prog_UsageFolder *folder = &usage->folders[index];
    ArrayPush(arena, *cells, ((Prog_UsageCell){ .rect = rect, .folder = index, .file = UINT32_MAX, .type = UINT32_MAX, .frame = true }));
    rect = (R_Rect){ rect.x0 + 1, rect.y0 + 1, rect.x1 - 1, rect.y1 - 1 };
    Scratch scratch = GetScratchEx(arena);
    u32 most = folder->fileCount + 2;
    for (u32 child = folder->firstChild; child != UINT32_MAX; child = usage->folders[child].nextSibling) ++most;
    Prog__UsageItem *items = ArenaPushArray(scratch.arena, most, Prog__UsageItem);
    u32 count = 0;
    for (u32 child = folder->firstChild; child != UINT32_MAX; child = usage->folders[child].nextSibling) {
        if (usage->folders[child].bytes) items[count++] = (Prog__UsageItem){ usage->folders[child].bytes, child, UINT32_MAX, true };
    }
    for (u32 f = 0; f < folder->fileCount; ++f) {
        if (usage->files[folder->firstFile + f].bytes) items[count++] = (Prog__UsageItem){ usage->files[folder->firstFile + f].bytes, index, folder->firstFile + f, false };
    }
    if (folder->restBytes) items[count++] = (Prog__UsageItem){ folder->restBytes, index, UINT32_MAX, false };
    u32 *order = ArenaPushArray(scratch.arena, count + 1, u32), *spare = ArenaPushArray(scratch.arena, count + 1, u32);
    u64 *keys = ArenaPushArray(scratch.arena, count + 1, u64), total = 0;
    for (u32 i = 0; i < count; ++i) order[i] = i, keys[i] = items[i].bytes, total += items[i].bytes;
    Prog__UsageSort(order, count, keys, spare);
    f64 x0 = rect.x0, y0 = rect.y0, x1 = rect.x1, y1 = rect.y1, left = (f64)total;
    for (u32 i = 0; i < count && left > 0 && x1 - x0 >= 1 && y1 - y0 >= 1;) {
        f64 width = x1 - x0, height = y1 - y0, scale = width * height / left, side = MinF64(width, height);
        // The row grows while that makes its least square item squarer.
        u32 end = i + 1;
        f64 sum = (f64)items[order[i]].bytes * scale, largest = sum;
        f64 worst = MaxF64(side * side * largest / (sum * sum), sum * sum / (side * side * sum));
        while (end < count) {
            f64 area = (f64)items[order[end]].bytes * scale, grown = sum + area;
            f64 next = MaxF64(side * side * largest / (grown * grown), grown * grown / (side * side * area));
            if (next > worst) break;
            worst = next, sum = grown, ++end;
        }
        // The rest are too small to see.
        if (sum < 1) break;
        f64 thick = sum / side, along = 0;
        for (u32 k = i; k < end; ++k) {
            f64 length = (f64)items[order[k]].bytes * scale / thick;
            R_Rect place = width >= height ? (R_Rect){ (f32)x0, (f32)(y0 + along), (f32)(x0 + thick), (f32)(y0 + along + length) }
                                           : (R_Rect){ (f32)(x0 + along), (f32)y0, (f32)(x0 + along + length), (f32)(y0 + thick) };
            Prog__UsagePlace(arena, usage, &items[order[k]], place, cells);
            along += length;
        }
        if (width >= height) x0 += thick;
        else y0 += thick;
        for (u32 k = i; k < end; ++k) left -= (f64)items[order[k]].bytes;
        i = end;
    }
    ReleaseScratch(scratch);
}

// The treemap of folder `root` filling `rect`, in `arena`: frames first, each followed by what is inside it.
function u32 Prog_UsageLayout(Arena *arena, const Prog_Usage *usage, u32 root, R_Rect rect, Prog_UsageCell **cells)
{
    Prog__UsageCells laid = {0};
    if (root < usage->folderCount && rect.x1 - rect.x0 >= 2 && rect.y1 - rect.y0 >= 2) Prog__UsageLayoutFolder(arena, usage, root, rect, &laid);
    *cells = laid.v;
    return (u32)laid.header.count;
}

// Takes the item at `path`, a folder or a kept file below the folder scanned, out of `usage`, as when it was deleted, with its bytes and counts out of the folders it was in; returns the folder it was in, or UINT32_MAX when the usage has no such item.
// ponytail: the types keep a gone folder's files, which a scan again brings right.
function u32 Prog_UsageRemove(Prog_Usage *usage, String path)
{
    String root = usage->path;
    while (root.length > 1 && root.value[root.length - 1] == '/') --root.length;
    if (!usage->folderCount || path.length <= root.length + 1 || !StrIsEqual(StrFromTo(path, 0, (i64)root.length), root) || path.value[root.length] != '/') return UINT32_MAX;
    String rest = StrFrom(path, root.length + 1);
    u32 at = 0;
    for (;;) {
        Index slash = StrIndexChar(rest, '/', SSF_None);
        String name = slash.exists ? StrFromTo(rest, 0, (i64)slash.value) : rest;
        u32 child = usage->folders[at].firstChild, previous = UINT32_MAX;
        while (child != UINT32_MAX && !StrIsEqual(usage->folders[child].name, name)) previous = child, child = usage->folders[child].nextSibling;
        if (slash.exists) {
            if (child == UINT32_MAX) return UINT32_MAX;
            at = child, rest = StrFrom(rest, slash.value + 1);
            continue;
        }
        u64 bytes = 0, files = 0, folders = 0;
        if (child != UINT32_MAX) {
            Prog_UsageFolder *gone = &usage->folders[child];
            bytes = gone->bytes, files = gone->files, folders = gone->folders + 1;
            if (previous == UINT32_MAX) usage->folders[at].firstChild = gone->nextSibling;
            else usage->folders[previous].nextSibling = gone->nextSibling;
        } else {
            Prog_UsageFolder *folder = &usage->folders[at];
            u32 found = UINT32_MAX;
            for (u32 f = 0; f < folder->fileCount && found == UINT32_MAX; ++f) if (StrIsEqual(usage->files[folder->firstFile + f].name, name)) found = folder->firstFile + f;
            if (found == UINT32_MAX) return UINT32_MAX;
            Prog_UsageFile *file = &usage->files[found];
            bytes = file->bytes, files = 1;
            if (file->type < usage->typeCount) usage->types[file->type].bytes -= Min(file->bytes, usage->types[file->type].bytes), usage->types[file->type].files -= Min(1u, (u32)usage->types[file->type].files);
            // The kept files close up over it, in their order.
            MemMove(file, file + 1, (folder->fileCount - (found - folder->firstFile) - 1) * sizeof(Prog_UsageFile));
            --folder->fileCount;
        }
        for (u32 up = at; up != UINT32_MAX; up = usage->folders[up].parent) {
            Prog_UsageFolder *folder = &usage->folders[up];
            folder->bytes -= Min(bytes, folder->bytes), folder->files -= Min(files, folder->files), folder->folders -= Min(folders, folder->folders);
        }
        return at;
    }
}
