// Requires WinPlatform.c for the Win32 declarations.

#pragma push_macro("function")
#undef function
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <lm.h>
#include <winioctl.h>
#pragma pop_macro("function")

// Phones, cameras and other devices that the shell shows under This PC have no file paths.
// Filey names them and what is on them by the names shown, under the folder that lists the drives, as "This PC/Phone/Internal storage/DCIM".
// The file APIs cannot reach them, so these have no Win32 form; the shell reaches them by their item id lists, learned as their folders are listed.
function b32 P__IsDevicePath(String path)
{
    return path.length > 8 && MemCompare(path.value, "This PC/", 8) == 0;
}

function String P_NetworkFolder(void) { return S("Network"); }

// The Network folder, or a path under it, which only a mistake would make, as a new folder there.
function b32 P__IsNetworkPath(String path)
{
    return path.length >= 7 && MemCompare(path.value, "Network", 7) == 0 && (path.length == 7 || path.value[7] == '/');
}

function P_DirectoryResult P__EnumerateNetwork(Arena *arena, const P_CancelToken *cancel);

// The shell's item id list of the device path `path`, for ILFree, or NULL when it is not there; `folder` says whether it is a folder.
function PIDLIST_ABSOLUTE P__DeviceIdList(String path, b32 *folder);
function P_DirectoryResult P__EnumerateDevice(Arena *arena, String path, const P_CancelToken *cancel);

function wchar *P__FilePathWide(Arena *arena, String path)
{
    if (P__IsDevicePath(path) || P__IsNetworkPath(path)) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return NULL;
    }
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

// The shell's item id list of `path`, a device's too, for ILFree; NULL when it is not there.
function PIDLIST_ABSOLUTE P__PathIdList(String path)
{
    if (P__IsDevicePath(path)) return P__DeviceIdList(path, NULL);
    Scratch scratch = GetScratch();
    wchar *wide = P__FilePathWide(scratch.arena, path);
    PIDLIST_ABSOLUTE list = NULL;
    if (!wide || FAILED(SHParseDisplayName(wide, NULL, &list, 0, NULL))) list = NULL;
    ReleaseScratch(scratch);
    return list;
}

function String P__FileStringWide(Arena *arena, const wchar *text, i32 length)
{
    i32 count = WideCharToMultiByte(CP_UTF8, 0, text, length, NULL, 0, NULL, NULL);
    char *bytes = ArenaPushArray(arena, (u64)count + 1, char);
    if (count) WideCharToMultiByte(CP_UTF8, 0, text, length, bytes, count, NULL, NULL);
    return StrMake(bytes, (u64)count);
}

function b32 P_FileErrorIsCancel(P_FileError error)
{
    // The shell reports a stop as ERROR_CANCELLED, as its own DE_OPCANCELLED, or as the first one's HRESULT.
    return error.code == ERROR_CANCELLED || error.code == 0x75 || error.code == (u32)HRESULT_FROM_WIN32(ERROR_CANCELLED);
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
            P_ProgressName(progress, StrFrom(name, StrIndexChar(name, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value));
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
    if (P__IsDevicePath(path) || P__IsNetworkPath(path)) {
        while (path.length > 7 && path.value[path.length - 1] == '/') --path.length;
        return StrCopy(arena, path);
    }
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

function i64 P_LocalOffset(i64 timeUnix)
{
    i64 ticks = 0;
    if (ckd_add(&ticks, timeUnix, INT64_C(11644473600)) || ticks < 0 || ckd_mul(&ticks, ticks, INT64_C(10000000))) return 0;
    FILETIME time = {.dwLowDateTime = (u32)((u64)ticks & UINT32_MAX), .dwHighDateTime = (u32)((u64)ticks >> 32)}, localTime = {0};
    SYSTEMTIME utc = {0}, local = {0};
    if (!FileTimeToSystemTime(&time, &utc) || !SystemTimeToTzSpecificLocalTime(NULL, &utc, &local) || !SystemTimeToFileTime(&local, &localTime)) return 0;
    i64 localTicks = (i64)(((u64)localTime.dwHighDateTime << 32) | localTime.dwLowDateTime);
    return (localTicks - ticks) / INT64_C(10000000);
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

// The server of a path that is only a server, as "//nas" or "//nas/", which lists the folders it shares; false for any other path.
function b32 P__ServerRoot(String path, String *server)
{
    if (path.length < 3 || path.value[0] != '/' || path.value[1] != '/' || path.value[2] == '?' || path.value[2] == '.') return false;
    u64 end = path.length;
    while (end > 2 && path.value[end - 1] == '/') --end;
    for (u64 i = 2; i < end; ++i) if (path.value[i] == '/') return false;
    *server = StrFromTo(path, 0, (i64)end);
    return end > 2;
}

// The names of the folders `server` shares, as NetShareEnum asks it: the disk shares, leaving out the system's own, as C$ and ADMIN$, and
// those named with a $ at the end to keep them out of sight, as Explorer leaves them out. The error is the server's when it gives none.
function ArrayStr P__ShareNames(Arena *arena, String server, const P_CancelToken *cancel, u32 *error)
{
    ArrayStr result = {0};
    Scratch scratch = GetScratchEx(arena);
    wchar *name = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, server));
    NET_API_STATUS status = name ? NERR_Success : ERROR_INVALID_NAME;
    DWORD resume = 0;
    while (name && !P_IsCancelled(cancel)) {
        SHARE_INFO_1 *shares = NULL;
        DWORD read = 0, total = 0;
        status = NetShareEnum(name, 1, (LPBYTE *)&shares, MAX_PREFERRED_LENGTH, &read, &total, &resume);
        if (status != NERR_Success && status != ERROR_MORE_DATA) break;
        for (DWORD i = 0; i < read; ++i) {
            i32 length = lstrlenW(shares[i].shi1_netname);
            if (shares[i].shi1_type != STYPE_DISKTREE || !length || shares[i].shi1_netname[length - 1] == L'$') continue;
            ArrayPush(arena, result, P__FileStringWide(arena, shares[i].shi1_netname, length));
        }
        if (shares) NetApiBufferFree(shares);
        if (status != ERROR_MORE_DATA) break;
    }
    ReleaseScratch(scratch);
    if (error) *error = status == ERROR_MORE_DATA ? NERR_Success : (u32)status;
    return result;
}

function P_ArrayPlace P_ServerShares(Arena *arena, String server, const P_CancelToken *cancel)
{
    P_ArrayPlace result = {0};
    Scratch scratch = GetScratchEx(arena);
    ArrayStr names = P__ShareNames(scratch.arena, server, cancel, NULL);
    String native = P_NativePath(scratch.arena, server);
    for (u64 i = 0; i < names.header.count && !P_IsCancelled(cancel); ++i) {
        P_Place place = { .name = StrFormat(arena, "%S (%S)", names.v[i], native), .path = StrFormat(arena, "%S/%S", server, names.v[i]) };
        wchar *root = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, StrFormat(scratch.arena, "%S/", place.path)));
        ULARGE_INTEGER available = {0}, size = {0};
        if (root && GetDiskFreeSpaceExW(root, &available, &size, NULL)) {
            place.totalBytes = size.QuadPart;
            place.freeBytes = available.QuadPart;
        }
        ArrayPush(arena, result, place);
    }
    ReleaseScratch(scratch);
    return result;
}

