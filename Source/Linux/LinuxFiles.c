#include <dirent.h>
#include <dlfcn.h>
#include <locale.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <execinfo.h>

extern char **environ;

function char *P__FilePathCString(Arena *arena, String path)
{
    if (StrIndexChar(path, '\0', SSF_None).exists) {
        errno = EINVAL;
        return NULL;
    }
    char *result = ArenaPushArray(arena, path.length + 1, char);
    MemCopy(result, path.value, path.length);
    return result;
}

function b32 P_FileErrorIsCancel(P_FileError error)
{
    return error.code == ECANCELED;
}

function P_FileError P__FileError(Arena *arena, u32 code)
{
    char message[256] = {0};
    char *text = strerror_r((i32)code, message, sizeof(message));
    return (P_FileError){.code = code, .message = StrCopy(arena, StrFromCStr(text))};
}

function String P_AbsolutePath(Arena *arena, String path)
{
    if (StrIndexChar(path, '\0', SSF_None).exists) {
        errno = EINVAL;
        return (String){0};
    }
    Scratch scratch = GetScratchEx(arena);
    StrBuilder full = {0};
    if (!path.length || path.value[0] != '/') {
        u64 capacity = 256;
        char *cwd = NULL;
        for (;;) {
            cwd = ArenaPushArray(scratch.arena, capacity, char);
            if (getcwd(cwd, capacity)) break;
            if (errno != ERANGE) {
                ReleaseScratch(scratch);
                return (String){0};
            }
            capacity *= 2;
        }
        StrBuildCStr(scratch.arena, &full, cwd);
        StrBuildChar(scratch.arena, &full, '/');
    }
    StrBuildStr(scratch.arena, &full, path);
    ArrayStr components = StrSplit(scratch.arena, full.buffer, '/');
    u64 count = 0;
    for (u64 i = 0; i < components.header.count; ++i) {
        String part = components.v[i];
        if (StrIsEqual(part, S("."))) continue;
        if (StrIsEqual(part, S(".."))) {
            if (count) --count;
        } else {
            components.v[count++] = part;
        }
    }
    StrBuilder result = {0};
    StrBuildChar(arena, &result, '/');
    for (u64 i = 0; i < count; ++i) {
        if (i) StrBuildChar(arena, &result, '/');
        StrBuildStr(arena, &result, components.v[i]);
    }
    ReleaseScratch(scratch);
    return result.buffer;
}

