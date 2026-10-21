#pragma once

#include "../Platform/Files.h"

// The version Filey shows; a release build passes its own through EXTRA_CFLAGS.
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
    Prog_Sort_Count,
} Prog_Sort;

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
#define Prog_FolderColumnKinds (Prog_ColumnKind_FolderUsage + 1)

// A panel's columns in their order, each with its share of the row; a share of 0 is the kind's own, and no columns at all are the defaults.
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
};

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
    Prog_JobKind_Measure,
    Prog_JobKind_Inspect,
    Prog_JobKind_GoTo,
    Prog_JobKind_Index,
    Prog_JobKind_IndexLoad,
    Prog_JobKind_IndexFinish,
    Prog_JobKind_Transfer,
    Prog_JobKind_Trash,
    Prog_JobKind_Delete,
    Prog_JobKind_EmptyTrash,
    Prog_JobKind_Restore,
    Prog_JobKind_BinWait,      // waits until the bin's items at `paths` have left it, after a command of its menu      // `paths` from the bin `path` back where they came from, or all of it without any
    Prog_JobKind_Create,
    Prog_JobKind_Places,
    Prog_JobKind_Save,
    Prog_JobKind_Preview,
    Prog_JobKind_Undo,
    Prog_JobKind_HexPage,
    Prog_JobKind_PdfPage,
    Prog_JobKind_Git,
    Prog_JobKind_FilesList,
    Prog_JobKind_Reveal,
    Prog_JobKind_Fonts,
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
    u64 wanted;    // the last frame that drew or waited for it, which orders the thumbnails still to be made
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

typedef struct Prog_JobInput {
    enum8(Prog_JobKind) kind;
    u64 panel;  // the panel that asked, whose results these are; 0 for work of the whole window
    String path;
    u64 generation;
    enum8(Prog_Sort) sort;
    b32 descending;
    enum8(Prog_FolderSort) folderSort;
    b32 showHidden;
    b32 showSystem;
    b32 miller;            // FilesList: for a Miller column of `panel`, rather than the folder dropdown
    b32 explorer;          // Open: the folder `path` in the system's file manager
    b32 expanded;
    Prog_Partial *partial;  // an expanded listing's rows found so far, handed over while its walk goes on
    b32 addHistory;
    u64 historyAt;
    b32 passing;  // gone to by Up or a crumb, passing through the folder rather than opening it, which the recents do not count
    // The names of the recents inside `path`, which are marked, and sorted first with sortRecents.
    String *recentNames;
    u32 recentCount;
    b32 sortRecents;
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
    u64 offset;            // HexPage: where its page starts in the file; PdfPage: the page
    b32 hex;               // Preview: the file's bytes, whatever it holds
    u64 size;              // Preview: the file's size; PdfPage: the width to draw it at
    b32 inTab;             // Reveal: shown in a new tab
    b32 move;
    b32 folder;
    b32 archive;           // Preview: an archive the inspector's panel lists, which the job counts
    b32 confirm;           // EmptyTrash and Delete: the system asks first
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
} Prog_Transfer;

#define Prog_MaxTransfers 4

// What the preview pane shows of a file: a picture of Prog_PreviewSize squared, or the start of its text.
#define Prog_PreviewSize 512
// How many of the recents the sidebar and Go To show.
#define Prog_ShownRecentCount 10
#define Prog_PreviewBytes (1 << 20)
// The largest file inside an archive that is taken out to be previewed.
#define Prog_ArchivePreviewBytes (UINT64_C(256) << 20)
// A file shown as hex is read a page at a time as its rows come into sight, up to Prog_HexRows rows; past that the view would need more
// precision than its float scroll offset has.
#define Prog_HexPageBytes (1 << 20)
#define Prog_HexRows (1 << 20)
// The inspector's text size in points, unless changed.
#define Prog_InspectorFontSize 10
#define Prog_InspectorFontMin 8
#define Prog_InspectorFontMax 16

// A picture is the file's own at up to Prog_PreviewImageSide on its longer side when it is an image,
// else the system's thumbnail of Prog_PreviewSize squared.
#define Prog_PreviewImageSide 4096

