// Requires WinPlatform.c for the Win32 declarations.

#pragma push_macro("function")
#undef function
#include <shellapi.h>
#include <objbase.h>
#pragma pop_macro("function")

function wchar *P__FilePathWide(Arena *arena, String path)
{
    if (path.length > INT32_MAX || StrIndexChar(path, '\0', SSF_None).exists) {
        SetLastError(ERROR_INVALID_NAME);
        return NULL;
    }
    i32 length = path.length ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.value, (i32)path.length, NULL, 0) : 0;
    if (path.length && !length) {
        return NULL;
    }
    wchar *result = ArenaPushArray(arena, (u64)length + 1, wchar);
    if (length) {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.value, (i32)path.length, result, length);
    }
    for (i32 i = 0; i < length; ++i) {
        if (result[i] == L'/') result[i] = L'\\';
    }
    return result;
}

function String P__FileStringWide(Arena *arena, const wchar *text, i32 length)
{
    i32 count = WideCharToMultiByte(CP_UTF8, 0, text, length, NULL, 0, NULL, NULL);
    char *bytes = ArenaPushArray(arena, (u64)count + 1, char);
    if (count) WideCharToMultiByte(CP_UTF8, 0, text, length, bytes, count, NULL, NULL);
    return StrMake(bytes, (u64)count);
}

function P_FileError P__FileError(Arena *arena, u32 code)
{
    wchar message[512] = {0};
    u32 length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, message, (u32)CArrayCount(message), NULL);
    while (length && (message[length - 1] == L'\r' || message[length - 1] == L'\n')) --length;
    String text = length ? P__FileStringWide(arena, message, (i32)length) : StrFormat(arena, "System error %u", code);
    return (P_FileError){.code = code, .message = text};
}

