#define Prog_CommandsTable
#include "Meta/Commands.meta.h"
#include "Meta/Icons.meta.h"

function Prog_Command *Prog_QueueCommand(Prog_State *state, Prog_CommandKind kind, u64 row, u32 pressCount, u64 inputOrder)
{
    Assert(kind > Prog_CommandKind_None && kind < Prog_CommandKind_Count);
    Prog_Command *command = ArenaPushStruct(&state->frameArena, Prog_Command);
    command->kind = (u8)kind;
    command->panel = Prog__PanelId(state);
    command->row = row;
    command->pressCount = pressCount;
    command->inputOrder = inputOrder;
    // Widgets are built in tree order; input commands execute in event order.
    Prog_Command **link = &state->commandFirst;
    while (*link && (*link)->inputOrder <= inputOrder) {
        link = &(*link)->next;
    }
    command->next = *link;
    *link = command;
    if (command->next == NULL) {
        state->commandLast = command;
    }
    return command;
}

function void Prog_Reload(Prog_State *state)
{
    String path = state->directory.path;
    b32 addHistory = false;
    u64 historyAt = state->historyAt;
    if (state->directoryJob) {
        Prog_JobInput *input = state->directoryJob->input;
        path = input->path;
        addHistory = input->addHistory;
        historyAt = input->historyAt;
    }
    if (path.length) Prog_RequestDirectory(state, path, addHistory, historyAt);
}

function void Prog_CloseMenu(Prog_State *state)
{
    if (state->menuOpen) P_ContextMenuClose(&state->menu);
    ArenaClear(&state->menuArena);
    state->menu = (P_ContextMenu){0};
    state->menuOpen = false;
    state->menuRows = NULL;
    state->menuTarget = (String){0};
    state->menuIcons = NULL;
    state->menuCommands = NULL;
    state->menuRowCount = 0;
    state->menuDepth = 0;
    state->menuHighlight = UINT32_MAX;
    state->menuOpener = Prog_CommandKind_None;
    state->menuRight = 0;
    state->menuSearch = (Prog_TextEdit){0};
}

// Shown items of a menu level: unlabeled owner-drawn items are dropped, and so are separators at the ends or next to another separator.
function b32 Prog__ContainsFolded(String text, String needle);
function u64 Prog__CrumbStart(const Prog_State *state, String path, b32 *drive);
function void Prog__EditReplace(Prog_TextEdit *edit, const u8 *bytes, u32 count);

function u32 Prog_MenuVisible(const P_ContextMenu *menu, u32 first, u32 count, String filter, u32 *out)
{
    u32 result = 0;
    for (u32 i = first; i < first + count && i < menu->count; ++i) {
        const P_MenuItem *item = &menu->items[i];
        if (filter.length) {
            if (!item->separator && Prog__ContainsFolded(item->label, filter)) out[result++] = i;
        } else if (item->separator) {
            if (result && !menu->items[out[result - 1]].separator) out[result++] = i;
        } else if (item->label.length) {
            out[result++] = i;
        }
    }
    while (result && menu->items[out[result - 1]].separator) --result;
    return result;
}

// What the system menu's search narrows `level` to; only its top level is searched, and Filey's own menus are not.
function String Prog_MenuFilter(const Prog_State *state, u32 level)
{
    return level == 0 && !state->menuCommands ? Prog_EditText(&state->menuSearch) : (String){0};
}

// The level the keyboard moves in: the deepest open one.
function void Prog__MenuLevel(const Prog_State *state, u32 *first, u32 *count)
{
    *first = 0;
    *count = state->menu.rootCount;
    if (state->menuDepth) {
        const P_MenuItem *parent = &state->menu.items[state->menuPath[state->menuDepth - 1]];
        *first = parent->firstChild;
        *count = parent->childCount;
    }
}

function void Prog__MenuMove(Prog_State *state, i32 step)
{
    u32 first = 0, count = 0;
    Prog__MenuLevel(state, &first, &count);
    Scratch scratch = GetScratch();
    u32 *visible = ArenaPushArray(scratch.arena, Max(count, 1u), u32);
    u32 shown = Prog_MenuVisible(&state->menu, first, count, Prog_MenuFilter(state, state->menuDepth), visible);
    i64 at = -1;
    for (u32 i = 0; i < shown; ++i) if (visible[i] == state->menuHighlight) at = i;
    for (u32 tries = 0; tries < shown; ++tries) {
        at = at < 0 ? (step > 0 ? 0 : (i64)shown - 1) : (at + step + (i64)shown) % (i64)shown;
        const P_MenuItem *item = &state->menu.items[visible[at]];
        if (!item->separator && (!item->disabled || item->childCount)) {
            state->menuHighlight = visible[at];
            break;
        }
    }
    ReleaseScratch(scratch);
}

function void Prog__MenuEnter(Prog_State *state)
{
    if (state->menuHighlight >= state->menu.count || state->menuDepth >= CArrayCount(state->menuPath)) return;
    state->menuPath[state->menuDepth++] = state->menuHighlight;
    state->menuHighlight = UINT32_MAX;
    Prog__MenuMove(state, 1);
}

// Shell menus and drags take items of one folder.
// In an expanded listing that is the folder of the first row; rows in other folders are dropped from `rows`.
// Fills `names` with the rows' names inside that folder and returns its path.
function String Prog__RowsFolder(Prog_State *state, Arena *arena, u64 *rows, u64 *count, String *names)
{
    String parent = *count ? Prog_EntryParent(state->directory.entries[rows[0]].file.name) : (String){0};
    u64 kept = 0;
    for (u64 i = 0; i < *count; ++i) {
        String name = state->directory.entries[rows[i]].file.name;
        if (!StrIsEqual(Prog_EntryParent(name), parent)) continue;
        rows[kept] = rows[i];
        names[kept++] = Prog_EntryLeaf(name);
    }
    *count = kept;
    return parent.length ? Prog_JoinPath(arena, state->directory.path, parent) : state->directory.path;
}

// Opens Filey's own menu of `items` with its corner at `at`.
function void Prog_OpenCommandMenu(Prog_State *state, const Prog_MenuCommand *items, u32 count, UI_Vec2 at)
{
    Prog_CloseMenu(state);
    if (!count) return;
    state->menu.items = ArenaPushArray(&state->menuArena, count, P_MenuItem);
    state->menuCommands = ArenaPushArray(&state->menuArena, count, Prog_MenuCommand);
    state->menuIcons = ArenaPushArray(&state->menuArena, count, const Prog_Icon *);
    for (u32 i = 0; i < count; ++i) {
        Prog_MenuCommand item = items[i];
        item.label = StrCopy(&state->menuArena, item.label.length ? item.label : Prog_Commands[item.kind].label);
        state->menuCommands[i] = item;
        state->menu.items[i] = (P_MenuItem){ .label = item.label, .id = i + 1, .separator = item.kind == Prog_CommandKind_None, .isDefault = (b8)(item.checked != 0) };
    }
    state->menu.count = state->menu.rootCount = count;
    state->menuOpen = true;
    state->menuAt = at;
    // The keyboard starts on the first item, as the reference's menus do.
    state->menuHighlight = UINT32_MAX;
    for (u32 i = 0; i < count && state->menuHighlight == UINT32_MAX; ++i) if (!state->menu.items[i].separator) state->menuHighlight = i;
}

// Key names as Commands.txt and the settings spell them, and shorter ones for showing.
global const char *Prog_KeyNames[Key_Count] = {
    [Key_Escape] = "Escape", [Key_Enter] = "Enter", [Key_Tab] = "Tab", [Key_Backspace] = "Backspace", [Key_Delete] = "Delete", [Key_Insert] = "Insert",
    [Key_Space] = "Space", [Key_Left] = "Left", [Key_Right] = "Right", [Key_Up] = "Up", [Key_Down] = "Down", [Key_Home] = "Home", [Key_End] = "End",
    [Key_PageUp] = "PageUp", [Key_PageDown] = "PageDown", [Key_ContextMenu] = "ContextMenu", [Key_Comma] = "Comma",
};
global const char *Prog_KeyShortNames[Key_Count] = {
    [Key_Escape] = "Esc", [Key_Enter] = "Enter", [Key_Tab] = "Tab", [Key_Backspace] = "Backspace", [Key_Delete] = "Del", [Key_Insert] = "Ins",
    [Key_Space] = "Space", [Key_Left] = "Left", [Key_Right] = "Right", [Key_Up] = "Up", [Key_Down] = "Down", [Key_Home] = "Home", [Key_End] = "End",
    [Key_PageUp] = "PgUp", [Key_PageDown] = "PgDn", [Key_ContextMenu] = "Menu", [Key_Comma] = ",",
};

function b32 Prog__EqualFolded(String a, String b)
{
    if (a.length != b.length) return false;
    for (u64 i = 0; i < a.length; ++i) {
        if (Prog__FoldAscii((u8)a.value[i]) != Prog__FoldAscii((u8)b.value[i])) return false;
    }
    return true;
}

// The hotkey `key` with `mods` runs: the settings' own first, then the defaults of the commands they leave alone.
function const Prog_Hotkey *Prog_FindHotkey(const Prog_State *state, Key key, KeyMods mods)
{
    for (u32 i = 0; i < state->userHotkeyCount; ++i) {
        if (state->userHotkeys[i].key == key && state->userHotkeys[i].mods == mods) return &state->userHotkeys[i];
    }
    for (u64 i = 0; i < CArrayCount(Prog_DefaultHotkeys); ++i) {
        const Prog_Hotkey *hotkey = &Prog_DefaultHotkeys[i];
        if (hotkey->key == key && hotkey->mods == mods && !state->hotkeyOverridden[hotkey->command]) return hotkey;
    }
    return NULL;
}

// "Ctrl+Shift+S", modifiers in any order and case; false when it names no key.
function b32 Prog_ParseHotkey(String text, Key *key, KeyMods *mods)
{
    *key = Key_None;
    *mods = 0;
    for (u64 at = 0; at <= text.length;) {
        Index plus = StrIndexChar(StrFrom(text, at), '+', SSF_CountOnFail);
        String part = StrFromTo(text, at, (i64)(at + plus.value));
        while (part.length && part.value[0] == ' ') part = StrFrom(part, 1);
        while (part.length && part.value[part.length - 1] == ' ') --part.length;
        at += plus.value + 1;
        b32 last = at > text.length;
        if (!part.length) return false;
        if (!last) {
            if (Prog__EqualFolded(part, S("Ctrl")) || Prog__EqualFolded(part, S("Control"))) *mods |= KeyMod_Control;
            else if (Prog__EqualFolded(part, S("Shift"))) *mods |= KeyMod_Shift;
            else if (Prog__EqualFolded(part, S("Alt"))) *mods |= KeyMod_Alt;
            else if (Prog__EqualFolded(part, S("Super")) || Prog__EqualFolded(part, S("Win")) || Prog__EqualFolded(part, S("Meta"))) *mods |= KeyMod_Super;
            else return false;
            continue;
        }
        u8 c = Prog__FoldAscii((u8)part.value[0]);
        if (part.length == 1 && c >= 'a' && c <= 'z') *key = (Key)(Key_A + (c - 'a'));
        else if (part.length == 1 && c >= '0' && c <= '9') *key = (Key)(Key_0 + (c - '0'));
        else if (part.length >= 2 && c == 'f' && part.value[1] >= '1' && part.value[1] <= '9') {
            u32 number = (u32)(part.value[1] - '0');
            if (part.length == 3 && part.value[2] >= '0' && part.value[2] <= '9') number = number * 10 + (u32)(part.value[2] - '0');
            if (part.length <= 3 && number >= 1 && number <= 12) *key = (Key)(Key_F1 + number - 1);
        } else {
            for (u32 k = 0; k < Key_Count && *key == Key_None; ++k) {
                if ((Prog_KeyNames[k] && Prog__EqualFolded(part, StrFromCStr(Prog_KeyNames[k]))) || (Prog_KeyShortNames[k] && Prog__EqualFolded(part, StrFromCStr(Prog_KeyShortNames[k])))) *key = (Key)k;
            }
        }
    }
    return *key != Key_None;
}

// "Ctrl+Shift+S" for the first hotkey of `kind`, the settings' own before the defaults; empty when it has none.
// Every hotkey of `kind`, up to `max` of them, in the order they are looked up; returns how many.
function u32 Prog_HotkeyTexts(Arena *arena, const Prog_State *state, Prog_CommandKind kind, String *out, u32 max)
{
    u32 count = 0;
    for (u64 i = 0; i < state->userHotkeyCount + CArrayCount(Prog_DefaultHotkeys) && count < max; ++i) {
        b32 user = i < state->userHotkeyCount;
        const Prog_Hotkey *hotkey = user ? &state->userHotkeys[i] : &Prog_DefaultHotkeys[i - state->userHotkeyCount];
        if (hotkey->command != kind || (!user && state->hotkeyOverridden[kind])) continue;
        char key[4] = {0};
        const char *name = hotkey->key < Key_Count ? Prog_KeyShortNames[hotkey->key] : NULL;
        if (hotkey->key >= Key_A && hotkey->key <= Key_Z) key[0] = (char)('A' + (hotkey->key - Key_A));
        else if (hotkey->key >= Key_0 && hotkey->key <= Key_9) key[0] = (char)('0' + (hotkey->key - Key_0));
        else if (hotkey->key >= Key_F1 && hotkey->key <= Key_F12) {
            u32 number = (u32)(hotkey->key - Key_F1) + 1;
            key[0] = 'F';
            key[1] = (char)(number < 10 ? '0' + number : '1');
            key[2] = (char)(number < 10 ? 0 : '0' + number - 10);
        }
        if (key[0]) name = key;
        if (!name) continue;
        out[count++] = StrFormat(arena, "%s%s%s%s%s", (hotkey->mods & KeyMod_Control) ? "Ctrl+" : "", (hotkey->mods & KeyMod_Alt) ? "Alt+" : "",
                                 (hotkey->mods & KeyMod_Shift) ? "Shift+" : "", (hotkey->mods & KeyMod_Super) ? "Super+" : "", name);
    }
    return count;
}

function String Prog_HotkeyText(Arena *arena, const Prog_State *state, Prog_CommandKind kind)
{
    String result = {0};
    Prog_HotkeyTexts(arena, state, kind, &result, 1);
    return result;
}

// Whether `needle` appears in `text` as one run, ignoring ASCII case.
function b32 Prog__ContainsFolded(String text, String needle)
{
    for (u64 start = 0; start + needle.length <= text.length; ++start) {
        u64 i = 0;
        while (i < needle.length && Prog__FoldAscii((u8)text.value[start + i]) == Prog__FoldAscii((u8)needle.value[i])) ++i;
        if (i == needle.length) return true;
    }
    return false;
}

