function b32 P__TestWriteFile(TestFileFixture *fixture, String name, u32 size, b32 hidden)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    String path = StrFormat(scratch.arena, "%S/%S", fixture->path, name);
    wchar *nativePath = P__FilePathWide(scratch.arena, path);
    HANDLE file = CreateFileW(nativePath, GENERIC_WRITE, 0, NULL, CREATE_NEW, hidden ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL, NULL);
    b32 result = file != INVALID_HANDLE_VALUE;
    if (result) {
        LARGE_INTEGER length = {.QuadPart = size};
        result = SetFilePointerEx(file, length, NULL, FILE_BEGIN) && SetEndOfFile(file);
        CloseHandle(file);
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 P__TestMakeDirectory(TestFileFixture *fixture, String name)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    String path = StrFormat(scratch.arena, "%S/%S", fixture->path, name);
    b32 result = CreateDirectoryW(P__FilePathWide(scratch.arena, path), NULL) != 0;
    ReleaseScratch(scratch);
    return result;
}

function b32 P__TestRemovePath(TestFileFixture *fixture, String name, b32 directory)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    String path = StrFormat(scratch.arena, "%S/%S", fixture->path, name);
    wchar *nativePath = P__FilePathWide(scratch.arena, path);
    b32 result = directory ? RemoveDirectoryW(nativePath) != 0 : DeleteFileW(nativePath) != 0;
    u32 error = GetLastError();
    ReleaseScratch(scratch);
    return result || error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

function b32 P__TestCreateFixture(TestFileFixture *fixture)
{
    wchar temporary[MAX_PATH] = {0}, path[MAX_PATH] = {0};
    u32 length = GetTempPathW((u32)CArrayCount(temporary), temporary);
    if (!length || length >= CArrayCount(temporary) || !GetTempFileNameW(temporary, L"Fyt", 0, path)) return false;
    if (!DeleteFileW(path) || !CreateDirectoryW(path, NULL)) return false;
    fixture->path = P_AbsolutePath(&fixture->arena, P__FileStringWide(&fixture->arena, path, lstrlenW(path)));
    fixture->created = true;
    for (u64 i = 0; i < CArrayCount(TestFixtureEntries); ++i) {
        TestFixtureEntry entry = TestFixtureEntries[i];
        if (!(entry.directory ? P__TestMakeDirectory(fixture, entry.name) : P__TestWriteFile(fixture, entry.name, entry.size, entry.hidden))) return false;
    }
    return true;
}

function void P__TestRemoveFixture(TestFileFixture *fixture)
{
    if (fixture->created && fixture->path.length) {
        Check(P__TestRemovePath(fixture, S("refresh.txt"), false));
        for (u64 i = CArrayCount(TestFixtureEntries); i > 0; --i) {
            TestFixtureEntry entry = TestFixtureEntries[i - 1];
            Check(P__TestRemovePath(fixture, entry.name, entry.directory));
        }
        Scratch scratch = GetScratchEx(&fixture->arena);
        Check(RemoveDirectoryW(P__FilePathWide(scratch.arena, fixture->path)) != 0);
        ReleaseScratch(scratch);
    }
    ArenaRelease(&fixture->arena);
    *fixture = (TestFileFixture){0};
}

function void P__TestPause(void)
{
    Sleep(1);
}