// The libarchive Windows ships for its tar and Explorer, taken only from the system folder.
function void *P__ArchiveSymbol(const char *name)
{
    local_persist b32 tried;
    local_persist HMODULE library;
    if (!tried) {
        tried = true;
        library = LoadLibraryExW(L"archiveint.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    return library ? (void *)GetProcAddress(library, name) : NULL;
}

function const void *P__ArchiveNativePath(Arena *arena, String path) { return P__FilePathWide(arena, path); }

#define P__ArchiveOpenReadName "archive_read_open_filename_w"
#define P__ArchiveOpenWriteName "archive_write_open_filename_w"
enum { P__ArchiveCancelled = ERROR_CANCELLED, P__ArchiveUnsupported = ERROR_NOT_SUPPORTED, P__ArchiveFailed = ERROR_WRITE_FAULT };

#include "Platform/PlatformArchive.c"

// Linked folders are left out, as they can lead in circles.
function u32 P__ArchiveWritePath(void *writer, String path, String name, P_Progress *progress, const P_CancelToken *cancel)
{
    P__Archive *a = &P__archive;
    if (P_IsCancelled(cancel)) return ERROR_CANCELLED;
    Scratch scratch = GetScratch();
    wchar *native = P__FilePathWide(scratch.arena, path);
    char *inside = ArenaPushArray(scratch.arena, name.length + 1, char);
    MemCopy(inside, name.value, name.length);
    WIN32_FILE_ATTRIBUTE_DATA info = {0};
    u32 error = native && GetFileAttributesExW(native, GetFileExInfoStandard, &info) ? 0 : GetLastError();
    b32 folder = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    u64 bytes = ((u64)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    if (!error && !folder && !writer) atomic_fetch_add(&progress->totalBytes, bytes);
    if (!error && !(folder && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))) {
        HANDLE file = folder || !writer ? INVALID_HANDLE_VALUE : CreateFileW(native, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                                                          FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (!folder && writer && file == INVALID_HANDLE_VALUE) error = GetLastError();
        if (!error && writer) {
            u64 ticks = ((u64)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
            void *entry = a->entryNew();
            a->entrySetPath(entry, inside);
            a->entrySetFiletype(entry, folder ? 0040000 : 0100000);
            a->entrySetPerm(entry, folder ? 0755 : 0644);
            a->entrySetSize(entry, folder ? 0 : (i64)bytes);
            a->entrySetMtime(entry, (i64)(ticks / 10000000) - INT64_C(11644473600), (i64)(ticks % 10000000) * 100);
            if (a->writeHeader(writer, entry) < P__ArchiveWarn) error = ERROR_WRITE_FAULT;
            a->entryFree(entry);
        }
        if (file != INVALID_HANDLE_VALUE) {
            P__ProgressName(progress, StrFrom(name, StrIndexChar(name, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value));
            u8 *buffer = ArenaPushArrayNoZero(scratch.arena, 65536, u8);
            for (DWORD got = 0; !error;) {
                if (!ReadFile(file, buffer, 65536, &got, NULL)) error = GetLastError();
                else if (!got) break;
                else if (a->writeData(writer, buffer, got) != (i64)got) error = ERROR_WRITE_FAULT;
                atomic_fetch_add(&progress->doneBytes, (u64)got);
                if (!error && !P__Proceed(progress, cancel)) error = ERROR_CANCELLED;
            }
            CloseHandle(file);
        } else if (!error && folder) {
            P_DirectoryResult children = P_EnumerateDirectory(scratch.arena, path, cancel);
            error = children.cancelled ? ERROR_CANCELLED : children.errorCode;
            for (u64 i = 0; i < children.files.header.count && !error; ++i) {
                String child = children.files.v[i].name;
                error = P__ArchiveWritePath(writer, StrFormat(scratch.arena, "%S/%S", path, child), StrFormat(scratch.arena, "%S/%S", name, child), progress, cancel);
            }
        }
    }
    ReleaseScratch(scratch);
    return error;
}

// Each folder up to `path` is made in turn where it is missing; the last one tells whether all are there.
function u32 P__ArchiveMakeFolders(String path)
{
    Scratch scratch = GetScratch();
    u32 error = 0;
    for (u64 at = 1; at <= path.length; ++at) {
        if (at < path.length && path.value[at] != '/') continue;
        wchar *wide = P__FilePathWide(scratch.arena, StrFromTo(path, 0, (i64)at));
        error = !wide ? ERROR_INVALID_NAME : CreateDirectoryW(wide, NULL) || GetLastError() == ERROR_ALREADY_EXISTS ? 0 : GetLastError();
    }
    ReleaseScratch(scratch);
    return error;
}

// Written beside its place and moved there once complete, so that a file cut short never takes the name.
function u32 P__ArchiveWriteFile(void *reader, String path, i64 modified, u64 *read, P_Progress *progress, const P_CancelToken *cancel)
{
    P__Archive *a = &P__archive;
    Scratch scratch = GetScratch();
    String part = StrFormat(scratch.arena, "%S.part", path);
    Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
    u32 error = slash.exists && slash.value ? P__ArchiveMakeFolders(StrFromTo(path, 0, (i64)slash.value)) : 0;
    wchar *wide = P__FilePathWide(scratch.arena, part);
    HANDLE file = !error && wide ? CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (!error && file == INVALID_HANDLE_VALUE) error = wide ? GetLastError() : ERROR_INVALID_NAME;
    const void *block = NULL;
    u64 size = 0;
    i64 offset = 0;
    i32 status = 0;
    while (file != INVALID_HANDLE_VALUE && !error && (status = a->readBlock(reader, &block, &size, &offset)) == 0) {
        // A sparse entry's blocks start past the end of what is written.
        LARGE_INTEGER at = { .QuadPart = offset };
        DWORD wrote = 0;
        if (!SetFilePointerEx(file, at, NULL, FILE_BEGIN) || !WriteFile(file, block, (DWORD)size, &wrote, NULL) || wrote != size) error = GetLastError();
        P__ArchiveAdvance(progress, reader, read);
        if (!error && !P__Proceed(progress, cancel)) error = ERROR_CANCELLED;
    }
    if (!error && status < 0) error = ERROR_READ_FAULT;
    if (file != INVALID_HANDLE_VALUE) {
        u64 ticks = (u64)(modified + INT64_C(11644473600)) * 10000000;
        FILETIME time = { (DWORD)ticks, (DWORD)(ticks >> 32) };
        if (modified > 0) SetFileTime(file, NULL, NULL, &time);
        CloseHandle(file);
    }
    if (!error) error = P_ReplaceFile(scratch.arena, part, path).code;
    if (error && wide) DeleteFileW(wide);
    ReleaseScratch(scratch);
    return error;
}

function u64 P__ArchiveFileSize(String path)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FilePathWide(scratch.arena, path);
    WIN32_FILE_ATTRIBUTE_DATA info = {0};
    u64 result = wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &info) ? ((u64)info.nFileSizeHigh << 32) | info.nFileSizeLow : 0;
    ReleaseScratch(scratch);
    return result;
}

function void P__ArchiveRemove(String path)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FilePathWide(scratch.arena, path);
    if (wide) DeleteFileW(wide);
    ReleaseScratch(scratch);
}

function String P_AbsolutePath(Arena *arena, String path)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *input = P__FilePathWide(scratch.arena, path.length ? path : S("."));
    String result = {0};
    if (input) {
        u32 capacity = GetFullPathNameW(input, 0, NULL, NULL);
        if (capacity) {
            wchar *fullPath = ArenaPushArray(scratch.arena, (u64)capacity + 1, wchar);
            u32 length = GetFullPathNameW(input, capacity + 1, fullPath, NULL);
            if (length && length <= capacity) {
                result = P__FileStringWide(arena, fullPath, (i32)length);
                for (u64 i = 0; i < result.length; ++i) {
                    if (result.value[i] == '\\') ((char *)result.value)[i] = '/';
                }
            }
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function String P_FormatFileDate(Arena *arena, i64 timeUnix)
{
    i64 ticks = 0;
    if (ckd_add(&ticks, timeUnix, INT64_C(11644473600)) || ticks < 0 || ckd_mul(&ticks, ticks, INT64_C(10000000))) return (String){0};
    FILETIME time = {.dwLowDateTime = (u32)((u64)ticks & UINT32_MAX), .dwHighDateTime = (u32)((u64)ticks >> 32)};
    SYSTEMTIME utc = {0};
    SYSTEMTIME local = {0};
    if (!FileTimeToSystemTime(&time, &utc) || !SystemTimeToTzSpecificLocalTime(NULL, &utc, &local) || local.wYear > 9999) return (String){0};
    return StrFormat(arena, "%04u-%02u-%02u", (u32)local.wYear, (u32)local.wMonth, (u32)local.wDay);
}

function i64 P_NowUnix(void)
{
    FILETIME now = {0};
    GetSystemTimeAsFileTime(&now);
    u64 ticks = ((u64)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return (i64)(ticks / 10000000) - INT64_C(11644473600);
}

function String P_FormatFileTime(Arena *arena, i64 modifiedTimeUnix)
{
    i64 ticks = 0;
    if (ckd_add(&ticks, modifiedTimeUnix, INT64_C(11644473600)) || ticks < 0 || ckd_mul(&ticks, ticks, INT64_C(10000000))) return (String){0};
    FILETIME time = {.dwLowDateTime = (u32)((u64)ticks & UINT32_MAX), .dwHighDateTime = (u32)((u64)ticks >> 32)};
    SYSTEMTIME utc = {0};
    SYSTEMTIME local = {0};
    if (!FileTimeToSystemTime(&time, &utc) || !SystemTimeToTzSpecificLocalTime(NULL, &utc, &local) || local.wYear > 9999) return (String){0};
    // The user's short date and time without seconds, as Explorer shows them.
    wchar date[64], clock[64];
    i32 dateLength = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, NULL, date, (i32)CArrayCount(date), NULL);
    i32 clockLength = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, NULL, clock, (i32)CArrayCount(clock));
    if (dateLength <= 1 || clockLength <= 1) {
        return StrFormat(arena, "%04u-%02u-%02u %02u:%02u", (u32)local.wYear, (u32)local.wMonth, (u32)local.wDay, (u32)local.wHour, (u32)local.wMinute);
    }
    // Newer locales put a narrow no-break space before AM and PM; a plain one needs no glyph of its own.
    for (i32 i = 0; i < clockLength; ++i) if (clock[i] == 0x202F || clock[i] == 0x00A0) clock[i] = ' ';
    Scratch scratch = GetScratchEx(arena);
    String result = StrFormat(arena, "%S %S", P__FileStringWide(scratch.arena, date, dateLength - 1), P__FileStringWide(scratch.arena, clock, clockLength - 1));
    ReleaseScratch(scratch);
    return result;
}

function wchar *P__FileExtendedPath(Arena *arena, String path)
{
    String absolute = P_AbsolutePath(arena, path);
    if (!absolute.length) return NULL;
    wchar *wide = P__FilePathWide(arena, absolute);
    if (!wide || (absolute.length >= 4 && wide[0] == L'\\' && wide[1] == L'\\' && wide[2] == L'?' && wide[3] == L'\\')) return wide;
    b32 unc = wide[0] == L'\\' && wide[1] == L'\\';
    u64 length = 0;
    while (wide[length]) ++length;
    u64 prefixLength = unc ? 8 : 4;
    wchar *result = ArenaPushArray(arena, prefixLength + length + 1, wchar);
    const wchar *prefix = unc ? L"\\\\?\\UNC\\" : L"\\\\?\\";
    MemCopy(result, prefix, prefixLength * sizeof(wchar));
    MemCopy(result + prefixLength, wide + (unc ? 2 : 0), (length - (unc ? 2 : 0)) * sizeof(wchar));
    return result;
}

function P_DirectoryResult P_EnumerateDirectory(Arena *arena, String path, const P_CancelToken *cancel)
{
    P_DirectoryResult result = {0};
    if (P_IsCancelled(cancel)) {
        result.cancelled = true;
        return result;
    }
    ArenaMark mark = ArenaGetMark(arena);
    Scratch scratch = GetScratchEx(arena);
    wchar *directory = P__FileExtendedPath(scratch.arena, path);
    u32 error = directory ? 0 : GetLastError();
    HANDLE find = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW data = {0};
    if (directory) {
        u64 length = 0;
        while (directory[length]) ++length;
        wchar *pattern = ArenaPushArray(scratch.arena, length + 3, wchar);
        MemCopy(pattern, directory, length * sizeof(wchar));
        if (length && pattern[length - 1] != L'\\') pattern[length++] = L'\\';
        pattern[length] = L'*';
        find = FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND) {
                u32 attributes = GetFileAttributesW(directory);
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) error = 0;
            }
        }
    }
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (P_IsCancelled(cancel)) {
                result.cancelled = true;
                break;
            }
            if (data.cFileName[0] == L'.' && (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2]))) continue;
            i32 nameLength = 0;
            while (data.cFileName[nameLength]) ++nameLength;
            u64 time = ((u64)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
            u64 created = ((u64)data.ftCreationTime.dwHighDateTime << 32) | data.ftCreationTime.dwLowDateTime;
            u64 accessed = ((u64)data.ftLastAccessTime.dwHighDateTime << 32) | data.ftLastAccessTime.dwLowDateTime;
            P_File file = {
                .name = P__FileStringWide(arena, data.cFileName, nameLength),
                .size = ((u64)data.nFileSizeHigh << 32) | data.nFileSizeLow,
                .modifiedTimeUnix = (i64)(time / UINT64_C(10000000)) - INT64_C(11644473600),
                .createdTimeUnix = created ? (i64)(created / UINT64_C(10000000)) - INT64_C(11644473600) : 0,
                .accessedTimeUnix = accessed ? (i64)(accessed / UINT64_C(10000000)) - INT64_C(11644473600) : 0,
                .kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? P_FileKind_Directory : (data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) ? P_FileKind_Other : P_FileKind_File,
                .hidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0 || data.cFileName[0] == L'.',
                .system = (data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0,
                .metadataValid = true,
                .symbolicLink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
            };
            ArrayPush(arena, result.files, file);
        } while (FindNextFileW(find, &data));
        if (!result.cancelled && GetLastError() != ERROR_NO_MORE_FILES) error = GetLastError();
        FindClose(find);
    }
    if (P_IsCancelled(cancel)) result.cancelled = true;
    // The system marks a drive's root hidden, which no listing shows it as.
    if (!error && !result.cancelled) {
        u64 end = path.length;
        while (end > 0 && path.value[end - 1] == '/') --end;
        u64 start = end;
        while (start > 0 && path.value[start - 1] != '/') --start;
        String leaf = StrFromTo(path, start, (i64)end);
        u32 attributes = GetFileAttributesW(directory);
        result.hidden = leaf.length && leaf.value[leaf.length - 1] != ':' &&
                        (leaf.value[0] == '.' || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_HIDDEN)));
    }
    ReleaseScratch(scratch);
    if (error || result.cancelled) {
        ArenaRestore(mark);
        result.files = (P_FileArray){0};
        if (error && !result.cancelled) {
            P_FileError status = P__FileError(arena, error);
            result.errorCode = status.code;
            result.errorString = status.message;
        }
    }
    return result;
}