function P_DirectoryResult P_EnumerateDirectory(Arena *arena, String path, const P_CancelToken *cancel)
{
    P_DirectoryResult result = {0};
    if (P_IsCancelled(cancel)) {
        result.cancelled = true;
        return result;
    }
    if (P__IsDevicePath(path)) return P__EnumerateDevice(arena, path, cancel);
    if (StrIsEqual(path, P_NetworkFolder())) return P__EnumerateNetwork(arena, cancel);
    ArenaMark mark = ArenaGetMark(arena);
    Scratch scratch = GetScratchEx(arena);
    wchar *directory = P__FileExtendedPath(scratch.arena, path);
    u32 error = directory ? 0 : GetLastError();
    HANDLE find = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW data = {0};
    // A server lists the folders it shares, as Explorer's \\nas does.
    String server = {0};
    if (P__ServerRoot(path, &server)) {
        ArrayStr shares = P__ShareNames(scratch.arena, server, cancel, &error);
        for (u64 i = 0; i < shares.header.count; ++i) {
            ArrayPush(arena, result.files, ((P_File){ .name = StrCopy(arena, shares.v[i]), .kind = P_FileKind_Directory }));
        }
        directory = NULL;
    }
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
                .modifiedTicks = (u32)(time % UINT64_C(10000000)),
                .createdTicks = (u32)(created % UINT64_C(10000000)),
                .accessedTicks = (u32)(accessed % UINT64_C(10000000)),
                .kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? P_FileKind_Directory : (data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) ? P_FileKind_Other : P_FileKind_File,
                .hidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0 || data.cFileName[0] == L'.',
                .system = (data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0,
                .metadataValid = true,
                // A cloud drive's placeholders are reparse points too, of tags of their own; only links and junctions lead elsewhere.
                .symbolicLink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && (data.dwReserved0 == IO_REPARSE_TAG_SYMLINK || data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT),
                .online = !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (data.dwFileAttributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_OFFLINE)) != 0,
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

function P_FileError P_SetMode(Arena *arena, String path, u16 mode)
{
    (void)path, (void)mode;
    return P__FileError(arena, ERROR_NOT_SUPPORTED);
}

global const CLSID P__CLSID_FileOperation = { 0x3AD05575, 0x8857, 0x4850, { 0x92, 0x77, 0x11, 0xB8, 0x5B, 0xDB, 0x8E, 0x09 } };
global const IID P__IID_IFileOperation   = { 0x947AAB5F, 0x0A5C, 0x4C13, { 0xB4, 0xD6, 0x4B, 0xF7, 0x83, 0x6F, 0xC9, 0xF8 } };
global const IID P__IID_IShellItem       = { 0x43826D1E, 0xE718, 0x42EE, { 0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE } };

// Renames the device item `path` to `name`, or with `create` makes the item `name` in the device folder `path`, a folder or an empty file, through the shell's file operation; the error, or 0.
function u32 P__DeviceChange(String path, String name, b32 create, b32 folder)
{
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Scratch scratch = GetScratch();
    PIDLIST_ABSOLUTE list = P__PathIdList(path);
    wchar *wide = P__FilePathWide(scratch.arena, name);
    IShellItem *item = NULL;
    IFileOperation *op = NULL;
    u32 error = !list || !wide ? ERROR_PATH_NOT_FOUND : 0;
    if (!error && FAILED(SHCreateItemFromIDList(list, &P__IID_IShellItem, (void **)&item))) error = ERROR_PATH_NOT_FOUND;
    if (!error && FAILED(CoCreateInstance(&P__CLSID_FileOperation, NULL, CLSCTX_ALL, &P__IID_IFileOperation, (void **)&op))) error = ERROR_NOT_SUPPORTED;
    if (!error) {
        op->lpVtbl->SetOperationFlags(op, FOF_NOERRORUI | FOF_NOCONFIRMATION | FOF_SILENT);
        HRESULT queued = create ? op->lpVtbl->NewItem(op, item, folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, wide, NULL, NULL)
                                : op->lpVtbl->RenameItem(op, item, wide, NULL);
        HRESULT done = SUCCEEDED(queued) ? op->lpVtbl->PerformOperations(op) : queued;
        BOOL aborted = FALSE;
        op->lpVtbl->GetAnyOperationsAborted(op, &aborted);
        error = aborted ? ERROR_CANCELLED : FAILED(done) ? (HRESULT_FACILITY(done) == FACILITY_WIN32 ? (u32)HRESULT_CODE(done) : ERROR_GEN_FAILURE) : 0;
    }
    if (op) op->lpVtbl->Release(op);
    if (item) item->lpVtbl->Release(item);
    ILFree(list);
    ReleaseScratch(scratch);
    if (SUCCEEDED(com)) CoUninitialize();
    return error;
}

function P_FileError P_RenamePath(Arena *arena, String directory, String from, String to)
{
    if (!P__ValidFileName(from) || !P__ValidFileName(to)) return P__FileError(arena, ERROR_INVALID_NAME);
    if (P__IsDevicePath(directory)) {
        Scratch scratch = GetScratchEx(arena);
        u32 error = P_PathExists(StrFormat(scratch.arena, "%S/%S", directory, to)) ? ERROR_ALREADY_EXISTS : P__DeviceChange(StrFormat(scratch.arena, "%S/%S", directory, from), to, false, false);
        ReleaseScratch(scratch);
        return error ? P__FileError(arena, error) : (P_FileError){0};
    }
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
    String device;          // a device's folder, which is listed again in turn rather than watched
    P_CancelToken cancel;   // ends a listing of it under way when the watch stops
} P__Watch;

function b32 P__ListDevice(Arena *arena, String path, const P_CancelToken *cancel, P_FileArray *files, ArrayStr *types, u32 *error);

// A device sends no notices of its changes, so its folder is listed again in turn, and a change in what it holds, or its going, wakes the program.
// The next listing waits four times as long as the last one took, and at least two seconds, so that a large folder on a slow phone is not listed without end.
function DWORD WINAPI P__DeviceWatchThread(void *user)
{
    P__Watch *watch = user;
    b32 known = false, was = false;
    u64 before = 0;
    DWORD pause = 2000;
    while (WaitForSingleObject(watch->stop, pause) == WAIT_TIMEOUT) {
        Arena arena = {0};
        P_FileArray files = {0};
        u64 start = PlatformNowMicroseconds(), seen = 0;
        b32 listed = P__ListDevice(&arena, watch->device, &watch->cancel, &files, NULL, NULL);
        if (P_IsCancelled(&watch->cancel)) {
            ArenaRelease(&arena);
            break;
        }
        // In any order: the sum of each item's hash.
        for (u64 i = 0; i < files.header.count; ++i) {
            const P_File *file = &files.v[i];
            u64 parts[3] = { MapHashBytes(file->name.value, file->name.length), file->size, (u64)file->modifiedTimeUnix };
            seen += MapHashBytes(parts, sizeof(parts));
        }
        ArenaRelease(&arena);
        if (known && (listed != was || seen != before) && !atomic_exchange(&watch->changed, true) && watch->wake) watch->wake(watch->user);
        known = true, was = listed, before = seen;
        pause = (DWORD)Max((u64)2000, (PlatformNowMicroseconds() - start) * 4 / 1000);
    }
    return 0;
}

function DWORD WINAPI P__WatchThread(void *user)
{
    P__Watch *watch = user;
    HANDLE handles[2] = { watch->stop, watch->change };
    while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        // Only the first change since the program last looked needs a wakeup.
        if (!atomic_exchange(&watch->changed, true) && watch->wake) watch->wake(watch->user);
        if (!FindNextChangeNotification(watch->change)) break;
        // Changes during the pause are kept and signal the handle again.
        if (WaitForSingleObject(watch->stop, P_WatchPause) == WAIT_OBJECT_0) break;
    }
    return 0;
}

