// libarchive, which reads and writes archives on both platforms: Linux loads the system's, Windows its own copy, archiveint.dll, which its
// tar and Explorer use. It is loaded when first needed, so that it is not a build dependency. Each platform's files include this after giving
// it what differs there: P__ArchiveSymbol, which loads the library, P__ArchiveNativePath with the names of the open functions that take such
// paths, and the error codes P__ArchiveCancelled, P__ArchiveUnsupported and P__ArchiveFailed. They define P__ArchiveWritePath after it.
function u32 P__ArchiveWritePath(void *writer, String path, String name, const P_CancelToken *cancel);

// Writing, which adds to archives, is loaded with the rest and only needed then.
typedef struct P__Archive {
    b32 tried, loaded, writable;
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
} P__Archive;
global P__Archive P__archive;

function b32 P__LoadArchive(void)
{
    P__Archive *a = &P__archive;
    if (a->tried) return a->loaded;
    a->tried = true;
    *(void **)&a->readNew = P__ArchiveSymbol("archive_read_new");
    if (!a->readNew) return false;
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
    a->loaded = a->readNew && a->supportFilterAll && a->supportFormatAll && a->openFilename && a->nextHeader && a->pathname && a->size && a->filetype && a->free &&
                a->mtime && a->readBlock && a->error && a->format && a->filterCode;
    a->writable = a->loaded && a->writeNew && a->writeFormat && a->writeFilter && a->writeOptions && a->writeOpen && a->writeHeader && a->writeData && a->writeClose && a->writeFree &&
                  a->entryNew && a->entryFree && a->entrySetPath && a->entryCopyStat && a->entrySetFiletype && a->entrySetPerm && a->entrySetSize && a->entrySetMtime;
    return a->loaded;
}

// A reader of the archive at `path`, open, or NULL.
function void *P__ArchiveRead(String path)
{
    P__Archive *a = &P__archive;
    Scratch scratch = GetScratch();
    const void *native = P__ArchiveNativePath(scratch.arena, path);
    void *reader = native ? a->readNew() : NULL;
    if (reader) {
        a->supportFilterAll(reader);
        a->supportFormatAll(reader);
        if (a->openFilename(reader, native, 65536) != 0) {
            a->free(reader);
            reader = NULL;
        }
    }
    ReleaseScratch(scratch);
    return reader;
}

// The next entry's path inside the archive, without "./" before it or '/' after a folder's, with whether it is a folder; false at the end.
// ARCHIVE_OK is 0 and ARCHIVE_WARN -20, which still gives the entry.
enum { P__ArchiveWarn = -20 };
function b32 P__ArchiveNext(void *reader, void **entry, String *name, b32 *folder)
{
    P__Archive *a = &P__archive;
    i32 status = 0;
    while ((status = a->nextHeader(reader, entry)) == 0 || status == P__ArchiveWarn) {
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

// libarchive's codes for the formats and filters it writes, and what an archive's name says it is when it has nothing in it to tell.
enum { P__ArchiveTar = 0x30000, P__ArchiveZip = 0x50000, P__Archive7z = 0xE0000 };
enum { P__FilterGzip = 1, P__FilterBzip2 = 2, P__FilterXz = 6, P__FilterZstd = 14 };

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
function void *P__ArchiveWriter(void *reader, String path, String like)
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
    ok = ok && a->writeOpen(writer, native) == 0;
    if (!ok && writer) {
        a->writeFree(writer);
        writer = NULL;
    }
    ReleaseScratch(scratch);
    return writer;
}

function P_FileError P_AddToArchive(Arena *arena, String archive, String inner, const String *paths, u64 count, const P_CancelToken *cancel)
{
    if (!P__LoadArchive() || !P__archive.writable) return P__FileError(arena, P__ArchiveUnsupported);
    P__Archive *a = &P__archive;
    Scratch scratch = GetScratchEx(arena);
    // Where each item goes inside, and the new archive beside the old one until it is complete.
    String *names = ArenaPushArray(scratch.arena, Max(count, (u64)1), String);
    for (u64 k = 0; k < count; ++k) {
        String leaf = StrFrom(paths[k], StrIndexChar(paths[k], '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
        names[k] = inner.length ? StrFormat(scratch.arena, "%S/%S", inner, leaf) : leaf;
    }
    Index slash = StrIndexChar(archive, '/', SSF_SearchBackwards);
    String part = slash.exists ? StrFormat(scratch.arena, "%S/.%S.part", StrFromTo(archive, 0, (i64)slash.value), StrFrom(archive, slash.value + 1)) : (String){0};
    void *reader = P__ArchiveRead(archive), *writer = NULL, *entry = NULL;
    u32 error = reader && part.length ? 0 : P__ArchiveFailed;
    String name = {0};
    b32 folder = false;
    // Everything already there is copied over, but for what the new items replace.
    while (!error && P__ArchiveNext(reader, &entry, &name, &folder)) {
        if (!writer && !(writer = P__ArchiveWriter(reader, part, archive))) error = P__ArchiveUnsupported;
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
        }
        if (P_IsCancelled(cancel)) error = P__ArchiveCancelled;
    }
    if (!error && !writer && !(writer = P__ArchiveWriter(NULL, part, archive))) error = P__ArchiveUnsupported;
    for (u64 k = 0; k < count && !error; ++k) error = P__ArchiveWritePath(writer, paths[k], names[k], cancel);
    if (writer && a->writeClose(writer) != 0 && !error) error = P__ArchiveFailed;
    if (writer) a->writeFree(writer);
    if (reader) a->free(reader);
    // The new archive takes the old one's place in one step.
    P_FileError result = {0};
    if (!error) result = P_ReplaceFile(arena, part, archive);
    else result = P__FileError(arena, error);
    if (result.code && writer) P_DeleteItems(scratch.arena, &part, 1, false, NULL);
    ReleaseScratch(scratch);
    return result;
}
