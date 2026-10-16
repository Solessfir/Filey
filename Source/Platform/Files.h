#pragma once

#include "Jobs.h"

typedef enum P_FileKind {
    P_FileKind_Directory,
    P_FileKind_File,
    P_FileKind_Other,
    P_FileKind_Drive,  // a drive's root or mount point, for its icon; listings never return it
    P_FileKind_Count,
} P_FileKind;

typedef struct P_File {
    String name;
    u64 size;
    i64 modifiedTimeUnix; // Seconds since the Unix epoch.
    i64 createdTimeUnix;  // 0 when the file system does not say
    i64 accessedTimeUnix;
    enum8(P_FileKind) kind;
    b8 hidden;          // marked hidden, or named with a dot first
    b8 system;          // marked as the system's own, on Windows
    b8 metadataValid;
    b8 symbolicLink;
} P_File;

typedef struct P_FileArray {
    ArrayHeader header;
    P_File *v;
} P_FileArray;

typedef struct P_DirectoryResult {
    P_FileArray files;
    u32 errorCode;
    String errorString;
    b32 cancelled;
} P_DirectoryResult;

typedef struct P_FileError {
    u32 code;
    String message;
} P_FileError;

// Results belong to arena. Cancellation and directory errors discard partial listings.
function P_DirectoryResult P_EnumerateDirectory(Arena *arena, String path, const P_CancelToken *cancel);
// Up to `limit` items inside an archive (zip, 7z, rar, tar and the like), each named by its path inside it with '/' between folders, folders
// as P_FileKind_Directory; false, with none, when the system cannot read it as an archive or finds nothing in it. Slow; run it in a job.
function b32 P_ListArchive(Arena *arena, String path, u64 limit, const P_CancelToken *cancel, P_FileArray *files);

typedef struct P_DirectoryTotals {
    u64 folders;
    u64 files;
    u64 bytes;
    b32 complete;  // false when cancelled or when `path` itself could not be read
} P_DirectoryTotals;

// Adds the items directly inside `path`, hidden ones included, to `totals`: its folders, its files and their bytes.
// Appends to `subfolders`, in `arena`, the paths of the folders a walk goes on into, which leaves out links and junctions.
// False when `path` cannot be read. Touches the disk, so run it in a job.
function b32 P_CountFolder(Arena *arena, String path, P_DirectoryTotals *totals, ArrayStr *subfolders);
// Whether `path` is a folder the system makes up rather than stores, such as Linux's /proc, which a walk of the whole drive leaves out.
function b32 P_IsVirtualFolder(String path);
// What P_LoadFileIcon takes for the icon of the computer, which lists the drives; empty where there is none to load.
function String P_ComputerIconPath(void);
// When `path` was created, in seconds since the Unix epoch; 0 when the file system does not say.
function i64 P_FileCreatedTime(String path);
// Lexically normalized absolute path with '/' separators; empty means failure.
function String P_AbsolutePath(Arena *arena, String path);
// The paths the app was started with as arguments, made absolute.
function ArrayStr P_StartupPaths(Arena *arena);
function String P_FormatFileTime(Arena *arena, i64 modifiedTimeUnix);
// The local date of `timeUnix` as "2026-10-16", which a file name can hold; empty when it is out of range.
function String P_FormatFileDate(Arena *arena, i64 timeUnix);
// Now, in seconds since the Unix epoch.
function i64 P_NowUnix(void);
// The order a folder is shown in, so that an app opened on one of its files can step through the rest in the same order, as from Explorer.
typedef enum P_FileOrderKey {
    P_FileOrderKey_Name,
    P_FileOrderKey_Type,
    P_FileOrderKey_Modified,
    P_FileOrderKey_Size,
    P_FileOrderKey_Count,
} P_FileOrderKey;

typedef struct P_FileOrder {
    enum8(P_FileOrderKey) key;
    b8 descending;
} P_FileOrder;

// Opens `path` with its default application.
// Windows hands packaged apps such as Photos the rest of the folder in `order`.
// May block while the application starts.
// Run in a job.
function P_FileError P_OpenPath(Arena *arena, String path, P_FileOrder order);
// Starts another Filey window, a process of its own, showing the folder `path`.
function P_FileError P_OpenWindow(Arena *arena, String path);
// Reports changes to the items of one folder, not of its subfolders, from a thread of its own.
// {0} is a stopped watch.
// `wake` runs on that thread when a change follows a call to P_DirectoryWatchTakeChanged.
typedef struct P_DirectoryWatch {
    Arena arena;
    void *native;
} P_DirectoryWatch;

