#include <dirent.h>
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
#include <sys/wait.h>
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
            struct stat metadata = {0};
            if (fstatat(dirfd(stream), entry->d_name, &metadata, AT_SYMLINK_NOFOLLOW) == 0) {
                file.symbolicLink = S_ISLNK(metadata.st_mode);
                file.metadataValid = !file.symbolicLink || fstatat(dirfd(stream), entry->d_name, &metadata, 0) == 0;
                if (file.metadataValid) {
                    file.kind = S_ISDIR(metadata.st_mode) ? P_FileKind_Directory : S_ISREG(metadata.st_mode) ? P_FileKind_File : P_FileKind_Other;
                    file.size = metadata.st_size > 0 ? (u64)metadata.st_size : 0;
                    file.modifiedTimeUnix = (i64)metadata.st_mtime;
                }
            }
            ArrayPush(arena, result.files, file);
        }
        if (closedir(stream) != 0 && !error) error = (u32)errno;
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

function char *P__FileLauncher(Arena *arena, u32 *error)
{
    const char *environmentPath = getenv("PATH");
    String searchPath = environmentPath ? StrFromCStr(environmentPath) : S("/bin:/usr/bin");
    *error = ENOENT;
    for (u64 from = 0, to = 0; to <= searchPath.length; ++to) {
        if (to != searchPath.length && searchPath.value[to] != ':') continue;
        String directory = StrFromTo(searchPath, from, (i64)to);
        String candidate = StrFormat(arena, "%S%Sxdg-open", directory, directory.length ? S("/") : S(""));
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

// ponytail: `order` is unused until a Linux launcher protocol carries neighboring files.
function P_FileError P_OpenPath(Arena *arena, String path, P_FileOrder order)
{
    (void)order;
    Scratch scratch = GetScratchEx(arena);
    String absolute = P_AbsolutePath(scratch.arena, path);
    char *file = absolute.length ? P__FilePathCString(scratch.arena, absolute) : NULL;
    u32 error = file ? 0 : (u32)errno;
    char *executable = file ? P__FileLauncher(scratch.arena, &error) : NULL;
    if (executable) {
        char *args[] = {"xdg-open", file, NULL};
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
                    execve(executable, args, environ);
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
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

// ponytail: only "Open", which the program carries out itself; add Open With,
// Copy Path and the like as Filey implements them.
function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
    if (count == 0) return false;
    menu->items = ArenaPushArray(arena, 1, P_MenuItem);
    menu->items[0] = (P_MenuItem){ .label = S("Open"), .verb = S("open"), .id = 1, .isDefault = true };
    menu->count = menu->rootCount = 1;
    return true;
}

function void P_ContextMenuWarm(String directory, String name)
{
    (void)directory;
    (void)name;
}

function P_FileError P_ContextMenuInvoke(Arena *arena, P_ContextMenu *menu, u32 id)
{
    return P__FileError(arena, ENOTSUP);
}

function void P_ContextMenuClose(P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
}

// ponytail: no drag and drop yet; needs wl_data_device offers (text/uri-list) and a copy engine for drops.
function b32 P_DragFiles(String directory, const String *names, u64 count)
{
    (void)directory;
    (void)names;
    (void)count;
    return false;
}

function void P_SetDropDirectory(String path)
{
    (void)path;
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

typedef struct P__MeasureStack { ArrayHeader header; DIR **v; } P__MeasureStack;

function P_DirectoryTotals P_MeasureDirectory(String path, const P_CancelToken *cancel)
{
    P_DirectoryTotals result = {0};
    Scratch scratch = GetScratch();
    char *root = P__FilePathCString(scratch.arena, path.length ? path : S("."));
    P__MeasureStack stack = {0};
    // ponytail: one open folder per level, so trees deeper than the descriptor limit are cut short.
    DIR *first = root ? opendir(root) : NULL;
    if (first) ArrayPush(scratch.arena, stack, first);
    result.complete = first != NULL;
    while (stack.header.count) {
        if (P_IsCancelled(cancel)) {
            result.complete = false;
            break;
        }
        DIR *stream = stack.v[stack.header.count - 1];
        struct dirent *entry = readdir(stream);
        if (!entry) {
            closedir(stream);
            --stack.header.count;
            continue;
        }
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
            ++result.folders;
            i32 fd = openat(dirfd(stream), name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            DIR *child = fd >= 0 ? fdopendir(fd) : NULL;
            if (child) ArrayPush(scratch.arena, stack, child);
            else if (fd >= 0) close(fd);
        } else {
            ++result.files;
            if (statted && S_ISREG(metadata.st_mode) && metadata.st_size > 0) result.bytes += (u64)metadata.st_size;
        }
    }
    for (u64 i = 0; i < stack.header.count; ++i) closedir(stack.v[i]);
    ReleaseScratch(scratch);
    return result;
}