// The palette's groups and their commands, in the reference's order. Quick holds the most used commands, which their own groups then leave out.
#define Prog__PaletteGroup(name) ((u32)Prog_CommandKind_Count + (u32)Prog_CommandGroup_##name)
global const u16 Prog_PaletteTree[] = {
    Prog__PaletteGroup(Quick),
    Prog_CommandKind_ToggleExpandFolder, Prog_CommandKind_TogglePreview, Prog_CommandKind_SplitRight, Prog_CommandKind_SplitBottom, Prog_CommandKind_TabNew,
    Prog_CommandKind_TabClose, Prog_CommandKind_OpenOptions, Prog_CommandKind_ToggleHidden,
    Prog__PaletteGroup(Folder),
    Prog_CommandKind_OpenFocused, Prog_CommandKind_OpenInTab, Prog_CommandKind_CopySelection, Prog_CommandKind_CutSelection, Prog_CommandKind_Paste,
    Prog_CommandKind_Trash, Prog_CommandKind_SelectionDelete, Prog_CommandKind_Duplicate, Prog_CommandKind_Undo, Prog_CommandKind_Rename,
    Prog_CommandKind_CopyAsPath, Prog_CommandKind_CreateFile, Prog_CommandKind_CreateFolder, Prog_CommandKind_ToggleFolders, Prog_CommandKind_ToggleFiles,
    Prog_CommandKind_EditPath, Prog_CommandKind_StartFilter, Prog_CommandKind_StartExpandedSearch, Prog_CommandKind_ApplyFilter, Prog_CommandKind_RemoveFilter,
    Prog_CommandKind_ToggleFilterPin, Prog_CommandKind_Refresh, Prog_CommandKind_Back, Prog_CommandKind_Forward, Prog_CommandKind_Up,
    Prog_CommandKind_BookmarksToggle,
    Prog__PaletteGroup(View),
    Prog_CommandKind_ZoomIn, Prog_CommandKind_ZoomOut, Prog_CommandKind_ViewDetails, Prog_CommandKind_ViewList, Prog_CommandKind_ViewColumns,
    Prog_CommandKind_ViewMediumIcons, Prog_CommandKind_ViewLargeIcons, Prog_CommandKind_ViewXLIcons, Prog_CommandKind_FolderViewToggle,
    Prog__PaletteGroup(Sort),
    Prog_CommandKind_SortByName, Prog_CommandKind_SortBySize, Prog_CommandKind_SortByDateCreated, Prog_CommandKind_SortByDateModified,
    Prog_CommandKind_SortByDateAccessed, Prog_CommandKind_SortByItemCount, Prog_CommandKind_SortByType,
    Prog__PaletteGroup(Inspector),
    Prog_CommandKind_InspectorToggleDetails, Prog_CommandKind_InspectorChangeLayout, Prog_CommandKind_InspectorIncreaseContentsSize,
    Prog_CommandKind_InspectorDecreaseContentsSize, Prog_CommandKind_InspectorToggleWordWrap, Prog_CommandKind_InspectorToggleLineNumbers,
    Prog_CommandKind_InspectorToggleWhitespace, Prog_CommandKind_InspectorStartFilter, Prog_CommandKind_InspectorToggleImageGrid,
    Prog_CommandKind_InspectorToggleImageMinimap, Prog_CommandKind_InspectorToggleImageScale, Prog_CommandKind_InspectorScaleImageUp,
    Prog_CommandKind_InspectorScaleImageDown,
    Prog__PaletteGroup(Panels),
    Prog_CommandKind_PanelSelectNext, Prog_CommandKind_PanelSelectPrev, Prog_CommandKind_PanelLeft, Prog_CommandKind_PanelRight, Prog_CommandKind_PanelUp,
    Prog_CommandKind_PanelDown,
    Prog__PaletteGroup(Tabs),
    Prog_CommandKind_TabDuplicate, Prog_CommandKind_TabShiftLeft, Prog_CommandKind_TabShiftRight, Prog_CommandKind_TabCloseOthers, Prog_CommandKind_TabCloseLeftTabs,
    Prog_CommandKind_TabCloseRightTabs, Prog_CommandKind_TabReopen, Prog_CommandKind_TabSelectNext, Prog_CommandKind_TabSelectPrev, Prog_CommandKind_TabSelect1,
    Prog_CommandKind_TabSelect2, Prog_CommandKind_TabSelect3, Prog_CommandKind_TabSelect4, Prog_CommandKind_TabSelect5, Prog_CommandKind_TabSelect6,
    Prog_CommandKind_TabSelect7, Prog_CommandKind_TabSelect8, Prog_CommandKind_TabSelectLast, Prog_CommandKind_TabList,
    Prog__PaletteGroup(Misc),
    Prog_CommandKind_ToggleQuickAccess, Prog_CommandKind_GoTo, Prog_CommandKind_CommandPalette, Prog_CommandKind_ContextMenu, Prog_CommandKind_OpenSettings, Prog_CommandKind_BookmarksClear, Prog_CommandKind_FolderViewsClear,
    Prog_CommandKind_RecentsClear, Prog_CommandKind_NewWindow, Prog_CommandKind_ToggleTransparency, Prog_CommandKind_ToggleDebugTheme,
    Prog__PaletteGroup(Scroll),
    Prog_CommandKind_SelectAll,
};
static_assert(Prog_CommandKind_Count < 256, "commands fit Prog_PaletteRow");

// The palette's rows: with nothing typed, each group's title and the commands of the groups open;
// otherwise the commands the text finds by the reference's score of their label and description, best first, then those only their search words find.
// `out` has room for Prog_MaxPaletteRows; returns how many it holds.
function u32 Prog_PaletteMatches(const Prog_State *state, Prog_PaletteRow *out)
{
    String text = Prog_EditText(&state->palette);
    i32 scores[Prog_MaxPaletteRows];
    u32 count = 0, group = 0;
    for (u32 i = 0; i < CArrayCount(Prog_PaletteTree); ++i) {
        u32 item = Prog_PaletteTree[i];
        if (item >= Prog_CommandKind_Count) {
            group = item - Prog_CommandKind_Count;
            if (!text.length) out[count++] = (Prog_PaletteRow){ (u8)group, Prog_CommandKind_None };
            continue;
        }
        if (!text.length) {
            if (state->paletteExpanded & (1u << group)) out[count++] = (Prog_PaletteRow){ (u8)group, (u8)item };
            continue;
        }
        const Prog_CommandDesc *desc = &Prog_Commands[item];
        i32 score = Prog_FuzzyScore(desc->label, desc->tooltip, text, NULL, NULL);
        if (score <= 0) {
            if (!Prog_FilterMatch(desc->aliases, text, NULL)) continue;
            score = INT32_MIN;
        }
        u32 at = count++;
        for (; at && scores[at - 1] < score; --at) {
            out[at] = out[at - 1];
            scores[at] = scores[at - 1];
        }
        out[at] = (Prog_PaletteRow){ (u8)group, (u8)item };
        scores[at] = score;
    }
    return count;
}

// The tabs the palette's text finds in its tab mode, best first: folder names holding it as typed, then paths that do, then names holding its letters in order.
// `out` has room for Prog_MaxTabs; returns how many it holds.
function u32 Prog_TabMatches(const Prog_State *state, u32 *out)
{
    String text = Prog_EditText(&state->palette);
    u32 count = 0;
    for (u32 tier = 0; tier < 3; ++tier) {
        for (u32 i = 0; i < state->tabCount && i < Prog_MaxTabs; ++i) {
            String path = Prog_TabPath(state, i), name = Prog__FolderName(path);
            b32 named = Prog__ContainsFolded(name, text);
            b32 within = !named && Prog__ContainsFolded(path, text);
            b32 letters = !named && !within && Prog_FilterMatch(name, text, NULL);
            if ((tier == 0 && named) || (tier == 1 && within) || (tier == 2 && letters)) out[count++] = i;
        }
    }
    return count;
}

// `typed` as an absolute path, taken from the shown folder unless it starts at a root; empty when it cannot be one.
function String Prog__GoToPath(Prog_State *state, String typed)
{
    if (!typed.length) return typed;
    u8 separator = (u8)P_NativePath(&state->frameArena, S("/")).value[0];
    b32 absolute = typed.value[0] == '/' || typed.value[0] == separator || (typed.length >= 2 && typed.value[1] == ':');
    String path = P_AbsolutePath(&state->frameArena, absolute ? typed : StrFormat(&state->frameArena, "%S/%S", state->directory.path, typed));
    // Without the separator typed last, except at a root such as "C:/" or "/".
    while (path.length > 1 && path.value[path.length - 1] == '/' && path.value[path.length - 2] != ':') --path.length;
    return path;
}

// What a Go To row scores: its name and its path as the row shows it, from the root it is in or its drive.
function i32 Prog__GoToScore(const Prog_State *state, String name, String path, String typed)
{
    b32 drive;
    u64 start = Prog__CrumbStart(state, path, &drive);
    String shown = StrFrom(path, drive ? 0 : start);
    while (shown.length > 2 && shown.value[shown.length - 1] == '/') --shown.length;
    return Prog_FuzzyScore(name, shown, typed, NULL, NULL);
}

// Go To's search ranks matches by score; ranks of Prog_GoToRanks and above tie, which only long texts of several words reach.
enum { Prog_GoToRanks = 8192, Prog_GoToPool = 2048 };

function u16 Prog__GoToRank(i32 score)
{
    return (u16)(Prog_GoToRanks - 1 - Min(Max(score, 0), (i32)Prog_GoToRanks - 1));
}

// Whether the index's `folder` is one of the folders close at hand, whose entries match by their path too.
function b32 Prog__GoToNearby(const Prog_State *state, const Prog_Index *index, u32 folder)
{
    if (folder >= index->names.header.count) return false;
    String name = index->names.v[folder];
    if (index->parents.v[folder] == UINT32_MAX) name = Prog__FolderName(name);
    if (!MapsGetPtrNull(state->gotoNearbyNames, name)) return false;
    char made[1024];
    return MapsGetPtrNull(state->gotoNearbyPaths, Prog__IndexPathTo(made, sizeof(made), index, folder)) != NULL;
}

typedef struct Prog__GoToFound {
    u32 entry;
    i32 score;
} Prog__GoToFound;

enum { Prog_GoToBlock = 2048 };

// A frame's look through the index: the lanes take blocks of its entries in turn until the time is up, so that a block costly to look
// through holds up no other lane, and the blocks taken are the first ones.
typedef struct Prog__GoToLook {
    const Prog_State *state;
    const Prog_Index *index;
    String text;
    u64 first, count, until;
    u32 floor;
    i32 pathBest;
    atomic_uint_fast64_t next;  // the next block to take
    Prog__GoToFound *found;     // per block, from its first entry's place on
    u32 *foundCount;            // per block
} Prog__GoToLook;

function void Prog__GoToBlock(Prog__GoToLook *look, u64 block)
{
    const Prog_Index *index = look->index;
    u64 from = look->first + block * Prog_GoToBlock, to = Min(from + Prog_GoToBlock, look->first + look->count);
    Prog__GoToFound *found = look->found + block * Prog_GoToBlock;
    u32 count = 0;
    // Entries of a folder follow one another, so that its answer is asked once.
    u32 lastParent = UINT32_MAX - 1;
    b32 inside = false;
    for (u64 at = from; at < to; ++at) {
        u32 entry = (u32)at, parent = index->parents.v[entry];
        String name = index->names.v[entry];
        // A root's name is its whole path.
        if (parent == UINT32_MAX) name = Prog__FolderName(name);
        if (parent != lastParent) {
            lastParent = parent;
            inside = Prog__GoToNearby(look->state, index, parent);
        }
        i32 nameScore = Prog_FuzzyScore(name, (String){0}, look->text, NULL, NULL);
        if (nameScore <= 0 && !inside) continue;
        // A name that cannot get past the floor with the best path there is does not have its path looked at.
        if (Prog__GoToRank(nameScore + look->pathBest) >= look->floor) continue;
        char made[1024];
        i32 score = Prog__GoToScore(look->state, name, Prog__IndexPathTo(made, sizeof(made), index, entry), look->text);
        if (score <= 0 || Prog__GoToRank(score) >= look->floor) continue;
        found[count++] = (Prog__GoToFound){ entry, score };
    }
    look->foundCount[block] = count;
}

function void Prog__GoToLane(void *user, u32 lane)
{
    (void)lane;
    Prog__GoToLook *look = user;
    u64 blocks = (look->count + Prog_GoToBlock - 1) / Prog_GoToBlock;
    while (PlatformNowMicroseconds() < look->until) {
        u64 block = atomic_fetch_add(&look->next, 1);
        if (block >= blocks) break;
        Prog__GoToBlock(look, block);
    }
}

// Adds a match to the pool. A full pool keeps its better half in its order, the ranks before the one that would overflow it and of that one
// the earliest, and refuses from then on what does not rank before that one, so that the whole index is searched with little kept.
function void Prog__GoToPool(Prog_State *state, Arena *scratch, Prog_GoToItem item, u16 rank)
{
    if (rank >= state->gotoFloor) return;
    if (state->gotoPooled == Prog_GoToPool) {
        u32 *bins = ArenaPushArray(scratch, Prog_GoToRanks, u32);
        for (u32 p = 0; p < Prog_GoToPool; ++p) ++bins[state->gotoRanks[p]];
        u32 floor = 0, kept = 0;
        while (kept + bins[floor] <= Prog_GoToPool / 2) kept += bins[floor++];
        u32 room = Prog_GoToPool / 2 - kept, to = 0;
        for (u32 p = 0; p < Prog_GoToPool; ++p) {
            u16 r = state->gotoRanks[p];
            if (r > floor || (r == floor && !room)) continue;
            if (r == floor) --room;
            state->gotoRanks[to] = r;
            state->gotoPool[to++] = state->gotoPool[p];
        }
        state->gotoPooled = to;
        state->gotoFloor = floor;
        if (rank >= floor) return;
    }
    state->gotoRanks[state->gotoPooled] = rank;
    state->gotoPool[state->gotoPooled++] = item;
}