function String P_FormatFileTime(Arena *arena, i64 modifiedTimeUnix)
{
    time_t time = (time_t)modifiedTimeUnix;
    struct tm local = {0};
    if (!localtime_r(&time, &local) || local.tm_year < -1899 || local.tm_year > 8099) return (String){0};
    return StrFormat(arena, "%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
}

function i64 P_LocalOffset(i64 timeUnix)
{
    time_t time = (time_t)timeUnix;
    struct tm local = {0};
    return localtime_r(&time, &local) ? (i64)local.tm_gmtoff : 0;
}

function String P_FormatFileDate(Arena *arena, i64 timeUnix)
{
    time_t time = (time_t)timeUnix;
    struct tm local = {0};
    if (!localtime_r(&time, &local) || local.tm_year < -1899 || local.tm_year > 8099) return (String){0};
    return StrFormat(arena, "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

function i64 P_NowUnix(void)
{
    return (i64)time(NULL);
}

// The system's libarchive; every Arch system has it, as pacman uses it.
// libarchive turns names into the thread's character set, which is ASCII until a locale is chosen, so a thread that opens an archive
// switches to UTF-8 first; names are UTF-8 everywhere else here anyway. It is made while the library loads, which one job does.
global locale_t P__archiveLocale;

function void *P__ArchiveSymbol(const char *name)
{
    local_persist b32 tried;
    local_persist void *library;
    if (!tried) {
        tried = true;
        library = dlopen("libarchive.so.13", RTLD_NOW | RTLD_LOCAL);
        P__archiveLocale = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
    }
    return library ? dlsym(library, name) : NULL;
}

function const void *P__ArchiveNativePath(Arena *arena, String path)
{
    if (P__archiveLocale) uselocale(P__archiveLocale);
    return P__FilePathCString(arena, path);
}

#define P__ArchiveOpenReadName "archive_read_open_filename"
#define P__ArchiveOpenWriteName "archive_write_open_filename"
enum { P__ArchiveCancelled = ECANCELED, P__ArchiveUnsupported = ENOSYS, P__ArchiveFailed = EIO };

#include "Platform/PlatformArchive.c"

function P_DirectoryResult P_EnumerateDirectory(Arena *arena, String path, const P_CancelToken *cancel)
{
    P_DirectoryResult result = {0};
    if (P_IsCancelled(cancel)) {
        result.cancelled = true;
        return result;
    }
    ArenaMark mark = ArenaGetMark(arena);
    Scratch scratch = GetScratchEx(arena);
    char *directory = P__FilePathCString(scratch.arena, path.length ? path : S("."));
    DIR *stream = directory ? opendir(directory) : NULL;
    u32 error = stream ? 0 : (u32)errno;
    if (stream) {
        for (;;) {
            if (P_IsCancelled(cancel)) {
                result.cancelled = true;
                break;
            }
            errno = 0;
            struct dirent *entry = readdir(stream);
            if (!entry) {
                error = (u32)errno;
                break;
            }
            if (entry->d_name[0] == '.' && (!entry->d_name[1] || (entry->d_name[1] == '.' && !entry->d_name[2]))) continue;
            P_File file = {
                .name = StrCopy(arena, StrFromCStr(entry->d_name)),
                .kind = P_FileKind_Other,
                .hidden = entry->d_name[0] == '.',
            };
            // statx, unlike stat, also says when the item was created.
            struct statx metadata = {0};
            u32 wanted = STATX_BASIC_STATS | STATX_BTIME;
            if (statx(dirfd(stream), entry->d_name, AT_SYMLINK_NOFOLLOW, wanted, &metadata) == 0) {
                file.symbolicLink = S_ISLNK(metadata.stx_mode);
                file.metadataValid = !file.symbolicLink || statx(dirfd(stream), entry->d_name, 0, wanted, &metadata) == 0;
                if (file.metadataValid) {
                    file.kind = S_ISDIR(metadata.stx_mode) ? P_FileKind_Directory : S_ISREG(metadata.stx_mode) ? P_FileKind_File : P_FileKind_Other;
                    file.size = metadata.stx_size;
                    file.mode = (u16)(metadata.stx_mode & 07777);
                    file.modifiedTimeUnix = (i64)metadata.stx_mtime.tv_sec;
                    file.accessedTimeUnix = (i64)metadata.stx_atime.tv_sec;
                    file.createdTimeUnix = (metadata.stx_mask & STATX_BTIME) ? (i64)metadata.stx_btime.tv_sec : 0;
                    file.modifiedTicks = metadata.stx_mtime.tv_nsec / 100;
                    file.accessedTicks = metadata.stx_atime.tv_nsec / 100;
                    file.createdTicks = (metadata.stx_mask & STATX_BTIME) ? metadata.stx_btime.tv_nsec / 100 : 0;
                }
            }
            ArrayPush(arena, result.files, file);
        }
        if (closedir(stream) != 0 && !error) error = (u32)errno;
    }
    if (P_IsCancelled(cancel)) result.cancelled = true;
    ReleaseScratch(scratch);
    if (!error && !result.cancelled) {
        u64 end = path.length;
        while (end > 0 && path.value[end - 1] == '/') --end;
        u64 start = end;
        while (start > 0 && path.value[start - 1] != '/') --start;
        result.hidden = end > start && path.value[start] == '.';
    }
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

// Where crash reports go and the version they are named after, ready before any crash, as a signal handler can do little.
global char P__crashFolder[PATH_MAX];
global char P__crashVersion[32];

// Appends `text`, or the number `value` with at least `digits` digits, to `out` at `*at`, as far as `room` lets it.
function void P__CrashAppend(char *out, u64 room, u64 *at, const char *text, u64 value, u32 digits)
{
    char number[24];
    u32 length = 0;
    if (!text) {
        do number[sizeof(number) - 1 - length++] = (char)('0' + value % 10), value /= 10; while ((value || length < digits) && length < sizeof(number));
        text = number + sizeof(number) - length;
    }
    for (u64 i = 0; (length ? i < length : text[i] != 0) && *at + 1 < room; ++i) out[(*at)++] = text[i];
    out[*at] = 0;
}

function void P__OnCrash(i32 signal)
{
    if (P__crashFolder[0]) {
        mkdir(P__crashFolder, 0700);
        char path[PATH_MAX + 64];
        u64 at = 0;
        time_t now = time(NULL);
        P__CrashAppend(path, sizeof(path), &at, P__crashFolder, 0, 0);
        P__CrashAppend(path, sizeof(path), &at, "/Filey-", 0, 0);
        P__CrashAppend(path, sizeof(path), &at, P__crashVersion, 0, 0);
        P__CrashAppend(path, sizeof(path), &at, "-", 0, 0);
        P__CrashAppend(path, sizeof(path), &at, NULL, (u64)now, 1);
        P__CrashAppend(path, sizeof(path), &at, ".txt", 0, 0);
        i32 file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (file >= 0) {
            char head[96];
            u64 used = 0;
            P__CrashAppend(head, sizeof(head), &used, "Filey ", 0, 0);
            P__CrashAppend(head, sizeof(head), &used, P__crashVersion, 0, 0);
            P__CrashAppend(head, sizeof(head), &used, " stopped on signal ", 0, 0);
            P__CrashAppend(head, sizeof(head), &used, NULL, (u64)signal, 1);
            P__CrashAppend(head, sizeof(head), &used, "\n", 0, 0);
            if (write(file, head, used) < 0) {}
            void *frames[64];
            backtrace_symbols_fd(frames, backtrace(frames, 64), file);
            close(file);
        }
    }
    // The system's own handling then, as the handler was set to go back to it.
    raise(signal);
}

function void P_CatchCrashes(String folder, String version)
{
    if (folder.length >= sizeof(P__crashFolder)) return;
    MemCopy(P__crashFolder, folder.value, folder.length);
    P__crashFolder[folder.length] = 0;
    u64 length = Min(version.length, (u64)sizeof(P__crashVersion) - 1);
    MemCopy(P__crashVersion, version.value, length);
    P__crashVersion[length] = 0;
    // The first backtrace loads what it needs, which a signal handler could not.
    void *warm[1];
    backtrace(warm, 1);
    struct sigaction action = { .sa_handler = P__OnCrash, .sa_flags = (i32)(SA_RESETHAND | SA_NODEFER) };
    local_persist const i32 signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (u64 i = 0; i < CArrayCount(signals); ++i) sigaction(signals[i], &action, NULL);
}

// The executable `name` runs: itself when it has a slash, else the first match on PATH.
function char *P__FindExecutable(Arena *arena, String name, u32 *error)
{
    const char *environmentPath = getenv("PATH");
    b32 direct = StrIndexChar(name, '/', SSF_None).exists;
    String searchPath = direct ? S("") : environmentPath ? StrFromCStr(environmentPath) : S("/bin:/usr/bin");
    *error = ENOENT;
    for (u64 from = 0, to = 0; to <= searchPath.length; ++to) {
        if (to != searchPath.length && searchPath.value[to] != ':') continue;
        String directory = StrFromTo(searchPath, from, (i64)to);
        String candidate = direct ? name : StrFormat(arena, "%S%S%S", directory, directory.length ? S("/") : S(""), name);
        String absolute = P_AbsolutePath(arena, candidate);
        if (!absolute.length) {
            *error = (u32)errno;
            return NULL;
        }
        char *executable = P__FilePathCString(arena, absolute);
        struct stat metadata = {0};
        i32 status = stat(executable, &metadata);
        if (status == 0 && S_ISREG(metadata.st_mode)) {
            if (access(executable, X_OK) == 0) {
                *error = 0;
                return executable;
            }
            if (errno == EACCES) *error = EACCES;
        }
        if (status != 0 && errno == EACCES) *error = EACCES;
        from = to + 1;
    }
    return NULL;
}

// Starts `executable` with `args` in `directory` (the current one when NULL), detached from Filey.
// Returns the error of starting it, which includes a failed exec.
function u32 P__Spawn(const char *executable, char **args, const char *directory)
{
    u32 error = 0;
    i32 pipeEnds[2] = {0};
    if (pipe2(pipeEnds, O_CLOEXEC) != 0) error = (u32)errno;
    else {
        // The orphaned launcher is reaped by init; the error pipe closes on successful exec.
        pid_t child = _Fork();
        if (child == 0) {
            close(pipeEnds[0]);
            pid_t launcher = _Fork();
            if (launcher == 0) {
                setsid();
                if (!directory || chdir(directory) == 0) execve(executable, args, environ);
            }
            if (launcher <= 0) {
                u32 launchError = (u32)errno;
                while (write(pipeEnds[1], &launchError, sizeof(launchError)) < 0 && errno == EINTR) {}
            }
            _exit(0);
        }
        if (child < 0) error = (u32)errno;
        close(pipeEnds[1]);
        if (child > 0) {
            pid_t waited;
            do {
                waited = waitpid(child, NULL, 0);
            } while (waited < 0 && errno == EINTR);
            if (waited < 0) error = (u32)errno;
            u64 received = 0;
            u32 launchError = 0;
            while (!error && received < sizeof(launchError)) {
                i64 count = read(pipeEnds[0], (u8 *)&launchError + received, sizeof(launchError) - received);
                if (count < 0 && errno == EINTR) continue;
                if (count < 0) error = (u32)errno;
                if (count <= 0) break;
                received += (u64)count;
            }
            if (!error && received) error = received == sizeof(launchError) ? launchError : EIO;
        }
        close(pipeEnds[0]);
    }
    return error;
}

function String P_ExecutablePath(Arena *arena)
{
    char executable[PATH_MAX];
    i64 length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    return length > 0 ? StrCopy(arena, StrMake(executable, (u64)length)) : (String){0};
}

function P_FileError P_ReplaceExecutable(Arena *arena, String executable, String replacement)
{
    Scratch scratch = GetScratchEx(arena);
    char *target = P__FilePathCString(scratch.arena, executable), *source = P__FilePathCString(scratch.arena, replacement);
    u32 error = target && source ? 0 : EINVAL;
    if (!error && chmod(source, 0755) != 0) error = (u32)errno;
    if (!error && rename(source, target) != 0) {
        error = (u32)errno;
        // On another file system the new one is copied beside the old first, so that it is put in place whole.
        if (error == EXDEV) {
            String contents = {0}, beside = StrFormat(scratch.arena, "%S.new", executable);
            char *besideName = P__FilePathCString(scratch.arena, beside);
            error = !P_ReadFile(scratch.arena, replacement, &contents) ? EIO : P_WriteFile(scratch.arena, beside, contents).code;
            if (!error && (chmod(besideName, 0755) != 0 || rename(besideName, target) != 0)) error = (u32)errno;
        }
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function void P_RemoveReplaced(String executable) {}

function P_FileError P_Restart(Arena *arena, String executable)
{
    Scratch scratch = GetScratchEx(arena);
    char *program = P__FilePathCString(scratch.arena, executable), *after = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "--after=%d", (i32)getpid()));
    char *args[] = {program, after, NULL};
    u32 error = program && after ? P__Spawn(program, args, NULL) : EINVAL;
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function void P_WaitForExit(u32 process, u32 timeoutMilliseconds)
{
    for (u64 start = PlatformNowMicroseconds(); kill((pid_t)process, 0) == 0 && PlatformNowMicroseconds() - start < (u64)timeoutMilliseconds * 1000;) {
        struct timespec pause = {0, 20 * 1000000};
        nanosleep(&pause, NULL);
    }
}

function P_FileError P_OpenWindow(Arena *arena, String path, String option)
{
    Scratch scratch = GetScratchEx(arena);
    char executable[PATH_MAX];
    i64 length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    char *folder = P__FilePathCString(scratch.arena, path);
    u32 error = length > 0 && folder ? 0 : (u32)errno;
    if (!error) {
        executable[length] = 0;
        // The flag gives it a window of its own rather than a tab in this one.
        char *extra = option.length ? P__FilePathCString(scratch.arena, option) : NULL;
        char *args[] = {executable, "--new-window", folder, extra, NULL};
        error = P__Spawn(executable, args, NULL);
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

// Wayland drags are asynchronous: this starts one and returns, and the window code handles the rest.
function b32 LinuxStartDrag(String directory, const String *names, u64 count);

function b32 P_DragFiles(String directory, const String *names, u64 count)
{
    return LinuxStartDrag(directory, names, count);
}

function String P_RunFolder(Arena *arena);

// A drop takes files, so the items are taken out into a folder of their own first.
// ponytail: that holds the window while a large item is taken out; taking it out when the target asks for the data would not.
function b32 P_DragArchiveItems(String archive, String inner, const String *names, u64 count)
{
    Arena arena = {0};
    String run = P_RunFolder(&arena);
    local_persist u32 drags;
    String folder = run.length ? StrFormat(&arena, "%S/drag-%u", run, ++drags) : (String){0};
    char *native = folder.length ? P__FilePathCString(&arena, folder) : NULL;
    b32 result = native && mkdir(native, 0700) == 0 && !P_ExtractArchive(&arena, archive, inner, names, count, folder, NULL, NULL).code && LinuxStartDrag(folder, names, count);
    ArenaRelease(&arena);
    return result;
}

// Where dropped files go; the window's drop handler reads it.
// While `takesFolders` is on, drops are kept in `dropped` for the program instead. The window sets `pending` while a drag is over it.
global struct {
    char path[PATH_MAX];
    u64 length;
    b32 takesFolders;
    b32 pending;
    Arena droppedArena;
    ArrayStr dropped;
    Arena filesArena;     // the files of a drop, until the program takes them
    ArrayStr files;
    String filesDirectory;
    b32 filesMove;
} P__drop;

function void P_SetDropTakesFolders(b32 takes) { P__drop.takesFolders = takes; }

// Wayland drags have no handlers of a file's own, so a drop on a file goes to its folder.
function void P_SetDropOnFile(b32 onFile) { (void)onFile; }

function b32 P_DropPending(void) { return P__drop.pending; }

function ArrayStr P_TakeDroppedFolders(Arena *arena)
{
    ArrayStr result = {0};
    for (u64 i = 0; i < P__drop.dropped.header.count; ++i) ArrayPush(arena, result, StrCopy(arena, P__drop.dropped.v[i]));
    P__drop.dropped = (ArrayStr){0};
    ArenaClear(&P__drop.droppedArena);
    return result;
}

// Keeps the folders among `paths` for P_TakeDroppedFolders.
function void P__KeepDroppedFolders(const ArrayStr *paths)
{
    Scratch scratch = GetScratch();
    for (u64 i = 0; i < paths->header.count; ++i) {
        char *native = P__FilePathCString(scratch.arena, paths->v[i]);
        struct stat metadata;
        if (native && stat(native, &metadata) == 0 && S_ISDIR(metadata.st_mode)) {
            ArrayPush(&P__drop.droppedArena, P__drop.dropped, StrCopy(&P__drop.droppedArena, paths->v[i]));
        }
    }
    ReleaseScratch(scratch);
}

function void P_SetDropDirectory(String path)
{
    P__drop.length = path.length < sizeof(P__drop.path) ? path.length : 0;
    MemCopy(P__drop.path, path.value, P__drop.length);
}

function String P__FilePathFromUri(Arena *arena, String uri);

// Parses a file list the way the clipboard and drag and drop carry it: GNOME's starts with "copy" or "cut", and both list file:// URIs.
function void P__ParseFileList(Arena *arena, String list, b32 gnome, ArrayStr *paths, b32 *cut)
{
    *paths = (ArrayStr){0};
    *cut = false;
    Scratch scratch = GetScratchEx(arena);
    ArrayStr lines = StrSplit(scratch.arena, list, '\n');
    for (u64 i = 0; i < lines.header.count; ++i) {
        String line = lines.v[i];
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        if (gnome && i == 0) {
            *cut = StrIsEqual(line, S("cut"));
            continue;
        }
        if (!line.length || line.value[0] == '#') continue;
        String path = P__FilePathFromUri(arena, line);
        if (path.length) ArrayPush(arena, *paths, path);
    }
    ReleaseScratch(scratch);
}

// Keeps the files dropped into `directory` for P_TakeDroppedFiles.
// ponytail: one drop is kept until the program takes it on the next frame; a second one before then joins it, into the newer folder.
function void P__KeepDroppedFiles(const ArrayStr *paths, String directory, b32 move)
{
    for (u64 i = 0; i < paths->header.count; ++i) ArrayPush(&P__drop.filesArena, P__drop.files, StrCopy(&P__drop.filesArena, paths->v[i]));
    P__drop.filesDirectory = StrCopy(&P__drop.filesArena, directory);
    P__drop.filesMove = move;
}

function ArrayStr P_TakeShellDropped(Arena *arena, String *directory)
{
    (void)arena;
    *directory = (String){0};
    return (ArrayStr){0};
}

function ArrayStr P_TakeDroppedFiles(Arena *arena, String *directory, b32 *move)
{
    ArrayStr result = {0};
    for (u64 i = 0; i < P__drop.files.header.count; ++i) ArrayPush(arena, result, StrCopy(arena, P__drop.files.v[i]));
    *directory = StrCopy(arena, P__drop.filesDirectory);
    *move = P__drop.filesMove;
    P__drop.files = (ArrayStr){0};
    P__drop.filesDirectory = (String){0};
    ArenaClear(&P__drop.filesArena);
    return result;
}

// A '/' would turn a rename into a move.
function b32 P__ValidFileName(String name)
{
    return name.length && !StrIsEqual(name, S(".")) && !StrIsEqual(name, S("..")) && !StrIndexChar(name, '/', SSF_None).exists;
}

function P_FileError P_SetMode(Arena *arena, String path, u16 mode)
{
    Scratch scratch = GetScratchEx(arena);
    char *native = P__FilePathCString(scratch.arena, path);
    u32 error = !native ? (u32)errno : chmod(native, (mode_t)(mode & 07777)) != 0 ? (u32)errno : 0;
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P_RenamePath(Arena *arena, String directory, String from, String to)
{
    if (!P__ValidFileName(from) || !P__ValidFileName(to)) return P__FileError(arena, EINVAL);
    Scratch scratch = GetScratchEx(arena);
    char *source = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "%S/%S", directory, from));
    char *target = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "%S/%S", directory, to));
    u32 error = 0;
    if (!source || !target) {
        error = (u32)errno;
    } else if (renameat2(AT_FDCWD, source, AT_FDCWD, target, RENAME_NOREPLACE) != 0) {
        error = (u32)errno;
        // ponytail: filesystems without RENAME_NOREPLACE get a check-then-rename race; fine for interactive renames.
        if (error == EINVAL || error == ENOSYS) {
            struct stat existing;
            error = lstat(target, &existing) == 0 ? EEXIST : rename(source, target) == 0 ? 0 : (u32)errno;
        }
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

typedef struct P__Watch {
    i32 notify;
    i32 stop;
    pthread_t thread;
    b32 started;
    atomic_bool changed;
    P_JobWakeCallback *wake;
    void *user;
} P__Watch;

function void *P__WatchThread(void *user)
{
    P__Watch *watch = user;
    struct pollfd fds[2] = { { .fd = watch->stop, .events = POLLIN }, { .fd = watch->notify, .events = POLLIN } };
    u8 events[4096];
    for (;;) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[0].revents) break;
        if (fds[1].revents & POLLIN) {
            while (read(watch->notify, events, sizeof(events)) > 0) {}
            // Only the first change since the program last looked needs a wakeup.
            if (!atomic_exchange(&watch->changed, true) && watch->wake) watch->wake(watch->user);
            // Changes during the pause wait in the queue.
            if (poll(fds, 1, P_WatchPause) > 0) break;
        } else if (fds[1].revents) {
            break;
        }
    }
    return NULL;
}

function b32 P_DirectoryWatchStart(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user)
{
    P_DirectoryWatchStop(watch);
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, path);
    P__Watch *state = ArenaPushStruct(&watch->arena, P__Watch);
    *state = (P__Watch){ .notify = -1, .stop = -1, .wake = wake, .user = user };
    watch->native = state;
    state->notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    u32 mask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
    b32 ok = native && state->notify >= 0 && inotify_add_watch(state->notify, native, mask) >= 0;
    ReleaseScratch(scratch);
    if (ok) state->stop = eventfd(0, EFD_CLOEXEC);
    ok = ok && state->stop >= 0 && pthread_create(&state->thread, NULL, P__WatchThread, state) == 0;
    state->started = ok;
    if (!ok) P_DirectoryWatchStop(watch);
    return ok;
}

function b32 P_DirectoryWatchStartTree(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user)
{
    return P_DirectoryWatchStart(watch, path, wake, user);
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
        if (state->started) {
            u64 one = 1;
            while (write(state->stop, &one, sizeof(one)) < 0 && errno == EINTR) {}
            pthread_join(state->thread, NULL);
        }
        if (state->stop >= 0) close(state->stop);
        if (state->notify >= 0) close(state->notify);
    }
    ArenaRelease(&watch->arena);
    *watch = (P_DirectoryWatch){0};
}

// ponytail: no computer icon yet; load the theme's "computer" icon if the glyph looks out of place.
// nemo's name for the computer, which P_LoadFileIcon gives the theme's icon of it.
function String P_ComputerIconPath(void) { return S("computer:///"); }

// ponytail: none until "//server" paths list a server's shares here, through smbclient or GVFS's smb mounts.
function String P_NetworkFolder(void) { return (String){0}; }

function b32 P_PathsIgnoreCase(void) { return false; }

function b32 P_IsVirtualFolder(String path)
{
    local_persist const char *virtuals[] = { "/proc", "/sys", "/dev", "/run" };
    for (u32 i = 0; i < CArrayCount(virtuals); ++i) {
        if (StrIsEqual(path, StrFromCStr(virtuals[i]))) return true;
    }
    return false;
}

function i64 P_FileCreatedTime(String path)
{
    Scratch scratch = GetScratch();
    char *name = P__FilePathCString(scratch.arena, path);
    struct statx info = {0};
    b32 found = name && statx(AT_FDCWD, name, 0, STATX_BTIME, &info) == 0 && (info.stx_mask & STATX_BTIME);
    ReleaseScratch(scratch);
    return found ? (i64)info.stx_btime.tv_sec : 0;
}

function i64 P_FileModifiedTime(String path)
{
    Scratch scratch = GetScratch();
    char *name = P__FilePathCString(scratch.arena, path);
    struct stat info;
    b32 found = name && stat(name, &info) == 0;
    ReleaseScratch(scratch);
    return found ? (i64)info.st_mtime : 0;
}

function b32 P_CountFolder(Arena *arena, String path, P_DirectoryTotals *totals, ArrayStr *subfolders)
{
    Scratch scratch = GetScratchEx(arena);
    char *cpath = P__FilePathCString(scratch.arena, path.length ? path : S("."));
    DIR *stream = cpath ? opendir(cpath) : NULL;
    String parent = path.length > 1 && path.value[path.length - 1] == '/' ? StrFromTo(path, 0, (i64)path.length - 1) : path;
    struct stat folder = {0};
    b32 device = stream && fstat(dirfd(stream), &folder) == 0;
    // The kernel's own file systems hold no data, and some of their files give sizes that are not any, as /proc/kcore's 128 TB; their
    // files are counted without bytes: proc, sysfs, cgroup, cgroup2, debugfs, tracefs, securityfs, bpf, configfs, pstore and efivarfs.
    b32 sized = true;
    struct statfs system;
    if (stream && fstatfs(dirfd(stream), &system) == 0) {
        local_persist const u32 virtuals[] = { 0x9FA0, 0x62656572, 0x27E0EB, 0x63677270, 0x64626720, 0x74726163, 0x73636673, 0xCAFE4A11,
                                               0x62656570, 0x6165676C, 0xDE5E81E4 };
        for (u64 i = 0; i < CArrayCount(virtuals); ++i) sized &= (u32)system.f_type != virtuals[i];
    }
    if (stream) {
        for (struct dirent *entry; (entry = readdir(stream));) {
            const char *name = entry->d_name;
            if (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))) continue;
            u8 type = entry->d_type;
            struct stat metadata = {0};
            b32 statted = false;
            if (type == DT_UNKNOWN || type == DT_REG) {
                statted = fstatat(dirfd(stream), name, &metadata, AT_SYMLINK_NOFOLLOW) == 0;
                if (statted) type = S_ISDIR(metadata.st_mode) ? DT_DIR : S_ISLNK(metadata.st_mode) ? DT_LNK : DT_REG;
            }
            if (type == DT_DIR) {
                ++totals->folders;
                // A walk stays on its file system, as du -x does.
                // /proc and the like, other drives mounted inside it and btrfs subvolumes are counted as folders but left to their own walks;
                // a snapshot subvolume would otherwise count the whole drive again.
                if (!statted) statted = fstatat(dirfd(stream), name, &metadata, AT_SYMLINK_NOFOLLOW) == 0;
                if (!device || !statted || metadata.st_dev == folder.st_dev) {
                    ArrayPush(arena, *subfolders, StrFormat(arena, "%S/%s", StrIsEqual(parent, S("/")) ? (String){0} : parent, name));
                }
            } else {
                ++totals->files;
                if (sized && statted && S_ISREG(metadata.st_mode) && metadata.st_size > 0) totals->bytes += (u64)metadata.st_size;
            }
        }
        closedir(stream);
    }
    ReleaseScratch(scratch);
    return stream != NULL;
}

function String P_NativePath(Arena *arena, String path)
{
    return StrCopy(arena, path);
}

// `path` percent-encoded the way URI paths are.
function String P__PercentEncode(Arena *arena, String path)
{
    StrBuilder encoded = {0};
    for (u64 i = 0; i < path.length; ++i) {
        u8 c = (u8)path.value[i];
        // What GLib leaves as it is in a file URI, so that the thumbnail cache's names, hashed from the URI, are the ones other programs use.
        b32 plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr("!$&'()*+,-./:=@_~", c));
        if (plain) StrBuildChar(arena, &encoded, (char)c);
        else StrBuildStr(arena, &encoded, StrFormat(arena, "%%%02X", c));
    }
    return encoded.buffer;
}

function String P__FileUri(Arena *arena, String path)
{
    return StrFormat(arena, "file://%S", P__PercentEncode(arena, path));
}

function u8 P__HexDigit(u8 c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0xFF;
}

