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
#include <windows.data.pdf.h>
#include <windows.storage.streams.h>
#include <shcore.h>
#include <dbghelp.h>
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

global const IID P__IID_IPdfDocumentStatics   = { 0x433A0B5F, 0xC007, 0x4788, { 0x90, 0xF2, 0x08, 0x14, 0x3D, 0x92, 0x25, 0x99 } };
global const IID P__IID_IPdfPageRenderOptions = { 0x3C98056F, 0xB7CF, 0x4C29, { 0x9A, 0x04, 0x52, 0xD9, 0x02, 0x67, 0xF4, 0x25 } };
global const IID P__IID_IRandomAccessStream   = { 0x905A0FE1, 0xBC53, 0x11DF, { 0x8C, 0x49, 0x00, 0x1E, 0x4F, 0xC6, 0x86, 0xDA } };
global const IID P__IID_IStream               = { 0x0000000C, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

// The PDF at `path`, opened by the system's own PDF renderer, which draws pages for the shell's preview pane but gives the thumbnail cache
// nothing; NULL when it cannot be read, as when it has a password.
function __x_ABI_CWindows_CData_CPdf_CIPdfDocument *P__PdfOpen(const wchar *path)
{
    __x_ABI_CWindows_CStorage_CIStorageFileStatics *files = P__WinRtFactory(RuntimeClass_Windows_Storage_StorageFile, &P__IID_IStorageFileStatics);
    __x_ABI_CWindows_CData_CPdf_CIPdfDocumentStatics *documents = P__WinRtFactory(RuntimeClass_Windows_Data_Pdf_PdfDocument, &P__IID_IPdfDocumentStatics);
    __FIAsyncOperation_1_Windows__CStorage__CStorageFile *opening = NULL;
    __x_ABI_CWindows_CStorage_CIStorageFile *file = NULL;
    __FIAsyncOperation_1_Windows__CData__CPdf__CPdfDocument *loading = NULL;
    __x_ABI_CWindows_CData_CPdf_CIPdfDocument *result = NULL;
    HSTRING hpath = P__HString(path);
    b32 ok = files && documents && hpath &&
             SUCCEEDED(files->lpVtbl->GetFileFromPathAsync(files, hpath, &opening)) && P__WinRtWait(opening) == P__Wait_Completed &&
             SUCCEEDED(opening->lpVtbl->GetResults(opening, &file)) && file &&
             SUCCEEDED(documents->lpVtbl->LoadFromFileAsync(documents, file, &loading)) && P__WinRtWait(loading) == P__Wait_Completed;
    if (ok && FAILED(loading->lpVtbl->GetResults(loading, &result))) result = NULL;
    WindowsDeleteString(hpath);
    if (loading) loading->lpVtbl->Release(loading);
    if (file) file->lpVtbl->Release(file);
    if (opening) opening->lpVtbl->Release(opening);
    if (documents) documents->lpVtbl->Release(documents);
    if (files) files->lpVtbl->Release(files);
    return result;
}

// Page `index` of the PDF at `path` as a PNG in memory, `side` pixels wide, or with `sideIsWidth` false that long on its longer side.
function IStream *P__PdfPageStream(const wchar *path, u32 index, u32 side, b32 sideIsWidth)
{
    __x_ABI_CWindows_CData_CPdf_CIPdfDocument *document = P__PdfOpen(path);
    __x_ABI_CWindows_CData_CPdf_CIPdfPageRenderOptions *options = P__WinRtCreate(RuntimeClass_Windows_Data_Pdf_PdfPageRenderOptions, &P__IID_IPdfPageRenderOptions);
    __x_ABI_CWindows_CStorage_CStreams_CIRandomAccessStream *memory = P__WinRtCreate(RuntimeClass_Windows_Storage_Streams_InMemoryRandomAccessStream, &P__IID_IRandomAccessStream);
    __x_ABI_CWindows_CData_CPdf_CIPdfPage *page = NULL;
    __x_ABI_CWindows_CFoundation_CIAsyncAction *rendering = NULL;
    __x_ABI_CWindows_CFoundation_CSize size = {0};
    IStream *result = NULL;
    b32 ok = document && options && memory && SUCCEEDED(document->lpVtbl->GetPage(document, index, &page)) && page &&
             SUCCEEDED(page->lpVtbl->get_Size(page, &size)) && size.Width > 0 && size.Height > 0;
    if (ok) {
        f32 scale = (f32)side / (sideIsWidth ? size.Width : MaxF32(size.Width, size.Height));
        ok = SUCCEEDED(options->lpVtbl->put_DestinationWidth(options, (UINT32)MaxF32(1, size.Width * scale))) &&
             SUCCEEDED(options->lpVtbl->put_DestinationHeight(options, (UINT32)MaxF32(1, size.Height * scale))) &&
             SUCCEEDED(page->lpVtbl->RenderWithOptionsToStreamAsync(page, memory, options, &rendering)) && P__WinRtWait(rendering) == P__Wait_Completed &&
             SUCCEEDED(CreateStreamOverRandomAccessStream((IUnknown *)memory, &P__IID_IStream, (void **)&result));
    }
    if (result) result->lpVtbl->Seek(result, (LARGE_INTEGER){0}, STREAM_SEEK_SET, NULL);
    if (rendering) rendering->lpVtbl->Release(rendering);
    if (page) page->lpVtbl->Release(page);
    if (memory) memory->lpVtbl->Release(memory);
    if (options) options->lpVtbl->Release(options);
    if (document) document->lpVtbl->Release(document);
    return result;
}

function u32 P_PdfPages(Arena *arena, String path, u32 most, f32 **sizes)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *wide = P__FilePathWide(scratch.arena, path);
    __x_ABI_CWindows_CData_CPdf_CIPdfDocument *document = wide ? P__PdfOpen(wide) : NULL;
    UINT32 count = 0;
    if (document && FAILED(document->lpVtbl->get_PageCount(document, &count))) count = 0;
    count = Min(count, most);
    *sizes = ArenaPushArray(arena, Max(count, 1u) * 2, f32);
    for (u32 i = 0; i < count; ++i) {
        __x_ABI_CWindows_CData_CPdf_CIPdfPage *page = NULL;
        __x_ABI_CWindows_CFoundation_CSize size = {0};
        if (SUCCEEDED(document->lpVtbl->GetPage(document, i, &page)) && page && SUCCEEDED(page->lpVtbl->get_Size(page, &size))) {
            (*sizes)[2 * i] = size.Width, (*sizes)[2 * i + 1] = size.Height;
        }
        if (page) page->lpVtbl->Release(page);
        // A page that does not say takes the size of the one before it, or a letter page's.
        if ((*sizes)[2 * i] <= 0 || (*sizes)[2 * i + 1] <= 0) {
            (*sizes)[2 * i] = i ? (*sizes)[2 * i - 2] : 816, (*sizes)[2 * i + 1] = i ? (*sizes)[2 * i - 1] : 1056;
        }
    }
    if (document) document->lpVtbl->Release(document);
    ReleaseScratch(scratch);
    return count;
}