// The git repository around a folder: its branch, or a detached HEAD's short hash, and how many commits it is ahead of its upstream and behind
// it, -1 for each when that is not known.
typedef struct Prog_GitStatus {
    String branch;
    i32 ahead, behind;
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
    u32 color, image;
    u32 bare;  // while read, the color for when its picture does not decode, 0 for `color`
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
    ArrayStr missing;  // the bookmarks whose folders are gone
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
    Prog_Option_Font,
    Prog_Option_FontSize,
    Prog_Option_InspectorFont,
    Prog_Option_InspectorFontSize,
    Prog_Option_Spacing,
    Prog_Option_Animations,
    Prog_Option_RoundCorners,
    Prog_Option_InlineSubmenus,
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
    Prog_Option_AlternatingRows,
    Prog_Option_InactiveOpacity,
    Prog_Option_ShowFileExtension,
    Prog_Option_ShowHiddenFiles,
    Prog_Option_ShowSystemFiles,
    Prog_Option_ShortenedPaths,
    Prog_Option_Breadcrumbs,
    Prog_Option_ShowCheckboxes,
    Prog_Option_ConfirmEmptyTrash,
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
    Prog_Option_Overscroll,
    Prog_Option_ReverseScroll,
    Prog_Option_CharacterKeyAction,
    Prog_Option_InspectorLayout,
    Prog_Option_HighlightCode,
    Prog_Option_LoadOnStartup,
    Prog_Option_NewTabShow,
    Prog_Option_VSync,
    Prog_Option_Transparency,
    Prog_Option_WindowOpacity,
    Prog_Option_PopupBlur,
    Prog_Option_Backdrop,
    Prog_Option_Count,
} Prog_Option;

