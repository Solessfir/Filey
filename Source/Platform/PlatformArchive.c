// libarchive, which reads and writes archives on both platforms: Linux loads the system's, Windows its own copy, archiveint.dll, which its
// tar and Explorer use. It is loaded when first needed, so that it is not a build dependency. Each platform's files include this after giving
// it what differs there: P__ArchiveSymbol, which loads the library, P__ArchiveNativePath with the names of the open functions that take such
// paths, and the error codes P__ArchiveCancelled, P__ArchiveUnsupported and P__ArchiveFailed. They define the file system's part after it.

// Writes the item at `path` on disk into `writer` as `name`, a folder with all it holds; with no writer, only adds its size to the progress.
function u32 P__ArchiveWritePath(void *writer, String path, String name, P_Progress *progress, const P_CancelToken *cancel);
// Writes the data of `reader`'s entry to a new file at `path`, its folders made first, with the time `modified`; nothing is left of it when it
// fails or is cancelled.
function u32 P__ArchiveWriteFile(void *reader, String path, i64 modified, u64 *read, P_Progress *progress, const P_CancelToken *cancel);
function u32 P__ArchiveMakeFolders(String path);
function u64 P__ArchiveFileSize(String path);
function void P__ArchiveRemove(String path);

// Writing, which adds to archives, is loaded with the rest and only needed then.
typedef struct P__Archive {
    b32 loaded, writable;
    void *(*readNew)(void);
    i32 (*supportFilterAll)(void *);
    i32 (*supportFormatAll)(void *);
    i32 (*openFilename)(void *, const void *, u64);
    i32 (*nextHeader)(void *, void **);
    const char *(*pathname)(void *);
    i64 (*size)(void *);
    // A 16-bit mode on Windows, so only its low bits are read.
    u32 (*filetype)(void *);
    i32 (*free)(void *);
    i64 (*mtime)(void *);
    i32 (*readBlock)(void *, const void **, u64 *, i64 *);
    const char *(*error)(void *);
    i32 (*format)(void *);
    i32 (*filterCode)(void *, i32);
    i64 (*filterBytes)(void *, i32);
    void *(*writeNew)(void);
    i32 (*writeFormat)(void *, i32);
    i32 (*writeFilter)(void *, i32);
    i32 (*writeOptions)(void *, const char *);
    i32 (*writeOpen)(void *, const void *);
    i32 (*writeHeader)(void *, void *);
    i64 (*writeData)(void *, const void *, u64);
    i32 (*writeClose)(void *);
    i32 (*writeFree)(void *);
    void *(*entryNew)(void);
    void (*entryFree)(void *);
    void (*entrySetPath)(void *, const char *);
    void (*entryCopyStat)(void *, const void *);
    void (*entrySetFiletype)(void *, u32);
    void (*entrySetPerm)(void *, u32);
    void (*entrySetSize)(void *, i64);
    void (*entrySetMtime)(void *, i64, i64);
    // Encrypted entries, which a zip's may be; libarchive cannot take the 7z's and RAR's apart.
    i32 (*addPassphrase)(void *, const char *);
    i32 (*entryEncrypted)(void *);
} P__Archive;
global P__Archive P__archive;
global atomic_uint P__archiveLoad;  // 0 before the first try, 1 while one job loads it, 2 once it is loaded or missing

function void P__LoadArchiveSymbols(void)
{
    P__Archive *a = &P__archive;
    *(void **)&a->readNew = P__ArchiveSymbol("archive_read_new");
    if (!a->readNew) return;
    *(void **)&a->supportFilterAll = P__ArchiveSymbol("archive_read_support_filter_all");
    *(void **)&a->supportFormatAll = P__ArchiveSymbol("archive_read_support_format_all");
    *(void **)&a->openFilename = P__ArchiveSymbol(P__ArchiveOpenReadName);
    *(void **)&a->nextHeader = P__ArchiveSymbol("archive_read_next_header");
    *(void **)&a->pathname = P__ArchiveSymbol("archive_entry_pathname_utf8");
    *(void **)&a->size = P__ArchiveSymbol("archive_entry_size");
    *(void **)&a->filetype = P__ArchiveSymbol("archive_entry_filetype");
    *(void **)&a->free = P__ArchiveSymbol("archive_read_free");
    *(void **)&a->mtime = P__ArchiveSymbol("archive_entry_mtime");
    *(void **)&a->readBlock = P__ArchiveSymbol("archive_read_data_block");
    *(void **)&a->error = P__ArchiveSymbol("archive_error_string");
    *(void **)&a->format = P__ArchiveSymbol("archive_format");
    *(void **)&a->filterCode = P__ArchiveSymbol("archive_filter_code");
    *(void **)&a->filterBytes = P__ArchiveSymbol("archive_filter_bytes");
    *(void **)&a->writeNew = P__ArchiveSymbol("archive_write_new");
    *(void **)&a->writeFormat = P__ArchiveSymbol("archive_write_set_format");
    *(void **)&a->writeFilter = P__ArchiveSymbol("archive_write_add_filter");
    *(void **)&a->writeOptions = P__ArchiveSymbol("archive_write_set_options");
    *(void **)&a->writeOpen = P__ArchiveSymbol(P__ArchiveOpenWriteName);
    *(void **)&a->writeHeader = P__ArchiveSymbol("archive_write_header");
    *(void **)&a->writeData = P__ArchiveSymbol("archive_write_data");
    *(void **)&a->writeClose = P__ArchiveSymbol("archive_write_close");
    *(void **)&a->writeFree = P__ArchiveSymbol("archive_write_free");
    *(void **)&a->entryNew = P__ArchiveSymbol("archive_entry_new");
    *(void **)&a->entryFree = P__ArchiveSymbol("archive_entry_free");
    *(void **)&a->entrySetPath = P__ArchiveSymbol("archive_entry_set_pathname_utf8");
    *(void **)&a->entryCopyStat = P__ArchiveSymbol("archive_entry_copy_stat");
    *(void **)&a->entrySetFiletype = P__ArchiveSymbol("archive_entry_set_filetype");
    *(void **)&a->entrySetPerm = P__ArchiveSymbol("archive_entry_set_perm");
    *(void **)&a->entrySetSize = P__ArchiveSymbol("archive_entry_set_size");
    *(void **)&a->entrySetMtime = P__ArchiveSymbol("archive_entry_set_mtime");
    *(void **)&a->addPassphrase = P__ArchiveSymbol("archive_read_add_passphrase");
    *(void **)&a->entryEncrypted = P__ArchiveSymbol("archive_entry_is_encrypted");
    a->loaded = a->readNew && a->supportFilterAll && a->supportFormatAll && a->openFilename && a->nextHeader && a->pathname && a->size && a->filetype && a->free &&
                a->mtime && a->readBlock && a->error && a->format && a->filterCode && a->filterBytes;
    a->writable = a->loaded && a->writeNew && a->writeFormat && a->writeFilter && a->writeOptions && a->writeOpen && a->writeHeader && a->writeData && a->writeClose && a->writeFree &&
                  a->entryNew && a->entryFree && a->entrySetPath && a->entryCopyStat && a->entrySetFiletype && a->entrySetPerm && a->entrySetSize && a->entrySetMtime;
}