function b32 P_OpensInApp(String path)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FilePathWide(scratch.arena, path);
    const wchar *extension = wide ? PathFindExtensionW(wide) : L"";
    wchar program[MAX_PATH] = {0}, own[MAX_PATH] = {0};
    DWORD length = CArrayCount(program);
    b32 result = extension[0] && SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN | ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, extension, L"open", program, &length)) &&
                 lstrcmpiW(PathFindFileNameW(program), L"explorer.exe") != 0 && lstrcmpiW(PathFindFileNameW(program), L"rundll32.exe") != 0;
    if (result && GetModuleFileNameW(NULL, own, CArrayCount(own))) result = lstrcmpiW(program, own) != 0;
    ReleaseScratch(scratch);
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

function P_FileError P_OpenUrl(Arena *arena, String url)
{
    Scratch scratch = GetScratchEx(arena);
    i32 count = MultiByteToWideChar(CP_UTF8, 0, url.value, (i32)url.length, NULL, 0);
    wchar *wide = ArenaPushArray(scratch.arena, (u64)count + 1, wchar);
    MultiByteToWideChar(CP_UTF8, 0, url.value, (i32)url.length, wide, count);
    // Below 33 is a failure.
    u32 error = (INT_PTR)ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL) > 32 ? 0 : GetLastError();
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error ? error : ERROR_NO_ASSOCIATION) : (P_FileError){0};
}

