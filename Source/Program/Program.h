#pragma once

#include "../Platform/Files.h"

// The version Filey shows; a release build passes its own through EXTRA_CFLAGS.
// Translation: the text shown for `english` in the chosen language, English where the language has none. Tr() takes a literal, TrF() a
// format for StrFormat, whose translation keeps its conversions; TrInit() marks a literal in a table as one to translate where it shows. The
// build lists every one of them (Meta/Strings.meta.h) for new language files.
function String Prog_Tr(String english);
function String Prog_Untr(String shown);
function const char *Prog_TrFormat(const char *english);
function const char *Prog_TrPlural(u64 count, const char *one, const char *other);
#define Tr(text) Prog_Tr(S(text))
#define TrF(text) Prog_TrFormat(text)
#define TrInit(text) SInit(text)
#define TrN(count, one, other) Prog_TrPlural(count, one, other)
// A text made where it cannot be translated, as on a worker thread, which is translated where it is shown.
#define TrMark(text) S(text)
// Text kept as it is on purpose, as a saved name or a file's, which the build's check for untranslated text passes over.
#define NoTr(text) S(text)
#define NoTrInit(text) SInit(text)
#define NoTrF(text) text

// Where crashes are reported, a new issue of the project's.
#define Prog_IssuesUrl "https://github.com/Solessfir/Filey/issues/new"

#ifndef FILEY_VERSION
#define FILEY_VERSION "0.1.0"
#endif

// The first values match P_FileOrderKey, which carries them to the platform.
// The platform does not know folder totals, so files open in name order under the rest.
typedef enum Prog_Sort {
    Prog_Sort_Name,
    Prog_Sort_Type,
    Prog_Sort_Modified,
    Prog_Sort_Size,
    Prog_Sort_Folders,
    Prog_Sort_Files,
    Prog_Sort_Created,
    Prog_Sort_Accessed,
    Prog_Sort_Items,     // folders and files inside, together
    // The Recycle Bin's and This PC's own columns.
    Prog_Sort_Deleted,
    Prog_Sort_Original,
    Prog_Sort_FreeSpace,
    Prog_Sort_PercentFull,
    Prog_Sort_FileSystem,
    Prog_Sort_Count,
} Prog_Sort;

// Sorts by a text rather than a number: from A first, the rest from the most.
#define Prog_SortByText(sort) ((sort) == Prog_Sort_Name || (sort) == Prog_Sort_Type || (sort) == Prog_Sort_Original || (sort) == Prog_Sort_FileSystem)

static_assert((i32)Prog_Sort_Size == (i32)P_FileOrderKey_Size && (i32)Prog_Sort_Folders == (i32)P_FileOrderKey_Count);

// Where folders go among the files, whatever the sort: before them, after them, or mixed in.
typedef enum Prog_FolderSort {
    Prog_FolderSort_First,
    Prog_FolderSort_Last,
    Prog_FolderSort_Mixed,
    Prog_FolderSort_Count,
} Prog_FolderSort;

// What a panel shows a folder with, as a folder view keeps it.
typedef struct Prog_ViewSettings Prog_ViewSettings;

// How a folder's view is kept: not at all, by its own line, by its own with its subfolders', or as a folder's above it is with its subfolders'.
// The view new tabs start in, the settings' newtab.view.
typedef enum Prog_NewTabView {
    Prog_NewTabView_Details,
    Prog_NewTabView_List,
    Prog_NewTabView_Columns,
    Prog_NewTabView_Icons,
    Prog_NewTabView_LargeIcons,
    Prog_NewTabView_ExtraLargeIcons,
    Prog_NewTabView_Count,
} Prog_NewTabView;

typedef enum Prog_ViewScope {
    Prog_ViewScope_None,
    Prog_ViewScope_Folder,
    Prog_ViewScope_Tree,
    Prog_ViewScope_Inherited,
    Prog_ViewScope_Count,
} Prog_ViewScope;

// What a details column shows, one value of an entry each.
typedef enum Prog_ColumnId {
    Prog_ColumnId_Name,
    Prog_ColumnId_Type,
    Prog_ColumnId_Modified,
    Prog_ColumnId_Folders,
    Prog_ColumnId_Files,
    Prog_ColumnId_Size,
    Prog_ColumnId_Created,
    Prog_ColumnId_Accessed,
    Prog_ColumnId_FolderUsage,
    Prog_ColumnId_VolumeUsage,
    Prog_ColumnId_FileSystem,
    Prog_ColumnId_FreeSpace,
    Prog_ColumnId_PercentFull,
    Prog_ColumnId_OriginalLocation,
    Prog_ColumnId_DateDeleted,
    Prog_ColumnId_Git,
    Prog_ColumnId_Count,
} Prog_ColumnId;

// The columns a details list can have, in the order the header's menu lists them; Items is the Folders and Files pair.
typedef enum Prog_ColumnKind {
    Prog_ColumnKind_Name,
    Prog_ColumnKind_Type,
    Prog_ColumnKind_Size,
    Prog_ColumnKind_Items,
    Prog_ColumnKind_Created,
    Prog_ColumnKind_Modified,
    Prog_ColumnKind_Accessed,
    Prog_ColumnKind_VolumeUsage,
    Prog_ColumnKind_FolderUsage,
    Prog_ColumnKind_Git,
    // This PC's, for its drives.
    Prog_ColumnKind_FileSystem,
    Prog_ColumnKind_FreeSpace,
    Prog_ColumnKind_PercentFull,
    // The Recycle Bin's.
    Prog_ColumnKind_OriginalLocation,
    Prog_ColumnKind_DateDeleted,
    Prog_ColumnKind_Count,
} Prog_ColumnKind;

// The kinds a folder's columns can be, which the header's menu offers.
#define Prog_FolderColumnKinds (Prog_ColumnKind_Git + 1)

// A panel's columns in their order, each with its share of the row; a share of 0 is the kind's own, and no columns at all are the defaults.
// The least and the most of the row a resized column takes.
#define Prog_ColumnShareMin 0.02f
#define Prog_ColumnShareMax 0.8f

typedef struct Prog_ColumnSet {
    u32 count;
    u8 kinds[Prog_ColumnKind_Count];
    f32 shares[Prog_ColumnKind_Count];
} Prog_ColumnSet;

struct Prog_ViewSettings {
    u32 zoom;
    enum8(Prog_Sort) sort;
    b32 descending;
    enum8(Prog_FolderSort) folders;
    Prog_ColumnSet columns;
    enum8(Prog_GroupBy) groupBy;
};

// What a list groups its items under headings by.
typedef enum Prog_GroupBy {
    Prog_GroupBy_None,
    Prog_GroupBy_Type,
    Prog_GroupBy_Modified,
    Prog_GroupBy_Size,
    Prog_GroupBy_Name,
    Prog_GroupBy_Created,
    Prog_GroupBy_Accessed,
    Prog_GroupBy_Items,
    Prog_GroupBy_Deleted,
    Prog_GroupBy_Original,
    Prog_GroupBy_FreeSpace,
    Prog_GroupBy_FileSystem,
    Prog_GroupBy_PercentFull,
    Prog_GroupBy_VolumeUsage,
    Prog_GroupBy_FolderUsage,
    Prog_GroupBy_Count,
} Prog_GroupBy;

// A heading of a grouped list and its items, view positions [start, start + count): none while it is folded, of the `total` it has.
// `top` is where the last layout put the heading in the list's content.
typedef struct Prog_ListGroup {
    String label;
    u64 start;
    u64 count;
    u64 total;
    f32 top;
} Prog_ListGroup;

// Prog_CommandKind is generated from Program/Commands.txt.
#include "Meta/Commands.meta.h"

// Where a command's hotkeys work: the browser, any text box, the rename box (before the text box's), Go To or the folder dropdown.
// A key may mean a command in each.
typedef enum Prog_HotkeyScope {
    Prog_HotkeyScope_Browser,
    Prog_HotkeyScope_Text,
    Prog_HotkeyScope_Rename,
    Prog_HotkeyScope_GoTo,
    Prog_HotkeyScope_Files,
    Prog_HotkeyScope_Count,
} Prog_HotkeyScope;

typedef struct Prog_CommandDesc {
    String name;
    String label;    // empty for internal commands that never reach a palette or menu
    String aliases;  // comma-separated search words
    String tooltip;
    b32 noRepeat;    // ignore auto-repeat key events
    enum8(Prog_HotkeyScope) scope;
} Prog_CommandDesc;

typedef struct Prog_Hotkey {
    Key key;
    KeyMods mods;  // matched exactly
    Key lead;      // for a sequence such as "G G", the key pressed before it, without modifiers
    enum16(Prog_CommandKind) command;
} Prog_Hotkey;

// A chip of a command's hotkeys as a row shows it.
typedef struct Prog_HotkeyChip {
    String text;
    u8 slot;       // which of the command's hotkeys, for a click to rebind; UINT8_MAX for a mouse button's, which stays
    b8 idle;       // a character key that types while Character Key Action is not Trigger Hotkey
    b8 conflict;   // another command has the same key where it works
    b8 recording;  // "Press A Key" in its place, waiting for the key to bind
    b8 adds;       // recording a new one, past the last
} Prog_HotkeyChip;

// The slot the palette's plus records into, past any command's last hotkey; UINT8_MAX is a mouse button's chip.
enum { Prog_NewHotkeySlot = UINT8_MAX - 1 };

// What a character key without modifiers but Shift does in the browser: types into the filter, runs its hotkey, or selects the first item whose name
// starts with what is typed. Space and '/' run their hotkeys in every case.
typedef enum Prog_CharacterKeys {
    Prog_CharacterKeys_TypeInFilter,
    Prog_CharacterKeys_TriggerHotkey,
    Prog_CharacterKeys_SelectTypedItem,
    Prog_CharacterKeys_Count,
} Prog_CharacterKeys;

typedef struct Prog_Command Prog_Command;
struct Prog_Command {
    Prog_Command *next;
    enum16(Prog_CommandKind) kind;
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
    enum16(Prog_CommandKind) kind;
    u64 row;
    String label;  // the command's label when empty
    b32 checked;
    String icon;   // a glyph of the icon font before the label, or none
    b8 toggle;     // shows `checked` as a box at the row's end rather than a check before the label
    b8 radio;      // with toggle, the box is round and chosen shows a dot, as one of the choices around it
    b8 disabled;
    String mouse;  // a mouse button that does it too, in a chip before the hotkeys'
    enum16(Prog_CommandKind) hotkeysOf;  // the command whose hotkeys its chips show, when not its own
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
    // A drive in This PC, whose name is its root's path, or an item in the Recycle Bin, whose name is where the bin keeps it;
    // `label` is the name it shows.
    b32 drive;
    String label;
    String fileSystem;
    u64 freeBytes;
    String original;     // a binned item's folder before it was deleted
    i64 deletedTimeUnix;
    // Among the recents, and sorted before the rest for it.
    b8 recent;
    b8 recentFirst;
    // Where a grid view last drew it, in the list's content, and in which UI frame, so that it can glide to where a new layout puts it.
    f32 shownX, shownY;
    u64 shownFrame;
} Prog_Entry;

// The folder that lists the drives, as Explorer's This PC does; no real path looks like it.
#define Prog_ThisPC S("This PC")

typedef enum Prog_JobKind {
    Prog_JobKind_Directory,
    Prog_JobKind_Open,
    Prog_JobKind_Icons,
    Prog_JobKind_Rename,
    Prog_JobKind_WarmMenu,
    Prog_JobKind_Tidy,             // removes the run folders of the runs that are over, and brings the installed apps' entry up to date
    Prog_JobKind_LongPaths,        // sets the system's long path support, `confirm` saying on or off
    Prog_JobKind_ArchivePassword,  // whether the password `name` opens the archive `path`
    Prog_JobKind_Measure,
    Prog_JobKind_Inspect,
    Prog_JobKind_GoTo,
    Prog_JobKind_Index,
    Prog_JobKind_IndexLoad,
    Prog_JobKind_IndexFinish,
    Prog_JobKind_IndexRefresh,     // lists `path` whole, to bring its items in the finished drive index up to date
    Prog_JobKind_Transfer,
    Prog_JobKind_Trash,
    Prog_JobKind_Delete,
    Prog_JobKind_EmptyTrash,
    Prog_JobKind_Restore,
    Prog_JobKind_BinWait,      // waits until the bin's items at `paths` have left it, after a command of its menu      // `paths` from the bin `path` back where they came from, or all of it without any
    Prog_JobKind_Create,
    Prog_JobKind_Link,
    Prog_JobKind_Places,
    Prog_JobKind_Shares,
    Prog_JobKind_UpdateCheck,
    Prog_JobKind_UpdateInstall,
    Prog_JobKind_Save,
    Prog_JobKind_Preview,
    Prog_JobKind_Undo,
    Prog_JobKind_HexPage,
    Prog_JobKind_HexFind,     // the first place from `offset` on, round to it again, where the bytes `name` are in the file `path`
    Prog_JobKind_TextWindow,  // a big text file's window: from `offset`, the line numbered `line`, which starts inside highlighter state `open`
    Prog_JobKind_TextIndex,   // a big text file's lines, counted in steps
    Prog_JobKind_TextFind,    // a big text file's lines the filter `name` keeps, for its version `line`
    Prog_JobKind_PdfPage,
    Prog_JobKind_Git,
    Prog_JobKind_Repos,
    Prog_JobKind_RecentsCheck,
    Prog_JobKind_GitBranches,  // the local branches of the repository at `path`
    Prog_JobKind_GitSwitch,    // `git switch name` in `path`
    Prog_JobKind_GitPull,      // `git pull --ff-only` in `path`
    Prog_JobKind_GitFetch,     // `git fetch` in the repository `path`
    Prog_JobKind_FilesList,
    Prog_JobKind_Reveal,
    Prog_JobKind_Fonts,
    Prog_JobKind_Play,         // plays the audio file `path` from `offset` milliseconds in
    Prog_JobKind_Mount,        // connects to the server at `name`, whose folder the panel then shows
    Prog_JobKind_Usage,        // the disk usage of the folder `path`, for the inspector
    Prog_JobKind_TorrentProgress,  // how far the torrent's files `paths`, `sizes` long, are, by aria2c's control file `path`, for the inspector showing `name`
    Prog_JobKind_Count,
} Prog_JobKind;

// File operations that Undo reverses: a rename goes back, new items and copies go to the trash, and moved items return.
typedef enum Prog_UndoKind {
    Prog_UndoKind_Rename,
    Prog_UndoKind_Create,
    Prog_UndoKind_Copy,
    Prog_UndoKind_Move,
    Prog_UndoKind_Trash,  // `to` the items sent to the bin, which undoing takes back out
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
    b8 plain;          // its type's icon at a thumbnail's size, which stands in while the thumbnail loads
    b8 cacheOnly;      // a thumbnail from the system's cache only, which a request to make it follows when there is none
    b8 slowDisk;       // on a spinning disk, which makes one thumbnail at a time rather than seek between several
    b8 warm;           // made for the system's cache only, for a file the list shows at its icon size, so that zooming in finds it there
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
    b32 smooth;  // its pictures are drawn at other sizes than its own, which it samples smoothly, each a half texel in from its cell's edge
} Prog_IconAtlas;

// Zoom levels, smallest first: Details, List, Columns, then icon grids that grow a step at a time.
// Zoom in tenths of a percent: Details at 0, List at 100 and Columns at 200, then the icon views from M Icons at 300 through L Icons from 650 to
// XL Icons at 1000, which a zoom step moves through 55 at a time.
#define Prog_ZoomList    100
#define Prog_ZoomColumns 200
#define Prog_ZoomIcons   300
#define Prog_ZoomLarge   650
#define Prog_ZoomMax     1000
#define Prog_ZoomStep    55