// The local path a file:// URI names, or empty for other URIs.
// A host part is accepted only when it is empty or "localhost".
function String P__FilePathFromUri(Arena *arena, String uri)
{
    if (uri.length < 7 || !StrIsEqual(StrFromTo(uri, 0, 7), S("file://"))) return (String){0};
    String rest = StrFrom(uri, 7);
    if (!rest.length || rest.value[0] != '/') {
        if (rest.length < 9 || !StrIsEqual(StrFromTo(rest, 0, 9), S("localhost"))) return (String){0};
        rest = StrFrom(rest, 9);
    }
    char *bytes = ArenaPushArrayNoZero(arena, Max(rest.length, (u64)1), char);
    u64 length = 0;
    for (u64 i = 0; i < rest.length; ++i) {
        u8 c = (u8)rest.value[i];
        if (c == '%' && i + 2 < rest.length) {
            u8 high = P__HexDigit((u8)rest.value[i + 1]);
            u8 low = P__HexDigit((u8)rest.value[i + 2]);
            if (high != 0xFF && low != 0xFF) {
                bytes[length++] = (char)(high << 4 | low);
                i += 2;
                continue;
            }
        }
        bytes[length++] = (char)c;
    }
    // A null would cut the path short at the system boundary.
    for (u64 i = 0; i < length; ++i) if (!bytes[i]) return (String){0};
    return StrMake(bytes, length);
}

// `name` numbered for attempt `n`, as a path for the system.
function char *P__NumberedName(Arena *arena, String name, b32 folder, u32 n)
{
    return P__FilePathCString(arena, P__NumberedString(arena, name, folder, n));
}

function P_FileError P_CreateItem(Arena *arena, String directory, String name, b32 folder, String *created)
{
    *created = (String){0};
    if (!P__ValidFileName(name)) return P__FileError(arena, EINVAL);
    Scratch scratch = GetScratchEx(arena);
    char *path = P__FilePathCString(scratch.arena, directory);
    i32 parent = path ? open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u32 error = parent < 0 ? (u32)errno : EEXIST;
    for (u32 n = 1; n < 10000 && error == EEXIST; ++n) {
        char *candidate = P__NumberedName(scratch.arena, name, folder, n);
        if (folder) {
            error = mkdirat(parent, candidate, 0777) == 0 ? 0 : (u32)errno;
        } else {
            i32 file = openat(parent, candidate, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
            error = file >= 0 ? 0 : (u32)errno;
            if (file >= 0) close(file);
        }
        if (!error) *created = StrCopy(arena, StrFromCStr(candidate));
    }
    if (parent >= 0) close(parent);
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P_CreateLink(Arena *arena, String directory, String target, String *created)
{
    *created = (String){0};
    Scratch scratch = GetScratchEx(arena);
    while (target.length > 1 && target.value[target.length - 1] == '/') --target.length;
    Index slash = StrIndexChar(target, '/', SSF_SearchBackwards);
    String leaf = slash.exists ? StrFrom(target, slash.value + 1) : target;
    if (!leaf.length) leaf = S("Root");
    char *path = P__FilePathCString(scratch.arena, directory), *to = P__FilePathCString(scratch.arena, target);
    i32 parent = path ? open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u32 error = parent < 0 || !to ? (u32)errno : EEXIST;
    for (u32 n = 1; n < 10000 && error == EEXIST; ++n) {
        char *candidate = P__NumberedName(scratch.arena, leaf, false, n);
        error = symlinkat(to, parent, candidate) == 0 ? 0 : (u32)errno;
        if (!error) *created = StrCopy(arena, StrFromCStr(candidate));
    }
    if (parent >= 0) close(parent);
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

// Makes the folders of `path` that are missing, the last one included.
function b32 P__MakeFolders(char *path)
{
    for (char *at = path + 1; *at; ++at) {
        if (*at != '/') continue;
        *at = 0;
        b32 made = mkdir(path, 0777) == 0 || errno == EEXIST;
        *at = '/';
        if (!made) return false;
    }
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

// Links are left out.
function u32 P__ArchiveWritePath(void *writer, String path, String name, P_Progress *progress, const P_CancelToken *cancel)
{
    P__Archive *a = &P__archive;
    if (P_IsCancelled(cancel)) return ECANCELED;
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, path), *inside = P__FilePathCString(scratch.arena, name);
    struct stat info = {0};
    u32 error = native && inside && lstat(native, &info) == 0 ? 0 : (u32)errno;
    b32 file = !error && S_ISREG(info.st_mode), folder = !error && S_ISDIR(info.st_mode);
    if (file && !writer) atomic_fetch_add(&progress->totalBytes, (u64)info.st_size);
    if ((file || folder) && writer) {
        void *entry = a->entryNew();
        a->entrySetPath(entry, inside);
        a->entryCopyStat(entry, &info);
        if (a->writeHeader(writer, entry) < P__ArchiveWarn) error = EIO;
        a->entryFree(entry);
    }
    if (!error && file && writer) {
        P_ProgressName(progress, StrFrom(name, StrIndexChar(name, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value));
        i32 in = open(native, O_RDONLY | O_CLOEXEC);
        if (in < 0) error = (u32)errno;
        char buffer[65536];
        ssize_t got = 0;
        while (in >= 0 && !error && (got = read(in, buffer, sizeof(buffer))) > 0) {
            if (a->writeData(writer, buffer, (u64)got) != got) error = EIO;
            atomic_fetch_add(&progress->doneBytes, (u64)got);
            if (!P__Proceed(progress, cancel)) error = ECANCELED;
        }
        if (!error && got < 0) error = (u32)errno;
        if (in >= 0) close(in);
    } else if (!error && folder) {
        DIR *listing = opendir(native);
        if (!listing) error = (u32)errno;
        for (struct dirent *item; listing && !error && (item = readdir(listing)) != NULL;) {
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
            error = P__ArchiveWritePath(writer, StrFormat(scratch.arena, "%S/%s", path, item->d_name), StrFormat(scratch.arena, "%S/%s", name, item->d_name), progress, cancel);
        }
        if (listing) closedir(listing);
    }
    ReleaseScratch(scratch);
    return error;
}

function u32 P__ArchiveMakeFolders(String path)
{
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, path);
    u32 error = !native ? EINVAL : P__MakeFolders(native) ? 0 : (u32)errno;
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
    char *native = P__FilePathCString(scratch.arena, part);
    i32 file = !error && native ? open(native, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666) : -1;
    if (!error && file < 0) error = native ? (u32)errno : EINVAL;
    const void *block = NULL;
    u64 size = 0;
    i64 offset = 0;
    i32 status = 0;
    while (file >= 0 && !error && (status = a->readBlock(reader, &block, &size, &offset)) == 0) {
        if (pwrite(file, block, size, (off_t)offset) != (ssize_t)size) error = (u32)errno;
        P__ArchiveAdvance(progress, reader, read);
        if (!P__Proceed(progress, cancel)) error = ECANCELED;
    }
    if (!error && status < 0) error = EIO;
    if (file >= 0) {
        const struct timespec times[2] = { { 0, UTIME_OMIT }, { modified, 0 } };
        futimens(file, times);
        if (close(file) != 0 && !error) error = (u32)errno;
    }
    if (!error) error = P_ReplaceFile(scratch.arena, part, path).code;
    if (error && native) unlink(native);
    ReleaseScratch(scratch);
    return error;
}

function u64 P__ArchiveFileSize(String path)
{
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, path);
    struct stat info = {0};
    u64 result = native && stat(native, &info) == 0 ? (u64)info.st_size : 0;
    ReleaseScratch(scratch);
    return result;
}

function void P__ArchiveRemove(String path)
{
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, path);
    if (native) unlink(native);
    ReleaseScratch(scratch);
}

function b32 P_IsDirectory(String path)
{
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, path);
    struct stat info = {0};
    b32 result = native && stat(native, &info) == 0 && S_ISDIR(info.st_mode);
    ReleaseScratch(scratch);
    return result;
}