// Where crash reports go and the version they are named after, ready before any crash, as the handler does as little as it can.
global wchar P__crashFolder[MAX_PATH];
global wchar P__crashVersion[32];
typedef BOOL (WINAPI *P__MiniDumpWriter)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
global P__MiniDumpWriter P__miniDumpWrite;

function LONG WINAPI P__OnCrash(EXCEPTION_POINTERS *exception)
{
    if (!P__miniDumpWrite || !P__crashFolder[0]) return EXCEPTION_CONTINUE_SEARCH;
    CreateDirectoryW(P__crashFolder, NULL);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar path[MAX_PATH + 64];
    swprintf(path, CArrayCount(path), L"%ls\\Filey-%ls-%04u%02u%02u-%02u%02u%02u.dmp", P__crashFolder, P__crashVersion, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        // The plain kind: the threads, their stacks and the modules, not the rest of memory.
        MINIDUMP_EXCEPTION_INFORMATION info = { .ThreadId = GetCurrentThreadId(), .ExceptionPointers = exception, .ClientPointers = FALSE };
        P__miniDumpWrite(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &info, NULL, NULL);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

function void P_CatchCrashes(String folder, String version)
{
    Scratch scratch = GetScratch();
    wchar *wide = P__FilePathWide(scratch.arena, folder);
    if (wide && lstrlenW(wide) < (i32)CArrayCount(P__crashFolder)) lstrcpynW(P__crashFolder, wide, CArrayCount(P__crashFolder));
    MultiByteToWideChar(CP_UTF8, 0, version.value, (i32)Min(version.length, (u64)CArrayCount(P__crashVersion) - 1), P__crashVersion, CArrayCount(P__crashVersion) - 1);
    ReleaseScratch(scratch);
    HMODULE help = LoadLibraryW(L"dbghelp.dll");
    P__miniDumpWrite = help ? (P__MiniDumpWriter)(void *)GetProcAddress(help, "MiniDumpWriteDump") : NULL;
    SetUnhandledExceptionFilter(P__OnCrash);
}

function P_FileError P_OpenPath(Arena *arena, String path, P_FileOrder order)
{
    // A device's file is opened by its item, which the shell copies off the device first, as Explorer's are.
    if (P__IsDevicePath(path)) {
        HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        PIDLIST_ABSOLUTE list = P__PathIdList(path);
        SHELLEXECUTEINFOW info = {.cbSize = sizeof(info), .fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI, .lpIDList = (void *)list, .nShow = SW_SHOWNORMAL};
        u32 error = !list ? ERROR_PATH_NOT_FOUND : ShellExecuteExW(&info) ? 0 : GetLastError();
        ILFree(list);
        if (SUCCEEDED(com)) CoUninitialize();
        return error ? P__FileError(arena, error) : (P_FileError){0};
    }
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

// `text` as UTF-16 as it is, its slashes kept, as an argument that is not a path.
function wchar *P__FileStringWideRaw(Arena *arena, String text)
{
    i32 length = text.length ? MultiByteToWideChar(CP_UTF8, 0, text.value, (i32)text.length, NULL, 0) : 0;
    wchar *result = ArenaPushArray(arena, (u64)length + 1, wchar);
    if (length) MultiByteToWideChar(CP_UTF8, 0, text.value, (i32)text.length, result, length);
    // A quote would end the argument early; names hold none.
    for (i32 i = 0; i < length; ++i) if (result[i] == L'"') result[i] = L'\'';
    return result;
}

function String P_ExecutablePath(Arena *arena)
{
    wchar executable[1024];
    u32 length = GetModuleFileNameW(NULL, executable, (u32)CArrayCount(executable));
    if (!length || length >= CArrayCount(executable)) return (String){0};
    Scratch scratch = GetScratchEx(arena);
    String result = P_AbsolutePath(arena, P__FileStringWide(scratch.arena, executable, (i32)length));
    ReleaseScratch(scratch);
    return result;
}

// An executable's path without its ".exe".
function String P__ExecutableStem(String executable)
{
    const char *end = executable.value + executable.length;
    b32 named = executable.length > 4 && end[-4] == '.' && (end[-3] | 32) == 'e' && (end[-2] | 32) == 'x' && (end[-1] | 32) == 'e';
    return named ? StrFromTo(executable, 0, (i64)executable.length - 4) : executable;
}

function void P_RemoveReplaced(String executable)
{
    Scratch scratch = GetScratch();
    String stem = P__ExecutableStem(executable);
    Index slash = StrIndexChar(stem, '/', SSF_SearchBackwards);
    String folder = slash.exists ? StrFromTo(stem, 0, (i64)slash.value) : (String){0};
    wchar *pattern = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, StrFormat(scratch.arena, "%S.old*.exe", stem)));
    WIN32_FIND_DATAW data = {0};
    HANDLE find = pattern && folder.length ? FindFirstFileW(pattern, &data) : INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            String name = P__FileStringWide(scratch.arena, data.cFileName, lstrlenW(data.cFileName));
            wchar *path = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, StrFormat(scratch.arena, "%S/%S", folder, name)));
            if (path) DeleteFileW(path);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    ReleaseScratch(scratch);
}