// Jobs that want it at the same time wait for the first one to load it.
function b32 P__LoadArchive(void)
{
    u32 untried = 0;
    if (atomic_compare_exchange_strong(&P__archiveLoad, &untried, 1)) {
        P__LoadArchiveSymbols();
        atomic_store(&P__archiveLoad, 2);
    }
    while (atomic_load(&P__archiveLoad) != 2) P_Sleep(1);
    return P__archive.loaded;
}

// Puts `name` up as the item being worked on, for the program to show.
function void P_ProgressName(P_Progress *progress, String name)
{
    atomic_fetch_add(&progress->nameVersion, 1);
    u64 length = 0;
    for (; length < name.length && length + 1 < CArrayCount(progress->name); ++length) atomic_store_explicit(&progress->name[length], name.value[length], memory_order_relaxed);
    atomic_store_explicit(&progress->name[length], 0, memory_order_relaxed);
    atomic_fetch_add(&progress->nameVersion, 1);
}

// Waits while the program has the operation paused; false once it is cancelled.
function b32 P__Proceed(P_Progress *progress, const P_CancelToken *cancel)
{
    while (atomic_load(&progress->paused) && !P_IsCancelled(cancel)) P_Sleep(50);
    return !P_IsCancelled(cancel);
}

// Counts how far `reader` has moved on through its archive file since `*read`, UINT64_MAX before the first time, as done. A step back, as
// from a 7z's index at its end to its data at the start, only moves the mark.
function void P__ArchiveAdvance(P_Progress *progress, void *reader, u64 *read)
{
    i64 at = P__archive.filterBytes(reader, -1);
    if (at < 0) return;
    if (*read != UINT64_MAX && (u64)at > *read) atomic_fetch_add(&progress->doneBytes, (u64)at - *read);
    *read = (u64)at;
}

// The passwords given for encrypted archives this run, by archive, which every read of that archive is opened with. They are kept only
// in memory.
global struct {
    atomic_uint lock;
    Arena arena;
    String archives[32], passwords[32];
    u32 count;
} P__archivePasswords;

function void P__PasswordsLock(void)
{
    while (atomic_exchange(&P__archivePasswords.lock, 1u)) P_Sleep(1);
}

function void P__PasswordsUnlock(void)
{
    atomic_store(&P__archivePasswords.lock, 0u);
}

function void P_ArchiveSetPassword(String archive, String password)
{
    P__PasswordsLock();
    u32 at = 0;
    while (at < P__archivePasswords.count && !StrIsEqual(P__archivePasswords.archives[at], archive)) ++at;
    if (at < CArrayCount(P__archivePasswords.archives)) {
        if (at == P__archivePasswords.count) P__archivePasswords.archives[P__archivePasswords.count++] = StrCopy(&P__archivePasswords.arena, archive);
        P__archivePasswords.passwords[at] = StrCopy(&P__archivePasswords.arena, password);
    }
    P__PasswordsUnlock();
}

// `text` as a C string in `arena`, as libarchive takes passwords.
function const char *P__ArchiveCString(Arena *arena, String text)
{
    char *result = ArenaPushArrayNoZero(arena, text.length + 1, char);
    MemCopy(result, text.value, text.length);
    result[text.length] = 0;
    return result;
}

// `archive`'s password in `arena`, or NULL when none was given.
function const char *P__ArchivePassword(Arena *arena, String archive)
{
    const char *result = NULL;
    P__PasswordsLock();
    for (u32 i = 0; i < P__archivePasswords.count && !result; ++i) {
        if (StrIsEqual(P__archivePasswords.archives[i], archive)) result = P__ArchiveCString(arena, P__archivePasswords.passwords[i]);
    }
    P__PasswordsUnlock();
    return result;
}

function b32 P_ArchiveHasPassword(String archive)
{
    Scratch scratch = GetScratch();
    b32 result = P__ArchivePassword(scratch.arena, archive) != NULL;
    ReleaseScratch(scratch);
    return result;
}

