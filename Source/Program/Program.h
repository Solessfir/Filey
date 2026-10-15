#pragma once

#include "../Platform/Files.h"

// The first values match P_FileOrderKey, which carries them to the platform.
// The platform does not know folder totals, so files open in name order under the rest.
typedef enum Prog_Sort {
    Prog_Sort_Name,
    Prog_Sort_Type,
    Prog_Sort_Modified,
    Prog_Sort_Size,
    Prog_Sort_Folders,
    Prog_Sort_Files,
    Prog_Sort_Created,   // these have no column
    Prog_Sort_Accessed,
    Prog_Sort_Items,     // folders and files inside, together
    Prog_Sort_Count,
} Prog_Sort;

static_assert((i32)Prog_Sort_Size == (i32)P_FileOrderKey_Size && (i32)Prog_Sort_Folders == (i32)P_FileOrderKey_Count);

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
    u64 panel;   // the panel whose UI queued it, which becomes the active one; 0 for the active one
    u64 row;
    u64 target;  // MoveTab: the slot the tab moves to
    u32 pressCount;
    u64 inputOrder;
    KeyMods mods;
    UI_Vec2 at;  // menus: where they open
    f32 atRight;  // menus: when nonzero, the right edge they line up with instead of at.x
};

// An item of Filey's own menus, which queues `kind` with `row`; Prog_CommandKind_None is a separator.
typedef struct Prog_MenuCommand {
    enum8(Prog_CommandKind) kind;
    u64 row;
    String label;  // the command's label when empty
    b32 checked;
    String icon;   // a glyph of the icon font before the label, or none
    b8 toggle;     // shows `checked` as a box at the row's end rather than a check before the label
} Prog_MenuCommand;

typedef struct Prog_Entry {
    P_File file;
    String type;
    b32 selected;
    b32 marqueeBase;  // selected when the current box selection began
    // Recursive totals of a folder, filled in by measure jobs.
    b32 measured;
    b32 remeasure;    // the totals are a past visit's, shown until a measure job brings fresh ones
    u64 folders;
    u64 files;
    u64 bytes;
    u32 parent;       // in a listing of the subfolders, the folder it is in, by its place in `parents`
} Prog_Entry;

typedef enum Prog_JobKind {
    Prog_JobKind_Directory,
    Prog_JobKind_Open,
    Prog_JobKind_Icons,
    Prog_JobKind_Rename,
    Prog_JobKind_WarmMenu,
    Prog_JobKind_Measure,
    Prog_JobKind_Inspect,
    Prog_JobKind_GoTo,
    Prog_JobKind_Index,
    Prog_JobKind_IndexLoad,
    Prog_JobKind_IndexFinish,
    Prog_JobKind_Transfer,
    Prog_JobKind_Trash,
    Prog_JobKind_Delete,
    Prog_JobKind_Create,
    Prog_JobKind_Places,
    Prog_JobKind_Save,
    Prog_JobKind_Preview,
    Prog_JobKind_Undo,
    Prog_JobKind_Count,
} Prog_JobKind;

// File operations that Undo reverses: a rename goes back, new items and copies go to the trash, and moved items return.
typedef enum Prog_UndoKind {
    Prog_UndoKind_Rename,
    Prog_UndoKind_Create,
    Prog_UndoKind_Copy,
    Prog_UndoKind_Move,
    Prog_UndoKind_Count,
} Prog_UndoKind;

// Items that were at `from` and are now at `to`; new items and copies have no `from`, and an empty `to` was skipped.
typedef struct Prog_Undo {
    enum8(Prog_UndoKind) kind;
    String *from;
    String *to;
    u64 count;
} Prog_Undo;

typedef struct Prog_ArrayUndo { ArrayHeader header; Prog_Undo *v; } Prog_ArrayUndo;

// ponytail: older steps are dropped past this, and their strings stay in undoArena until the stack empties.
#define Prog_MaxUndo 32

typedef struct Prog_IconRequest {
    String path;
    String key;
    enum8(P_FileKind) kind;
    i32 size;          // square size to load at; above the list icon size it is a thumbnail for the icon views
    b8 exact;          // the item's own icon rather than its type's
} Prog_IconRequest;

typedef struct Prog_ArrayIconRequest { ArrayHeader header; Prog_IconRequest *v; } Prog_ArrayIconRequest;

typedef enum Prog_IconState {
    Prog_IconState_Pending,
    Prog_IconState_Loaded,
    Prog_IconState_Failed,
    Prog_IconState_Count,
} Prog_IconState;

#define Prog_SpinnerFrames 9

// Square icons of one size packed into RGBA pages; pages survive resets and are reused.
typedef struct Prog_IconAtlas {
    R_TextureHandle pages[8];
    u32 pageLimit;
    u32 pageCount;
    u32 cell;
    i32 size;
    i32 pageSize;
} Prog_IconAtlas;