// The rows Go To offers for the text typed, best first, in the frame arena: without a separator typed, the recent folders and the bookmarks
// holding it, those of folded sections left out; with one, what is inside the folder typed so far, `folder`, that starts with or holds what
// follows it. A folder not listed yet starts its listing and offers nothing until it arrives.
function Prog_GoToItem *Prog_GoToMatches(Prog_State *state, u32 *count, String *folder)
{
    Arena *frame = &state->frameArena;
    String text = Prog_EditText(&state->palette);
    u8 separator = (u8)P_NativePath(frame, S("/")).value[0];
    i64 cut = -1;
    for (u64 i = 0; i < text.length; ++i) if (text.value[i] == '/' || text.value[i] == separator) cut = (i64)i;
    *count = 0;
    *folder = (String){0};
    // Something typed searches every folder known, those of the drive index included, by name and then the few close at hand by path, as one list.
    if (cut < 0 && text.length) {
        // The walk's index once it is done, and before then the last run's, unless there is none.
        const Prog_Index *index = state->indexDone || !state->savedIndex.names.header.count ? &state->index : &state->savedIndex;
        u64 recents = state->recents.header.count, bookmarks = state->bookmarks.header.count, known = state->knownFolders.header.count;
        u64 nearby = recents + bookmarks + known;
        // Ranked by the reference's score of the name and the shown path; within a score the walk's order keeps shallower entries first.
        // The few close at hand and what is right inside them match by their path too, the rest of the index by their name only.
        Arena *arena = &state->gotoFoundArena;
        String last = StrMake((const char *)state->gotoFoundText, state->gotoFoundTextLength);
        if (!state->gotoPool || !StrIsEqual(last, text) || state->gotoNearby != nearby || state->gotoIndex != index) {
            ArenaClear(arena);
            state->gotoPool = ArenaPushArray(arena, Prog_GoToPool, Prog_GoToItem);
            state->gotoRanks = ArenaPushArray(arena, Prog_GoToPool, u16);
            state->gotoPooled = 0;
            state->gotoFloor = Prog_GoToRanks;
            state->gotoScanAt = 0;
            state->gotoScanFrame = UINT64_MAX;
            state->gotoNearby = nearby;
            state->gotoIndex = index;
            state->gotoSeen = (Prog_PathSet){0};
            state->gotoNearbyNames = (Prog_PathSet){0};
            state->gotoNearbyPaths = (Prog_PathSet){0};
            state->gotoFoundTextLength = (u32)Min(text.length, (u64)sizeof(state->gotoFoundText));
            MemCopy(state->gotoFoundText, text.value, state->gotoFoundTextLength);
            // The folders close at hand first, which the index's entries are then told apart by.
            for (u64 i = 0; i < nearby; ++i) {
                String path = i < recents ? state->recents.v[i] : i < recents + bookmarks ? state->bookmarks.v[i - recents] : state->knownFolders.v[i - recents - bookmarks];
                String name = Prog__FolderName(path);
                MapsPut(arena, state->gotoNearbyNames, name, true);
                MapsPut(arena, state->gotoNearbyPaths, path, true);
                i32 score = Prog__GoToScore(state, name, path, text);
                if (score <= 0 || MapsGetPtrNull(state->gotoSeen, path)) continue;
                MapsPut(arena, state->gotoSeen, path, true);
                Prog__GoToPool(state, frame, (Prog_GoToItem){ path, Prog_GoToSection_Found, false, (b8)(name.length && name.value[0] == '.'), {0}, NULL }, Prog__GoToRank(score));
            }
        }
        // Once a frame, so that every look in a frame sees the same rows; the walk's new entries are reached in turn.
        // Each frame looks for about 4 ms.
        u64 entries = index->names.header.count;
        if (state->gotoScanFrame != state->ui.frameIndex && state->gotoScanAt < entries) ProfileScope("GoToSearch") {
            state->gotoScanFrame = state->ui.frameIndex;
            Prog__GoToLook look = {
                .state = state, .index = index, .text = text, .first = state->gotoScanAt, .count = Min(entries - state->gotoScanAt, (u64)1 << 20),
                .until = PlatformNowMicroseconds() + 4000, .floor = state->gotoFloor, .pathBest = Prog_FuzzyPathBest(text),
            };
            u64 blocks = (look.count + Prog_GoToBlock - 1) / Prog_GoToBlock;
            look.found = ArenaPushArrayNoZero(frame, look.count, Prog__GoToFound);
            look.foundCount = ArenaPushArrayNoZero(frame, blocks, u32);
            P_RunLanes(Max(1u, Min(P_ProcessorCount(), (u32)Prog_WalkLanes)), Prog__GoToLane, &look);
            u64 taken = Min((u64)atomic_load(&look.next), blocks);
            state->gotoScanAt = Min(look.first + taken * Prog_GoToBlock, entries);
            // In the blocks' order, which is the walk's.
            for (u64 block = 0; block < taken; ++block) {
                for (u32 i = 0; i < look.foundCount[block]; ++i) {
                    Prog__GoToFound found = look.found[block * Prog_GoToBlock + i];
                    u16 rank = Prog__GoToRank(found.score);
                    if (rank >= state->gotoFloor) continue;
                    char made[1024];
                    String path = Prog__IndexPathTo(made, sizeof(made), index, found.entry);
                    // The index finds the folders close at hand again.
                    if (MapsGetPtrNull(state->gotoSeen, path)) continue;
                    u8 flags = (u8)index->flags.v[found.entry];
                    String name = index->names.v[found.entry];
                    b32 hidden = (flags & Prog_IndexFlag_Hidden) || (name.length && name.value[0] == '.');
                    Prog__GoToPool(state, frame, (Prog_GoToItem){ StrCopy(arena, path), Prog_GoToSection_Found, (b8)((flags & Prog_IndexFlag_File) != 0), (b8)hidden, {0}, NULL }, rank);
                }
            }
        }
        state->gotoSearching = state->gotoScanAt < entries;
        // A counting sort, which keeps the order within a rank.
        u32 pooled = state->gotoPooled;
        u32 *starts = ArenaPushArray(frame, Prog_GoToRanks + 1, u32);
        for (u32 i = 0; i < pooled; ++i) ++starts[state->gotoRanks[i] + 1];
        for (u32 r = 0; r < Prog_GoToRanks; ++r) starts[r + 1] += starts[r];
        u32 *order = ArenaPushArray(frame, Max(pooled, 1u), u32);
        for (u32 i = 0; i < pooled; ++i) order[starts[state->gotoRanks[i]]++] = i;
        *count = Min(pooled, (u32)Prog_MaxGoToFound);
        Prog_GoToItem *result = ArenaPushArray(frame, Max(*count, 1u), Prog_GoToItem);
        for (u32 i = 0; i < *count; ++i) result[i] = state->gotoPool[order[i]];
        return result;
    }
    if (cut < 0) {
        // The reference keeps the ten most recent.
        u64 recents = Min(state->recents.header.count, (u64)10);
        Prog_GoToItem *result = ArenaPushArray(frame, recents + state->bookmarks.header.count + state->drives.header.count + state->places.header.count + 1, Prog_GoToItem);
        for (u32 section = 0; section < Prog_GoToSection_Folder; ++section) {
            if (state->gotoFolded[section]) continue;
            if (section == Prog_GoToSection_Storage || section == Prog_GoToSection_Places) {
                const P_ArrayPlace *places = section == Prog_GoToSection_Storage ? &state->drives : &state->places;
                for (u64 i = 0; i < places->header.count; ++i) {
                    const P_Place *place = &places->v[i];
                    result[(*count)++] = (Prog_GoToItem){ place->path, (u8)section, false, false, place->name, section == Prog_GoToSection_Storage ? place : NULL };
                }
                continue;
            }
            const ArrayStr *list = section == Prog_GoToSection_Recents ? &state->recents : &state->bookmarks;
            u64 limit = section == Prog_GoToSection_Recents ? recents : list->header.count;
            for (u32 tier = 0; tier < 2; ++tier) {
                for (u64 i = 0; i < limit; ++i) {
                    String path = list->v[i];
                    b32 named = Prog__ContainsFolded(Prog__FolderName(path), text);
                    if ((tier == 0 && named) || (tier == 1 && !named && Prog__ContainsFolded(path, text))) {
                        String name = Prog__FolderName(path);
                        result[(*count)++] = (Prog_GoToItem){ path, (u8)section, false, (b8)(name.length && name.value[0] == '.'), {0}, NULL };
                    }
                }
            }
        }
        return result;
    }
    String leaf = StrFrom(text, (u64)cut + 1);
    String path = Prog__GoToPath(state, StrFromTo(text, 0, cut + 1));
    if (!path.length) return NULL;
    *folder = path;
    String listed = StrMake((const char *)state->gotoFolder, state->gotoFolderLength);
    if (!StrIsEqual(path, listed) && path.length <= sizeof(state->gotoFolder)) {
        if (state->gotoJob) P_RequestCancel(&state->gotoJob->cancelToken);
        MemCopy(state->gotoFolder, path.value, path.length);
        state->gotoFolderLength = (u32)path.length;
        state->gotoListed = false;
        P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
        input->kind = Prog_JobKind_GoTo;
        input->path = StrCopy(&job->arena, path);
        input->showHidden = state->showHidden;
        job->input = input;
        state->gotoJob = job;
        P_JobSubmit(job);
    }
    if (!state->gotoListed) return NULL;
    Prog_GoToItem *result = ArenaPushArray(frame, Max(state->gotoListing.count, (u64)1), Prog_GoToItem);
    // Names that start with what is typed after the folder come before those that only hold it.
    for (u32 tier = 0; tier < 2; ++tier) {
        for (u64 i = 0; i < state->gotoListing.count; ++i) {
            const P_File *file = &state->gotoListing.files[i];
            b32 starts = file->name.length >= leaf.length && Prog__ContainsFolded(StrFromTo(file->name, 0, (i64)leaf.length), leaf);
            if ((tier == 0 && starts) || (tier == 1 && !starts && Prog__ContainsFolded(file->name, leaf))) {
                b32 hidden = file->hidden || (file->name.length && file->name.value[0] == '.');
                result[(*count)++] = (Prog_GoToItem){ Prog_JoinPath(frame, path, file->name), Prog_GoToSection_Folder, file->kind != P_FileKind_Directory, (b8)hidden, {0}, NULL };
            }
        }
    }
    return result;
}

// Puts `text` in Go To's box, remembered for its back button when `remember` is set.
function void Prog__GoToSetText(Prog_State *state, String text, b32 remember)
{
    if (remember) {
        // Going on from an earlier text forgets the ones after it.
        if (!state->gotoHistory.header.count) ArrayPush(&state->gotoHistoryArena, state->gotoHistory, StrCopy(&state->gotoHistoryArena, Prog_EditText(&state->palette)));
        state->gotoHistory.header.count = state->gotoHistoryAt + 1;
        ArrayPush(&state->gotoHistoryArena, state->gotoHistory, StrCopy(&state->gotoHistoryArena, text));
        state->gotoHistoryAt = (u32)state->gotoHistory.header.count - 1;
    }
    state->palette.cursor = 0;
    state->palette.anchor = state->palette.length;
    Prog__EditReplace(&state->palette, (const u8 *)text.value, (u32)Min(text.length, (u64)sizeof(state->palette.text) - 1));
    state->paletteHighlight = 0;
    state->gotoScroll = 0;
}

// Go To's box with `path` in it to go on into, in the system's separators.
function void Prog__GoToExpand(Prog_State *state, String path)
{
    Prog__GoToSetText(state, P_NativePath(&state->frameArena, Prog_JoinPath(&state->frameArena, path, S(""))), true);
}

// Opens Go To's row: a folder in the panel, a file in its app.
function void Prog__GoToOpen(Prog_State *state, Prog_GoToItem item)
{
    if (!item.file) {
        if (item.path.length && !StrIsEqual(item.path, state->directory.path)) Prog_RequestDirectory(state, item.path, true, 0);
        return;
    }
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Open;
    input->generation = state->generation;
    input->path = StrCopy(&job->arena, item.path);
    job->input = input;
    P_JobSubmit(job);
}

// Filey's menus beside the views and tabs: the view, the folder's history, the tabs and everything else.
function void Prog__OpenProgramMenu(Prog_State *state, Prog_Command *command)
{
    Scratch scratch = GetScratch();
    u32 capacity = Max(Prog_MaxTabs, (u32)state->history.header.count) + 32, count = 0;
    Prog_MenuCommand *items = ArenaPushArray(scratch.arena, capacity, Prog_MenuCommand);
#define Prog__Item(...) (items[count++] = (Prog_MenuCommand){ __VA_ARGS__ })
    switch (command->kind) {
        case Prog_CommandKind_ViewMenu: {
            u32 zoom = state->zoom;
            Prog__Item(.kind = Prog_CommandKind_ViewDetails, .checked = zoom == 0);
            Prog__Item(.kind = Prog_CommandKind_ViewList, .checked = zoom == 1);
            Prog__Item(.kind = Prog_CommandKind_ViewColumns, .checked = zoom == 2);
            Prog__Item(.kind = Prog_CommandKind_ViewMediumIcons, .checked = zoom >= Prog_ZoomFirstIcon && zoom < Prog_ZoomFirstIcon + 7);
            Prog__Item(.kind = Prog_CommandKind_ViewLargeIcons, .checked = zoom >= Prog_ZoomFirstIcon + 7 && zoom + 1 < Prog_ZoomLevels);
            Prog__Item(.kind = Prog_CommandKind_ViewXLIcons, .checked = zoom + 1 == Prog_ZoomLevels);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_ZoomIn);
            Prog__Item(.kind = Prog_CommandKind_ZoomOut);
        } break;
        case Prog_CommandKind_HistoryMenu: {
            // Newest first, as browsers list it.
            for (u64 i = state->history.header.count; i-- > 0;) Prog__Item(.kind = Prog_CommandKind_HistoryGo, .row = i, .label = state->history.v[i], .checked = i == state->historyAt);
        } break;
        case Prog_CommandKind_MainMenu: {
            Prog__Item(.kind = Prog_CommandKind_CommandPalette, .icon = Icon_Terminal);
            Prog__Item(.kind = Prog_CommandKind_NewWindow, .icon = Icon_AppWindow);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_OpenOptions, .icon = Icon_Settings);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_ToggleTransparency, .icon = Icon_Blend, .toggle = true, .checked = state->transparent);
            Prog__Item(.kind = Prog_CommandKind_ToggleDebugTheme, .icon = Icon_Palette, .toggle = true, .checked = state->debugTheme);
        } break;
        // In the reference's order: the panel's views, its splits, its tabs, the app, then what the listing shows.
        case Prog_CommandKind_PanelMenu: {
            Prog__Item(.kind = Prog_CommandKind_ToggleExpandFolder, .icon = Icon_FolderTree, .toggle = true, .checked = state->directory.expanded);
            Prog__Item(.kind = Prog_CommandKind_TogglePreview, .icon = Icon_ScanEye, .toggle = true, .checked = state->previewShown);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_SplitRight, .icon = Icon_UnfoldHorizontal);
            Prog__Item(.kind = Prog_CommandKind_SplitBottom, .icon = Icon_UnfoldVertical);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_TabNew, .icon = Icon_SquarePlus);
            Prog__Item(.kind = Prog_CommandKind_TabClose, .icon = Icon_SquareX);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_OpenOptions, .icon = Icon_Settings);
            Prog__Item(.kind = Prog_CommandKind_CopyAsPath, .icon = Icon_Copy);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_ToggleHidden, .icon = Icon_Eye, .toggle = true, .checked = state->showHidden);
            Prog__Item(.kind = Prog_CommandKind_ToggleQuickAccess, .icon = Icon_PanelLeft, .toggle = true, .checked = !state->sidebarHidden);
            Prog__Item(.kind = Prog_CommandKind_BookmarksToggle, .icon = Icon_Bookmark, .toggle = true, .checked = Prog_IsBookmarked(state, state->directory.path));
        } break;
        default: break;
    }
#undef Prog__Item
    Prog_OpenCommandMenu(state, items, count, command->at);
    if (state->menuOpen) {
        state->menuOpener = command->kind;
        state->menuRight = command->atRight;
    }
    ReleaseScratch(scratch);
}

