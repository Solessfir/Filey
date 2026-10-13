#pragma once

#include "../Platform/Files.h"

// Matches P_FileOrderKey, which carries it to the platform.
typedef enum Prog_Sort {
    Prog_Sort_Name,
    Prog_Sort_Type,
    Prog_Sort_Modified,
    Prog_Sort_Size,
    Prog_Sort_Count,
} Prog_Sort;

static_assert((i32)Prog_Sort_Size == (i32)P_FileOrderKey_Size && (i32)Prog_Sort_Count == (i32)P_FileOrderKey_Count);

typedef enum Prog_ColumnId {
    Prog_ColumnId_Name,
    Prog_ColumnId_Type,
    Prog_ColumnId_Modified,
    Prog_ColumnId_Folders,
    Prog_ColumnId_Files,
    Prog_ColumnId_Size,
    Prog_ColumnId_Count,
} Prog_ColumnId;

// Prog_CommandKind is generated from Program/Commands.txt.
#include "Meta/Commands.meta.h"

typedef struct Prog_CommandDesc {
    String name;
    String label;    // empty for internal commands that never reach a palette or menu
    String aliases;  // comma-separated search words
    String tooltip;
    b32 noRepeat;    // ignore auto-repeat key events
} Prog_CommandDesc;

typedef struct Prog_Hotkey {
    Key key;
    KeyMods mods;  // matched exactly
    enum8(Prog_CommandKind) command;
} Prog_Hotkey;

typedef struct Prog_Command Prog_Command;
struct Prog_Command {
    Prog_Command *next;
    enum8(Prog_CommandKind) kind;
    u64 row;
    u32 pressCount;
    u64 inputOrder;
    KeyMods mods;
};

typedef struct Prog_Entry {
    P_File file;
    String type;
    String modified;
    b32 selected;
    // Recursive totals of a folder, filled in by measure jobs.
    b32 measured;
    u64 folders;
    u64 files;
    u64 bytes;
} Prog_Entry;

typedef enum Prog_JobKind {
    Prog_JobKind_Directory,
    Prog_JobKind_Open,
    Prog_JobKind_Icons,
    Prog_JobKind_Rename,
    Prog_JobKind_WarmMenu,
    Prog_JobKind_Measure,
    Prog_JobKind_Count,
} Prog_JobKind;

typedef struct Prog_IconRequest {
    String path;
    String key;
    enum8(P_FileKind) kind;
} Prog_IconRequest;

typedef struct Prog_ArrayIconRequest { ArrayHeader header; Prog_IconRequest *v; } Prog_ArrayIconRequest;

typedef enum Prog_IconState {
    Prog_IconState_Pending,
    Prog_IconState_Loaded,
    Prog_IconState_Failed,
    Prog_IconState_Count,
} Prog_IconState;

typedef struct Prog_Icon {
    enum8(Prog_IconState) state;
    R_TextureHandle texture;
    R_Rect uv;
} Prog_Icon;

typedef struct Prog_JobInput {
    enum8(Prog_JobKind) kind;
    String path;
    u64 generation;
    enum8(Prog_Sort) sort;
    b32 descending;
    b32 showHidden;
    b32 addHistory;
    u64 historyAt;
    // Rename `name` to `newName` inside `path`; WarmMenu uses `path` and `name`.
    String name;
    String newName;
    // Measure the folders `names` inside `path`: the entries `rows` of listing `listing`.
    String *names;
    u64 *rows;
    u64 rowCount;
    u64 listing;
    // Icons
    Prog_IconRequest *icons;
    u64 iconCount;
    i32 iconSize;
} Prog_JobInput;

typedef struct Prog_Measured {
    P_DirectoryTotals *totals;  // for the first `count` rows of the job
    u64 count;
} Prog_Measured;

typedef struct Prog_Directory {
    String path;
    Prog_Entry *entries;
    u64 count;
    u64 folderCount;
    P_FileError error;
    b32 cancelled;
    enum8(Prog_Sort) sort;
    b32 descending;
    b32 showHidden;
} Prog_Directory;