// Zoom levels, smallest first: Details, List, Columns, then icon grids that grow a step at a time.
#define Prog_ZoomLevels    17
#define Prog_ZoomFirstIcon 3

typedef enum Prog_View {
    Prog_View_Details,
    Prog_View_List,
    Prog_View_Columns,
    Prog_View_Icons,
    Prog_View_Count,
} Prog_View;

typedef struct Prog_Icon {
    enum8(Prog_IconState) state;
    R_TextureHandle texture;
    R_Rect uv;
} Prog_Icon;

typedef struct Prog_IconMap { MapHeader header; struct { String key; Prog_Icon value; } *v; } Prog_IconMap;

typedef struct Prog_JobInput {
    enum8(Prog_JobKind) kind;
    u64 panel;  // the panel that asked, whose results these are; 0 for work of the whole window
    String path;
    u64 generation;
    enum8(Prog_Sort) sort;
    b32 descending;
    b32 showHidden;
    b32 expanded;
    b32 addHistory;
    u64 historyAt;
    // Rename `name` to `newName` inside `path`, which is `prefix` below the shown folder; WarmMenu uses `path` and `name`.
    String name;
    String newName;
    String prefix;
    // Measure the folders `names` inside `path`: the entries `rows` of listing `listing`.
    String *names;
    u64 *rows;
    u64 rowCount;
    u64 listing;
    // Transfer copies or moves `paths` into `path`, Trash trashes them, Delete deletes them for good, and Create makes `name` inside `path`, a folder when `folder` is set.
    // Save writes `name` to the file `path`.
    String *paths;
    u64 pathCount;
    b32 move;
    b32 folder;
    P_Progress *progress;  // Transfer: its slot's, or NULL without one
    Prog_Undo undo;        // Undo: the step it reverses
    // Index lists `paths`, the index entries `entries`, keeps at most `room` of what it finds and adds that to the file `name` when there is one,
    // emptied first with `paths` as its roots when `restart` is set. IndexLoad reads the index file `name`; IndexFinish moves `path` over `name`.
    u32 *entries;
    u64 room;
    b32 restart;
    // Directory: what it has found so far, which the status bar shows while it walks a big tree; the totals count hidden items too.
    atomic_uint_fast64_t foundFolders, foundFiles, shownFolders, shownFiles;
    // Icons
    Prog_IconRequest *icons;
    u64 iconCount;
    i32 iconSize;
} Prog_JobInput;

// A copy or move on its way, shown with its progress; the job writes `progress` in place, so the slot stays put until the job is taken back.
typedef struct Prog_Transfer {
    P_Job *job;          // NULL for a free slot
    P_Progress progress;
    b32 move;
    b32 answerAll;       // the next answer about a taken name answers the rest too
    u64 started;         // quick ones finish before they would show
    u8 target[256];      // the target folder's name
    u32 targetLength;
} Prog_Transfer;

#define Prog_MaxTransfers 4

// What the preview pane shows of a file: a picture of Prog_PreviewSize squared, or the start of its text.
#define Prog_PreviewSize 512
#define Prog_PreviewBytes 16384

typedef struct Prog_Preview {
    u32 *pixels;
    String text;
} Prog_Preview;

// A copy or move's result: where each item went, for undoing it.
typedef struct Prog_Transferred {
    P_FileError error;  // first, as for the other file operations' results
    ArrayStr placed;
} Prog_Transferred;

typedef struct Prog_Created {
    P_FileError error;
    String name;
} Prog_Created;

typedef struct Prog_Places {
    P_ArrayPlace places;
    P_ArrayPlace drives;
    ArrayStr roots;
} Prog_Places;

typedef enum Prog_PaletteMode {
    Prog_PaletteMode_Commands,
    Prog_PaletteMode_Tabs,
    Prog_PaletteMode_GoTo,
    Prog_PaletteMode_Count,
} Prog_PaletteMode;

// The groups the command palette files its commands under, in the reference's order.
typedef enum Prog_CommandGroup {
    Prog_CommandGroup_Quick,
    Prog_CommandGroup_Folder,
    Prog_CommandGroup_View,
    Prog_CommandGroup_Sort,
    Prog_CommandGroup_Inspector,
    Prog_CommandGroup_Panels,
    Prog_CommandGroup_Tabs,
    Prog_CommandGroup_GoTo,
    Prog_CommandGroup_Misc,
    Prog_CommandGroup_Options,
    Prog_CommandGroup_Rename,
    Prog_CommandGroup_Text,
    Prog_CommandGroup_Scroll,
    Prog_CommandGroup_Count,
} Prog_CommandGroup;