// Separators would turn a rename into a move.
function b32 P__ValidFileName(String name)
{
    return name.length && !StrIsEqual(name, S(".")) && !StrIsEqual(name, S("..")) &&
           !StrIndexChar(name, '/', SSF_None).exists && !StrIndexChar(name, '\\', SSF_None).exists;
}

function P_FileError P_RenamePath(Arena *arena, String directory, String from, String to)
{
    if (!P__ValidFileName(from) || !P__ValidFileName(to)) return P__FileError(arena, ERROR_INVALID_NAME);
    Scratch scratch = GetScratchEx(arena);
    wchar *source = P__FilePathWide(scratch.arena, StrFormat(scratch.arena, "%S/%S", directory, from));
    wchar *target = P__FilePathWide(scratch.arena, StrFormat(scratch.arena, "%S/%S", directory, to));
    // Without MOVEFILE_REPLACE_EXISTING an existing target fails with ERROR_ALREADY_EXISTS.
    u32 error = !source || !target ? GetLastError() : MoveFileExW(source, target, 0) ? 0 : GetLastError();
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

typedef struct P__Watch {
    HANDLE change;
    HANDLE stop;
    HANDLE thread;
    atomic_bool changed;
    P_JobWakeCallback *wake;
    void *user;
} P__Watch;

function DWORD WINAPI P__WatchThread(void *user)
{
    P__Watch *watch = user;
    HANDLE handles[2] = { watch->stop, watch->change };
    while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        // Only the first change since the program last looked needs a wakeup.
        if (!atomic_exchange(&watch->changed, true) && watch->wake) watch->wake(watch->user);
        if (!FindNextChangeNotification(watch->change)) break;
    }
    return 0;
}