// A reader of the archive at `path`, open, or NULL. Its encrypted entries are read with `password`, else with the one given for it.
function void *P__ArchiveRead(String path, const char *password)
{
    P__Archive *a = &P__archive;
    Scratch scratch = GetScratch();
    const void *native = P__ArchiveNativePath(scratch.arena, path);
    void *reader = native ? a->readNew() : NULL;
    if (reader) {
        a->supportFilterAll(reader);
        a->supportFormatAll(reader);
        if (!password) password = P__ArchivePassword(scratch.arena, path);
        if (password && a->addPassphrase) a->addPassphrase(reader, password);
        if (a->openFilename(reader, native, 65536) != 0) {
            a->free(reader);
            reader = NULL;
        }
    }
    ReleaseScratch(scratch);
    return reader;
}

// libarchive's codes for the formats and filters it writes, and what an archive's name says it is when it has nothing in it to tell.
enum { P__ArchiveTar = 0x30000, P__ArchiveZip = 0x50000, P__Archive7z = 0xE0000 };
enum { P__FilterGzip = 1, P__FilterBzip2 = 2, P__FilterXz = 6, P__FilterZstd = 14 };

// The next entry's path inside the archive, without "./" before it or '/' after a folder's, with whether it is a folder; false at the end
// or once cancelled. ARCHIVE_OK is 0 and ARCHIVE_WARN -20, which still gives the entry.
enum { P__ArchiveWarn = -20 };
function b32 P__ArchiveNext(void *reader, void **entry, String *name, b32 *folder, const P_CancelToken *cancel)
{
    P__Archive *a = &P__archive;
    // What is left of the last entry is read through here where skipping it means unpacking it anyway, in a compressed stream, so that a
    // cancel need not wait for a large one; a zip or a plain tar skips it with a seek, and a 7z that is only listed without unpacking.
    // ponytail: skipping to a later entry of a solid 7z unpacks the ones before it without a cancel; read them through here when that shows.
    if (*entry && a->filterCode(reader, 0) > 0) {
        const void *block = NULL;
        u64 size = 0;
        i64 offset = 0;
        while (!P_IsCancelled(cancel) && a->readBlock(reader, &block, &size, &offset) == 0) {}
    }
    i32 status = 0;
    while (!P_IsCancelled(cancel) && ((status = a->nextHeader(reader, entry)) == 0 || status == P__ArchiveWarn)) {
        const char *path = a->pathname(*entry);
        if (!path) continue;
        String inner = StrFromCStr(path);
        while (inner.length >= 2 && inner.value[0] == '.' && inner.value[1] == '/') inner = StrFrom(inner, 2);
        *folder = (a->filetype(*entry) & 0170000) == 0040000;
        while (inner.length && inner.value[inner.length - 1] == '/') --inner.length;
        if (!inner.length) continue;
        *name = inner;
        return true;
    }
    return false;
}

// Encrypted 7z and RAR archives, which libarchive cannot decrypt, are read with 7-Zip's command line program once their password is
// given, which it is handed on its standard input.
function b32 P__SevenZipKind(String archive)
{
    String names[] = { S(".7z"), S(".rar"), S(".cb7"), S(".cbr") };
    for (u32 i = 0; i < CArrayCount(names); ++i) {
        b32 same = archive.length > names[i].length;
        for (u64 c = 0; c < names[i].length && same; ++c) {
            char letter = archive.value[archive.length - names[i].length + c];
            same = (letter >= 'A' && letter <= 'Z' ? (char)(letter + 32) : letter) == names[i].value[c];
        }
        if (same) return true;
    }
    return false;
}

// 7-Zip's program by the names its packages give it on the PATH, else where its installer puts it on Windows; empty without one.
function String P__SevenZipProgram(Arena *arena)
{
    String names[] = { S("7z"), S("7zz"), S("7za") };
    for (u32 i = 0; i < CArrayCount(names); ++i) if (P_ProgramFound(names[i])) return names[i];
    String programs = P_EnvironmentValue(arena, S("ProgramFiles"));
    String installed = programs.length ? StrFormat(arena, "%S/7-Zip/7z.exe", programs) : (String){0};
    return installed.length && P_PathExists(installed) ? installed : (String){0};
}

// Whether 7-Zip reads `archive`: a 7z or RAR one with a password given, and 7-Zip there to read it with, as `program`.
function b32 P__SevenZipReads(Arena *arena, String archive, String *program)
{
    *program = P__SevenZipKind(archive) && P_ArchiveHasPassword(archive) ? P__SevenZipProgram(arena) : (String){0};
    return program->length != 0;
}

// `name` in the folder `folder`.
function String P__ArchiveJoin(Arena *arena, String folder, String name)
{
    return StrFormat(arena, folder.length && folder.value[folder.length - 1] == '/' ? "%S%S" : "%S/%S", folder, name);
}

// The folder `archive` is in, which 7-Zip runs in.
function String P__ArchiveFolder(String archive)
{
    Index slash = StrIndexChar(archive, '/', SSF_SearchBackwards);
    return slash.exists ? StrFromTo(archive, 0, (i64)Max(slash.value, (u64)(slash.value == 2 ? 3 : 1))) : S(".");
}