// A palette row: a group's title when `kind` is None, else one of the group's commands.
typedef struct Prog_PaletteRow {
    enum8(Prog_CommandGroup) group;
    enum8(Prog_CommandKind) kind;
} Prog_PaletteRow;

enum { Prog_MaxPaletteRows = (u32)Prog_CommandKind_Count + (u32)Prog_CommandGroup_Count };

// Go To's rows come from the recent folders and the bookmarks, each section under a title that folds it, or from the folder typed.
typedef enum Prog_GoToSection {
    Prog_GoToSection_Recents,
    Prog_GoToSection_Bookmarks,
    Prog_GoToSection_Storage,
    Prog_GoToSection_Places,
    Prog_GoToSection_Folder,
    Prog_GoToSection_Found,
    Prog_GoToSection_Count,
} Prog_GoToSection;

typedef struct Prog_GoToItem {
    String path;
    enum8(Prog_GoToSection) section;
    b8 file;
    b8 hidden;  // hidden by the system or named with a leading dot, which dims it as the list does
    String name;           // shown instead of the folder's own name when set, as a drive's or a place's
    const P_Place *drive;  // the drive whose fill shows under the name, valid for the frame
} Prog_GoToItem;

typedef struct Prog_ArrayFlag { ArrayHeader header; b8 *v; } Prog_ArrayFlag;

typedef struct Prog_ArrayU32 { ArrayHeader header; u32 *v; } Prog_ArrayU32;

enum { Prog_IndexFlag_Hidden = 1, Prog_IndexFlag_File = 2 };

// Every folder and file on the drives, each as its name inside the entry of its parent, so that a few million fit; a root's name is its whole path.
typedef struct Prog_Index {
    Arena arena;
    ArrayStr names;
    Prog_ArrayU32 parents;  // UINT32_MAX for a root
    Prog_ArrayFlag flags;   // Prog_IndexFlag_
} Prog_Index;

// What was found inside the first `listed` of an index job's folders, in order, each with the entry of the folder it is in.
// IndexLoad hands back a whole index the same way, `count` entries in its own arena.
typedef struct Prog_Indexed {
    String *names;
    u32 *parents;
    b8 *flags;
    u64 count;
    u64 listed;
} Prog_Indexed;

// A folder's folders, then its files.
typedef struct Prog_GoToListed {
    P_File *files;
    u64 count;
} Prog_GoToListed;

// Sidebar sections, in display order.
typedef enum Prog_Group {
    Prog_Group_Recents,
    Prog_Group_Bookmarks,
    Prog_Group_Storage,
    Prog_Group_Places,
    Prog_Group_Count,
} Prog_Group;

// Folder totals measured before, by path, so that a folder shown again has its sizes at once.
typedef struct Prog_PathSet { MapHeader header; struct { String key; b32 value; } *v; } Prog_PathSet;
typedef struct Prog_SizeMap { MapHeader header; struct { String key; P_DirectoryTotals value; } *v; } Prog_SizeMap;

// ponytail: the cache starts over past this many folders; evict the oldest if big trees make that common.
#define Prog_MaxCachedSizes 50000

// What a sidebar item's tip shows of its folder.
typedef struct Prog_Inspected {
    P_DirectoryTotals totals;
    i64 created;
} Prog_Inspected;

typedef struct Prog_Measured {
    P_DirectoryTotals *totals;  // for the first `count` rows of the job
    u64 count;
} Prog_Measured;

// A single-line text field.
// Offsets are bytes into `text`; names never reach the buffer size on any file system.
typedef struct Prog_TextEdit {
    u8 text[1024];
    u32 length;
    u32 cursor;
    u32 anchor;
    u32 version;  // bumped by every change to the text
} Prog_TextEdit;

typedef struct Prog_Directory {
    String path;
    Prog_Entry *entries;
    u64 count;
    u64 folderCount;
    // Everything the folder holds, hidden items included.
    u64 totalFolders;
    u64 totalFiles;
    P_FileError error;
    b32 cancelled;
    enum8(Prog_Sort) sort;
    b32 descending;
    b32 showHidden;
    // Every subfolder's items are listed too, named by their path below `path` with '/' separators.
    b32 expanded;
    // The folders their entries are in, each once, for what is worked out per folder rather than per entry.
    String *parents;
    u32 parentCount;
} Prog_Directory;