typedef enum Prog_OptionControl {
    Prog_OptionControl_Switch,
    Prog_OptionControl_Stepper,
    Prog_OptionControl_Slider,
    Prog_OptionControl_Choice,
    Prog_OptionControl_Font,  // a font family's name, chosen from the installed ones
    Prog_OptionControl_Expand,  // opens and closes the rows after it
    Prog_OptionControl_Color,   // one of the scheme's colors, which the color picker changes
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
// Each name is packed as its length in two bytes and then its bytes, in `arena` or in the saved index file read into it.
typedef struct Prog_Index {
    Arena arena;
    Arena tables;           // names, parents and flags, made for Prog_MaxIndexed entries at once so that they never move and leave copies behind
    const u8 **names;
    u32 *parents;           // UINT32_MAX for a root
    u8 *flags;              // Prog_IndexFlag_
    u64 count;
} Prog_Index;

// What was found inside the first `listed` of an index job's folders, in order, each with the entry of the folder it is in.
// IndexLoad hands back a whole index the same way, `count` entries in its own arena.
typedef struct Prog_Indexed {
    String *names;
    const u8 **packed;  // IndexLoad: the names, in the file read
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
    Prog_LayoutAsk_Count,
} Prog_LayoutAsk;

#define Prog_MaxLayouts 64
// Default and the reference's six, which the custom schemes follow.
#define Prog_BuiltInSchemes 7
// The colors a scheme is made of, its groups, as the Options list them from Prog_Option_ColorClear on.
#define Prog_ColorGroupCount 33

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
    enum8(Prog_Sort) sort;
    b32 descending;
    enum8(Prog_FolderSort) folderSort;
    b32 showHidden;
    enum8(P_DriveKind) driveKind;
    u64 recentCount;  // entries among the recents
    b32 hidden;       // the folder itself, as P_DirectoryResult says
    // Every subfolder's items are listed too, named by their path below `path` with '/' separators.
    b32 expanded;
    b32 archive;      // a folder inside an archive, listed from it
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
    R_TextureHandle *images;
    u32 imageCount;
    f32 center[3], radius;
    b32 zUp;
    f32 turn[3][3];
    f32 yaw, pitch, zoom;
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
//   previewPicture: the texture holds previewPath's picture, previewTextureWidth by previewTextureHeight; previewImage: it is an image file's own,
//     previewImageWidth by previewImageHeight; previewFolder: a folder, which the inspector's panel lists
//   previewHex: the file shows as hex from hexPages, its pages read so far, NULL for the rest, in previewArena, hexPageCount of them out of
//     hexSize bytes, hexPageJob the page on its way, one at a time
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
//   inspectorFilter, inspectorFiltering: the text view's filter, with the keyboard in its box, which keeps previewKept, previewKeptCount lines,
//     picked for its version previewKeptVersion
#define Prog_PreviewFields(X) \
    X(b32, previewShown) X(Arena, previewArena) X(String, previewPath) X(P_Job *, previewJob) X(String, previewText) X(R_TextureHandle, previewTexture) \
    X(Prog_ModelView, model) X(Prog_PdfView, pdf) \
    X(b32, previewPicture) X(i32, previewTextureWidth) X(i32, previewTextureHeight) X(b32, previewImage) X(i32, previewImageWidth) X(i32, previewImageHeight) \
    X(b32, previewFolder) X(b32, previewHex) X(u64, hexSize) X(u8 **, hexPages) X(u64, hexPageCount) X(P_Job *, hexPageJob) \
    X(b32, previewArchive) X(b32, previewDocument) X(P_DirectoryTotals, previewArchiveTotals) X(b32, previewArchiveCounted) X(u64, previewStamp) \
    X(u64, previewSubmitted) \
    X(enum8(Prog_ImageScale), imageScaleMode) X(f32, imageScale) X(f32, imageFit) X(UI_Vec2, imagePan) X(UI_Vec2, imageDragFrom) \
    X(u32 *, previewLines) X(enum8(Prog_Syntax), previewSyntax) X(u8 *, previewLineStates) X(u32, previewLineCount) X(u32 *, previewRows) \
    X(u32, previewRowColumns) X(u64, previewAnchor) X(u64, previewCaret) X(u32, previewClicks) X(b32, previewTextFocus) X(R_Rect, inspectorTextRect) \
    X(b32, inspectorResetScroll) X(Prog_TextEdit, inspectorFilter) X(b32, inspectorFiltering) X(u32 *, previewKept) X(u32, previewKeptCount) \
    X(u32, previewKeptVersion)

// The Prog_State fields each tab has its own of.
// The shown tab's live in Prog_State; switching swaps them with the tab's slot, so a tab keeps its listing, selection, filter, scroll and view:
// zoom, up to Prog_ZoomMax, the details columns (columnSet, see Prog_PanelColumns), and while a locked folder's view applies, the tab's own
// (ownView, ownSettings), which the next folder without one gets back.
// The filter keeps the rows it lets through in `view`, best match first and in listing order within a score; rows it hides are never selected, so commands cannot act on them.
#define Prog_TabFields(X) \
    X(Arena, directoryArena) X(Prog_Directory, directory) X(Arena, historyArena) X(ArrayStr, history) X(u64, historyAt) \
    X(u64, focusedRow) X(u64, anchorRow) X(u64, selectedCount) X(b32, hasFocus) X(b32, hasAnchor) \
    X(b32, showHidden) X(enum8(Prog_Sort), sort) X(b32, descending) X(enum8(Prog_FolderSort), folderSort) X(u32, zoom) X(Prog_ColumnSet, columnSet) \
    X(b32, ownView) X(Prog_ViewSettings, ownSettings) \
    X(Prog_TextEdit, filter) X(b32, filterSticky) X(b32, expanded) X(b32, hideFolders) X(b32, hideFiles) X(Prog_TypeFilter, types) \
    X(Arena, viewArena) X(u64 *, view) X(u64, viewCount) X(u64, viewFolders) X(UI_Vec2, scroll) X(Prog_Trail, trail) \
    Prog_PreviewFields(X)

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
//   revealFocus, centerFocus, resetScroll, restoreScroll: scroll changes the next build makes; pageRows: lines visible in the last layout, for Page Up/Down
//   tabs, tabCount, activeTab: the panel's Prog_MaxTabs tab slots, in tabArena from the first use; the shown one's slot holds nothing while it is shown
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
//   statusRight: how wide the status bar's counts, view and zoom were in the last layout, which the filter box leaves room for
//   listScrolls: its items ran past the bottom in the last layout
//   inspectorView: how its inspector lists folders, kept while the inspector's panel is closed
//   miller, millerColumns: Miller columns are on, and the folders they list, made in millerArena once they first show
#define Prog_PanelFields(X) \
    X(Arena, errorArena) X(String, error) X(P_Job *, directoryJob) X(u64, generation) X(b32, loading) \
    X(b32, revealFocus) X(b32, centerFocus) X(b32, resetScroll) X(b32, restoreScroll) X(u64, pageRows) \
    X(Arena, tabArena) X(Prog_Tab *, tabs) X(u32, tabCount) X(u32, activeTab) \
    X(u64, gridColumns) X(UI_Vec2, gridPitch) X(R_Rect, gridItem) \
    X(b32, marquee) X(UI_Key, marqueeKey) X(UI_Vec2, marqueeAnchor) X(R_Rect, marqueeBox) X(KeyMods, marqueeAdds) \
    X(b32, filtering) X(R_Rect, filterRect) X(u64, listing) X(P_Job *, measureJob) X(b32, sortPending) \
    X(P_DirectoryWatch, watch) X(b32, watchDirty) X(u64, watchReloadTime) X(R_Rect, focusedRowRect) X(u64, renameCreatedUntil) X(R_Rect, panelRect)     X(u64, listStarted) X(u64, listTook) \
    X(f32, statusRight) X(b32, listScrolls) \
    X(Prog_ViewSettings, inspectorView) \
    X(Arena, gitArena) X(Prog_GitStatus, git) X(P_Job *, gitJob) X(b32, gitAgain) X(u64, gitTime) \
    X(b32, miller) X(Arena, millerArena) X(Prog_MillerColumns *, millerColumns)

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
    // ponytail: fixed page count (8192 icons at 16 px); later icons fall back to glyphs.
    Prog_IconAtlas iconAtlas;
    // Thumbnails for the icon views, keyed by P_FileThumbnailKey, loaded at the tier of sizes at or above what the zoom draws and scaled down
    // to it, so that most zoom steps load nothing. Each tier in use has a slot of its own, so that panels zoomed apart keep theirs; a new
    // tier takes the slot drawn from longest ago, and until its thumbnails come another slot's are drawn. A full atlas drops its slot's
    // thumbnails, and the items still shown ask again.
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
    u32 menuPath[8];          // the open submenu item at each level
    R_Rect menuRects[9];      // each open level's panel in the last build, empty past the last one
    b32 menuApart[9];         // each submenu that could not sit against its opener's panel in the last build, whose opener is then lit
    u32 menuSquare[9];        // each level's corners that the submenu joined to it squared off in the last build, as R_Shape flags
    b32 menuInline;           // submenus open inside their menu, under their item, rather than beside it
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
    // The palette's Quick group: the starred commands by name, in the order they were starred.
    Arena quickArena;
    ArrayStr quickCommands;
    P_ContextMenu menuBase;
    UI_Vec2 menuPointer;      // where the pointer was in the last build, as only its moving decides the highlight
    b32 menuPointerMoved;
    String menuMoved;         // the label of the item last pinned or unpinned, in menuArena, which slides to its place lit
    u32 menuDepth;            // open submenu levels below the top one
    u32 menuHighlight;        // item index, or UINT32_MAX for none
    const Prog_Icon **menuIcons;  // per item, NULL when it has no picture
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
    b32 paletteShown;         // it was open last frame, so that a close starts its fade
    u64 paletteClosedAt;      // when it closed, while it fades out
    f32 paletteFade;          // its opacity as it fades out, 1 while open
    Prog_TextEdit palette;
    u32 paletteHighlight;     // UINT32_MAX for none, which Go To starts with from Edit Path
    u32 paletteScroll;        // the first command row shown
    b32 paletteWheeled;       // the wheel scrolled the list last, which keeps its place rather than following the highlight until a key is pressed
    u16 paletteExpanded;      // bits by Prog_CommandGroup, the groups shown open
    b32 paletteCompact;       // its rows are only the commands' names, without their descriptions
    u32 fitColumn;            // the active panel's column, by its place + 1, that the next build sizes to its values; UINT32_MAX all of them
    u64 listBytes, volumeBytes;  // what the usage columns of the list being built are shares of: its items' bytes and its drive's
    // What Settings.conf last set the inspector's text size and layout to: they apply when the file changes them, and the inspector's own controls change them meanwhile.
    u32 settingInspectorFontSize;
    u8 settingInspectorLayout;  // its value + 1, 0 before any
    u8 scheme;                  // the settings' Color Scheme, by its place among the built-in schemes and then the custom ones
    u8 lightScheme;             // the one the system's light mode brings, as `scheme`
    Prog_FontName schemeName, lightSchemeName;  // their names
    // Custom color schemes, kept in Schemes.conf: their names and their colors as "Group = #rrggbb" lines.
    Arena schemesArena;
    ArrayStr schemeNames, schemeTexts;
    u32 schemeGroups[Prog_ColorGroupCount];      // the shown scheme's colors as 0xRRGGBB, by Prog_Option_ColorClear's order
    b32 colorsExpanded;         // Options shows the scheme's colors
    // The schemes' dropdown under a scheme option while open: that option + 1, and its highlighted row.
    u8 schemesOpen;
    u32 schemesHighlight;
    u64 settingsHash;           // Settings.conf and Schemes.conf as last applied
    // The color picker while open: the option of the color it changes, and that color as hue, saturation and brightness from 0 to 1.
    b32 colorPicking;
    enum8(Prog_Option) colorOption;
    f32 pickerHue, pickerSaturation, pickerBrightness;
    b32 systemScheme;           // the system's light or dark choice picks the light scheme or that one
    // How the lists look: the space between their items and how much inactive panels show, as offsets from the default 4 and 60 so that
    // {0} has them, square corners and every other row tinted.
    i32 spacingShift;
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
    // Where a start and a new tab go, how the recents are kept, whether the lists mark them and sort them first, and whether frames wait for the display.
    enum8(Prog_Startup) startup;
    enum8(Prog_NewTab) newTab;
    enum8(Prog_StoreRecents) storeRecents;
    b32 showRecents, sortRecents;
    b32 noVSync;
    b32 noHighlight;          // inspector.highlight = false: the inspector shows code in one color
    // The families of the UI's text and of the inspector's, by name.
    Prog_FontName font, inspectorFont;
    b32 noFolderSizes;
    b32 noFolderSizesSolidState, folderSizesSpinning, folderSizesNetwork;
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
    b32 gotoShowHidden;            // what the shown tab showed of the hidden items when the pool was filled
    Prog_PathSet gotoNearbyNames;  // the names of the folders close at hand, which the index finds again only under them
    Prog_PathSet gotoNearbyPaths;
    Prog_PathSet gotoSeen;
    u8 gotoFoundText[1024];
    u32 gotoFoundTextLength;
    b32 gotoSearching;        // the look has not reached the end yet
    Prog_Transfer transfers[Prog_MaxTransfers];
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
    b32 setupDesktop, setupOnPath, setupDefault;
    R_TextureHandle setupLogo;
    i32 setupLogoSize;
    const R_Font *setupFonts[3];
    Prog_TextEdit layoutName;
    // What the layouts ask first, about the layout named `layoutAskName`.
    enum8(Prog_LayoutAsk) layoutAsk;
    u8 layoutAskName[256];
    u32 layoutAskLength;
    // The installed font families once listed, which the font picker offers for the option `fontOption`.
    Arena fontsArena;
    String *fontNames;
    u64 fontCount;
    P_Job *fontsJob;
    enum8(Prog_Option) fontOption;
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
    b32 inspectorDetails;     // the card of the item's details under its name
    b32 inspectorDetailsFolded; // showing only its title
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
    // The Quick Access filter: what its box holds, which leaves the sidebar the items whose names it finds, and whether the box has the keyboard.
    Prog_TextEdit sidebarFilter;
    b32 sidebarFiltering;
    R_Rect sidebarFilterRect;  // the box, as the last build laid it out
    f32 sidebarWidth;         // logical pixels its edge was dragged to; 0 for the default
    f32 sidebarGrab;          // how far right of its edge line the pointer took it
    PlatformCursor cursor;    // the pointer's shape the last build asked for, over an edge that drags
    b32 groupToggled[Prog_Group_Count];  // differs from the group's default, which is open for bookmarks and storage
    b32 placesWanted;         // the sidebar was built, so its places are kept current
    b32 windowInactive;       // another window has the focus, which dims the title strip
    b32 drivesChanged;        // the system told of a drive or medium coming or going, which the places are listed again for
    Arena placesArena;        // the latest places listing
    P_ArrayPlace places;
    P_ArrayPlace drives;
    ArrayStr pathRoots;       // folders that breadcrumbs start from, for folders inside them
    ArrayStr missingBookmarks;  // bookmarks whose folders were gone at the last listing, which the sidebar fades
    P_Job *placesJob;
    u64 placesTime;
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
    b32 requestPassing;            // the next Prog_RequestDirectory passes through its folder, as Up does
    // One save of each list at a time, since two writing one file at once collide; a change during a save saves again after it.
    b32 saving[5];            // bookmarks, recents, folder views, menu pins, quick commands
    b32 saveAgain[5];
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
    // Loud colors for telling UI surfaces apart; FILEY_DEBUG_THEME turns it on at startup.
    b32 debugTheme;
    // "animations = false" in the settings: menus, scrolling and the caret change at once.
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
// The path the crumbs show, where the shown folder ends in it, and where each crumb ends, up to `max`; none in This PC.
function u32 Prog_CrumbEnds(Prog_State *state, String *path, u64 *shownEnd, u64 *ends, u32 max);
// A picture of Prog_PreviewSize squared of the 3D model in the file of `size` bytes at `path`; NULL for a file that is not one or does not read.
// The model at `path` as a mesh for the GPU, Y up, with only triangles whose vertices are there, and the center and radius of its bounds.
function b32 Prog_ModelMesh(Arena *arena, String path, u64 size, const P_CancelToken *cancel, Prog_ModelShape *shape);
// A 3D model's picture for the icon views, `size` squared, in straight RGBA; NULL when `path` is no model the readers know or has nothing to draw.
function u32 *Prog_ModelThumbnail(Arena *arena, String path, i32 size, const P_CancelToken *cancel);
// Whether `path` names a 3D model the readers know, by its extension.
function b32 Prog_IsModel(String path);
// A Word document's or a spreadsheet's text for the inspector, from the zip archive of XML a .docx or an .xlsx is; empty when it has none.
function b32 Prog_IsOffice(String path);
function String Prog_OfficeText(Arena *arena, String path, u64 size);
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
function void Prog_SetDebugTheme(Prog_State *state, b32 debug);
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
function void Prog_LoadSettings(Prog_State *state, String configDirectory, String cacheDirectory);
function void Prog_SessionWindow(String configDirectory, PlatformWindowDesc *desc);
function b32 Prog_IsBookmarked(const Prog_State *state, String path);
function void Prog_ToggleBookmark(Prog_State *state, String path);
function b32 Prog_FolderView(const Prog_State *state, String path, Prog_ViewSettings *view);
function Prog_ViewSettings Prog_CurrentView(const Prog_State *state);
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
function String Prog_TrailText(const Prog_State *state);
function b32 Prog_MeasuresFolders(const Prog_State *state);
function b32 Prog_ReadyToShow(const Prog_State *state);
function void Prog_StartUp(Prog_State *state);
function String Prog_HomePath(Arena *arena);
function b32 Prog_ShowsThumbnails(const Prog_State *state);
function void Prog_ListInInspector(Prog_State *state, String path);
function void Prog_DropInspectorPanel(Prog_State *state, u64 owner);
function void Prog_ClosePanel(Prog_State *state, u32 index);
function void Prog_DropTab(Prog_State *state, Prog_TabDropTarget drop);
function void Prog_FocusPanelToward(Prog_State *state, i32 dx, i32 dy);
function b32 Prog_GroupOpen(const Prog_State *state, Prog_Group group);
function String Prog_TabPath(const Prog_State *state, u32 index);
function void Prog_OpenTab(Prog_State *state, String path, u32 at, b32 activate);
function String Prog_NewTabPath(Prog_State *state, Arena *arena, String current);
function const P_Place *Prog_PathDrive(const Prog_State *state, String path);
function String Prog_PathName(const Prog_State *state, String path);
function void Prog_SelectTab(Prog_State *state, u32 index);
function void Prog_CloseTab(Prog_State *state, u32 index);
function void Prog_MoveTab(Prog_State *state, u32 from, u32 to);
function u32 *Prog_ResizeIcon(Arena *arena, P_Image source, i32 size);
function const Prog_Icon *Prog_ImageIcon(Prog_State *state, P_Image image);
function const Prog_Icon *Prog_SpinnerIcon(Prog_State *state, u64 nowMicroseconds);
function Prog_View Prog_ZoomView(u32 zoom);
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
