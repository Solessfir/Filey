#include <dirent.h>
#include <dlfcn.h>
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
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

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

// libarchive, loaded when first needed so that it is not a build dependency; every Arch system has it, as pacman uses it.
typedef struct P__Archive {
    b32 tried, loaded;
    void *(*readNew)(void);
    i32 (*supportFilterAll)(void *);
    i32 (*supportFormatAll)(void *);
    i32 (*openFilename)(void *, const char *, u64);
    i32 (*nextHeader)(void *, void **);
    const char *(*pathname)(void *);
    i64 (*size)(void *);
    u32 (*filetype)(void *);
    i32 (*free)(void *);
} P__Archive;
global P__Archive P__archive;

function b32 P__LoadArchive(void)
{
    P__Archive *a = &P__archive;
    if (a->tried) return a->loaded;
    a->tried = true;
    void *library = dlopen("libarchive.so.13", RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
    *(void **)&a->readNew = dlsym(library, "archive_read_new");
    *(void **)&a->supportFilterAll = dlsym(library, "archive_read_support_filter_all");
    *(void **)&a->supportFormatAll = dlsym(library, "archive_read_support_format_all");
    *(void **)&a->openFilename = dlsym(library, "archive_read_open_filename");
    *(void **)&a->nextHeader = dlsym(library, "archive_read_next_header");
    *(void **)&a->pathname = dlsym(library, "archive_entry_pathname_utf8");
    *(void **)&a->size = dlsym(library, "archive_entry_size");
    *(void **)&a->filetype = dlsym(library, "archive_entry_filetype");
    *(void **)&a->free = dlsym(library, "archive_read_free");
    a->loaded = a->readNew && a->supportFilterAll && a->supportFormatAll && a->openFilename && a->nextHeader && a->pathname && a->size && a->filetype && a->free;
    return a->loaded;
}

function b32 P_ListArchive(Arena *arena, String path, u64 limit, const P_CancelToken *cancel, P_FileArray *files)
{
    *files = (P_FileArray){0};
    if (!P__LoadArchive()) return false;
    P__Archive *a = &P__archive;
    Scratch scratch = GetScratchEx(arena);
    char *cpath = P__FilePathCString(scratch.arena, path);
    void *archive = cpath ? a->readNew() : NULL;
    b32 result = false;
    if (archive) {
        a->supportFilterAll(archive);
        a->supportFormatAll(archive);
        // ARCHIVE_OK is 0 and ARCHIVE_WARN -20, which still gives the entry; the data of each entry is skipped by the next header.
        if (a->openFilename(archive, cpath, 65536) == 0) {
            void *entry = NULL;
            i32 status = 0;
            while (files->header.count < limit && !P_IsCancelled(cancel) && ((status = a->nextHeader(archive, &entry)) == 0 || status == -20)) {
                const char *name = a->pathname(entry);
                if (!name) continue;
                String inner = StrMake(name, strlen(name));
                if (inner.length >= 2 && inner.value[0] == '.' && inner.value[1] == '/') inner = StrFrom(inner, 2);
                b32 folder = (a->filetype(entry) & 0170000) == 0040000;
                while (inner.length && inner.value[inner.length - 1] == '/') --inner.length;
                if (!inner.length) continue;
                ArrayPush(arena, *files, ((P_File){ .name = StrCopy(arena, inner), .size = folder ? 0 : (u64)Max(a->size(entry), (i64)0),
                                                     .kind = folder ? P_FileKind_Directory : P_FileKind_File }));
            }
        }
        a->free(archive);
    }
    ReleaseScratch(scratch);
    result = files->header.count != 0;
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
                    file.modifiedTimeUnix = (i64)metadata.stx_mtime.tv_sec;
                    file.accessedTimeUnix = (i64)metadata.stx_atime.tv_sec;
                    file.createdTimeUnix = (metadata.stx_mask & STATX_BTIME) ? (i64)metadata.stx_btime.tv_sec : 0;
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

function P_FileError P_OpenWindow(Arena *arena, String path)
{
    Scratch scratch = GetScratchEx(arena);
    char executable[PATH_MAX];
    i64 length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    char *folder = P__FilePathCString(scratch.arena, path);
    u32 error = length > 0 && folder ? 0 : (u32)errno;
    if (!error) {
        executable[length] = 0;
        char *args[] = {executable, folder, NULL};
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

// Where dropped files go; the window's drop handler reads it.
// While `takesFolders` is on, drops are kept in `dropped` for the program instead. The window sets `pending` while a drag is over it.
global struct { char path[PATH_MAX]; u64 length; b32 takesFolders; b32 pending; Arena droppedArena; ArrayStr dropped; } P__drop;

function void P_SetDropTakesFolders(b32 takes) { P__drop.takesFolders = takes; }

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

typedef struct P__DropTransfer {
    Arena arena;
    String *paths;
    u64 count;
    String directory;
    b32 move;
} P__DropTransfer;

function void *P__DropThread(void *user)
{
    P__DropTransfer *transfer = user;
    P_CancelToken token = {0};
    P_FileError error = P_TransferItems(&transfer->arena, transfer->paths, transfer->count, transfer->directory, transfer->move, NULL, NULL, &token);
    // ponytail: drop errors only reach stderr; report them in the status bar through the program if drops fail in practice.
    if (error.code) fprintf(stderr, "Filey: dropping into %.*s failed: %.*s\n", (i32)transfer->directory.length, transfer->directory.value, (i32)error.message.length, error.message.value);
    Arena arena = transfer->arena;
    ArenaRelease(&arena);
    return NULL;
}

// Copies or moves dropped files into the drop folder on a thread of their own, so a big drop never holds the window; the folder's watch shows the result.
function void P__TransferDropped(const ArrayStr *paths, b32 move)
{
    if (!paths->header.count || !P__drop.length) return;
    P__DropTransfer *transfer = ArenaBootstrapStruct(P__DropTransfer, arena);
    transfer->directory = StrCopy(&transfer->arena, StrMake(P__drop.path, P__drop.length));
    transfer->paths = ArenaPushArray(&transfer->arena, paths->header.count, String);
    for (u64 i = 0; i < paths->header.count; ++i) transfer->paths[i] = StrCopy(&transfer->arena, paths->v[i]);
    transfer->count = paths->header.count;
    transfer->move = move;
    pthread_t thread;
    if (pthread_create(&thread, NULL, P__DropThread, transfer) == 0) {
        pthread_detach(thread);
    } else {
        Arena arena = transfer->arena;
        ArenaRelease(&arena);
    }
}

// A '/' would turn a rename into a move.
function b32 P__ValidFileName(String name)
{
    return name.length && !StrIsEqual(name, S(".")) && !StrIsEqual(name, S("..")) && !StrIndexChar(name, '/', SSF_None).exists;
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

function b32 P_CountFolder(Arena *arena, String path, P_DirectoryTotals *totals, ArrayStr *subfolders)
{
    Scratch scratch = GetScratchEx(arena);
    char *cpath = P__FilePathCString(scratch.arena, path.length ? path : S("."));
    DIR *stream = cpath ? opendir(cpath) : NULL;
    String parent = path.length > 1 && path.value[path.length - 1] == '/' ? StrFromTo(path, 0, (i64)path.length - 1) : path;
    struct stat folder = {0};
    b32 device = stream && fstat(dirfd(stream), &folder) == 0;
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
                if (statted && S_ISREG(metadata.st_mode) && metadata.st_size > 0) totals->bytes += (u64)metadata.st_size;
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
        b32 plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
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

// `name` numbered for attempt `n`: the name itself, then "stem (2).ext" for files and "name (2)" for folders.
function char *P__NumberedName(Arena *arena, String name, b32 folder, u32 n)
{
    if (n == 1) return P__FilePathCString(arena, name);
    Index dot = folder ? (Index){0} : StrIndexChar(name, '.', SSF_SearchBackwards);
    String stem = dot.exists && dot.value ? StrFromTo(name, 0, (i64)dot.value) : name;
    String extension = dot.exists && dot.value ? StrFrom(name, dot.value) : (String){0};
    return P__FilePathCString(arena, StrFormat(arena, "%S (%u)%S", stem, n, extension));
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

// Copies `name` in `sourceDir` to `targetName` in `targetDir`.
// Folders are copied with everything in them and links as links; sockets, pipes and devices are skipped.
// Returns EEXIST without touching anything when `targetName` is taken; `created` tells whether the target was made, so that a failed copy can be removed without touching anything else.
function u32 P__CopyAt(i32 sourceDir, const char *name, i32 targetDir, const char *targetName, b32 *created, P_Progress *progress, const P_CancelToken *cancel)
{
    *created = false;
    if (P_IsCancelled(cancel)) return ECANCELED;
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
    i32 in = openat(sourceDir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) return (u32)errno;
    i32 out = openat(targetDir, targetName, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, (info.st_mode & 07777) | S_IWUSR);
    if (out < 0) {
        u32 error = (u32)errno;
        close(in);
        return error;
    }
    *created = true;
    u32 error = 0;
    b32 kernelCopy = true;
    char buffer[65536];
    for (;;) {
        if (P_IsCancelled(cancel)) { error = ECANCELED; break; }
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
    if (close(out) != 0 && !error) error = (u32)errno;
    close(in);
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
        if (P_IsCancelled(cancel)) { error = ECANCELED; break; }
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

function b32 P_PrefersDarkColors(void)
{
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
function String P_TrashName(void) { return S("Trash"); }

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

// The folders of user-dirs.dirs ("XDG_DOWNLOAD_DIR="$HOME/Downloads""), and the usual names when it does not set them.
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
    String settings = {0};
    P_ReadFile(scratch.arena, config && config[0] == '/' ? StrFormat(scratch.arena, "%s/user-dirs.dirs", config) : StrFormat(scratch.arena, "%S/.config/user-dirs.dirs", home), &settings);
    ArrayStr lines = StrSplit(scratch.arena, settings, '\n');
    for (u32 i = 0; i < CArrayCount(folders); ++i) {
        String path = StrFormat(scratch.arena, "%S/%s", home, folders[i].name);
        String key = StrFormat(scratch.arena, "XDG_%s_DIR=\"", folders[i].key);
        for (u64 j = 0; j < lines.header.count; ++j) {
            String line = lines.v[j];
            if (line.length <= key.length || !StrIsEqual(StrFromTo(line, 0, (i64)key.length), key)) continue;
            String value = StrFrom(line, key.length);
            Index quote = StrIndexChar(value, '"', SSF_None);
            if (quote.exists) value = StrFromTo(value, 0, (i64)quote.value);
            if (value.length >= 5 && StrIsEqual(StrFromTo(value, 0, 5), S("$HOME"))) path = StrFormat(scratch.arena, "%S%S", home, StrFrom(value, 5));
            else if (value.length && value.value[0] == '/') path = value;
        }
        path = P_AbsolutePath(arena, path);
        // Folders the user turned off point at the home itself.
        if (path.length && !StrIsEqual(path, home) && P__IsDirectory(scratch.arena, path)) {
            ArrayPush(arena, result, ((P_Place){ .name = StrFromCStr(folders[i].name), .path = path }));
        }
    }
    String trash = P_TrashFolder(arena);
    if (trash.length) ArrayPush(arena, result, ((P_Place){ .name = S("Trash"), .path = trash }));
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
function P_ArrayPlace P_Drives(Arena *arena)
{
    P_ArrayPlace result = {0};
    Scratch scratch = GetScratchEx(arena);
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
    ReleaseScratch(scratch);
    return result;
}

function String P_ConfigDirectory(Arena *arena)
{
    const char *override = getenv("FILEY_CONFIG_DIR"), *config = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    String base = config && config[0] == '/' ? StrFromCStr(config) : home && home[0] == '/' ? StrFormat(arena, "%s/.config", home) : (String){0};
    String result = override && override[0] == '/' ? P_AbsolutePath(arena, StrFromCStr(override)) : base.length ? StrFormat(arena, "%S/filey", base) : (String){0};
    if (!result.length) return result;
    Scratch scratch = GetScratchEx(arena);
    if (base.length && !(override && override[0] == '/')) mkdir(P__FilePathCString(scratch.arena, base), 0700);
    b32 ok = mkdir(P__FilePathCString(scratch.arena, result), 0700) == 0 || errno == EEXIST;
    ReleaseScratch(scratch);
    return ok ? result : (String){0};
}

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

function b32 P_RunCommand(Arena *arena, String directory, const String *arguments, u32 count, u32 timeoutMilliseconds, String *output)
{
    *output = (String){0};
    if (!count) return false;
    Scratch scratch = GetScratchEx(arena);
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
    i32 ends[2] = {0};
    b32 result = false;
    if (program && folder && pipe2(ends, O_CLOEXEC) == 0) {
        pid_t child = _Fork();
        if (child == 0) {
            i32 quiet = open("/dev/null", O_RDWR);
            dup2(ends[1], 1);
            if (quiet >= 0) {
                dup2(quiet, 0);
                dup2(quiet, 2);
            }
            if (chdir(folder) == 0) execve(program, args, environ);
            _exit(127);
        }
        close(ends[1]);
        if (child > 0) {
            StrBuilder out = {0};
            u64 began = PlatformNowMicroseconds();
            b32 late = false;
            for (;;) {
                i64 left = (i64)timeoutMilliseconds - (i64)((PlatformNowMicroseconds() - began) / 1000);
                if (left <= 0) {
                    late = true;
                    break;
                }
                struct pollfd wait = { .fd = ends[0], .events = POLLIN };
                i32 ready = poll(&wait, 1, (i32)left);
                if (ready < 0 && errno == EINTR) continue;
                if (ready <= 0) {
                    late = ready == 0;
                    break;
                }
                char bytes[4096];
                ssize_t got = read(ends[0], bytes, sizeof(bytes));
                if (got < 0 && errno == EINTR) continue;
                if (got <= 0) break;
                StrBuildStr(arena, &out, StrMake(bytes, (u64)got));
            }
            if (late) kill(child, SIGKILL);
            i32 status = 0;
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            result = !late && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            *output = out.buffer;
        }
        close(ends[0]);
    }
    ReleaseScratch(scratch);
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