typedef enum Prog_View {
    Prog_View_Details,
    Prog_View_List,
    Prog_View_Columns,
    Prog_View_Icons,
    Prog_View_Count,
} Prog_View;

typedef struct Prog_Icon {
    enum8(Prog_IconState) state;
    b8 making;     // a thumbnail the system's cache lacked, waiting to be made, which the first frame does not wait for
    b8 reloading;  // loaded and asked for again, as the system's icons changed; it shows until the new one takes its cell
    b8 exact, plain;
    enum8(P_FileKind) kind;
    String path;   // what it was loaded from, kept in the icon arena, for loading it again
    R_TextureHandle texture;
    R_Rect uv;
    R_Rect pictureUv;  // a picture fitted to its cell's width: the rows it covers, and its width over its height; else `aspect` is 0
    f32 aspect;
    u64 wanted;    // the last frame that drew or waited for it, which orders the thumbnails still to be made and picks the cell a full atlas gives up
} Prog_Icon;

typedef struct Prog_IconMap { MapHeader header; struct { String key; Prog_Icon value; } *v; } Prog_IconMap;

// The thumbnails of one tier: their atlas's size is the tier, 0 for a slot not in use.
typedef struct Prog_ThumbnailSlot {
    Arena arena;
    Prog_IconMap map;
    Prog_IconAtlas atlas;
    u64 drawn;  // the frame it was last drawn in
} Prog_ThumbnailSlot;

#define Prog_ThumbnailSlots 3

// What a measure of a folder's tree has counted so far, readable while it runs; `pending` is the folders of it left to list, 0 once it is done.
typedef struct Prog_TreeTotals {
    atomic_uint_fast64_t folders, files, bytes;
    atomic_uint_fast64_t pending;
    atomic_bool failed;  // its folder could not be read
} Prog_TreeTotals;

typedef struct Prog_Partial Prog_Partial;

// A folder's disk usage, which the inspector's usage view shows: the folders below it by size, the largest files in each one, and the kinds of file by extension. Each folder keeps its Prog_UsageKeptFiles largest files by name; the rest count together as its smaller files.
#define Prog_UsageKeptFiles 32
typedef struct Prog_UsageFolder {
    String name;          // the root's is its whole path
    u32 parent;           // UINT32_MAX for the root, the first folder
    u32 firstChild;       // its largest folder, UINT32_MAX for none; the others follow by nextSibling, largest first
    u32 nextSibling;
    u32 firstFile;        // its largest files, fileCount of them in Prog_Usage.files, largest first
    u32 fileCount;
    u64 restFiles;        // its files too small to keep one by one, and their bytes
    u64 restBytes;
    u64 bytes;            // everything below it, at any depth
    u64 files, folders;
    i64 modified;         // the newest change below it
    b8 hidden, system;
    b8 unreadable;        // its listing failed, as without the rights to read it
} Prog_UsageFolder;
typedef struct Prog_UsageFile {
    String name;
    u64 bytes;
    u32 type;  // in Prog_Usage.types
} Prog_UsageFile;
// Files by extension, lowercase and without its dot; an empty one is the files without one. `name` is the system's name of the type.
typedef struct Prog_UsageType {
    String extension;
    String name;
    u64 bytes;
    u64 files;
} Prog_UsageType;
typedef struct Prog_Usage {
    Arena arena;  // holds everything here once it is taken from its job
    String path;
    Prog_UsageFolder *folders;
    u32 folderCount;
    Prog_UsageFile *files;
    u32 fileCount;
    Prog_UsageType *types;  // largest first
    u32 typeCount;
    u64 microseconds;  // how long the scan took
} Prog_Usage;
// What a scan has found so far, read while it runs.
typedef struct Prog_UsageProgress {
    atomic_uint_fast64_t folders, files, bytes;
} Prog_UsageProgress;
// A treemap's cell: a folder's frame, which its contents are drawn over, a file, or a folder's files too small to show or a folder too small to go into, which have no file (UINT32_MAX). `type` colors a file, UINT32_MAX for none.
typedef struct Prog_UsageCell {
    R_Rect rect;
    u32 folder;
    u32 file;
    u32 type;
    b8 frame;
    b8 rest;  // a folder's smaller files, rather than a folder too small to go into
} Prog_UsageCell;

// What a Transfer does with archives instead of copying: packs `paths` into the new archive `name` in `path`, or takes the archive `paths[0]`
// out into `path` itself or into the new folder `name` there.
typedef enum Prog_TransferOp {
    Prog_TransferOp_None,
    Prog_TransferOp_Pack,
    Prog_TransferOp_ExtractHere,
    Prog_TransferOp_ExtractToFolder,
    Prog_TransferOp_CompressVideo,  // encodes the videos `paths` again, each beside itself as "<name> (compressed).mp4"
    Prog_TransferOp_Download,       // downloads the video at the web address `paths[0]` into `path` with yt-dlp
    Prog_TransferOp_Torrent,        // downloads the torrent file `paths[0]` beside itself with aria2c, the files `name` chooses, as --select-file takes them, or all
    Prog_TransferOp_TorrentFetch,   // fetches the torrent `paths[0]`, a magnet link or a .torrent's web address, into `path` as a .torrent file
    Prog_TransferOp_Count,
} Prog_TransferOp;

typedef struct Prog_JobInput {
    enum8(Prog_JobKind) kind;
    enum8(Prog_TransferOp) transferOp;
    u8 archiveLevel;  // how hard what goes into an archive is compressed, 0 to 9
    u8 videoCodec, videoQuality;  // what a video is compressed to
    b32 downloadAudio;            // a download is the sound alone
    b32 downloadPlaylist;         // a video's address in a playlist downloads the whole playlist
    const atomic_uint *volume;    // what audio plays at, 0 to 100, read as it plays
    u64 panel;  // the panel that asked, whose results these are; 0 for work of the whole window
    String path;
    String password;  // a server's, which a mount tries
    u64 generation;
    enum8(Prog_Sort) sort;
    b32 descending;
    enum8(Prog_FolderSort) folderSort;
    b32 showHidden;
    b32 showSystem;
    b32 miller;            // FilesList: for a Miller column of `panel`, rather than the folder dropdown
    b32 explorer;          // Open: the folder `path` in the system's file manager
    b32 expanded;
    b32 tree;               // a tree: the folders `treeOpen` names, by their paths from `path`, are listed too
    String *treeOpen;
    u64 treeOpenCount;
    String *treeAll;        // and of those, the ones whose subfolders are all unfolded too
    u64 treeAllCount;
    Prog_Partial *partial;  // an expanded listing's rows found so far, handed over while its walk goes on
    b32 addHistory;
    u64 historyAt;
    ArrayStr drivesSeen;  // This PC's: the drives' paths in the order they first came, which it lists them in
    String driveRoot;  // the root of the drive `path` is on, which a gone folder is not left for; empty for the system's root
    b32 passing;  // gone to by Up or a crumb, passing through the folder rather than opening it, which the recents do not count
    // The names of the recents inside `path`, which are marked, and sorted first with sortRecents.
    String *recentNames;
    u32 recentCount;
    b32 sortRecents;
    // Rename `name` to `newName` inside `path`, which is `prefix` below the shown folder; WarmMenu uses `path` and `name`.
    String name;
    String newName;
    String prefix;
    // Measure the folders `names` inside `path`: the entries `rows` of listing `listing`. Shares asks the servers `paths` for theirs but the
    // shares `names`, rowCount of them, which network drives stand for.
    String *names;
    u64 *rows;
    u64 rowCount;
    u64 listing;
    // Transfer copies or moves `paths` into `path`, Trash trashes them, Delete deletes them for good, and Create makes `name` inside `path`, a folder when `folder` is set.
    // Save writes `name` to the file `path`.
    String *paths;
    u64 pathCount;
    u64 *sizes;            // TorrentProgress: each of `paths`' size
    u64 offset;            // HexPage: where its page starts in the file; PdfPage: the page; TextWindow: where it starts
    u64 line;              // TextWindow: the number of the line at `offset`, and what it starts inside of
    u8 open;
    b32 hex;               // Preview: the file's bytes, whatever it holds
    u64 size;              // Preview: the file's size; PdfPage: the width to draw it at
    b32 inTab;             // Reveal: shown in a new tab
    b32 move;
    b32 folder;
    b32 archive;           // Preview: an archive the inspector's panel lists, which the job counts
    b32 confirm;           // EmptyTrash and Delete: the system asks first
    u64 indexBase;         // Index: how many entries the index had, which the first of those found follows
    P_Progress *progress;  // Transfer: its slot's, or NULL without one
    Prog_Undo undo;        // Undo: the step it reverses
    b32 redo;              // Undo: the step came from the redo stack
    // Index lists `paths`, the index entries `entries`, keeps at most `room` of what it finds and adds that to the file `name` when there is one,
    // emptied first with `paths` as its roots when `restart` is set. IndexLoad reads the index file `name`; IndexFinish moves `path` over `name`.
    u32 *entries;
    u64 room;
    b32 restart;
    // Measure: each of `names`' totals so far.
    Prog_TreeTotals *trees;
    // Directory: what it has found so far, which the status bar shows while it walks a big tree; the totals count hidden items too.
    atomic_uint_fast64_t foundFolders, foundFiles, shownFolders, shownFiles;
    // Usage: what the scan has found so far.
    Prog_UsageProgress usage;
    // Icons
    Prog_IconRequest *icons;
    u64 iconCount;
    i32 iconSize;
    f32 iconScale;
} Prog_JobInput;

// A copy or move on its way, shown with its progress; the job writes `progress` in place, so the slot stays put until the job is taken back.
typedef struct Prog_Transfer {
    P_Job *job;          // NULL for a free slot
    P_Progress progress;
    b32 move;
    b32 opening;         // a file taken out of an archive to open it, rather than a copy
    b32 answerAll;       // the next answer about a taken name answers the rest too
    u64 started;         // quick ones finish before they would show
    u8 target[256];      // the target folder's name
    u32 targetLength;
    enum8(Prog_TransferOp) op;
    u64 sourceHash;      // a torrent's: the hash of its .torrent file's path, by which the inspector finds its download
    u64 targetHash;      // the hash of the target folder's path, whose panel shows the transfer's card
} Prog_Transfer;

#define Prog_MaxTransfers 4
// Torrents download in slots of their own after those, this many at once, and the rest wait in Torrents.conf's order.
#define Prog_TorrentsAtOnce 5
#define Prog_TransferSlots (Prog_MaxTransfers + Prog_TorrentsAtOnce)

// What the preview pane shows of a file: a picture of Prog_PreviewSize squared, or the start of its text.
#define Prog_PreviewSize 512
// How many of the recents the sidebar and Go To show.
#define Prog_ShownRecentCount 10
#define Prog_PreviewBytes (1 << 20)
// A text file is held whole up to Prog_TextWindow; a bigger one shows that much of itself at a time, its window, which moves along as the
// view does, and its lines are counted once in steps of Prog_TextStep for the numbers and the scroll bar of the whole file.
#define Prog_TextWindow (16 << 20)
// The folder dropdown command's target for This PC's crumb, which comes before a drive's.
#define Prog_ThisPCCrumb UINT64_MAX
#define Prog_NetworkCrumb (UINT64_MAX - 1)
#define Prog_TextStep (1 << 20)

// A big text file's lines in steps of Prog_TextStep bytes: where the first line starting in each step starts, its number, and what the
// highlighter is inside of there, for `count` steps, and the file's `lines`.
typedef struct Prog_TextIndex {
    u64 *offsets, *numbers;
    u8 *states;
    u64 count, lines;
    b32 done;
} Prog_TextIndex;

// Lines of text, `count` of them, each starting at its place in `lines` and the last ending at `lines[count]`, with what the highlighter is
// inside of at each start. A big file's lines its filter keeps, found through the whole file, have their numbers in it, from 0; those for
// the filter's version `version`, all of them unless `cut`, when they would have been more than a window holds.
typedef struct Prog_FoundLines {
    String text;
    u32 *lines;
    u8 *states;
    u64 *numbers;
    u32 count, version;
    b32 cut, done;
} Prog_FoundLines;
// The largest file inside an archive that is taken out to be previewed.
#define Prog_ArchivePreviewBytes (UINT64_C(256) << 20)
// A file shown as hex is read a page at a time as its rows come into sight, into Prog_HexSlots pages kept at once. The view holds up to
// Prog_HexRows rows of it at a time, which is as far as the precision of its float scroll offset goes, and moves them along as it scrolls.
// Up to Prog_HexCopyBytes of it are copied as hex.
#define Prog_HexPageBytes (1 << 20)
#define Prog_HexRows (1 << 20)
#define Prog_HexSlots 32
#define Prog_HexCopyBytes (4 << 20)
// The inspector's text size in points, unless changed.
#define Prog_InspectorFontSize 10
#define Prog_InspectorFontMin 8
#define Prog_InspectorFontMax 16

// A picture is the file's own at up to Prog_PreviewImageSide on its longer side when it is an image,
// else the system's thumbnail of Prog_PreviewSize squared.
#define Prog_PreviewImageSide 4096

// What git says of an item that is not as last committed, the folders by the most telling of what is inside them.
typedef enum Prog_GitMark {
    Prog_GitMark_None,
    Prog_GitMark_Untracked,
    Prog_GitMark_Added,
    Prog_GitMark_Modified,
    Prog_GitMark_Conflict,
    Prog_GitMark_Count,
} Prog_GitMark;

// The shown folder's items git marks, by their names in the listing, the folders on the way to an expanded listing's items included.
typedef struct Prog_GitMarks {
    MapHeader header;
    struct { String key; u8 value; } *v;
} Prog_GitMarks;

// A repository among the shown folder's subfolders: its branch, and how many tracked files it has changed, -1 until known.
typedef struct Prog_Repo {
    String branch;
    i32 changes;
    i32 ahead, behind;  // the commits to push and to pull, -1 while not known or without an upstream
} Prog_Repo;

typedef struct Prog_Repos {
    MapHeader header;
    struct { String key; Prog_Repo value; } *v;
} Prog_Repos;

// The git repository around a folder: its branch, or a detached HEAD's short hash, how many commits it is ahead of its upstream and behind
// it, -1 for each when that is not known, and the changes not yet committed, in all and in the folder.
typedef struct Prog_GitStatus {
    String branch;
    String root;     // the repository's folder
    String folder;   // the folder it was found for, which a panel's tabs share it between
    i32 ahead, behind;
    u32 changes;
    u32 counts[Prog_GitMark_Count];  // the changes by kind, a deletion counted as a change
    Prog_GitMarks marks;
} Prog_GitStatus;

// The languages the inspector colors the text of, by the file's extension.
typedef enum Prog_Syntax {
    Prog_Syntax_None,
    Prog_Syntax_C,         // C, C++, shaders
    Prog_Syntax_CSharp,
    Prog_Syntax_Script,    // JavaScript and TypeScript
    Prog_Syntax_Rust,
    Prog_Syntax_Go,
    Prog_Syntax_Java,      // Java and Kotlin
    Prog_Syntax_Python,
    Prog_Syntax_Lua,
    Prog_Syntax_Shell,
    Prog_Syntax_PowerShell,
    Prog_Syntax_Batch,     // Windows .bat and .cmd
    Prog_Syntax_Json,
    Prog_Syntax_Ini,       // INI, TOML, YAML and other key files
    Prog_Syntax_Markdown,
    Prog_Syntax_Markup,    // XML, HTML, SVG and the project files written in XML
    Prog_Syntax_CMake,
    Prog_Syntax_Make,      // Makefiles
    Prog_Syntax_Count,
} Prog_Syntax;

