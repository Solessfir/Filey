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
    enum8(P_FileKind) kind;
    b8 hidden;
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

typedef struct P_DirectoryTotals {
    u64 folders;
    u64 files;
    u64 bytes;
    b32 complete;  // false when cancelled or when `path` itself could not be read
} P_DirectoryTotals;

// Totals of everything below `path`, hidden items included.
// Links and junctions count as items but are not followed; unreadable subfolders are skipped.
// Big trees take a while, so run it in a job.
function P_DirectoryTotals P_MeasureDirectory(String path, const P_CancelToken *cancel);
// Lexically normalized absolute path with '/' separators; empty means failure.
function String P_AbsolutePath(Arena *arena, String path);
// The paths the app was started with as arguments, made absolute.
function ArrayStr P_StartupPaths(Arena *arena);
function String P_FormatFileTime(Arena *arena, i64 modifiedTimeUnix);
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
// Copies or moves the items at `paths` into `directory`.
// Existing items are never replaced: a taken name gets a number on Linux and the shell's "- Copy" name on Windows.
// A folder is never put inside itself, and moving an item into the folder it is in does nothing.
// Linux moves across file systems by copying and removes the source only once its copy is complete; Windows shows its own progress window for long operations.
function P_FileError P_TransferItems(Arena *arena, const String *paths, u64 count, String directory, b32 move, const P_CancelToken *cancel);
// Moves the items at `paths` to the Trash, or the Recycle Bin on Windows, which asks before deleting anything it cannot keep.
function P_FileError P_TrashItems(Arena *arena, const String *paths, u64 count);
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
} P_Place;

typedef struct P_ArrayPlace { ArrayHeader header; P_Place *v; } P_ArrayPlace;

// The user's home and its desktop, documents, downloads, music, pictures and videos folders that exist.
function P_ArrayPlace P_UserPlaces(Arena *arena);
// Mounted drives with their sizes; network drives may be slow to answer, so run it in a job.
function P_ArrayPlace P_Drives(Arena *arena);
// The folder Filey keeps its settings in, created when missing; empty when there is none.
// FILEY_CONFIG_DIR, when set, names the folder instead, for portable or test setups.
function String P_ConfigDirectory(Arena *arena);
// The whole file; false when it cannot be read.
function b32 P_ReadFile(Arena *arena, String path, String *contents);
// Replaces the file with `contents` in one step, so that a crash leaves the old or the new file and never half of one.
function P_FileError P_WriteFile(Arena *arena, String path, String contents);

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
function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, P_Image *image);
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
    P_MenuAction_Count,
} P_MenuAction;

typedef struct P_MenuItem {
    String label;
    String verb;       // canonical verb such as "open" or "delete" when the system has one
    u32 id;            // passed back to P_ContextMenuInvoke; 0 for separators
    u32 firstChild;    // index into P_ContextMenu.items
    u32 childCount;    // nonzero for submenus
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
