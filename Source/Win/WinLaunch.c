// Opening files.
// Packaged apps step through the rest of a folder only when the launch carries a neighboring files query, as Explorer's does, and only the WinRT launcher can attach one.
// The Windows 11 Photos app ignores that query from desktop callers but builds the folder itself when opened through its ms-photos: viewer link.
// Every other file keeps ShellExecute.
// Requires WinFiles.c and WinShell.c (P__shellOwner).

#pragma push_macro("function")
#pragma push_macro("global")
#undef function
#undef global
#include <shlwapi.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.storage.h>
#include <windows.storage.search.h>
#include <windows.system.h>
#pragma pop_macro("global")
#pragma pop_macro("function")

global const IID P__IID_IAsyncInfo                   = { 0x00000036, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
global const IID P__IID_IInitializeWithWindow        = { 0x3E68D4BD, 0x7135, 0x4D10, { 0x80, 0x18, 0x9F, 0xB6, 0xD9, 0xF3, 0x3F, 0xA1 } };
global const IID P__IID_ILauncherStatics             = { 0x277151C3, 0x9E3E, 0x42F6, { 0x91, 0xA4, 0x5D, 0xFD, 0xEB, 0x23, 0x24, 0x51 } };
global const IID P__IID_ILauncherOptions             = { 0xBAFA21D8, 0xB071, 0x4CD8, { 0x85, 0x3E, 0x34, 0x12, 0x03, 0xE5, 0x57, 0xD3 } };
global const IID P__IID_ILauncherOptions2            = { 0x3BA08EB4, 0x6E40, 0x4DCE, { 0xA1, 0xA3, 0x2F, 0x53, 0x95, 0x0A, 0xFB, 0x49 } };
global const IID P__IID_IStorageFolderStatics        = { 0x08F327FF, 0x85D5, 0x48B9, { 0xAE, 0xE9, 0x28, 0x51, 0x1E, 0x33, 0x9F, 0x9F } };
global const IID P__IID_IStorageFileStatics          = { 0x5984C710, 0xDAF2, 0x43C8, { 0x8B, 0xB4, 0xA4, 0xD3, 0xEA, 0xCF, 0xD0, 0x3F } };
global const IID P__IID_IQueryOptions                = { 0x1E5E46EE, 0x0F45, 0x4838, { 0xA8, 0xE9, 0xD0, 0x47, 0x9D, 0x44, 0x6C, 0x30 } };
global const IID P__IID_IStorageFolderQueryOperations = { 0xCB43CCC9, 0x446B, 0x4A4F, { 0xBE, 0x97, 0x75, 0x77, 0x71, 0xBE, 0x52, 0x03 } };

typedef __x_ABI_CWindows_CStorage_CSearch_CIStorageFileQueryResult P__FileQuery;

typedef enum P__Wait {
    P__Wait_Failed,
    P__Wait_Completed,
    P__Wait_TimedOut,
    P__Wait_Count,
} P__Wait;

// ponytail: polls instead of registering a completion delegate; the waits are short and run on a job.
function P__Wait P__WinRtWait(void *operation)
{
    IUnknown *unknown = operation;
    IAsyncInfo *info = NULL;
    if (FAILED(unknown->lpVtbl->QueryInterface(unknown, &P__IID_IAsyncInfo, (void **)&info))) return P__Wait_Failed;
    AsyncStatus status = Started;
    for (u32 i = 0; i < 10000 && SUCCEEDED(info->lpVtbl->get_Status(info, &status)) && status == Started; ++i) Sleep(1);
    info->lpVtbl->Release(info);
    return status == Completed ? P__Wait_Completed : status == Started ? P__Wait_TimedOut : P__Wait_Failed;
}

function HSTRING P__HString(const wchar *text)
{
    HSTRING result = NULL;
    if (FAILED(WindowsCreateString(text, (u32)lstrlenW(text), &result))) result = NULL;
    return result;
}

function void *P__WinRtFactory(const wchar *className, const IID *iid)
{
    void *result = NULL;
    HSTRING name = P__HString(className);
    if (!name || FAILED(RoGetActivationFactory(name, iid, &result))) result = NULL;
    WindowsDeleteString(name);
    return result;
}

function void *P__WinRtCreate(const wchar *className, const IID *iid)
{
    void *result = NULL;
    IInspectable *instance = NULL;
    HSTRING name = P__HString(className);
    if (name && SUCCEEDED(RoActivateInstance(name, &instance))) {
        if (FAILED(instance->lpVtbl->QueryInterface(instance, iid, &result))) result = NULL;
        instance->lpVtbl->Release(instance);
    }
    WindowsDeleteString(name);
    return result;
}

// The app id of a packaged default handler, which names its app under the ProgID; desktop handlers have none.
function b32 P__PackagedHandler(const wchar *path, wchar *appId, u32 capacity)
{
    const wchar *extension = PathFindExtensionW(path);
    wchar progId[256] = {0};
    DWORD length = CArrayCount(progId);
    if (!extension[0] || FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, extension, NULL, progId, &length))) return false;
    wchar key[300] = {0};
    if (lstrlenW(progId) + 13 >= (i32)CArrayCount(key)) return false;
    lstrcpyW(key, progId);
    lstrcatW(key, L"\\Application");
    DWORD size = capacity * sizeof(wchar);
    return RegGetValueW(HKEY_CLASSES_ROOT, key, L"AppUserModelID", RRF_RT_REG_SZ, NULL, appId, &size) == ERROR_SUCCESS;
}