function b32 P__WatchStart(P_DirectoryWatch *watch, String path, b32 tree, P_JobWakeCallback *wake, void *user)
{
    P_DirectoryWatchStop(watch);
    if (P__IsDevicePath(path)) {
        P__Watch *state = ArenaPushStruct(&watch->arena, P__Watch);
        *state = (P__Watch){ .change = INVALID_HANDLE_VALUE, .wake = wake, .user = user, .device = StrCopy(&watch->arena, path) };
        state->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
        state->thread = state->stop ? CreateThread(NULL, 0, P__DeviceWatchThread, state, 0, NULL) : NULL;
        watch->native = state;
        if (!state->thread) {
            P_DirectoryWatchStop(watch);
            return false;
        }
        return true;
    }
    Scratch scratch = GetScratch();
    wchar *wide = P__FilePathWide(scratch.arena, path);
    P__Watch *state = ArenaPushStruct(&watch->arena, P__Watch);
    *state = (P__Watch){ .wake = wake, .user = user };
    state->change = wide ? FindFirstChangeNotificationW(wide, tree ? TRUE : FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
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

function b32 P_DirectoryWatchStart(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user)
{
    return P__WatchStart(watch, path, false, wake, user);
}

function b32 P_DirectoryWatchStartTree(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user)
{
    return P__WatchStart(watch, path, true, wake, user);
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
            P_RequestCancel(&state->cancel);
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

function i64 P_FileModifiedTime(String path)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FileExtendedPath(scratch.arena, path);
    WIN32_FILE_ATTRIBUTE_DATA data = {0};
    b32 found = wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &data);
    ReleaseScratch(scratch);
    if (!found) return 0;
    u64 time = ((u64)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
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
        b32 device = P__IsDevicePath(directory);
        wchar *path = device ? NULL : P__FilePathWide(scratch.arena, StrFormat(scratch.arena, "%S/%S", directory, candidate));
        if (device) {
            error = P_PathExists(StrFormat(scratch.arena, "%S/%S", directory, candidate)) ? ERROR_ALREADY_EXISTS : P__DeviceChange(directory, candidate, true, folder);
        } else if (!path) {
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

// The same through the shell's file operation on the items, for a copy, move or delete with a device's items in it, which have no paths for the string form to name.
function P_FileError P__ShellItemOperation(Arena *arena, u32 operation, const String *paths, u64 count, String directory, u16 flags, HWND owner, String *placed)
{
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Scratch scratch = GetScratchEx(arena);
    PIDLIST_ABSOLUTE *lists = ArenaPushArray(scratch.arena, Max(count, (u64)1), PIDLIST_ABSOLUTE);
    u32 error = 0;
    for (u64 i = 0; i < count; ++i) {
        lists[i] = P__PathIdList(paths[i]);
        if (!lists[i]) error = ERROR_PATH_NOT_FOUND;
    }
    PIDLIST_ABSOLUTE target = directory.length ? P__PathIdList(directory) : NULL;
    if (directory.length && !target) error = ERROR_PATH_NOT_FOUND;
    IShellItemArray *items = NULL;
    IShellItem *into = NULL;
    IFileOperation *op = NULL;
    if (!error && FAILED(SHCreateShellItemArrayFromIDLists((UINT)count, (PCIDLIST_ABSOLUTE_ARRAY)lists, &items))) error = ERROR_PATH_NOT_FOUND;
    if (!error && target && FAILED(SHCreateItemFromIDList(target, &P__IID_IShellItem, (void **)&into))) error = ERROR_PATH_NOT_FOUND;
    if (!error && FAILED(CoCreateInstance(&P__CLSID_FileOperation, NULL, CLSCTX_ALL, &P__IID_IFileOperation, (void **)&op))) error = ERROR_NOT_SUPPORTED;
    if (!error) {
        op->lpVtbl->SetOperationFlags(op, (DWORD)flags | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR);
        if (owner) op->lpVtbl->SetOwnerWindow(op, owner);
        HRESULT queued = operation == FO_COPY ? op->lpVtbl->CopyItems(op, (IUnknown *)items, into)
                       : operation == FO_MOVE ? op->lpVtbl->MoveItems(op, (IUnknown *)items, into)
                       : operation == FO_DELETE ? op->lpVtbl->DeleteItems(op, (IUnknown *)items) : E_NOTIMPL;
        HRESULT done = SUCCEEDED(queued) ? op->lpVtbl->PerformOperations(op) : queued;
        BOOL aborted = FALSE;
        op->lpVtbl->GetAnyOperationsAborted(op, &aborted);
        error = aborted ? ERROR_CANCELLED : FAILED(done) ? (HRESULT_FACILITY(done) == FACILITY_WIN32 ? (u32)HRESULT_CODE(done) : ERROR_GEN_FAILURE) : 0;
    }
    for (u64 i = 0; placed && i < count; ++i) placed[i] = P__PlacedPath(arena, directory, paths[i]);
    if (op) op->lpVtbl->Release(op);
    if (into) into->lpVtbl->Release(into);
    if (items) items->lpVtbl->Release(items);
    for (u64 i = 0; i < count; ++i) ILFree(lists[i]);
    ILFree(target);
    ReleaseScratch(scratch);
    if (SUCCEEDED(com)) CoUninitialize();
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P__ShellFileOperation(Arena *arena, u32 operation, const String *paths, u64 count, String directory, u16 flags, HWND owner, String *placed)
{
    b32 device = P__IsDevicePath(directory);
    for (u64 i = 0; i < count && !device; ++i) device = P__IsDevicePath(paths[i]);
    if (device) return P__ShellItemOperation(arena, operation, paths, count, directory, flags, owner, placed);
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
                        // The shell may give the folder its long name where the path had its short one, as in "USER~1", which leaves the
                        // item's own name to tell which it is.
                        String oldName = P__PlacedPath(scratch.arena, S(""), old), name = P__PlacedPath(scratch.arena, S(""), paths[i]);
                        b32 named = P__PathWithin(oldName, name) && P__PathWithin(name, oldName);
                        if (!named && (!P__PathWithin(old, placed[i]) || !P__PathWithin(placed[i], old))) continue;
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

function b32 P_MountsRemote(void) { return false; }

function P_FileError P_MountRemote(Arena *arena, String address, String password, String *folder)
{
    (void)address, (void)password;
    *folder = (String){0};
    return P__FileError(arena, ERROR_NOT_SUPPORTED);
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
    b32 duplicate = false;
    for (u64 i = 0; i < count && !error; ++i) {
        String path = paths[i];
        while (path.length > 3 && (path.value[path.length - 1] == '/' || path.value[path.length - 1] == '\\')) --path.length;
        Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
        Index backslash = StrIndexChar(path, '\\', SSF_SearchBackwards);
        u64 cut = Max(slash.exists ? slash.value : 0, backslash.exists ? backslash.value : 0);
        String parent = StrFromTo(path, 0, (i64)cut);
        duplicate |= P__PathWithin(parent, directory) && P__PathWithin(directory, parent);
        if (P__PathWithin(directory, path)) error = ERROR_INVALID_PARAMETER;
        else if (!(move && P__PathWithin(parent, directory) && P__PathWithin(directory, parent))) {
            keptFrom[keptCount] = i;
            kept[keptCount++] = path;
        }
    }
    String *keptPlaced = placed ? ArenaPushArray(arena, Max(keptCount, (u64)1), String) : NULL;
    b32 asks = !duplicate && P__shellOwner;
    P_FileError result = error ? P__FileError(arena, error)
                       // A copy into the folder it is in is named anew, as Explorer's "- Copy"; elsewhere a taken name asks whether to replace,
                       // skip or keep both, in the shell's own window, where there is a window to own it.
                       : P__ShellFileOperation(arena, move ? FO_MOVE : FO_COPY, kept, keptCount, directory, FOF_ALLOWUNDO | (asks ? 0 : FOF_RENAMEONCOLLISION),
                                               asks ? P__shellOwner : NULL, keptPlaced);
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
    // A No is reported as a cancel.
    return confirm && P_FileErrorIsCancel(error) ? (P_FileError){0} : error;
}

// Only with a window to own its dialog, so that the headless tests confirm as other platforms do.
function b32 P_DeleteAsks(void) { return P__shellOwner != NULL; }
function b32 P_HasSystemFiles(void) { return true; }

// NTFS's master file table, enumerated through the volume as FSCTL_ENUM_USN_DATA gives it: a record for every file with its reference,
// its folder's and its name, which opening the volume needs an administrator for. The table's own files, the first 24 records, are left out.
typedef struct P__VolumeRecord {
    u64 ref, parent;
    u32 attributes;
    String name;
} P__VolumeRecord;
typedef struct P__ArrayVolumeRecord { ArrayHeader header; P__VolumeRecord *v; } P__ArrayVolumeRecord;
function b32 P__VolumeTreeFrom(Arena *arena, const P__VolumeRecord *records, u64 count, u64 rootRef, u64 most, P_VolumeTree *tree);

function b32 P_ReadVolumeTree(Arena *arena, String root, u64 most, const P_CancelToken *cancel, P_VolumeTree *tree)
{
    *tree = (P_VolumeTree){0};
    if (root.length < 2 || root.length > 3 || root.value[1] != ':' || (root.length == 3 && root.value[2] != '/' && root.value[2] != '\\')) return false;
    wchar rootPath[4] = { (wchar)root.value[0], L':', L'\\', 0 }, system[16] = {0};
    if (!GetVolumeInformationW(rootPath, NULL, 0, NULL, NULL, NULL, system, CArrayCount(system)) || lstrcmpiW(system, L"NTFS") != 0) return false;
    wchar device[8] = { L'\\', L'\\', L'.', L'\\', rootPath[0], L':', 0 };
    HANDLE volume = CreateFileW(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (volume == INVALID_HANDLE_VALUE) return false;
    // The root folder's own reference, which its files and folders name as theirs.
    u64 rootRef = 0;
    HANDLE folder = CreateFileW(rootPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    BY_HANDLE_FILE_INFORMATION info = {0};
    if (folder != INVALID_HANDLE_VALUE && GetFileInformationByHandle(folder, &info)) rootRef = ((u64)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    if (folder != INVALID_HANDLE_VALUE) CloseHandle(folder);
    if (!rootRef) {
        CloseHandle(volume);
        return false;
    }
    Scratch scratch = GetScratchEx(arena);
    P__ArrayVolumeRecord records = {0};
    MFT_ENUM_DATA_V0 enumerate = { .StartFileReferenceNumber = 0, .LowUsn = 0, .HighUsn = MAXLONGLONG };
    u32 size = 1u << 20;
    u8 *buffer = ArenaPushArrayNoZero(scratch.arena, size, u8);
    DWORD got = 0;
    b32 read = false;
    while (!P_IsCancelled(cancel) && DeviceIoControl(volume, FSCTL_ENUM_USN_DATA, &enumerate, sizeof(enumerate), buffer, size, &got, NULL) && got > sizeof(u64)) {
        read = true;
        MemCopy(&enumerate.StartFileReferenceNumber, buffer, sizeof(u64));
        for (u32 at = sizeof(u64); at + sizeof(USN_RECORD_V2) <= got;) {
            const USN_RECORD_V2 *record = (const USN_RECORD_V2 *)(buffer + at);
            if (!record->RecordLength) break;
            if (record->MajorVersion == 2 && (record->FileReferenceNumber & UINT64_C(0xFFFFFFFFFFFF)) >= 24) {
                const wchar *name = (const wchar *)((const u8 *)record + record->FileNameOffset);
                P__VolumeRecord kept = { record->FileReferenceNumber, record->ParentFileReferenceNumber, record->FileAttributes,
                                         P__FileStringWide(scratch.arena, name, (i32)(record->FileNameLength / sizeof(wchar))) };
                ArrayPush(scratch.arena, records, kept);
            }
            at += record->RecordLength;
        }
    }
    CloseHandle(volume);
    b32 result = read && records.header.count && !P_IsCancelled(cancel) && P__VolumeTreeFrom(arena, records.v, records.header.count, rootRef, most, tree);
    ReleaseScratch(scratch);
    return result;
}

// The tree of `count` table records under the root whose reference is `rootRef`, breadth first, at most `most` entries.
function b32 P__VolumeTreeFrom(Arena *arena, const P__VolumeRecord *records, u64 count, u64 rootRef, u64 most, P_VolumeTree *tree)
{
    Scratch scratch = GetScratchEx(arena);
    // Each record found by its reference, in a table twice its size of one more than its place, 0 for none.
    u64 slots = 1;
    while (slots < count * 2) slots <<= 1;
    u32 *table = ArenaPushArray(scratch.arena, slots, u32);
    for (u64 i = 0; i < count; ++i) {
        u64 at = (records[i].ref * UINT64_C(0x9E3779B97F4A7C15)) >> 20 & (slots - 1);
        while (table[at]) at = (at + 1) & (slots - 1);
        table[at] = (u32)i + 1;
    }
    // Each record's folder, UINT32_MAX - 1 for the root and UINT32_MAX for one outside the tree, and the folders' records in runs.
    u32 *owner = ArenaPushArrayNoZero(scratch.arena, count, u32), *runStart = ArenaPushArray(scratch.arena, count + 2, u32);
    for (u64 i = 0; i < count; ++i) {
        u64 parent = records[i].parent;
        owner[i] = UINT32_MAX;
        if (parent == rootRef) {
            owner[i] = UINT32_MAX - 1;
            continue;
        }
        for (u64 at = (parent * UINT64_C(0x9E3779B97F4A7C15)) >> 20 & (slots - 1); table[at]; at = (at + 1) & (slots - 1)) {
            if (records[table[at] - 1].ref == parent) {
                owner[i] = table[at] - 1;
                break;
            }
        }
    }
    // Children by folder: the root's at slot `count`, counted, then placed.
    for (u64 i = 0; i < count; ++i) if (owner[i] != UINT32_MAX) ++runStart[(owner[i] == UINT32_MAX - 1 ? count : owner[i]) + 1];
    for (u64 i = 1; i < count + 2; ++i) runStart[i] += runStart[i - 1];
    u32 *children = ArenaPushArrayNoZero(scratch.arena, Max(runStart[count + 1], 1u), u32), *filled = ArenaPushArray(scratch.arena, count + 1, u32);
    for (u64 i = 0; i < count; ++i) {
        if (owner[i] == UINT32_MAX) continue;
        u64 group = owner[i] == UINT32_MAX - 1 ? count : owner[i];
        children[runStart[group] + filled[group]++] = (u32)i;
    }
    // Breadth first from the root, as a walk of the folders takes them; a link is not gone into.
    u64 room = Min(most, (u64)runStart[count + 1]);
    P_VolumeEntry *entries = ArenaPushArray(arena, Max(room, (u64)1), P_VolumeEntry);
    u32 *source = ArenaPushArrayNoZero(scratch.arena, Max(room, (u64)1), u32);
    u64 shown = 0;
    for (u32 i = runStart[count]; i < runStart[count + 1] && shown < room; ++i) source[shown] = children[i], entries[shown++].parent = UINT32_MAX;
    for (u64 next = 0; next < shown; ++next) {
        const P__VolumeRecord *record = &records[source[next]];
        b32 isFolder = (record->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0, link = (record->attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        entries[next].name = StrCopy(arena, record->name);
        entries[next].folder = (b8)isFolder;
        entries[next].hidden = (b8)((record->attributes & FILE_ATTRIBUTE_HIDDEN) != 0);
        entries[next].link = (b8)link;
        if (!isFolder || link) continue;
        for (u32 i = runStart[source[next]]; i < runStart[source[next] + 1] && shown < room; ++i) source[shown] = children[i], entries[shown++].parent = (u32)next;
    }
    tree->entries = entries;
    tree->count = shown;
    ReleaseScratch(scratch);
    return true;
}

function b32 P_HasPathLimit(void) { return true; }

function b32 P_LongPathsEnabled(void)
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\FileSystem", L"LongPathsEnabled", RRF_RT_REG_DWORD, NULL, &value, &size) == ERROR_SUCCESS && value;
}

// Through reg.exe run as an administrator, which Windows asks the user to allow.
function b32 P_SetLongPaths(b32 on)
{
    SHELLEXECUTEINFOW info = {
        .cbSize = sizeof(info), .fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI, .lpVerb = L"runas", .lpFile = L"reg.exe", .nShow = SW_HIDE,
        .lpParameters = on ? L"add HKLM\\SYSTEM\\CurrentControlSet\\Control\\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f"
                           : L"add HKLM\\SYSTEM\\CurrentControlSet\\Control\\FileSystem /v LongPathsEnabled /t REG_DWORD /d 0 /f",
    };
    if (!ShellExecuteExW(&info) || !info.hProcess) return false;
    WaitForSingleObject(info.hProcess, 60000);
    CloseHandle(info.hProcess);
    return P_LongPathsEnabled() == (on != 0);
}
function String P_TrashName(void) { return S("Recycle Bin"); }
function String P_TerminalFont(Arena *arena) { return (String){0}; }

function String P_SystemLanguage(Arena *arena)
{
    wchar name[LOCALE_NAME_MAX_LENGTH];
    i32 length = LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0);
    String result = length > 1 ? P__FileStringWide(arena, name, length - 1) : (String){0};
    Index dash = StrIndexChar(result, '-', SSF_None);
    return dash.exists ? StrFromTo(result, 0, (i64)dash.value) : result;
}

function String P_ExpandVariables(Arena *arena, String text, b32 final)
{
    (void)final;
    if (!StrIndexChar(text, '%', SSF_None).exists) return text;
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, text);
    u32 needed = wide ? ExpandEnvironmentStringsW(wide, NULL, 0) : 0;
    wchar *expanded = needed ? ArenaPushArrayNoZero(scratch.arena, needed, wchar) : NULL;
    String result = text;
    if (expanded && ExpandEnvironmentStringsW(wide, expanded, needed)) {
        // A short 8.3 name, as %TEMP% holds, becomes its long one, the name the folder shows under.
        u32 longer = GetLongPathNameW(expanded, NULL, 0);
        wchar *full = longer ? ArenaPushArrayNoZero(scratch.arena, longer, wchar) : NULL;
        if (full && GetLongPathNameW(expanded, full, longer)) expanded = full;
        result = P__FileStringWide(arena, expanded, lstrlenW(expanded));
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P_StartupFlag(String flag)
{
    i32 count = 0;
    wchar **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    b32 result = false;
    Scratch scratch = GetScratch();
    for (i32 i = 1; arguments && i < count && !result; ++i) result = StrIsEqual(P__FileStringWide(scratch.arena, arguments[i], lstrlenW(arguments[i])), flag);
    ReleaseScratch(scratch);
    if (arguments) LocalFree(arguments);
    return result;
}

function String P_StartupOption(Arena *arena, String prefix)
{
    i32 count = 0;
    wchar **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    String result = {0};
    for (i32 i = 1; arguments && i < count && !result.length; ++i) {
        String argument = P__FileStringWide(arena, arguments[i], lstrlenW(arguments[i]));
        if (argument.length > prefix.length && StrIsEqual(StrFromTo(argument, 0, (i64)prefix.length), prefix)) result = StrFrom(argument, prefix.length);
    }
    if (arguments) LocalFree(arguments);
    return result;
}

// One entry of `list` (separated by ';'), matched as Windows matches paths, without case and with or without a trailing backslash.
function b32 P__PathListHas(const wchar *list, const wchar *entry, u32 length)
{
    while (length && entry[length - 1] == L'\\') --length;
    for (const wchar *at = list; *at;) {
        const wchar *end = at;
        while (*end && *end != L';') ++end;
        u32 size = (u32)(end - at);
        while (size && at[size - 1] == L'\\') --size;
        if (size == length && length && CompareStringOrdinal(at, (i32)size, entry, (i32)length, TRUE) == CSTR_EQUAL) return true;
        at = *end ? end + 1 : end;
    }
    return false;
}

function void P_CompleteSearchPath(void)
{
    Scratch scratch = GetScratch();
    u32 room = GetEnvironmentVariableW(L"PATH", NULL, 0);
    u64 capacity = (u64)room + 65536;
    wchar *path = ArenaPushArray(scratch.arena, capacity, wchar);
    u32 length = room ? GetEnvironmentVariableW(L"PATH", path, room) : 0;
    b32 added = false;
    // The machine's first, as the system puts it before the user's.
    struct { HKEY root; const wchar *key; } saved[] = {
        { HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment" },
        { HKEY_CURRENT_USER, L"Environment" },
    };
    for (u32 i = 0; i < CArrayCount(saved); ++i) {
        DWORD size = 0;
        if (RegGetValueW(saved[i].root, saved[i].key, L"Path", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, NULL, &size) != ERROR_SUCCESS || !size) continue;
        wchar *list = ArenaPushArray(scratch.arena, size / sizeof(wchar) + 1, wchar);
        if (RegGetValueW(saved[i].root, saved[i].key, L"Path", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, list, &size) != ERROR_SUCCESS) continue;
        for (const wchar *at = list; *at;) {
            const wchar *end = at;
            while (*end && *end != L';') ++end;
            u32 count = (u32)(end - at);
            path[length] = 0;
            if (count && !P__PathListHas(path, at, count) && length + count + 2 < capacity) {
                if (length && path[length - 1] != L';') path[length++] = L';';
                for (u32 c = 0; c < count; ++c) path[length++] = at[c];
                added = true;
            }
            at = *end ? end + 1 : end;
        }
    }
    path[length] = 0;
    if (added) SetEnvironmentVariableW(L"PATH", path);
    ReleaseScratch(scratch);
}

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
        // Win+E passes its own shell folder, "::{52205fd8-...}", which starts Filey as it starts without a folder; flags are not folders.
        if ((argument[0] == L':' && argument[1] == L':') || (argument[0] == L'-' && argument[1] == L'-')) continue;
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

typedef LONG (WINAPI *P__SuspendCall)(HANDLE);

// Pauses or resumes every process in `group`, or `process` alone without one; yt-dlp, for one, downloads in a process it starts.
// ntdll's NtSuspendProcess does it, which no documented call does for a whole process.
function void P__PauseCommand(HANDLE group, HANDLE process, b32 pause)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    P__SuspendCall call = ntdll ? (P__SuspendCall)(void *)GetProcAddress(ntdll, pause ? "NtSuspendProcess" : "NtResumeProcess") : NULL;
    if (!call) return;
    u64 storage[2 + 64];
    JOBOBJECT_BASIC_PROCESS_ID_LIST *list = (JOBOBJECT_BASIC_PROCESS_ID_LIST *)storage;
    // Past the 64 that fit, the list is cut short but filled.
    if (!group || (!QueryInformationJobObject(group, JobObjectBasicProcessIdList, list, sizeof(storage), NULL) && GetLastError() != ERROR_MORE_DATA)) {
        call(process);
        return;
    }
    for (u32 i = 0; i < list->NumberOfProcessIdsInList; ++i) {
        HANDLE each = OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)list->ProcessIdList[i]);
        // One gone since the list was taken may have left its number to another program.
        BOOL inside = FALSE;
        if (each && IsProcessInJob(each, group, &inside) && inside) call(each);
        if (each) CloseHandle(each);
    }
}

// The programs a command runs are kept in a job object, so that pausing or stopping it does the same to what it started, as yt-dlp's ffmpeg.
// With `together` they also end when it does, or when Filey does.
// Without, as for git's status, helpers such as its file system monitor outlive a command that finished.
function b32 P__RunCommand(Arena *conflict, String directory, const String *arguments, u32 count, const String *environment, u32 environmentCount, b32 errors,
                           u32 timeoutMilliseconds, P_CommandOutput *output, void *user, const P_CancelToken *cancel, P_Progress *progress, b32 together, String input)
{
    if (!count) return false;
    Scratch scratch = GetScratchEx(conflict);
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
    // The program's own environment with `environment` over it, each "NAME=value" ended by a zero, the whole ended by another.
    wchar *block = NULL;
    if (environmentCount) {
        StrBuilder joined = {0};
        wchar *own = GetEnvironmentStringsW();
        for (const wchar *at = own; at && *at; at += lstrlenW(at) + 1) {
            String entry = P__FileStringWide(scratch.arena, at, lstrlenW(at));
            Index equals = StrIndexChar(entry, '=', SSF_None);
            b32 replaced = false;
            for (u32 i = 0; equals.exists && equals.value && i < environmentCount && !replaced; ++i) {
                Index named = StrIndexChar(environment[i], '=', SSF_None);
                // Windows' names match whatever their case.
                replaced = named.exists && named.value == equals.value;
                for (u64 c = 0; replaced && c < named.value; ++c) {
                    char a = entry.value[c], b = environment[i].value[c];
                    replaced = (a >= 'a' && a <= 'z' ? a - 32 : a) == (b >= 'a' && b <= 'z' ? b - 32 : b);
                }
            }
            if (!replaced) StrBuildStr(scratch.arena, &joined, entry), StrBuildChar(scratch.arena, &joined, 0);
        }
        if (own) FreeEnvironmentStringsW(own);
        for (u32 i = 0; i < environmentCount; ++i) StrBuildStr(scratch.arena, &joined, environment[i]), StrBuildChar(scratch.arena, &joined, 0);
        StrBuildChar(scratch.arena, &joined, 0);
        i32 wideLength = MultiByteToWideChar(CP_UTF8, 0, joined.buffer.value, (i32)joined.buffer.length, NULL, 0);
        block = ArenaPushArray(scratch.arena, (u64)wideLength + 1, wchar);
        MultiByteToWideChar(CP_UTF8, 0, joined.buffer.value, (i32)joined.buffer.length, block, wideLength);
    }
    SECURITY_ATTRIBUTES inherit = { .nLength = sizeof(inherit), .bInheritHandle = TRUE };
    HANDLE readEnd = NULL, writeEnd = NULL;
    HANDLE group = CreateJobObjectW(NULL, NULL);
    if (group && together) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = { .BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE };
        SetInformationJobObject(group, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    b32 result = false;
    // A pipe of a megabyte, so that a program writing much, as ffmpeg's decoded sound, goes on while the wait below sleeps for the timer's
    // step, which the system's few kilobytes would hold to a trickle.
    if (folder && CreatePipe(&readEnd, &writeEnd, &inherit, 1 << 20)) {
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
        // What it reads, written once it has started; the pipe holds all of it.
        HANDLE inputRead = NULL, inputWrite = NULL;
        if (input.length && CreatePipe(&inputRead, &inputWrite, &inherit, (DWORD)Max(input.length, (u64)4096))) SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup = { .cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES, .hStdOutput = writeEnd, .hStdError = errors ? writeEnd : NULL, .hStdInput = inputRead };
        PROCESS_INFORMATION process = {0};
        b32 started = CreateProcessW(NULL, wide, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, block, folder, &startup, &process) != 0;
        CloseHandle(writeEnd);
        if (inputRead) CloseHandle(inputRead);
        if (inputWrite) {
            DWORD written = 0;
            if (started) WriteFile(inputWrite, input.value, (DWORD)input.length, &written, NULL);
            CloseHandle(inputWrite);
        }
        if (started) {
            // Where it cannot join one, as inside a job that takes no others, the command alone is paused and stopped.
            if (group && !AssignProcessToJobObject(group, process.hProcess)) CloseHandle(group), group = NULL;
            ResumeThread(process.hThread);
            CloseHandle(process.hThread);
            u64 began = GetTickCount64();
            b32 stopped = false, suspended = false;
            for (;;) {
                if (P_IsCancelled(cancel) || (timeoutMilliseconds && GetTickCount64() - began > timeoutMilliseconds)) {
                    stopped = true;
                    break;
                }
                b32 pause = progress && atomic_load(&progress->paused);
                if (pause != suspended) {
                    P__PauseCommand(group, process.hProcess, pause);
                    suspended = pause;
                }
                // What it wrote before the pause waits in the pipe with it, as sound would otherwise play on from there.
                if (suspended) {
                    Sleep(50);
                    continue;
                }
                DWORD waiting = 0;
                if (!PeekNamedPipe(readEnd, NULL, 0, NULL, &waiting, NULL)) break;
                if (waiting) {
                    char bytes[65536];
                    DWORD read = 0;
                    if (!ReadFile(readEnd, bytes, (DWORD)Min((u64)waiting, sizeof(bytes)), &read, NULL) || !read) break;
                    output(user, StrMake(bytes, read));
                    continue;
                }
                // Done once it has exited and the pipe is drained.
                if (WaitForSingleObject(process.hProcess, suspended ? 50 : 5) == WAIT_OBJECT_0) {
                    if (!PeekNamedPipe(readEnd, NULL, 0, NULL, &waiting, NULL) || !waiting) break;
                }
            }
            DWORD code = 1;
            if (suspended) P__PauseCommand(group, process.hProcess, false);
            if (stopped) {
                if (group) TerminateJobObject(group, 1);
                else TerminateProcess(process.hProcess, 1);
            }
            WaitForSingleObject(process.hProcess, 1000);
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hProcess);
            result = !stopped && code == 0;
        }
        CloseHandle(readEnd);
    }
    if (group) CloseHandle(group);
    ReleaseScratch(scratch);
    return result;
}

typedef struct P__CommandText {
    Arena *arena;
    StrBuilder text;
} P__CommandText;

function void P__CollectOutput(void *user, String text)
{
    P__CommandText *collected = user;
    StrBuildStr(collected->arena, &collected->text, text);
}

function b32 P__RunCollected(Arena *arena, String directory, const String *arguments, u32 count, const String *environment, u32 environmentCount,
                             u32 timeoutMilliseconds, String *output, b32 errors, const P_CancelToken *cancel, b32 together)
{
    P__CommandText collected = { .arena = arena };
    b32 result = P__RunCommand(arena, directory, arguments, count, environment, environmentCount, errors, timeoutMilliseconds, P__CollectOutput, &collected, cancel, NULL,
                               together, (String){0});
    *output = collected.text.buffer;
    return result;
}

function b32 P_RunCommandWith(Arena *arena, String directory, const String *arguments, u32 count, const String *environment, u32 environmentCount,
                              u32 timeoutMilliseconds, String *output, b32 errors, const P_CancelToken *cancel)
{
    return P__RunCollected(arena, directory, arguments, count, environment, environmentCount, timeoutMilliseconds, output, errors, cancel, true);
}

function b32 P_ProgramFound(String name)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FilePathWide(scratch.arena, name);
    b32 result = wide && SearchPathW(NULL, wide, L".exe", 0, NULL, NULL) > 0;
    ReleaseScratch(scratch);
    return result;
}

function b32 P_RunCommand(Arena *arena, String directory, const String *arguments, u32 count, u32 timeoutMilliseconds, String *output, b32 errors)
{
    return P__RunCollected(arena, directory, arguments, count, NULL, 0, timeoutMilliseconds, output, errors, NULL, false);
}

function b32 P_RunCommandStream(String directory, const String *arguments, u32 count, b32 errors, P_CommandOutput *output, void *user, const P_CancelToken *cancel,
                                P_Progress *progress)
{
    return P__RunCommand(NULL, directory, arguments, count, NULL, 0, errors, 0, output, user, cancel, progress, true, (String){0});
}

function b32 P_RunCommandInput(Arena *arena, String directory, const String *arguments, u32 count, String input, u32 timeoutMilliseconds, String *output,
                               const P_CancelToken *cancel)
{
    P__CommandText collected = { .arena = arena };
    b32 result = P__RunCommand(arena, directory, arguments, count, NULL, 0, false, timeoutMilliseconds, P__CollectOutput, &collected, cancel, NULL, true, input);
    *output = collected.text.buffer;
    return result;
}

function String P_EnvironmentValue(Arena *arena, String name)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, name);
    u32 room = wide ? GetEnvironmentVariableW(wide, NULL, 0) : 0;
    wchar *value = ArenaPushArray(scratch.arena, (u64)room + 1, wchar);
    u32 length = room ? GetEnvironmentVariableW(wide, value, room) : 0;
    String result = length && length < room ? P__FileStringWide(arena, value, (i32)length) : (String){0};
    ReleaseScratch(scratch);
    return result;
}

function b32 P_IsDirectory(String path)
{
    if (P__IsDevicePath(path)) {
        b32 folder = false;
        PIDLIST_ABSOLUTE list = P__DeviceIdList(path, &folder);
        ILFree(list);
        return list && folder;
    }
    if (P__IsNetworkPath(path)) return StrIsEqual(path, P_NetworkFolder());
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

// Removes `path` with everything in it, read-only files too; links and junctions are removed, never followed. What will not go, as a file
// another program has open, stays for the next try.
function void P__RemoveTree(String path)
{
    Scratch scratch = GetScratch();
    wchar *pattern = P__FileExtendedPath(scratch.arena, StrFormat(scratch.arena, "%S/*", path));
    WIN32_FIND_DATAW found;
    HANDLE find = pattern ? FindFirstFileExW(pattern, FindExInfoBasic, &found, FindExSearchNameMatch, NULL, 0) : INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const wchar *name = found.cFileName;
            if (name[0] == L'.' && (!name[1] || (name[1] == L'.' && !name[2]))) continue;
            String child = StrFormat(scratch.arena, "%S/%S", path, P__FileStringWide(scratch.arena, name, lstrlenW(name)));
            b32 folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, link = (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            wchar *wide = P__FileExtendedPath(scratch.arena, child);
            if (folder && !link) {
                P__RemoveTree(child);
            } else if (wide) {
                SetFileAttributesW(wide, FILE_ATTRIBUTE_NORMAL);
                if (folder) RemoveDirectoryW(wide);
                else DeleteFileW(wide);
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    wchar *own = P__FileExtendedPath(scratch.arena, path);
    if (own) RemoveDirectoryW(own);
    ReleaseScratch(scratch);
}

// Whether process `id` may still run: one that cannot be looked at, as another user's, may.
function b32 P__ProcessRunning(u32 id)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process) return GetLastError() != ERROR_INVALID_PARAMETER;
    DWORD code = 0;
    b32 result = !GetExitCodeProcess(process, &code) || code == STILL_ACTIVE;
    CloseHandle(process);
    return result;
}

function void P__RemoveStaleRunFolders(String root, u32 own)
{
    Scratch scratch = GetScratch();
    wchar *pattern = P__FileExtendedPath(scratch.arena, StrFormat(scratch.arena, "%S/*", root));
    WIN32_FIND_DATAW found;
    HANDLE find = pattern ? FindFirstFileExW(pattern, FindExInfoBasic, &found, FindExSearchNameMatch, NULL, 0) : INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            // Only a folder named by a process's number.
            u64 id = 0;
            b32 numbered = found.cFileName[0] != 0 && (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
            for (const wchar *c = found.cFileName; numbered && *c; ++c) {
                numbered = *c >= L'0' && *c <= L'9' && id < UINT32_MAX;
                id = id * 10 + (u64)(*c - L'0');
            }
            if (numbered && id <= UINT32_MAX && (u32)id != own && !P__ProcessRunning((u32)id)) {
                P__RemoveTree(StrFormat(scratch.arena, "%S/%S", root, P__FileStringWide(scratch.arena, found.cFileName, lstrlenW(found.cFileName))));
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    ReleaseScratch(scratch);
}

function void P_RemoveStaleRunFolders(void)
{
    Scratch scratch = GetScratch();
    String run = P_RunFolder(scratch.arena);
    Index slash = StrIndexChar(run, '/', SSF_SearchBackwards);
    if (run.length && slash.exists) P__RemoveStaleRunFolders(StrFromTo(run, 0, (i64)slash.value), (u32)GetCurrentProcessId());
    ReleaseScratch(scratch);
}

function b32 P_PathExists(String path)
{
    if (P__IsDevicePath(path)) {
        PIDLIST_ABSOLUTE list = P__DeviceIdList(path, NULL);
        ILFree(list);
        return list != NULL;
    }
    if (P__IsNetworkPath(path)) return StrIsEqual(path, P_NetworkFolder());
    Scratch scratch = GetScratch();
    wchar *wide = P__FileExtendedPath(scratch.arena, path);
    b32 result = wide && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
    ReleaseScratch(scratch);
    return result;
}

function b32 P_FileSize(String path, u64 *size)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FileExtendedPath(scratch.arena, path);
    WIN32_FILE_ATTRIBUTE_DATA data = {0};
    b32 result = wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &data) && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    *size = result ? ((u64)data.nFileSizeHigh << 32) | data.nFileSizeLow : 0;
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