typedef struct Prog_State {
    Arena frameArena;
    Arena jobsArena;
    Arena directoryArena;
    Arena historyArena;
    Arena errorArena;
    UI_Context ui;
    P_JobQueue jobs;
    P_Job *directoryJob;
    Prog_Directory directory;
    ArrayStr history;
    u64 historyAt;
    u64 generation;
    u64 focusedRow;
    u64 anchorRow;
    u64 selectedCount;
    b32 hasFocus;
    b32 hasAnchor;
    b32 loading;
    b32 showHidden;
    enum8(Prog_Sort) sort;
    b32 descending;
    String error;
    Prog_Command *commandFirst;
    Prog_Command *commandLast;
    b32 revealFocus;
    b32 resetScroll;
    u64 pageRows;  // rows visible in the last layout, for Page Up/Down
    // File icons, keyed by P_FileIconKey. The map and keys live in iconArena;
    // atlas pages survive resets and are reused.
    Arena iconArena;
    struct { MapHeader header; struct { String key; Prog_Icon value; } *v; } icons;
    Prog_ArrayIconRequest iconRequests;  // queued while building UI, submitted by Prog_ProcessCommands
    i32 iconSize;
    u32 iconCell;
    u32 iconPageCount;
    // ponytail: fixed page count (8192 icons at 16 px); later icons fall back to glyphs.
    R_TextureHandle iconPages[8];
    // Context menu. Items and rows live in menuArena until the menu closes.
    Arena menuArena;
    P_ContextMenu menu;
    b32 menuOpen;
    UI_Vec2 menuAt;
    u64 *menuRows;            // directory entries the menu acts on
    u64 menuRowCount;
    u32 menuPath[8];          // the open submenu item at each level
    u32 menuDepth;            // open submenu levels below the top one
    u32 menuHighlight;        // item index, or UINT32_MAX for none
    UI_Key menuHot;           // hot block when the menu was last built
    R_Rect focusedRowRect;    // last layout, to place a keyboard-opened menu
    // Drag and drop. The system's drag loop is modal, so a drag is requested
    // during a frame and run between frames by Prog_DragSelection.
    b32 dragRequested;
    b32 dragging;
    u64 hoverRow;             // row under the pointer in the last build
    b32 hasHoverRow;
    // Inline rename of the entry named renameFrom. Offsets are bytes into
    // renameText; names never reach the buffer sizes on any file system.
    b32 renaming;
    u8 renameFrom[1024];
    u32 renameFromLength;
    u8 renameText[1024];
    u32 renameLength;
    u32 renameCursor;
    u32 renameAnchor;
    R_Rect renameRect;        // last layout of the edit box
    u64 renameCreatedUntil;   // until then, rename the one item a listing of this folder adds
    u8 selectText[1024];      // select this name in the next listing
    u32 selectLength;
    // Folder totals. Each adopted listing gets a new id; results for an older
    // listing of the same folder are matched by name.
    u64 listing;
    P_Job *measureJob;
    // The shown folder's watch. Changes reload it, at most every Prog_WatchInterval.
    P_DirectoryWatch watch;
    b32 watchDirty;
    u64 watchReloadTime;
    // Set by the app so that the first listing loads shell extensions in the
    // background; the first context menu would otherwise stall on them.
    b32 warmMenus;
} Prog_State;

function b32 Prog_Init(Prog_State *state, String path, P_JobWakeCallback *wake, void *user);
function void Prog_Release(Prog_State *state);
function void Prog_RequestDirectory(Prog_State *state, String path, b32 addHistory, u64 historyAt);
function b32 Prog_TakeCompletions(Prog_State *state);
function String Prog_ParentPath(String path);
function String Prog_JoinPath(Arena *arena, String path, String name);
function String Prog_FormatSize(Arena *arena, u64 bytes);
function i32 Prog_CompareNames(String a, String b);
function void Prog_SelectRow(Prog_State *state, u64 row, KeyMods mods);
function void Prog_OpenRow(Prog_State *state, u64 row);
function Prog_Command *Prog_QueueCommand(Prog_State *state, Prog_CommandKind kind, u64 row, u32 pressCount, u64 inputOrder);
function void Prog_HandleInput(Prog_State *state, PlatformEventList events);
function b32 Prog_ProcessCommands(Prog_State *state);
function void Prog_BuildUI(Prog_State *state, const R_Font *font, const R_Font *icons, f32 scale);
function f32 Prog_RowHeight(const R_Font *font);
function const Prog_Icon *Prog_FileIcon(Prog_State *state, String directory, const Prog_Entry *entry);
function void Prog_SubmitIconRequests(Prog_State *state);
function void Prog_CloseMenu(Prog_State *state);
function void Prog_DragSelection(Prog_State *state);
function void Prog_Reload(Prog_State *state);
function void Prog_BeginRename(Prog_State *state, u64 row);
function String Prog_RenameText(const Prog_State *state);
function u32 *Prog_ResizeIcon(Arena *arena, P_Image source, i32 size);