// What each byte of a line is, for its color.
typedef enum Prog_Token {
    Prog_Token_Plain,
    Prog_Token_Keyword,    // a keyword, a JSON key, a preprocessor line's directive
    Prog_Token_String,
    Prog_Token_Comment,
    Prog_Token_Number,
    Prog_Token_Heading,    // a Markdown heading, an INI section
    Prog_Token_Count,
} Prog_Token;

// A run of a model's triangles drawn alike: `count` indices from `first`, in `color`, straight RGBA, times the model's picture `image`
// unless that is UINT32_MAX. A reader gives only `first`, each run ending where the next starts, and a color of 0 for the default.
typedef struct Prog_MeshPart {
    u64 first, count;
    u32 color, image;  // the color's alpha is how much it hides what is behind it, with `blend`
    u32 bare;          // while read, the color for when its picture does not decode, 0 for `color`
    b32 blend;         // see-through by the color's, the vertices' and the picture's alphas, rather than solid with the picture's cut out
} Prog_MeshPart;

// A 3D model's mesh as the GPU draws it: positions, Y up, each vertex's texture coordinate, from a picture's top left, and straight RGBA
// color, three indices a triangle, all in range, in parts, with its pictures and its bounds' center and radius.
typedef struct Prog_ModelShape {
    f32 *positions;
    f32 *uvs;
    u32 *colors;
    u32 vertexCount;
    u32 *indices;
    u64 indexCount;
    Prog_MeshPart *parts;
    u32 partCount;
    P_Image *images;
    u32 imageCount;
    f32 center[3];
    f32 radius;
    b32 zUp;  // the file's Z was up, which the positions turned to Y
} Prog_ModelShape;

// A torrent's file: its path inside the torrent's folder, its size, and whether it is padding that lines the next file up with the pieces,
// which the inspector leaves out and nothing downloads.
typedef struct Prog_TorrentFile {
    String path;
    u64 size;
    b32 pad;
} Prog_TorrentFile;

// What a .torrent holds: its name and size, and its files in the order aria2c numbers them from 1; one of a single file, which its name names,
// downloads as that file rather than into a folder of its name.
typedef struct Prog_TorrentInfo {
    String name;
    u64 size;
    Prog_TorrentFile *files;
    u32 fileCount;
    b32 single;
} Prog_TorrentInfo;

// How far a torrent's files are, read off the disk: their bytes done by the pieces aria2c's control file marks downloaded while it has one,
// else which of them are there at all, as once the download is done.
typedef struct Prog_TorrentProgress {
    u64 *done;
    b8 *exists;
    b32 control;
} Prog_TorrentProgress;

typedef struct Prog_Preview {
    u32 *pixels;
    i32 width, height;              // of pixels
    i32 sourceWidth, sourceHeight;  // the image's own size, for an image
    b32 image;
    String text;
    b32 hex;                        // the text is the first page of a file that is neither text nor a picture, shown as hex
    b32 archive;                    // the text lists what an archive holds
    b32 document;                   // the text is a Word document's, whose prose wraps
    // An archive the inspector's panel lists: all it holds, not complete when there was more than was counted.
    P_DirectoryTotals archiveTotals;
    b32 archiveCounted;
    u32 pdfPages;                   // a PDF's pages, with each one's width and height in points
    f32 *pdfSizes;
    Prog_ModelShape model;          // a 3D model's mesh, when it has indices
    String media;                   // an audio or video file's details from ffprobe, a "label\tvalue" line each
    f64 seconds;                    // and its length
    Prog_TorrentInfo torrent;       // a .torrent's files; none for any other file
    // The text's lines, where each starts and one past its end, and with its syntax what each starts inside of; `windowed` when the text
    // is the start of a file too big to hold, read from `readPath`, its copy taken out when it is inside an archive.
    u32 *lines;
    u8 *lineStates;
    u32 lineCount;
    u8 syntax;
    b32 windowed;
    String readPath;
} Prog_Preview;

// Pages of a PDF read at most, and drawn ones kept at once, each in a texture of its own.
#define Prog_MaxPdfPages 5000
#define Prog_PdfDrawn 6

// A page of the inspector's PDF drawn `width` pixels wide; a {0} texture for a free slot.
typedef struct Prog_PdfPage {
    u32 page;
    i32 width, height;
    R_TextureHandle texture;
    u64 used;  // the last frame it was in sight, the least recent of which a new page replaces
} Prog_PdfPage;

// How the inspector sizes an image: shrunk to fit its view, at its own size, or at a scale set with the zoom commands.
typedef enum Prog_ImageScale {
    Prog_ImageScale_Fit,
    Prog_ImageScale_Original,
    Prog_ImageScale_Custom,
    Prog_ImageScale_Count,
} Prog_ImageScale;

// A copy or move's result: where each item went, for undoing it.
// A notice in the window's corner: an error stays until closed, a warning and an info fade after a while, and the same one again counts up.
typedef enum Prog_NoticeKind {
    Prog_NoticeKind_Info,
    Prog_NoticeKind_Warning,
    Prog_NoticeKind_Error,
    Prog_NoticeKind_Count,
} Prog_NoticeKind;

typedef struct Prog_Notice {
    u64 id;       // keys its card and its buttons' commands
    u64 since;    // when it came, or came again, or was last under the pointer, which its fading counts from
    enum8(Prog_NoticeKind) kind;
    u32 count;
    u32 titleLength, messageLength, keyLength;
    u8 key[48];   // what kind of notice it is, as "rename.failed", which muting it keeps out from then on
    u8 title[160];
    u8 message[640];
    // A button under the message that runs `action` with the notice as its row; a notice with one stays until it is used or closed.
    u16 action;
    u32 actionLength;
    u8 actionLabel[32];
} Prog_Notice;

enum { Prog_MaxNotices = 5 };

typedef struct Prog_Transferred {
    P_FileError error;  // first, as for the other file operations' results
    ArrayStr placed;
    u64 sizeBefore, sizeAfter;  // the compressed videos' bytes before and after
} Prog_Transferred;

typedef struct Prog_Created {
    P_FileError error;
    String name;
} Prog_Created;

// The latest release GitHub gives: its version, the tag without the v, its page and the archive of this system's build. `fetched` once GitHub
// answered, and `notify` while it is newer than this build and was not offered before.
typedef struct Prog_LatestRelease {
    String version;
    String page;
    String asset;
    b32 fetched;
    b32 notify;
} Prog_LatestRelease;

typedef struct Prog_Places {
    P_ArrayPlace places;
    P_ArrayPlace drives;
    ArrayStr roots;
    ArrayStr searched;  // P_SearchPlaces
    ArrayStr missing;   // the bookmarks whose folders are gone
} Prog_Places;

typedef enum Prog_PaletteMode {
    Prog_PaletteMode_Commands,
    Prog_PaletteMode_Tabs,
    Prog_PaletteMode_GoTo,
    Prog_PaletteMode_Options,
    Prog_PaletteMode_Layouts,
    Prog_PaletteMode_Fonts,
    Prog_PaletteMode_Count,
} Prog_PaletteMode;

// The options the Options popup lists, each kept in Settings.conf.
typedef enum Prog_Option {
    Prog_Option_Language,
    Prog_Option_Font,
    Prog_Option_FontSize,
    Prog_Option_InspectorFont,
    Prog_Option_InspectorFontSize,
    Prog_Option_Borders,
    Prog_Option_Spacing,
    Prog_Option_Animations,
    Prog_Option_RoundCorners,
    Prog_Option_InlineSubmenus,
    Prog_Option_Tooltips,
    Prog_Option_SystemScheme,
    Prog_Option_ColorScheme,
    Prog_Option_LightScheme,
    Prog_Option_DarkScheme,
    Prog_Option_ColorGroups,
    Prog_Option_ColorClear,
    Prog_Option_ColorCaption,
    Prog_Option_ColorBackground,
    Prog_Option_ColorSurface,
    Prog_Option_ColorForeground,
    Prog_Option_ColorInner,
    Prog_Option_ColorBorder,
    Prog_Option_ColorOutline,
    Prog_Option_ColorSeparator,
    Prog_Option_ColorAlternatingRow,
    Prog_Option_ColorIconTint,
    Prog_Option_ColorText,
    Prog_Option_ColorSecondary,
    Prog_Option_ColorGroup,
    Prog_Option_ColorFile,
    Prog_Option_ColorFolder,
    Prog_Option_ColorWarning,
    Prog_Option_ColorProgress,
    Prog_Option_ColorSelection,
    Prog_Option_ColorRectSelection,
    Prog_Option_ColorMatch,
    Prog_Option_ColorHidden,
    Prog_Option_ColorHover,
    Prog_Option_ColorDisabled,
    Prog_Option_ColorContentHover,
    Prog_Option_ColorContentSelection,
    Prog_Option_ColorContentDisabledSelection,
    Prog_Option_ColorOutlineHover,
    Prog_Option_ColorOutlineSelection,
    Prog_Option_ColorOutlineDisabledSelection,
    Prog_Option_ColorMatchHover,
    Prog_Option_ColorMatchSelection,
    Prog_Option_ColorMatchDisabledSelection,
    // The inspector's code, past the reference's groups.
    Prog_Option_ColorCodeKeyword,
    Prog_Option_ColorCodeString,
    Prog_Option_ColorCodeNumber,
    Prog_Option_ColorCodeHeading,
    Prog_Option_AlternatingRows,
    Prog_Option_InactiveOpacity,
    Prog_Option_ShowFileExtension,
    Prog_Option_ShowHiddenFiles,
    Prog_Option_ShowSystemFiles,
    Prog_Option_LongPaths,
    Prog_Option_ShortenedPaths,
    Prog_Option_Breadcrumbs,
    Prog_Option_ShowCheckboxes,
    Prog_Option_ConfirmEmptyTrash,
    Prog_Option_GitFetch,
    Prog_Option_CompressionLevel,
    Prog_Option_VideoCodec,
    Prog_Option_VideoQuality,
    Prog_Option_DownloadAudio,
    Prog_Option_DownloadPlaylist,
    Prog_Option_DeleteFinishedTorrents,
    Prog_Option_AudioVolume,
    Prog_Option_Notices,
    Prog_Option_MutedNotices,
    Prog_Option_NoticesInSidebar,
    Prog_Option_CheckUpdates,
    Prog_Option_StoreRecents,
    Prog_Option_ShowRecents,
    Prog_Option_SortRecents,
    Prog_Option_LoadThumbnails,
    Prog_Option_CalcFolderSizes,
    Prog_Option_CalcFolderSizesSSD,
    Prog_Option_CalcFolderSizesHDD,
    Prog_Option_CalcFolderSizesNetwork,
    Prog_Option_ActOnPress,
    Prog_Option_SingleClick,
    Prog_Option_SingleClickFoldersOnly,
    Prog_Option_HoverToSelect,
    Prog_Option_HoverToFocus,
    Prog_Option_UnderlineOnHover,
    Prog_Option_DoubleClickToParent,
    Prog_Option_ClickToRename,
    Prog_Option_Overscroll,
    Prog_Option_ReverseScroll,
    Prog_Option_CharacterKeyAction,
    Prog_Option_InspectorLayout,
    Prog_Option_HighlightCode,
    Prog_Option_LoadOnStartup,
    Prog_Option_NewTabShow,
    Prog_Option_NewTabView,
    Prog_Option_SingleWindow,
    Prog_Option_VSync,
    Prog_Option_Transparency,
    Prog_Option_WindowOpacity,
    Prog_Option_PopupBlur,
    Prog_Option_PopupOpacity,
    Prog_Option_Backdrop,
    Prog_Option_Count,
} Prog_Option;

// The Options popup's rows: the options, and while nothing is typed, the headings of their sections, as Prog_Option_Count and past it.
enum { Prog_OptionSectionCount = 8, Prog_OptionRows = Prog_Option_Count + Prog_OptionSectionCount };
static_assert(Prog_OptionRows <= 256, "the Options rows fit a byte");
function b32 Prog_OptionHeading(u8 row);
function u32 Prog_OptionSection(u32 option);

typedef enum Prog_OptionControl {
    Prog_OptionControl_Switch,
    Prog_OptionControl_Stepper,
    Prog_OptionControl_Slider,
    Prog_OptionControl_Choice,
    Prog_OptionControl_Font,  // a font family's name, chosen from the installed ones
    Prog_OptionControl_Expand,  // opens and closes the rows after it
    Prog_OptionControl_Color,   // one of the scheme's colors, which the color picker changes
    Prog_OptionControl_Muted,   // how many kinds of notice are muted, and a button that shows them again
    Prog_OptionControl_Count,
} Prog_OptionControl;

typedef struct Prog_OptionDesc {
    String label;
    String tooltip;
    enum8(Prog_OptionControl) control;
    i32 least, most;         // a stepper's or slider's values; a choice's are its names, from 0
    const String *choices;   // the names, as Settings.conf has them, of a choice's values
    u32 choiceCount;
    String setting;          // its name in Settings.conf
} Prog_OptionDesc;

// What a start without folders to open shows: the last run's panels and tabs, This PC or the home folder.
typedef enum Prog_Startup {
    Prog_Startup_LastSession,
    Prog_Startup_Root,
    Prog_Startup_Home,
    Prog_Startup_Count,
} Prog_Startup;

// What a new tab shows: This PC, the home folder or the folder the current tab shows.
typedef enum Prog_NewTab {
    Prog_NewTab_Root,
    Prog_NewTab_Home,
    Prog_NewTab_Current,
    Prog_NewTab_Count,
} Prog_NewTab;

typedef struct Prog_RecentRank {
    u32 ordinal;  // when it was last opened
    u8 score;     // 0 to 10, up one each time it is opened
    u8 decay;     // listings that showed it since it was last opened, up to 10
    b8 file;      // a file opened from a list, which only the listings mark
    b8 hidden;    // as the list marks it when it was last opened, which dims it as the list does
} Prog_RecentRank;

// How long the recents are kept: across runs, until the app closes, or not at all.
typedef enum Prog_StoreRecents {
    Prog_StoreRecents_Persistent,
    Prog_StoreRecents_Session,
    Prog_StoreRecents_Never,
    Prog_StoreRecents_Count,
} Prog_StoreRecents;

// Which folders' icon views show their files' thumbnails; those that do not show the files' type icons.
typedef enum Prog_Thumbnails {
    Prog_Thumbnails_LocalDisk,
    Prog_Thumbnails_Never,
    Prog_Thumbnails_Always,
    Prog_Thumbnails_Count,
} Prog_Thumbnails;

// Where the inspector goes: to the right of the list or below it, by the panel's shape for Auto.
typedef enum Prog_InspectorLayout {
    Prog_InspectorLayout_Auto,
    Prog_InspectorLayout_Right,
    Prog_InspectorLayout_Below,
    Prog_InspectorLayout_Count,
} Prog_InspectorLayout;

// The groups the command palette files its commands under.
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
    enum16(Prog_CommandKind) kind;
} Prog_PaletteRow;

enum { Prog_MaxPaletteRows = (u32)Prog_CommandKind_Count + (u32)Prog_CommandGroup_Count };

// Go To's rows come from the sidebar's groups, each section under a title that folds it, or from the folder typed. A group of the user's
// own, g, is listed as the section Prog_GoToSection_Count + g.
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

// Listed: a folder whose items the index has already, read with its whole drive from the file system's table, which the walk does not list.
// Gone: an item no longer there when its folder was listed again, which search leaves out with what was inside it.
enum { Prog_IndexFlag_Hidden = 1, Prog_IndexFlag_File = 2, Prog_IndexFlag_Listed = 4, Prog_IndexFlag_Gone = 8 };

