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

// Forwards a copy of the fixture's plain.txt into its Empty folder through the shell drop target, as a drag from another program would.
// Only touches the fixture.
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

// The query handed to packaged apps lists the fixture's files in Filey's order. Nothing is launched.
function void P__TestNeighborQuery(TestFileFixture *fixture)
{
    local_persist const IID storageItem = { 0x4207A996, 0xCA2F, 0x42F7, { 0xBD, 0xE8, 0x8B, 0x10, 0x45, 0x7A, 0x7F, 0x30 } };
    Scratch scratch = GetScratchEx(&fixture->arena);
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    for (u32 descending = 0; descending < 2; ++descending) {
        P__FileQuery *query = P__NeighborQuery(P__FilePathWide(scratch.arena, fixture->path), (P_FileOrder){ P_FileOrderKey_Name, (b8)descending });
        __FIAsyncOperation_1___FIVectorView_1_Windows__CStorage__CStorageFile *listing = NULL;
        __FIVectorView_1_Windows__CStorage__CStorageFile *files = NULL;
        Check(query && SUCCEEDED(query->lpVtbl->GetFilesAsyncDefaultStartAndCount(query, &listing)) && P__WinRtWait(listing) == P__Wait_Completed &&
              SUCCEEDED(listing->lpVtbl->GetResults(listing, &files)) && files);
        u32 count = 0;
        if (files) files->lpVtbl->get_Size(files, &count);
        Check(count >= 4);
        String previous = {0};
        for (u32 i = 0; i < count; ++i) {
            __x_ABI_CWindows_CStorage_CIStorageFile *file = NULL;
            __x_ABI_CWindows_CStorage_CIStorageItem *item = NULL;
            HSTRING name = NULL;
            String current = {0};
            if (SUCCEEDED(files->lpVtbl->GetAt(files, i, &file)) && SUCCEEDED(file->lpVtbl->QueryInterface(file, &storageItem, (void **)&item)) &&
                SUCCEEDED(item->lpVtbl->get_Name(item, &name))) {
                u32 length = 0;
                const wchar *raw = WindowsGetStringRawBuffer(name, &length);
                current = P__FileStringWide(scratch.arena, raw, (i32)length);
            }
            Check(current.length);
            if (previous.length) Check(descending ? Prog_CompareNames(previous, current) > 0 : Prog_CompareNames(previous, current) < 0);
            previous = current;
            WindowsDeleteString(name);
            if (item) item->lpVtbl->Release(item);
            if (file) file->lpVtbl->Release(file);
        }
        if (files) files->lpVtbl->Release(files);
        if (listing) listing->lpVtbl->Release(listing);
        if (query) query->lpVtbl->Release(query);
    }
    if (SUCCEEDED(com)) CoUninitialize();
    ReleaseScratch(scratch);
}