function void Prog__OpenMenu(Prog_State *state, Prog_Command *command)
{
    Prog_CloseMenu(state);
    if (state->loading) return;
    b32 mouse = command->kind == Prog_CommandKind_ContextMenuAt;
    b32 background = mouse && command->row >= state->directory.count;
    if (mouse && !background && !state->directory.entries[command->row].selected) Prog_SelectRow(state, command->row, 0);
    if (background) {
        for (u64 i = 0; i < state->directory.count; ++i) state->directory.entries[i].selected = false;
        state->selectedCount = 0;
    }
    // The keyboard acts on the selection, else on the focused row, else on the folder.
    u64 count = background ? 0 : state->selectedCount ? state->selectedCount : state->hasFocus ? 1 : 0;
    state->menuRows = ArenaPushArray(&state->menuArena, Max(count, (u64)1), u64);
    String *names = ArenaPushArray(&state->menuArena, Max(count, (u64)1), String);
    if (count && !state->selectedCount) {
        state->menuRows[state->menuRowCount++] = state->focusedRow;
    } else if (count) {
        for (u64 i = 0; i < state->directory.count; ++i) {
            if (state->directory.entries[i].selected) state->menuRows[state->menuRowCount++] = i;
        }
    }
    String folder = Prog__RowsFolder(state, &state->menuArena, state->menuRows, &state->menuRowCount, names);
    ProfileScope("OpenContextMenu") {
        state->menuOpen = P_ContextMenuOpen(&state->menuArena, folder, names, state->menuRowCount, &state->menu);
    }
    if (!state->menuOpen) {
        Prog_CloseMenu(state);
        return;
    }
    state->menuIcons = ArenaPushArray(&state->menuArena, Max(state->menu.count, 1u), const Prog_Icon *);
    for (u32 i = 0; i < state->menu.count; ++i) state->menuIcons[i] = Prog_ImageIcon(state, state->menu.items[i].icon);
    state->menuAt = mouse ? state->ui.mouse
                  : state->menuRowCount ? (UI_Vec2){ state->focusedRowRect.x0 + 24, state->focusedRowRect.y1 }
                  : state->ui.mouse;
    state->menuHighlight = UINT32_MAX;
    if (!mouse) Prog__MenuMove(state, 1);
}

function String Prog_EditText(const Prog_TextEdit *edit)
{
    return StrMake((const char *)edit->text, edit->length);
}

// The next codepoint boundary before (`step` < 0) or after `at`.
function u32 Prog__EditStep(const Prog_TextEdit *edit, u32 at, i32 step)
{
    if (step < 0) {
        if (at) --at;
        while (at && (edit->text[at] & 0xC0) == 0x80) --at;
    } else if (at < edit->length) {
        ++at;
        while (at < edit->length && (edit->text[at] & 0xC0) == 0x80) ++at;
    }
    return at;
}

// Replaces the selection with `bytes`.
function void Prog__EditReplace(Prog_TextEdit *edit, const u8 *bytes, u32 count)
{
    u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
    u32 length = edit->length - (to - from) + count;
    if (length >= sizeof(edit->text)) return;
    MemMove(edit->text + from + count, edit->text + to, edit->length - to);
    MemCopy(edit->text + from, bytes, count);
    edit->length = length;
    edit->cursor = edit->anchor = from + count;
    if (from != to || count) ++edit->version;
}

function void Prog__EditInsert(Prog_TextEdit *edit, rune c)
{
    u8 bytes[4];
    u32 count = 0;
    if (c < 0x80) {
        bytes[count++] = (u8)c;
    } else if (c < 0x800) {
        bytes[count++] = (u8)(0xC0 | (c >> 6));
        bytes[count++] = (u8)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        bytes[count++] = (u8)(0xE0 | (c >> 12));
        bytes[count++] = (u8)(0x80 | ((c >> 6) & 0x3F));
        bytes[count++] = (u8)(0x80 | (c & 0x3F));
    } else if (c < 0x110000) {
        bytes[count++] = (u8)(0xF0 | (c >> 18));
        bytes[count++] = (u8)(0x80 | ((c >> 12) & 0x3F));
        bytes[count++] = (u8)(0x80 | ((c >> 6) & 0x3F));
        bytes[count++] = (u8)(0x80 | (c & 0x3F));
    }
    Prog__EditReplace(edit, bytes, count);
}

function b32 Prog__IsWordByte(u8 c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
}

// Places the caret at byte `at` from a click.
// Shift or a drag extends the selection, and a double click selects the word.
function void Prog__EditClick(Prog_TextEdit *edit, u32 at, KeyMods mods, u32 pressCount)
{
    at = Min(at, edit->length);
    while (at && at < edit->length && (edit->text[at] & 0xC0) == 0x80) --at;
    if (pressCount == 2 && !(mods & KeyMod_Shift)) {
        u32 from = at, to = at;
        while (from && Prog__IsWordByte(edit->text[from - 1])) --from;
        while (to < edit->length && Prog__IsWordByte(edit->text[to])) ++to;
        edit->anchor = from;
        edit->cursor = to;
        return;
    }
    edit->cursor = at;
    if (!(mods & KeyMod_Shift)) edit->anchor = at;
}

// Pastes the clipboard over the selection.
// Names are one line, so line breaks and other control characters are dropped, and text that does not fit is cut at a codepoint.
function void Prog__EditPaste(Prog_TextEdit *edit)
{
    Scratch scratch = GetScratch();
    String text = PlatformClipboardGetText(scratch.arena);
    u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
    u32 room = (u32)sizeof(edit->text) - 1 - (edit->length - (to - from));
    u8 *bytes = ArenaPushArrayNoZero(scratch.arena, Max(text.length, (u64)1), u8);
    u32 count = 0;
    u64 at = 0;
    for (; at < text.length && count < room; ++at) {
        u8 c = (u8)text.value[at];
        if (c >= 0x20 && c != 0x7F) bytes[count++] = c;
    }
    if (at < text.length && count) {
        // Drops a last sequence that lost its tail.
        u32 lead = count - 1;
        while (lead && (bytes[lead] & 0xC0) == 0x80) --lead;
        u8 c = bytes[lead];
        u32 size = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        if (lead + size > count) count = lead;
    }
    Prog__EditReplace(edit, bytes, count);
    ReleaseScratch(scratch);
}

// Caret movement, deletion, Ctrl+A and the clipboard.
// Returns false for keys the field does not use.
// The byte where the next word starts toward `step` from `at`: past any separators, then past the word.
function u32 Prog__EditWordStep(const Prog_TextEdit *edit, u32 at, i32 step)
{
    if (step < 0) {
        while (at && !Prog__IsWordByte(edit->text[at - 1])) --at;
        while (at && Prog__IsWordByte(edit->text[at - 1])) --at;
    } else {
        while (at < edit->length && !Prog__IsWordByte(edit->text[at])) ++at;
        while (at < edit->length && Prog__IsWordByte(edit->text[at])) ++at;
    }
    return at;
}

// Ctrl moves and deletes by word, as the reference's text fields do.
function b32 Prog__EditKey(Prog_TextEdit *edit, const PlatformEvent *event)
{
    b32 shift = (event->mods & KeyMod_Shift) != 0, word = (event->mods & KeyMod_Control) != 0;
    u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
    switch (event->key) {
        case Key_Left:
        case Key_Right: {
            i32 step = event->key == Key_Left ? -1 : 1;
            if (!shift && !word && from != to) edit->cursor = step < 0 ? from : to;
            else edit->cursor = word ? Prog__EditWordStep(edit, edit->cursor, step) : Prog__EditStep(edit, edit->cursor, step);
            if (!shift) edit->anchor = edit->cursor;
        } return true;
        case Key_Home:
        case Key_End: {
            edit->cursor = event->key == Key_Home ? 0 : edit->length;
            if (!shift) edit->anchor = edit->cursor;
        } return true;
        case Key_Backspace:
        case Key_Delete: {
            i32 step = event->key == Key_Backspace ? -1 : 1;
            if (from == to) edit->anchor = word ? Prog__EditWordStep(edit, edit->cursor, step) : Prog__EditStep(edit, edit->cursor, step);
            Prog__EditReplace(edit, NULL, 0);
        } return true;
        case Key_A: {
            if (!(event->mods & KeyMod_Control)) return false;
            edit->anchor = 0;
            edit->cursor = edit->length;
        } return true;
        case Key_C:
        case Key_X: {
            if (!(event->mods & KeyMod_Control)) return false;
            if (from != to) {
                PlatformClipboardSetText(StrMake((const char *)edit->text + from, to - from));
                if (event->key == Key_X) Prog__EditReplace(edit, NULL, 0);
            }
        } return true;
        case Key_V: {
            if (!(event->mods & KeyMod_Control)) return false;
            Prog__EditPaste(edit);
        } return true;
        default: return false;
    }
}

function void Prog_BeginRename(Prog_State *state, u64 row)
{
    if (state->loading || row >= state->directory.count) return;
    const Prog_Entry *entry = &state->directory.entries[row];
    String name = Prog_EntryLeaf(entry->file.name);
    if (entry->file.name.length >= sizeof(state->renameFrom)) return;
    Prog_CloseMenu(state);
    state->filtering = false;
    Prog_TextEdit *edit = &state->rename;
    MemCopy(edit->text, name.value, name.length);
    MemCopy(state->renameFrom, entry->file.name.value, entry->file.name.length);
    state->renameFromLength = (u32)entry->file.name.length;
    edit->length = (u32)name.length;
    // Files start with the part before the extension selected, as in Explorer.
    u32 end = edit->length;
    if (entry->file.kind != P_FileKind_Directory) {
        for (u32 i = edit->length; i > 1; --i) {
            if (edit->text[i - 1] == '.') { end = i - 1; break; }
        }
    }
    edit->anchor = 0;
    edit->cursor = end;
    state->renaming = true;
    state->focusedRow = row;
    state->hasFocus = true;
    state->revealFocus = true;
}

function void Prog__EndRename(Prog_State *state, b32 commit)
{
    if (!state->renaming) return;
    state->renaming = false;
    String listed = StrMake((const char *)state->renameFrom, state->renameFromLength);
    String from = Prog_EntryLeaf(listed), prefix = Prog_EntryParent(listed);
    String to = Prog_EditText(&state->rename);
    if (!commit || !to.length || StrIsEqual(from, to) || state->loading) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Rename;
    input->path = prefix.length ? Prog_JoinPath(&job->arena, state->directory.path, prefix) : StrCopy(&job->arena, state->directory.path);
    input->prefix = StrCopy(&job->arena, prefix);
    input->name = StrCopy(&job->arena, from);
    input->newName = StrCopy(&job->arena, to);
    job->input = input;
    P_JobSubmit(job);
}

function void Prog__FileCommand(Prog_State *state, Prog_CommandKind kind);

function void Prog__InvokeMenu(Prog_State *state, u32 index)
{
    if (!state->menuOpen || index >= state->menu.count) return;
    P_MenuItem item = state->menu.items[index];
    if (item.separator || item.disabled || item.childCount) return;
    if (state->menuCommands) {
        Prog_MenuCommand chosen = state->menuCommands[index];
        Prog_CloseMenu(state);
        Prog_QueueCommand(state, chosen.kind, chosen.row, 0, UINT64_MAX);
        return;
    }
    // A sidebar folder's menu: Open shows it here, and of Filey's own items, which act on the list's selection, only Copy As Path applies.
    if (state->menuTarget.length && (item.action || StrIsEqual(item.verb, S("open")))) {
        String target = StrCopy(&state->frameArena, state->menuTarget);
        Prog_CloseMenu(state);
        if (item.action == P_MenuAction_CopyAsPath) PlatformClipboardSetText(P_NativePath(&state->frameArena, target));
        else if (!item.action) Prog_RequestDirectory(state, target, true, 0);
        return;
    }
    // "Open" stays inside Filey: folders open here and files go through the same job as double-click.
    // The shell's Rename needs an Explorer view to edit in, so Filey edits the name itself.
    if (StrIsEqual(item.verb, S("rename")) && state->menuRowCount == 1) {
        u64 row = state->menuRows[0];
        Prog_CloseMenu(state);
        Prog_BeginRename(state, row);
        return;
    }
    // Items Filey implements itself, which are all of them on Linux.
    if (item.action) {
        static_assert(P_MenuAction_Count == 10);
        local_persist const Prog_CommandKind actions[P_MenuAction_Count] = {
            [P_MenuAction_Cut] = Prog_CommandKind_CutSelection,
            [P_MenuAction_Copy] = Prog_CommandKind_CopySelection,
            [P_MenuAction_Paste] = Prog_CommandKind_Paste,
            [P_MenuAction_Duplicate] = Prog_CommandKind_Duplicate,
            [P_MenuAction_CopyAsPath] = Prog_CommandKind_CopyAsPath,
            [P_MenuAction_Trash] = Prog_CommandKind_Trash,
            [P_MenuAction_Delete] = Prog_CommandKind_SelectionDelete,
            [P_MenuAction_NewFolder] = Prog_CommandKind_CreateFolder,
            [P_MenuAction_NewFile] = Prog_CommandKind_CreateFile,
        };
        Prog_CloseMenu(state);
        if (item.action < P_MenuAction_Count) Prog__FileCommand(state, actions[item.action]);
        return;
    }
    if (StrIsEqual(item.verb, S("open")) && state->menuRowCount) {
        if (state->menuRowCount == 1) {
            Prog_OpenRow(state, state->menuRows[0]);
        } else {
            for (u64 i = 0; i < state->menuRowCount; ++i) {
                if (state->directory.entries[state->menuRows[i]].file.kind != P_FileKind_Directory) Prog_OpenRow(state, state->menuRows[i]);
            }
        }
    } else {
        // Submenu commands such as New > Folder create one item that Explorer would put into rename.
        b32 creates = index >= state->menu.rootCount && !StrIsEqual(item.verb, S("paste")) && !StrIsEqual(item.verb, S("openwith"));
        state->renameCreatedUntil = creates ? PlatformNowMicroseconds() + Millions(5) : 0;
        P_FileError error = P_ContextMenuInvoke(&state->frameArena, &state->menu, item.id);
        ArenaClear(&state->errorArena);
        state->error = error.code ? StrFormat(&state->errorArena, "%S: %S", item.label, error.message) : (String){0};
        // The watch would catch the change too; reloading now shows it without waiting.
        Prog_Reload(state);
    }
    Prog_CloseMenu(state);
}

// Full paths of the selected rows, or of the focused row when nothing is selected.
function u64 Prog__SelectedPaths(Prog_State *state, Arena *arena, String **paths)
{
    u64 count = state->selectedCount ? state->selectedCount : state->hasFocus ? 1 : 0;
    *paths = ArenaPushArray(arena, Max(count, (u64)1), String);
    u64 at = 0;
    for (u64 i = 0; i < state->directory.count && at < count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        if (state->selectedCount ? entry->selected : i == state->focusedRow) (*paths)[at++] = Prog_JoinPath(arena, state->directory.path, entry->file.name);
    }
    return at;
}

