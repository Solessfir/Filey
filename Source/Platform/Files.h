#pragma once

#include "Jobs.h"

typedef enum P_FileKind {
    P_FileKind_Directory,
    P_FileKind_File,
    P_FileKind_Other,
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
// Lexically normalized absolute path with '/' separators; empty means failure.
function String P_AbsolutePath(Arena *arena, String path);
function String P_FormatFileTime(Arena *arena, i64 modifiedTimeUnix);
// The order a folder is shown in, so that an app opened on one of its files
// can step through the rest in the same order, as from Explorer.
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

// Opens `path` with its default application. Windows hands packaged apps such
// as Photos the rest of the folder in `order`. May block while the application
// starts. Run in a job.
function P_FileError P_OpenPath(Arena *arena, String path, P_FileOrder order);
// Reports changes to the items of one folder, not of its subfolders, from a
// thread of its own. {0} is a stopped watch. `wake` runs on that thread when
// a change follows a call to P_DirectoryWatchTakeChanged.
typedef struct P_DirectoryWatch {
    Arena arena;
    void *native;
} P_DirectoryWatch;

function b32 P_DirectoryWatchStart(P_DirectoryWatch *watch, String path, P_JobWakeCallback *wake, void *user);
// True once for any number of changes since the previous call.
function b32 P_DirectoryWatchTakeChanged(P_DirectoryWatch *watch);
function void P_DirectoryWatchStop(P_DirectoryWatch *watch);

// Renames `from` to `to` inside `directory`. Never replaces an existing item,
// and refuses names that are empty, "." or "..", or contain a path separator.
// May touch a slow disk, so run it in a job.
function P_FileError P_RenamePath(Arena *arena, String directory, String from, String to);

// Straight-alpha RGBA8 pixels (red in the lowest byte), top row first.
typedef struct P_Image {
    u32 *pixels;
    i32 width;
    i32 height;
} P_Image;

// Key shared by files of one type: lowercase ".ext", or the folder and
// extensionless-file keys. Pure string work, safe while building UI.
function String P_FileTypeKey(Arena *arena, String path, P_FileKind kind);
// The type key, except that files with their own icon (executables,
// shortcuts) are keyed by path.
function String P_FileIconKey(Arena *arena, String path, P_FileKind kind);
// The system icon for `path`, at least `size` pixels when available. The
// image may be larger or smaller; scaling is left to the caller. May touch
// the disk, so run it in a job.
function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, P_Image *image);
// Display name of a file's type ("Text Document", "File folder") from the shell
// on Windows and shared-mime-info on Linux. Only the name is used, never the
// file. Slow enough to run in a job and cache per P_FileTypeKey.
function String P_FileTypeName(Arena *arena, String name, P_FileKind kind);

// Context menus. Windows reads the shell's own menu, extensions included;
// Linux offers the actions Filey implements itself.
typedef struct P_MenuItem {
    String label;
    String verb;       // canonical verb such as "open" or "delete" when the system has one
    u32 id;            // passed back to P_ContextMenuInvoke; 0 for separators
    u32 firstChild;    // index into P_ContextMenu.items
    u32 childCount;    // nonzero for submenus
    b8 separator;
    b8 disabled;
    b8 isDefault;
} P_MenuItem;

typedef struct P_ContextMenu {
    P_MenuItem *items;  // the top level is [0, rootCount); each submenu's children are contiguous
    u32 count;
    u32 rootCount;
    void *native;       // platform state that keeps the menu alive until P_ContextMenuClose
} P_ContextMenu;

// Menu for `names` inside `directory`, or for the directory background when
// `count` is 0. Main thread only: shell menus belong to the thread that built
// them, and their verbs may need its message loop (clipboard, dialogs). Loading
// shell extensions can take a moment.
function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu);
// Builds and drops the menus of `directory` and of `name` inside it, on any
// thread, so that shell extensions are loaded before the first real menu.
function void P_ContextMenuWarm(String directory, String name);
function P_FileError P_ContextMenuInvoke(Arena *arena, P_ContextMenu *menu, u32 id);
function void P_ContextMenuClose(P_ContextMenu *menu);

// Drag and drop between Filey and other programs, through the system's own
// drag protocol. Main thread only.
//
// Starts a drag of `names` inside `directory` and returns once they are dropped
// (true) or the drag is cancelled. The system's drag loop runs inside this call
// and renders frames through the modal frame callback while the pointer is over
// the window, so call it outside a frame.
function b32 P_DragFiles(String directory, const String *names, u64 count);
// The folder that files dropped on the window go into, updated as the pointer
// moves; empty refuses drops.
function void P_SetDropDirectory(String path);
