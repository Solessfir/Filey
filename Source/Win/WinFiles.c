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

function String P_FormatFileTime(Arena *arena, i64 modifiedTimeUnix)
{
    i64 ticks = 0;
    if (ckd_add(&ticks, modifiedTimeUnix, INT64_C(11644473600)) || ticks < 0 || ckd_mul(&ticks, ticks, INT64_C(10000000))) return (String){0};
    FILETIME time = {.dwLowDateTime = (u32)((u64)ticks & UINT32_MAX), .dwHighDateTime = (u32)((u64)ticks >> 32)};
    SYSTEMTIME utc = {0};
    SYSTEMTIME local = {0};
    if (!FileTimeToSystemTime(&time, &utc) || !SystemTimeToTzSpecificLocalTime(NULL, &utc, &local) || local.wYear > 9999) return (String){0};
    return StrFormat(arena, "%04u-%02u-%02u %02u:%02u", (u32)local.wYear, (u32)local.wMonth, (u32)local.wDay, (u32)local.wHour, (u32)local.wMinute);
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
            P_File file = {
                .name = P__FileStringWide(arena, data.cFileName, nameLength),
                .size = ((u64)data.nFileSizeHigh << 32) | data.nFileSizeLow,
                .modifiedTimeUnix = (i64)(time / UINT64_C(10000000)) - INT64_C(11644473600),
                .kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? P_FileKind_Directory : (data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) ? P_FileKind_Other : P_FileKind_File,
                .hidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0,
                .metadataValid = true,
                .symbolicLink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
            };
            ArrayPush(arena, result.files, file);
        } while (FindNextFileW(find, &data));
        if (!result.cancelled && GetLastError() != ERROR_NO_MORE_FILES) error = GetLastError();
        FindClose(find);
    }
    if (P_IsCancelled(cancel)) result.cancelled = true;
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

typedef struct P__MeasureFrame {
    HANDLE find;
    u32 length;    // where names inside this folder start in the path buffer
    b32 fresh;     // `data` holds the first entry, not yet counted
    WIN32_FIND_DATAW data;
} P__MeasureFrame;

typedef struct P__MeasureStack { ArrayHeader header; P__MeasureFrame *v; } P__MeasureStack;

// Opens the folder named by buffer[0, length) as a new level of the walk.
function b32 P__MeasurePush(Arena *arena, P__MeasureStack *stack, wchar *buffer, u32 length, u32 capacity)
{
    if (!length || length + 3 > capacity) return false;
    if (buffer[length - 1] != L'\\') buffer[length++] = L'\\';
    buffer[length] = L'*';
    buffer[length + 1] = 0;
    P__MeasureFrame frame = { .length = length, .fresh = true };
    frame.find = FindFirstFileExW(buffer, FindExInfoBasic, &frame.data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (frame.find == INVALID_HANDLE_VALUE) return false;
    ArrayPush(arena, *stack, frame);
    return true;
}

function P_DirectoryTotals P_MeasureDirectory(String path, const P_CancelToken *cancel)
{
    P_DirectoryTotals result = {0};
    Scratch scratch = GetScratch();
    wchar *root = P__FileExtendedPath(scratch.arena, path);
    // One path buffer for the whole walk, at the longest path Windows allows.
    u32 capacity = 32768;
    wchar *buffer = ArenaPushArray(scratch.arena, capacity, wchar);
    u32 length = 0;
    while (root && root[length] && length + 1 < capacity) {
        buffer[length] = root[length];
        ++length;
    }
    P__MeasureStack stack = {0};
    result.complete = root && P__MeasurePush(scratch.arena, &stack, buffer, length, capacity);
    while (stack.header.count) {
        if (P_IsCancelled(cancel)) {
            result.complete = false;
            break;
        }
        P__MeasureFrame *frame = &stack.v[stack.header.count - 1];
        if (!frame->fresh && !FindNextFileW(frame->find, &frame->data)) {
            FindClose(frame->find);
            --stack.header.count;
            continue;
        }
        frame->fresh = false;
        const wchar *name = frame->data.cFileName;
        if (name[0] == L'.' && (!name[1] || (name[1] == L'.' && !name[2]))) continue;
        if (frame->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ++result.folders;
            if (frame->data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            u32 nameLength = (u32)lstrlenW(name);
            u32 at = frame->length;
            if (at + nameLength + 3 > capacity) continue;
            MemCopy(buffer + at, name, nameLength * sizeof(wchar));
            P__MeasurePush(scratch.arena, &stack, buffer, at + nameLength, capacity);
        } else {
            ++result.files;
            result.bytes += ((u64)frame->data.nFileSizeHigh << 32) | frame->data.nFileSizeLow;
        }
    }
    for (u64 i = 0; i < stack.header.count; ++i) FindClose(stack.v[i].find);
    ReleaseScratch(scratch);
    return result;
}