// "ms-photos:viewer?fileName=" with the path percent-encoded as UTF-8.
function wchar *P__PhotosLink(Arena *arena, String path)
{
    local_persist const char hex[] = "0123456789ABCDEF";
    String prefix = S("ms-photos:viewer?fileName=");
    u8 *link = ArenaPushArray(arena, prefix.length + path.length * 3, u8);
    MemCopy(link, prefix.value, prefix.length);
    u64 length = prefix.length;
    for (u64 i = 0; i < path.length; ++i) {
        u8 c = (u8)path.value[i];
        b32 plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        // The viewer expects native separators.
        if (c == '/') c = '\\';
        if (plain) {
            link[length++] = c;
        } else {
            link[length++] = '%';
            link[length++] = (u8)hex[c >> 4];
            link[length++] = (u8)hex[c & 15];
        }
    }
    return P__FilePathWide(arena, StrMake((const char *)link, length));
}

// The files of `directory` in Filey's order, for the launched app to step through.
function P__FileQuery *P__NeighborQuery(const wchar *directory, P_FileOrder order)
{
    local_persist const wchar *properties[P_FileOrderKey_Count] = {
        L"System.ItemNameDisplay", L"System.ItemTypeText", L"System.DateModified", L"System.Size",
    };
    P__FileQuery *result = NULL;
    __x_ABI_CWindows_CStorage_CIStorageFolderStatics *folders = P__WinRtFactory(RuntimeClass_Windows_Storage_StorageFolder, &P__IID_IStorageFolderStatics);
    __x_ABI_CWindows_CStorage_CSearch_CIQueryOptions *options = P__WinRtCreate(RuntimeClass_Windows_Storage_Search_QueryOptions, &P__IID_IQueryOptions);
    __FIAsyncOperation_1_Windows__CStorage__CStorageFolder *opening = NULL;
    __x_ABI_CWindows_CStorage_CIStorageFolder *folder = NULL;
    __x_ABI_CWindows_CStorage_CSearch_CIStorageFolderQueryOperations *queries = NULL;
    __FIVector_1_Windows__CStorage__CSearch__CSortEntry *sort = NULL;
    HSTRING path = P__HString(directory);
    HSTRING key = P__HString(properties[order.key < P_FileOrderKey_Count ? order.key : P_FileOrderKey_Name]);
    HSTRING name = P__HString(properties[P_FileOrderKey_Name]);
    b32 ok = folders && options && path && key && name &&
             SUCCEEDED(folders->lpVtbl->GetFolderFromPathAsync(folders, path, &opening)) && P__WinRtWait(opening) == P__Wait_Completed &&
             SUCCEEDED(opening->lpVtbl->GetResults(opening, &folder)) && folder &&
             SUCCEEDED(folder->lpVtbl->QueryInterface(folder, &P__IID_IStorageFolderQueryOperations, (void **)&queries)) &&
             SUCCEEDED(options->lpVtbl->get_SortOrder(options, &sort)) && SUCCEEDED(sort->lpVtbl->Clear(sort)) &&
             SUCCEEDED(sort->lpVtbl->Append(sort, (__x_ABI_CWindows_CStorage_CSearch_CSortEntry){ key, !order.descending }));
    // Ties fall back to the name, as in the listing.
    if (ok && order.key != P_FileOrderKey_Name) ok = SUCCEEDED(sort->lpVtbl->Append(sort, (__x_ABI_CWindows_CStorage_CSearch_CSortEntry){ name, true }));
    if (ok && FAILED(queries->lpVtbl->CreateFileQueryWithOptions(queries, options, &result))) result = NULL;
    WindowsDeleteString(name);
    WindowsDeleteString(key);
    WindowsDeleteString(path);
    if (sort) sort->lpVtbl->Release(sort);
    if (queries) queries->lpVtbl->Release(queries);
    if (folder) folder->lpVtbl->Release(folder);
    if (opening) opening->lpVtbl->Release(opening);
    if (options) options->lpVtbl->Release(options);
    if (folders) folders->lpVtbl->Release(folders);
    return result;
}