// The Prog_State fields each tab has its own of.
// The shown tab's live in Prog_State; switching swaps them with the tab's slot, so a tab keeps its listing, selection, filter and scroll.
// The filter keeps the rows it lets through in `view`, best match first and in listing order within a score; rows it hides are never selected, so commands cannot act on them.
#define Prog_TabFields(X) \
    X(Arena, directoryArena) X(Prog_Directory, directory) X(Arena, historyArena) X(ArrayStr, history) X(u64, historyAt) \
    X(u64, focusedRow) X(u64, anchorRow) X(u64, selectedCount) X(b32, hasFocus) X(b32, hasAnchor) \
    X(b32, showHidden) X(enum8(Prog_Sort), sort) X(b32, descending) \
    X(Prog_TextEdit, filter) X(b32, filterSticky) X(b32, expanded) X(b32, hideFolders) X(b32, hideFiles) \
    X(Arena, viewArena) X(u64 *, view) X(u64, viewCount) X(u64, viewFolders) X(UI_Vec2, scroll)

#define Prog__TabField(type, name) type name;
typedef struct Prog_Tab {
    u64 id;  // keys the tab's blocks, so a drag follows it across slots
    Prog_TabFields(Prog__TabField)
} Prog_Tab;
#undef Prog__TabField

// ponytail: fixed tab count; grow the array if 32 tabs per panel falls short.
#define Prog_MaxTabs 32

// The Prog_State fields each panel has its own of, besides its tabs' (Prog_TabFields).
// The current panel's live in Prog_State; Prog__UsePanel swaps them with the panel's slot, so every panel keeps its own listing, jobs, watch and layout.
//   error: the last failure, in errorArena; directoryJob, generation, loading: the listing on its way
//   revealFocus, resetScroll, restoreScroll: scroll changes the next build makes; pageRows: lines visible in the last layout, for Page Up/Down
//   tabs, tabCount, activeTab: the panel's Prog_MaxTabs tab slots, in tabArena from the first use; the shown one's slot holds nothing while it is shown
//   zoom: the view's zoom level, below Prog_ZoomLevels
//   gridColumns, gridPitch, gridItem: the list's last layout, in its coordinates (x from its left edge, y from the top of its content):
//     items per line (1 in Details and List), the step from one cell to the next, and the first cell's item box
//   marquee, marqueeKey, marqueeAnchor, marqueeBox, marqueeAdds: the box selection; the key is the block the drag began on, kept after it so that its release is not a click
//   filtering: the filter box has the keyboard; filterRect: its last layout
//   listing, measureJob, sortPending: folder totals; each adopted listing gets a new id, and totals that arrive while a menu or drag holds the rows still sort later
//   watch, watchDirty, watchReloadTime: the shown folder's watch, which reloads it at most every Prog_WatchInterval
//   focusedRowRect: last layout of the focused row, to place a keyboard-opened menu
//   renameCreatedUntil: until then, rename the one item a listing of this folder adds
//   panelRect: the panel's last layout, for moving between panels with the keyboard
//   listStarted, listTook: when the last listing was asked for and, once it arrived, how long it took
#define Prog_PanelFields(X) \
    X(Arena, errorArena) X(String, error) X(P_Job *, directoryJob) X(u64, generation) X(b32, loading) \
    X(b32, revealFocus) X(b32, resetScroll) X(b32, restoreScroll) X(u64, pageRows) \
    X(Arena, tabArena) X(Prog_Tab *, tabs) X(u32, tabCount) X(u32, activeTab) X(u32, zoom) \
    X(u64, gridColumns) X(UI_Vec2, gridPitch) X(R_Rect, gridItem) \
    X(b32, marquee) X(UI_Key, marqueeKey) X(UI_Vec2, marqueeAnchor) X(R_Rect, marqueeBox) X(KeyMods, marqueeAdds) \
    X(b32, filtering) X(R_Rect, filterRect) X(u64, listing) X(P_Job *, measureJob) X(b32, sortPending) \
    X(P_DirectoryWatch, watch) X(b32, watchDirty) X(u64, watchReloadTime) X(R_Rect, focusedRowRect) X(u64, renameCreatedUntil) X(R_Rect, panelRect)     X(u64, listStarted) X(u64, listTook)

#define Prog__PanelField(type, name) type name;
typedef struct Prog_Panel {
    u64 id;  // 0 for a free slot; keys the panel's blocks and tags its commands and jobs
    u64 splitMicroseconds;  // when a split made it, which it zooms in from; 0 for the panels the window starts with
    u64 focusMicroseconds;  // when it last became or stopped being the active panel, which its status bar slides and its dimming fades from; 0 while settled
    b8 wasActive, drawn;
    Prog_TabFields(Prog__PanelField)
    Prog_PanelFields(Prog__PanelField)
} Prog_Panel;
#undef Prog__PanelField

// ponytail: fixed panel count; 8 side by side are already narrow.
#define Prog_MaxPanels 8