function b32 P_DirectoryWatchStart(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user)
{
    P_DirectoryWatchStop(watch);
    Scratch scratch = GetScratch();
    wchar *wide = P__FilePathWide(scratch.arena, path);
    P__Watch *state = ArenaPushStruct(&watch->arena, P__Watch);
    *state = (P__Watch){ .wake = wake, .user = user };
    state->change = wide ? FindFirstChangeNotificationW(wide, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                                                     FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE) : INVALID_HANDLE_VALUE;
    ReleaseScratch(scratch);
    state->stop = state->change != INVALID_HANDLE_VALUE ? CreateEventW(NULL, TRUE, FALSE, NULL) : NULL;
    state->thread = state->stop ? CreateThread(NULL, 0, P__WatchThread, state, 0, NULL) : NULL;
    watch->native = state;
    if (!state->thread) {
        P_DirectoryWatchStop(watch);
        return false;
    }
    return true;
}

function b32 P_DirectoryWatchTakeChanged(P_DirectoryWatch *watch)
{
    P__Watch *state = watch->native;
    return state && atomic_exchange(&state->changed, false);
}

function void P_DirectoryWatchStop(P_DirectoryWatch *watch)
{
    P__Watch *state = watch->native;
    if (state) {
        if (state->thread) {
            SetEvent(state->stop);
            WaitForSingleObject(state->thread, INFINITE);
            CloseHandle(state->thread);
        }
        if (state->stop) CloseHandle(state->stop);
        if (state->change != INVALID_HANDLE_VALUE) FindCloseChangeNotification(state->change);
    }
    ArenaRelease(&watch->arena);
    *watch = (P_DirectoryWatch){0};
}