function String P_RunFolder(Arena *arena)
{
    const char *base = getenv("TMPDIR");
    if (!base || !*base) base = "/tmp";
    String filey = StrFormat(arena, "%s/filey-%u", base, (u32)getuid()), folder = StrFormat(arena, "%S/%d", filey, (i32)getpid());
    Scratch scratch = GetScratchEx(arena);
    char *top = P__FilePathCString(scratch.arena, filey), *own = P__FilePathCString(scratch.arena, folder);
    mkdir(top, 0700);
    // Only a folder of this user's, not a link another user left in its place.
    struct stat info = {0};
    b32 ours = lstat(top, &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == getuid();
    if (ours) mkdir(own, 0700);
    ReleaseScratch(scratch);
    return ours && P_IsDirectory(folder) ? folder : (String){0};
}

// Copies `name` in `sourceDir` to `targetName` in `targetDir`.
// Folders are copied with everything in them and links as links; sockets, pipes and devices are skipped.
// Returns EEXIST without touching anything when `targetName` is taken; `created` tells whether the target was made, so that a failed copy can be removed without touching anything else.
function u32 P__CopyAt(i32 sourceDir, const char *name, i32 targetDir, const char *targetName, b32 *created, P_Progress *progress, const P_CancelToken *cancel)
{
    *created = false;
    if (!P__Proceed(progress, cancel)) return ECANCELED;
    struct stat info;
    if (fstatat(sourceDir, name, &info, AT_SYMLINK_NOFOLLOW) != 0) return (u32)errno;
    struct timespec times[2] = { info.st_atim, info.st_mtim };
    if (S_ISLNK(info.st_mode)) {
        char link[PATH_MAX];
        ssize_t length = readlinkat(sourceDir, name, link, sizeof(link) - 1);
        if (length < 0) return (u32)errno;
        link[length] = 0;
        if (symlinkat(link, targetDir, targetName) != 0) return (u32)errno;
        *created = true;
        utimensat(targetDir, targetName, times, AT_SYMLINK_NOFOLLOW);
        return 0;
    }
    if (S_ISDIR(info.st_mode)) {
        // Writable while its contents are copied in; the source's mode is applied at the end.
        if (mkdirat(targetDir, targetName, (info.st_mode & 07777) | S_IRWXU) != 0) return (u32)errno;
        *created = true;
        i32 from = openat(sourceDir, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        i32 to = openat(targetDir, targetName, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        DIR *stream = from >= 0 ? fdopendir(from) : NULL;
        u32 error = !stream || to < 0 ? (u32)errno : 0;
        if (!stream && from >= 0) close(from);
        // ponytail: one open folder per level, so trees deeper than the descriptor limit fail; walk with an explicit stack if that shows.
        for (struct dirent *entry; !error && stream && (entry = readdir(stream));) {
            const char *child = entry->d_name;
            if (child[0] == '.' && (!child[1] || (child[1] == '.' && !child[2]))) continue;
            b32 childCreated = false;
            error = P__CopyAt(dirfd(stream), child, to, child, &childCreated, progress, cancel);
        }
        if (to >= 0) {
            fchmod(to, info.st_mode & 07777);
            futimens(to, times);
            close(to);
        }
        if (stream) closedir(stream);
        return error;
    }
    if (!S_ISREG(info.st_mode)) return 0;
    struct stat taken;
    if (fstatat(targetDir, targetName, &taken, AT_SYMLINK_NOFOLLOW) == 0) return EEXIST;
    i32 in = openat(sourceDir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) return (u32)errno;
    // Written unnamed, which leaves nothing when cut off and shows nothing in the folder until it is complete.
    // Elsewhere under a hidden name; one left by an earlier copy cut off is kept, as it may be all there is of that file.
    // ponytail: a name close to NAME_MAX is cut for it, mid-character where it falls so.
    char part[NAME_MAX + 1] = {0};
    i32 out = openat(targetDir, ".", O_TMPFILE | O_WRONLY | O_CLOEXEC, (info.st_mode & 07777) | S_IWUSR), kept = (i32)Min(strlen(targetName), (u64)NAME_MAX - 16);
    for (u32 n = 1; out < 0 && n < 100; ++n) {
        if (n == 1) snprintf(part, sizeof(part), ".%.*s.part", kept, targetName);
        else snprintf(part, sizeof(part), ".%.*s.%u.part", kept, targetName, n);
        out = openat(targetDir, part, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (info.st_mode & 07777) | S_IWUSR);
        if (out < 0 && errno != EEXIST) break;
    }
    if (out < 0) {
        u32 error = (u32)errno;
        close(in);
        return error;
    }
    P_ProgressName(progress, StrFromCStr(name));
    u32 error = 0;
    // Its whole size is taken first on another file system, so that a file too big for the drive fails before anything is written, and it
    // lies in one piece. Not on its own, where the kernel may share the source's blocks, which takes no room.
    struct stat placed;
    if (info.st_size > 0 && fstat(out, &placed) == 0 && placed.st_dev != info.st_dev && fallocate(out, FALLOC_FL_KEEP_SIZE, 0, info.st_size) != 0 && errno == ENOSPC) {
        error = ENOSPC;
    }
    b32 kernelCopy = true;
    char buffer[65536];
    while (!error) {
        if (!P__Proceed(progress, cancel)) { error = ECANCELED; break; }
        ssize_t count = kernelCopy ? copy_file_range(in, NULL, out, NULL, Megabytes(8), 0) : read(in, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        // Some file systems cannot copy in the kernel; plain reads take over from where it stopped.
        if (count < 0 && kernelCopy && (errno == EXDEV || errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP)) { kernelCopy = false; continue; }
        if (count < 0) { error = (u32)errno; break; }
        if (count == 0) break;
        atomic_fetch_add(&progress->doneBytes, (u64)count);
        for (ssize_t at = 0; !kernelCopy && at < count;) {
            ssize_t written = write(out, buffer + at, (size_t)(count - at));
            if (written < 0 && errno == EINTR) continue;
            if (written < 0) { error = (u32)errno; break; }
            at += written;
        }
        if (error) break;
    }
    fchmod(out, info.st_mode & 07777);
    futimens(out, times);
    // Into place only where the name is still free, which something else may have taken meanwhile; an unnamed file is linked by its descriptor.
    if (!error && !part[0]) {
        char self[64];
        snprintf(self, sizeof(self), "/proc/self/fd/%d", out);
        if (linkat(AT_FDCWD, self, targetDir, targetName, AT_SYMLINK_FOLLOW) != 0) error = (u32)errno;
    }
    if (close(out) != 0 && !error) error = (u32)errno;
    close(in);
    if (!error && part[0] && renameat2(targetDir, part, targetDir, targetName, RENAME_NOREPLACE) != 0) {
        error = (u32)errno;
        // ponytail: file systems without RENAME_NOREPLACE get a check-then-rename race, as moves do.
        if (error == EINVAL || error == ENOSYS) {
            error = fstatat(targetDir, targetName, &taken, AT_SYMLINK_NOFOLLOW) == 0 ? EEXIST : renameat(targetDir, part, targetDir, targetName) == 0 ? 0 : (u32)errno;
        }
    }
    if (error && part[0]) unlinkat(targetDir, part, 0);
    *created = !error;
    return error;
}

// The bytes of the regular files at `name` in `directory`, everything in it included; links are not followed.
function u64 P__SizeAt(i32 directory, const char *name, const P_CancelToken *cancel)
{
    struct stat info;
    if (P_IsCancelled(cancel) || fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) != 0) return 0;
    if (S_ISREG(info.st_mode)) return (u64)info.st_size;
    if (!S_ISDIR(info.st_mode)) return 0;
    i32 fd = openat(directory, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *stream = fd >= 0 ? fdopendir(fd) : NULL;
    if (!stream) {
        if (fd >= 0) close(fd);
        return 0;
    }
    u64 result = 0;
    for (struct dirent *entry; (entry = readdir(stream));) {
        const char *child = entry->d_name;
        if (child[0] == '.' && (!child[1] || (child[1] == '.' && !child[2]))) continue;
        result += P__SizeAt(dirfd(stream), child, cancel);
    }
    closedir(stream);
    return result;
}

// What to do with `name`, already taken in the target folder: the policy, or else the program's answer, which the job waits for.
function P_Conflict P__AskConflict(P_Progress *progress, String name, b32 folder, const P_CancelToken *cancel)
{
    P_Conflict policy = (P_Conflict)atomic_load(&progress->policy);
    if (policy != P_Conflict_Ask) return policy;
    u64 length = Min(name.length, (u64)sizeof(progress->conflictName) - 1);
    MemCopy(progress->conflictName, name.value, length);
    progress->conflictName[length] = 0;
    progress->conflictFolder = folder;
    atomic_store(&progress->asking, true);
    struct timespec pause = { 0, 10 * 1000 * 1000 };
    while (atomic_load(&progress->asking) && !P_IsCancelled(cancel)) nanosleep(&pause, NULL);
    atomic_store(&progress->asking, false);
    return P_IsCancelled(cancel) ? P_Conflict_Skip : (P_Conflict)atomic_load(&progress->answer);
}

// Removes `name` in `directory` with everything in it; links are removed, never followed.
function u32 P__RemoveAt(i32 directory, const char *name)
{
    struct stat info;
    if (fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) != 0) return (u32)errno;
    if (!S_ISDIR(info.st_mode)) return unlinkat(directory, name, 0) == 0 ? 0 : (u32)errno;
    i32 fd = openat(directory, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *stream = fd >= 0 ? fdopendir(fd) : NULL;
    if (!stream) {
        u32 error = (u32)errno;
        if (fd >= 0) close(fd);
        return error;
    }
    u32 error = 0;
    for (struct dirent *entry; !error && (entry = readdir(stream));) {
        const char *child = entry->d_name;
        if (child[0] == '.' && (!child[1] || (child[1] == '.' && !child[2]))) continue;
        error = P__RemoveAt(dirfd(stream), child);
    }
    closedir(stream);
    return error ? error : unlinkat(directory, name, AT_REMOVEDIR) == 0 ? 0 : (u32)errno;
}

function void P__RemoveStaleRunFolders(String root, u32 own)
{
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, root);
    DIR *stream = native ? opendir(native) : NULL;
    for (struct dirent *entry; stream && (entry = readdir(stream));) {
        // Only a folder named by a process's number, whose process is gone.
        const char *name = entry->d_name;
        u64 id = 0;
        b32 numbered = name[0] != 0;
        for (const char *c = name; numbered && *c; ++c) {
            numbered = *c >= '0' && *c <= '9' && id < INT32_MAX;
            id = id * 10 + (u64)(*c - '0');
        }
        struct stat info;
        b32 folder = numbered && fstatat(dirfd(stream), name, &info, AT_SYMLINK_NOFOLLOW) == 0 && S_ISDIR(info.st_mode);
        if (folder && id <= INT32_MAX && (u32)id != own && kill((pid_t)id, 0) != 0 && errno == ESRCH) P__RemoveAt(dirfd(stream), name);
    }
    if (stream) closedir(stream);
    ReleaseScratch(scratch);
}

function void P_RefreshInstallEntry(void) {}

function void P_RemoveStaleRunFolders(void)
{
    Scratch scratch = GetScratch();
    String run = P_RunFolder(scratch.arena);
    Index slash = StrIndexChar(run, '/', SSF_SearchBackwards);
    if (run.length && slash.exists) P__RemoveStaleRunFolders(StrFromTo(run, 0, (i64)slash.value), (u32)getpid());
    ReleaseScratch(scratch);
}

// Splits a path into its folder and last component, ignoring trailing slashes.
function void P__SplitPath(String path, String *parent, String *leaf)
{
    while (path.length > 1 && path.value[path.length - 1] == '/') --path.length;
    Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
    *parent = !slash.exists ? S(".") : slash.value ? StrFromTo(path, 0, (i64)slash.value) : S("/");
    *leaf = slash.exists ? StrFrom(path, slash.value + 1) : path;
}

function P_FileError P_TransferItems(Arena *arena, const String *paths, u64 count, String directory, b32 move, P_Progress *progress, ArrayStr *placed, const P_CancelToken *cancel)
{
    P_Progress numbered = { .policy = P_Conflict_KeepBoth };
    if (!progress) progress = &numbered;
    if (placed) {
        *placed = (ArrayStr){0};
        for (u64 i = 0; i < count; ++i) ArrayPush(arena, *placed, (String){0});
    }
    Scratch scratch = GetScratchEx(arena);
    char *targetPath = P__FilePathCString(scratch.arena, directory);
    char realTarget[PATH_MAX];
    i32 targetDir = targetPath ? open(targetPath, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u32 error = targetDir < 0 || !realpath(targetPath, realTarget) ? (u32)errno : 0;
    struct stat targetInfo;
    if (!error && fstat(targetDir, &targetInfo) != 0) error = (u32)errno;
    // Bytes count for what is copied: everything for a copy, and what a move takes to another file system.
    atomic_store(&progress->totalItems, count);
    for (u64 i = 0; i < count && !error; ++i) {
        struct stat info;
        char *sourcePath = P__FilePathCString(scratch.arena, paths[i]);
        if (sourcePath && lstat(sourcePath, &info) == 0 && (!move || info.st_dev != targetInfo.st_dev)) {
            atomic_fetch_add(&progress->totalBytes, P__SizeAt(AT_FDCWD, sourcePath, cancel));
        }
    }
    for (u64 i = 0; i < count && !error; ++i) {
        if (!P__Proceed(progress, cancel)) { error = ECANCELED; break; }
        String parent, leaf;
        P__SplitPath(paths[i], &parent, &leaf);
        char *parentPath = P__FilePathCString(scratch.arena, parent);
        char *leafName = P__FilePathCString(scratch.arena, leaf);
        char *sourcePath = P__FilePathCString(scratch.arena, paths[i]);
        char realSource[PATH_MAX], realParent[PATH_MAX];
        if (!parentPath || !leafName || !sourcePath || !P__ValidFileName(leaf)) { error = EINVAL; break; }
        if (!realpath(parentPath, realParent)) { error = (u32)errno; break; }
        struct stat info;
        i32 sourceDir = open(parentPath, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (sourceDir < 0 || fstatat(sourceDir, leafName, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            error = (u32)errno;
            if (sourceDir >= 0) close(sourceDir);
            break;
        }
        // A folder cannot go inside itself; links are moved or copied as links, so only real folders are checked.
        if (S_ISDIR(info.st_mode) && realpath(sourcePath, realSource)) {
            String source = StrFromCStr(realSource), target = StrFromCStr(realTarget);
            if (StrIsEqual(source, target) || (target.length > source.length && StrIsEqual(StrFromTo(target, 0, (i64)source.length), source) && target.value[source.length] == '/')) {
                error = EINVAL;
                close(sourceDir);
                break;
            }
        }
        b32 sameFolder = strcmp(realParent, realTarget) == 0;
        if (move && sameFolder) {
            close(sourceDir);
            atomic_fetch_add(&progress->doneItems, 1);
            if (placed) placed->v[i] = StrCopy(arena, paths[i]);
            continue;
        }
        // A taken name asks what to do, except where the item is copied next to itself, which keeps both.
        P_Conflict conflict = P_Conflict_KeepBoth;
        struct stat existing;
        if (!sameFolder && fstatat(targetDir, leafName, &existing, AT_SYMLINK_NOFOLLOW) == 0) {
            conflict = P__AskConflict(progress, leaf, S_ISDIR(existing.st_mode), cancel);
            if (P_IsCancelled(cancel)) { error = ECANCELED; close(sourceDir); break; }
            if (conflict == P_Conflict_Replace && (S_ISDIR(existing.st_mode) || S_ISDIR(info.st_mode))) conflict = P_Conflict_KeepBoth;
        }
        if (conflict == P_Conflict_Skip) {
            close(sourceDir);
            atomic_fetch_add(&progress->doneItems, 1);
            continue;
        }
        // A replacing move on one file system swaps the file in at once.
        if (conflict == P_Conflict_Replace && move && renameat(sourceDir, leafName, targetDir, leafName) == 0) {
            close(sourceDir);
            atomic_fetch_add(&progress->doneItems, 1);
            if (placed) placed->v[i] = StrFormat(arena, "%S/%S", directory, leaf);
            continue;
        }
        // Otherwise the item arrives under a free name, which a replacing copy then moves over the old file once it is complete.
        error = EEXIST;
        char *used = leafName;
        for (u32 n = conflict == P_Conflict_Replace ? 2 : 1; n < 10000 && error == EEXIST; ++n) {
            char *name = P__NumberedName(scratch.arena, leaf, S_ISDIR(info.st_mode), n);
            used = name;
            if (move) {
                error = renameat2(sourceDir, leafName, targetDir, name, RENAME_NOREPLACE) == 0 ? 0 : (u32)errno;
                // ponytail: file systems without RENAME_NOREPLACE get a check-then-rename race.
                if (error == EINVAL || error == ENOSYS) {
                    struct stat existing;
                    error = fstatat(targetDir, name, &existing, AT_SYMLINK_NOFOLLOW) == 0 ? EEXIST : renameat(sourceDir, leafName, targetDir, name) == 0 ? 0 : (u32)errno;
                }
                if (error != EXDEV) continue;
            }
            // Copies, and moves across file systems: the source goes only once its copy is complete.
            b32 created = false;
            error = P__CopyAt(sourceDir, leafName, targetDir, name, &created, progress, cancel);
            if (error && created) P__RemoveAt(targetDir, name);
            if (!error && conflict == P_Conflict_Replace && renameat(targetDir, name, targetDir, leafName) != 0) error = (u32)errno;
            if (!error && move) error = P__RemoveAt(sourceDir, leafName);
        }
        if (!error && placed) placed->v[i] = StrFormat(arena, "%S/%s", directory, conflict == P_Conflict_Replace ? leafName : used);
        close(sourceDir);
        atomic_fetch_add(&progress->doneItems, 1);
    }
    if (targetDir >= 0) close(targetDir);
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

// Moves the item at `source`, called `leaf`, into the freedesktop trash at `trash`: an info file records where it came from, then the item moves under files/.
// Returns EXDEV when the trash is on another file system.
function u32 P__TrashInto(Arena *arena, const char *trash, const char *source, String leaf, b32 folder)
{
    char *files = P__FilePathCString(arena, StrFormat(arena, "%s/files", trash));
    char *infos = P__FilePathCString(arena, StrFormat(arena, "%s/info", trash));
    if ((mkdir(trash, 0700) != 0 && errno != EEXIST) || (mkdir(files, 0700) != 0 && errno != EEXIST) || (mkdir(infos, 0700) != 0 && errno != EEXIST)) return (u32)errno;
    // The path in the info file is percent-encoded like a URI path.
    String encoded = P__PercentEncode(arena, StrFromCStr(source));
    char date[32];
    time_t now = time(NULL);
    struct tm local;
    strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", localtime_r(&now, &local));
    String info = StrFormat(arena, "[Trash Info]\nPath=%S\nDeletionDate=%s\n", encoded, date);
    u32 error = EEXIST;
    for (u32 n = 1; n < 10000 && error == EEXIST; ++n) {
        char *name = P__NumberedName(arena, leaf, folder, n);
        char *infoPath = P__FilePathCString(arena, StrFormat(arena, "%s/%s.trashinfo", infos, name));
        char *target = P__FilePathCString(arena, StrFormat(arena, "%s/%s", files, name));
        // The info file claims the name, as the specification asks.
        i32 fd = open(infoPath, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) { error = (u32)errno; continue; }
        b32 written = write(fd, info.value, info.length) == (ssize_t)info.length;
        close(fd);
        error = !written ? EIO : renameat2(AT_FDCWD, source, AT_FDCWD, target, RENAME_NOREPLACE) == 0 ? 0 : (u32)errno;
        if (error) unlink(infoPath);
    }
    return error;
}

// GTK_THEME's ":dark" or a dark theme name, else the GTK settings' preference or theme name.
function P_DriveKind P_PathDriveKind(String path)
{
    Scratch scratch = GetScratch();
    const char *native = P__FilePathCString(scratch.arena, path);
    struct statfs system;
    struct stat info;
    P_DriveKind result = P_DriveKind_SolidState;
    if (statfs(native, &system) == 0) {
        // NFS, SMB, CIFS, SMB2 and 9p (which WSL mounts Windows drives with).
        local_persist const u64 remote[] = { 0x6969, 0x517B, 0xFF534D42, 0xFE534D42, 0x01021997 };
        for (u64 i = 0; i < CArrayCount(remote); ++i) if ((u64)(u32)system.f_type == remote[i]) result = P_DriveKind_Network;
        // A FUSE file system with no disk under it, as rclone's, Filen's, gvfs's and sshfs's mounts are, is a network one; ntfs-3g and the
        // like are FUSE on a disk, which their device says.
        if ((u32)system.f_type == 0x65735546 && stat(native, &info) == 0 && !major(info.st_dev)) result = P_DriveKind_Network;
    }
    // The block device's queue says whether it spins; a partition's is its disk's, one folder up.
    if (result != P_DriveKind_Network && stat(native, &info) == 0 && major(info.st_dev)) {
        for (u32 up = 0; up < 2; ++up) {
            char name[96];
            snprintf(name, sizeof(name), "/sys/dev/block/%u:%u/%squeue/rotational", major(info.st_dev), minor(info.st_dev), up ? "../" : "");
            i32 file = open(name, O_RDONLY | O_CLOEXEC);
            if (file < 0) continue;
            char value = 0;
            b32 read1 = read(file, &value, 1) == 1;
            close(file);
            if (read1) {
                if (value == '1') result = P_DriveKind_Spinning;
                break;
            }
        }
    }
    ReleaseScratch(scratch);
    return result;
}

// The desktop's choice from the settings portal, as gdbus reads it: 1 prefers dark, 2 light, and 0 or -1 for none or no portal.
function i32 P__PortalColorScheme(void)
{
    Scratch scratch = GetScratch();
    String arguments[] = { S("gdbus"), S("call"), S("--session"), S("--timeout"), S("1"), S("--dest"), S("org.freedesktop.portal.Desktop"),
                           S("--object-path"), S("/org/freedesktop/portal/desktop"), S("--method"), S("org.freedesktop.portal.Settings.Read"),
                           S("org.freedesktop.appearance"), S("color-scheme") };
    String output = {0};
    i32 result = -1;
    // It answers as "(<<uint32 1>>,)".
    if (P_RunCommand(scratch.arena, S("/"), arguments, CArrayCount(arguments), 1500, &output, false)) {
        Index at = StrIndexStr(output, S("uint32 "), SSF_None);
        if (at.exists && at.value + 7 < output.length) result = output.value[at.value + 7] - '0';
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P_PrefersDarkColors(void)
{
    // The portal first, which the desktops set, asked at most every two seconds as that runs a program; GTK's own settings where it has no
    // choice; dark where neither says.
    local_persist u64 askedAt;
    local_persist i32 portal;
    u64 now = PlatformNowMicroseconds();
    if (!askedAt || now - askedAt > 2000000) {
        portal = P__PortalColorScheme();
        askedAt = now;
    }
    if (portal == 1 || portal == 2) return portal == 1;
    const char *theme = getenv("GTK_THEME");
    if (theme && theme[0]) return strstr(theme, "dark") != NULL || strstr(theme, "Dark") != NULL;
    Scratch scratch = GetScratch();
    const char *config = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    String folder = config && config[0] == '/' ? StrFromCStr(config) : home ? StrFormat(scratch.arena, "%s/.config", home) : (String){0};
    String text = {0};
    b32 result = true;
    if (folder.length && P_ReadFile(scratch.arena, StrFormat(scratch.arena, "%S/gtk-3.0/settings.ini", folder), &text)) {
        ArrayStr lines = StrSplit(scratch.arena, text, '\n');
        for (u64 i = 0; i < lines.header.count; ++i) {
            String line = lines.v[i];
            Index equals = StrIndexChar(line, '=', SSF_None);
            if (!equals.exists) continue;
            String key = StrFromTo(line, 0, (i64)equals.value), value = StrFrom(line, equals.value + 1);
            while (key.length && key.value[key.length - 1] == ' ') --key.length;
            while (value.length && value.value[0] == ' ') value = StrFrom(value, 1);
            if (StrIsEqual(key, S("gtk-application-prefer-dark-theme"))) result = StrIsEqual(value, S("1")) || StrIsEqual(value, S("true"));
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function String P_TrashFolder(Arena *arena)
{
    const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    return data && data[0] == '/' ? StrFormat(arena, "%s/Trash", data) : home && home[0] == '/' ? StrFormat(arena, "%s/.local/share/Trash", home) : (String){0};
}

function b32 P_TrashHasItems(void)
{
    Scratch scratch = GetScratch();
    String trash = P_TrashFolder(scratch.arena);
    DIR *folder = trash.length ? opendir(P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "%S/files", trash))) : NULL;
    b32 result = false;
    for (struct dirent *entry; folder && !result && (entry = readdir(folder));) result = strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0;
    if (folder) closedir(folder);
    ReleaseScratch(scratch);
    return result;
}

function P_FileError P_EmptyTrash(Arena *arena, b32 confirm)
{
    (void)confirm;
    Scratch scratch = GetScratchEx(arena);
    String trash = P_TrashFolder(scratch.arena);
    P_FileError result = {0};
    // The deleted items, then the notes on where they came from.
    local_persist const char *parts[] = { "files", "info", "expunged" };
    for (u32 p = 0; trash.length && p < CArrayCount(parts) && !result.code; ++p) {
        String folder = StrFormat(scratch.arena, "%S/%s", trash, parts[p]);
        P_DirectoryResult listing = P_EnumerateDirectory(scratch.arena, folder, NULL);
        String *paths = ArenaPushArray(scratch.arena, Max(listing.files.header.count, (u64)1), String);
        for (u64 i = 0; i < listing.files.header.count; ++i) paths[i] = StrFormat(scratch.arena, "%S/%S", folder, listing.files.v[i].name);
        if (listing.files.header.count) result = P_DeleteItems(arena, paths, listing.files.header.count, false, NULL);
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P_EmptyTrashAsks(void) { return false; }
function b32 P_DeleteAsks(void) { return false; }
function b32 P_HasSystemFiles(void) { return false; }
function b32 P_HasPathLimit(void) { return false; }
function b32 P_ReadVolumeTree(Arena *arena, String root, u64 most, const P_CancelToken *cancel, P_VolumeTree *tree)
{
    (void)arena, (void)root, (void)most, (void)cancel;
    *tree = (P_VolumeTree){0};
    return false;
}
function b32 P_LongPathsEnabled(void) { return true; }
function b32 P_SetLongPaths(b32 on) { return on; }
function String P_TrashName(void) { return S("Trash"); }

function String P_SystemLanguage(Arena *arena)
{
    // The first of the variables that name the language of messages, as gettext takes them; "C" and "POSIX" name none.
    const char *names[] = { "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG" };
    for (u32 i = 0; i < CArrayCount(names); ++i) {
        const char *value = getenv(names[i]);
        if (!value || !value[0]) continue;
        String locale = StrFromCStr(value);
        u64 end = 0;
        while (end < locale.length && locale.value[end] >= 'a' && locale.value[end] <= 'z') ++end;
        return end >= 2 ? StrCopy(arena, StrFromTo(locale, 0, (i64)end)) : (String){0};
    }
    return (String){0};
}

function b32 P_IsTrashFolder(String path)
{
    // Worked out once, since every join of a path asks.
    local_persist char trash[4096];
    local_persist u64 length = UINT64_MAX;
    if (length == UINT64_MAX) {
        Scratch scratch = GetScratch();
        String found = P_TrashFolder(scratch.arena);
        length = found.length < sizeof(trash) ? found.length : 0;
        MemCopy(trash, found.value, length);
        ReleaseScratch(scratch);
    }
    while (path.length > 1 && path.value[path.length - 1] == '/') --path.length;
    return length && StrIsEqual(path, StrMake(trash, length));
}

// The home trash: each item under files/ with an info file under info/ naming where it came from, percent-encoded, and when it was deleted.
function P_ArrayTrashed P_ListTrash(Arena *arena, String path, const P_CancelToken *cancel)
{
    P_ArrayTrashed result = {0};
    Scratch scratch = GetScratchEx(arena);
    String filesFolder = StrFormat(scratch.arena, "%S/files", path), infoFolder = StrFormat(scratch.arena, "%S/info", path);
    P_DirectoryResult files = P_EnumerateDirectory(scratch.arena, filesFolder, cancel);
    for (u64 i = 0; i < files.files.header.count; ++i) {
        const P_File *kept = &files.files.v[i];
        String info = {0};
        if (!P_ReadFile(scratch.arena, StrFormat(scratch.arena, "%S/%S.trashinfo", infoFolder, kept->name), &info)) continue;
        String original = {0}, date = {0};
        ArrayStr lines = StrSplit(scratch.arena, info, '\n');
        for (u64 l = 0; l < lines.header.count; ++l) {
            String line = lines.v[l];
            if (line.length > 5 && StrIsEqual(StrFromTo(line, 0, 5), S("Path="))) original = P__FilePathFromUri(scratch.arena, StrFormat(scratch.arena, "file://%S", StrFrom(line, 5)));
            if (line.length > 13 && StrIsEqual(StrFromTo(line, 0, 13), S("DeletionDate="))) date = StrFrom(line, 13);
        }
        Index slash = StrIndexChar(original, '/', SSF_SearchBackwards);
        if (!slash.exists) continue;
        P_Trashed *item = ArrayAddClear(arena, result, 1);
        item->file = *kept;
        item->file.name = StrFormat(arena, "%S/%S", filesFolder, kept->name);
        item->name = StrCopy(arena, StrFrom(original, slash.value + 1));
        item->original = StrCopy(arena, StrFromTo(original, 0, slash.value ? (i64)slash.value : 1));
        struct tm when = {0};
        char *text = P__FilePathCString(scratch.arena, date);
        if (text && strptime(text, "%Y-%m-%dT%H:%M:%S", &when)) {
            when.tm_isdst = -1;
            item->deletedTimeUnix = (i64)mktime(&when);
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function P_FileError P_RestoreTrash(Arena *arena, String bin, const String *paths, u64 count)
{
    Scratch scratch = GetScratchEx(arena);
    P_ArrayTrashed binned = P_ListTrash(scratch.arena, bin, NULL);
    u32 error = 0, taken = 0;
    for (u64 i = 0; i < binned.header.count && !error; ++i) {
        const P_Trashed *item = &binned.v[i];
        b32 wanted = !count;
        for (u64 p = 0; p < count && !wanted; ++p) wanted = StrIsEqual(item->file.name, paths[p]);
        if (!wanted) continue;
        String folder = item->original;
        String target = StrIsEqual(folder, S("/")) ? StrFormat(scratch.arena, "/%S", item->name) : StrFormat(scratch.arena, "%S/%S", folder, item->name);
        char *from = P__FilePathCString(scratch.arena, item->file.name), *to = P__FilePathCString(scratch.arena, target);
        if (!from || !to) {
            error = EINVAL;
            break;
        }
        struct stat seen;
        if (lstat(to, &seen) == 0) {
            ++taken;
            continue;
        }
        // Its folder, and the ones above it, may have gone since.
        for (u64 at = 1; at <= folder.length; ++at) {
            if (at < folder.length && folder.value[at] != '/') continue;
            char *part = P__FilePathCString(scratch.arena, StrFromTo(folder, 0, (i64)at));
            if (part) mkdir(part, 0777);
        }
        if (rename(from, to) != 0) {
            error = (u32)errno;
            break;
        }
        String parent, leaf;
        P__SplitPath(item->file.name, &parent, &leaf);
        char *info = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "%S/info/%S.trashinfo", bin, leaf));
        if (info) unlink(info);
    }
    ReleaseScratch(scratch);
    if (error) return P__FileError(arena, error);
    if (taken) return (P_FileError){ .code = EEXIST, .message = StrFormat(arena, "%u item%s stayed in the Trash, as something else is where %s came from.", taken, taken == 1 ? "" : "s", taken == 1 ? "it" : "they") };
    return (P_FileError){0};
}

function P_FileError P_TrashItems(Arena *arena, const String *paths, u64 count)
{
    Scratch scratch = GetScratchEx(arena);
    const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    String homeTrash = data && data[0] ? StrFormat(scratch.arena, "%s/Trash", data) : home ? StrFormat(scratch.arena, "%s/.local/share/Trash", home) : (String){0};
    u32 error = homeTrash.length ? 0 : ENOENT;
    if (!error && data && data[0]) mkdir(data, 0700);
    for (u64 i = 0; i < count && !error; ++i) {
        String parent, leaf;
        P__SplitPath(paths[i], &parent, &leaf);
        char *source = P__FilePathCString(scratch.arena, paths[i]);
        struct stat info;
        if (!source || !P__ValidFileName(leaf) || lstat(source, &info) != 0) { error = source && P__ValidFileName(leaf) ? (u32)errno : EINVAL; break; }
        b32 folder = S_ISDIR(info.st_mode);
        error = P__TrashInto(scratch.arena, P__FilePathCString(scratch.arena, homeTrash), source, leaf, folder);
        if (error != EXDEV) continue;
        // Items on other file systems go to that file system's own trash, at the top of its mount.
        char top[PATH_MAX];
        char *parentPath = P__FilePathCString(scratch.arena, parent);
        if (!parentPath || !realpath(parentPath, top)) { error = (u32)errno; break; }
        for (;;) {
            char *slash = strrchr(top, '/');
            if (!slash || slash == top) {
                struct stat root;
                if (stat("/", &root) == 0 && root.st_dev == info.st_dev) strcpy(top, "/");
                break;
            }
            char saved = *slash;
            *slash = 0;
            struct stat above;
            b32 same = stat(top[0] ? top : "/", &above) == 0 && above.st_dev == info.st_dev;
            if (!same) {
                *slash = saved;
                break;
            }
        }
        String trash = StrFormat(scratch.arena, "%s/.Trash-%u", strcmp(top, "/") == 0 ? "" : top, (u32)getuid());
        error = P__TrashInto(scratch.arena, P__FilePathCString(scratch.arena, trash), source, leaf, folder);
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function String P_ExpandVariables(Arena *arena, String text, b32 final)
{
    if (!StrIndexChar(text, '$', SSF_None).exists && !StrIndexChar(text, '%', SSF_None).exists && !(text.length && text.value[0] == '~')) return text;
    Scratch scratch = GetScratchEx(arena);
    StrBuilder out = {0};
    b32 changed = false;
    for (u64 at = 0; at < text.length;) {
        char c = text.value[at];
        u64 from = at + 1, to = from, next = from;
        b32 named = false;
        if (c == '~' && at == 0 && (text.length == 1 ? final : text.value[1] == '/')) {
            const char *home = getenv("HOME");
            if (home) {
                StrBuildStr(scratch.arena, &out, StrFromCStr(home));
                changed = true;
                ++at;
                continue;
            }
        }
        if (c == '$' && from < text.length && text.value[from] == '{') {
            to = ++from;
            while (to < text.length && text.value[to] != '}') ++to;
            named = to < text.length;
            next = to + 1;
        } else if (c == '$' || c == '%') {
            for (u8 n; to < text.length && ((n = (u8)text.value[to]) == '_' || (n >= '0' && n <= '9') || ((n | 0x20) >= 'a' && (n | 0x20) <= 'z'));) ++to;
            // A name runs to a character that cannot be in one, or for %NAME% to its closing sign.
            named = to > from && (c == '%' ? to < text.length && text.value[to] == '%' : to < text.length || final);
            next = c == '%' ? to + 1 : to;
        }
        const char *value = NULL;
        if (named) {
            char *name = ArenaPushArray(scratch.arena, to - from + 1, char);
            MemCopy(name, text.value + from, to - from);
            value = getenv(name);
        }
        if (value) {
            StrBuildStr(scratch.arena, &out, StrFromCStr(value));
            changed = true;
            at = next;
        } else {
            StrBuildChar(scratch.arena, &out, c);
            ++at;
        }
    }
    String result = changed ? StrCopy(arena, out.buffer) : text;
    ReleaseScratch(scratch);
    return result;
}

function b32 P_StartupFlag(String flag)
{
    Scratch scratch = GetScratch();
    StrBuilder text = {0};
    i32 fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    char buffer[4096];
    for (ssize_t length; fd >= 0 && (length = read(fd, buffer, sizeof(buffer))) > 0;) StrBuildStr(scratch.arena, &text, StrMake(buffer, (u64)length));
    if (fd >= 0) close(fd);
    ArrayStr arguments = StrSplit(scratch.arena, text.buffer, 0);
    b32 result = false;
    for (u64 i = 1; i < arguments.header.count && !result; ++i) result = StrIsEqual(arguments.v[i], flag);
    ReleaseScratch(scratch);
    return result;
}

function String P_StartupOption(Arena *arena, String prefix)
{
    Scratch scratch = GetScratchEx(arena);
    StrBuilder text = {0};
    i32 fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    char buffer[4096];
    for (ssize_t length; fd >= 0 && (length = read(fd, buffer, sizeof(buffer))) > 0;) StrBuildStr(scratch.arena, &text, StrMake(buffer, (u64)length));
    if (fd >= 0) close(fd);
    ArrayStr arguments = StrSplit(scratch.arena, text.buffer, 0);
    String result = {0};
    for (u64 i = 1; i < arguments.header.count && !result.length; ++i) {
        String argument = arguments.v[i];
        if (argument.length > prefix.length && StrIsEqual(StrFromTo(argument, 0, (i64)prefix.length), prefix)) result = StrCopy(arena, StrFrom(argument, prefix.length));
    }
    ReleaseScratch(scratch);
    return result;
}

function void P_CompleteSearchPath(void) {}

function ArrayStr P_StartupPaths(Arena *arena)
{
    // The arguments are null-separated in /proc, which spares the program's entry point from passing them down.
    Scratch scratch = GetScratchEx(arena);
    StrBuilder text = {0};
    i32 fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    char buffer[4096];
    for (ssize_t length; fd >= 0 && (length = read(fd, buffer, sizeof(buffer))) > 0;) StrBuildStr(scratch.arena, &text, StrMake(buffer, (u64)length));
    if (fd >= 0) close(fd);
    ArrayStr result = {0};
    ArrayStr arguments = StrSplit(scratch.arena, text.buffer, 0);
    for (u64 i = 1; i < arguments.header.count; ++i) {
        // Flags are not folders.
        if (StrIsEqual(StrFromTo(arguments.v[i], 0, 2), S("--"))) continue;
        String path = arguments.v[i].length ? P_AbsolutePath(arena, arguments.v[i]) : (String){0};
        if (path.length) ArrayPush(arena, result, path);
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P__IsDirectory(Arena *scratch, String path)
{
    struct stat info;
    char *native = P__FilePathCString(scratch, path);
    return native && stat(native, &info) == 0 && S_ISDIR(info.st_mode);
}

// The folder user-dirs.dirs names by `key` ("XDG_DOWNLOAD_DIR="$HOME/Downloads""), or `name` in the home when it does not set it.
function String P__UserDir(Arena *arena, String home, const char *key, const char *name)
{
    Scratch scratch = GetScratchEx(arena);
    const char *config = getenv("XDG_CONFIG_HOME");
    String settings = {0}, path = StrFormat(scratch.arena, "%S/%s", home, name), prefix = StrFormat(scratch.arena, "XDG_%s_DIR=\"", key);
    P_ReadFile(scratch.arena, config && config[0] == '/' ? StrFormat(scratch.arena, "%s/user-dirs.dirs", config) : StrFormat(scratch.arena, "%S/.config/user-dirs.dirs", home), &settings);
    ArrayStr lines = StrSplit(scratch.arena, settings, '\n');
    for (u64 j = 0; j < lines.header.count; ++j) {
        String line = lines.v[j];
        if (line.length <= prefix.length || !StrIsEqual(StrFromTo(line, 0, (i64)prefix.length), prefix)) continue;
        String value = StrFrom(line, prefix.length);
        Index quote = StrIndexChar(value, '"', SSF_None);
        if (quote.exists) value = StrFromTo(value, 0, (i64)quote.value);
        if (value.length >= 5 && StrIsEqual(StrFromTo(value, 0, 5), S("$HOME"))) path = StrFormat(scratch.arena, "%S%S", home, StrFrom(value, 5));
        else if (value.length && value.value[0] == '/') path = value;
    }
    String result = P_AbsolutePath(arena, path);
    ReleaseScratch(scratch);
    return result;
}

// The folder whose files the New menu offers to start from, as Nemo's does: the user's templates folder.
function String P__TemplatesDir(Arena *arena)
{
    const char *home = getenv("HOME");
    return home && home[0] == '/' ? P__UserDir(arena, P_AbsolutePath(arena, StrFromCStr(home)), "TEMPLATES", "Templates") : (String){0};
}

// The folders of user-dirs.dirs, and the usual names when it does not set them.
function P_ArrayPlace P_UserPlaces(Arena *arena)
{
    P_ArrayPlace result = {0};
    const char *homeValue = getenv("HOME");
    if (!homeValue || homeValue[0] != '/') return result;
    String home = P_AbsolutePath(arena, StrFromCStr(homeValue));
    Index slash = StrIndexChar(home, '/', SSF_SearchBackwards);
    String name = slash.exists && slash.value + 1 < home.length ? StrFrom(home, slash.value + 1) : S("Home");
    ArrayPush(arena, result, ((P_Place){ .name = name, .path = home, .home = true }));
    local_persist const struct { const char *key; const char *name; } folders[] = {
        { "DESKTOP", "Desktop" }, { "DOCUMENTS", "Documents" }, { "DOWNLOAD", "Downloads" },
        { "MUSIC", "Music" }, { "PICTURES", "Pictures" }, { "VIDEOS", "Videos" },
    };
    Scratch scratch = GetScratchEx(arena);
    const char *config = getenv("XDG_CONFIG_HOME");
    for (u32 i = 0; i < CArrayCount(folders); ++i) {
        String path = P__UserDir(arena, home, folders[i].key, folders[i].name);
        // Folders the user turned off point at the home itself.
        if (path.length && !StrIsEqual(path, home) && P__IsDirectory(scratch.arena, path)) {
            ArrayPush(arena, result, ((P_Place){ .name = StrFromCStr(folders[i].name), .path = path }));
        }
    }
    // The folders bookmarked in GTK's file chooser and in Nautilus, a file:// address and an optional name a line.
    String bookmarks = {0};
    P_ReadFile(scratch.arena, config && config[0] == '/' ? StrFormat(scratch.arena, "%s/gtk-3.0/bookmarks", config) : StrFormat(scratch.arena, "%S/.config/gtk-3.0/bookmarks", home), &bookmarks);
    ArrayStr marks = StrSplit(scratch.arena, bookmarks, '\n');
    for (u64 i = 0; i < marks.header.count; ++i) {
        Index space = StrIndexChar(marks.v[i], ' ', SSF_None);
        String uri = space.exists ? StrFromTo(marks.v[i], 0, (i64)space.value) : marks.v[i], label = space.exists ? StrFrom(marks.v[i], space.value + 1) : (String){0};
        String path = P__FilePathFromUri(scratch.arena, uri);
        if (!path.length || !P__IsDirectory(scratch.arena, path)) continue;
        path = P_AbsolutePath(arena, path);
        b32 known = false;
        for (u64 k = 0; k < result.header.count && !known; ++k) known = StrIsEqual(result.v[k].path, path);
        Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
        String name = label.length ? StrCopy(arena, label) : slash.exists && slash.value + 1 < path.length ? StrFrom(path, slash.value + 1) : path;
        if (!known) ArrayPush(arena, result, ((P_Place){ .name = name, .path = path }));
    }
    String trash = P_TrashFolder(arena);
    if (trash.length) ArrayPush(arena, result, ((P_Place){ .name = S("Trash"), .path = trash }));
    ReleaseScratch(scratch);
    return result;
}

function ArrayStr P_SearchPlaces(Arena *arena)
{
    ArrayStr result = {0};
    const char *homeValue = getenv("HOME");
    String home = homeValue && homeValue[0] == '/' ? StrFromCStr(homeValue) : (String){0};
    Scratch scratch = GetScratchEx(arena);
    String folders[] = {
        home.length ? StrFormat(scratch.arena, "%S/.config", home) : (String){0}, home.length ? StrFormat(scratch.arena, "%S/.local/share", home) : (String){0},
        home.length ? StrFormat(scratch.arena, "%S/.cache", home) : (String){0}, home.length ? StrFormat(scratch.arena, "%S/.local/share/applications", home) : (String){0},
        S("/usr/share/applications"), S("/usr/share/fonts"), S("/etc"), S("/opt"),
    };
    for (u32 i = 0; i < CArrayCount(folders); ++i) {
        if (folders[i].length && P_IsDirectory(folders[i])) ArrayPush(arena, result, P_AbsolutePath(arena, folders[i]));
    }
    ReleaseScratch(scratch);
    return result;
}

function ArrayStr P_PathRoots(Arena *arena)
{
    ArrayStr result = {0};
    P_ArrayPlace places = P_UserPlaces(arena);
    for (u64 i = 0; i < places.header.count; ++i) ArrayPush(arena, result, places.v[i].path);
    return result;
}

// A mount point as /proc lists it, whose spaces and other bytes are octal escapes ("\040").
function String P__MountPoint(Arena *arena, String raw)
{
    StrBuilder point = {0};
    for (u64 at = 0; at < raw.length; ++at) {
        if (raw.value[at] == '\\' && at + 3 < raw.length) {
            u32 value = (u32)(raw.value[at + 1] - '0') * 64 + (u32)(raw.value[at + 2] - '0') * 8 + (u32)(raw.value[at + 3] - '0');
            StrBuildChar(arena, &point, (char)value);
            at += 3;
        } else {
            StrBuildChar(arena, &point, raw.value[at]);
        }
    }
    return point.buffer;
}

// Whether the file system mounted at `path` is on a local device, as /proc lists its source ("/dev/nvme0n1p3"), rather than a network
// share, a lookup in which could wait for as long as the share does not answer.
function b32 P__LocalMount(Arena *arena, String path)
{
    Scratch scratch = GetScratchEx(arena);
    String mounts = {0};
    P_ReadFile(scratch.arena, S("/proc/self/mounts"), &mounts);
    ArrayStr lines = StrSplit(scratch.arena, mounts, '\n');
    b32 result = false;
    for (u64 i = 0; i < lines.header.count; ++i) {
        ArrayStr fields = StrSplit(scratch.arena, lines.v[i], ' ');
        if (fields.header.count < 3 || !StrIsEqual(P__MountPoint(scratch.arena, fields.v[1]), path)) continue;
        // A later mount on the same point covers the earlier one.
        result = fields.v[0].length > 5 && StrIsEqual(StrFromTo(fields.v[0], 0, 5), S("/dev/"));
    }
    ReleaseScratch(scratch);
    return result;
}

// "3.6 TB", in the binary units disks are listed by.
function String P__ShortSize(Arena *arena, u64 bytes)
{
    local_persist const char *units[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    f64 value = (f64)bytes;
    u32 unit = 0;
    while (value >= 1024.0 && unit + 1 < CArrayCount(units)) value /= 1024.0, ++unit;
    return StrFormat(arena, "%.1f %s", value, units[unit]);
}

// The UUID of the file system on `device`, as /dev/disk/by-uuid lists it; empty where it has none.
function String P__DeviceUuid(Arena *arena, String device)
{
    Index slash = StrIndexChar(device, '/', SSF_SearchBackwards);
    String leaf = StrFrom(device, slash.exists ? slash.value + 1 : 0);
    DIR *folder = leaf.length ? opendir("/dev/disk/by-uuid") : NULL;
    String result = {0};
    for (struct dirent *entry; folder && !result.length && (entry = readdir(folder));) {
        char target[PATH_MAX];
        i64 length = readlinkat(dirfd(folder), entry->d_name, target, sizeof(target) - 1);
        if (length <= 0) continue;
        String linked = StrMake(target, (u64)length);
        Index at = StrIndexChar(linked, '/', SSF_SearchBackwards);
        if (StrIsEqual(StrFrom(linked, at.exists ? at.value + 1 : 0), leaf)) result = StrCopy(arena, StrFromCStr(entry->d_name));
    }
    if (folder) closedir(folder);
    return result;
}

// Whether the mount folder `name` is the UUID of `device`, as /dev/disk/by-uuid lists it.
function b32 P__NamedByUuid(Arena *arena, String name, String device)
{
    char target[PATH_MAX];
    char *link = P__FilePathCString(arena, StrFormat(arena, "/dev/disk/by-uuid/%S", name));
    i64 length = link ? readlink(link, target, sizeof(target) - 1) : -1;
    if (length <= 0) return false;
    String linked = StrMake(target, (u64)length);
    Index at = StrIndexChar(linked, '/', SSF_SearchBackwards), slash = StrIndexChar(device, '/', SSF_SearchBackwards);
    return StrIsEqual(StrFrom(linked, at.exists ? at.value + 1 : 0), StrFrom(device, slash.exists ? slash.value + 1 : 0));
}

function b32 P_RealPath(Arena *arena, String path, String *real)
{
    *real = (String){0};
    Scratch scratch = GetScratchEx(arena);
    char *native = P__FilePathCString(scratch.arena, path), resolved[PATH_MAX];
    b32 result = native && realpath(native, resolved);
    if (result) *real = StrCopy(arena, StrFromCStr(resolved));
    ReleaseScratch(scratch);
    return result;
}

// The model of the disk `device` is on, as sysfs gives it under the disk that holds a partition; empty where there is none.
function String P__DiskModel(Arena *arena, String device)
{
    Index slash = StrIndexChar(device, '/', SSF_SearchBackwards);
    char *block = P__FilePathCString(arena, StrFormat(arena, "/sys/class/block/%S", StrFrom(device, slash.exists ? slash.value + 1 : 0)));
    char resolved[PATH_MAX];
    if (!block || !realpath(block, resolved)) return (String){0};
    String disk = StrFromCStr(resolved);
    if (P_PathExists(StrFormat(arena, "%S/partition", disk))) disk = StrFromTo(disk, 0, (i64)StrIndexChar(disk, '/', SSF_SearchBackwards).value);
    String model = {0};
    P_ReadFile(arena, StrFormat(arena, "%S/device/model", disk), &model);
    while (model.length && (model.value[model.length - 1] == ' ' || model.value[model.length - 1] == '\n')) --model.length;
    while (model.length && model.value[0] == ' ') model = StrFrom(model, 1);
    return model;
}

// The root and what is mounted under the usual places for disks: /run/media and /media for removable ones, /mnt for the rest.
// Mounted shares, as a CIFS or NFS mount under /mnt, are drives here, and there is no asking a server for its shares without Samba's tools.
function P_ArrayPlace P_ServerShares(Arena *arena, String server, const P_CancelToken *cancel)
{
    return (P_ArrayPlace){0};
}

function b32 P_RunCommand(Arena *arena, String directory, const String *arguments, u32 count, u32 timeoutMilliseconds, String *output, b32 errors);

// Mounts the phones and cameras GVFS knows of and asks a file manager to mount, as Nautilus and Nemo do; GNOME's shell does it itself,
// but other desktops leave them unmounted, and so missing from GVFS's folder. `gio` takes half a second to answer, so this runs on a thread
// of its own, one at a time, which the drive listing does not wait for; the mounting runs on its own too, as a phone may first ask to be
// let in, and each is tried again only after half a minute until it first mounts. Once it has been mounted it is left alone, so that one
// ejected stays so, until it is gone from GVFS's list, as when it is unplugged, and comes again.
// ponytail: one `gio mount -li` each drive refresh; watch the volume monitor over D-Bus if that shows in profiles.
global atomic_bool P__mountingDevices;

// The disks' partitions udisks offers but nothing mounted, as a second system's, which the drives list to be mounted when opened, as nemo
// does; mounting an internal disk asks for an administrator's password, so none is mounted unasked. The last `gio` scan's, under the lock.
typedef struct P__Volume { char name[128], device[64], uuid[64]; } P__Volume;
global pthread_mutex_t P__volumesLock = PTHREAD_MUTEX_INITIALIZER;
global P__Volume P__volumes[8];
global u32 P__volumeCount;
// Set as the scan finds them changed, for the window to list the drives again.
global atomic_bool P__drivesChanged;

function void *P__MountDevices(void *unused)
{
    (void)unused;
    local_persist struct { char root[256]; u64 tried; b32 mounted, seen; } attempts[8];
    for (u64 a = 0; a < CArrayCount(attempts); ++a) attempts[a].seen = false;
    Scratch scratch = GetScratch();
    u32 error = 0;
    char *gio = P__FindExecutable(scratch.arena, S("gio"), &error);
    String output = {0};
    String list[] = { S("gio"), S("mount"), S("-li") };
    // Each line indented under the volume or drive it is of; a field is "name=value".
    ArrayStr lines = gio && P_RunCommand(scratch.arena, S("/"), list, CArrayCount(list), 2000, &output, false) ? StrSplit(scratch.arena, output, '\n') : (ArrayStr){0};
    for (u64 i = 0; i < lines.header.count; ++i) {
        String *line = &lines.v[i];
        while (line->length && (line->value[0] == ' ' || line->value[0] == '\t')) *line = StrFrom(*line, 1);
    }
    P__Volume volumes[CArrayCount(P__volumes)];
    u32 volumeCount = 0;
    for (u64 i = 0; i < lines.header.count; ++i) {
        if (!StrIsEqual(StrFromTo(lines.v[i], 0, 7), S("Volume("))) continue;
        String root = {0}, unixDevice = {0}, uuid = {0}, title = StrFrom(lines.v[i], StrIndexStr(lines.v[i], S("): "), SSF_IndexPlusOne).value);
        b32 automount = false, mounted = false, mountable = false;
        for (u64 j = i + 1; j < lines.header.count; ++j) {
            String field = lines.v[j];
            if (StrIsEqual(StrFromTo(field, 0, 7), S("Volume(")) || StrIsEqual(StrFromTo(field, 0, 6), S("Drive("))) break;
            if (StrIsEqual(StrFromTo(field, 0, 6), S("Mount("))) mounted = true;
            if (StrIsEqual(StrFromTo(field, 0, 16), S("activation_root="))) root = StrFrom(field, 16);
            if (StrIsEqual(field, S("should_automount=1"))) automount = true;
            if (StrIsEqual(field, S("can_mount=1"))) mountable = true;
            if (StrIsEqual(StrFromTo(field, 0, 5), S("uuid="))) uuid = StrFrom(field, 5);
            if (StrIsEqual(StrFromTo(field, 0, 19), S("unix-device: '/dev/"))) unixDevice = StrFromTo(field, 14, (i64)field.length - 1);
        }
        if (unixDevice.length && mountable && !mounted && volumeCount < CArrayCount(volumes) && unixDevice.length < sizeof(volumes[0].device)) {
            P__Volume *volume = &volumes[volumeCount++];
            *volume = (P__Volume){0};
            MemCopy(volume->device, unixDevice.value, unixDevice.length);
            MemCopy(volume->name, title.value, Min(title.length, (u64)sizeof(volume->name) - 1));
            MemCopy(volume->uuid, uuid.value, Min(uuid.length, (u64)sizeof(volume->uuid) - 1));
        }
        b32 device = StrIsEqual(StrFromTo(root, 0, 6), S("mtp://")) || StrIsEqual(StrFromTo(root, 0, 10), S("gphoto2://"));
        if (!device || !automount || root.length >= sizeof(attempts[0].root)) continue;
        u64 now = PlatformNowMicroseconds(), slot = 0;
        b32 known = false;
        for (u64 a = 0; a < CArrayCount(attempts) && !known; ++a) {
            if (StrIsEqual(StrFromCStr(attempts[a].root), root)) slot = a, known = true;
            else if (attempts[a].tried < attempts[slot].tried) slot = a;
        }
        if (!known) {
            MemCopy(attempts[slot].root, root.value, root.length);
            attempts[slot].root[root.length] = 0;
            attempts[slot].tried = 0, attempts[slot].mounted = false;
        }
        attempts[slot].seen = true;
        attempts[slot].mounted |= mounted;
        if (mounted || attempts[slot].mounted || (attempts[slot].tried && now - attempts[slot].tried < Millions(30))) continue;
        attempts[slot].tried = now;
        char *args[] = { gio, "mount", attempts[slot].root, NULL };
        P__Spawn(gio, args, NULL);
    }
    if (gio && lines.header.count) {
        pthread_mutex_lock(&P__volumesLock);
        b32 changed = volumeCount != P__volumeCount || MemCompare(P__volumes, volumes, volumeCount * sizeof(volumes[0])) != 0;
        MemCopy(P__volumes, volumes, volumeCount * sizeof(volumes[0]));
        P__volumeCount = volumeCount;
        pthread_mutex_unlock(&P__volumesLock);
        if (changed) {
            atomic_store(&P__drivesChanged, true);
            P_WakeWindow();
        }
    }
    // One gone from the list starts over when it comes again.
    for (u64 a = 0; a < CArrayCount(attempts) && lines.header.count; ++a) if (!attempts[a].seen) attempts[a].root[0] = 0;
    ReleaseScratch(scratch);
    atomic_store(&P__mountingDevices, false);
    return NULL;
}

// The name GVFS shows a phone or camera by, as "SAMSUNG Android": its USB model, from udev's record of the device whose serial is
// `serial`, the host in its GVFS folder's name; empty when none has it.
function String P__UsbModel(Arena *arena, String serial)
{
    String result = {0};
    Scratch scratch = GetScratchEx(arena);
    P_DirectoryResult devices = P_EnumerateDirectory(scratch.arena, S("/sys/bus/usb/devices"), NULL);
    for (u64 i = 0; i < devices.files.header.count && !result.length; ++i) {
        String number = {0}, record = {0};
        if (!P_ReadFile(scratch.arena, StrFormat(scratch.arena, "/sys/bus/usb/devices/%S/dev", devices.files.v[i].name), &number)) continue;
        while (number.length && (number.value[number.length - 1] == '\n')) --number.length;
        if (!P_ReadFile(scratch.arena, StrFormat(scratch.arena, "/run/udev/data/c%S", number), &record)) continue;
        if (!StrIndexStr(record, StrFormat(scratch.arena, "E:ID_SERIAL=%S\n", serial), SSF_None).exists) continue;
        Index model = StrIndexStr(record, S("E:ID_MODEL="), SSF_IndexPlusOne);
        if (!model.exists) continue;
        String rest = StrFrom(record, model.value);
        result = StrCopy(arena, StrFromTo(rest, 0, (i64)StrIndexChar(rest, '\n', SSF_CountOnFail).value));
        for (u64 c = 0; c < result.length; ++c) if (result.value[c] == '_') ((char *)result.value)[c] = ' ';
    }
    ReleaseScratch(scratch);
    return result;
}

// GVFS mounts the server, and `gio info` tells the folder it is reached at. Without a password its questions are answered empty, so that it
// gives up rather than waits; with one they are answered in the order it asks them: the user, the address's or else the login's, unless
// the address names one, the domain for a Windows share, left as it offers, and the password.
function b32 P_MountsRemote(void) { return true; }

function P_FileError P_MountRemote(Arena *arena, String address, String password, String *folder)
{
    *folder = (String){0};
    Scratch scratch = GetScratchEx(arena);
    // A partition by its device, which udisks mounts under /run/media, asking through the desktop's polkit agent where it must.
    if (StrIsEqual(StrFromTo(address, 0, 5), S("/dev/"))) {
        String device[] = { S("gio"), S("mount"), S("-d"), address }, output = {0}, mounts = {0};
        P_RunCommand(scratch.arena, S("/"), device, CArrayCount(device), 120000, &output, true);
        P_ReadFile(scratch.arena, S("/proc/self/mounts"), &mounts);
        ArrayStr lines = StrSplit(scratch.arena, mounts, '\n');
        for (u64 i = 0; i < lines.header.count && !folder->length; ++i) {
            ArrayStr fields = StrSplit(scratch.arena, lines.v[i], ' ');
            if (fields.header.count >= 2 && StrIsEqual(fields.v[0], address)) *folder = P__MountPoint(arena, fields.v[1]);
        }
        while (output.length && (output.value[output.length - 1] == '\n' || output.value[output.length - 1] == ' ')) --output.length;
        P_FileError result = folder->length ? (P_FileError){0} : (P_FileError){ .code = EIO, .message = StrCopy(arena, output.length ? output : S("The drive could not be mounted")) };
        ReleaseScratch(scratch);
        return result;
    }
    String mount[] = { S("gio"), S("mount"), address }, info[] = { S("gio"), S("info"), address }, output = {0}, details = {0};
    String answers = S("\n\n\n\n");
    if (password.length) {
        Index scheme = StrIndexStr(address, S("://"), SSF_None);
        String rest = scheme.exists ? StrFrom(address, scheme.value + 3) : address;
        String authority = StrFromTo(rest, 0, (i64)StrIndexChar(rest, '/', SSF_CountOnFail).value);
        b32 named = StrIndexChar(authority, '@', SSF_None).exists, windows = StrIsEqual(StrFromTo(address, 0, 6), S("smb://"));
        const char *login = getenv("USER");
        StrBuilder lines = {0};
        if (!named) StrBuildStr(scratch.arena, &lines, StrFormat(scratch.arena, "%s\n", login ? login : ""));
        if (windows) StrBuildChar(scratch.arena, &lines, '\n');
        StrBuildStr(scratch.arena, &lines, StrFormat(scratch.arena, "%S\n", password));
        answers = lines.buffer;
    }
    // One already mounted fails here and is found below.
    P_RunCommandInput(scratch.arena, S("/"), mount, CArrayCount(mount), answers, 60000, &output, NULL);
    P_FileError result = {0};
    if (P_RunCommand(scratch.arena, S("/"), info, CArrayCount(info), 10000, &details, false)) {
        Index at = StrIndexStr(details, S("local path: "), SSF_IndexPlusOne);
        if (at.exists) {
            String rest = StrFrom(details, at.value);
            *folder = StrCopy(arena, StrFromTo(rest, 0, (i64)StrIndexChar(rest, '\n', SSF_CountOnFail).value));
        }
    }
    if (!folder->length) {
        while (output.length && (output.value[output.length - 1] == '\n' || output.value[output.length - 1] == ' ')) --output.length;
        b32 asked = StrIndexStr(output, S("Password"), SSF_None).exists || StrIndexStr(output, S("Authentication"), SSF_None).exists;
        result = (P_FileError){ .code = asked ? P_MountNeedsPassword : EIO, .message = StrCopy(arena, output.length ? output : S("The server could not be reached")) };
    }
    ReleaseScratch(scratch);
    return result;
}

function P_ArrayPlace P_Drives(Arena *arena)
{
    // Phones and cameras after the disks, as they are in GVFS's one mount, made as the session starts, though they come later.
    P_ArrayPlace result = {0}, devices = {0};
    Scratch scratch = GetScratchEx(arena);
    if (!atomic_exchange(&P__mountingDevices, true)) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, P__MountDevices, NULL) == 0) pthread_detach(thread);
        else atomic_store(&P__mountingDevices, false);
    }
    String mounts = {0};
    P_ReadFile(scratch.arena, S("/proc/self/mounts"), &mounts);
    ArrayStr lines = StrSplit(scratch.arena, mounts, '\n');
    for (u64 i = 0; i < lines.header.count; ++i) {
        ArrayStr fields = StrSplit(scratch.arena, lines.v[i], ' ');
        if (fields.header.count < 3) continue;
        String path = P__MountPoint(scratch.arena, fields.v[1]);
        b32 root = StrIsEqual(path, S("/"));
        b32 wanted = root;
        local_persist const char *prefixes[] = { "/run/media/", "/media/", "/mnt/" };
        for (u32 j = 0; j < CArrayCount(prefixes); ++j) {
            String prefix = StrFromCStr(prefixes[j]);
            wanted |= path.length > prefix.length && StrIsEqual(StrFromTo(path, 0, (i64)prefix.length), prefix);
        }
        b32 seen = false;
        for (u64 j = 0; j < result.header.count; ++j) seen |= StrIsEqual(result.v[j].path, path);
        // GVFS mounts what it reaches in folders of its one mount, phones as "mtp:host=SAMSUNG_Android_R58N" and cameras as "gphoto2:host=...".
        if (!seen && StrIsEqual(fields.v[2], S("fuse.gvfsd-fuse"))) {
            P_DirectoryResult listing = P_EnumerateDirectory(scratch.arena, path, NULL);
            for (u64 j = 0; j < listing.files.header.count; ++j) {
                String name = listing.files.v[j].name;
                b32 phone = StrIsEqual(StrFromTo(name, 0, 4), S("mtp:")), camera = StrIsEqual(StrFromTo(name, 0, 8), S("gphoto2:"));
                // A server connected to, as "sftp:host=example.org,user=me" or "smb-share:server=nas,share=media": its share on its host.
                if (!phone && !camera) {
                    Index colon = StrIndexChar(name, ':', SSF_None);
                    if (!colon.exists) continue;
                    String fields = StrFrom(name, colon.value + 1), server = {0}, share = {0};
                    ArrayStr parts = StrSplit(scratch.arena, fields, ',');
                    for (u64 p = 0; p < parts.header.count; ++p) {
                        if (StrIsEqual(StrFromTo(parts.v[p], 0, 5), S("host="))) server = StrFrom(parts.v[p], 5);
                        if (StrIsEqual(StrFromTo(parts.v[p], 0, 7), S("server="))) server = StrFrom(parts.v[p], 7);
                        if (StrIsEqual(StrFromTo(parts.v[p], 0, 6), S("share="))) share = StrFrom(parts.v[p], 6);
                    }
                    if (!server.length) continue;
                    String shown = share.length ? StrFormat(arena, "%S (%S)", share, server) : StrCopy(arena, server);
                    ArrayPush(arena, devices, ((P_Place){ .name = shown, .path = StrFormat(arena, "%S/%S", path, name), .type = S("Network Drive"),
                                                    .remote = share.length ? StrFormat(arena, "//%S/%S", server, share) : StrFormat(arena, "//%S", server) }));
                    continue;
                }
                Index host = StrIndexStr(name, S("host="), SSF_IndexPlusOne);
                // By the name GVFS shows, else by its address, its underscores spaces.
                String address = host.exists ? StrFrom(name, host.value) : name, shown = P__UsbModel(arena, address);
                if (!shown.length) {
                    shown = StrCopy(arena, address);
                    for (u64 c = 0; c < shown.length; ++c) if (shown.value[c] == '_') ((char *)shown.value)[c] = ' ';
                }
                ArrayPush(arena, devices, ((P_Place){ .name = shown, .path = StrFormat(arena, "%S/%S", path, name), .type = phone ? S("Mobile Phone") : S("Camera"), .device = true }));
            }
            continue;
        }
        if (!wanted || seen) continue;
        P_Place place = { .path = StrCopy(arena, path), .fileSystem = StrCopy(arena, fields.v[2]) };
        Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
        place.name = root ? S("File System") : StrCopy(arena, StrFrom(path, slash.value + 1));
        // Mounts under /run/media and /media are removable ones, as file managers there call them.
        b32 removable = StrIsEqual(StrFromTo(path, 0, 7), S("/media/")) || StrIsEqual(StrFromTo(path, 0, 11), S("/run/media/"));
        place.type = removable ? S("Removable Disk") : S("Local Disk");
        struct statvfs info;
        char *native = P__FilePathCString(scratch.arena, path);
        if (native && statvfs(native, &info) == 0) {
            place.totalBytes = (u64)info.f_blocks * info.f_frsize;
            place.freeBytes = (u64)info.f_bavail * info.f_frsize;
        }
        // udisks names a mount whose file system has no label by its UUID, as "FEBA1237BA11ED41".
        // Such a drive goes by its disk's model instead, as "Samsung SSD 990 PRO 4TB", or by its size where the disk has none, as nemo names it.
        place.id = P__DeviceUuid(arena, fields.v[0]);
        if (!root && P__NamedByUuid(scratch.arena, place.name, fields.v[0])) {
            String model = P__DiskModel(scratch.arena, fields.v[0]);
            if (model.length) place.name = StrCopy(arena, model);
            else if (place.totalBytes) place.name = StrFormat(arena, "%S Volume", P__ShortSize(scratch.arena, place.totalBytes));
        }
        ArrayPush(arena, result, place);
    }
    // Two partitions of one disk would share its model, so each such name gets its size.
    for (u64 i = 0; i < result.header.count; ++i) {
        b32 shared = false;
        for (u64 j = 0; j < result.header.count; ++j) shared |= i != j && StrIsEqual(result.v[i].name, result.v[j].name);
        if (shared && result.v[i].totalBytes) result.v[i].name = StrFormat(arena, "%S (%S)", result.v[i].name, P__ShortSize(scratch.arena, result.v[i].totalBytes));
    }
    // The partitions not mounted after the mounted disks, by their device, which opening mounts.
    pthread_mutex_lock(&P__volumesLock);
    for (u32 i = 0; i < P__volumeCount; ++i) {
        String device = StrFromCStr(P__volumes[i].device);
        b32 seen = false;
        for (u64 j = 0; j < lines.header.count && !seen; ++j) seen = StrIsEqual(StrFromTo(lines.v[j], 0, (i64)device.length + 1), StrFormat(scratch.arena, "%S ", device));
        if (seen) continue;
        // As a mounted one without a label goes: by its disk's model, else as GVFS calls it, as "4.0 TB Volume".
        String model = P__DiskModel(scratch.arena, device);
        ArrayPush(arena, result, ((P_Place){ .name = StrCopy(arena, model.length ? model : StrFromCStr(P__volumes[i].name)), .path = StrCopy(arena, device), .type = S("Local Disk"),
                                             .id = StrCopy(arena, StrFromCStr(P__volumes[i].uuid)) }));
    }
    pthread_mutex_unlock(&P__volumesLock);
    for (u64 i = 0; i < devices.header.count; ++i) ArrayPush(arena, result, devices.v[i]);
    ReleaseScratch(scratch);
    return result;
}

// "filey" under $<variable> or ~/<fallback>, or FILEY_CONFIG_DIR when set, created when missing.
function String P__AppDirectory(Arena *arena, const char *variable, const char *fallback)
{
    const char *override = getenv("FILEY_CONFIG_DIR"), *config = getenv(variable), *home = getenv("HOME");
    String base = config && config[0] == '/' ? StrFromCStr(config) : home && home[0] == '/' ? StrFormat(arena, "%s/%s", home, fallback) : (String){0};
    String result = override && override[0] == '/' ? P_AbsolutePath(arena, StrFromCStr(override)) : base.length ? StrFormat(arena, "%S/filey", base) : (String){0};
    if (!result.length) return result;
    Scratch scratch = GetScratchEx(arena);
    if (base.length && !(override && override[0] == '/')) mkdir(P__FilePathCString(scratch.arena, base), 0700);
    b32 ok = mkdir(P__FilePathCString(scratch.arena, result), 0700) == 0 || errno == EEXIST;
    ReleaseScratch(scratch);
    return ok ? result : (String){0};
}

function String P_ConfigDirectory(Arena *arena) { return P__AppDirectory(arena, "XDG_CONFIG_HOME", ".config"); }
function String P_CacheDirectory(Arena *arena) { return P__AppDirectory(arena, "XDG_CACHE_HOME", ".cache"); }

function b32 P_ReadFile(Arena *arena, String path, String *contents)
{
    *contents = (String){0};
    Scratch scratch = GetScratchEx(arena);
    char *native = P__FilePathCString(scratch.arena, path);
    i32 fd = native ? open(native, O_RDONLY | O_CLOEXEC) : -1;
    ReleaseScratch(scratch);
    if (fd < 0) return false;
    // /proc files report no size, so the file is read until its end.
    StrBuilder text = {0};
    char buffer[16384];
    ssize_t length = 0;
    while ((length = read(fd, buffer, sizeof(buffer))) > 0 && text.buffer.length < (1 << 24)) StrBuildBytes(arena, &text, buffer, (u64)length);
    close(fd);
    *contents = text.buffer;
    return length == 0;
}

// The command gets a process group of its own, so that stopping or pausing it reaches the programs it started too, as yt-dlp's ffmpeg.
function b32 P__RunCommand(Arena *conflict, String directory, const String *arguments, u32 count, const String *environment, u32 environmentCount, b32 errors,
                           u32 timeoutMilliseconds, P_CommandOutput *output, void *user, const P_CancelToken *cancel, P_Progress *progress, String input)
{
    if (!count) return false;
    Scratch scratch = GetScratchEx(conflict);
    // The program is looked up here, before the fork, since the child may only make async-signal-safe calls.
    char *program = NULL;
    const char *paths = getenv("PATH");
    String name = arguments[0];
    for (String rest = paths ? StrFromCStr(paths) : (String){0}; rest.length && !program;) {
        Index colon = StrIndexChar(rest, ':', SSF_CountOnFail);
        String folder = StrFromTo(rest, 0, (i64)colon.value);
        rest = StrFrom(rest, Min(colon.value + 1, rest.length));
        char *candidate = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "%S/%S", folder, name));
        if (folder.length && candidate && access(candidate, X_OK) == 0) program = candidate;
    }
    char **args = ArenaPushArray(scratch.arena, count + 1, char *);
    for (u32 i = 0; i < count; ++i) args[i] = P__FilePathCString(scratch.arena, arguments[i]);
    char *folder = P__FilePathCString(scratch.arena, directory);
    // The program's own environment with `environment` over it, as getenv takes the first of a name.
    char **variables = environ;
    if (environmentCount) {
        u64 own = 0;
        while (environ[own]) ++own;
        variables = ArenaPushArray(scratch.arena, own + environmentCount + 1, char *);
        u64 kept = 0;
        for (u32 i = 0; i < environmentCount; ++i) variables[kept++] = P__FilePathCString(scratch.arena, environment[i]);
        for (u64 i = 0; i < own; ++i) {
            String entry = StrFromCStr(environ[i]);
            Index equals = StrIndexChar(entry, '=', SSF_None);
            b32 replaced = false;
            for (u32 j = 0; equals.exists && j < environmentCount && !replaced; ++j) {
                replaced = environment[j].length > equals.value && environment[j].value[equals.value] == '=' && StrIsEqual(StrFromTo(entry, 0, (i64)equals.value + 1), StrFromTo(environment[j], 0, (i64)equals.value + 1));
            }
            if (!replaced) variables[kept++] = environ[i];
        }
        variables[kept] = NULL;
    }
    i32 ends[2] = {0}, reads[2] = { -1, -1 };
    b32 result = false;
    // What it reads comes through a pipe of its own, written once it has started.
    if (input.length && pipe2(reads, O_CLOEXEC) != 0) reads[0] = reads[1] = -1;
    if (program && folder && pipe2(ends, O_CLOEXEC) == 0) {
        pid_t child = _Fork();
        if (child == 0) {
            setpgid(0, 0);
            i32 quiet = open("/dev/null", O_RDWR);
            dup2(ends[1], 1);
            if (errors) dup2(ends[1], 2);
            if (reads[0] >= 0) dup2(reads[0], 0);
            if (quiet >= 0) {
                if (reads[0] < 0) dup2(quiet, 0);
                if (!errors) dup2(quiet, 2);
            }
            if (chdir(folder) == 0) execve(program, args, variables);
            _exit(127);
        }
        close(ends[1]);
        if (reads[0] >= 0) {
            close(reads[0]);
            // A program that quits before reading it would otherwise end Filey with SIGPIPE.
            signal(SIGPIPE, SIG_IGN);
            if (child > 0) for (u64 at = 0; at < input.length;) {
                ssize_t wrote = write(reads[1], input.value + at, input.length - at);
                if (wrote < 0 && errno == EINTR) continue;
                if (wrote <= 0) break;
                at += (u64)wrote;
            }
            close(reads[1]);
        }
        if (child > 0) {
            // Set from here too, as the child may not have run yet when a signal is sent to its group.
            setpgid(child, child);
            u64 began = PlatformNowMicroseconds();
            b32 stopped = false, suspended = false;
            for (;;) {
                if (P_IsCancelled(cancel) || (timeoutMilliseconds && (PlatformNowMicroseconds() - began) / 1000 > timeoutMilliseconds)) {
                    stopped = true;
                    break;
                }
                b32 pause = progress && atomic_load(&progress->paused);
                if (pause != suspended) {
                    kill(-child, pause ? SIGSTOP : SIGCONT);
                    suspended = pause;
                }
                // What it wrote before the pause waits in the pipe with it, as sound would otherwise play on from there.
                if (suspended) {
                    poll(NULL, 0, 50);
                    continue;
                }
                // Woken at least every tenth of a second to see to a cancel or a pause.
                struct pollfd wait = { .fd = ends[0], .events = POLLIN };
                i32 ready = poll(&wait, 1, 100);
                if (ready < 0 && errno == EINTR) continue;
                if (ready < 0) break;
                if (ready == 0) continue;
                char bytes[65536];
                ssize_t got = read(ends[0], bytes, sizeof(bytes));
                if (got < 0 && errno == EINTR) continue;
                if (got <= 0) break;
                output(user, StrMake(bytes, (u64)got));
            }
            if (stopped) kill(-child, SIGKILL);
            else if (suspended) kill(-child, SIGCONT);
            i32 status = 0;
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            result = !stopped && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        close(ends[0]);
    }
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

function b32 P_RunCommandWith(Arena *arena, String directory, const String *arguments, u32 count, const String *environment, u32 environmentCount,
                              u32 timeoutMilliseconds, String *output, b32 errors, const P_CancelToken *cancel)
{
    P__CommandText collected = { .arena = arena };
    b32 result = P__RunCommand(arena, directory, arguments, count, environment, environmentCount, errors, timeoutMilliseconds, P__CollectOutput, &collected, cancel, NULL, (String){0});
    *output = collected.text.buffer;
    return result;
}

function b32 P_ProgramFound(String name)
{
    const char *path = getenv("PATH");
    if (!path || !name.length) return false;
    Scratch scratch = GetScratch();
    b32 result = false;
    for (String rest = StrFromCStr(path); rest.length && !result;) {
        Index colon = StrIndexChar(rest, ':', SSF_CountOnFail);
        String folder = StrFromTo(rest, 0, (i64)colon.value);
        rest = StrFrom(rest, Min(colon.value + 1, rest.length));
        char *file = folder.length ? P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "%S/%S", folder, name)) : NULL;
        result = file && access(file, X_OK) == 0;
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P_RunCommand(Arena *arena, String directory, const String *arguments, u32 count, u32 timeoutMilliseconds, String *output, b32 errors)
{
    return P_RunCommandWith(arena, directory, arguments, count, NULL, 0, timeoutMilliseconds, output, errors, NULL);
}

function String P_EnvironmentValue(Arena *arena, String name)
{
    Scratch scratch = GetScratchEx(arena);
    char *native = P__FilePathCString(scratch.arena, name);
    const char *value = native ? getenv(native) : NULL;
    String result = value ? StrCopy(arena, StrFromCStr(value)) : (String){0};
    ReleaseScratch(scratch);
    return result;
}

function b32 P_RunCommandStream(String directory, const String *arguments, u32 count, b32 errors, P_CommandOutput *output, void *user, const P_CancelToken *cancel,
                                P_Progress *progress)
{
    return P__RunCommand(NULL, directory, arguments, count, NULL, 0, errors, 0, output, user, cancel, progress, (String){0});
}

function b32 P_RunCommandInput(Arena *arena, String directory, const String *arguments, u32 count, String input, u32 timeoutMilliseconds, String *output,
                               const P_CancelToken *cancel)
{
    P__CommandText collected = { .arena = arena };
    b32 result = P__RunCommand(arena, directory, arguments, count, NULL, 0, false, timeoutMilliseconds, P__CollectOutput, &collected, cancel, NULL, input);
    *output = collected.text.buffer;
    return result;
}

function b32 P_PathExists(String path)
{
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, path);
    struct stat info;
    b32 result = native && lstat(native, &info) == 0;
    ReleaseScratch(scratch);
    return result;
}

function b32 P_FileSize(String path, u64 *size)
{
    Scratch scratch = GetScratch();
    char *native = P__FilePathCString(scratch.arena, path);
    struct stat info;
    b32 result = native && stat(native, &info) == 0 && S_ISREG(info.st_mode);
    *size = result ? (u64)info.st_size : 0;
    ReleaseScratch(scratch);
    return result;
}

function void P_Sleep(u32 milliseconds)
{
    struct timespec wait = { .tv_sec = milliseconds / 1000, .tv_nsec = (long)(milliseconds % 1000) * 1000000 };
    while (nanosleep(&wait, &wait) != 0 && errno == EINTR) {}
}

function b32 P_ReadFileStart(Arena *arena, String path, u64 limit, String *contents)
{
    return P_ReadFileAt(arena, path, 0, limit, contents);
}

function b32 P_ReadFileAt(Arena *arena, String path, u64 offset, u64 limit, String *contents)
{
    *contents = (String){0};
    Scratch scratch = GetScratchEx(arena);
    char *native = P__FilePathCString(scratch.arena, path);
    i32 fd = native ? open(native, O_RDONLY | O_CLOEXEC) : -1;
    ReleaseScratch(scratch);
    if (fd < 0) return false;
    char *bytes = ArenaPushArrayNoZero(arena, limit + 1, char);
    u64 have = 0;
    ssize_t length = 0;
    while (have < limit && ((length = pread(fd, bytes + have, limit - have, (off_t)(offset + have))) > 0 || (length < 0 && errno == EINTR))) {
        if (length > 0) have += (u64)length;
    }
    close(fd);
    *contents = StrMake(bytes, have);
    return length >= 0;
}

function P_FileError P_WriteFile(Arena *arena, String path, String contents)
{
    Scratch scratch = GetScratchEx(arena);
    char *native = P__FilePathCString(scratch.arena, path);
    char *temporary = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "%S.tmp", path));
    u32 code = EINVAL;
    if (native && temporary) {
        i32 fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        code = fd < 0 ? (u32)errno : 0;
        for (u64 at = 0; !code && at < contents.length;) {
            ssize_t written = write(fd, contents.value + at, contents.length - at);
            if (written < 0 && errno != EINTR) code = (u32)errno;
            else if (written > 0) at += (u64)written;
        }
        if (!code && fsync(fd) != 0) code = (u32)errno;
        if (fd >= 0) close(fd);
        if (!code && rename(temporary, native) != 0) code = (u32)errno;
        if (code) unlink(temporary);
    }
    ReleaseScratch(scratch);
    return code ? P__FileError(arena, code) : (P_FileError){0};
}

function P_FileError P_AppendFile(Arena *arena, String path, String contents, b32 restart)
{
    Scratch scratch = GetScratchEx(arena);
    char *native = P__FilePathCString(scratch.arena, path);
    u32 code = EINVAL;
    if (native) {
        i32 fd = open(native, O_WRONLY | O_CREAT | O_CLOEXEC | (restart ? O_TRUNC : O_APPEND), 0600);
        code = fd < 0 ? (u32)errno : 0;
        for (u64 at = 0; !code && at < contents.length;) {
            ssize_t written = write(fd, contents.value + at, contents.length - at);
            if (written < 0 && errno != EINTR) code = (u32)errno;
            else if (written > 0) at += (u64)written;
        }
        if (fd >= 0) close(fd);
    }
    ReleaseScratch(scratch);
    return code ? P__FileError(arena, code) : (P_FileError){0};
}

function P_FileError P_ReplaceFile(Arena *arena, String from, String to)
{
    Scratch scratch = GetScratchEx(arena);
    char *source = P__FilePathCString(scratch.arena, from), *target = P__FilePathCString(scratch.arena, to);
    u32 code = EINVAL;
    // The new file keeps the permissions of the one it replaces.
    struct stat info = {0};
    if (source && target && stat(target, &info) == 0) chmod(source, info.st_mode & 07777);
    if (source && target) code = rename(source, target) == 0 ? 0 : (u32)errno;
    ReleaseScratch(scratch);
    return code ? P__FileError(arena, code) : (P_FileError){0};
}

// ponytail: cancellation is checked between items, not inside one big folder.
function P_FileError P_DeleteItems(Arena *arena, const String *paths, u64 count, b32 confirm, const P_CancelToken *cancel)
{
    (void)confirm;
    Scratch scratch = GetScratchEx(arena);
    u32 error = 0;
    for (u64 i = 0; i < count && !error; ++i) {
        if (P_IsCancelled(cancel)) { error = ECANCELED; break; }
        String parent = {0}, leaf = {0};
        P__SplitPath(paths[i], &parent, &leaf);
        if (!leaf.length || StrIsEqual(leaf, S(".")) || StrIsEqual(leaf, S(".."))) { error = EINVAL; break; }
        char *folder = P__FilePathCString(scratch.arena, parent), *name = P__FilePathCString(scratch.arena, leaf);
        i32 fd = folder && name ? open(folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
        error = fd < 0 ? (u32)errno : P__RemoveAt(fd, name);
        if (fd >= 0) close(fd);
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}