// The panel layout: a tree of splits whose leaves are panels, kept in Prog_State.splits with the root first.
typedef enum Prog_SplitKind {
    Prog_SplitKind_None,        // a free node
    Prog_SplitKind_Panel,
    Prog_SplitKind_Horizontal,  // `first` left of `second`
    Prog_SplitKind_Vertical,    // `first` above `second`
    Prog_SplitKind_Count,
} Prog_SplitKind;

typedef struct Prog_Split {
    enum8(Prog_SplitKind) kind;
    u8 first, second;  // child nodes of a split
    u64 panel;         // a leaf's panel id
    f32 ratio;         // the first child's share; 0 halves
} Prog_Split;

#define Prog_MaxSplits (2 * Prog_MaxPanels)

// Where a dragged tab goes: a slot of a panel's strip, into a panel, a new panel on one of a panel's sides, or a new window.
typedef enum Prog_TabDrop {
    Prog_TabDrop_None,
    Prog_TabDrop_Strip,
    Prog_TabDrop_Into,
    Prog_TabDrop_Left,
    Prog_TabDrop_Right,
    Prog_TabDrop_Top,
    Prog_TabDrop_Bottom,
    Prog_TabDrop_Window,
    Prog_TabDrop_Count,
} Prog_TabDrop;

typedef struct Prog_TabDropTarget {
    enum8(Prog_TabDrop) kind;
    u64 panel;        // the panel it goes into or beside
    u32 slot;         // the strip slot it goes before
    u64 fromPanel;    // where the tab is
    u32 fromSlot;
    b32 copy;         // Ctrl leaves the tab where it was
} Prog_TabDropTarget;