function b32 P_DirectoryWatchStart(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user);
// True once for any number of changes since the previous call.
function b32 P_DirectoryWatchTakeChanged(P_DirectoryWatch *watch);
function void P_DirectoryWatchStop(P_DirectoryWatch *watch);

// Renames `from` to `to` inside `directory`.
// Never replaces an existing item, and refuses names that are empty, "." or "..", or contain a path separator.
// May touch a slow disk, so run it in a job.
function P_FileError P_RenamePath(Arena *arena, String directory, String from, String to);

// File operations Filey carries out itself.
// Each may touch slow disks or many files, so run them in jobs.
//
// Creates an empty folder or file called `name` inside `directory`, numbering the name when it is taken ("New folder (2)"), and returns the name used in `created`.
function P_FileError P_CreateItem(Arena *arena, String directory, String name, b32 folder, String *created);
// What a copy or move does with a name already taken in the target folder.
typedef enum P_Conflict {
    P_Conflict_Ask,       // waits for the program's answer through P_Progress
    P_Conflict_KeepBoth,  // numbers the new item's name
    P_Conflict_Replace,   // the new file takes the name once it is complete; a folder is never replaced, so both are kept
    P_Conflict_Skip,
    P_Conflict_Count,
} P_Conflict;

// A long file operation's progress, which its job writes and the main thread reads, and the questions the main thread answers.
// While `asking` is set the job waits on the main thread, which stores a P_Conflict in `answer` (and in `policy` too to answer for the rest) and then clears `asking`.
typedef struct P_Progress {
    atomic_uint_fast64_t totalBytes, doneBytes, totalItems, doneItems;
    atomic_bool asking;
    atomic_uint answer;
    atomic_uint policy;       // a P_Conflict, set before the operation starts
    b32 conflictFolder;       // what is asked about: the existing item `conflictName` in the target folder
    char conflictName[256];
} P_Progress;

// Copies or moves the items at `paths` into `directory`.
// `progress`, when not NULL, is kept current and says what taken names do; without it a taken name gets a number.
// `placed`, when not NULL, receives in `arena` the path each item ended up at, in the order of `paths`, empty for one that was skipped.
// Windows asks with its own conflict and progress windows instead, and the shell's "- Copy" names replace numbers.
// A folder is never put inside itself, and moving an item into the folder it is in does nothing.
// Linux moves across file systems by copying and removes the source only once its copy is complete.
function P_FileError P_TransferItems(Arena *arena, const String *paths, u64 count, String directory, b32 move, P_Progress *progress, ArrayStr *placed, const P_CancelToken *cancel);
// Moves the items at `paths` to the Trash, or the Recycle Bin on Windows, which asks before deleting anything it cannot keep.
function P_FileError P_TrashItems(Arena *arena, const String *paths, u64 count);

// An item in the Recycle Bin or the Trash: its file where the bin keeps it, `file.name` that whole path, with its own name, where it came from
// and when it was deleted.
typedef struct P_Trashed {
    P_File file;
    String name;
    String original;  // the folder it was in
    i64 deletedTimeUnix;
} P_Trashed;

typedef struct P_ArrayTrashed { ArrayHeader header; P_Trashed *v; } P_ArrayTrashed;

// The kind of drive a folder is on, for work that is cheap on some and slow on others; a local drive that cannot be told apart counts as a solid-state one.
typedef enum P_DriveKind {
    P_DriveKind_SolidState,
    P_DriveKind_Spinning,
    P_DriveKind_Network,
    P_DriveKind_Count,
} P_DriveKind;

// The kind of drive `path` is on; it asks the system each time, so it belongs in background jobs.
function P_DriveKind P_PathDriveKind(String path);

// Whether the system asks apps for dark colors: Windows' app mode, or on Linux the GTK theme; dark without a word either way.
function b32 P_PrefersDarkColors(void);