// True once the app was launched, or may still be launching after the wait gave up.
function b32 P__LaunchWithNeighbors(const wchar *path, P_FileOrder order)
{
    wchar directory[MAX_PATH * 4] = {0};
    i32 length = lstrlenW(path);
    if (length >= (i32)CArrayCount(directory)) return false;
    lstrcpyW(directory, path);
    PathRemoveFileSpecW(directory);

    __x_ABI_CWindows_CSystem_CILauncherStatics *launcher = P__WinRtFactory(RuntimeClass_Windows_System_Launcher, &P__IID_ILauncherStatics);
    __x_ABI_CWindows_CStorage_CIStorageFileStatics *files = P__WinRtFactory(RuntimeClass_Windows_Storage_StorageFile, &P__IID_IStorageFileStatics);
    __x_ABI_CWindows_CSystem_CILauncherOptions *options = P__WinRtCreate(RuntimeClass_Windows_System_LauncherOptions, &P__IID_ILauncherOptions);
    __x_ABI_CWindows_CSystem_CILauncherOptions2 *options2 = NULL;
    IInitializeWithWindow *window = NULL;
    __FIAsyncOperation_1_Windows__CStorage__CStorageFile *opening = NULL;
    __x_ABI_CWindows_CStorage_CIStorageFile *file = NULL;
    __FIAsyncOperation_1_boolean *launching = NULL;
    P__FileQuery *neighbors = P__NeighborQuery(directory, order);
    HSTRING hpath = P__HString(path);
    b32 ok = launcher && files && options && neighbors && hpath &&
             SUCCEEDED(options->lpVtbl->QueryInterface(options, &P__IID_ILauncherOptions2, (void **)&options2)) &&
             SUCCEEDED(options2->lpVtbl->put_NeighboringFilesQuery(options2, neighbors)) &&
             SUCCEEDED(files->lpVtbl->GetFileFromPathAsync(files, hpath, &opening)) && P__WinRtWait(opening) == P__Wait_Completed &&
             SUCCEEDED(opening->lpVtbl->GetResults(opening, &file)) && file;
    // Desktop callers own the window that any launcher prompt belongs to.
    if (ok && P__shellOwner && SUCCEEDED(options->lpVtbl->QueryInterface(options, &P__IID_IInitializeWithWindow, (void **)&window))) {
        window->lpVtbl->Initialize(window, P__shellOwner);
        window->lpVtbl->Release(window);
    }
    b32 result = false;
    if (ok && SUCCEEDED(launcher->lpVtbl->LaunchFileWithOptionsAsync(launcher, file, options, &launching))) {
        P__Wait wait = P__WinRtWait(launching);
        boolean launched = false;
        // A launch still pending after the wait may yet open the app, so it must not be repeated.
        result = wait == P__Wait_TimedOut || (wait == P__Wait_Completed && SUCCEEDED(launching->lpVtbl->GetResults(launching, &launched)) && launched);
    }
    WindowsDeleteString(hpath);
    if (launching) launching->lpVtbl->Release(launching);
    if (file) file->lpVtbl->Release(file);
    if (opening) opening->lpVtbl->Release(opening);
    if (neighbors) neighbors->lpVtbl->Release(neighbors);
    if (options2) options2->lpVtbl->Release(options2);
    if (options) options->lpVtbl->Release(options);
    if (files) files->lpVtbl->Release(files);
    if (launcher) launcher->lpVtbl->Release(launcher);
    return result;
}