// Starts loading the preview of the active panel's focused item when it is another than the one shown; a folder is listed in the inspector's panel.
// While the keyboard is in that panel, the item is still its owner's; once the inspector no longer lists a folder, the keyboard goes back to the owner.
function void Prog__UpdatePreview(Prog_State *state)
{
    u64 inner = state->panels[state->currentPanel].inspectorOf ? state->panels[state->currentPanel].id : 0;
    if (inner) {
        Prog__UsePanel(state, Prog_PanelIndex(state, state->panels[state->currentPanel].inspectorOf));
        Prog__UpdatePreview(state);
        u32 at = Prog_PanelIndex(state, inner);
        if (at != UINT32_MAX && state->previewFolder) Prog__UsePanel(state, at);
        else if (at != UINT32_MAX) Prog__ActivatePanel(state, state->currentPanel);
        return;
    }
    if (!state->previewShown) {
        Prog_DropInspectorPanel(state, state->panels[state->currentPanel].id);
        return;
    }
    String want = {0};
    b32 folder = false;
    if (!state->loading && state->hasFocus && state->focusedRow < state->directory.count) {
        const Prog_Entry *entry = &state->directory.entries[state->focusedRow];
        want = Prog_JoinPath(&state->frameArena, state->directory.path, entry->file.name);
        folder = entry->file.kind == P_FileKind_Directory;
    }
    if (StrIsEqual(want, state->previewPath)) return;
    if (state->previewJob) P_RequestCancel(&state->previewJob->cancelToken);
    state->previewJob = NULL;
    ArenaClear(&state->previewArena);
    state->previewPath = StrCopy(&state->previewArena, want);
    state->previewText = (String){0};
    state->previewPicture = state->previewImage = false;
    state->previewFolder = folder;
    state->imagePan = (UI_Vec2){0};
    state->previewLines = state->previewRows = state->previewKept = NULL;
    state->previewLineCount = state->previewRowColumns = 0;
    state->inspectorResetScroll = true;
    if (!want.length) return;
    if (folder) Prog_ListInInspector(state, want);
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Preview;
    input->path = StrCopy(&job->arena, want);
    input->folder = folder;
    input->showHidden = state->showHidden;
    job->input = input;
    state->previewJob = job;
    P_JobSubmit(job);
}

// Zooms the inspector's image a fifth of its size in or out from the scale it shows, down to no smaller than fitting the view.
function void Prog__ScaleImage(Prog_State *state, b32 up)
{
    f32 fit = state->imageFit > 0 ? state->imageFit : 1, least = MinF32(fit, 1);
    f32 shown = state->imageScaleMode == Prog_ImageScale_Custom ? state->imageScale : state->imageScaleMode == Prog_ImageScale_Original ? 1 : least;
    f32 scale = roundf((shown + (up ? 0.2f : -0.2f)) * 100.0f) / 100.0f;
    state->imageScale = MinF32(MaxF32(scale, least), 32.0f);
    state->imageScaleMode = Prog_ImageScale_Custom;
}

function void Prog__FileJob(Prog_State *state, Prog_JobKind kind, const String *paths, u64 count, String directory, b32 flag, String name)
{
    if (!directory.length || (kind != Prog_JobKind_Create && !count)) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = (u8)kind;
    input->path = StrCopy(&job->arena, directory);
    input->paths = ArenaPushArray(&job->arena, Max(count, (u64)1), String);
    for (u64 i = 0; i < count; ++i) input->paths[i] = StrCopy(&job->arena, paths[i]);
    input->pathCount = count;
    input->move = kind == Prog_JobKind_Transfer && flag;
    input->folder = kind == Prog_JobKind_Create && flag;
    input->name = StrCopy(&job->arena, name);
    // A copy or move takes a free slot for its progress and questions; without one, taken names get numbers.
    for (u32 i = 0; kind == Prog_JobKind_Transfer && i < Prog_MaxTransfers; ++i) {
        Prog_Transfer *transfer = &state->transfers[i];
        if (transfer->job) continue;
        String target = Prog_EntryLeaf(directory);
        *transfer = (Prog_Transfer){ .job = job, .move = input->move, .started = PlatformNowMicroseconds(), .targetLength = (u32)Min(target.length, sizeof(transfer->target)) };
        MemCopy(transfer->target, target.value, transfer->targetLength);
        atomic_store(&transfer->progress.policy, P_Conflict_Ask);
        input->progress = &transfer->progress;
        break;
    }
    job->input = input;
    P_JobSubmit(job);
}

// Filey's own file commands; Windows context menus run the shell's versions instead.
function void Prog__FileCommand(Prog_State *state, Prog_CommandKind kind)
{
    if (state->loading || state->renaming) return;
    Arena *frame = &state->frameArena;
    String *paths = NULL;
    u64 count = Prog__SelectedPaths(state, frame, &paths);
    switch (kind) {
        case Prog_CommandKind_CopySelection:
        case Prog_CommandKind_CutSelection: {
            if (count) PlatformClipboardSetFiles(paths, count, kind == Prog_CommandKind_CutSelection);
        } break;
        case Prog_CommandKind_Paste: {
            ArrayStr files = {0};
            b32 cut = false;
            if (PlatformClipboardGetFiles(frame, &files, &cut)) Prog__FileJob(state, Prog_JobKind_Transfer, files.v, files.header.count, state->directory.path, cut, (String){0});
        } break;
        case Prog_CommandKind_Trash: Prog__FileJob(state, Prog_JobKind_Trash, paths, count, state->directory.path, false, (String){0}); break;
        case Prog_CommandKind_SelectionDelete: {
            // Nothing is deleted until the confirmation is accepted.
            if (!count) break;
            ArenaClear(&state->confirmArena);
            state->confirmPaths = ArenaPushArray(&state->confirmArena, count, String);
            for (u64 i = 0; i < count; ++i) state->confirmPaths[i] = StrCopy(&state->confirmArena, paths[i]);
            state->confirmCount = count;
            state->confirmingDelete = true;
            state->filtering = false;
        } break;
        case Prog_CommandKind_Duplicate: {
            // Each copy goes next to its original, so an expanded listing duplicates the items of one folder at a time.
            u64 *rows = ArenaPushArray(frame, Max(count, (u64)1), u64);
            u64 rowCount = 0;
            for (u64 i = 0; i < state->directory.count && rowCount < count; ++i) {
                if (state->selectedCount ? state->directory.entries[i].selected : i == state->focusedRow) rows[rowCount++] = i;
            }
            String *names = ArenaPushArray(frame, Max(count, (u64)1), String);
            String folder = Prog__RowsFolder(state, frame, rows, &rowCount, names);
            for (u64 i = 0; i < rowCount; ++i) paths[i] = Prog_JoinPath(frame, folder, names[i]);
            Prog__FileJob(state, Prog_JobKind_Transfer, paths, rowCount, folder, false, (String){0});
        } break;
        case Prog_CommandKind_CopyAsPath: {
            StrBuilder text = {0};
            for (u64 i = 0; i < count; ++i) {
                if (i) StrBuildChar(frame, &text, '\n');
                StrBuildStr(frame, &text, P_NativePath(frame, paths[i]));
            }
            if (count) PlatformClipboardSetText(text.buffer);
        } break;
        case Prog_CommandKind_CreateFolder:
        case Prog_CommandKind_CreateFile: {
            b32 folder = kind == Prog_CommandKind_CreateFolder;
            Prog__FileJob(state, Prog_JobKind_Create, NULL, 0, state->directory.path, folder, folder ? S("New folder") : S("New file"));
        } break;
        default: InvalidPath;
    }
}

// The folder of sidebar item `index` in `group`; empty when there is no such item.
function String Prog__SidebarPath(const Prog_State *state, Prog_Group group, u64 index)
{
    switch (group) {
        case Prog_Group_Recents: return index < state->recents.header.count ? state->recents.v[index] : (String){0};
        case Prog_Group_Bookmarks: return index < state->bookmarks.header.count ? state->bookmarks.v[index] : (String){0};
        case Prog_Group_Storage: return index < state->drives.header.count ? state->drives.v[index].path : (String){0};
        case Prog_Group_Places: return index < state->places.header.count ? state->places.v[index].path : (String){0};
        default: return (String){0};
    }
}

// Opens `row`, or the selection or focused row when it is UINT64_MAX, in new tabs behind the shown one: folders themselves and files' containing folders.
function void Prog__OpenInTabs(Prog_State *state, u64 row)
{
    if (state->loading) return;
    Arena *frame = &state->frameArena;
    // Each opens after the previous one, keeping the selection's order.
    u32 at = state->activeTab + 1;
    for (u64 i = 0; i < state->directory.count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        b32 wanted = row != UINT64_MAX ? i == row : state->selectedCount ? entry->selected : state->hasFocus && i == state->focusedRow;
        if (!wanted) continue;
        String name = entry->file.kind == P_FileKind_Directory ? entry->file.name : Prog_EntryParent(entry->file.name);
        String path = name.length ? Prog_JoinPath(frame, state->directory.path, name) : state->directory.path;
        Prog_OpenTab(state, path, at++, false);
    }
}

function void Prog_DragSelection(Prog_State *state)
{
    state->dragRequested = false;
    if (state->loading || !state->selectedCount) return;
    Scratch scratch = GetScratch();
    String *names = ArenaPushArray(scratch.arena, state->selectedCount, String);
    u64 *rows = ArenaPushArray(scratch.arena, state->selectedCount, u64);
    u64 count = 0;
    for (u64 i = 0; i < state->directory.count && count < state->selectedCount; ++i) {
        if (state->directory.entries[i].selected) rows[count++] = i;
    }
    String folder = Prog__RowsFolder(state, scratch.arena, rows, &count, names);
    state->dragging = true;
    P_DragFiles(folder, names, count);
    state->dragging = false;
    // The system's drag loop took the button release.
    UI_CancelActive();
    ReleaseScratch(scratch);
}