typedef struct Prog_State {
    Arena frameArena;
    Arena jobsArena;
    UI_Context ui;
    P_JobQueue jobs;
    Prog_Command *commandFirst;
    Prog_Command *commandLast;
    // The current panel's fields; see Prog_TabFields and Prog_PanelFields.
#define Prog__StateField(type, name) type name;
    Prog_TabFields(Prog__StateField)
    Prog_PanelFields(Prog__StateField)
#undef Prog__StateField
    u64 nextTabId;
    // Panels. The current one's slot holds nothing while it is current; the active one has the keyboard and is current between frames.
    Prog_Panel panels[Prog_MaxPanels];
    u32 panelCount;
    u32 currentPanel;
    u32 activePanel;
    u64 nextPanelId;
    Prog_Split splits[Prog_MaxSplits];
    u64 hoverPanel;           // the panel of the row under the pointer, and the panel under the pointer, in the last build
    u64 pointerPanel;
    // A dragged tab, whose block is `tabDragKey`; the build finds `tabDrop`, where it would go, and the release drops it there.
    b32 tabDrag;
    b32 tabDragCancelled;  // Escape ended the drag; the held tab does not start another until it is let go
    UI_Key tabDragKey;
    Prog_TabDropTarget tabDrop;
    Prog_TabDropTarget tabDropped;  // the drop that TabDrop carries out
    Arena closedArena;        // paths of closed tabs, for Reopen Closed Tab
    ArrayStr closedTabs;
    // File icons, keyed by P_FileIconKey.
    // The map and keys live in iconArena; atlas pages survive resets and are reused.
    Arena iconArena;
    Prog_IconMap icons;
    Prog_ArrayIconRequest iconRequests;  // queued while building UI, submitted by Prog_ProcessCommands
    i32 iconSize;
    // ponytail: fixed page count (8192 icons at 16 px); later icons fall back to glyphs.
    Prog_IconAtlas iconAtlas;
    // Thumbnails for the icon views, keyed by P_FileThumbnailKey, at the one size the zoom draws them.
    // A new size, or a full atlas, drops them all; the items still shown ask again.
    Arena thumbnailArena;
    Prog_IconMap thumbnails;
    Prog_IconAtlas thumbnailAtlas;
    // The busy spinner's frames in the icon atlas; its eight rays repeat every eighth of a turn, so the frames cover that much.
    Prog_Icon spinner[Prog_SpinnerFrames];
    i32 spinnerSize;          // iconSize the frames were drawn at; 0 before they are
    // Context menu. Items and rows live in menuArena until the menu closes.
    Arena menuArena;
    P_ContextMenu menu;
    b32 menuOpen;
    UI_Vec2 menuAt;
    u64 *menuRows;            // directory entries the menu acts on
    u64 menuRowCount;
    String menuTarget;        // the folder a sidebar item's menu acts on instead, in menuArena
    u32 menuPath[8];          // the open submenu item at each level
    u32 menuDepth;            // open submenu levels below the top one
    u32 menuHighlight;        // item index, or UINT32_MAX for none
    const Prog_Icon **menuIcons;  // per item, NULL when it has no picture
    Prog_MenuCommand *menuCommands;  // per item of Filey's own menus; NULL for the system's
    UI_Key menuHot;           // hot block when the menu was last built
    Prog_TextEdit menuSearch; // typed into a system menu, which then shows only its top items holding it
    enum8(Prog_CommandKind) menuOpener;  // the button whose menu is open, which stays lit; None for other menus
    // The button under the pointer this frame, whose tip shows once the pointer rests on it.
    UI_Key tooltipKey;
    R_Rect tooltipRect;
    enum8(Prog_CommandKind) tooltipKind;
    String tooltipLabel;      // the command's label when empty
    // A sidebar item's tip: its folder, inspected by tipJob into tipArena.
    String tipPath;
    Arena tipArena;
    P_Job *tipJob;
    b32 tipKnown;
    Prog_Inspected tip;
    String tooltipDetail;     // a line over the command, such as the folder a history button goes to
    String tooltipPath;       // a sidebar folder, whose tip shows what it holds instead
    f32 menuRight;            // when nonzero, the right edge the top level lines up with
    // Drag and drop.
    // The system's drag loop is modal, so a drag is requested during a frame and run between frames by Prog_DragSelection.
    b32 dragRequested;
    // The system's window move loop is modal too, and runs between frames for the same reason.
    b32 moveRequested;
    b32 dragging;
    u64 hoverRow;             // row under the pointer in the last build
    f32 zoomWheel;            // wheel travel with Ctrl held that has not made a whole step yet
    b32 hasHoverRow;
    // Inline rename of the entry named renameFrom.
    b32 renaming;
    u8 renameFrom[1024];
    u32 renameFromLength;
    Prog_TextEdit rename;
    R_Rect renameRect;        // last layout of the edit box
    u8 selectText[1024];      // select this name in the next listing
    u32 selectLength;
    b32 selectRename;         // and rename it, for items Filey just created
    // The command palette, which takes the keyboard while open; the highlight indexes Prog_PaletteMatches.
    b32 paletteOpen;
    Prog_TextEdit palette;
    u32 paletteHighlight;     // UINT32_MAX for none, which Go To starts with from Edit Path
    u32 paletteScroll;        // the first command row shown
    u16 paletteExpanded;      // bits by Prog_CommandGroup, the groups shown open
    // In its tab mode it finds the active panel's tabs, in a box under that panel's tab list button; the highlight indexes Prog_TabMatches.
    // In its Go To mode it finds folders to open, recent ones or those inside the folder typed; the highlight indexes Prog_GoToMatches.
    enum8(Prog_PaletteMode) paletteMode;
    R_Rect tabListRect;       // that button in the last layout
    // Go To lists the folder typed so far, gotoFolder, whose subfolders gotoJob puts in gotoArena; gotoListed once they are there.
    u8 gotoFolder[1024];
    u32 gotoFolderLength;
    P_Job *gotoJob;
    Arena gotoArena;
    Prog_GoToListed gotoListing;
    b32 gotoListed;
    b32 gotoFolded[Prog_GoToSection_Folder];  // sections shown by their title alone
    u32 gotoScroll;           // the first row shown
    // The texts the back and forward buttons go through, which every expansion adds to.
    Arena gotoHistoryArena;
    ArrayStr gotoHistory;
    u32 gotoHistoryAt;
    // Every folder listed since the start, and the folders in it, which Go To searches with the recent folders and the bookmarks.
    Arena knownArena;
    ArrayStr knownFolders;
    Prog_PathSet knownSet;
    // The drives, which indexJob walks breadth first a slice at a time, the folders from indexAt on still to be listed. Until it is done Go To
    // searches savedIndex, the last run's, when there is one.
    Prog_Index index;
    Prog_Index savedIndex;
    u64 indexAt;
    P_Job *indexJob;
    P_Job *indexLoadJob;
    b32 indexStarted;
    b32 indexDone;
    // Go To's search, which goes through the folders close at hand and then the index a few milliseconds at every look, so that typing never waits
    // for it; what it found is kept while the text, the folders close at hand and the index searched stay the same.
    Arena gotoFoundArena;
    Prog_GoToItem *gotoPool;  // what matched so far
    u16 *gotoRanks;
    u32 gotoPooled;
    u32 gotoFloor;            // the rank a match must beat, once the pool has been full
    u64 gotoScanAt;           // the index's next entry to look at
    u64 gotoScanFrame;        // the UI frame that looked last
    u64 gotoNearby;
    const Prog_Index *gotoIndex;
    Prog_PathSet gotoNearbyNames;  // the names of the folders close at hand, which the index finds again only under them
    Prog_PathSet gotoNearbyPaths;
    Prog_PathSet gotoSeen;
    u8 gotoFoundText[1024];
    u32 gotoFoundTextLength;
    b32 gotoSearching;        // the look has not reached the end yet
    Prog_Transfer transfers[Prog_MaxTransfers];
    Arena sizeArena;
    Prog_SizeMap sizes;
    // File operations Undo can reverse, newest last.
    Arena undoArena;
    Prog_ArrayUndo undo;
    // The preview pane beside the active panel's list: the focused item's picture or text, loaded by previewJob into previewArena.
    b32 previewShown;
    Arena previewArena;
    String previewPath;       // shown, or on its way
    P_Job *previewJob;
    String previewText;
    R_TextureHandle previewTexture;
    b32 previewPicture;       // the texture holds previewPath's picture
    // Permanent delete waits for this confirmation; the paths live in confirmArena.
    b32 confirmingDelete;
    Arena confirmArena;
    String *confirmPaths;
    u64 confirmCount;
    // Sidebar.
    b32 sidebarHidden;
    b32 groupToggled[Prog_Group_Count];  // differs from the group's default, which is open for bookmarks and storage
    b32 placesWanted;         // the sidebar was built, so its places are kept current
    Arena placesArena;        // the latest places listing
    P_ArrayPlace places;
    P_ArrayPlace drives;
    ArrayStr pathRoots;       // folders that breadcrumbs start from, for folders inside them
    P_Job *placesJob;
    u64 placesTime;
    // Bookmarked and recently shown folders, saved in configDirectory when the app sets one.
    Arena settingsArena;
    String configDirectory;
    Arena bookmarkArena;
    ArrayStr bookmarks;
    // Where a drag over the bookmarks would pin its folders: one more than the position, or 0 when it is elsewhere.
    // The build shows it; the drop, which arrives before the next build, uses the one committed after the last.
    u64 bookmarkDropShown;
    u64 bookmarkDrop;
    Arena recentArena;
    ArrayStr recents;
    // One save of each list at a time, since two writing one file at once collide; a change during a save saves again after it.
    b32 saving[2];            // bookmarks, recents
    b32 saveAgain[2];
    // See-through window with the glass theme; the app applies it to the window.
    b32 transparent;
    // Settings.txt in the config folder: the UI scale and hotkeys here, colors in the custom theme; its folder's watch applies edits as they are saved.
    f32 uiScale;              // 0 is 1
    Prog_Hotkey userHotkeys[64];
    u32 userHotkeyCount;
    b8 hotkeyOverridden[Prog_CommandKind_Count];  // the command's default hotkeys give way to the settings' ones
    P_DirectoryWatch configWatch;
    // Loud colors for telling UI surfaces apart; FILEY_DEBUG_THEME turns it on at startup.
    b32 debugTheme;
    // "animations = off" in the settings: menus, scrolling and the caret change at once.
    b32 animationsOff;
    // A point smaller than the main font, for the details columns; the main font while unset.
    const R_Font *smallFont;
    const R_Font *smallIcons;  // the icons at about two thirds of their size
    // Set by the app so that the first listing loads shell extensions in the background; the first context menu would otherwise stall on them.
    b32 warmMenus;
} Prog_State;