// Days from 1970-01-01 to the date, by the proleptic Gregorian calendar.
function i64 P__DaysFromCivil(i64 year, i64 month, i64 day)
{
    year -= month <= 2;
    i64 era = (year >= 0 ? year : year - 399) / 400, yearOfEra = year - era * 400;
    i64 dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1, dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

// Every entry of `archive` as 7-Zip lists it, by its whole path with '/' between parts, with `password`; the first encrypted file's path in
// `encrypted`. False when 7-Zip could not list it, as for a wrong password where the names are encrypted too.
function b32 P__SevenZipList(Arena *arena, String program, String archive, String password, const P_CancelToken *cancel, P_FileArray *files, String *encrypted)
{
    String arguments[] = { program, S("l"), S("-slt"), S("-ba"), S("-sccUTF-8"), S("--"), archive }, output = {0};
    b32 result = P_RunCommandInput(arena, P__ArchiveFolder(archive), arguments, CArrayCount(arguments), StrFormat(arena, "%S\n", password), 120000, &output, cancel);
    *files = (P_FileArray){0};
    if (encrypted) *encrypted = (String){0};
    P_File file = {0};
    b32 started = false, locked = false;
    ArrayStr lines = StrSplit(arena, output, '\n');
    for (u64 l = 0; l <= lines.header.count; ++l) {
        String line = l < lines.header.count ? lines.v[l] : (String){0};
        while (line.length && (line.value[line.length - 1] == '\r' || line.value[line.length - 1] == ' ')) --line.length;
        if (!line.length || (line.length > 7 && StrIsEqual(StrFromTo(line, 0, 7), S("Path = ")))) {
            if (started && file.name.length) {
                ArrayPush(arena, *files, file);
                if (encrypted && locked && !encrypted->length && file.kind != P_FileKind_Directory) *encrypted = file.name;
            }
            file = (P_File){ .kind = P_FileKind_File, .metadataValid = true };
            started = line.length != 0, locked = false;
            if (!line.length) continue;
        }
        Index equals = StrIndexStr(line, S(" = "), SSF_None);
        if (!equals.exists || !started) continue;
        String key = StrFromTo(line, 0, (i64)equals.value), value = StrFrom(line, equals.value + 3);
        if (StrIsEqual(key, S("Path"))) {
            char *name = ArenaPushArray(arena, value.length + 1, char);
            for (u64 i = 0; i < value.length; ++i) name[i] = value.value[i] == '\\' ? '/' : value.value[i];
            file.name = StrMake(name, value.length);
        } else if (StrIsEqual(key, S("Folder")) && value.length && value.value[0] == '+') {
            file.kind = P_FileKind_Directory;
        } else if (StrIsEqual(key, S("Attributes")) && value.length && value.value[0] == 'D') {
            file.kind = P_FileKind_Directory;
        } else if (StrIsEqual(key, S("Size"))) {
            for (u64 i = 0; i < value.length && value.value[i] >= '0' && value.value[i] <= '9'; ++i) file.size = file.size * 10 + (u64)(value.value[i] - '0');
        } else if (StrIsEqual(key, S("Encrypted"))) {
            locked = value.length && value.value[0] == '+';
        } else if (StrIsEqual(key, S("Modified")) && value.length >= 19) {
            // "2024-01-31 12:34:56", in local time.
            i64 parts[6] = {0};
            u64 at[] = { 0, 5, 8, 11, 14, 17 }, width[] = { 4, 2, 2, 2, 2, 2 };
            for (u32 p = 0; p < 6; ++p) for (u64 i = 0; i < width[p]; ++i) parts[p] = parts[p] * 10 + (value.value[at[p] + i] - '0');
            i64 local = P__DaysFromCivil(parts[0], parts[1], parts[2]) * 86400 + parts[3] * 3600 + parts[4] * 60 + parts[5];
            file.modifiedTimeUnix = local - P_LocalOffset(local);
        }
    }
    return result;
}

function b32 P_ListArchive(Arena *arena, String path, u64 limit, const P_CancelToken *cancel, P_FileArray *files)
{
    *files = (P_FileArray){0};
    Scratch scratch = GetScratchEx(arena);
    String program = {0};
    if (P__SevenZipReads(scratch.arena, path, &program)) {
        P_FileArray all = {0};
        P__SevenZipList(scratch.arena, program, path, StrFromCStr(P__ArchivePassword(scratch.arena, path)), cancel, &all, NULL);
        for (u64 i = 0; i < all.header.count && files->header.count < limit; ++i) {
            P_File file = all.v[i];
            file.name = StrCopy(arena, file.name);
            ArrayPush(arena, *files, file);
        }
        ReleaseScratch(scratch);
        return files->header.count != 0;
    }
    ReleaseScratch(scratch);
    if (!P__LoadArchive()) return false;
    P__Archive *a = &P__archive;
    void *reader = P__ArchiveRead(path, NULL), *entry = NULL;
    String inner = {0};
    b32 folder = false;
    while (reader && files->header.count < limit && P__ArchiveNext(reader, &entry, &inner, &folder, cancel)) {
        ArrayPush(arena, *files, ((P_File){ .name = StrCopy(arena, inner), .size = folder ? 0 : (u64)Max(a->size(entry), (i64)0),
                                             .kind = folder ? P_FileKind_Directory : P_FileKind_File }));
    }
    if (reader) a->free(reader);
    return files->header.count != 0;
}

// The items of the level `inner` of an archive whose entries are `all`, by their whole paths: its folders, met as entries of their own or
// only in the paths of what they hold, each listed once.
function void P__ArchiveLevel(Arena *arena, const P_FileArray *all, String inner, P_DirectoryResult *result)
{
    Scratch scratch = GetScratchEx(arena);
    struct { MapHeader header; struct { String key; u64 value; } *v; } seen = {0};
    for (u64 i = 0; i < all->header.count; ++i) {
        String name = all->v[i].name, rest = name;
        if (inner.length) {
            if (name.length <= inner.length + 1 || name.value[inner.length] != '/' || !StrIsEqual(StrFromTo(name, 0, (i64)inner.length), inner)) continue;
            rest = StrFrom(name, inner.length + 1);
        }
        Index slash = StrIndexChar(rest, '/', SSF_None);
        String child = slash.exists ? StrFromTo(rest, 0, (i64)slash.value) : rest;
        b32 deeper = slash.exists, isFolder = deeper || all->v[i].kind == P_FileKind_Directory;
        u64 at = MapsGet(seen, u64, child);
        if (!at) {
            ArrayPush(arena, result->files, ((P_File){ .name = StrCopy(arena, child), .kind = isFolder ? P_FileKind_Directory : P_FileKind_File }));
            at = result->files.header.count;
            MapsPut(scratch.arena, seen, result->files.v[at - 1].name, at);
        }
        if (!deeper) {
            P_File *file = &result->files.v[at - 1];
            file->size = isFolder ? 0 : all->v[i].size;
            file->modifiedTimeUnix = all->v[i].modifiedTimeUnix;
            file->metadataValid = true;
            file->hidden = child.value[0] == '.';
        }
    }
    ReleaseScratch(scratch);
}

function P_DirectoryResult P_EnumerateArchive(Arena *arena, String archive, String inner, const P_CancelToken *cancel)
{
    P_DirectoryResult result = {0};
    Scratch scratch = GetScratchEx(arena);
    String program = {0};
    if (P__SevenZipReads(scratch.arena, archive, &program)) {
        P_FileArray all = {0};
        if (!P__SevenZipList(scratch.arena, program, archive, StrFromCStr(P__ArchivePassword(scratch.arena, archive)), cancel, &all, NULL) && !all.header.count) {
            result.errorCode = P__ArchiveFailed;
            result.errorString = StrCopy(arena, S("7-Zip could not read the archive, or its password is wrong."));
        }
        P__ArchiveLevel(arena, &all, inner, &result);
        result.cancelled = P_IsCancelled(cancel);
        ReleaseScratch(scratch);
        return result;
    }
    ReleaseScratch(scratch);
    P__Archive *a = &P__archive;
    void *reader = P__LoadArchive() ? P__ArchiveRead(archive, NULL) : NULL, *entry = NULL;
    if (!reader) {
        P_FileError error = P__FileError(arena, P__archive.loaded ? P__ArchiveFailed : P__ArchiveUnsupported);
        result.errorCode = error.code;
        result.errorString = P__archive.loaded ? error.message : S("libarchive is not installed");
        return result;
    }
    // The level's folders, met as entries of their own or only in the paths of what they hold, each listed once.
    struct { MapHeader header; struct { String key; u64 value; } *v; } seen = {0};
    String name = {0};
    b32 folder = false;
    while (P__ArchiveNext(reader, &entry, &name, &folder, cancel)) {
        if (a->entryEncrypted && a->entryEncrypted(entry) > 0) result.locked = true;
        String rest = name;
        if (inner.length) {
            if (name.length <= inner.length + 1 || name.value[inner.length] != '/' || !StrIsEqual(StrFromTo(name, 0, (i64)inner.length), inner)) continue;
            rest = StrFrom(name, inner.length + 1);
        }
        Index slash = StrIndexChar(rest, '/', SSF_None);
        String child = slash.exists ? StrFromTo(rest, 0, (i64)slash.value) : rest;
        b32 deeper = slash.exists, isFolder = deeper || folder;
        u64 at = MapsGet(seen, u64, child);
        if (!at) {
            ArrayPush(arena, result.files, ((P_File){ .name = StrCopy(arena, child), .kind = isFolder ? P_FileKind_Directory : P_FileKind_File }));
            at = result.files.header.count;
            MapsPut(arena, seen, result.files.v[at - 1].name, at);
        }
        if (!deeper) {
            P_File *file = &result.files.v[at - 1];
            file->size = isFolder ? 0 : (u64)Max(a->size(entry), (i64)0);
            file->modifiedTimeUnix = a->mtime(entry);
            file->metadataValid = true;
            file->hidden = child.value[0] == '.';
        }
    }
    result.cancelled = P_IsCancelled(cancel);
    // A 7z or RAR one stops at what is encrypted, or lists names whose data is, which 7-Zip reads once the password is given; without 7-Zip
    // there is nothing to ask a password for.
    if (P__SevenZipKind(archive)) {
        const char *error = a->error(reader);
        b32 encrypted = result.locked || (error && (StrIndexStr(StrFromCStr(error), S("ncrypt"), SSF_None).exists || StrIndexStr(StrFromCStr(error), S("assword"), SSF_None).exists));
        Scratch found = GetScratchEx(arena);
        b32 sevenZip = encrypted && P__SevenZipProgram(found.arena).length;
        ReleaseScratch(found);
        result.locked = sevenZip;
        if (encrypted && !sevenZip) {
            result.errorCode = P__ArchiveUnsupported;
            result.errorString = StrCopy(arena, S("The archive is encrypted; 7-Zip opens it once installed."));
        }
    }
    a->free(reader);
    return result;
}

function b32 P_ArchiveCheckPassword(String archive, String password, const P_CancelToken *cancel)
{
    // A 7z or RAR one by 7-Zip: listed, which a wrong password fails where the names are encrypted, then its first encrypted file tested.
    if (P__SevenZipKind(archive)) {
        Scratch scratch = GetScratch();
        String program = P__SevenZipProgram(scratch.arena), encrypted = {0};
        P_FileArray all = {0};
        b32 opens = program.length && P__SevenZipList(scratch.arena, program, archive, password, cancel, &all, &encrypted);
        if (opens && encrypted.length) {
            String arguments[] = { program, S("t"), S("-sccUTF-8"), S("--"), archive, encrypted }, output = {0};
            opens = P_RunCommandInput(scratch.arena, P__ArchiveFolder(archive), arguments, CArrayCount(arguments), StrFormat(scratch.arena, "%S\n", password), 0, &output, cancel);
        }
        ReleaseScratch(scratch);
        return opens && !P_IsCancelled(cancel);
    }
    if (!P__LoadArchive() || !P__archive.addPassphrase || !P__archive.entryEncrypted) return false;
    P__Archive *a = &P__archive;
    Scratch scratch = GetScratch();
    void *reader = P__ArchiveRead(archive, P__ArchiveCString(scratch.arena, password)), *entry = NULL;
    String name = {0};
    b32 folder = false, result = reader != NULL, tried = false;
    while (result && !tried && P__ArchiveNext(reader, &entry, &name, &folder, cancel)) {
        if (folder || a->size(entry) <= 0 || a->entryEncrypted(entry) <= 0) continue;
        const void *block = NULL;
        u64 size = 0;
        i64 offset = 0;
        // ARCHIVE_OK or ARCHIVE_EOF.
        i32 status = a->readBlock(reader, &block, &size, &offset);
        result = status == 0 || status == 1;
        tried = true;
    }
    if (reader) a->free(reader);
    ReleaseScratch(scratch);
    return result && !P_IsCancelled(cancel);
}

// Whether `reader` stopped for an entry's password, missing or wrong.
function b32 P__ArchiveWantsPassword(void *reader)
{
    const char *error = P__archive.error(reader);
    return error && StrIndexStr(StrFromCStr(error), S("assphrase"), 0).exists;
}

// `name` numbered for attempt `n`: the name itself, then "stem (2).ext" for files and "name (2)" for folders.
function String P__NumberedString(Arena *arena, String name, b32 folder, u32 n)
{
    if (n == 1) return name;
    Index dot = folder ? (Index){0} : StrIndexChar(name, '.', SSF_SearchBackwards);
    String stem = dot.exists && dot.value ? StrFromTo(name, 0, (i64)dot.value) : name;
    String extension = dot.exists && dot.value ? StrFrom(name, dot.value) : (String){0};
    return StrFormat(arena, "%S (%u)%S", stem, n, extension);
}

// Whether a path from inside an archive stays below the folder it is taken out into: not absolute, not on a drive, and no ".." part, with
// either slash between parts, as Windows takes both.
function b32 P__ArchivePathSafe(String path)
{
    if (path.length && (path.value[0] == '/' || path.value[0] == '\\' || (path.length > 1 && path.value[1] == ':'))) return false;
    for (u64 at = 0, start = 0; at <= path.length; ++at) {
        if (at < path.length && path.value[at] != '/' && path.value[at] != '\\') continue;
        if (at - start == 2 && path.value[start] == '.' && path.value[start + 1] == '.') return false;
        start = at + 1;
    }
    return true;
}

// The progress counts the archive file read through, as the time it takes follows that more than the size of what is taken out.
// P_ExtractArchive by 7-Zip, into a folder of its own in `directory`, which goes once its items are moved to their places there.
function P_FileError P__SevenZipExtract(Arena *arena, String program, String archive, String inner, const String *names, const String *placed, u64 count,
                                        String directory, P_Progress *progress, const P_CancelToken *cancel)
{
    Scratch scratch = GetScratchEx(arena);
    String made = {0};
    P_FileError result = P_CreateItem(scratch.arena, directory, S(".filey-7z"), true, &made);
    String through = P__ArchiveJoin(scratch.arena, directory, made);
    String *arguments = ArenaPushArray(scratch.arena, count + 7, String);
    u32 used = 0;
    arguments[used++] = program, arguments[used++] = S("x"), arguments[used++] = S("-y"), arguments[used++] = S("-sccUTF-8");
    arguments[used++] = StrFormat(scratch.arena, "-o%S", through), arguments[used++] = S("--"), arguments[used++] = archive;
    for (u64 k = 0; k < count; ++k) arguments[used++] = inner.length ? StrFormat(scratch.arena, "%S/%S", inner, names[k]) : names[k];
    String output = {0};
    if (!result.code && !P_RunCommandInput(scratch.arena, P__ArchiveFolder(archive), arguments, used, StrFormat(scratch.arena, "%S\n", StrFromCStr(P__ArchivePassword(scratch.arena, archive))), 0, &output, cancel)) {
        result = P_IsCancelled(cancel) ? P__FileError(arena, P__ArchiveCancelled) : (P_FileError){ P__ArchiveFailed, StrCopy(arena, S("7-Zip could not take the items out, or the archive's password is wrong.")) };
    }
    for (u64 k = 0; !result.code && k < count; ++k) {
        String taken = inner.length ? StrFormat(scratch.arena, "%S/%S/%S", through, inner, names[k]) : P__ArchiveJoin(scratch.arena, through, names[k]);
        result = P_ReplaceFile(scratch.arena, taken, P__ArchiveJoin(scratch.arena, directory, placed[k]));
        if (!result.code) atomic_fetch_add(&progress->doneItems, 1);
    }
    if (made.length) P_DeleteItems(scratch.arena, &through, 1, false, NULL);
    ReleaseScratch(scratch);
    return result.code && !result.message.value ? P__FileError(arena, result.code) : result;
}

function P_FileError P_ExtractArchive(Arena *arena, String archive, String inner, const String *names, u64 count, String directory, P_Progress *progress,
                                      const P_CancelToken *cancel)
{
    P_Progress none = {0};
    if (!progress) progress = &none;
    Scratch scratch = GetScratchEx(arena);
    // Each item goes under its own name, or a numbered one where that is taken, as copies do.
    String *placed = ArenaPushArray(scratch.arena, Max(count, (u64)1), String);
    for (u64 k = 0; k < count; ++k) {
        for (u32 n = 1; n < 10000; ++n) {
            placed[k] = P__NumberedString(scratch.arena, names[k], false, n);
            if (!P_PathExists(StrFormat(scratch.arena, "%S/%S", directory, placed[k]))) break;
        }
    }
    String program = {0};
    if (P__SevenZipReads(scratch.arena, archive, &program)) {
        atomic_fetch_add(&progress->totalItems, count);
        P_FileError result = P__SevenZipExtract(arena, program, archive, inner, names, placed, count, directory, progress, cancel);
        ReleaseScratch(scratch);
        return result;
    }
    if (!P__LoadArchive()) {
        ReleaseScratch(scratch);
        return P__FileError(arena, P__ArchiveUnsupported);
    }
    P__Archive *a = &P__archive;
    atomic_fetch_add(&progress->totalItems, count);
    atomic_fetch_add(&progress->totalBytes, P__ArchiveFileSize(archive));
    void *reader = P__ArchiveRead(archive, NULL), *entry = NULL;
    u32 error = reader ? 0 : P__ArchiveFailed;
    u64 read = UINT64_MAX;
    String name = {0};
    b32 folder = false;
    while (!error && P__ArchiveNext(reader, &entry, &name, &folder, cancel)) {
        P__ArchiveAdvance(progress, reader, &read);
        if (!P__Proceed(progress, cancel)) break;
        String rest = name;
        if (inner.length) {
            if (name.length <= inner.length + 1 || name.value[inner.length] != '/' || !StrIsEqual(StrFromTo(name, 0, (i64)inner.length), inner)) continue;
            rest = StrFrom(name, inner.length + 1);
        }
        u64 k = 0;
        for (; k < count; ++k) {
            if (StrIsEqual(rest, names[k]) || (rest.length > names[k].length && rest.value[names[k].length] == '/' && StrIsEqual(StrFromTo(rest, 0, (i64)names[k].length), names[k]))) break;
        }
        // Links and devices are left out, and so is anything that would land outside the folder.
        u32 type = a->filetype(entry) & 0170000;
        if (k == count || !P__ArchivePathSafe(rest) || (type != 0100000 && type != 0040000)) continue;
        ArenaMark mark = ArenaGetMark(scratch.arena);
        String target = StrFormat(scratch.arena, "%S/%S%S", directory, placed[k], StrFrom(rest, names[k].length));
        P_ProgressName(progress, StrFrom(rest, StrIndexChar(rest, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value));
        if (type == 0040000) error = P__ArchiveMakeFolders(target);
        else error = P__ArchiveWriteFile(reader, target, a->mtime(entry), &read, progress, cancel);
        ArenaRestore(mark);
    }
    if (!error && P_IsCancelled(cancel)) error = P__ArchiveCancelled;
    if (!error) atomic_fetch_add(&progress->doneItems, count);
    b32 locked = error && reader && P__ArchiveWantsPassword(reader);
    if (reader) a->free(reader);
    ReleaseScratch(scratch);
    if (locked) return (P_FileError){ P__ArchiveFailed, StrCopy(arena, S("The archive's password is missing or wrong.")) };
    if (error && P__SevenZipKind(archive)) return (P_FileError){ P__ArchiveFailed, StrCopy(arena, S("The archive could not be read; an encrypted 7z or RAR one opens with 7-Zip installed.")) };
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function void P__ArchiveKindOf(String path, i32 *format, i32 *filter)
{
    local_persist const struct { String ending; i32 format, filter; } kinds[] = {
        { SInit(".zip"), P__ArchiveZip, 0 }, { SInit(".jar"), P__ArchiveZip, 0 }, { SInit(".7z"), P__Archive7z, 0 }, { SInit(".tar"), P__ArchiveTar, 0 },
        { SInit(".tgz"), P__ArchiveTar, P__FilterGzip }, { SInit(".tar.gz"), P__ArchiveTar, P__FilterGzip }, { SInit(".tbz2"), P__ArchiveTar, P__FilterBzip2 },
        { SInit(".tar.bz2"), P__ArchiveTar, P__FilterBzip2 }, { SInit(".txz"), P__ArchiveTar, P__FilterXz }, { SInit(".tar.xz"), P__ArchiveTar, P__FilterXz },
        { SInit(".tar.zst"), P__ArchiveTar, P__FilterZstd },
    };
    *format = 0;
    *filter = 0;
    for (u32 i = 0; i < CArrayCount(kinds); ++i) {
        String ending = kinds[i].ending;
        b32 same = path.length >= ending.length;
        for (u64 c = 0; c < ending.length && same; ++c) {
            char letter = path.value[path.length - ending.length + c];
            same = (letter >= 'A' && letter <= 'Z' ? (char)(letter + 32) : letter) == ending.value[c];
        }
        if (same) {
            *format = kinds[i].format;
            *filter = kinds[i].filter;
        }
    }
}

function b32 P_ArchiveWritable(String archive)
{
    i32 format = 0, filter = 0;
    P__ArchiveKindOf(archive, &format, &filter);
    return format != 0;
}

// A writer to `path` in the format and with the compression of `reader`'s archive, whose first header is read, or of what `like` is named.
function void *P__ArchiveWriter(void *reader, String path, String like, u32 level)
{
    P__Archive *a = &P__archive;
    i32 format = 0, filter = 0;
    if (reader) {
        format = a->format(reader);
        filter = a->filterCode(reader, 0);
    } else {
        P__ArchiveKindOf(like, &format, &filter);
    }
    Scratch scratch = GetScratch();
    const void *native = P__ArchiveNativePath(scratch.arena, path);
    void *writer = native && format ? a->writeNew() : NULL;
    // Every tar is written as libarchive's plain one, restricted pax, which is ustar but where a name or size needs more; ustar alone would
    // keep names outside ASCII as bytes that each system reads in its own code page.
    if ((format & 0xFF0000) == P__ArchiveTar) format = P__ArchiveTar;
    b32 ok = writer && a->writeFormat(writer, format) == 0 && (filter <= 0 || a->writeFilter(writer, filter) == 0);
    // Names go in as UTF-8 rather than in the system's code page, which has no letters for most of them; formats that always store
    // Unicode, as 7z does, have no such option and say so, which changes nothing.
    if (ok) a->writeOptions(writer, "hdrcharset=UTF-8");
    // The level for whichever of the format and its filter take one; the others say they do not, which changes nothing.
    if (ok) {
        char option[32];
        snprintf(option, sizeof(option), "compression-level=%u", Min(level, 9u));
        a->writeOptions(writer, option);
    }
    ok = ok && a->writeOpen(writer, native) == 0;
    if (!ok && writer) {
        a->writeFree(writer);
        writer = NULL;
    }
    ReleaseScratch(scratch);
    return writer;
}

// The progress counts the archive file read through as it is copied over, then the items added.
function P_FileError P_AddToArchive(Arena *arena, String archive, String inner, const String *paths, u64 count, u32 level, P_Progress *progress, const P_CancelToken *cancel)
{
    if (!P__LoadArchive() || !P__archive.writable) return P__FileError(arena, P__ArchiveUnsupported);
    P__Archive *a = &P__archive;
    P_Progress none = {0};
    if (!progress) progress = &none;
    Scratch scratch = GetScratchEx(arena);
    // Where each item goes inside, and the new archive beside the old one until it is complete.
    String *names = ArenaPushArray(scratch.arena, Max(count, (u64)1), String);
    for (u64 k = 0; k < count; ++k) {
        String leaf = StrFrom(paths[k], StrIndexChar(paths[k], '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
        names[k] = inner.length ? StrFormat(scratch.arena, "%S/%S", inner, leaf) : leaf;
    }
    atomic_fetch_add(&progress->totalItems, count);
    atomic_fetch_add(&progress->totalBytes, P__ArchiveFileSize(archive));
    u32 error = 0;
    for (u64 k = 0; k < count && !error; ++k) error = P__ArchiveWritePath(NULL, paths[k], names[k], progress, cancel);
    Index slash = StrIndexChar(archive, '/', SSF_SearchBackwards);
    String part = slash.exists ? StrFormat(scratch.arena, "%S/.%S.part", StrFromTo(archive, 0, (i64)slash.value), StrFrom(archive, slash.value + 1)) : (String){0};
    // An archive not there yet is made, with nothing to copy over.
    b32 existing = P_PathExists(archive);
    void *reader = error || !existing ? NULL : P__ArchiveRead(archive, NULL), *writer = NULL, *entry = NULL;
    if (!error && ((existing && !reader) || !part.length)) error = P__ArchiveFailed;
    u64 read = UINT64_MAX;
    String name = {0};
    b32 folder = false;
    // Everything already there is copied over, but for what the new items replace.
    while (!error && reader && P__ArchiveNext(reader, &entry, &name, &folder, cancel)) {
        if (!writer && !(writer = P__ArchiveWriter(reader, part, archive, level))) error = P__ArchiveUnsupported;
        b32 replaced = false;
        for (u64 k = 0; k < count && !replaced; ++k) {
            replaced = StrIsEqual(name, names[k]) || (name.length > names[k].length && name.value[names[k].length] == '/' && StrIsEqual(StrFromTo(name, 0, (i64)names[k].length), names[k]));
        }
        if (error || replaced) continue;
        if (a->writeHeader(writer, entry) < P__ArchiveWarn) error = P__ArchiveFailed;
        const void *block = NULL;
        u64 size = 0;
        i64 offset = 0;
        while (!error && a->readBlock(reader, &block, &size, &offset) == 0) {
            if (a->writeData(writer, block, size) != (i64)size) error = P__ArchiveFailed;
            P__ArchiveAdvance(progress, reader, &read);
            if (!P__Proceed(progress, cancel)) error = P__ArchiveCancelled;
        }
        P__ArchiveAdvance(progress, reader, &read);
    }
    if (!error && P_IsCancelled(cancel)) error = P__ArchiveCancelled;
    if (!error && !writer && !(writer = P__ArchiveWriter(NULL, part, archive, level))) error = P__ArchiveUnsupported;
    for (u64 k = 0; k < count && !error; ++k) error = P__ArchiveWritePath(writer, paths[k], names[k], progress, cancel);
    if (writer && a->writeClose(writer) != 0 && !error) error = P__ArchiveFailed;
    if (writer) a->writeFree(writer);
    if (reader) a->free(reader);
    // The new archive takes the old one's place in one step.
    P_FileError result = {0};
    if (!error) result = P_ReplaceFile(arena, part, archive);
    else result = P__FileError(arena, error);
    if (result.code && writer) P__ArchiveRemove(part);
    if (!result.code) atomic_fetch_add(&progress->doneItems, count);
    ReleaseScratch(scratch);
    return result;
}