function b32 Prog_ProcessCommands(Prog_State *state)
{
    b32 changed = state->commandFirst != NULL;
    for (Prog_Command *command = state->commandFirst; command; command = command->next) {
        // Input in another panel's UI makes that panel active first.
        u32 from = command->panel ? Prog_PanelIndex(state, command->panel) : UINT32_MAX;
        if (from != UINT32_MAX && from != state->activePanel) Prog__ActivatePanel(state, from);
        switch (command->kind) {
            case Prog_CommandKind_SelectRow: {
                if (!state->loading) {
                    Prog_SelectRow(state, command->row, command->mods);
                    // Ctrl+double-click opens the item in a new tab instead.
                    if (command->pressCount == 2 && (command->mods & KeyMod_Control)) Prog__OpenInTabs(state, command->row);
                    else if (command->pressCount == 2) Prog_OpenRow(state, command->row);
                }
            } break;
            case Prog_CommandKind_OpenInTab: Prog__OpenInTabs(state, UINT64_MAX); break;
            case Prog_CommandKind_OpenRowInTab: if (command->row < state->directory.count) Prog__OpenInTabs(state, command->row); break;
            case Prog_CommandKind_SelectTab: Prog_SelectTab(state, (u32)command->row); break;
            case Prog_CommandKind_CloseTabAt: Prog_CloseTab(state, (u32)command->row); break;
            case Prog_CommandKind_MoveTab: Prog_MoveTab(state, (u32)command->row, (u32)command->target); break;
            case Prog_CommandKind_TabNew:
            case Prog_CommandKind_TabDuplicate: {
                // ponytail: new tabs show the current folder; a setting for other starting places can come later.
                String path = Prog_TabPath(state, state->activeTab);
                Prog_OpenTab(state, path, state->activeTab + 1, true);
            } break;
            case Prog_CommandKind_TabShiftLeft: if (state->activeTab) Prog_MoveTab(state, state->activeTab, state->activeTab - 1); break;
            case Prog_CommandKind_TabShiftRight: Prog_MoveTab(state, state->activeTab, state->activeTab + 1); break;
            case Prog_CommandKind_TabClose: Prog_CloseTab(state, state->activeTab); break;
            case Prog_CommandKind_TabCloseOthers:
            case Prog_CommandKind_TabCloseLeftTabs:
            case Prog_CommandKind_TabCloseRightTabs: {
                b32 left = command->kind != Prog_CommandKind_TabCloseRightTabs, right = command->kind != Prog_CommandKind_TabCloseLeftTabs;
                while (right && state->activeTab + 1 < state->tabCount) Prog_CloseTab(state, state->tabCount - 1);
                while (left && state->activeTab > 0) Prog_CloseTab(state, 0);
            } break;
            case Prog_CommandKind_TabReopen: {
                if (state->closedTabs.header.count) {
                    String path = state->closedTabs.v[--state->closedTabs.header.count];
                    Prog_OpenTab(state, path, state->activeTab + 1, true);
                }
            } break;
            case Prog_CommandKind_TabSelectNext:
            case Prog_CommandKind_TabSelectPrev: {
                // Wraps around at either end.
                u32 count = Max(state->tabCount, 1u);
                Prog_SelectTab(state, (state->activeTab + (command->kind == Prog_CommandKind_TabSelectNext ? 1 : count - 1)) % count);
            } break;
            case Prog_CommandKind_TabSelect1:
            case Prog_CommandKind_TabSelect2:
            case Prog_CommandKind_TabSelect3:
            case Prog_CommandKind_TabSelect4:
            case Prog_CommandKind_TabSelect5:
            case Prog_CommandKind_TabSelect6:
            case Prog_CommandKind_TabSelect7:
            case Prog_CommandKind_TabSelect8: Prog_SelectTab(state, (u32)(command->kind - Prog_CommandKind_TabSelect1)); break;
            case Prog_CommandKind_TabSelectLast: if (state->tabCount) Prog_SelectTab(state, state->tabCount - 1); break;
            case Prog_CommandKind_ToggleQuickAccess: {
                state->sidebarHidden = !state->sidebarHidden;
                state->sidebarToggled = PlatformNowMicroseconds();
            } break;
            case Prog_CommandKind_TogglePreview: state->previewShown = !state->previewShown; break;
            case Prog_CommandKind_InspectorToggleWordWrap: state->inspectorWrap = !state->inspectorWrap; break;
            case Prog_CommandKind_InspectorToggleDetails: state->inspectorDetails = !state->inspectorDetails; break;
            case Prog_CommandKind_InspectorFoldDetails: state->inspectorDetailsFolded = !state->inspectorDetailsFolded; break;
            case Prog_CommandKind_InspectorChangeLayout: state->inspectorLayout = state->inspectorBelow ? Prog_InspectorLayout_Right : Prog_InspectorLayout_Below; break;
            case Prog_CommandKind_InspectorSetLayout: if (command->row < Prog_InspectorLayout_Count) state->inspectorLayout = (u8)command->row; break;
            case Prog_CommandKind_InspectorStartFilter: {
                // A folder's list filters as the panel's does, with the keyboard in it.
                u32 inner = state->previewFolder ? Prog_InspectorPanel(state, state->panels[state->currentPanel].id, false) : UINT32_MAX;
                if (inner != UINT32_MAX) {
                    Prog__ActivatePanel(state, inner);
                    state->filtering = true;
                    break;
                }
                state->inspectorFiltering = true;
                state->inspectorFilter.anchor = 0;
                state->inspectorFilter.cursor = state->inspectorFilter.length;
            } break;
            case Prog_CommandKind_InspectorApplyFilter: state->inspectorFiltering = false; break;
            case Prog_CommandKind_InspectorRemoveFilter: {
                state->inspectorFilter = (Prog_TextEdit){ .version = state->inspectorFilter.version + 1 };
                state->inspectorFiltering = false;
            } break;
            case Prog_CommandKind_InspectorToggleLineNumbers: state->inspectorHideNumbers = !state->inspectorHideNumbers; break;
            case Prog_CommandKind_InspectorToggleWhitespace: state->inspectorWhitespace = !state->inspectorWhitespace; break;
            case Prog_CommandKind_InspectorToggleImageGrid: state->imageHideGrid = !state->imageHideGrid; break;
            case Prog_CommandKind_InspectorToggleImageMinimap: state->imageHideMinimap = !state->imageHideMinimap; break;
            case Prog_CommandKind_InspectorToggleImageScale: {
                state->imageScaleMode = state->imageScaleMode == Prog_ImageScale_Fit ? Prog_ImageScale_Original : Prog_ImageScale_Fit;
                state->imagePan = (UI_Vec2){0};
            } break;
            case Prog_CommandKind_InspectorSetImageScale: {
                if (command->row < Prog_ImageScale_Count) state->imageScaleMode = (u8)command->row;
                if (state->imageScaleMode == Prog_ImageScale_Custom && state->imageScale <= 0) state->imageScale = 1;
                state->imagePan = (UI_Vec2){0};
            } break;
            case Prog_CommandKind_InspectorScaleImageUp:
            case Prog_CommandKind_InspectorScaleImageDown: Prog__ScaleImage(state, command->kind == Prog_CommandKind_InspectorScaleImageUp); break;
            case Prog_CommandKind_InspectorIncreaseContentsSize:
            case Prog_CommandKind_InspectorDecreaseContentsSize: {
                // An image zooms instead of the text growing, and a folder's list zooms as a panel's does.
                u64 owner = state->panels[state->currentPanel].inspectorOf ? state->panels[state->currentPanel].inspectorOf : state->panels[state->currentPanel].id;
                u32 inner = state->previewFolder ? Prog_InspectorPanel(state, owner, false) : UINT32_MAX;
                if (inner != UINT32_MAX) {
                    b32 up = command->kind == Prog_CommandKind_InspectorIncreaseContentsSize;
                    u32 back = state->currentPanel;
                    Prog__UsePanel(state, inner);
                    if (up && state->zoom + 1 < Prog_ZoomLevels) ++state->zoom;
                    if (!up && state->zoom > 0) --state->zoom;
                    state->revealFocus = true;
                    Prog__UsePanel(state, back);
                    break;
                }
                if (state->previewImage) {
                    Prog__ScaleImage(state, command->kind == Prog_CommandKind_InspectorIncreaseContentsSize);
                    break;
                }
                u32 size = state->inspectorFontSize ? state->inspectorFontSize : Prog_InspectorFontSize;
                b32 up = command->kind == Prog_CommandKind_InspectorIncreaseContentsSize;
                state->inspectorFontSize = up ? Min(size + 1, (u32)Prog_InspectorFontMax) : Max(size - 1, (u32)Prog_InspectorFontMin);
            } break;
            case Prog_CommandKind_OpenSettings: {
                // A first open writes every setting there is, commented out at its default.
                if (!state->configDirectory.length) break;
                String path = Prog_JoinPath(&state->frameArena, state->configDirectory, S("Settings.txt"));
                String existing = {0};
                if (!P_ReadFile(&state->frameArena, path, &existing)) P_WriteFile(&state->frameArena, path, Prog_SettingsTemplate(&state->frameArena, state));
                P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
                Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
                input->kind = Prog_JobKind_Open;
                input->generation = state->generation;
                input->path = StrCopy(&job->arena, path);
                job->input = input;
                P_JobSubmit(job);
            } break;
            case Prog_CommandKind_Undo: if (!state->renaming) Prog_UndoLast(state); break;
            case Prog_CommandKind_BookmarksToggle: Prog_ToggleBookmark(state, state->directory.path); break;
            case Prog_CommandKind_FolderViewToggle: Prog_ToggleFolderView(state); break;
            case Prog_CommandKind_FolderViewsClear: Prog_ClearFolderViews(state); break;
            case Prog_CommandKind_BookmarksClear: {
                Prog__SetList(&state->bookmarkArena, &state->bookmarks, NULL, 0);
                Prog__SaveList(state, Prog_BookmarksFile, state->bookmarks);
            } break;
            case Prog_CommandKind_RecentsClear: {
                Prog__SetList(&state->recentArena, &state->recents, NULL, 0);
                Prog__SaveList(state, Prog_RecentsFile, state->recents);
            } break;
            case Prog_CommandKind_SidebarOpen:
            case Prog_CommandKind_SidebarOpenInTab: {
                String path = Prog__SidebarPath(state, (Prog_Group)command->row, command->target);
                if (!path.length) break;
                if (command->kind == Prog_CommandKind_SidebarOpenInTab) Prog_OpenTab(state, path, state->activeTab + 1, false);
                else if (!StrIsEqual(path, state->directory.path)) Prog_RequestDirectory(state, path, true, 0);
            } break;
            case Prog_CommandKind_SidebarMenu: {
                String path = Prog__SidebarPath(state, (Prog_Group)command->row, command->target);
                if (!path.length) break;
                Prog_CloseMenu(state);
                state->menuTarget = StrCopy(&state->menuArena, path);
                // A drive's root has no folder above it, so its own background menu stands in.
                String parent = Prog_ParentPath(path), name = Prog__FolderName(path);
                b32 root = StrIsEqual(parent, path) || !parent.length;
                state->menuOpen = P_ContextMenuOpen(&state->menuArena, root ? path : parent, &name, root ? 0 : 1, &state->menu);
                if (!state->menuOpen) {
                    Prog_CloseMenu(state);
                    break;
                }
                state->menuIcons = ArenaPushArray(&state->menuArena, Max(state->menu.count, 1u), const Prog_Icon *);
                for (u32 i = 0; i < state->menu.count; ++i) state->menuIcons[i] = Prog_ImageIcon(state, state->menu.items[i].icon);
                state->menuAt = state->ui.mouse;
            } break;
            case Prog_CommandKind_ToggleGroup: if (command->row < Prog_Group_Count) state->groupToggled[command->row] = !state->groupToggled[command->row]; break;
            case Prog_CommandKind_RemoveBookmark: {
                if (command->row < state->bookmarks.header.count) Prog_ToggleBookmark(state, state->bookmarks.v[command->row]);
            } break;
            case Prog_CommandKind_OpenFocused: if (state->hasFocus) Prog_OpenRow(state, state->focusedRow); break;
            case Prog_CommandKind_Refresh: {
                Prog_Reload(state);
            } break;
            case Prog_CommandKind_Back: {
                if (state->historyAt > 0) Prog_RequestDirectory(state, state->history.v[state->historyAt - 1], false, state->historyAt - 1);
            } break;
            case Prog_CommandKind_Forward: {
                if (state->historyAt + 1 < state->history.header.count) Prog_RequestDirectory(state, state->history.v[state->historyAt + 1], false, state->historyAt + 1);
            } break;
            case Prog_CommandKind_Up: {
                String parent = Prog_ParentPath(state->directory.path);
                if (parent.length && !StrIsEqual(parent, state->directory.path)) Prog_RequestDirectory(state, parent, true, 0);
            } break;
            case Prog_CommandKind_SortByName:
            case Prog_CommandKind_SortBySize:
            case Prog_CommandKind_SortByDateCreated:
            case Prog_CommandKind_SortByDateModified:
            case Prog_CommandKind_SortByDateAccessed:
            case Prog_CommandKind_SortByItemCount:
            case Prog_CommandKind_SortByType: {
                local_persist const Prog_Sort sorts[] = { Prog_Sort_Name, Prog_Sort_Size, Prog_Sort_Created, Prog_Sort_Modified, Prog_Sort_Accessed, Prog_Sort_Items, Prog_Sort_Type };
                static_assert(Prog_CommandKind_SortByType - Prog_CommandKind_SortByName + 1 == CArrayCount(sorts), "a sort for every Sort By command");
                command->row = (u64)sorts[command->kind - Prog_CommandKind_SortByName];
            }
                [[fallthrough]];
            case Prog_CommandKind_Sort: {
                // A new sort starts with the biggest and newest first, names and types from A; the same sort again turns it around.
                if (command->row < Prog_Sort_Count) {
                    b32 alphabetic = command->row == Prog_Sort_Name || command->row == Prog_Sort_Type;
                    state->descending = state->sort == command->row ? !state->descending : !alphabetic;
                    state->sort = (u8)command->row;
                    Prog_Reload(state);
                }
            } break;
            case Prog_CommandKind_NavigatePrefix: {
                String path = state->directory.path;
                if (command->row > 0 && command->row < path.length) {
                    // A drive prefix needs its slash: "C:" is the drive's current directory, "C:/" its root.
                    u64 length = path.value[command->row - 1] == ':' ? command->row + 1 : command->row;
                    Prog_RequestDirectory(state, StrFromTo(path, 0, (i64)length), true, 0);
                }
            } break;
            case Prog_CommandKind_ToggleHidden: {
                state->showHidden = !state->showHidden;
                Prog_Reload(state);
            } break;
            case Prog_CommandKind_MarqueeStart: {
                state->marqueeAdds = command->mods & (KeyMod_Control | KeyMod_Shift);
                for (u64 i = 0; i < state->directory.count; ++i) state->directory.entries[i].marqueeBase = state->directory.entries[i].selected;
            } break;
            case Prog_CommandKind_MarqueeSelect: {
                if (state->loading) break;
                b32 toggles = (state->marqueeAdds & KeyMod_Control) != 0;
                for (u64 i = 0; i < state->directory.count; ++i) {
                    Prog_Entry *entry = &state->directory.entries[i];
                    entry->selected = state->marqueeAdds && entry->marqueeBase;
                }
                // Items whose box the selection box touches, found from the grid of the last layout.
                R_Rect box = state->marqueeBox, item = state->gridItem;
                UI_Vec2 pitch = state->gridPitch;
                u64 columns = Max(state->gridColumns, (u64)1), first = UINT64_MAX, last = 0;
                if (pitch.x > 0 && pitch.y > 0 && state->viewCount) {
                    u64 lines = (state->viewCount + columns - 1) / columns;
                    u64 line0 = (u64)MaxF32(0, floorf((box.y0 - item.y1) / pitch.y)), line1 = Min(lines - 1, (u64)MaxF32(0, floorf((box.y1 - item.y0) / pitch.y)) + 1);
                    for (u64 line = line0; line <= line1 && line < lines; ++line) {
                        for (u64 column = 0; column < columns; ++column) {
                            u64 at = line * columns + column;
                            if (at >= state->viewCount) break;
                            f32 x = (f32)column * pitch.x, y = (f32)line * pitch.y;
                            if (!(item.x0 + x < box.x1 && box.x0 < item.x1 + x && item.y0 + y < box.y1 && box.y0 < item.y1 + y)) continue;
                            Prog_Entry *entry = &state->directory.entries[state->view[at]];
                            entry->selected = toggles ? !entry->marqueeBase : true;
                            first = Min(first, at);
                            last = Max(last, at);
                        }
                    }
                }
                state->selectedCount = 0;
                for (u64 i = 0; i < state->directory.count; ++i) state->selectedCount += state->directory.entries[i].selected != 0;
                // Focus follows the moving corner; the corner the box began at becomes the anchor for Shift.
                if (first != UINT64_MAX) {
                    b32 down = command->pressCount != 0;
                    state->focusedRow = state->view[down ? last : first];
                    state->anchorRow = state->view[down ? first : last];
                    state->hasFocus = state->hasAnchor = true;
                }
            } break;
            case Prog_CommandKind_SplitRight:
            case Prog_CommandKind_SplitBottom: {
                String path = Prog_TabPath(state, state->activeTab);
                Prog_SplitPanel(state, state->activePanel, command->kind == Prog_CommandKind_SplitRight ? Prog_SplitKind_Horizontal : Prog_SplitKind_Vertical, false, path);
            } break;
            case Prog_CommandKind_TabDrop: Prog_DropTab(state, state->tabDropped); break;
            case Prog_CommandKind_PanelSelectNext:
            case Prog_CommandKind_PanelSelectPrev: {
                u64 order[Prog_MaxPanels];
                u32 count = Prog__PanelsInOrder(state, 0, order, 0), at = 0;
                if (count < 2) break;
                for (u32 i = 0; i < count; ++i) if (order[i] == state->panels[state->activePanel].id) at = i;
                at = command->kind == Prog_CommandKind_PanelSelectNext ? (at + 1) % count : (at + count - 1) % count;
                u32 index = Prog_PanelIndex(state, order[at]);
                if (index != UINT32_MAX) Prog__ActivatePanel(state, index);
            } break;
            case Prog_CommandKind_PanelLeft: Prog_FocusPanelToward(state, -1, 0); break;
            case Prog_CommandKind_PanelRight: Prog_FocusPanelToward(state, 1, 0); break;
            case Prog_CommandKind_PanelUp: Prog_FocusPanelToward(state, 0, -1); break;
            case Prog_CommandKind_PanelDown: Prog_FocusPanelToward(state, 0, 1); break;
            case Prog_CommandKind_ResizeSplit: {
                if (command->row < Prog_MaxSplits) state->splits[command->row].ratio = UI__Clamp((f32)command->target / 1e6f, 0.1f, 0.9f);
            } break;
            case Prog_CommandKind_ZoomIn: if (state->zoom + 1 < Prog_ZoomLevels) { ++state->zoom; state->revealFocus = true; } break;
            case Prog_CommandKind_ZoomOut: if (state->zoom) { --state->zoom; state->revealFocus = true; } break;
            case Prog_CommandKind_ViewDetails: state->zoom = 0; state->revealFocus = true; break;
            case Prog_CommandKind_ViewList: state->zoom = 1; state->revealFocus = true; break;
            case Prog_CommandKind_ViewColumns: state->zoom = 2; state->revealFocus = true; break;
            case Prog_CommandKind_ViewMediumIcons: state->zoom = Prog_ZoomFirstIcon; state->revealFocus = true; break;
            case Prog_CommandKind_ViewLargeIcons: state->zoom = Prog_ZoomFirstIcon + 7; state->revealFocus = true; break;
            case Prog_CommandKind_ViewXLIcons: state->zoom = Prog_ZoomLevels - 1; state->revealFocus = true; break;
            case Prog_CommandKind_SelectAll:
            case Prog_CommandKind_ToggleSelectAll: {
                if (!state->loading) {
                    b32 select = command->kind == Prog_CommandKind_SelectAll || state->selectedCount < state->viewCount || !state->viewCount;
                    for (u64 i = 0; i < state->viewCount; ++i) state->directory.entries[state->view[i]].selected = select;
                    state->selectedCount = select ? state->viewCount : 0;
                }
            } break;
            case Prog_CommandKind_FocusUp:
            case Prog_CommandKind_FocusDown:
            case Prog_CommandKind_FocusLeft:
            case Prog_CommandKind_FocusRight:
            case Prog_CommandKind_FocusFirst:
            case Prog_CommandKind_FocusLast:
            case Prog_CommandKind_FocusPageUp:
            case Prog_CommandKind_FocusPageDown: {
                if (!state->loading && state->viewCount) {
                    // Moves through the rows the filter shows.
                    u64 row = state->hasFocus ? Prog_ViewPosition(state, state->focusedRow) : 0;
                    if (row == UINT64_MAX) row = 0;
                    // In a grid, up and down move a whole line and left and right one item; a line's worth of a page is a line of items.
                    u64 step = Max(state->gridColumns, (u64)1);
                    if ((command->kind == Prog_CommandKind_FocusLeft || command->kind == Prog_CommandKind_FocusRight) && step == 1) break;
                    u64 page = (state->pageRows > 1 ? state->pageRows - 1 : 1) * step;
                    u64 last = state->viewCount - 1;
                    switch (command->kind) {
                        case Prog_CommandKind_FocusUp: if (row >= step) row -= step; break;
                        // Down from a line with nothing under it goes to the last item, as in Explorer.
                        case Prog_CommandKind_FocusDown: if (state->hasFocus) row = row + step <= last ? row + step : row / step < last / step ? last : row; break;
                        case Prog_CommandKind_FocusLeft: if (row) --row; break;
                        case Prog_CommandKind_FocusRight: if (state->hasFocus && row < last) ++row; break;
                        case Prog_CommandKind_FocusFirst: row = 0; break;
                        case Prog_CommandKind_FocusLast: row = last; break;
                        case Prog_CommandKind_FocusPageUp: row = row > page ? row - page : 0; break;
                        case Prog_CommandKind_FocusPageDown: row = Min(row + page, last); break;
                        default: InvalidPath;
                    }
                    row = state->view[row];
                    if (command->mods & KeyMod_Control) { state->focusedRow = row; state->hasFocus = true; }
                    else Prog_SelectRow(state, row, command->mods);
                    state->revealFocus = true;
                }
            } break;
            case Prog_CommandKind_ContextMenu:
            case Prog_CommandKind_ContextMenuAt: Prog__OpenMenu(state, command); break;
            case Prog_CommandKind_ContextMenuInvoke: Prog__InvokeMenu(state, (u32)command->row); break;
            case Prog_CommandKind_ContextMenuClose: Prog_CloseMenu(state); break;
            case Prog_CommandKind_TransferAnswer: {
                Prog_Transfer *transfer = command->row < Prog_MaxTransfers ? &state->transfers[command->row] : NULL;
                if (!transfer || !transfer->job || !atomic_load(&transfer->progress.asking) || command->target >= P_Conflict_Count) break;
                atomic_store(&transfer->progress.answer, (u32)command->target);
                if (transfer->answerAll) atomic_store(&transfer->progress.policy, (u32)command->target);
                atomic_store(&transfer->progress.asking, false);
            } break;
            case Prog_CommandKind_TransferAnswerAll: {
                if (command->row < Prog_MaxTransfers) state->transfers[command->row].answerAll = !state->transfers[command->row].answerAll;
            } break;
            case Prog_CommandKind_TransferCancel: {
                Prog_Transfer *transfer = command->row < Prog_MaxTransfers ? &state->transfers[command->row] : NULL;
                if (transfer && transfer->job) P_RequestCancel(&transfer->job->cancelToken);
            } break;
            case Prog_CommandKind_CommandPalette: {
                Prog_CloseMenu(state);
                state->paletteOpen = !state->paletteOpen || state->paletteMode != Prog_PaletteMode_Commands;
                state->paletteMode = Prog_PaletteMode_Commands;
                state->palette = (Prog_TextEdit){0};
                state->paletteHighlight = 0;
                state->paletteScroll = 0;
            } break;
            case Prog_CommandKind_GoTo:
            case Prog_CommandKind_EditPath: {
                Prog_CloseMenu(state);
                b32 edit = command->kind == Prog_CommandKind_EditPath;
                state->paletteOpen = edit || !(state->paletteOpen && state->paletteMode == Prog_PaletteMode_GoTo);
                state->paletteMode = Prog_PaletteMode_GoTo;
                state->palette = (Prog_TextEdit){0};
                state->paletteHighlight = 0;
                state->gotoScroll = 0;
                ArenaClear(&state->gotoHistoryArena);
                state->gotoHistory = (ArrayStr){0};
                state->gotoHistoryAt = 0;
                if (edit && state->directory.path.length) {
                    Prog__GoToSetText(state, P_NativePath(&state->frameArena, Prog_JoinPath(&state->frameArena, state->directory.path, S(""))), false);
                    state->palette.anchor = 0;
                    state->paletteHighlight = UINT32_MAX;
                }
            } break;
            case Prog_CommandKind_GoToBack:
            case Prog_CommandKind_GoToForward: {
                b32 back = command->kind == Prog_CommandKind_GoToBack;
                if (back ? state->gotoHistoryAt == 0 : state->gotoHistoryAt + 1 >= state->gotoHistory.header.count) break;
                state->gotoHistoryAt = back ? state->gotoHistoryAt - 1 : state->gotoHistoryAt + 1;
                Prog__GoToSetText(state, state->gotoHistory.v[state->gotoHistoryAt], false);
            } break;
            case Prog_CommandKind_GoToUp: {
                u32 count;
                String folder;
                Prog_GoToMatches(state, &count, &folder);
                String parent = Prog_ParentPath(folder);
                // Above a root there are only the recent folders and the bookmarks again.
                if (!folder.length) break;
                if (!parent.length || StrIsEqual(parent, folder)) Prog__GoToSetText(state, S(""), true);
                else Prog__GoToExpand(state, parent);
            } break;
            case Prog_CommandKind_GoToFold: {
                if (command->row < Prog_GoToSection_Folder) state->gotoFolded[command->row] = !state->gotoFolded[command->row];
                state->paletteHighlight = 0;
            } break;
            case Prog_CommandKind_TabList: {
                Prog_CloseMenu(state);
                state->paletteOpen = !(state->paletteOpen && state->paletteMode == Prog_PaletteMode_Tabs);
                state->paletteMode = Prog_PaletteMode_Tabs;
                state->palette = (Prog_TextEdit){0};
                state->paletteHighlight = 0;
            } break;
            case Prog_CommandKind_PaletteClose: state->paletteOpen = false; break;
            case Prog_CommandKind_PaletteToggleDetails: state->paletteCompact = !state->paletteCompact; break;
            case Prog_CommandKind_PaletteRun: {
                if (!state->paletteOpen) break;
                if (state->paletteMode == Prog_PaletteMode_GoTo) {
                    u32 count;
                    String folder;
                    Prog_GoToItem *found = Prog_GoToMatches(state, &count, &folder);
                    u64 chosen = command->row == UINT64_MAX ? state->paletteHighlight : command->row;
                    // With nothing found, the text itself is the place to go.
                    Prog_GoToItem item = chosen < count ? found[chosen] : (Prog_GoToItem){ Prog__GoToPath(state, Prog_EditText(&state->palette)), Prog_GoToSection_Folder, false, false, {0}, NULL };
                    state->paletteOpen = false;
                    // Ctrl shows it in the panel instead: its folder, with it selected.
                    String name = Prog__FolderName(item.path), parent = Prog_ParentPath(item.path);
                    if ((command->mods & KeyMod_Control) && parent.length && !StrIsEqual(parent, item.path) && name.length <= sizeof(state->selectText)) {
                        MemCopy(state->selectText, name.value, name.length);
                        state->selectLength = (u32)name.length;
                        Prog_RequestDirectory(state, parent, !StrIsEqual(parent, state->directory.path), 0);
                    } else {
                        Prog__GoToOpen(state, item);
                    }
                    break;
                }
                if (state->paletteMode == Prog_PaletteMode_Options) {
                    // Enter turns a switch over and moves a choice on; steppers and sliders change with Left and Right.
                    u8 found[Prog_Option_Count];
                    u32 count = Prog_OptionMatches(state, found);
                    u64 chosen = command->row == UINT64_MAX ? state->paletteHighlight : command->row;
                    if (chosen >= count) break;
                    state->paletteHighlight = (u32)chosen;
                    Prog_Option option = (Prog_Option)found[chosen];
                    const Prog_OptionDesc *desc = &Prog_Options[option];
                    i32 value = Prog_OptionValue(state, option);
                    if (desc->control == Prog_OptionControl_Switch) Prog_SetOption(state, option, !value);
                    if (desc->control == Prog_OptionControl_Choice) Prog_SetOption(state, option, (value + 1) % (i32)desc->choiceCount);
                    break;
                }
                if (state->paletteMode == Prog_PaletteMode_Tabs) {
                    u32 tabs[Prog_MaxTabs];
                    u32 count = Prog_TabMatches(state, tabs);
                    u64 chosen = command->row == UINT64_MAX ? state->paletteHighlight : command->row;
                    state->paletteOpen = false;
                    if (chosen < count) Prog_SelectTab(state, tabs[chosen]);
                    break;
                }
                Prog_PaletteRow rows[Prog_MaxPaletteRows];
                u32 count = Prog_PaletteMatches(state, rows);
                u64 chosen = command->row == UINT64_MAX ? state->paletteHighlight : command->row;
                if (chosen >= count) break;
                // A group's title opens or closes the group.
                if (rows[chosen].kind == Prog_CommandKind_None) {
                    state->paletteExpanded ^= (u16)(1u << rows[chosen].group);
                    state->paletteHighlight = (u32)chosen;
                    break;
                }
                state->paletteOpen = false;
                Prog_QueueCommand(state, rows[chosen].kind, 0, 0, UINT64_MAX);
            } break;
            case Prog_CommandKind_ViewMenu:
            case Prog_CommandKind_HistoryMenu:
            case Prog_CommandKind_MainMenu:
            case Prog_CommandKind_PanelMenu: Prog__OpenProgramMenu(state, command); break;
            case Prog_CommandKind_HistoryGo: {
                if (command->row < state->history.header.count) Prog_RequestDirectory(state, state->history.v[command->row], false, command->row);
            } break;
            case Prog_CommandKind_NewWindow: {
                P_FileError error = P_OpenWindow(&state->frameArena, Prog_TabPath(state, state->activeTab));
                if (error.code) {
                    ArenaClear(&state->errorArena);
                    state->error = StrFormat(&state->errorArena, "New window: %S", error.message);
                }
            } break;
            case Prog_CommandKind_ToggleTransparency: Prog_SetOption(state, Prog_Option_Transparency, !state->transparent); break;
            case Prog_CommandKind_OpenOptions: {
                Prog_CloseMenu(state);
                state->paletteOpen = !(state->paletteOpen && state->paletteMode == Prog_PaletteMode_Options);
                state->paletteMode = Prog_PaletteMode_Options;
                state->palette = (Prog_TextEdit){0};
                state->paletteHighlight = 0;
                state->paletteScroll = 0;
            } break;
            case Prog_CommandKind_OptionSet: {
                if (command->pressCount) Prog_PreviewOption(state, (Prog_Option)command->row, (i32)(i64)command->target);
                else Prog_SetOption(state, (Prog_Option)command->row, (i32)(i64)command->target);
            } break;
            case Prog_CommandKind_ToggleDebugTheme: Prog_SetDebugTheme(state, !state->debugTheme); break;
            case Prog_CommandKind_WindowMove: state->moveRequested = true; break;
            case Prog_CommandKind_WindowMinimize: PlatformWindowMinimize(); break;
            case Prog_CommandKind_WindowToggleMaximize: PlatformWindowToggleMaximize(); break;
            case Prog_CommandKind_WindowClose: PlatformWindowClose(); break;
            case Prog_CommandKind_Rename: if (state->hasFocus) Prog_BeginRename(state, state->focusedRow); break;
            case Prog_CommandKind_RenameCommit: Prog__EndRename(state, true); break;
            case Prog_CommandKind_RenameCancel: Prog__EndRename(state, false); break;
            case Prog_CommandKind_StartFilter: {
                if (!state->renaming) {
                    state->filtering = true;
                    state->filter.anchor = 0;
                    state->filter.cursor = state->filter.length;
                }
            } break;
            case Prog_CommandKind_RenameCaret: {
                if (state->renaming) Prog__EditClick(&state->rename, (u32)command->row, command->mods, command->pressCount);
            } break;
            case Prog_CommandKind_FilterCaret: {
                if (!state->renaming) {
                    state->filtering = true;
                    Prog__EditClick(&state->filter, (u32)command->row, command->mods, command->pressCount);
                }
            } break;
            case Prog_CommandKind_ToggleExpandFolder:
            case Prog_CommandKind_StartExpandedSearch: {
                // Both toggle: the search ends as it started, back to the folder's own items with the filter kept, as the reference does.
                b32 expand = !state->expanded;
                state->expanded = expand;
                Prog_Reload(state);
                if (command->kind == Prog_CommandKind_StartExpandedSearch && expand && !state->renaming) {
                    state->filtering = true;
                    state->filter.anchor = 0;
                    state->filter.cursor = state->filter.length;
                }
            } break;
            case Prog_CommandKind_ApplyFilter: state->filtering = false; break;
            case Prog_CommandKind_RemoveFilter: {
                Prog_ClearFilter(state);
                Prog_ApplyFilter(state, false);
                state->revealFocus = true;
            } break;
            case Prog_CommandKind_ToggleFilterPin: state->filterSticky = !state->filterSticky; break;
            case Prog_CommandKind_ToggleFolders:
            case Prog_CommandKind_ToggleFiles: {
                if (command->kind == Prog_CommandKind_ToggleFolders) state->hideFolders = !state->hideFolders;
                else state->hideFiles = !state->hideFiles;
                Prog_ApplyFilter(state, false);
            } break;
            case Prog_CommandKind_CopySelection:
            case Prog_CommandKind_CutSelection:
            case Prog_CommandKind_Paste:
            case Prog_CommandKind_Trash:
            case Prog_CommandKind_SelectionDelete:
            case Prog_CommandKind_Duplicate:
            case Prog_CommandKind_CopyAsPath:
            case Prog_CommandKind_CreateFolder:
            case Prog_CommandKind_CreateFile: Prog__FileCommand(state, command->kind); break;
            case Prog_CommandKind_DeleteConfirm:
            case Prog_CommandKind_DeleteCancel: {
                if (state->confirmingDelete && command->kind == Prog_CommandKind_DeleteConfirm) {
                    Prog__FileJob(state, Prog_JobKind_Delete, state->confirmPaths, state->confirmCount, state->directory.path, false, (String){0});
                }
                state->confirmingDelete = false;
                state->confirmCount = 0;
                ArenaClear(&state->confirmArena);
            } break;
            case Prog_CommandKind_DragSelection: if (!state->dragging && !state->loading) state->dragRequested = true; break;
            default: InvalidPath;
        }
    }
    state->commandFirst = NULL;
    state->commandLast = NULL;
    if (state->sortPending) Prog__SortByTotals(state);
    // Files dragged onto the window go into the folder under the pointer, else into the folder of the panel under it.
    u32 dropPanel = Prog_PanelIndex(state, state->hasHoverRow ? state->hoverPanel : state->pointerPanel);
    if (dropPanel != UINT32_MAX) Prog__UsePanel(state, dropPanel);
    String drop = state->loading ? (String){0} : state->directory.path;
    if (!state->loading && state->hasHoverRow && state->hoverRow < state->directory.count) {
        const Prog_Entry *entry = &state->directory.entries[state->hoverRow];
        // A dragged folder cannot take itself.
        if (entry->file.kind == P_FileKind_Directory && !(state->dragging && entry->selected)) {
            drop = Prog_JoinPath(&state->frameArena, state->directory.path, entry->file.name);
        }
    }
    Prog__UsePanel(state, state->activePanel);
    P_SetDropDirectory(drop);
    // Folders dropped on the bookmarks are pinned where the last build showed the drop would go.
    ArrayStr pinned = P_TakeDroppedFolders(&state->frameArena);
    if (pinned.header.count) {
        Prog_PinBookmarks(state, &pinned, state->bookmarkDrop ? state->bookmarkDrop - 1 : state->bookmarks.header.count);
        changed = true;
    }
    state->bookmarkDrop = state->bookmarkDropShown;
    P_SetDropTakesFolders(state->bookmarkDrop != 0);
    Prog_SubmitIconRequests(state);
    Prog_KeepFolderView(state);
    // The header spinner turns while folders are being measured.
    Prog__UpdatePreview(state);
    // Transfers redraw while they run, for their progress and questions.
    b32 transferring = false;
    for (u32 i = 0; i < Prog_MaxTransfers; ++i) transferring |= state->transfers[i].job != NULL;
    return changed || transferring || Prog__AnyMeasuring(state);
}