function b32 Prog_Init(Prog_State *state, String path, P_JobWakeCallback *wake, void *user);
function void Prog_Release(Prog_State *state);
function void Prog_RequestDirectory(Prog_State *state, String path, b32 addHistory, u64 historyAt);
function b32 Prog_TakeCompletions(Prog_State *state);
function String Prog_ParentPath(String path);
function String Prog_JoinPath(Arena *arena, String path, String name);
function String Prog_FormatSize(Arena *arena, u64 bytes);
function void Prog_InspectFolder(Prog_State *state, String path);
function i32 Prog_CompareNames(String a, String b);
function void Prog_SelectRow(Prog_State *state, u64 row, KeyMods mods);
function void Prog_OpenRow(Prog_State *state, u64 row);
function Prog_Command *Prog_QueueCommand(Prog_State *state, Prog_CommandKind kind, u64 row, u32 pressCount, u64 inputOrder);
function void Prog_HandleInput(Prog_State *state, PlatformEventList events);
function b32 Prog_ProcessCommands(Prog_State *state);
function void Prog_BuildUI(Prog_State *state, const R_Font *font, const R_Font *icons, f32 scale);
function f32 Prog_RowHeight(const R_Font *font);
function const Prog_Icon *Prog_FileIcon(Prog_State *state, String directory, const Prog_Entry *entry);
function const Prog_Icon *Prog_PathIcon(Prog_State *state, String path, P_FileKind kind);
function const Prog_Icon *Prog_FolderIcon(Prog_State *state, String path);
function const Prog_Icon *Prog_ThumbnailIcon(Prog_State *state, String directory, const Prog_Entry *entry, i32 size);
function void Prog_SubmitIconRequests(Prog_State *state);
function void Prog_CloseMenu(Prog_State *state);
function void Prog_DragSelection(Prog_State *state);
function void Prog_Reload(Prog_State *state);
function void Prog_BeginRename(Prog_State *state, u64 row);
function void Prog_SetTransparent(Prog_State *state, b32 transparent);
function void Prog_SetDebugTheme(Prog_State *state, b32 debug);
function String Prog_EditText(const Prog_TextEdit *edit);
function String Prog_EntryLeaf(String name);
function String Prog_EntryParent(String name);
function b32 Prog_FilterMatch(String name, String filter, u8 *marks);
function i32 Prog_FuzzyScore(String name, String path, String typed, u8 *nameMarks, u8 *pathMarks);
function i32 Prog_EntryScore(String name, String filter, u8 *marks);
function void Prog_ApplyFilter(Prog_State *state, b32 edited);
function void Prog_ClearFilter(Prog_State *state);
function u64 Prog_ViewPosition(const Prog_State *state, u64 row);
function void Prog_LoadSettings(Prog_State *state, String configDirectory);
function b32 Prog_IsBookmarked(const Prog_State *state, String path);
function void Prog_ToggleBookmark(Prog_State *state, String path);
function void Prog_PinBookmarks(Prog_State *state, const ArrayStr *paths, u64 at);
function u32 Prog_PanelIndex(const Prog_State *state, u64 id);
function u64 Prog__PanelId(const Prog_State *state);
function UI_Key Prog_PanelKeySeed(u64 id);
function void Prog__ReleaseTab(Prog_Tab *tab);
function u32 Prog_SplitPanel(Prog_State *state, u32 index, Prog_SplitKind kind, b32 before, String path);
function void Prog_ClosePanel(Prog_State *state, u32 index);
function void Prog_DropTab(Prog_State *state, Prog_TabDropTarget drop);
function void Prog_FocusPanelToward(Prog_State *state, i32 dx, i32 dy);
function b32 Prog_GroupOpen(const Prog_State *state, Prog_Group group);
function String Prog_TabPath(const Prog_State *state, u32 index);
function void Prog_OpenTab(Prog_State *state, String path, u32 at, b32 activate);
function void Prog_SelectTab(Prog_State *state, u32 index);
function void Prog_CloseTab(Prog_State *state, u32 index);
function void Prog_MoveTab(Prog_State *state, u32 from, u32 to);
function u32 *Prog_ResizeIcon(Arena *arena, P_Image source, i32 size);
function const Prog_Icon *Prog_ImageIcon(Prog_State *state, P_Image image);
function const Prog_Icon *Prog_SpinnerIcon(Prog_State *state, u64 nowMicroseconds);
function Prog_View Prog_ZoomView(u32 zoom);
function void Prog_OpenCommandMenu(Prog_State *state, const Prog_MenuCommand *items, u32 count, UI_Vec2 at);
function String Prog_HotkeyText(Arena *arena, const Prog_State *state, Prog_CommandKind kind);
function const Prog_Hotkey *Prog_FindHotkey(const Prog_State *state, Key key, KeyMods mods);
function b32 Prog_ParseHotkey(String text, Key *key, KeyMods *mods);
function void Prog_ApplySettings(Prog_State *state, String text);
function String Prog_SettingsTemplate(Arena *arena, const Prog_State *state);
function void Prog_ReloadSettingsFile(Prog_State *state);
function u32 Prog_PaletteMatches(const Prog_State *state, Prog_PaletteRow *out);
function u32 Prog_TabMatches(const Prog_State *state, u32 *out);
#define Prog_MaxGoToFound 500
// ponytail: the index stops growing here, at about 300 MB, mostly names and their String headers; packing the names into one buffer would let it cover more.
#define Prog_MaxIndexed 4000000
// The index is kept between runs in the settings folder: the magic, then for every entry its parent's entry (u32), its flags (u8), its name's
// length (u16) and the name.
#define Prog_IndexFile      S("Index.bin")
#define Prog_IndexFileMagic S("Filey index 1\n")
// The searched index's entry `entry` as a whole path, in `arena`.
function String Prog_IndexPath(Arena *arena, const Prog_Index *index, u32 entry);
// When the entry was last changed as the list shows it, in `arena`; made only for the rows shown, since a big tree has millions.
function String Prog_EntryModified(Arena *arena, const Prog_Entry *entry);
function Prog_GoToItem *Prog_GoToMatches(Prog_State *state, u32 *count, String *folder);
function String Prog_SessionText(Arena *arena, Prog_State *state);
function b32 Prog_RestoreSession(Prog_State *state, String text);
function void Prog_LoadSession(Prog_State *state);
function void Prog_PushUndo(Prog_State *state, Prog_UndoKind kind, const String *from, const String *to, u64 count);
function void Prog_UndoLast(Prog_State *state);