// Every folder and file on the drives, each as its name inside the entry of its parent, so that a few million fit; a root's name is its whole path.
// Each name is packed as its length in two bytes and then its bytes, in `arena` or in the saved index file read into it.
typedef struct Prog_Index {
    Arena arena;
    Arena tables;           // names, parents and flags, made for Prog_MaxIndexed entries at once so that they never move and leave copies behind
    const u8 **names;
    u32 *parents;           // UINT32_MAX for a root
    u8 *flags;              // Prog_IndexFlag_
    u64 count;
    // Once it is whole: each entry's first item and the next item in its folder, UINT32_MAX for none, so that a folder listed again
    // brings its items up to date.
    u32 *firstChild, *nextSibling;
} Prog_Index;

// What was found inside the first `listed` of an index job's folders, in order, each with the entry of the folder it is in.
// IndexLoad hands back a whole index the same way, `count` entries in its own arena.
typedef struct Prog_Indexed {
    String *names;
    const u8 **packed;  // IndexLoad: the names, in the file read
    b32 fresh;          // IndexLoad: the file was written less than Prog_IndexFreshSeconds ago
    Prog_Index whole;   // IndexLoad: then the index made of it, linked, to serve as it is
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

// A folder dropdown's listing: folders, then files, each by name.
// What the layouts ask before they go on: overwriting a saved layout, or deleting one.
typedef enum Prog_LayoutAsk {
    Prog_LayoutAsk_None,
    Prog_LayoutAsk_Overwrite,
    Prog_LayoutAsk_Delete,
    Prog_LayoutAsk_DeleteScheme,
    Prog_LayoutAsk_SwitchBranch,  // switching branch with changes not committed, which go along
    Prog_LayoutAsk_Count,
} Prog_LayoutAsk;

#define Prog_MaxLayouts 64
// Default and the reference's six, which the custom schemes follow.
#define Prog_BuiltInSchemes 7
// The first built-in scheme, shown where the settings name none, and the light one that "light" names.
#define Prog_DefaultScheme "filey-dark"
#define Prog_LightScheme 2
// A custom scheme's file in the settings folder ends so, after its name.
#define Prog_SchemeExtension S(".scheme")
// The colors a scheme is made of, its groups, as the Options list them from Prog_Option_ColorClear on.
#define Prog_ColorGroupCount 37

// The installed font families, sorted by name and each once.
typedef struct Prog_FontsListed {
    String *names;
    u64 count;
} Prog_FontsListed;

typedef struct Prog_FilesListed {
    Prog_Entry *items;
    u64 count;
} Prog_FilesListed;

// A folder a Miller column lists beside the panel's own list: one of the shown folder's parents, or the folder the focus is on.
typedef struct Prog_MillerColumn {
    Arena arena;        // its items', the job's taken over
    Arena pathArena;
    String path;
    P_Job *job;         // its listing on its way
    Prog_Entry *items;
    u64 count;
    b32 listed;
    b32 revealed;       // its lit row was scrolled into sight since it came into view
    b32 hidden;         // it was listed with hidden items shown
    u64 listedAt;       // when its listing arrived; one shown again after a while is listed again
    u64 usedFrame;      // the frame it was last shown in; the one unused longest is listed over
    P_DirectoryWatch watch;  // its folder's, from its first listing, which lists it again when it changes
    b32 stale;
} Prog_MillerColumn;

#define Prog_MillerSlots 8
typedef struct Prog_MillerColumns {
    Prog_MillerColumn v[Prog_MillerSlots];
} Prog_MillerColumns;

// Sidebar sections, in display order.
typedef enum Prog_Group {
    Prog_Group_Recents,
    Prog_Group_Bookmarks,
    Prog_Group_Storage,
    Prog_Group_Places,
    Prog_Group_Count,
} Prog_Group;

// The user's own sidebar groups, numbered after the built-in ones: a name, folders by path, and whether it is folded.
#define Prog_MaxUserGroups 16
#define Prog_SidebarGroupMax (Prog_Group_Count + Prog_MaxUserGroups)
typedef struct Prog_UserGroup {
    String name;
    ArrayStr folders;
    b32 closed;
} Prog_UserGroup;

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
// The deepest folder a tab has visited below the one it shows, whose crumbs go on past the shown folder's.
typedef struct Prog_Trail {
    u32 length;
    char text[1024];
} Prog_Trail;

// A font family's name from the settings, empty for the default.
typedef struct Prog_FontName {
    u32 length;
    char text[64];
} Prog_FontName;

// The families the UI and the inspector are drawn in without a font of the settings' own.
#define Prog_DefaultFont S("Segoe UI")
#if defined(_WIN32)
#define Prog_DefaultInspectorFont S("Consolas")
#else
#define Prog_DefaultInspectorFont S("monospace")
#endif

// The file types a listing shows, by the names its Type column gives them, each ended by '\n'; empty shows every type.
// ponytail: names up to a kilobyte in all, far more than a folder's types; grow it if one runs out.
typedef struct Prog_TypeFilter {
    u8 names[1024];
    u32 length;
} Prog_TypeFilter;

// ponytail: one step of undo, which Redo takes back; keep a stack of steps if one falls short.
typedef struct Prog_TextEdit {
    u8 text[1024];
    u32 length;
    u32 cursor;
    u32 anchor;
    u32 version;  // bumped by every change to the text
    // The text before the last change, a run of typing counting as one, or once undone the text the undo replaced.
    u8 other[1024];
    u32 otherLength;
    u32 otherCursor;
    b8 undoable;
    b8 undone;    // Redo swaps it back
    b8 typing;    // the last change typed a character, which the next typed one joins
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
    b32 gone;  // the folder and every one above it are gone, as on a drive taken away
    enum8(Prog_Sort) sort;
    b32 descending;
    enum8(Prog_FolderSort) folderSort;
    b32 showHidden;
    enum8(P_DriveKind) driveKind;
    u64 recentCount;  // entries among the recents
    b32 hidden;       // the folder itself, as P_DirectoryResult says
    // Every subfolder's items are listed too, named by their path below `path` with '/' separators.
    b32 expanded;
    // A tree: the unfolded folders' items too, named so, each right after its folder.
    b32 tree;
    ArrayStr treeAdded;  // the folders unfolded with all inside one, which the tree keeps unfolded
    b32 archive;      // a folder inside an archive, listed from it
    b32 locked;       // and the archive has encrypted entries
    // The folders their entries are in, each once, for what is worked out per folder rather than per entry.
    String *parents;
    u32 parentCount;
} Prog_Directory;

// The rows of an expanded listing found so far: the latest ones its job has handed over and the window not yet taken, in their own arena.
struct Prog_Partial {
    P_Mutex mutex;
    b32 ready;
    Arena arena;
    Prog_Directory directory;
};

// A 3D model a tab's inspector shows, turned by dragging and zoomed by the wheel: its mesh on the GPU, its parts and its pictures' textures,
// in previewArena, its bounds, whether the file's Z was up, the view's turn about the up axis and tip toward the view in radians and its
// zoom, the rotation as last drawn, which the axes in its corner follow, and the texture it is drawn into, redrawn as the view or its size
// changes.
typedef struct Prog_ModelView {
    R_MeshHandle mesh;
    R_MeshPart *parts;
    u32 partCount;
    // The see-through parts' triangles, in the parts' order, and each one's middle, put in order from the farthest each time it is drawn.
    u32 *seeIndices;
    f32 *seeMiddles;
    u64 seeTriangles;
    R_TextureHandle *images;
    u32 imageCount;
    f32 center[3], radius;
    b32 zUp;
    f32 turn[3][3];
    f32 yaw, pitch, zoom;                 // the turn and zoom asked for
    f32 shownYaw, shownPitch, shownZoom;  // and those shown, easing to them
    UI_Vec2 dragFrom;
    R_TextureHandle texture;
    i32 textureWidth, textureHeight;
    f32 drawn[3];
    b32 stale;
} Prog_ModelView;

// A PDF a tab's inspector scrolls through: its pages' sizes in points, in previewArena, the pages in sight drawn the view's width wide, and
// the page on its way, one at a time.
typedef struct Prog_PdfView {
    u32 pageCount;
    f32 *sizes;
    Prog_PdfPage drawn[Prog_PdfDrawn];
    P_Job *job;
} Prog_PdfView;

// The inspector each tab has of its own, beside its list while shown, previewing its focused item, so that tabs of one panel and other panels
// each keep theirs:
//   previewShown: open; previewPath: the item shown or on its way, loaded by previewJob into previewArena; previewText, previewTexture: what it is
//   previewPicture: the texture holds previewPath's picture, previewTextureWidth by previewTextureHeight, an image file's with its pixels kept
//     in previewPixels, for the readout of the one under the pointer; previewImage: it is an image file's own,
//     previewImageWidth by previewImageHeight; previewFolder: a folder, which the inspector's panel lists
//   previewHex: the file shows as hex from hexPages, its pages kept, NULL for the rest, hexPageCount of them out of hexSize bytes, each in
//     one of the hexSlots, which hold the pages hexSlotPage says, in previewArena; hexPageJob the page on its way, one at a time; hexBase the
//     byte the view's rows start from; previewAnchor to previewCaret the bytes selected, from the one pressed, hexPress; hexFindJob a find
//     on its way, and hexJumpTo the place it found, which the view goes to
//   previewArchive: previewText lists an archive's contents; previewArchiveTotals, once previewArchiveCounted, what the inspector's panel lists of
//     one; previewStamp: its size and modified time as counted, so that a change counts it again
//   previewDocument: previewText is a Word document's, which wraps unless documentUnwrapped; previewSubmitted: when the job started, so that a quick
//     one shows no progress
//   imageScaleMode, imageScale (for Custom), imageFit (the scale Fit last showed, where the zoom commands start from and stop going down),
//     imagePan (of the image's center from the view's, in pixels) and imageDragFrom: the image view
//   previewLines: where each line of previewText starts, and one past its end; previewSyntax, previewLineStates: its code coloring, what each line
//     starts inside of; previewRows: the first wrapped row of each line, and the total, at previewRowColumns characters across
//   previewAnchor to previewCaret: the text selected, as offsets into previewText, previewClicks those of the press that made it, previewTextFocus
//     pressed on last, so that Select All selects it; inspectorTextRect: the text view's last layout, a press outside which drops the selection
//   inspectorResetScroll: a new file starts at its top
//   torrent, torrentSkip: a .torrent's files, in previewArena, and those unticked, which its download leaves out; torrentDone: each file's bytes
//     downloaded, NULL until known, torrentRead: asked for once already
//   inspectorFilter, inspectorFiltering: the text view's filter, with the keyboard in its box, which keeps previewKept, previewKeptCount lines,
//     picked for its version previewKeptVersion
//   previewWindowed: previewText is the window of a text file too big to hold, previewSize bytes, read from textReadPath: its bytes from
//     previewOffset, the lines from previewFirstLine, in textWindowArena; textWindowJob reads the next window, from textWindowFrom, and
//     with textKeepTop, once it is in place, the same line stays at the top, or with textJump it starts at its top, gone to from the scroll
//     bar; textIndex, from textIndexJob, in textIndexArena, counts the whole file's lines; textTopLine and
//     textTopPart: the line at the view's top and how far into it, which a new window keeps there, its rows wrapped or not
//   textFind: the lines the filter keeps out of the whole of a big file, from textFindJob, asked for the filter's version textFindAsked, in
//     textFindArena; textFound: they show in place of the window, kept meanwhile in textSaved and textSavedScroll, and previewNumbers is
//     their lines' numbers
#define Prog_PreviewFields(X) \
    X(b32, inspectorDetails) X(b32, inspectorDetailsFolded) X(b32, previewShown) X(Arena, previewArena) X(String, previewPath) X(P_Job *, previewJob) X(String, previewText) X(String, previewMedia) X(f64, previewSeconds) X(R_TextureHandle, previewTexture) \
    X(Prog_ModelView, model) X(Prog_PdfView, pdf) \
    X(b32, previewPicture) X(i32, previewTextureWidth) X(i32, previewTextureHeight) X(b32, previewImage) X(i32, previewImageWidth) X(i32, previewImageHeight) \
    X(u32 *, previewPixels) \
    X(b32, previewFolder) X(b32, previewHex) X(u64, hexSize) X(u8 **, hexPages) X(u64, hexPageCount) X(P_Job *, hexPageJob) \
    X(u8 **, hexSlots) X(u64 *, hexSlotPage) X(u64, hexBase) X(u64, hexPress) X(P_Job *, hexFindJob) X(u64, hexJumpTo) \
    X(b32, previewArchive) X(b32, previewDocument) X(P_DirectoryTotals, previewArchiveTotals) X(b32, previewArchiveCounted) X(u64, previewStamp) \
    X(u64, previewSubmitted) \
    X(enum8(Prog_ImageScale), imageScaleMode) X(f32, imageScale) X(f32, imageFit) X(UI_Vec2, imagePan) X(UI_Vec2, imageDragFrom) \
    X(u32 *, previewLines) X(enum8(Prog_Syntax), previewSyntax) X(u8 *, previewLineStates) X(u32, previewLineCount) X(u32 *, previewRows) \
    X(u32, previewRowColumns) X(u64, previewAnchor) X(u64, previewCaret) X(u32, previewClicks) X(b32, previewTextFocus) X(R_Rect, inspectorTextRect) \
    X(b32, inspectorResetScroll) X(Prog_TextEdit, inspectorFilter) X(b32, inspectorFiltering) X(u32 *, previewKept) X(u32, previewKeptCount) \
    X(u32, previewKeptVersion) X(Prog_TorrentInfo, torrent) X(b8 *, torrentSkip) X(u64 *, torrentDone) X(b32, torrentRead) \
    X(b32, previewWindowed) X(u64, previewSize) X(u64, previewOffset) X(u64, previewFirstLine) X(String, textReadPath) X(Arena, textWindowArena) \
    X(P_Job *, textWindowJob) X(u64, textWindowFrom) X(b32, textKeepTop) X(b32, textJump) X(Prog_TextIndex, textIndex) X(P_Job *, textIndexJob) X(Arena, textIndexArena) \
    X(u64, textTopLine) X(f32, textTopPart) X(Prog_FoundLines, textFind) X(P_Job *, textFindJob) X(u32, textFindAsked) X(Arena, textFindArena) X(b32, textFound) \
    X(Prog_FoundLines, textSaved) X(f32, textSavedScroll) X(u64 *, previewNumbers)

// The Prog_State fields each tab has its own of.
// The shown tab's live in Prog_State; switching swaps them with the tab's slot, so a tab keeps its listing, selection, filter, scroll and view:
// zoom, up to Prog_ZoomMax, the details columns (columnSet, see Prog_PanelColumns), and while a locked folder's view applies, the tab's own
// (ownView, ownSettings), which the next folder without one gets back.
// The filter keeps the rows it lets through in `view`, best match first and in listing order within a score; rows it hides are never selected, so commands cannot act on them.
// With groupBy the view runs group after group, as `groups` says, and the groups folded away, by their labels in groupsFolded, leave their items out of it.
// treeAll names the folders the next listing unfolds with everything inside them, which it takes.
#define Prog_TabFields(X) \
    X(Arena, directoryArena) X(Prog_Directory, directory) X(Arena, historyArena) X(ArrayStr, history) X(u64, historyAt) \
    X(u64, focusedRow) X(u64, anchorRow) X(u64, selectedCount) X(b32, hasFocus) X(b32, hasAnchor) \
    X(b32, showHidden) X(enum8(Prog_Sort), sort) X(b32, descending) X(enum8(Prog_FolderSort), folderSort) X(u32, zoom) X(Prog_ColumnSet, columnSet) \
    X(b32, ownView) X(Prog_ViewSettings, ownSettings) \
    X(Prog_TextEdit, filter) X(b32, filterSticky) X(b32, expanded) X(b32, tree) X(Arena, treeArena) X(ArrayStr, treeOpen) X(Arena, treeAllArena) X(ArrayStr, treeAll) X(b32, treeNested) X(enum8(Prog_GroupBy), groupBy) X(Arena, groupArena) X(ArrayStr, groupsFolded) X(b32, hideFolders) X(b32, hideFiles) X(Prog_TypeFilter, types) \
    X(Arena, viewArena) X(u64 *, view) X(u64 *, viewAt) X(u64, viewCount) X(u64, viewFolders) X(Prog_ListGroup *, groups) X(u32, groupCount) X(UI_Vec2, scroll) X(Prog_Trail, trail) \
    Prog_PreviewFields(X)

#define Prog__TabField(type, name) type name;
typedef struct Prog_Tab {
    u64 id;  // keys the tab's blocks, so a drag follows it across slots
    Prog_TabFields(Prog__TabField)
} Prog_Tab;
#undef Prog__TabField

// ponytail: fixed tab count; grow the array if 32 tabs per panel falls short.
#define Prog_MaxTabs 32

// The watches of a tree's unfolded folders, each a thread of its own, so only the first few are watched.
#define Prog_TreeWatchMax 16
typedef struct Prog_TreeWatches {
    P_DirectoryWatch v[Prog_TreeWatchMax];
    u32 count;
} Prog_TreeWatches;

// The Prog_State fields each panel has its own of, besides its tabs' (Prog_TabFields).
// The current panel's live in Prog_State; Prog__UsePanel swaps them with the panel's slot, so every panel keeps its own listing, jobs, watch and layout.
//   error: the last failure, in errorArena; directoryJob, generation, loading: the listing on its way
//   revealFocus, centerFocus, resetScroll, restoreScroll: scroll changes the next build makes; revealHeight: the list's height a reveal was made in, which
//   reveals again while it changes; pageRows: lines visible in the last layout, for Page Up/Down
//   tabs, tabCount, activeTab: the panel's Prog_MaxTabs tab slots, in tabArena from the first use; the shown one's slot holds nothing while it is shown
//   gridColumns, gridPitch, gridItem: the list's last layout, in its coordinates (x from its left edge, y from the top of its content):
//     items per line (1 in Details and List), the step from one cell to the next, and the first cell's item box; groupHeading: how tall a group's heading was
//   marquee, marqueeKey, marqueeAnchor, marqueeBox, marqueeAdds: the box selection; the key is the block the drag began on, kept after it so that its release is not a click
//   filtering: the filter box has the keyboard; filterRect: its last layout
//   listing, measureJob, sortPending: folder totals; each adopted listing gets a new id, and totals that arrive while a menu or drag holds the rows still sort and group later
//   watch, watchDirty: the shown folder's watch, and a change it saw that waits for the listing under way; treeWatches: its tree's unfolded folders'
//   focusedRowRect: last layout of the focused row, to place a keyboard-opened menu
//   renameCreatedUntil: until then, select the one item a listing of this folder adds, and rename it with createdRenames
//   panelRect: the panel's last layout, for moving between panels with the keyboard
//   listStarted, listTook: when the last listing was asked for and, once it arrived, how long it took
//   statusRight: how wide the status bar's counts, view and zoom were in the last layout, which the filter box leaves room for
//   listScrolls: its items ran past the bottom in the last layout
//   inspectorView: how its inspector lists folders, kept while the inspector's panel is closed
//   miller, millerColumns: Miller columns are on, and the folders they list, made in millerArena once they first show
//   pickNames, pickScroll: a moved tab's selection, in pickArena, and its scroll, which its first listing here takes
#define Prog_PanelFields(X) \
    X(Arena, errorArena) X(String, error) X(b32, errorListing) X(P_Job *, directoryJob) X(u64, generation) X(b32, loading) \
    X(b32, revealFocus) X(b32, centerFocus) X(b32, resetScroll) X(b32, restoreScroll) X(f32, revealHeight) X(u64, pageRows) \
    X(Arena, tabArena) X(Prog_Tab *, tabs) X(u32, tabCount) X(u32, activeTab) \
    X(u64, gridColumns) X(UI_Vec2, gridPitch) X(R_Rect, gridItem) X(f32, groupHeading) \
    X(b32, marquee) X(UI_Key, marqueeKey) X(UI_Vec2, marqueeAnchor) X(R_Rect, marqueeBox) X(KeyMods, marqueeAdds) \
    X(b32, filtering) X(R_Rect, filterRect) X(u64, listing) X(P_Job *, measureJob) X(b32, sortPending) \
    X(P_DirectoryWatch, watch) X(b32, watchDirty) X(Prog_TreeWatches, treeWatches) X(R_Rect, focusedRowRect) X(u64, renameCreatedUntil) X(b32, createdRenames) X(R_Rect, panelRect)     X(u64, listStarted) X(u64, listTook) \
    X(f32, statusRight) X(b32, listScrolls) \
    X(Prog_ViewSettings, inspectorView) \
    X(Arena, gitArena) X(Prog_GitStatus, git) X(P_Job *, gitJob) X(b32, gitAgain) X(u64, gitTime) \
    X(P_DirectoryWatch, gitWatch) X(P_DirectoryWatch, gitDirWatch) X(u64, gitWatched) X(b32, gitDirty) \
    X(Arena, repoArena) X(Prog_Repos, repos) X(String, reposPath) X(P_Job *, reposJob) X(u64, reposNext) X(u64, reposTime) \
    X(b32, miller) X(Arena, millerArena) X(Prog_MillerColumns *, millerColumns) \
    X(Arena, pickArena) X(ArrayStr, pickNames) X(f32, pickScroll) X(b32, pickScrollSet) \
    X(u64, pathFlashTime) X(u32, columnDrag) X(UI_Key, columnDragKey) X(f32, columnGrab) X(u32, columnDropAt) X(u64, columnSlideUntil)

#define Prog__PanelField(type, name) type name;
typedef struct Prog_Panel {
    u64 id;  // 0 for a free slot; keys the panel's blocks and tags its commands and jobs
    u64 splitMicroseconds;  // when a split made it, which it zooms in from; 0 for the panels the window starts with
    u64 focusMicroseconds;  // when it last became or stopped being the active panel, which its status bar slides and its dimming fades from; 0 while settled
    // The panel whose inspector lists a folder in this one, which no split holds; 0 for the layout's panels.
    u64 inspectorOf;
    R_Rect inspectorRect;  // its inspector's in the last layout; empty while it is hidden
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
// How narrow and wide dragging its edge makes the sidebar, in logical pixels.
#define Prog_SidebarMin 120.0f
#define Prog_SidebarMax 480.0f
// Its width before its edge is dragged.
#define Prog_SidebarDefault 179.2f

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
    // Thumbnails, which thumbnailers can take seconds a file to make, have workers of their own, so that listings, file operations and icons
    // never wait behind them.
    Arena thumbnailJobsArena;
    P_JobQueue thumbnailJobs;
    // The programs run for minutes or hours, the audio player, downloads, video compression and git's fetches and pulls, have workers of their own too:
    // one for each transfer slot, two players, as one stopped may still be ending, and a fetch and a pull of two repositories.
    Arena toolJobsArena;
    P_JobQueue toolJobs;
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
    UI_Vec2 focusMouse;       // where the pointer was in the last build, whose moves hand the panel under it the keyboard
    // A dragged tab, whose block is `tabDragKey`; the build finds `tabDrop`, where it would go, and the release drops it there.
    b32 tabDrag;
    b32 tabDragCancelled;  // Escape ended the drag; the held tab does not start another until it is let go
    UI_Key tabDragKey;
    Prog_TabDropTarget tabDrop;
    Prog_TabDropTarget tabDropped;  // the drop that TabDrop carries out
    Arena closedArena;        // paths of closed tabs, for Reopen Closed Tab
    ArrayStr closedTabs;
    b8 closedLastTab;  // the window closes with its last tab, which the session saves as a new tab, as the settings open one
    // File icons, keyed by P_FileIconKey.
    // The map and keys live in iconArena; atlas pages survive resets and are reused.
    Arena iconArena;
    Prog_IconMap icons;
    Prog_ArrayIconRequest iconRequests;  // queued while building UI, submitted by Prog_ProcessCommands
    // Thumbnails the system's cache did not have, waiting for a thumbnail job, and how many of those run, on any disk and on spinning ones.
    Arena thumbnailWaitArena;
    Prog_ArrayIconRequest thumbnailWaiting;
    u32 thumbnailsMaking, thumbnailsMakingSlow;
    // Files the lists showed at the icon size whose thumbnails are made for the system's cache while no other thumbnail waits, the last shown
    // first, and the ones asked for this run.
    Arena warmArena;
    Prog_ArrayIconRequest thumbnailWarming;
    Prog_PathSet warmed;
    i32 iconSize;
    f32 iconScale;                       // the display scale iconSize was reached at
    // A fixed page count (8192 icons at 16 px); once it is full, a new icon takes the cell of the one drawn longest ago.
    Prog_IconAtlas iconAtlas;
    // Thumbnails for the icon views, keyed by P_FileThumbnailKey, loaded at the tier of sizes at or above what the zoom draws and scaled down to it, so that most zoom steps load nothing.
    // Each tier in use has a slot of its own, so that panels zoomed apart keep theirs; a new tier takes the slot drawn from longest ago, and until its thumbnails come another slot's are drawn.
    // A full atlas gives a new thumbnail the cell of the one drawn longest ago, and drops its slot's thumbnails only when every one was drawn in the last frame.
    Prog_ThumbnailSlot thumbnailSlots[Prog_ThumbnailSlots];
    // The tab list's folder pictures, at their one size.
    Arena folderPictureArena;
    Prog_IconMap folderPictures;
    Prog_IconAtlas folderPictureAtlas;
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
    b8 menuMiller;            // and it is a Miller column's item, whose Rename goes to its folder to rename it there
    u32 menuSidebar;          // or one more than the sidebar group of the item, menuSidebarIndex, whose Rename edits its row
    u64 menuSidebarIndex;
    u32 menuPath[8];          // the open submenu item at each level
    R_Rect menuRects[9];      // each open level's panel in the last build, empty past the last one
    b32 menuApart[9];         // each submenu that could not sit against its opener's panel in the last build, whose opener is then lit
    u32 menuSquare[9];        // each level's corners that the submenu joined to it squared off in the last build, as R_Shape flags
    b32 menuInline;           // submenus open inside their menu, under their item, rather than beside it
    b32 noTooltips;           // no tips under the pointer's resting place
    u32 menuExpanded[32];     // with menuInline, the items whose submenus are open, in the order they were opened
    u32 menuExpandedCount;
    f32 menuTop;              // with menuInline, where the menu opened, which it keeps as it grows; set for menuSerial + 1 in menuTopSerial
    u32 menuTopSerial;
    u32 menuSerial;           // counts the menus opened, which keys their rows' scrolling afresh
    b32 menuReveal;           // the keyboard moved the highlight, which the list scrolls to
    // The system menu commands pinned to the top of every system menu, by label, saved as MenuPins.conf; menuBase is the open menu's items
    // before they were put in that order.
    Arena menuPinArena;
    ArrayStr menuPins;
    // What the menu editor did to the system menus' top level, by the items' labels: those hidden (MenuHidden.conf), and the order the editor
    // left them in (MenuOrder.conf), which they keep in the places they take between the items it never saw.
    Arena menuHiddenArena;
    ArrayStr menuHidden;
    Arena menuOrderArena;
    ArrayStr menuOrder;
    // The menu editor, open in the place of the menu it was opened from (menuEditAt): that menu's top level by label, in the order shown and
    // the system's own, whether each opens a submenu, and the row being dragged, plus one.
    b32 menuEditing;
    R_Rect menuEditAt;
    Arena menuEditArena;
    ArrayStr menuEditLabels;
    ArrayStr menuEditSystem;
    P_Image *menuEditIcons;  // the icon of each of menuEditSystem's items, as the menu had it
    b8 *menuEditSubs;
    u32 menuEditDrag;
    // The palette's Quick group: the starred commands by name, in the order they were starred.
    Arena quickArena;
    ArrayStr quickCommands;
    P_ContextMenu menuBase;
    UI_Vec2 menuPointer;      // where the pointer was in the last build, as only its moving decides the highlight
    b32 menuPointerMoved;
    String menuMoved;         // the label of the item last pinned or unpinned, in menuArena, which slides to its place lit
    u32 menuDepth;            // open submenu levels below the top one
    u32 menuHighlight;        // item index, or UINT32_MAX for none
    Prog_MenuCommand *menuCommands;  // per item of Filey's own menus; NULL for the system's
    UI_Key menuHot;           // hot block when the menu was last built
    Prog_TextEdit menuSearch; // typed into a system menu, which then shows only its top items holding it
    enum16(Prog_CommandKind) menuOpener;  // the button whose menu is open, which stays lit; None for other menus
    // The button under the pointer this frame, whose tip shows once the pointer rests on it.
    UI_Key tooltipKey;
    b32 tooltipAbove;         // it is a button's inside a menu or the palette, whose tips show over them
    R_Rect tooltipRect;
    enum16(Prog_CommandKind) tooltipKind;
    String tooltipLabel;      // the command's label when empty
    // A sidebar item's tip: its folder, inspected by tipJob into tipArena.
    String tipPath;
    Arena tipArena;
    P_Job *tipJob;
    b32 tipKnown;
    Prog_Inspected tip;
    String tooltipDetail;     // a line over the command, such as the folder a history button goes to
    b32 tooltipDescribes;     // that line says what the button does, and the command's hotkeys stay
    String tooltipChip;       // a key that does the same, in a chip after a tip's last line of words
    String tooltipPath;       // a sidebar folder, whose tip shows what it holds instead
    f32 menuRight;            // when nonzero, the right edge the top level lines up with
    // Drag and drop.
    // The system's drag loop is modal, so a drag is requested during a frame and run between frames by Prog_DragSelection.
    b32 dragRequested;
    // The system's window move loop is modal too, and runs between frames for the same reason.
    b32 moveRequested;
    b32 dragging;
    u64 hoverRow;             // row under the pointer in the last build
    u64 gridMoveTime;         // when the grid's layout last changed, which its items glide from for a moment
    u32 iconsPending;         // file icons and cached thumbnails this build drew without, still loading; the first frame waits for them
    f32 zoomWheel;            // wheel travel with Ctrl held that has not made a whole step yet
    f32 sizeWheel;            // the same with Alt held, for the font size and spacing
    // The views on a slider over the active panel's view button: shown while the pointer is over the button or the popup, and for a moment
    // after each Ctrl+wheel step.
    u64 zoomWheelTime;        // when Ctrl+wheel last zoomed the list, in microseconds
    b32 zoomShown;
    R_Rect zoomAnchor;        // the active panel's view button in the last layout
    R_Rect zoomBox;           // the popup in the last layout
    b32 hasHoverRow;
    u64 hoverSpacePanel;      // the panel whose list's empty space is under the pointer in the last build, 0 for none
    String hoverMenuPath;     // the sidebar place or Miller column item under the pointer in the last build, in frameArena
    // Inline rename of the entry named renameFrom.
    b32 renaming;
    u8 renameFrom[1024];
    u32 renameFromLength;
    Prog_TextEdit rename;
    R_Rect renameRect;        // last layout of the edit box
    u8 selectText[1024];      // select this name in the next listing of panel `selectPanel`, which Prog_SelectNext sets
    u32 selectLength;
    u64 selectPanel;
    b32 selectRename;         // and rename it, for items Filey just created
    // The command palette, which takes the keyboard while open; the highlight indexes Prog_PaletteMatches.
    b32 paletteOpen;
    b32 paletteShown;         // it was open last frame, so that a close starts its fade
    u64 paletteClosedAt;      // when it closed, while it fades out
    f32 paletteFade;          // its opacity as it fades out, 1 while open
    Prog_TextEdit palette;
    u32 paletteHighlight;     // UINT32_MAX for none, which Go To starts with from Edit Path
    u32 paletteScroll;        // the first command row shown
    b32 paletteKeyed;         // a key was pressed since Options opened, which shows its highlight only from then on
    u16 hotToolbarCommand;    // the command of the toolbar button under the pointer, which a press during a rename leaves to its click
    String permissionsPath;   // the item whose permissions the inspector shows this frame, which its letters change
    String crashReport;       // the crash report the last start found, in settingsArena, which Report on GitHub offers
    // A release newer than this build, in updateArena, as GitHub gave it at most a day ago, which Update.conf in the cache folder keeps
    // between runs with when it was asked (updateChecked) and the version offered last (updateShown). updateJob asks again or, with
    // updateInstalling, downloads it and puts it in place of updateExecutable, which starts again once updateReady and no copies are under way.
    Arena updateArena;
    Prog_LatestRelease update;
    String updateShown;
    String updateExecutable;
    i64 updateChecked;
    P_Job *updateJob;
    b32 updateInstalling;
    b32 updateManual;         // Check For Updates asked, which says so when there is nothing newer
    b32 updateReady;
    b32 checkUpdates;         // updates.check
    u64 wakeAt;               // when the frame wants another without input, for what shows only after a while; 0 for no such time
    u8 optionHovered;         // the Options row under the pointer, plus one
    b32 paletteWheeled;       // the wheel scrolled the list last, which keeps its place rather than following the highlight until a key is pressed
    u16 paletteFolded;        // bits by Prog_CommandGroup, the groups shown closed; all open at first
    u8 optionsFolded;         // bits by the Options' sections, those shown closed
    // The palette's group or the Options' section easing open or shut, as a list's group does, since when; its rows stay listed while it shuts.
    u64 paletteEaseStart;
    u8 paletteEaseGroup;
    b32 paletteEaseFolding;
    b32 paletteCompact;       // its rows are only the commands' names, without their descriptions
    b32 inspectorSliding;     // the inspector being drawn slides open or shut, at widths it does not keep
    u32 fitColumn;            // the active panel's column, by its place + 1, that the next build sizes to its values; UINT32_MAX all of them
    u64 listBytes, volumeBytes;  // what the usage columns of the list being built are shares of: its items' bytes and its drive's
    // What Settings.conf last set the inspector's text size and layout to: they apply when the file changes them, and the inspector's own controls change them meanwhile.
    u32 settingInspectorFontSize;
    u8 settingInspectorLayout;  // its value + 1, 0 before any
    u8 scheme;                  // the settings' Color Scheme, by its place among the built-in schemes and then the custom ones
    u8 lightScheme;             // the one the system's light mode brings, as `scheme`
    Prog_FontName schemeName, lightSchemeName;  // their names
    // Custom color schemes, a "<name>.scheme" file each in the settings folder: their names and their colors as "Group = #rrggbb" lines.
    Arena schemesArena;
    ArrayStr schemeNames, schemeTexts;
    u32 schemeGroups[Prog_ColorGroupCount];      // the shown scheme's colors as 0xRRGGBB, by Prog_Option_ColorClear's order
    b32 colorsExpanded;         // Options shows the scheme's colors
    // The schemes' dropdown under a scheme option while open: that option + 1, and its highlighted row.
    u8 schemesOpen;
    u32 schemesHighlight;
    u64 settingsHash;           // Settings.conf and the scheme files as last applied
    // The color picker while open: the option of the color it changes, and that color as hue, saturation and brightness from 0 to 1.
    b32 colorPicking;
    enum8(Prog_Option) colorOption;
    f32 pickerHue, pickerSaturation, pickerBrightness;
    u32 pickerOriginal;           // the color as it opened, which Cancel puts back
    UI_Vec2 pickerAt;             // where the pointer was then, the picker's top left
    u8 pickerField;               // the number box being typed in, 1 to 7 for H, S, B, R, G, B and the hex, or 0
    Prog_TextEdit pickerEdit;
    R_TextureHandle pickerSquare, pickerStrip;  // the saturations and brightnesses of pickerSquareHue, and the hues
    f32 pickerSquareHue;
    u8 hexOption;                 // the color group whose hex box is being typed in, or 0
    Prog_TextEdit hexEdit;
    u8 sliderOption;              // the option whose slider is being typed in, plus one, or 0
    b32 sliderInvalid;            // what was typed there does not read as a number
    b32 sliderDragged;            // the slider pressed has been dragged, so letting go sets it rather than starting to type
    Prog_TextEdit sliderEdit;
    b32 systemScheme;           // the system's light or dark choice picks the light scheme or that one
    b32 systemDark;             // the system's choice, as the settings last read it
    // How the lists look: the space between their items and how much inactive panels show, as offsets from the default 4 and 60 so that
    // {0} has them, square corners and every other row tinted.
    i32 spacingShift;
    u8 borderSize;            // the outlines' thickness, a Prog_OptionBorders index: 1 to 3 pixels in halves
    i32 inactiveShift;
    b32 squareCorners;
    b32 alternatingRows;
    // How the pointer works the lists; those on by default are kept the other way round, so that {0} has them.
    b32 actOnRelease;
    b32 singleClick, singleClickFoldersOnly;
    b32 hoverToSelect;
    b32 clickToFocus;         // a panel takes the keyboard on a click only, not under a moving pointer
    b32 underlineAlways;      // in single-click mode, every name is underlined, not only the pointed one
    b32 doubleClickToParent;
    b32 noClickRename;        // a slow second click on a name does not rename it
    b32 noOverscroll;
    b32 reverseScroll;
    // What the lists show: names without their extensions, and the system's own files; settingHidden is what Settings.conf last set the
    // hidden items to, + 1, which the shown tab then takes.
    b32 hideExtensions;
    b32 showSystem;
    u8 settingHidden;
    // The breadcrumbs: from the drive rather than from the first known folder, and without the trail of visited folders.
    b32 fullPaths;
    b32 noTrail;
    // Which folders get their files' thumbnails, and which have their subfolders measured, by the kind of drive they are on.
    enum8(Prog_Thumbnails) thumbnailMode;
    // While several items are selected, the rows show boxes that check them in place of their icons.
    b32 showCheckboxes;
    b32 noTrashConfirm;       // emptying the Recycle Bin or the Trash does not ask first
    // git.fetch: the shown folder's repository is fetched from its remote now and then, which keeps the commits to pull counted.
    // The last ones fetched, by the hash of their folder, and when, so that each is fetched at most every few minutes.
    b32 gitFetch;
    // ponytail: the last few repositories only; one pushed out of them is fetched again on its next visit.
    struct { u64 hash, time; } gitFetched[8];
    P_Job *gitFetchJob;
    u64 gitFetchHash;         // the hash of the folder of the repository being fetched
    P_Job *gitPullJob;
    u64 gitPullHash;          // and of the one being pulled
    b32 separateInstances;    // each run of the program opens a window of its own, rather than a tab in the one already open
    b32 separateWindow;       // started as a window of its own beside that one
    b32 leavesSession;        // the Setup window for uninstalling, which saves no session on exit
    b32 tabDropOther;         // a dragged tab is over another of the program's windows, which letting go hands it to
    UI_Block *tabGhost;       // the dragged tab, while the pointer is outside the window, for the frame to draw on its own and show at it
    UI_Vec2 tabGhostHot;      // where the pointer is in it
    // Another window's tab dragged over this one, at foreignPoint, and the strip slot it would go into, as the last build found it.
    b32 foreignDrag;
    UI_Vec2 foreignPoint;
    b32 foreignOver;
    u64 foreignPanel;
    u32 foreignSlot;
    // Where a start and a new tab go, how the recents are kept, whether the lists mark them and sort them first, and whether frames wait for the display.
    enum8(Prog_Startup) startup;
    enum8(Prog_NewTab) newTab;
    enum8(Prog_NewTabView) newTabView;  // the view new tabs and splits start in
    enum8(Prog_StoreRecents) storeRecents;
    u8 archiveLevel;  // the compression level of the archives Filey writes, 0 to 9
    u8 videoCodec;    // what Compress Video encodes to, a Prog_OptionVideoCodecs index
    u8 videoQuality;  // its constant quality, 0 (best) to 51
    b32 downloadAudio;  // downloads by URL take the sound alone, as MP3
    b32 downloadPlaylist;  // the address of a video in a playlist downloads all of the playlist
    b32 deleteFinishedTorrents;  // a torrent downloaded whole sends its .torrent to the bin
    u8 audioVolume;     // how loud the inspector plays audio, 0 to 100
    u8 noticeLevel;  // the least Prog_NoticeKind shown, Prog_NoticeKind_Count for none
    b32 noticesInSidebar;  // the notices and the progress cards stack at the bottom of the sidebar while it shows
    f32 sidebarCards;      // how high the progress cards there were in the last build, which the sidebar's list stops over
    b32 showRecents, sortRecents;
    b32 noVSync;
    b32 noHighlight;          // inspector.highlight = false: the inspector shows code in one color
    // The families of the UI's text and of the inspector's, by name.
    Prog_FontName font, inspectorFont;
    // The language Filey shows its text in by its name, empty for English, and the custom ones in the settings folder (<name>.lang).
    Prog_FontName language;
    Arena languagesArena;
    ArrayStr languageNames;
    b32 noFolderSizes;
    b32 noFolderSizesSolidState, folderSizesSpinning, folderSizesNetwork;
    // In its tab mode it finds the active panel's tabs, in a box under that panel's tab list button; the highlight indexes Prog_TabMatches.
    // In its Go To mode it finds folders to open, recent ones or those inside the folder typed; the highlight indexes Prog_GoToMatches.
    enum8(Prog_PaletteMode) paletteMode;
    b8 paletteQuick;          // the commands mode shows only the Quick commands, as Quick Commands opens it
    R_Rect tabListRect;       // that button in the last layout
    // Go To lists the folder typed so far, gotoFolder, whose subfolders gotoJob puts in gotoArena; gotoListed once they are there.
    u8 gotoFolder[1024];
    u32 gotoFolderLength;
    P_Job *gotoJob;
    Arena gotoArena;
    Prog_GoToListed gotoListing;
    b32 gotoListed;
    b32 gotoFolded[Prog_GoToSection_Folder];  // sections shown by their title alone
    u32 gotoGroupsFolded;                     // and the user's own groups folded there, a bit each
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
    u64 gotoNearby;
    b32 gotoShowHidden;       // what the shown tab showed of the hidden items when the pool was filled
    b32 gotoDrives;           // Alt+E searches the drive index too, after the short list, until Go To closes
    u64 gotoScanAt;           // the index's next entry to look at
    u64 gotoScanFrame;        // the UI frame that looked last
    const Prog_Index *gotoIndex;
    b32 gotoSearching;        // the look through the index has not reached its end yet
    Prog_PathSet gotoSeen;
    u8 gotoFoundText[1024];
    u32 gotoFoundTextLength;
    Prog_Transfer transfers[Prog_TransferSlots];
    // The audio file the inspector plays, by the hash of its path: its job, and the progress that counts the milliseconds played and pauses
    // it, two in turn, so that a job stopped for a seek does not write over the next one's.
    P_Job *playerJob;
    P_Progress playerProgress[2];
    u32 playerSlot;
    u64 playerHash;
    atomic_uint playerVolume;  // what it plays at, which the inspector's volume bar sets as it moves
    b32 audioMuted;      // the speaker button's, for the session
    b32 volumeDragging;  // the volume follows the pointer along its bar until the press ends
    u8 volumeDragged;
    b32 scrubbing;  // the playhead follows the pointer over the waveform until the press ends
    u64 playheadHeard, playheadAt;  // the milliseconds last heard and when that came, which the playhead goes on from smoothly
    Arena sizeArena;
    Prog_SizeMap sizes;
    // File operations Undo can reverse, newest last, and the renames and moves it reversed, which Redo makes again until another operation.
    Arena undoArena;
    Prog_ArrayUndo undo;
    Arena redoArena;
    Prog_ArrayUndo redo;
    // Saved layouts, kept in Layouts.conf: their names, and their texts, a window line before a session's.
    Arena layoutsArena;
    ArrayStr layoutNames, layoutTexts;
    // The Save Layout box while open, with the name typed into it.
    b32 layoutSaving;
    // Filey Setup, a page in place of the panels while open, on Windows only: the install as it was found, its switches as set on the page,
    // the logo, and the page's text at its three sizes, which the frame makes while it is open.
    b32 setupOpen;
    Arena setupArena;
    P_Install setup;
    b32 setupDesktop, setupOnPath, setupDefault, setupMenu;
    R_TextureHandle setupLogo;
    i32 setupLogoSize;
    u64 setupOpenedAt;  // when Setup last opened, from which its logo comes in
    const R_Font *setupFonts[4];
    Prog_TextEdit layoutName;
    // What the layouts ask first, about the layout named `layoutAskName`.
    enum8(Prog_LayoutAsk) layoutAsk;
    u8 layoutAskName[256];
    u32 layoutAskLength;
    // The installed font families once listed, which the font picker offers for the option `fontOption`.
    Arena fontsArena;
    String *fontNames;
    u64 fontCount;
    // The font picker's rows in their own families, the fonts made as rows come into sight and let go once the picker closes.
    struct {
        Arena arena;
        R_Font font;
        u64 family;      // the place in fontNames plus one; zero for an empty slot
        u64 frame;       // the UI frame that last drew it
        f32 pixels;
        b32 made;
    } fontPreviews[48];
    b32 fontPreviewsShown;  // the picker drew this frame, so that its fonts are kept
    P_Job *fontsJob;
    enum8(Prog_Option) fontOption;
    // Options as the font dropdown found it, its search, highlight and scroll, which come back as the dropdown closes.
    Prog_TextEdit optionsPalette;
    u32 optionsHighlight, optionsScroll;
    b32 optionsWheeled;       // Options were scrolled by the wheel, away from their highlight, where they stay under the font dropdown
    // The families the defaults are drawn in, as the system matches them, which Options name.
    Prog_FontName fontUsed, inspectorFontUsed;
    R_Rect fontAnchor;        // where the font dropdown's button was last seen, which it stays under while the button is out of sight
    b32 fontsOver;            // Options are being drawn under the font dropdown, whose button shows it open
    // The layout Load On Startup names, which takes the place of `startup` while there is one.
    u8 startupLayout[256];
    u32 startupLayoutLength;
    b32 inspectorHex;         // every file shows as hex
    // The inspector's image view: the checkered grid behind a picture and the minimap of where the view is while it does not fit.
    b32 imageHideGrid;
    b32 imageHideMinimap;
    // The inspector's text view: wrapped at its width, with line numbers unless hidden, its spaces and tabs as marks,
    // in inspectorFontSize points (0 for Prog_InspectorFontSize).
    b32 inspectorWrap;
    b32 documentUnwrapped;
    b32 inspectorHideNumbers;
    b32 inspectorWhitespace;
    u32 inspectorFontSize;
    enum8(Prog_InspectorLayout) inspectorLayout;
    b32 inspectorBelow;       // where the last frame put it, which Toggle Inspector Layout turns around
    b32 swallowText;          // the next character comes from a key that ran a command
    Key hotkeyLead;           // the last key, when it starts a sequence such as "G G"
    // A hotkey chip being rebound: the next key press replaces hotkey `hotkeySlot` of `hotkeyKind`, or adds one when the slot is past its last;
    // with hotkeyAsking its delete button asked whether to delete it instead, in a box at hotkeyAskAt.
    enum16(Prog_CommandKind) hotkeyKind;  // None while no chip is
    u8 hotkeySlot;
    b32 hotkeyAsking;
    UI_Vec2 hotkeyAskAt;
    b32 hotkeyChipShown;      // the build drew the chip, which stops recording once it is out of sight
    UI_Vec2 mainMenuAt;       // where the main menu opens from its button, for its hotkey too
    UI_Vec2 tabMenuAt;        // and the tab menu, under the active panel's shown tab
    UI_Vec2 pathMenuAt;       // and the path menu, under the active panel's path bar
    enum8(Prog_CharacterKeys) characterKeys;
    // Select Typed Item: what was typed so far, and when its last character came; a pause starts it over.
    Prog_TextEdit typed;
    u64 typedTime;
    // The Types popup over the active panel's listing: what is typed in its box, its highlighted row, and the header of the Type column it
    // hangs under. The listing's types, with how many items have each and the first of them for its icon, are gathered in typesArena
    // for each listing it shows, `typesListing`.
    b32 typesOpen;
    Prog_TextEdit typesSearch;
    u32 typesHighlight;
    R_Rect typesAnchor;
    Arena typesArena;
    u64 typesListing;
    // The folder dropdown under a crumb of the path bar: the crumb's folder and the one the path goes on into from it, which its listing marks;
    // a box that finds in the listing once a job brings it, and the highlighted row, unlit until the keyboard moves it or the marked one is lit.
    b32 filesOpen;
    Arena filesArena;            // filesFolder and filesChild
    String filesFolder, filesChild;
    Arena filesListArena;        // the job's, which holds filesItems
    Prog_Entry *filesItems;
    u64 filesCount;
    b32 filesListed;
    P_Job *filesJob;
    Prog_TextEdit filesSearch;
    u32 filesHighlight;          // among the rows the search finds
    b32 filesLit;
    b32 filesReveal;             // the highlight scrolls into sight at the next build
    R_Rect filesAnchor;          // the open crumb, as the last build laid it out
    f32 filesWidest;             // the widest name of the listing, measured once it arrives
    UI_Vec2 filesMouse;          // where the pointer was at the last build
    String *typeNames;
    u64 *typeCounts;
    u64 *typeFirsts;
    u32 typeCount;
    const R_Font *monoFont;   // the inspector's, at its size; NULL until made
    // Permanent delete waits for this confirmation; the paths live in confirmArena.
    b32 confirmingDelete;
    b32 confirmEmptyTrash;    // the confirmation is for emptying the Recycle Bin or the Trash rather than for confirmPaths
    Arena confirmArena;
    String *confirmPaths;
    u64 confirmCount;
    // Sidebar.
    b32 sidebarHidden;
    b32 uiHidden;  // Hide UI's focus mode: no sidebar, toolbars or status bars, but a status bar while its panel filters
    // The Quick Access filter: what its box holds, which leaves the sidebar the items whose names it finds, and whether the box has the keyboard.
    Prog_TextEdit sidebarFilter;
    Prog_TextEdit sidebarFilterBefore;  // what it was as this round of typing began, which Escape brings back
    b32 sidebarFiltering;
    R_Rect sidebarFilterRect;  // the box, as the last build laid it out
    f32 sidebarWidth;         // logical pixels its edge was dragged to; 0 for the default
    f32 sidebarGrab;          // how far right of its edge line the pointer took it
    PlatformCursor cursor;    // the pointer's shape the last build asked for, over an edge that drags
    b32 groupToggled[Prog_Group_Count];  // differs from the group's default, which is open for bookmarks and storage
    // The sidebar's groups in the order they were moved to, while groupsOrdered, which Session.conf keeps; the user's own come after the
    // built-in ones by number.
    u8 groupOrder[Prog_SidebarGroupMax];
    b32 groupsOrdered;
    // The user's own groups, in userGroupsArena, saved as Groups.conf. userGroupDrop is one more than where folders dragged over the group
    // userGroupDropIn (one more than its number among them) would go; the Shown ones are what this frame's build found, as for the bookmarks.
    Arena userGroupsArena;
    Prog_UserGroup userGroups[Prog_MaxUserGroups];
    u32 userGroupCount;
    u64 userGroupDrop, userGroupDropShown;
    u32 userGroupDropIn, userGroupDropShownIn;
    // A name edited in its sidebar row, which the Save Layout box's state carries: nameFor is 1 for the group of the user's nameGroup, and
    // 2 for the folder namePath, row nameIndex of sidebar group nameGroup; the press before it began, and whether its box was built.
    f32 inspectorShare;       // the inspector's share of its panel, 0 for half
    b32 longPaths;            // the system lets programs use paths longer than 260 characters, as last read
    b32 longPathsSetting;     // and a job is asking it to change that
    u8 nameFor;
    u32 nameGroup;
    u64 nameIndex;
    Arena nameArena;
    String namePath;
    u64 nameSince;
    b32 nameShown;
    // A slow second click on the row of the sidebar folder shown renames it, once the double-click time passes without another press: when,
    // the press that asked for it, one more than its group, and its row.
    u64 sidebarRenameAt, sidebarRenamePress;
    // A slow click on the name of the list's one selected item renames it the same way: the row pressed while it was so, and the rename
    // asked for, with the press that asked for it, its panel and the listing it was in.
    u64 listSlowRow;
    // A tree's folder unfolding or folding up eases open or shut: the panel and when it began, the folder's place in the view and how many
    // rows came or go; until an unfolded one's listing comes, the folder and how many rows the view had. One folding up keeps the listing,
    // its rows easing shut in it, and is listed again once they have.
    u64 treeEasePanel, treeEaseStart, treeEaseRow, treeEaseRows, treeEaseBefore;
    b32 treeEaseOpening, treeEasePending, treeEaseFolding, treeEaseRelisted;
    u8 treeEaseName[1024];
    u32 treeEaseNameLength;
    // A list group's heading clicked eases its items open or shut: the panel, when it began and the group; one folding up keeps its items
    // in the view until they are shut, and asks once for the view without them.
    u64 groupEasePanel, groupEaseStart;
    // A Go To section folding or unfolding eases, while the list does not scroll: the section, when it began, and whether it folds.
    u32 gotoEaseSection;
    u64 gotoEaseStart;
    b32 gotoEaseFolding;
    u32 groupEaseIndex;
    b32 groupEaseFolding, groupEaseSettling;
    u64 listRenameAt, listRenamePress, listRenamePanel, listRenameListing, listRenameRow;
    u32 sidebarRenameGroup;
    u64 sidebarRenameIndex;
    // The box asking for an encrypted archive's password: the archive, the password typed and the dots shown for it, whether the last one
    // tried was wrong and one is being tried, and a hash of the archive the box was dismissed for last, which it does not ask for again.
    Arena passwordArena;
    String passwordArchive;
    Prog_TextEdit password, passwordShown;
    b8 passwordWrong, passwordTrying;
    b8 passwordServer;        // passwordArchive is a server's address, which the password connects to
    u64 passwordDismissed;
    // The places hidden from the sidebar and the order they were put in, by path, saved as PlacesHidden.conf and PlacesOrder.conf;
    // placesShowHidden shows the hidden ones too, faded, to show them again. sidebarDrag is one more than the row a drag moves among the places
    // or a group of the user's, sidebarDragGroup one more than that group, and sidebarGap one more than the row it would go before, one past
    // the last for after it.
    Arena placesHiddenArena;
    ArrayStr placesHidden;
    Arena placesOrderArena;
    ArrayStr placesOrder;
    b32 placesShowHidden;
    u64 sidebarDrag;
    u64 sidebarGap;
    u32 sidebarDragGroup;
    // A group's title dragged among the others: one more than the group, one more than the place in the order it would go before, and
    // each group's title's top in the last build.
    u32 groupDrag, groupGap;
    f32 groupTops[Prog_SidebarGroupMax];
    b32 placesWanted;         // the sidebar was built, so its places are kept current
    b32 windowInactive;       // another window has the focus, which dims the title strip
    b32 drivesChanged;        // the system told of a drive or medium coming or going, which the places are listed again for
    Arena placesArena;        // the latest places listing
    P_ArrayPlace places;
    P_ArrayPlace drives;      // in the order they were first listed in, so that a drive connected later comes last
    Arena drivesSeenArena;
    ArrayStr drivesSeen;      // every drive's path, in the order it was first listed in
    ArrayStr pathRoots;       // folders that breadcrumbs start from, for folders inside them
    ArrayStr searchPlaces;    // the folders Go To searches besides the places shown, from P_SearchPlaces
    ArrayStr missingBookmarks;  // bookmarks whose folders were gone at the last listing, which the sidebar fades
    P_Job *placesJob;
    u64 placesTime;
    // The folders the known servers share, which Storage lists after the drives: the servers of the network drives, the bookmarks, the
    // recents and the shown folder, by sharesKey, their names' hash, asked again when they change and every half minute.
    Arena sharesArena;
    P_ArrayPlace shares;
    P_Job *sharesJob;
    u64 sharesTime;
    u64 sharesKey;
    // Bookmarked and recently shown folders, saved in configDirectory when the app sets one.
    Arena settingsArena;
    String configDirectory;
    String cacheDirectory;  // the drive index's folder
    Arena bookmarkArena;
    ArrayStr bookmarks;
    // Where a drag over the bookmarks would pin its folders: one more than the position, or 0 when it is elsewhere.
    // The build shows it; the drop, which arrives before the next build, uses the one committed after the last.
    u64 bookmarkDropShown;
    u64 bookmarkDrop;
    // The sidebar folder the last build found a drag over would go into, and the bookmark being dragged, both in dropPlaceArena.
    Arena dropPlaceArena;
    String dropPlace;
    u64 dropPanel;            // the panel whose own folder a drag over the window would drop into, which its list shows; 0 for none
    String dragPlace;
    u64 bookmarkGap;
    // The panel zoomed to fill the panels' area (an id, 0 for none), the one shown zoomed while it grows or shrinks back, and its place in
    // the layout, which it grows from.
    u64 zoomedPanel;
    u64 zoomShownPanel;
    R_Rect zoomFrom;          // where the last build found a drag over the bookmarks would go, plus one, for the rows to open a gap
    // The recents, best first: the more often one is opened the higher it ranks, and among equals the later; one listed without being opened
    // drops a step each listing after its tenth, and every tenth open all the others do.
    Arena recentArena;
    ArrayStr recents;
    Prog_RecentRank *recentRanks;  // one for each of the recents
    u32 recentOrdinal;             // the last one's, counting up across runs
    // The files cut to the clipboard, by their paths, dimmed in the lists until the clipboard is set again or they are pasted.
    Arena cutArena;
    struct { MapHeader header; struct { String key; b32 value; } *v; } cutPaths;
    u32 cutSequence;
    // The dropdown of git branches under a panel's branch in the path bar: the repository's local branches once listed, and what is
    // typed to find one.
    b32 branchesOpen;
    u64 branchesPanel;
    Arena branchesArena;
    ArrayStr branches;
    P_Job *branchesJob;
    Prog_TextEdit branchesSearch;
    u32 branchesHighlight, branchesScroll;
    UI_Key branchesAnchor;    // the branch's button, which the dropdown hangs under
    Prog_Notice notices[Prog_MaxNotices];  // oldest first
    Arena mutedArena;
    ArrayStr mutedNotices;  // the kinds of notice not shown, from MutedNotices.conf
    // The torrent downloads under way, running or waiting for a slot, and those a quit left, which the next run starts again, in the order they
    // start: "<.torrent path>\t<files chosen>" a line, from Torrents.conf.
    Arena torrentArena;
    ArrayStr torrents;
    P_Job *torrentProgressJob;  // the read of how far the files of the torrent an inspector shows are, and when the last one started
    u64 torrentProgressAt;
    b32 mutedOpen;          // the dropdown of them under their option
    u32 noticeCount;
    u64 noticeIds;
    // The inspector's disk usage view, while inspectorUsage, of usage.path: its tree's open folders, the folder and the type chosen (UINT32_MAX for none) and the one pointed at, and its treemap's cells as last laid out for usageCellsFor.
    b32 inspectorUsage;
    Prog_Usage usage;
    P_Job *usageJob;
    b8 *usageOpen;
    u32 usageSelected, usageType, usageHover;
    b32 usageReveal;  // the chosen folder's row scrolls into sight
    // The file chosen in the treemap, in usage.files, UINT32_MAX while the folder is; and the press that chose it, which while it is the last press the keys that trash and delete files act on the chosen item rather than the list's.
    u32 usageFile;
    u64 usagePress;
    b32 usageMenu;  // its menu's Trash or Delete runs, which acts on the item chosen whatever was pressed last
    Arena usageCellsArena;
    Prog_UsageCell *usageCells;
    u32 usageCellCount;
    R_Rect usageCellsFor;
    P_Job *recentsCheckJob;        // looking for recents that are gone, at most one at a time; a request meanwhile checks again after it
    b32 recentsCheckAgain;
    u64 recentsCheckTime;
    b32 requestPassing;            // the next Prog_RequestDirectory passes through its folder, as Up does
    // One save of each list at a time, since two writing one file at once collide; a change during a save saves again after it.
    b32 saving[12];           // bookmarks, recents, folder views, menu pins, quick commands, muted notices, hidden menu items, menu order, torrents, hidden places, places' order, groups
    b32 saveAgain[12];
    // The folders whose view is kept, a line each of their zoom, sort, whether it is descending and their path, saved as FolderViews.conf.
    Arena folderViewArena;
    ArrayStr folderViews;
    // See-through window with the glass theme, and what Windows draws behind it; the app applies both to the window.
    b32 transparent;
    enum8(PlatformBackdrop) glassBackdrop;
    // Settings.conf in the config folder: the UI scale and hotkeys here, colors in the custom theme; its folder's watch applies edits as they are saved.
    f32 uiScale;              // 0 is 1
    Prog_Hotkey userHotkeys[256];
    u32 userHotkeyCount;
    b8 hotkeyOverridden[Prog_CommandKind_Count];  // the command's default hotkeys give way to the settings' ones
    P_DirectoryWatch configWatch;
    // "animations = false" in the settings: menus, scrolling and the caret change at once.
    b32 animationsOff;
    // A point smaller than the main font, for the details columns; the main font while unset.
    const R_Font *smallFont;
    const R_Font *smallIcons;  // the icons at about two thirds of their size
    const R_Font *mediumIcons; // and at four fifths, for an icon that fills more of its square than those beside it
    // Set by the app so that the first listing loads shell extensions in the background; the first context menu would otherwise stall on them.
    b32 warmMenus;
} Prog_State;

function b32 Prog_Init(Prog_State *state, String path, P_JobWakeCallback *wake, void *user);
function void Prog_Release(Prog_State *state);
function void Prog_RequestDirectory(Prog_State *state, String path, b32 addHistory, u64 historyAt);
function b32 Prog_TakeCompletions(Prog_State *state);
function String Prog_ParentPath(String path);
function String Prog_JoinPath(Arena *arena, String path, String name);
// The path the crumbs show, where the shown folder ends in it, and where each crumb ends, up to `max`; none in This PC.
function u32 Prog_CrumbEnds(Prog_State *state, Arena *arena, String *path, u64 *shownEnd, u64 **ends);
function String Prog_CrumbLabel(Prog_State *state, String path, u64 end, b32 drive, b32 first);
// A picture of Prog_PreviewSize squared of the 3D model in the file of `size` bytes at `path`; NULL for a file that is not one or does not read.
// The model at `path` as a mesh for the GPU, Y up, with only triangles whose vertices are there, and the center and radius of its bounds.
function b32 Prog_ModelMesh(Arena *arena, String path, u64 size, const P_CancelToken *cancel, Prog_ModelShape *shape);
// A 3D model's picture for the icon views, `size` squared, in straight RGBA; NULL when `path` is no model the readers know or has nothing to draw.
function u32 *Prog_ModelThumbnail(Arena *arena, String path, i32 size, const P_CancelToken *cancel);
// Whether `path` names a 3D model the readers know, by its extension.
function b32 Prog_IsModel(String path);
function b32 Prog_IsBlp(String path);
function b32 Prog_ScanUsage(Arena *arena, String path, Prog_UsageProgress *progress, const P_CancelToken *cancel, Prog_Usage *usage);
function u32 Prog_UsageLayout(Arena *arena, const Prog_Usage *usage, u32 root, R_Rect rect, Prog_UsageCell **cells);
function u32 Prog_UsageRemove(Prog_Usage *usage, String path);
function String Prog_UsageChosen(Prog_State *state, Arena *arena, b32 any);
function b32 Prog__UsageHasChoice(const Prog_State *state);
function String Prog_UsageFolderPath(Arena *arena, const Prog_Usage *usage, u32 index);
function b32 Prog_DecodeBlp(Arena *arena, String data, i32 maxSide, P_Image *out, i32 *fullWidth, i32 *fullHeight);
function b32 Prog_LoadBlp(Arena *arena, String path, i32 maxSide, P_Image *out, i32 *fullWidth, i32 *fullHeight);
// A Word document's or a spreadsheet's text for the inspector, from the zip archive of XML a .docx or an .xlsx is; empty when it has none.
function b32 Prog_IsOffice(String path);
function String Prog_OfficeText(Arena *arena, String path, u64 size);
// An audio or video file by its extension, the details ffprobe reads of it with its length in seconds, an audio file's waveform and a video's frame
// from ffmpeg; nothing without the tools, or once `cancel` is set.
function b32 Prog_IsAudio(String path);
function b32 Prog_IsVideo(String path);
function String Prog_MediaDetails(Arena *arena, String path, const P_CancelToken *cancel, f64 *seconds);
function b32 Prog_MediaWaveform(Arena *arena, String path, const P_CancelToken *cancel, i32 side, P_Image *image, i32 *width, i32 *height);
function b32 Prog_MediaFrame(Arena *arena, String path, f64 duration, const P_CancelToken *cancel, i32 side, P_Image *image, i32 *width, i32 *height);
function u64 Prog_MediaLength(Arena *arena, String path, const P_CancelToken *cancel);
function b32 Prog_IsWebUrl(String text);
function b32 Prog_IsMagnet(String text);
function b32 Prog_IsTorrent(String text);
function P_FileError Prog_DownloadTorrent(Arena *arena, String source, String folder, String selection, P_Progress *progress, const P_CancelToken *cancel, ArrayStr *placed);
// Removes what a torrent's download stopped short left in `folder`: the files it was writing, aria2c's control file, and the folders that held
// them once they are empty. Without the control file the download had finished, or never started, and nothing goes.
function void Prog_DiscardTorrent(Arena *arena, String source, String folder);
function P_FileError Prog_FetchTorrent(Arena *arena, String source, String folder, P_Progress *progress, const P_CancelToken *cancel, ArrayStr *placed);
function b32 Prog_ReadTorrent(Arena *arena, String bytes, Prog_TorrentInfo *info);
function String Prog_MagnetHash(Arena *arena, String magnet);
function String Prog_TorrentSelection(Arena *arena, const Prog_TorrentInfo *info, const b8 *skip);
function void Prog_TorrentSkip(String selection, const Prog_TorrentInfo *info, b8 *skip);
function b32 Prog_TorrentControlDone(String control, const u64 *sizes, u64 count, u64 *done);
function void Prog_DownloadTorrentFile(Prog_State *state, String path);
function void Prog_ResumeTorrents(Prog_State *state);
function u32 Prog_TorrentSlot(const Prog_State *state, String path);
function u64 Prog_TorrentWaiting(const Prog_State *state, String path);
function void Prog_GitPull(Prog_State *state);
function b32 Prog_PlayAudio(Arena *arena, String path, u64 start, const atomic_uint *volume, P_Progress *progress, const P_CancelToken *cancel);
function Prog_Transfer *Prog__FileJobOp(Prog_State *state, Prog_JobKind kind, const String *paths, u64 count, String directory, b32 flag, String name, Prog_TransferOp op);
function P_FileError Prog_DownloadUrl(Arena *arena, String url, String folder, b32 audio, b32 playlist, P_Progress *progress, const P_CancelToken *cancel, ArrayStr *placed);
function P_FileError Prog_CompressVideo(Arena *arena, String path, u32 codec, u32 quality, u64 base, P_Progress *progress, const P_CancelToken *cancel, String *output);
function String Prog__NameStem(String name);
function String Prog__FreeName(Arena *arena, String directory, String stem, String extension);
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
function void Prog_ReloadFolderIcon(Prog_State *state, String path);
function void Prog_ReloadIcons(Prog_State *state);
function P_FileError Prog_OpenWindow(Prog_State *state, String path, String traits);
function void Prog_ApplyTabTraits(Prog_State *state, String traits);
function void Prog_WarmThumbnail(Prog_State *state, String directory, const Prog_Entry *entry);
function const Prog_Icon *Prog_ThumbnailIcon(Prog_State *state, String directory, const Prog_Entry *entry, i32 size);
function const Prog_Icon *Prog_ThumbnailStandIn(Prog_State *state, String directory, const Prog_Entry *entry, i32 size);
function const Prog_Icon *Prog_FolderPicture(Prog_State *state, String path, i32 size);
function void Prog_SubmitIconRequests(Prog_State *state);
function void Prog_CloseMenu(Prog_State *state);
function void Prog_DragSelection(Prog_State *state);
function void Prog_Reload(Prog_State *state);
function void Prog_BeginRename(Prog_State *state, u64 row);
function void Prog_SetTransparent(Prog_State *state, b32 transparent);
function String Prog_EditText(const Prog_TextEdit *edit);
function String Prog_EntryLeaf(String name);
function String Prog_EntryParent(String name);
function b32 Prog_FilterMatch(String name, String filter, u8 *marks);
function String Prog_HexDump(Arena *arena, String bytes, b32 cut);
function String Prog_ArchiveListing(Arena *arena, const P_FileArray *items, b32 cut);
function P_DirectoryTotals Prog_ArchiveTotals(const P_FileArray *items);
function void Prog_LoadLayouts(Prog_State *state);
function String Prog_LayoutText(Arena *arena, Prog_State *state);
function void Prog_SetLayout(Prog_State *state, String name, String text, b32 remove);
function b32 Prog_ApplyLayout(Prog_State *state, String name);
function u32 Prog_LayoutMatches(const Prog_State *state, u32 *out);
function u32 Prog_FontMatches(const Prog_State *state, u32 *out);
function void Prog_SetOptionText(Prog_State *state, Prog_Option option, String text);
function void Prog_LoadSchemes(Prog_State *state);
function void Prog_SetScheme(Prog_State *state, String name, String text, b32 remove);
function b32 Prog_ForkScheme(Prog_State *state, u32 scheme, Prog_Option option);
function void Prog_PickColor(Prog_State *state, u32 channel, f32 value);
function void Prog_CloseColorPicker(Prog_State *state);
function void Prog_PickHex(Prog_State *state, u32 rgb);
function String Prog_PickerFieldText(Prog_State *state, Arena *arena, u32 field);
function void Prog_PickerTyped(Prog_State *state);
function void Prog_EndHexEdit(Prog_State *state, b32 keep);
function b32 Prog_EvaluateExpression(String text, f64 *value);
function void Prog_BeginSliderEdit(Prog_State *state, Prog_Option option);
function b32 Prog_EndSliderEdit(Prog_State *state, b32 keep);
function R_Color Prog_OptionColor(const Prog_State *state, Prog_Option option);
function u8 Prog_ShownScheme(const Prog_State *state, String *name);
function void Prog_RgbToHsb(R_Color color, f32 *hue, f32 *saturation, f32 *brightness);
function b32 Prog_PreviewsArchive(const Prog_State *state, const Prog_Entry *entry);
function String Prog_ArchiveSummary(Arena *arena, P_DirectoryTotals totals, u64 packed);
function i32 Prog_FuzzyScore(String name, String path, String typed, u8 *nameMarks, u8 *pathMarks);
function i32 Prog_EntryScore(String name, String filter, u8 *marks);
function void Prog_ApplyFilter(Prog_State *state, b32 edited);
function void Prog_ClearFilter(Prog_State *state);
function u64 Prog_ViewPosition(const Prog_State *state, u64 row);
function u32 Prog_GroupAt(const Prog_State *state, u64 position);
function void Prog_GroupToggle(Prog_State *state, u32 index);
function void Prog_LoadSettings(Prog_State *state, String configDirectory, String cacheDirectory);
function void Prog_SessionWindow(String configDirectory, PlatformWindowDesc *desc);
function b32 Prog_IsBookmarked(const Prog_State *state, String path);
function void Prog_ToggleBookmark(Prog_State *state, String path);
function b32 Prog_FolderView(const Prog_State *state, String path, Prog_ViewSettings *view);
function Prog_ViewScope Prog_FolderViewScope(const Prog_State *state, String path, Prog_ViewSettings *view, String *key);
function Prog_ViewSettings Prog_DefaultView(const Prog_State *state);
function b32 Prog__SameView(Prog_ViewSettings a, Prog_ViewSettings b);
function void Prog_SetFolderViewScope(Prog_State *state, Prog_ViewScope scope);
function Prog_ViewSettings Prog_CurrentView(const Prog_State *state);
function void Prog_KeepLockedView(Prog_State *state);
function u32 Prog_ChoiceAt(Prog_Option option, u32 place);
function void Prog_CheckRecents(Prog_State *state);
function void Prog_Notify(Prog_State *state, Prog_NoticeKind kind, String key, String title, String message);
function String Prog__NoticeKey(Arena *arena, String verb);
function String Prog_NoticeKindName(Arena *arena, String key);
function u64 Prog_NoticeLeft(const Prog_Notice *notice, u64 now);
function void Prog_ToggleBranches(Prog_State *state);
function void Prog_SwitchBranch(Prog_State *state, String branch);
function u32 Prog_BranchMatches(const Prog_State *state, u32 *found);
function String Prog_ColumnsText(Arena *arena, Prog_ColumnSet set);
function b32 Prog_ParseColumns(String word, Prog_ColumnSet *set);
function void Prog_ToggleFolderView(Prog_State *state);
function void Prog_ClearFolderViews(Prog_State *state);
function Prog_ColumnSet Prog_PanelColumns(const Prog_State *state);
function f32 Prog_ColumnShare(Prog_ColumnKind kind);
function void Prog_EditColumns(Prog_State *state, Prog_CommandKind kind, u64 column, u64 value);
global const Prog_OptionDesc Prog_Options[Prog_Option_Count];
function u32 Prog_OptionMatches(Prog_State *state, u8 *found);
function const String *Prog_OptionChoices(const Prog_State *state, Prog_Option option, Arena *arena, u32 *count);
function i32 Prog_OptionValue(const Prog_State *state, Prog_Option option);
function void Prog_SetOption(Prog_State *state, Prog_Option option, i32 value);
function void Prog_UseLanguage(Prog_State *state, String name);
function b32 Prog_LanguageIsCustom(String name);
function void Prog_ListLanguages(Prog_State *state);
function String Prog_LanguageTemplate(Arena *arena, String name);
function b32 Prog_OptionIsDefault(const Prog_State *state, Prog_Option option);
function void Prog_RestoreOption(Prog_State *state, Prog_Option option);
function void Prog_SetHotkeys(Prog_State *state, Prog_CommandKind kind, const Prog_Hotkey *hotkeys, u32 count);
function void Prog_GatherTypes(Prog_State *state);
function void Prog_PreviewOption(Prog_State *state, Prog_Option option, i32 value);
function void Prog_PinBookmarks(Prog_State *state, const ArrayStr *paths, u64 at);
function u32 Prog_PanelIndex(const Prog_State *state, u64 id);
function u64 Prog__PanelId(const Prog_State *state);
function UI_Key Prog_PanelKeySeed(u64 id);
function void Prog__ReleaseTab(Prog_Tab *tab);
function u32 Prog_SplitPanel(Prog_State *state, u32 index, Prog_SplitKind kind, b32 before, String path);
function u32 Prog_InspectorPanel(Prog_State *state, u64 owner, b32 make);
function String Prog_UpPath(String path);
function b32 Prog__IsUnder(String path, String root);
function void Prog__SortDrives(Arena *arena, ArrayStr seen, P_ArrayPlace *drives);
function String Prog_BackPath(const Prog_State *state);
function void Prog_GoBack(Prog_State *state);
function String Prog_TrailText(const Prog_State *state);
function b32 Prog_MeasuresFolders(const Prog_State *state);
function b32 Prog_ReadyToShow(const Prog_State *state);
function void Prog_StartUp(Prog_State *state);
function String Prog_HomePath(Arena *arena);
function b32 Prog_ShowsThumbnails(const Prog_State *state);
function void Prog_ListInInspector(Prog_State *state, String path);
function void Prog_RequestUsage(Prog_State *state, String path, b32 again);
function void Prog_DropInspectorPanel(Prog_State *state, u64 owner);
function void Prog_ClosePanel(Prog_State *state, u32 index);
function void Prog_DropTab(Prog_State *state, Prog_TabDropTarget drop);
function void Prog_FocusPanelToward(Prog_State *state, i32 dx, i32 dy);
function b32 Prog_GroupOpen(const Prog_State *state, Prog_Group group);
function String Prog_TabPath(const Prog_State *state, u32 index);
function void Prog_OpenTab(Prog_State *state, String path, u32 at, b32 activate);
function void Prog_ShowInFolder(Prog_State *state, String path, b32 inTab);
function b32 Prog_TreeIsOpen(const Prog_State *state, String name);
function void Prog__SetList(Arena *arena, ArrayStr *list, const String *items, u64 count);
function void Prog_TreeToggle(Prog_State *state, String name);
function void Prog_TreeCommand(Prog_State *state, Prog_CommandKind kind);
function String Prog_NewTabPath(Prog_State *state, Arena *arena, String current);
function const P_Place *Prog_PathDrive(const Prog_State *state, String path);
function String Prog_PathName(const Prog_State *state, String path);
function void Prog_SelectTab(Prog_State *state, u32 index);
function u32 Prog_LayoutPanels(const Prog_State *state);
function void Prog_CloseTab(Prog_State *state, u32 index);
function void Prog_MoveTab(Prog_State *state, u32 from, u32 to);
function u32 *Prog_ResizeIcon(Arena *arena, P_Image source, i32 size);
function const Prog_Icon *Prog_ImageIcon(Prog_State *state, P_Image image);
function const Prog_Icon *Prog_SpinnerIcon(Prog_State *state, u64 nowMicroseconds);
function Prog_View Prog_ZoomView(u32 zoom);
function b32 Prog_TreeShowsNested(const Prog_State *state);
function u32 Prog_ZoomStepped(u32 zoom, b32 up);
function u32 Prog_ZoomFromSaved(u64 value);
function void Prog_OpenCommandMenu(Prog_State *state, const Prog_MenuCommand *items, u32 count, UI_Vec2 at);
function String Prog_HotkeyText(Arena *arena, const Prog_State *state, Prog_CommandKind kind);
function const Prog_Hotkey *Prog_FindHotkey(const Prog_State *state, Prog_HotkeyScope scope, Key lead, Key key, KeyMods mods);
function b32 Prog_ParseHotkey(String text, Prog_Hotkey *hotkey);
function b32 Prog_IsCharacterKey(Key key, KeyMods mods);
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
// How long an index saved by a finished walk serves as it is before another walk of the drives starts, as one an administrator's run
// read from the drives' file tables is worth keeping.
#define Prog_IndexFreshSeconds (24 * 60 * 60)
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
function void Prog_RedoLast(Prog_State *state);