function P_FileError P_ReplaceExecutable(Arena *arena, String executable, String replacement)
{
    Scratch scratch = GetScratchEx(arena);
    String stem = P__ExecutableStem(executable);
    wchar *target = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, executable)), *source = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, replacement));
    wchar *aside = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, StrFormat(scratch.arena, "%S.old.exe", stem)));
    u32 error = target && source && aside ? ERROR_SUCCESS : ERROR_INVALID_NAME;
    // The running copy cannot be written over, but it can be moved aside, where earlier replacements' copies go once nothing runs them; while
    // an older one still runs from there, as another window of the version before, this one takes a name of its own.
    if (!error) {
        P_RemoveReplaced(executable);
        if (GetFileAttributesW(aside) != INVALID_FILE_ATTRIBUTES) {
            aside = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, StrFormat(scratch.arena, "%S.old-%u.exe", stem, (u32)GetCurrentProcessId())));
        }
        if (!aside) error = ERROR_INVALID_NAME;
        else if (!MoveFileExW(target, aside, MOVEFILE_REPLACE_EXISTING)) error = GetLastError();
    }
    if (!error && !MoveFileExW(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        error = GetLastError();
        MoveFileExW(aside, target, 0);
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P_Restart(Arena *arena, String executable)
{
    Scratch scratch = GetScratchEx(arena);
    wchar *program = P__FilePathWide(scratch.arena, P_NativePath(scratch.arena, executable));
    P_FileError result = {0};
    if (!program) {
        result = P__FileError(arena, ERROR_INVALID_NAME);
    } else {
        u64 room = wcslen(program) + 40;
        wchar *line = ArenaPushArray(scratch.arena, room, wchar);
        swprintf(line, room, L"\"%ls\" --after=%lu", program, GetCurrentProcessId());
        STARTUPINFOW startup = { .cb = sizeof(startup) };
        PROCESS_INFORMATION process = {0};
        if (CreateProcessW(program, line, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        } else {
            result = P__FileError(arena, GetLastError());
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function void P_WaitForExit(u32 process, u32 timeoutMilliseconds)
{
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, process);
    if (!handle) return;
    WaitForSingleObject(handle, timeoutMilliseconds);
    CloseHandle(handle);
}

function P_FileError P_OpenWindow(Arena *arena, String path, String option)
{
    Scratch scratch = GetScratchEx(arena);
    wchar executable[1024];
    u32 length = GetModuleFileNameW(NULL, executable, (u32)CArrayCount(executable));
    wchar *folder = P__FilePathWide(scratch.arena, path);
    P_FileError result = {0};
    if (!length || length >= CArrayCount(executable) || !folder) {
        result = P__FileError(arena, GetLastError());
    } else {
        // The command line is the quoted program, then the quoted folder, as the program's own startup parses it; the flag gives it a
        // window of its own rather than a tab in this one.
        u64 folderLength = wcslen(folder);
        wchar *extra = option.length ? P__FileStringWideRaw(scratch.arena, option) : NULL;
        u64 room = length + folderLength + (extra ? wcslen(extra) : 0) + 32;
        wchar *line = ArenaPushArray(scratch.arena, room, wchar);
        if (extra) swprintf(line, room, L"\"%ls\" --new-window \"%ls\" \"%ls\"", executable, folder, extra);
        else swprintf(line, room, L"\"%ls\" --new-window \"%ls\"", executable, folder);
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
