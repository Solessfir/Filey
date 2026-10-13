#include <dirent.h>
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

// Every item is something the program carries out itself: Open and Rename by their verbs, the rest by their actions.
// ponytail: no Open With or Open in Terminal yet; they need the desktop's application database.
function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
    P_MenuItem items[16];
    u32 itemCount = 0;
    P_MenuItem separator = { .separator = true };
    if (count) {
        items[itemCount++] = (P_MenuItem){ .label = S("Open"), .verb = S("open"), .isDefault = true };
        items[itemCount++] = separator;
        items[itemCount++] = (P_MenuItem){ .label = S("Cut"), .verb = S("cut"), .action = P_MenuAction_Cut };
        items[itemCount++] = (P_MenuItem){ .label = S("Copy"), .verb = S("copy"), .action = P_MenuAction_Copy };
        items[itemCount++] = (P_MenuItem){ .label = S("Duplicate"), .action = P_MenuAction_Duplicate };
        items[itemCount++] = (P_MenuItem){ .label = S("Copy As Path"), .action = P_MenuAction_CopyAsPath };
        items[itemCount++] = separator;
        if (count == 1) items[itemCount++] = (P_MenuItem){ .label = S("Rename"), .verb = S("rename") };
        items[itemCount++] = (P_MenuItem){ .label = S("Trash"), .verb = S("delete"), .action = P_MenuAction_Trash };
    } else {
        items[itemCount++] = (P_MenuItem){ .label = S("Paste"), .verb = S("paste"), .action = P_MenuAction_Paste };
        items[itemCount++] = separator;
        items[itemCount++] = (P_MenuItem){ .label = S("New"), .firstChild = 0, .childCount = 2 };
    }
    u32 rootCount = itemCount;
    if (!count) {
        items[rootCount - 1].firstChild = rootCount;
        items[itemCount++] = (P_MenuItem){ .label = S("Folder"), .action = P_MenuAction_NewFolder };
        items[itemCount++] = (P_MenuItem){ .label = S("File"), .action = P_MenuAction_NewFile };
    }
    menu->items = ArenaPushArray(arena, itemCount, P_MenuItem);
    for (u32 i = 0; i < itemCount; ++i) {
        menu->items[i] = items[i];
        if (!items[i].separator) menu->items[i].id = i + 1;
    }
    menu->count = itemCount;
    menu->rootCount = rootCount;
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

// Wayland drags are asynchronous: this starts one and returns, and the window code handles the rest.
function b32 LinuxStartDrag(String directory, const String *names, u64 count);

function b32 P_DragFiles(String directory, const String *names, u64 count)
{
    return LinuxStartDrag(directory, names, count);
}

// Where dropped files go; the window's drop handler reads it.
global struct { char path[PATH_MAX]; u64 length; } P__drop;

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
    P_FileError error = P_TransferItems(&transfer->arena, transfer->paths, transfer->count, transfer->directory, transfer->move, &token);
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
function u32 P__CopyAt(i32 sourceDir, const char *name, i32 targetDir, const char *targetName, b32 *created, const P_CancelToken *cancel)
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
            error = P__CopyAt(dirfd(stream), child, to, child, &childCreated, cancel);
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

function P_FileError P_TransferItems(Arena *arena, const String *paths, u64 count, String directory, b32 move, const P_CancelToken *cancel)
{
    Scratch scratch = GetScratchEx(arena);
    char *targetPath = P__FilePathCString(scratch.arena, directory);
    char realTarget[PATH_MAX];
    i32 targetDir = targetPath ? open(targetPath, O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1;
    u32 error = targetDir < 0 || !realpath(targetPath, realTarget) ? (u32)errno : 0;
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
            continue;
        }
        error = EEXIST;
        for (u32 n = 1; n < 10000 && error == EEXIST; ++n) {
            char *name = P__NumberedName(scratch.arena, leaf, S_ISDIR(info.st_mode), n);
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
            error = P__CopyAt(sourceDir, leafName, targetDir, name, &created, cancel);
            if (error && created) P__RemoveAt(targetDir, name);
            if (!error && move) error = P__RemoveAt(sourceDir, leafName);
        }
        close(sourceDir);
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