function void Prog_HandleInput(Prog_State *state, PlatformEventList events)
{
    u64 order = 0;
    for (PlatformEvent *event = events.first; event; event = event->next) {
        ++order;
        // A key that ran a command does not also type its character, such as Shift+F's F or Space's space.
        if (event->kind == PlatformEventKind_Text && state->swallowText) {
            state->swallowText = false;
            continue;
        }
        if (event->kind == PlatformEventKind_KeyDown) state->swallowText = false;
        if (state->tabDrag && event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) {
            state->tabDrag = false;
            state->tabDragCancelled = true;
            continue;
        }
        // The palette takes the keyboard: typing finds commands, Up and Down pick one, Enter runs it and Escape closes.
        if (state->paletteOpen) {
            // The wheel moves Go To's highlight three rows a notch.
            if (event->kind == PlatformEventKind_MouseWheel && state->paletteMode != Prog_PaletteMode_Tabs) {
                Prog_PaletteRow rows[Prog_MaxPaletteRows];
                u32 count;
                String folder;
                u8 found[Prog_Option_Count];
                if (state->paletteMode == Prog_PaletteMode_GoTo) Prog_GoToMatches(state, &count, &folder);
                else if (state->paletteMode == Prog_PaletteMode_Options) count = Prog_OptionMatches(state, found);
                else count = Prog_PaletteMatches(state, rows);
                i64 at = (state->paletteHighlight == UINT32_MAX ? -1 : (i64)state->paletteHighlight) - (i64)(event->wheelY * 3);
                state->paletteHighlight = at <= 0 || !count ? 0 : (u32)Min(at, (i64)count - 1);
                continue;
            }
            if (event->kind == PlatformEventKind_Text) {
                Prog__EditInsert(&state->palette, event->codepoint);
                state->paletteHighlight = 0;
            } else if (event->kind == PlatformEventKind_KeyDown) {
                Prog_PaletteRow matches[Prog_MaxPaletteRows];
                u32 tabs[Prog_MaxTabs];
                b32 goTo = state->paletteMode == Prog_PaletteMode_GoTo;
                u32 count = 0;
                String folder = {0};
                Prog_GoToItem *found = goTo ? Prog_GoToMatches(state, &count, &folder) : NULL;
                u8 options[Prog_Option_Count];
                if (!goTo && state->paletteMode == Prog_PaletteMode_Options) count = Prog_OptionMatches(state, options);
                else if (!goTo) count = state->paletteMode == Prog_PaletteMode_Tabs ? Prog_TabMatches(state, tabs) : Prog_PaletteMatches(state, matches);
                // In Options, Left and Right step the highlighted option down and up, a slider by a twentieth of its range, while nothing is typed.
                if (state->paletteMode == Prog_PaletteMode_Options && !state->palette.length && state->paletteHighlight < count && (event->key == Key_Left || event->key == Key_Right)) {
                    Prog_Option option = (Prog_Option)options[state->paletteHighlight];
                    const Prog_OptionDesc *desc = &Prog_Options[option];
                    i32 step = desc->control == Prog_OptionControl_Slider ? Max((desc->most - desc->least) / 20, 1) : 1, value = Prog_OptionValue(state, option);
                    if (event->key == Key_Left) step = -step;
                    if (desc->control == Prog_OptionControl_Choice) value = (value + step + (i32)desc->choiceCount) % (i32)desc->choiceCount;
                    else value += step;
                    Prog_QueueCommand(state, Prog_CommandKind_OptionSet, (u64)option, 0, order)->target = (u64)(i64)value;
                    continue;
                }
                u32 version = state->palette.version;
                // Tab in Go To puts the highlighted folder in the box, to go on into it.
                if (goTo && event->key == Key_Tab) {
                    if (state->paletteHighlight < count && !found[state->paletteHighlight].file) Prog__GoToExpand(state, found[state->paletteHighlight].path);
                    continue;
                }
                // Its own hotkeys close it again.
                if (goTo && (event->key == Key_F4 || (event->key == Key_P && (event->mods & KeyMod_Control) && !(event->mods & KeyMod_Shift)))) {
                    Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, order);
                    continue;
                }
                // With nothing typed, Right opens the highlighted group and Left closes the group of the highlighted row, moving to its title.
                b32 tree = state->paletteMode == Prog_PaletteMode_Commands && !state->palette.length && state->paletteHighlight < count;
                if (tree && (event->key == Key_Right || event->key == Key_Left)) {
                    Prog_PaletteRow at = matches[state->paletteHighlight];
                    if (event->key == Key_Right && at.kind == Prog_CommandKind_None) state->paletteExpanded |= (u16)(1u << at.group);
                    if (event->key == Key_Left) {
                        state->paletteExpanded &= (u16)~(1u << at.group);
                        while (matches[state->paletteHighlight].kind != Prog_CommandKind_None) --state->paletteHighlight;
                    }
                    continue;
                }
                // Escape clears the text first, and closes once there is none.
                if (event->key == Key_Escape && state->palette.length) {
                    state->palette.anchor = 0;
                    state->palette.cursor = state->palette.length;
                    Prog__EditReplace(&state->palette, (const u8 *)"", 0);
                }
                else if (event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, order);
                else if (event->key == Key_Enter && !event->isRepeat) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, UINT64_MAX, 0, order)->mods = event->mods;
                // The command list wraps around; Go To stops at its ends, as the reference's does.
                else if (event->key == Key_Up) state->paletteHighlight = state->paletteHighlight && state->paletteHighlight != UINT32_MAX ? state->paletteHighlight - 1 : (count && !goTo ? count - 1 : 0);
                else if (event->key == Key_Down) state->paletteHighlight = state->paletteHighlight + 1 < count ? state->paletteHighlight + 1 : (goTo && count ? count - 1 : 0);
                else if ((event->key == (state->paletteMode == Prog_PaletteMode_Tabs ? Key_A : Key_P)) && (event->mods & KeyMod_Control) && (event->mods & KeyMod_Shift)) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, order);
                else Prog__EditKey(&state->palette, event);
                if (state->palette.version != version) state->paletteHighlight = 0;
            }
            continue;
        }
        // The delete confirmation takes the keyboard: Enter deletes, Escape cancels, and nothing else gets through.
        if (state->confirmingDelete) {
            if (event->kind == PlatformEventKind_KeyDown && !event->isRepeat && event->key == Key_Enter) Prog_QueueCommand(state, Prog_CommandKind_DeleteConfirm, 0, 0, order);
            if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_DeleteCancel, 0, 0, order);
            continue;
        }
        // The rename box takes the keyboard; a click anywhere else commits it.
        if (state->renaming) {
            if (event->kind == PlatformEventKind_MouseDown) {
                R_Rect box = state->renameRect;
                if (!(event->x >= box.x0 && event->x < box.x1 && event->y >= box.y0 && event->y < box.y1)) {
                    Prog_QueueCommand(state, Prog_CommandKind_RenameCommit, 0, 0, order);
                }
            } else if (event->kind == PlatformEventKind_Text) {
                Prog__EditInsert(&state->rename, event->codepoint);
            } else if (event->kind == PlatformEventKind_KeyDown) {
                if (event->key == Key_Enter) Prog_QueueCommand(state, Prog_CommandKind_RenameCommit, 0, 0, order);
                else if (event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_RenameCancel, 0, 0, order);
                else Prog__EditKey(&state->rename, event);
            }
            continue;
        }
        // A click outside the filter box gives the keyboard back to the list and keeps the filter.
        if (event->kind == PlatformEventKind_MouseDown && state->filtering) {
            R_Rect box = state->filterRect;
            if (!(event->x >= box.x0 && event->x < box.x1 && event->y >= box.y0 && event->y < box.y1)) state->filtering = false;
        }
        // Typing in a system menu searches it, from its first match on.
        if (event->kind == PlatformEventKind_Text && state->menuOpen) {
            if (!state->menuCommands) {
                Prog__EditInsert(&state->menuSearch, event->codepoint);
                state->menuDepth = 0;
                state->menuHighlight = UINT32_MAX;
                Prog__MenuMove(state, 1);
            }
            continue;
        }
        // The inspector's filter box takes the keyboard while it is in it: Enter keeps what it holds, Escape clears it.
        if (state->inspectorFiltering && !state->menuOpen) {
            if (event->kind == PlatformEventKind_Text) {
                Prog__EditInsert(&state->inspectorFilter, event->codepoint);
                continue;
            }
            if (event->kind == PlatformEventKind_KeyDown) {
                // Keys the box does not use, such as Up and Down, still reach the list; a space is typed.
                b32 used = true;
                if (event->key == Key_Enter) Prog_QueueCommand(state, Prog_CommandKind_InspectorApplyFilter, 0, 0, order);
                else if (event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_InspectorRemoveFilter, 0, 0, order);
                else used = Prog__EditKey(&state->inspectorFilter, event) || (event->key == Key_Space && !(event->mods & (KeyMod_Control | KeyMod_Alt | KeyMod_Super)));
                if (used) continue;
            }
        }
        // Typing anywhere in the browser filters it.
        if (event->kind == PlatformEventKind_Text && !state->menuOpen) {
            u32 version = state->filter.version;
            // '/' outside the box only puts the keyboard in it, like Ctrl+F.
            b32 opens = !state->filtering && event->codepoint == '/';
            state->filtering = true;
            if (!opens) Prog__EditInsert(&state->filter, event->codepoint);
            if (state->filter.version != version) Prog_ApplyFilter(state, true);
            continue;
        }
        // Ctrl+wheel zooms instead of scrolling, the inspector's contents under the pointer; fine-grained wheels add up to whole steps.
        if (event->kind == PlatformEventKind_MouseWheel && (event->mods & KeyMod_Control) && !state->menuOpen) {
            R_Rect pane = state->inspectorRect;
            b32 inspector = event->x >= pane.x0 && event->x < pane.x1 && event->y >= pane.y0 && event->y < pane.y1;
            Prog_CommandKind in = inspector ? Prog_CommandKind_InspectorIncreaseContentsSize : Prog_CommandKind_ZoomIn;
            Prog_CommandKind out = inspector ? Prog_CommandKind_InspectorDecreaseContentsSize : Prog_CommandKind_ZoomOut;
            state->zoomWheel += event->wheelY;
            for (; state->zoomWheel >= 1; state->zoomWheel -= 1) Prog_QueueCommand(state, in, 0, 0, order);
            for (; state->zoomWheel <= -1; state->zoomWheel += 1) Prog_QueueCommand(state, out, 0, 0, order);
            continue;
        }
        if (event->kind != PlatformEventKind_KeyDown) continue;
        KeyMods mods = event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super);
        // Escape removes the filter.
        // Keys the box does not use still reach the list: Up and Down move through it and Enter opens the focused item.
        if (!state->menuOpen && event->key == Key_Escape && (state->filtering || state->filter.length)) {
            Prog_QueueCommand(state, Prog_CommandKind_RemoveFilter, 0, 0, order);
            continue;
        }
        if (!state->menuOpen && state->filtering) {
            u32 version = state->filter.version;
            if (Prog__EditKey(&state->filter, event)) {
                if (state->filter.version != version) Prog_ApplyFilter(state, true);
                continue;
            }
        }
        // An open menu takes the keyboard.
        if (state->menuOpen) {
            switch (event->key) {
                case Key_Escape: {
                    if (state->menuDepth) state->menuHighlight = state->menuPath[--state->menuDepth];
                    else Prog_QueueCommand(state, Prog_CommandKind_ContextMenuClose, 0, 0, order);
                } break;
                case Key_Up: Prog__MenuMove(state, -1); break;
                case Key_Down: Prog__MenuMove(state, 1); break;
                case Key_Left: if (state->menuDepth) state->menuHighlight = state->menuPath[--state->menuDepth]; break;
                case Key_Right: {
                    if (state->menuHighlight < state->menu.count && state->menu.items[state->menuHighlight].childCount) Prog__MenuEnter(state);
                } break;
                case Key_Enter: {
                    if (state->menuHighlight >= state->menu.count) break;
                    if (state->menu.items[state->menuHighlight].childCount) Prog__MenuEnter(state);
                    else Prog_QueueCommand(state, Prog_CommandKind_ContextMenuInvoke, state->menuHighlight, 0, order);
                } break;
                default: {
                    u32 version = state->menuSearch.version;
                    if (!state->menuCommands && Prog__EditKey(&state->menuSearch, event) && state->menuSearch.version != version) {
                        state->menuDepth = 0;
                        state->menuHighlight = UINT32_MAX;
                        Prog__MenuMove(state, 1);
                    }
                } break;
            }
            continue;
        }
        // While filtering, a space is typed.
        if (state->filtering && event->key == Key_Space && !mods) continue;
        const Prog_Hotkey *hotkey = Prog_FindHotkey(state, event->key, mods);
        if (hotkey) state->swallowText = true;
        if (hotkey && (!event->isRepeat || !Prog_Commands[hotkey->command].noRepeat)) {
            Prog_Command *command = Prog_QueueCommand(state, hotkey->command, 0, 0, order);
            command->mods = event->mods;
        }
    }
}