function b32 P_CountFolder(Arena *arena, String path, P_DirectoryTotals *totals, ArrayStr *subfolders)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *folder = P__FileExtendedPath(scratch.arena, path);
    u32 length = 0;
    while (folder && folder[length]) ++length;
    wchar *pattern = ArenaPushArray(scratch.arena, length + 3, wchar);
    for (u32 i = 0; i < length; ++i) pattern[i] = folder[i];
    if (length && pattern[length - 1] != L'\\') pattern[length++] = L'\\';
    pattern[length] = L'*';
    WIN32_FIND_DATAW data;
    HANDLE find = folder ? FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH) : INVALID_HANDLE_VALUE;
    b32 result = find != INVALID_HANDLE_VALUE;
    String parent = path.length && path.value[path.length - 1] == '/' ? StrFromTo(path, 0, (i64)path.length - 1) : path;
    if (result) {
        do {
            const wchar *name = data.cFileName;
            if (name[0] == L'.' && (!name[1] || (name[1] == L'.' && !name[2]))) continue;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                ++totals->folders;
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    String leaf = P__FileStringWide(scratch.arena, name, lstrlenW(name));
                    ArrayPush(arena, *subfolders, StrFormat(arena, "%S/%S", parent, leaf));
                }
            } else {
                ++totals->files;
                totals->bytes += ((u64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P_IsVirtualFolder(String path) { return false; }

// The shell's This PC folder, by its class id.
function String P_ComputerIconPath(void) { return S("::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"); }

function i64 P_FileCreatedTime(String path)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FileExtendedPath(scratch.arena, path);
    WIN32_FILE_ATTRIBUTE_DATA data = {0};
    b32 found = wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &data);
    ReleaseScratch(scratch);
    if (!found) return 0;
    u64 time = ((u64)data.ftCreationTime.dwHighDateTime << 32) | data.ftCreationTime.dwLowDateTime;
    return (i64)(time / UINT64_C(10000000)) - INT64_C(11644473600);
}

function String P_NativePath(Arena *arena, String path)
{
    String result = StrCopy(arena, path);
    for (u64 i = 0; i < result.length; ++i) {
        if (result.value[i] == '/') ((char *)result.value)[i] = '\\';
    }
    return result;
}

// Whether `path` is `folder` or inside it, ignoring case and separator style as Windows does.
function b32 P__PathWithin(String path, String folder)
{
    while (folder.length && (folder.value[folder.length - 1] == '/' || folder.value[folder.length - 1] == '\\')) --folder.length;
    if (path.length < folder.length) return false;
    for (u64 i = 0; i < folder.length; ++i) {
        u8 a = (u8)path.value[i], b = (u8)folder.value[i];
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return path.length == folder.length || path.value[folder.length] == '/' || path.value[folder.length] == '\\';
}

function P_FileError P_CreateItem(Arena *arena, String directory, String name, b32 folder, String *created)
{
    *created = (String){0};
    if (!P__ValidFileName(name)) return P__FileError(arena, ERROR_INVALID_NAME);
    Scratch scratch = GetScratchEx(arena);
    // Files number before their extension, folders after the whole name.
    Index dot = folder ? (Index){0} : StrIndexChar(name, '.', SSF_SearchBackwards);
    String stem = dot.exists && dot.value ? StrFromTo(name, 0, (i64)dot.value) : name;
    String extension = dot.exists && dot.value ? StrFrom(name, dot.value) : (String){0};
    u32 error = ERROR_ALREADY_EXISTS;
    for (u32 n = 1; n < 10000 && error == ERROR_ALREADY_EXISTS; ++n) {
        String candidate = n == 1 ? name : StrFormat(scratch.arena, "%S (%u)%S", stem, n, extension);
        wchar *path = P__FilePathWide(scratch.arena, StrFormat(scratch.arena, "%S/%S", directory, candidate));
        if (!path) {
            error = GetLastError();
        } else if (folder) {
            error = CreateDirectoryW(path, NULL) ? 0 : GetLastError();
        } else {
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            error = file != INVALID_HANDLE_VALUE ? 0 : GetLastError();
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            if (error == ERROR_FILE_EXISTS) error = ERROR_ALREADY_EXISTS;
        }
        if (!error) *created = StrCopy(arena, candidate);
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

// The shell's file operation, which brings the Recycle Bin, renaming on collision and a progress window for long copies.
// Errors come back as codes instead of dialogs.
// `path`'s place in `directory` under its own name.
function String P__PlacedPath(Arena *arena, String directory, String path)
{
    Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
    Index backslash = StrIndexChar(path, '\\', SSF_SearchBackwards);
    u64 cut = Max(slash.exists ? slash.value + 1 : 0, backslash.exists ? backslash.value + 1 : 0);
    b32 separated = directory.length && (directory.value[directory.length - 1] == '/' || directory.value[directory.length - 1] == '\\');
    return StrFormat(arena, separated ? "%S%S" : "%S/%S", directory, StrFrom(path, cut));
}

// `placed`, for copies and moves, gets each item's new path: in `directory` under its own name, or under the one the shell gave it when that was taken.
// Set by WinWindow.c; dialogs that verbs open are owned by it.
global HWND P__shellOwner;

function P_FileError P__ShellFileOperation(Arena *arena, u32 operation, const String *paths, u64 count, String directory, u16 flags, HWND owner, String *placed)
{
    Scratch scratch = GetScratchEx(arena);
    // Both lists end with an extra null.
    u64 length = 1;
    wchar **wide = ArenaPushArray(scratch.arena, Max(count, (u64)1), wchar *);
    u32 error = 0;
    for (u64 i = 0; i < count && !error; ++i) {
        wide[i] = P__FilePathWide(scratch.arena, paths[i]);
        if (!wide[i]) error = GetLastError();
        else length += (u64)lstrlenW(wide[i]) + 1;
    }
    wchar *from = ArenaPushArray(scratch.arena, length, wchar);
    for (u64 i = 0, at = 0; i < count && !error; ++i) {
        u64 size = (u64)lstrlenW(wide[i]) + 1;
        MemCopy(from + at, wide[i], size * sizeof(wchar));
        at += size;
    }
    wchar *to = NULL;
    if (!error && directory.length) {
        wchar *target = P__FilePathWide(scratch.arena, directory);
        if (!target) error = GetLastError();
        u64 size = target ? (u64)lstrlenW(target) : 0;
        to = ArenaPushArray(scratch.arena, size + 2, wchar);
        if (target) MemCopy(to, target, size * sizeof(wchar));
    }
    if (!error && count) {
        SHFILEOPSTRUCTW op = { .hwnd = owner, .wFunc = operation, .pFrom = from, .pTo = to, .fFlags = (FILEOP_FLAGS)(flags | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR | (placed ? FOF_WANTMAPPINGHANDLE : 0)) };
        i32 result = SHFileOperationW(&op);
        error = result ? (u32)result : op.fAnyOperationsAborted ? ERROR_CANCELLED : 0;
        for (u64 i = 0; placed && i < count; ++i) placed[i] = P__PlacedPath(arena, directory, paths[i]);
        if (placed && op.hNameMappings) {
            // The handle is a count followed by the mappings, each from the item's old path to its new one.
            struct { UINT count; SHNAMEMAPPINGW *mappings; } *names = op.hNameMappings;
            for (UINT n = 0; n < names->count; ++n) {
                const SHNAMEMAPPINGW *mapping = &names->mappings[n];
                String old = P__FileStringWide(scratch.arena, mapping->pszOldPath, mapping->cchOldPath);
                for (u64 i = 0; i < count; ++i) {
                    if (!P__PathWithin(old, paths[i]) || !P__PathWithin(paths[i], old)) {
                        if (!P__PathWithin(old, placed[i]) || !P__PathWithin(placed[i], old)) continue;
                    }
                    String fresh = P__FileStringWide(arena, mapping->pszNewPath, mapping->cchNewPath);
                    for (u64 c = 0; c < fresh.length; ++c) if (fresh.value[c] == '\\') ((char *)fresh.value)[c] = '/';
                    placed[i] = fresh;
                }
            }
            SHFreeNameMappings(op.hNameMappings);
        }
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P_TransferItems(Arena *arena, const String *paths, u64 count, String directory, b32 move, P_Progress *progress, ArrayStr *placed, const P_CancelToken *cancel)
{
    // The shell's own windows show the progress and ask about taken names.
    (void)progress;
    if (P_IsCancelled(cancel)) return P__FileError(arena, ERROR_CANCELLED);
    Scratch scratch = GetScratchEx(arena);
    String *kept = ArenaPushArray(scratch.arena, Max(count, (u64)1), String);
    u64 *keptFrom = ArenaPushArray(scratch.arena, Max(count, (u64)1), u64);
    u64 keptCount = 0;
    if (placed) *placed = (ArrayStr){0};
    u32 error = 0;
    for (u64 i = 0; i < count && !error; ++i) {
        String path = paths[i];
        while (path.length > 3 && (path.value[path.length - 1] == '/' || path.value[path.length - 1] == '\\')) --path.length;
        Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
        Index backslash = StrIndexChar(path, '\\', SSF_SearchBackwards);
        u64 cut = Max(slash.exists ? slash.value : 0, backslash.exists ? backslash.value : 0);
        String parent = StrFromTo(path, 0, (i64)cut);
        if (P__PathWithin(directory, path)) error = ERROR_INVALID_PARAMETER;
        else if (!(move && P__PathWithin(parent, directory) && P__PathWithin(directory, parent))) {
            keptFrom[keptCount] = i;
            kept[keptCount++] = path;
        }
    }
    String *keptPlaced = placed ? ArenaPushArray(arena, Max(keptCount, (u64)1), String) : NULL;
    P_FileError result = error ? P__FileError(arena, error)
                       : P__ShellFileOperation(arena, move ? FO_MOVE : FO_COPY, kept, keptCount, directory, FOF_ALLOWUNDO | FOF_RENAMEONCOLLISION, NULL, keptPlaced);
    // Items moved into the folder they are in stay where they were.
    if (placed && !error) {
        for (u64 i = 0; i < count; ++i) ArrayPush(arena, *placed, StrCopy(arena, paths[i]));
        for (u64 k = 0; k < keptCount; ++k) placed->v[keptFrom[k]] = keptPlaced[k];
    }
    ReleaseScratch(scratch);
    return result;
}

function P_FileError P_TrashItems(Arena *arena, const String *paths, u64 count)
{
    return P__ShellFileOperation(arena, FO_DELETE, paths, count, (String){0}, FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING, NULL, NULL);
}

// Without FOF_ALLOWUNDO the shell deletes instead of recycling; it still shows progress for long deletes. Its confirmation names the item
// or how many go, owned by the window.
function P_FileError P_DeleteItems(Arena *arena, const String *paths, u64 count, b32 confirm, const P_CancelToken *cancel)
{
    if (P_IsCancelled(cancel)) return P__FileError(arena, ERROR_CANCELLED);
    P_FileError error = P__ShellFileOperation(arena, FO_DELETE, paths, count, (String){0}, confirm ? 0 : FOF_NOCONFIRMATION, confirm ? P__shellOwner : NULL, NULL);
    // A No is reported as a cancel, or as the shell's own DE_OPCANCELLED.
    return confirm && (error.code == ERROR_CANCELLED || error.code == 0x75) ? (P_FileError){0} : error;
}

// Only with a window to own its dialog, so that the headless tests confirm as other platforms do.
function b32 P_DeleteAsks(void) { return P__shellOwner != NULL; }
function b32 P_HasSystemFiles(void) { return true; }
function String P_TrashName(void) { return S("Recycle Bin"); }

function ArrayStr P_StartupPaths(Arena *arena)
{
    i32 count = 0;
    wchar **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    ArrayStr result = {0};
    Scratch scratch = GetScratchEx(arena);
    for (i32 i = 1; arguments && i < count; ++i) {
        // Short 8.3 names, as in %TEMP%, become their long ones, the names the folders show under.
        wchar *argument = arguments[i];
        // The shell quotes "%1", so a drive comes as "C:\", whose backslash the parsing took for an escaped quote; no path holds a quote.
        i32 length = lstrlenW(argument);
        if (length && argument[length - 1] == L'"') argument[length - 1] = 0;
        // Win+E passes its own shell folder, "::{52205fd8-...}", which starts Filey as it starts without a folder.
        if (argument[0] == L':' && argument[1] == L':') continue;
        u32 needed = GetLongPathNameW(argument, NULL, 0);
        if (needed) {
            wchar *full = ArenaPushArrayNoZero(scratch.arena, needed, wchar);
            if (GetLongPathNameW(argument, full, needed) && full[0]) argument = full;
        }
        String path = P_AbsolutePath(arena, P__FileStringWide(scratch.arena, argument, lstrlenW(argument)));
        if (path.length) ArrayPush(arena, result, path);
    }
    ReleaseScratch(scratch);
    if (arguments) LocalFree(arguments);
    return result;
}

function b32 P_ReadFile(Arena *arena, String path, String *contents)
{
    *contents = (String){0};
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    HANDLE file = wide ? CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL) : INVALID_HANDLE_VALUE;
    ReleaseScratch(scratch);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {0};
    // Settings files are small; anything past a few megabytes is not one.
    b32 result = GetFileSizeEx(file, &size) && size.QuadPart < (1 << 24);
    if (result) {
        char *bytes = ArenaPushArrayNoZero(arena, (u64)size.QuadPart + 1, char);
        DWORD read = 0;
        result = ReadFile(file, bytes, (DWORD)size.QuadPart, &read, NULL) && read == (DWORD)size.QuadPart;
        if (result) *contents = StrMake(bytes, read);
    }
    CloseHandle(file);
    return result;
}

function b32 P_RunCommand(Arena *arena, String directory, const String *arguments, u32 count, u32 timeoutMilliseconds, String *output)
{
    *output = (String){0};
    if (!count) return false;
    Scratch scratch = GetScratchEx(arena);
    // The command line quotes the arguments that hold spaces, as the C runtime splits it again; these never hold quotes.
    StrBuilder line = {0};
    for (u32 i = 0; i < count; ++i) {
        b32 quote = !arguments[i].length || StrIndexChar(arguments[i], ' ', SSF_None).exists;
        StrBuildF(scratch.arena, &line, i ? (quote ? " \"%S\"" : " %S") : (quote ? "\"%S\"" : "%S"), arguments[i]);
    }
    i32 length = MultiByteToWideChar(CP_UTF8, 0, line.buffer.value, (i32)line.buffer.length, NULL, 0);
    wchar *wide = ArenaPushArray(scratch.arena, (u64)length + 1, wchar);
    MultiByteToWideChar(CP_UTF8, 0, line.buffer.value, (i32)line.buffer.length, wide, length);
    wchar *folder = P__FilePathWide(scratch.arena, directory);
    SECURITY_ATTRIBUTES inherit = { .nLength = sizeof(inherit), .bInheritHandle = TRUE };
    HANDLE readEnd = NULL, writeEnd = NULL;
    b32 result = false;
    if (folder && CreatePipe(&readEnd, &writeEnd, &inherit, 0)) {
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup = { .cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES, .hStdOutput = writeEnd, .hStdError = NULL, .hStdInput = NULL };
        PROCESS_INFORMATION process = {0};
        b32 started = CreateProcessW(NULL, wide, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, folder, &startup, &process) != 0;
        CloseHandle(writeEnd);
        if (started) {
            CloseHandle(process.hThread);
            StrBuilder out = {0};
            u64 began = GetTickCount64();
            b32 late = false;
            for (;;) {
                DWORD waiting = 0;
                if (!PeekNamedPipe(readEnd, NULL, 0, NULL, &waiting, NULL)) break;
                if (waiting) {
                    char bytes[4096];
                    DWORD read = 0;
                    if (!ReadFile(readEnd, bytes, (DWORD)Min((u64)waiting, sizeof(bytes)), &read, NULL) || !read) break;
                    StrBuildStr(arena, &out, StrMake(bytes, read));
                    continue;
                }
                if (GetTickCount64() - began > timeoutMilliseconds) {
                    late = true;
                    break;
                }
                // Done once it has exited and the pipe is drained.
                if (WaitForSingleObject(process.hProcess, 5) == WAIT_OBJECT_0) {
                    if (!PeekNamedPipe(readEnd, NULL, 0, NULL, &waiting, NULL) || !waiting) break;
                }
            }
            DWORD code = 1;
            if (late) TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 1000);
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hProcess);
            result = !late && code == 0;
            *output = out.buffer;
        }
        CloseHandle(readEnd);
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P_IsDirectory(String path)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FileExtendedPath(scratch.arena, path);
    DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    ReleaseScratch(scratch);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

function String P_RunFolder(Arena *arena)
{
    wchar temporary[MAX_PATH] = {0};
    u32 length = GetTempPathW((u32)CArrayCount(temporary), temporary);
    if (!length || length >= CArrayCount(temporary)) return (String){0};
    String base = P_AbsolutePath(arena, P__FileStringWide(arena, temporary, (i32)length));
    while (base.length > 3 && base.value[base.length - 1] == '/') --base.length;
    String filey = StrFormat(arena, "%S/Filey", base), folder = StrFormat(arena, "%S/%u", filey, (u32)GetCurrentProcessId());
    Scratch scratch = GetScratchEx(arena);
    CreateDirectoryW(P__FilePathWide(scratch.arena, filey), NULL);
    CreateDirectoryW(P__FilePathWide(scratch.arena, folder), NULL);
    ReleaseScratch(scratch);
    return P_IsDirectory(folder) ? folder : (String){0};
}

function b32 P_PathExists(String path)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FileExtendedPath(scratch.arena, path);
    b32 result = wide && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
    ReleaseScratch(scratch);
    return result;
}

function void P_Sleep(u32 milliseconds)
{
    Sleep(milliseconds);
}

function b32 P_ReadFileStart(Arena *arena, String path, u64 limit, String *contents)
{
    return P_ReadFileAt(arena, path, 0, limit, contents);
}

function b32 P_ReadFileAt(Arena *arena, String path, u64 offset, u64 limit, String *contents)
{
    *contents = (String){0};
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    HANDLE file = wide ? CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL) : INVALID_HANDLE_VALUE;
    ReleaseScratch(scratch);
    if (file == INVALID_HANDLE_VALUE) return false;
    limit = Min(limit, (u64)UINT32_MAX);
    char *bytes = ArenaPushArrayNoZero(arena, limit + 1, char);
    DWORD read = 0;
    OVERLAPPED at = { .Offset = (u32)(offset & UINT32_MAX), .OffsetHigh = (u32)(offset >> 32) };
    b32 result = ReadFile(file, bytes, (DWORD)limit, &read, &at) != 0 || GetLastError() == ERROR_HANDLE_EOF;
    if (result) *contents = StrMake(bytes, read);
    CloseHandle(file);
    return result;
}

function P_FileError P_WriteFile(Arena *arena, String path, String contents)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    wchar *temporary = P__FilePathWide(scratch.arena, StrFormat(scratch.arena, "%S.tmp", path));
    u32 code = ERROR_INVALID_NAME;
    if (wide && temporary && contents.length <= UINT32_MAX) {
        HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD written = 0;
        b32 ok = file != INVALID_HANDLE_VALUE && WriteFile(file, contents.value, (DWORD)contents.length, &written, NULL) && written == contents.length && FlushFileBuffers(file);
        code = ok ? 0 : GetLastError();
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        if (ok && !MoveFileExW(temporary, wide, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) code = GetLastError();
        if (code) DeleteFileW(temporary);
    }
    ReleaseScratch(scratch);
    return code ? P__FileError(arena, code) : (P_FileError){0};
}

function P_FileError P_AppendFile(Arena *arena, String path, String contents, b32 restart)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    u32 code = ERROR_INVALID_NAME;
    if (wide && contents.length <= UINT32_MAX) {
        HANDLE file = CreateFileW(wide, restart ? GENERIC_WRITE : FILE_APPEND_DATA, 0, NULL, restart ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD written = 0;
        b32 ok = file != INVALID_HANDLE_VALUE && WriteFile(file, contents.value, (DWORD)contents.length, &written, NULL) && written == contents.length;
        code = ok ? 0 : GetLastError();
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    ReleaseScratch(scratch);
    return code ? P__FileError(arena, code) : (P_FileError){0};
}

function P_FileError P_ReplaceFile(Arena *arena, String from, String to)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *source = P__FilePathWide(scratch.arena, from), *target = P__FilePathWide(scratch.arena, to);
    u32 code = ERROR_INVALID_NAME;
    if (source && target) code = MoveFileExW(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : GetLastError();
    ReleaseScratch(scratch);
    return code ? P__FileError(arena, code) : (P_FileError){0};
}