// The folder the user's deleted items are listed in: the system drive's $Recycle.Bin, or the home Trash; P_UserPlaces lists it too.
function String P_TrashFolder(Arena *arena);
// True when `path` is a bin P_ListTrash reads: a drive's $Recycle.Bin, or the home Trash.
function b32 P_IsTrashFolder(String path);
// Whether the Recycle Bin, the system drive's on Windows, or the Trash holds anything; it reads the bin, so it is asked on a click, not every frame.
function b32 P_TrashHasItems(void);
// Deletes everything in the Recycle Bin of every drive, or in the Trash, for good. With `confirm`, Windows asks first with its own
// confirmation, which P_EmptyTrashAsks says; elsewhere it never asks, and the program confirms first.
function P_FileError P_EmptyTrash(Arena *arena, b32 confirm);
function b32 P_EmptyTrashAsks(void);
// What the bin at `path` holds; on Windows, the items of the user's own folders in it.
function P_ArrayTrashed P_ListTrash(Arena *arena, String path, const P_CancelToken *cancel);
// Deletes the items at `paths` for good, folders with everything in them, without asking; the program confirms first.
// Links are removed, never followed.
function P_FileError P_DeleteItems(Arena *arena, const String *paths, u64 count, const P_CancelToken *cancel);
// `path` with the system's own separators, for showing or copying.
function String P_NativePath(Arena *arena, String path);

// Sidebar locations.
typedef struct P_Place {
    String name;
    String path;
    u64 totalBytes;  // drives only; 0 when the drive has no medium or does not say
    u64 freeBytes;
    String type;        // drives only: what kind of drive it is, as "Local Disk"
    String fileSystem;  // drives only: as "NTFS" or "ext4"; empty without a medium
    b8 busy;            // drives only: did not answer in time, such as one spinning up a disc, so its details come with a later listing
} P_Place;

typedef struct P_ArrayPlace { ArrayHeader header; P_Place *v; } P_ArrayPlace;

// The user's home and its desktop, documents, downloads, music, pictures and videos folders that exist.
function P_ArrayPlace P_UserPlaces(Arena *arena);
// Folders that the breadcrumbs of the folders inside them start from: the user's places, and on Windows the local and roaming app data.
function ArrayStr P_PathRoots(Arena *arena);
// Mounted drives with their sizes; network drives may be slow to answer, so run it in a job.
function P_ArrayPlace P_Drives(Arena *arena);
// The folder Filey keeps its settings in, created when missing; empty when there is none.
// FILEY_CONFIG_DIR, when set, names the folder instead, for portable or test setups.
function String P_ConfigDirectory(Arena *arena);
// The whole file; false when it cannot be read.
function b32 P_ReadFile(Arena *arena, String path, String *contents);
// Reads at most `limit` bytes from the start of the file at `path`.
function b32 P_ReadFileStart(Arena *arena, String path, u64 limit, String *contents);
// Runs `arguments[0]`, found on the PATH, with the rest of `arguments` in `directory`, without a window, and gives what it wrote to its standard
// output; false when it could not start, ran past `timeoutMilliseconds` (it is then stopped) or exited with an error.
function b32 P_RunCommand(Arena *arena, String directory, const String *arguments, u32 count, u32 timeoutMilliseconds, String *output);
// Up to `limit` bytes of `path` from `offset` on; fewer past its end.
function b32 P_ReadFileAt(Arena *arena, String path, u64 offset, u64 limit, String *contents);
// Replaces the file with `contents` in one step, so that a crash leaves the old or the new file and never half of one.
function P_FileError P_WriteFile(Arena *arena, String path, String contents);
// Adds `contents` to the end of the file at `path`, which `restart` empties first; the file is made when missing.
function P_FileError P_AppendFile(Arena *arena, String path, String contents, b32 restart);
// Moves the file at `from` over the one at `to` in one step.
function P_FileError P_ReplaceFile(Arena *arena, String from, String to);

// Straight-alpha RGBA8 pixels (red in the lowest byte), top row first.
typedef struct P_Image {
    u32 *pixels;
    i32 width;
    i32 height;
} P_Image;

