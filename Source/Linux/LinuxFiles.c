#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
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

function P_FileError P_OpenPath(Arena *arena, String path)
{
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