// Explorer itself, as folders may be set to open in another file manager.
function P_FileError P_OpenFileExplorer(Arena *arena, String path)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *folder = path.length ? P__FilePathWide(scratch.arena, path) : NULL;
    HINSTANCE launched = ShellExecuteW(NULL, L"open", L"explorer.exe", folder, NULL, SW_SHOWNORMAL);
    ReleaseScratch(scratch);
    return (INT_PTR)launched > 32 ? (P_FileError){0} : P__FileError(arena, GetLastError());
}

function P_FileError P_OpenPath(Arena *arena, String path, P_FileOrder order)
{
    Scratch scratch = GetScratchEx(arena);
    String absolute = P_AbsolutePath(scratch.arena, path);
    wchar *wide = absolute.length ? P__FilePathWide(scratch.arena, absolute) : NULL;
    u32 error = wide ? 0 : GetLastError();
    if (wide) {
        HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        wchar appId[256] = {0};
        b32 packaged = P__PackagedHandler(wide, appId, CArrayCount(appId));
        b32 launched = false;
        // ponytail: Photos orders the folder itself; pass Filey's order once its link takes one.
        if (packaged && wcsncmp(appId, L"Microsoft.Windows.Photos_", 25) == 0) {
            wchar *link = P__PhotosLink(scratch.arena, absolute);
            SHELLEXECUTEINFOW info = {.cbSize = sizeof(info), .fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI, .lpVerb = L"open", .lpFile = link, .nShow = SW_SHOWNORMAL};
            launched = link && ShellExecuteExW(&info);
        }
        if (!launched && packaged) launched = P__LaunchWithNeighbors(wide, order);
        if (!launched) {
            SHELLEXECUTEINFOW info = {.cbSize = sizeof(info), .fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI, .lpVerb = L"open", .lpFile = wide, .nShow = SW_SHOWNORMAL};
            if (!ShellExecuteExW(&info)) error = GetLastError();
        }
        if (SUCCEEDED(com)) CoUninitialize();
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P_OpenConsole(Arena *arena, String directory)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *folder = P__FilePathWide(scratch.arena, directory);
    wchar line[] = L"cmd.exe";
    STARTUPINFOW startup = { .cb = sizeof(startup) };
    PROCESS_INFORMATION process = {0};
    P_FileError result = {0};
    // Its own console, which Windows opens in the terminal the user set as the default.
    if (folder && CreateProcessW(NULL, line, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, folder, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        result = P__FileError(arena, folder ? GetLastError() : ERROR_INVALID_NAME);
    }
    ReleaseScratch(scratch);
    return result;
}

function P_FileError P_OpenWindow(Arena *arena, String path)
{
    Scratch scratch = GetScratchEx(arena);
    wchar executable[1024];
    u32 length = GetModuleFileNameW(NULL, executable, (u32)CArrayCount(executable));
    wchar *folder = P__FilePathWide(scratch.arena, path);
    P_FileError result = {0};
    if (!length || length >= CArrayCount(executable) || !folder) {
        result = P__FileError(arena, GetLastError());
    } else {
        // The command line is the quoted program, then the quoted folder, as the program's own startup parses it.
        u64 folderLength = wcslen(folder);
        wchar *line = ArenaPushArray(scratch.arena, length + folderLength + 8, wchar);
        swprintf(line, length + folderLength + 8, L"\"%ls\" \"%ls\"", executable, folder);
        STARTUPINFOW startup = { .cb = sizeof(startup) };
        PROCESS_INFORMATION process = {0};
        if (CreateProcessW(executable, line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        } else {
            result = P__FileError(arena, GetLastError());
        }
    }
    ReleaseScratch(scratch);
    return result;
}