// Key shared by files of one type: lowercase ".ext", or the folder and extensionless-file keys.
// Pure string work, safe while building UI.
function String P_FileTypeKey(Arena *arena, String path, P_FileKind kind);
// The type key, except that files with their own icon (executables, shortcuts) are keyed by path.
function String P_FileIconKey(Arena *arena, String path, P_FileKind kind);
// The system icon for `path`, at least `size` pixels when available.
// The image may be larger or smaller; scaling is left to the caller.
// May touch the disk, so run it in a job.
// `exact` asks for the icon of this very item, as for special folders such as the desktop, rather than the one of its type.
function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, b32 exact, P_Image *image);
// A large picture for icon views, fitting a size x size square: the system's thumbnail where it makes one (an image's contents, a folder's preview),
// else the icon unless `thumbnailOnly`, which then fails.
function b32 P_LoadFileThumbnail(Arena *arena, String path, P_FileKind kind, i32 size, b32 thumbnailOnly, P_Image *image);
// The picture in an image file at its own size, or scaled down to fit a `maxSide` square when larger.
// `sourceWidth` and `sourceHeight` get the size it has. False for files that are not pictures the system can read.
// Reads the whole file, so run it in a job.
function b32 P_LoadImageFile(Arena *arena, String path, i32 maxSide, P_Image *image, i32 *sourceWidth, i32 *sourceHeight);
// Thumbnails are keyed by path where the system makes them per file, and by type where it does not.
function String P_FileThumbnailKey(Arena *arena, String path, P_FileKind kind);
// Display name of a file's type ("Text Document", "File folder") from the shell on Windows and shared-mime-info on Linux.
// Only the name is used, never the file.
// Slow enough to run in a job and cache per P_FileTypeKey.
function String P_FileTypeName(Arena *arena, String name, P_FileKind kind);

// Context menus.
// Windows reads the shell's own menu, extensions included; Linux offers the actions Filey implements itself.
// Those items carry an action, which the program carries out instead of P_ContextMenuInvoke.
typedef enum P_MenuAction {
    P_MenuAction_None,
    P_MenuAction_Cut,
    P_MenuAction_Copy,
    P_MenuAction_Paste,
    P_MenuAction_Duplicate,
    P_MenuAction_CopyAsPath,
    P_MenuAction_Trash,
    P_MenuAction_Delete,
    P_MenuAction_NewFolder,
    P_MenuAction_NewFile,
    P_MenuAction_EmptyTrash,
    P_MenuAction_Count,
} P_MenuAction;

typedef struct P_MenuItem {
    String label;
    String verb;       // canonical verb such as "open" or "delete", in lowercase, when the system has one
    u32 id;            // passed back to P_ContextMenuInvoke; 0 for separators
    u32 firstChild;    // index into P_ContextMenu.items
    u32 childCount;    // nonzero for submenus
    P_Image icon;      // the item's own picture at its native size, in the menu's arena; empty when it has none
    b8 separator;
    b8 disabled;
    b8 isDefault;
    enum8(P_MenuAction) action;
} P_MenuItem;

typedef struct P_ContextMenu {
    P_MenuItem *items;  // the top level is [0, rootCount); each submenu's children are contiguous
    u32 count;
    u32 rootCount;
    void *native;       // platform state that keeps the menu alive until P_ContextMenuClose
} P_ContextMenu;

// Menu for `names` inside `directory`, or for the directory background when `count` is 0.
// With an empty `directory`, `names` are full paths that share a folder, such as drives.
// Main thread only: shell menus belong to the thread that built them, and their verbs may need its message loop (clipboard, dialogs).
// Loading shell extensions can take a moment.
function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu);
// Builds and drops the menus of `directory` and of `name` inside it, on any thread, so that shell extensions are loaded before the first real menu.
function void P_ContextMenuWarm(String directory, String name);
function P_FileError P_ContextMenuInvoke(Arena *arena, P_ContextMenu *menu, u32 id);
function void P_ContextMenuClose(P_ContextMenu *menu);

// Drag and drop between Filey and other programs, through the system's own drag protocol.
// Main thread only.
//
// Starts a drag of `names` inside `directory` and returns once they are dropped (true) or the drag is cancelled.
// The system's drag loop runs inside this call and renders frames through the modal frame callback while the pointer is over the window, so call it outside a frame.
function b32 P_DragFiles(String directory, const String *names, u64 count);
// The folder that files dropped on the window go into, updated as the pointer moves; empty refuses drops.
function void P_SetDropDirectory(String path);
// While on, drops are not transferred: the folders among the dropped items are kept for P_TakeDroppedFolders, as a place to pin them is under the pointer.
function void P_SetDropTakesFolders(b32 takes);
// The folders dropped since the last call while P_SetDropTakesFolders was on, copied into `arena`.
function ArrayStr P_TakeDroppedFolders(Arena *arena);
// A drag, Filey's own or another program's, is over the window.
function b32 P_DropPending(void);
