#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

function char *P__TestNativePath(Arena *arena, TestFileFixture *fixture, String name)
{
    String path = StrFormat(arena, "%S/%S", fixture->path, name);
    char *result = ArenaPushArray(arena, path.length + 1, char);
    MemCopy(result, path.value, path.length);
    return result;
}

function b32 P__TestWriteFile(TestFileFixture *fixture, String name, u32 size, b32 hidden)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    i32 file = open(P__TestNativePath(scratch.arena, fixture, name), O_WRONLY | O_CREAT | O_EXCL, 0600);
    b32 result = file >= 0;
    if (result) {
        result = ftruncate(file, size) == 0;
        close(file);
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P__TestMakeDirectory(TestFileFixture *fixture, String name)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    b32 result = mkdir(P__TestNativePath(scratch.arena, fixture, name), 0700) == 0;
    ReleaseScratch(scratch);
    return result;
}

function b32 P__TestRemovePath(TestFileFixture *fixture, String name, b32 directory)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    char *nativePath = P__TestNativePath(scratch.arena, fixture, name);
    b32 result = (directory ? rmdir(nativePath) : unlink(nativePath)) == 0;
    i32 error = errno;
    ReleaseScratch(scratch);
    return result || error == ENOENT;
}

function b32 P__TestCreateFixture(TestFileFixture *fixture)
{
    char path[] = "/tmp/filey-tests-XXXXXX";
    if (!mkdtemp(path)) return false;
    fixture->path = StrCopy(&fixture->arena, StrFromCStr(path));
    fixture->created = true;
    for (u64 i = 0; i < CArrayCount(TestFixtureEntries); ++i) {
        TestFixtureEntry entry = TestFixtureEntries[i];
        if (!(entry.directory ? P__TestMakeDirectory(fixture, entry.name) : P__TestWriteFile(fixture, entry.name, entry.size, entry.hidden))) return false;
    }
    if (!P__TestWriteFile(fixture, S("invalid-\xff.txt"), 1, false)) return false;
    Scratch scratch = GetScratchEx(&fixture->arena);
    b32 result = symlink("missing-target", P__TestNativePath(scratch.arena, fixture, S("broken-link"))) == 0;
    ReleaseScratch(scratch);
    return result;
}

function void P__TestRemoveFixture(TestFileFixture *fixture)
{
    if (fixture->created && fixture->path.length) {
        String launcherFiles[] = {S("xdg-open"), S("launch-argument"), S("launch-running"), S("launch-finished")};
        for (u64 i = 0; i < CArrayCount(launcherFiles); ++i) Check(P__TestRemovePath(fixture, launcherFiles[i], false));
        Check(P__TestRemovePath(fixture, S("refresh.txt"), false));
        Check(P__TestRemovePath(fixture, S("broken-link"), false));
        Check(P__TestRemovePath(fixture, S("invalid-\xff.txt"), false));
        for (u64 i = CArrayCount(TestFixtureEntries); i > 0; --i) {
            TestFixtureEntry entry = TestFixtureEntries[i - 1];
            Check(P__TestRemovePath(fixture, entry.name, entry.directory));
        }
        Scratch scratch = GetScratchEx(&fixture->arena);
        char *nativePath = P__TestNativePath(scratch.arena, fixture, S(""));
        Check(rmdir(nativePath) == 0);
        ReleaseScratch(scratch);
    }
    ArenaRelease(&fixture->arena);
    *fixture = (TestFileFixture){0};
}

function void P__TestPause(void)
{
    struct timespec duration = {.tv_nsec = 1000000};
    nanosleep(&duration, NULL);
}

function b32 P__TestFileExists(TestFileFixture *fixture, String name)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    struct stat info = {0};
    b32 result = stat(P__TestNativePath(scratch.arena, fixture, name), &info) == 0;
    ReleaseScratch(scratch);
    return result;
}

function void P__TestOpenPath(TestFileFixture *fixture)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    const char *original = getenv("PATH");
    b32 hadPath = original != NULL;
    String savedPath = original ? StrCopy(scratch.arena, StrFromCStr(original)) : (String){0};
    char *launcher = P__TestNativePath(scratch.arena, fixture, S("xdg-open"));
    String script = S("#!/bin/sh\nroot=${0%/*}\nprintf '%s' \"$1\" > \"$root/launch-argument\"\n: > \"$root/launch-running\"\n/bin/sleep 1\n: > \"$root/launch-finished\"\n");
    i32 file = open(launcher, O_WRONLY | O_CREAT | O_EXCL, 0700);
    Check(file >= 0);
    if (file < 0) {
        ReleaseScratch(scratch);
        return;
    }
    Check(write(file, script.value, script.length) == (i64)script.length);
    Check(close(file) == 0);
    char *fixturePath = P__FilePathCString(scratch.arena, fixture->path);
    b32 isolated = setenv("PATH", fixturePath, 1) == 0;
    Check(isolated);
    if (!isolated) goto restore;
    String target = StrFormat(scratch.arena, "%S/Zażółć 🙂 ';$(printf unsafe).txt", fixture->path);
    P_FileError error = P_OpenPath(&fixture->arena, target, (P_FileOrder){0});
    Check(!error.code);
    u64 start = PlatformNowMicroseconds();
    while (!P__TestFileExists(fixture, S("launch-running")) && PlatformNowMicroseconds() - start < Millions(5)) P__TestPause();
    Check(P__TestFileExists(fixture, S("launch-running")) && !P__TestFileExists(fixture, S("launch-finished")));
    file = open(P__TestNativePath(scratch.arena, fixture, S("launch-argument")), O_RDONLY);
    Check(file >= 0);
    if (file >= 0) {
        char argument[4096] = {0};
        i64 length = read(file, argument, sizeof(argument));
        Check(length >= 0 && StrIsEqual(StrMake(argument, length > 0 ? (u64)length : 0), target));
        Check(close(file) == 0);
    }
    while (!P__TestFileExists(fixture, S("launch-finished")) && PlatformNowMicroseconds() - start < Millions(5)) P__TestPause();
    Check(P__TestFileExists(fixture, S("launch-finished")));
    error = P_OpenPath(&fixture->arena, S("bad\0path"), (P_FileOrder){0});
    Check(error.code == EINVAL);
    char *emptyPath = P__TestNativePath(scratch.arena, fixture, S("Empty"));
    isolated = setenv("PATH", emptyPath, 1) == 0;
    Check(isolated);
    if (!isolated) goto restore;
    error = P_OpenPath(&fixture->arena, target, (P_FileOrder){0});
    Check(error.code == ENOENT && error.message.length);
    isolated = setenv("PATH", fixturePath, 1) == 0 && chmod(launcher, 0600) == 0;
    Check(isolated);
    if (!isolated) goto restore;
    error = P_OpenPath(&fixture->arena, target, (P_FileOrder){0});
    Check(error.code == EACCES);
restore:
    if (hadPath) {
        Check(setenv("PATH", P__FilePathCString(scratch.arena, savedPath), 1) == 0);
    } else {
        Check(unsetenv("PATH") == 0);
    }
    ReleaseScratch(scratch);
}
