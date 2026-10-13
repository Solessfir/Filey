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

// Forwards a copy of the fixture's plain.txt into its Empty folder through the
// shell drop target, as a drag from another program would. Only touches the fixture.
function void P__TestDropForwarding(TestFileFixture *fixture)
{
    Scratch scratch = GetScratchEx(&fixture->arena);
    IShellFolder *folder = P__ShellFolder(P__FilePathWide(scratch.arena, fixture->path));
    IDataObject *data = NULL;
    String names[] = { S("plain.txt") };
    Check(folder && P__ShellChildren(folder, names, 1, &P__IID_IDataObject, (void **)&data));
    if (data) {
        FORMATETC drop = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        Check(data->lpVtbl->QueryGetData(data, &drop) == S_OK);

        POINTL point = {0};
        DWORD effect = DROPEFFECT_COPY;
        P_SetDropDirectory((String){0});
        P__DropEnter(data);
        P__DropOver(0, point, &effect);
        Check(effect == DROPEFFECT_NONE);
        P__DropLeave();

        String target = StrFormat(scratch.arena, "%S/Empty", fixture->path);
        P_SetDropDirectory(target);
        effect = DROPEFFECT_COPY;
        P__DropEnter(data);
        P__DropOver(0, point, &effect);
        Check(effect == DROPEFFECT_COPY);
        effect = DROPEFFECT_COPY;
        Check(P__DropDrop(0, point, &effect));
        Check(P__drop.data == NULL && P__drop.shell == NULL);

        // The shell may copy on its own thread.
        wchar *copy = P__FilePathWide(scratch.arena, StrFormat(scratch.arena, "%S/plain.txt", target));
        b32 copied = false;
        for (u32 i = 0; i < 500 && !copied; ++i) {
            copied = GetFileAttributesW(copy) != INVALID_FILE_ATTRIBUTES;
            if (!copied) Sleep(10);
        }
        Check(copied);
        if (copied) Check(DeleteFileW(copy) != 0);
        P_SetDropDirectory((String){0});
        data->lpVtbl->Release(data);
    }
    if (folder) folder->lpVtbl->Release(folder);
    ReleaseScratch(scratch);
}
