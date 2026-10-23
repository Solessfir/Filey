#define Prog_CommandsTable
#include "Meta/Commands.meta.h"
#include "Meta/Icons.meta.h"

// Commands' labels and descriptions, made once, with the bin named as this system names it where Commands.txt names Windows' Recycle Bin.
global Arena Prog__CommandTextArena;
global String Prog__CommandTexts[Prog_CommandKind_Count][2];

function String Prog__CommandText(Prog_CommandKind kind, u32 which)
{
    if (kind >= Prog_CommandKind_Count) return (String){0};
    String *text = &Prog__CommandTexts[kind][which];
    if (!text->value) {
        String source = Prog_Tr(which ? Prog_Commands[kind].tooltip : Prog_Commands[kind].label), bin = P_TrashName();
        Index at = StrIndexStr(source, NoTr("Recycle Bin"), SSF_None);
        *text = !at.exists || StrIsEqual(bin, NoTr("Recycle Bin")) ? source
              : StrFormat(&Prog__CommandTextArena, "%S%S%S", StrFromTo(source, 0, (i64)at.value), bin, StrFrom(source, at.value + NoTr("Recycle Bin").length));
        if (!text->value) *text = S("");
    }
    return *text;
}

function String Prog_CommandLabel(Prog_CommandKind kind) { return Prog__CommandText(kind, 0); }
function String Prog_CommandTooltip(Prog_CommandKind kind) { return Prog__CommandText(kind, 1); }

// Whether `kind` does anything on this system; the palette leaves out those that do not, as system files where files are never marked so.
function b32 Prog_CommandApplies(Prog_CommandKind kind)
{
    if (kind == Prog_CommandKind_SetupOpen) return P_CanInstall();
    return kind != Prog_CommandKind_ToggleSystemFiles || P_HasSystemFiles();
}

function Prog_Command *Prog_QueueCommand(Prog_State *state, Prog_CommandKind kind, u64 row, u32 pressCount, u64 inputOrder)
{
    Assert(kind > Prog_CommandKind_None && kind < Prog_CommandKind_Count);
    Prog_Command *command = ArenaPushStruct(&state->frameArena, Prog_Command);
    command->kind = (u16)kind;
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
    b32 addHistory = false, passing = false;
    u64 historyAt = state->historyAt;
    if (state->directoryJob) {
        Prog_JobInput *input = state->directoryJob->input;
        path = input->path;
        addHistory = input->addHistory;
        historyAt = input->historyAt;
        passing = input->passing;
    }
    if (!path.length) return;
    state->requestPassing = passing;
    Prog_RequestDirectory(state, path, addHistory, historyAt);
}

// Goes to `path` by Up or a crumb, passing through the folder on the way somewhere rather than opening it, which the recents do not count.
function void Prog__PassToDirectory(Prog_State *state, String path)
{
    state->requestPassing = true;
    Prog_RequestDirectory(state, path, true, 0);
}

function void Prog_CloseMenu(Prog_State *state)
{
    if (state->menuOpen) P_ContextMenuClose(&state->menu);
    ArenaClear(&state->menuArena);
    state->menu = (P_ContextMenu){0};
    state->menuOpen = false;
    MemZero(state->menuRects, sizeof(state->menuRects));
    state->menuRows = NULL;
    state->menuTarget = (String){0};
    state->menuIcons = NULL;
    state->menuCommands = NULL;
    state->menuRowCount = 0;
    state->menuDepth = 0;
    state->menuExpandedCount = 0;
    state->menuHighlight = UINT32_MAX;
    state->menuOpener = Prog_CommandKind_None;
    state->menuRight = 0;
    state->menuSearch = (Prog_TextEdit){0};
    state->menuBase = (P_ContextMenu){0};
    state->menuMoved = (String){0};
    ++state->menuSerial;
}

function b32 Prog_MenuPinned(const Prog_State *state, String label)
{
    for (u64 i = 0; label.length && i < state->menuPins.header.count; ++i) if (StrIsEqual(state->menuPins.v[i], label)) return true;
    return false;
}

// With inline submenus, whether the submenu of `index` is open under it.
function b32 Prog_MenuIsExpanded(const Prog_State *state, u32 index)
{
    for (u32 i = 0; i < state->menuExpandedCount; ++i) if (state->menuExpanded[i] == index) return true;
    return false;
}

// The open system menu in its pinned order: the pinned items first, in the order they were pinned, then a line and the rest as the system gave
// them; their submenus' children follow unchanged. Its icons are worked out again for the new places.
// Where `label` is in `list`, or -1.
function i64 Prog__ListPlace(const ArrayStr *list, String label)
{
    for (u64 i = 0; label.length && i < list->header.count; ++i) if (StrIsEqual(list->v[i], label)) return (i64)i;
    return -1;
}

// `base` as the menu editor left it: the top level without its hidden items, and those it ordered in its order, in the places they had
// between the rest; the submenus as they are.
function P_ContextMenu Prog__ShapeMenu(Prog_State *state, P_ContextMenu base)
{
    if (!state->menuHidden.header.count && !state->menuOrder.header.count) return base;
    Arena *frame = &state->frameArena;
    u32 *roots = ArenaPushArray(frame, base.rootCount + 1, u32), kept = 0;
    for (u32 i = 0; i < base.rootCount; ++i) {
        if (!base.items[i].separator && Prog__ListPlace(&state->menuHidden, base.items[i].label) >= 0) continue;
        roots[kept++] = i;
    }
    // The ordered ones fill their own places again, sorted by where the order has them.
    u32 *slots = ArenaPushArray(frame, kept + 1, u32), *moved = ArenaPushArray(frame, kept + 1, u32), count = 0;
    for (u32 k = 0; k < kept; ++k) {
        const P_MenuItem *item = &base.items[roots[k]];
        if (!item->separator && Prog__ListPlace(&state->menuOrder, item->label) >= 0) slots[count] = k, moved[count++] = roots[k];
    }
    for (u32 a = 1; a < count; ++a) {
        u32 take = moved[a];
        i64 rank = Prog__ListPlace(&state->menuOrder, base.items[take].label);
        u32 b = a;
        for (; b > 0 && Prog__ListPlace(&state->menuOrder, base.items[moved[b - 1]].label) > rank; --b) moved[b] = moved[b - 1];
        moved[b] = take;
    }
    for (u32 j = 0; j < count; ++j) roots[slots[j]] = moved[j];
    u32 children = base.count - base.rootCount;
    P_MenuItem *items = ArenaPushArray(frame, kept + children + 1, P_MenuItem);
    for (u32 k = 0; k < kept; ++k) items[k] = base.items[roots[k]];
    for (u32 c = 0; c < children; ++c) items[kept + c] = base.items[base.rootCount + c];
    for (u32 i = 0; i < kept + children; ++i) if (items[i].childCount) items[i].firstChild = items[i].firstChild - base.rootCount + kept;
    return (P_ContextMenu){ .items = items, .count = kept + children, .rootCount = kept, .native = base.native };
}

function void Prog__ApplyMenuPins(Prog_State *state)
{
    if (state->menuCommands || !state->menuOpen) return;
    if (!state->menuBase.items) state->menuBase = state->menu;
    P_ContextMenu base = Prog__ShapeMenu(state, state->menuBase);
    u32 *order = ArenaPushArray(&state->frameArena, base.rootCount + (u32)state->menuPins.header.count + 1, u32);
    b8 *taken = ArenaPushArray(&state->frameArena, base.rootCount + 1, b8);
    u32 pinned = 0;
    // A command the menu holds twice, as some handlers add theirs twice, shows once, pinned.
    // An item of a submenu, pinned where submenus open inside the menu, shows at the top as well as in its submenu.
    u32 liftedCount = 0;
    for (u64 p = 0; p < state->menuPins.header.count; ++p) {
        b32 placed = false;
        for (u32 i = 0; i < base.rootCount; ++i) {
            if (taken[i] || base.items[i].separator || !StrIsEqual(base.items[i].label, state->menuPins.v[p])) continue;
            taken[i] = true;
            if (!placed) order[pinned++] = i;
            placed = true;
        }
        for (u32 i = base.rootCount; i < base.count && !placed; ++i) {
            if (base.items[i].separator || !StrIsEqual(base.items[i].label, state->menuPins.v[p])) continue;
            ++liftedCount;
            order[pinned++] = i;
            placed = true;
        }
    }
    u32 line = pinned && pinned - liftedCount < base.rootCount ? 1 : 0;
    P_MenuItem *items = ArenaPushArray(&state->menuArena, base.count + pinned + line, P_MenuItem);
    // Which submenus are open goes by the items, whose places change.
    P_MenuItem *was = state->menu.items;
    u32 wasCount = state->menu.count, wasOpen[CArrayCount(state->menuExpanded)], wasOpenCount = state->menuExpandedCount;
    for (u32 i = 0; i < wasOpenCount; ++i) wasOpen[i] = state->menuExpanded[i];
    u32 at = 0;
    for (u32 i = 0; i < pinned; ++i) items[at++] = base.items[order[i]];
    if (line) items[at++] = (P_MenuItem){ .separator = true };
    for (u32 i = 0; i < base.rootCount; ++i) if (!taken[i]) items[at++] = base.items[i];
    // The children follow the top level, which has the line more and the second copies fewer.
    u32 roots = at;
    for (u32 i = base.rootCount; i < base.count; ++i) items[at++] = base.items[i];
    for (u32 i = 0; i < at; ++i) if (items[i].childCount) items[i].firstChild = items[i].firstChild - base.rootCount + roots;
    u32 wasRoots = state->menu.rootCount;
    state->menu.items = items;
    state->menu.count = at;
    state->menu.rootCount = roots;
    // Open submenus stay open: each is found again by its item, at the top level or in a submenu as it was.
    state->menuExpandedCount = 0;
    for (u32 k = 0; k < wasOpenCount && was; ++k) {
        u32 o = wasOpen[k];
        if (o >= wasCount) continue;
        for (u32 i = 0; i < at; ++i) {
            if ((i < roots) != (o < wasRoots) || items[i].id != was[o].id || items[i].childCount != was[o].childCount || !StrIsEqual(items[i].label, was[o].label)) continue;
            if (!Prog_MenuIsExpanded(state, i)) state->menuExpanded[state->menuExpandedCount++] = i;
            break;
        }
    }
    state->menuIcons = ArenaPushArray(&state->menuArena, Max(state->menu.count, 1u), const Prog_Icon *);
    for (u32 i = 0; i < state->menu.count; ++i) state->menuIcons[i] = Prog_ImageIcon(state, state->menu.items[i].icon);
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

// With inline submenus, the line that sets an open submenu's items off from those around it, which is no item of the menu.
#define Prog_MenuLine (UINT32_MAX - 1)
global const P_MenuItem Prog_MenuLineItem = { .separator = true };

function const P_MenuItem *Prog_MenuItemAt(const Prog_State *state, u32 index)
{
    return index == Prog_MenuLine ? &Prog_MenuLineItem : &state->menu.items[index];
}

// Whether the menu shows its submenus inside it; a search lists its top level alone.
function b32 Prog_MenuInline(const Prog_State *state)
{
    return state->menuInline && !Prog_MenuFilter(state, 0).length;
}

// The items of `first`..`count` and, under each open submenu, its own, `depth` deep, with a line before and after them where none is already.
function u32 Prog__MenuFlatten(const Prog_State *state, u32 first, u32 count, u8 depth, u32 *out, u8 *depths, u32 at)
{
    Scratch scratch = GetScratch();
    u32 *level = ArenaPushArray(scratch.arena, Max(count, 1u), u32);
    u32 shown = Prog_MenuVisible(&state->menu, first, count, (String){0}, level);
    for (u32 i = 0; i < shown; ++i) {
        const P_MenuItem *item = &state->menu.items[level[i]];
        depths[at] = depth;
        out[at++] = level[i];
        if (!item->childCount || !Prog_MenuIsExpanded(state, level[i]) || depth >= 8) continue;
        u32 start = at;
        depths[at] = (u8)(depth + 1);
        out[at++] = Prog_MenuLine;
        at = Prog__MenuFlatten(state, item->firstChild, item->childCount, (u8)(depth + 1), out, depths, at);
        // An empty submenu shows no lines; nor does one last in the menu, or with a line of the menu's own after it, close with one of its own.
        if (at == start + 1) { --at; continue; }
        b32 lineAfter = i + 1 < shown && state->menu.items[level[i + 1]].separator;
        if (i + 1 < shown && !lineAfter) {
            depths[at] = (u8)(depth + 1);
            out[at++] = Prog_MenuLine;
        }
    }
    ReleaseScratch(scratch);
    return at;
}

// The rows of an inline menu, its items and the open submenus' under them, into `out` with how deep each is in `depths`; both hold at least
// Prog_MenuInlineRoom entries. Returns how many.
function u32 Prog_MenuInlineRoom(const Prog_State *state) { return state->menu.count + 2 * state->menuExpandedCount + 2; }
function u32 Prog_MenuInlineVisible(const Prog_State *state, u32 *out, u8 *depths)
{
    return Prog__MenuFlatten(state, 0, state->menu.rootCount, 0, out, depths, 0);
}

// The item whose submenu holds `index`, or UINT32_MAX for one of the top level.
function u32 Prog_MenuOwner(const Prog_State *state, u32 index)
{
    if (index < state->menu.rootCount || index >= state->menu.count) return UINT32_MAX;
    for (u32 j = 0; j < state->menu.count; ++j) {
        const P_MenuItem *item = &state->menu.items[j];
        if (item->childCount && index >= item->firstChild && index < item->firstChild + item->childCount) return j;
    }
    return UINT32_MAX;
}

// Opens the submenu of `index` inside the menu, or closes it with those open inside it, which would otherwise show again with it all at once.
function void Prog_MenuToggleExpanded(Prog_State *state, u32 index)
{
    if (index >= state->menu.count || !state->menu.items[index].childCount) return;
    if (!Prog_MenuIsExpanded(state, index)) {
        if (state->menuExpandedCount < CArrayCount(state->menuExpanded)) state->menuExpanded[state->menuExpandedCount++] = index;
        return;
    }
    u32 kept = 0;
    for (u32 i = 0; i < state->menuExpandedCount; ++i) {
        u32 open = state->menuExpanded[i];
        b32 inside = false;
        for (u32 up = open, steps = 0; up != UINT32_MAX && !inside && steps <= 8; up = Prog_MenuOwner(state, up), ++steps) inside = up == index;
        if (!inside) state->menuExpanded[kept++] = open;
    }
    state->menuExpandedCount = kept;
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
    // Inline, the keyboard moves through the open submenus' items too.
    b32 flat = Prog_MenuInline(state);
    u32 room = flat ? Prog_MenuInlineRoom(state) : Max(count, 1u);
    u32 *visible = ArenaPushArray(scratch.arena, room, u32);
    u8 *depths = ArenaPushArray(scratch.arena, room, u8);
    u32 shown = flat ? Prog_MenuInlineVisible(state, visible, depths) : Prog_MenuVisible(&state->menu, first, count, Prog_MenuFilter(state, state->menuDepth), visible);
    i64 at = -1;
    for (u32 i = 0; i < shown; ++i) if (visible[i] == state->menuHighlight) at = i;
    for (u32 tries = 0; tries < shown; ++tries) {
        at = at < 0 ? (step > 0 ? 0 : (i64)shown - 1) : (at + step + (i64)shown) % (i64)shown;
        const P_MenuItem *item = Prog_MenuItemAt(state, visible[at]);
        if (!item->separator && (!item->disabled || item->childCount)) {
            state->menuHighlight = visible[at];
            state->menuReveal = true;
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
    // This PC's drives are named by their paths, which their menus take whole.
    if (StrIsEqual(state->directory.path, Prog_ThisPC)) {
        for (u64 i = 0; i < *count; ++i) names[i] = state->directory.entries[rows[i]].file.name;
        return (String){0};
    }
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
        item.label = StrCopy(&state->menuArena, item.label.length ? item.label : Prog_CommandLabel(item.kind));
        item.mouse = StrCopy(&state->menuArena, item.mouse);
        state->menuCommands[i] = item;
        state->menu.items[i] = (P_MenuItem){ .label = item.label, .id = i + 1, .separator = item.kind == Prog_CommandKind_None, .isDefault = (b8)(item.checked != 0),
                                             .disabled = item.disabled };
    }
    state->menu.count = state->menu.rootCount = count;
    state->menuOpen = true;
    state->menuAt = at;
    // The keyboard starts on the first item.
    state->menuHighlight = UINT32_MAX;
    for (u32 i = 0; i < count && state->menuHighlight == UINT32_MAX; ++i) if (!state->menu.items[i].separator && !state->menu.items[i].disabled) state->menuHighlight = i;
}

// Key names as Commands.txt and the settings spell them, and the ones chips show, with symbols for punctuation.
global const char *Prog_KeyNames[Key_Count] = {
    [Key_Escape] = "Escape", [Key_Enter] = "Enter", [Key_Tab] = "Tab", [Key_Backspace] = "Backspace", [Key_Delete] = "Delete", [Key_Insert] = "Insert",
    [Key_Space] = "Space", [Key_Left] = "Left", [Key_Right] = "Right", [Key_Up] = "Up", [Key_Down] = "Down", [Key_Home] = "Home", [Key_End] = "End",
    [Key_PageUp] = "PageUp", [Key_PageDown] = "PageDown", [Key_ContextMenu] = "ContextMenu", [Key_Comma] = "Comma",
    [Key_Equals] = "Equals", [Key_Minus] = "Minus", [Key_Slash] = "Slash", [Key_MouseX1] = "MouseX1", [Key_MouseX2] = "MouseX2",
    [Key_Keypad0] = "Keypad0", [Key_Keypad1] = "Keypad1", [Key_Keypad2] = "Keypad2", [Key_Keypad3] = "Keypad3", [Key_Keypad4] = "Keypad4",
    [Key_Keypad5] = "Keypad5", [Key_Keypad6] = "Keypad6", [Key_Keypad7] = "Keypad7", [Key_Keypad8] = "Keypad8", [Key_Keypad9] = "Keypad9",
};
global const char *Prog_KeyShortNames[Key_Count] = {
    [Key_Escape] = "Escape", [Key_Enter] = "Enter", [Key_Tab] = "Tab", [Key_Backspace] = "Backspace", [Key_Delete] = "Delete", [Key_Insert] = "Insert",
    [Key_Space] = "Space", [Key_Left] = "Left", [Key_Right] = "Right", [Key_Up] = "Up", [Key_Down] = "Down", [Key_Home] = "Home", [Key_End] = "End",
    [Key_PageUp] = "PageUp", [Key_PageDown] = "PageDown", [Key_ContextMenu] = "Menu", [Key_Comma] = ",",
    [Key_Equals] = "=", [Key_Minus] = "-", [Key_Slash] = "/", [Key_MouseX1] = "MouseX1", [Key_MouseX2] = "MouseX2",
    [Key_Keypad0] = "Keypad0", [Key_Keypad1] = "Keypad1", [Key_Keypad2] = "Keypad2", [Key_Keypad3] = "Keypad3", [Key_Keypad4] = "Keypad4",
    [Key_Keypad5] = "Keypad5", [Key_Keypad6] = "Keypad6", [Key_Keypad7] = "Keypad7", [Key_Keypad8] = "Keypad8", [Key_Keypad9] = "Keypad9",
};

function b32 Prog__EqualFolded(String a, String b)
{
    if (a.length != b.length) return false;
    for (u64 i = 0; i < a.length; ++i) {
        if (Prog__FoldAscii((u8)a.value[i]) != Prog__FoldAscii((u8)b.value[i])) return false;
    }
    return true;
}

// The hotkey in `scope` that `key` with `mods` runs after `lead`, or Key_None for a key on its own: the settings' own first, then the defaults of the
// commands they leave alone.
function const Prog_Hotkey *Prog_FindHotkey(const Prog_State *state, Prog_HotkeyScope scope, Key lead, Key key, KeyMods mods)
{
    for (u64 i = 0; i < state->userHotkeyCount + CArrayCount(Prog_DefaultHotkeys); ++i) {
        b32 user = i < state->userHotkeyCount;
        const Prog_Hotkey *hotkey = user ? &state->userHotkeys[i] : &Prog_DefaultHotkeys[i - state->userHotkeyCount];
        if (hotkey->key == key && hotkey->mods == mods && hotkey->lead == lead && Prog_Commands[hotkey->command].scope == scope && (user || !state->hotkeyOverridden[hotkey->command])) {
            return hotkey;
        }
    }
    return NULL;
}

// Whether `key` starts a sequence in `scope`, such as the first G of "G G".
function b32 Prog__HotkeyLeads(const Prog_State *state, Prog_HotkeyScope scope, Key key)
{
    for (u64 i = 0; i < state->userHotkeyCount + CArrayCount(Prog_DefaultHotkeys); ++i) {
        b32 user = i < state->userHotkeyCount;
        const Prog_Hotkey *hotkey = user ? &state->userHotkeys[i] : &Prog_DefaultHotkeys[i - state->userHotkeyCount];
        if (hotkey->lead == key && Prog_Commands[hotkey->command].scope == scope && (user || !state->hotkeyOverridden[hotkey->command])) return true;
    }
    return false;
}

// A key that types a character, without modifiers but Shift: letters, digits and punctuation. Space and '/' are left out, since their
// hotkeys run however Character Key Action is set.
function b32 Prog_IsCharacterKey(Key key, KeyMods mods)
{
    b32 typed = (key >= Key_A && key <= Key_Z) || (key >= Key_0 && key <= Key_9) || (key >= Key_Keypad0 && key <= Key_Keypad9) ||
                key == Key_Comma || key == Key_Equals || key == Key_Minus;
    return typed && !(mods & ~(KeyMods)KeyMod_Shift);
}

// "Ctrl+Shift+S", modifiers in any order and case, or a sequence of two keys such as "G G"; false when it names no key.
function b32 Prog_ParseHotkey(String text, Prog_Hotkey *hotkey)
{
    hotkey->key = hotkey->lead = Key_None;
    hotkey->mods = 0;
    while (text.length && text.value[0] == ' ') text = StrFrom(text, 1);
    while (text.length && text.value[text.length - 1] == ' ') --text.length;
    // A space between two keys, rather than beside a '+', splits a sequence.
    for (u64 i = 1; i + 1 < text.length; ++i) {
        if (text.value[i] != ' ' || text.value[i - 1] == '+' || text.value[i - 1] == ' ' || text.value[i + 1] == '+' || text.value[i + 1] == ' ') continue;
        Prog_Hotkey lead = {0};
        if (!Prog_ParseHotkey(StrFromTo(text, 0, (i64)i), &lead) || lead.mods || lead.lead) return false;
        b32 parsed = Prog_ParseHotkey(StrFrom(text, i + 1), hotkey);
        hotkey->lead = lead.key;
        return parsed && !hotkey->mods;
    }
    Key *key = &hotkey->key;
    KeyMods *mods = &hotkey->mods;
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

// A key's name: "A", "F5", and the symbols such as "," and "=" for showing, or the names Settings.conf reads back, such as "Comma" and "Equals".
function String Prog__KeyName(Arena *arena, Key key, b32 spelled)
{
    if (key >= Key_A && key <= Key_Z) return StrFormat(arena, "%c", 'A' + (i32)(key - Key_A));
    if (key >= Key_0 && key <= Key_9) return StrFormat(arena, "%c", '0' + (i32)(key - Key_0));
    if (key >= Key_F1 && key <= Key_F12) return StrFormat(arena, "F%u", (u32)(key - Key_F1) + 1);
    const char *name = key < Key_Count ? (spelled ? Prog_KeyNames : Prog_KeyShortNames)[key] : NULL;
    return name ? StrFromCStr(name) : (String){0};
}

// "Ctrl+Shift+S", or "G G" for a sequence; empty for a key without a name.
function String Prog_HotkeyString(Arena *arena, const Prog_Hotkey *hotkey, b32 spelled)
{
    String name = Prog__KeyName(arena, hotkey->key, spelled), lead = Prog__KeyName(arena, hotkey->lead, spelled);
    if (!name.length) return name;
    return StrFormat(arena, "%S%s%s%s%s%s%S", lead, lead.length ? " " : "", (hotkey->mods & KeyMod_Control) ? "Ctrl+" : "", (hotkey->mods & KeyMod_Alt) ? "Alt+" : "",
                     (hotkey->mods & KeyMod_Shift) ? "Shift+" : "", (hotkey->mods & KeyMod_Super) ? "Super+" : "", name);
}

// The hotkeys `kind` has, up to `max` of them, in the order they are looked up: the settings' own, or else its defaults; returns how many.
function u32 Prog_CommandHotkeys(const Prog_State *state, Prog_CommandKind kind, const Prog_Hotkey **out, u32 max)
{
    u32 count = 0;
    for (u64 i = 0; i < state->userHotkeyCount + CArrayCount(Prog_DefaultHotkeys) && count < max; ++i) {
        b32 user = i < state->userHotkeyCount;
        const Prog_Hotkey *hotkey = user ? &state->userHotkeys[i] : &Prog_DefaultHotkeys[i - state->userHotkeyCount];
        if (hotkey->command == kind && (user || !state->hotkeyOverridden[kind])) out[count++] = hotkey;
    }
    return count;
}

// Whether `hotkey` does nothing now, being a character key that types while Character Key Action is not Trigger Hotkey.
function b32 Prog_HotkeyIdle(const Prog_State *state, const Prog_Hotkey *hotkey)
{
    b32 character = Prog_IsCharacterKey(hotkey->key, hotkey->mods) || Prog_IsCharacterKey(hotkey->lead, 0);
    return character && Prog_Commands[hotkey->command].scope == Prog_HotkeyScope_Browser && state->characterKeys != Prog_CharacterKeys_TriggerHotkey;
}

// Every hotkey of `kind` as "Ctrl+Shift+S", up to `max` of them, in the order they are looked up, and into `idle`, when given, whether each
// does nothing now; returns how many.
function u32 Prog_HotkeyTexts(Arena *arena, const Prog_State *state, Prog_CommandKind kind, String *out, b8 *idle, u32 max)
{
    const Prog_Hotkey *hotkeys[16];
    u32 found = Prog_CommandHotkeys(state, kind, hotkeys, Min(max, (u32)CArrayCount(hotkeys))), count = 0;
    for (u32 i = 0; i < found; ++i) {
        String text = Prog_HotkeyString(arena, hotkeys[i], false);
        if (!text.length) continue;
        if (idle) idle[count] = (b8)Prog_HotkeyIdle(state, hotkeys[i]);
        out[count++] = text;
    }
    return count;
}

// `hotkeys` as a hotkey.<Command> line of Settings.conf takes them, "Ctrl+Comma, F6", or "none".
function String Prog_HotkeyListSetting(Arena *arena, const Prog_Hotkey *hotkeys, u32 count)
{
    StrBuilder out = {0};
    for (u32 i = 0; i < count; ++i) {
        String text = Prog_HotkeyString(arena, &hotkeys[i], true);
        if (!text.length) continue;
        if (out.buffer.length) StrBuildStr(arena, &out, S(", "));
        StrBuildStr(arena, &out, text);
    }
    return out.buffer.length ? out.buffer : S("none");
}

function String Prog_HotkeysSetting(Arena *arena, const Prog_State *state, Prog_CommandKind kind)
{
    const Prog_Hotkey *found[16];
    Prog_Hotkey hotkeys[16];
    u32 count = Prog_CommandHotkeys(state, kind, found, CArrayCount(found));
    for (u32 i = 0; i < count; ++i) hotkeys[i] = *found[i];
    return Prog_HotkeyListSetting(arena, hotkeys, count);
}

// `kind`'s default hotkeys, up to `max` of them, in Commands.txt's order; returns how many.
function u32 Prog_DefaultHotkeysOf(Prog_CommandKind kind, Prog_Hotkey *out, u32 max)
{
    u32 count = 0;
    for (u64 i = 0; i < CArrayCount(Prog_DefaultHotkeys) && count < max; ++i) {
        if (Prog_DefaultHotkeys[i].command == kind) out[count++] = Prog_DefaultHotkeys[i];
    }
    return count;
}

// Whether `kind`'s hotkeys differ from its defaults, which the palette's reset button brings back.
function b32 Prog_HotkeysChanged(const Prog_State *state, Prog_CommandKind kind)
{
    if (!state->hotkeyOverridden[kind]) return false;
    const Prog_Hotkey *now[16];
    Prog_Hotkey defaults[16];
    u32 count = Prog_CommandHotkeys(state, kind, now, CArrayCount(now)), expected = Prog_DefaultHotkeysOf(kind, defaults, CArrayCount(defaults));
    if (count != expected) return true;
    for (u32 i = 0; i < count; ++i) {
        if (now[i]->key != defaults[i].key || now[i]->mods != defaults[i].mods || now[i]->lead != defaults[i].lead) return true;
    }
    return false;
}

// Whether another command has `hotkey`'s key where it works, so that one of them never runs from it.
function b32 Prog_HotkeyConflicts(const Prog_State *state, const Prog_Hotkey *hotkey)
{
    for (u64 i = 0; i < state->userHotkeyCount + CArrayCount(Prog_DefaultHotkeys); ++i) {
        b32 user = i < state->userHotkeyCount;
        const Prog_Hotkey *other = user ? &state->userHotkeys[i] : &Prog_DefaultHotkeys[i - state->userHotkeyCount];
        if (other->command == hotkey->command || (!user && state->hotkeyOverridden[other->command])) continue;
        if (other->key == hotkey->key && other->mods == hotkey->mods && other->lead == hotkey->lead &&
            Prog_Commands[other->command].scope == Prog_Commands[hotkey->command].scope) {
            return true;
        }
    }
    return false;
}

// The chips of `kind`'s hotkeys, up to `max` of them: the one being rebound as "Press A Key", and a new one after them while one is being added.
// Returns how many.
function u32 Prog_HotkeyChips(Arena *arena, const Prog_State *state, Prog_CommandKind kind, Prog_HotkeyChip *out, u32 max)
{
    const Prog_Hotkey *hotkeys[16];
    u32 found = Prog_CommandHotkeys(state, kind, hotkeys, CArrayCount(hotkeys)), count = 0;
    b32 recording = kind && state->hotkeyKind == kind && !state->hotkeyAsking;
    for (u32 i = 0; i < found && count < max; ++i) {
        String text = Prog_HotkeyString(arena, hotkeys[i], false);
        if (!text.length) continue;
        b32 here = recording && state->hotkeySlot == i;
        out[count++] = (Prog_HotkeyChip){ .text = here ? Tr("Press A Key") : text, .slot = (u8)i, .recording = (b8)here,
                                          .idle = (b8)(!here && Prog_HotkeyIdle(state, hotkeys[i])), .conflict = (b8)(!here && Prog_HotkeyConflicts(state, hotkeys[i])) };
    }
    if (recording && state->hotkeySlot >= found && count < max) out[count++] = (Prog_HotkeyChip){ .text = Tr("Press A Key"), .slot = state->hotkeySlot, .recording = true, .adds = true };
    return count;
}

function String Prog_HotkeyText(Arena *arena, const Prog_State *state, Prog_CommandKind kind)
{
    String result = {0};
    Prog_HotkeyTexts(arena, state, kind, &result, NULL, 1);
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

// The palette's groups and their commands. Quick holds the starred commands, Prog_State.quickCommands, which their own groups then leave out.
#define Prog__PaletteGroup(name) ((u32)Prog_CommandKind_Count + (u32)Prog_CommandGroup_##name)
global const u16 Prog_PaletteTree[] = {
    Prog__PaletteGroup(Quick),
    Prog__PaletteGroup(Folder),
    Prog_CommandKind_ToggleCursorSelection, Prog_CommandKind_OpenFocused, Prog_CommandKind_OpenInTab, Prog_CommandKind_OpenInRightSplit,
    Prog_CommandKind_OpenInBottomSplit, Prog_CommandKind_OpenInWindow, Prog_CommandKind_CopySelection, Prog_CommandKind_CutSelection, Prog_CommandKind_Paste,
    Prog_CommandKind_PasteLink,
    Prog_CommandKind_Trash, Prog_CommandKind_SelectionDelete, Prog_CommandKind_SelectionRestore, Prog_CommandKind_TrashRestoreAll,
    Prog_CommandKind_EmptyTrash, Prog_CommandKind_Duplicate, Prog_CommandKind_CompressZip, Prog_CommandKind_Compress7z, Prog_CommandKind_ExtractHere, Prog_CommandKind_ExtractToFolder, Prog_CommandKind_CompressVideo, Prog_CommandKind_DownloadTorrent, Prog_CommandKind_GitPull, Prog_CommandKind_Undo, Prog_CommandKind_Redo, Prog_CommandKind_Rename, Prog_CommandKind_SelectionInspect,
    Prog_CommandKind_SelectionCopyPrevPanel, Prog_CommandKind_SelectionCopyNextPanel, Prog_CommandKind_SelectionMovePrevPanel,
    Prog_CommandKind_SelectionMoveNextPanel, Prog_CommandKind_CopyAsPath, Prog_CommandKind_CopyAsPathSmart, Prog_CommandKind_OpenContainingPath, Prog_CommandKind_OpenContainingPathInTab,
    Prog_CommandKind_OpenRelativePath, Prog_CommandKind_OpenRelativePathInTab,
    Prog_CommandKind_CalcDirsContents, Prog_CommandKind_CreateFile, Prog_CommandKind_CreateFolder, Prog_CommandKind_ToggleFolders, Prog_CommandKind_ToggleFiles,
    Prog_CommandKind_TypesPopupOpen, Prog_CommandKind_FilesPopupOpen, Prog_CommandKind_FilesPopupPrev, Prog_CommandKind_FilesPopupNext,
    Prog_CommandKind_EditPath, Prog_CommandKind_PathMenu, Prog_CommandKind_ToggleCompactPath, Prog_CommandKind_ToggleBreadcrumbs, Prog_CommandKind_StartFilter, Prog_CommandKind_ToggleExpandFolder, Prog_CommandKind_StartExpandedSearch, Prog_CommandKind_ApplyFilter, Prog_CommandKind_RemoveFilter,
    Prog_CommandKind_ToggleFilterPin, Prog_CommandKind_Refresh, Prog_CommandKind_Back, Prog_CommandKind_Forward, Prog_CommandKind_Up,
    Prog_CommandKind_BookmarksToggle,
    Prog__PaletteGroup(View),
    Prog_CommandKind_ZoomIn, Prog_CommandKind_ZoomOut, Prog_CommandKind_ViewDetails, Prog_CommandKind_ViewList, Prog_CommandKind_ViewColumns,
    Prog_CommandKind_ViewMediumIcons, Prog_CommandKind_ViewLargeIcons, Prog_CommandKind_ViewXLIcons, Prog_CommandKind_ToggleMillerColumns, Prog_CommandKind_ToggleTreeView, Prog_CommandKind_GroupByNone,
    Prog_CommandKind_GroupByType, Prog_CommandKind_GroupByDateModified, Prog_CommandKind_GroupBySize, Prog_CommandKind_FolderViewToggle,
    Prog_CommandKind_SizeAllColumnsToFit, Prog_CommandKind_CycleFolderSort,
    Prog__PaletteGroup(Sort),
    Prog_CommandKind_SortByName, Prog_CommandKind_SortBySize, Prog_CommandKind_SortByDateCreated, Prog_CommandKind_SortByDateModified,
    Prog_CommandKind_SortByDateAccessed, Prog_CommandKind_SortByItemCount, Prog_CommandKind_SortByType,
    Prog_CommandKind_PaletteToggleDetails,
    Prog__PaletteGroup(Inspector),
    Prog_CommandKind_TogglePreview, Prog_CommandKind_InspectorToggleDetails, Prog_CommandKind_InspectorChangeLayout, Prog_CommandKind_InspectorIncreaseContentsSize,
    Prog_CommandKind_InspectorDecreaseContentsSize, Prog_CommandKind_InspectorToggleWordWrap, Prog_CommandKind_InspectorToggleLineNumbers,
    Prog_CommandKind_InspectorToggleWhitespace, Prog_CommandKind_InspectorToggleHex, Prog_CommandKind_InspectorPlay, Prog_CommandKind_InspectorMute, Prog_CommandKind_InspectorStartFilter, Prog_CommandKind_InspectorToggleImageGrid,
    Prog_CommandKind_InspectorToggleImageMinimap, Prog_CommandKind_InspectorToggleImageScale, Prog_CommandKind_InspectorScaleImageUp,
    Prog_CommandKind_InspectorScaleImageDown,
    Prog__PaletteGroup(Panels),
    Prog_CommandKind_SplitRight, Prog_CommandKind_SplitBottom, Prog_CommandKind_PanelEvenSiblings, Prog_CommandKind_PanelEvenAll, Prog_CommandKind_TogglePanelZoom, Prog_CommandKind_PanelSelectNext, Prog_CommandKind_PanelSelectPrev, Prog_CommandKind_PanelLeft, Prog_CommandKind_PanelRight, Prog_CommandKind_PanelUp,
    Prog_CommandKind_PanelDown,
    Prog__PaletteGroup(Tabs),
    Prog_CommandKind_TabNew, Prog_CommandKind_TabClose, Prog_CommandKind_TabDuplicate, Prog_CommandKind_TabShiftLeft, Prog_CommandKind_TabShiftRight, Prog_CommandKind_TabCloseOthers, Prog_CommandKind_TabCloseLeftTabs,
    Prog_CommandKind_TabCloseRightTabs, Prog_CommandKind_TabCloseAll, Prog_CommandKind_TabReopen, Prog_CommandKind_TabSelectNext, Prog_CommandKind_TabSelectPrev, Prog_CommandKind_TabSelect1,
    Prog_CommandKind_TabSelect2, Prog_CommandKind_TabSelect3, Prog_CommandKind_TabSelect4, Prog_CommandKind_TabSelect5, Prog_CommandKind_TabSelect6,
    Prog_CommandKind_TabSelect7, Prog_CommandKind_TabSelect8, Prog_CommandKind_TabSelectLast, Prog_CommandKind_TabMenu, Prog_CommandKind_TabList,
    Prog__PaletteGroup(GoTo),
    Prog_CommandKind_GoToSelectInPanel, Prog_CommandKind_GoToBack, Prog_CommandKind_GoToForward, Prog_CommandKind_GoToUp,
    Prog__PaletteGroup(Misc),
    Prog_CommandKind_MainMenu, Prog_CommandKind_ToggleQuickAccess, Prog_CommandKind_StartQuickAccessFilter, Prog_CommandKind_GoTo, Prog_CommandKind_CommandPalette, Prog_CommandKind_QuickCommandsOpen, Prog_CommandKind_ContextMenu,
    Prog_CommandKind_ConsoleOpen, Prog_CommandKind_WindowMinimize, Prog_CommandKind_ToggleFullscreen, Prog_CommandKind_OpenSettings, Prog_CommandKind_LanguageCreate, Prog_CommandKind_BookmarksClear, Prog_CommandKind_FolderViewsClear,
    Prog_CommandKind_RecentsClear, Prog_CommandKind_PlacesShowHidden, Prog_CommandKind_PlacesRestore, Prog_CommandKind_GroupNew, Prog_CommandKind_GroupsRestore, Prog_CommandKind_NoticesUnmuteAll, Prog_CommandKind_NewWindow, Prog_CommandKind_WindowClose,
    Prog_CommandKind_LayoutsSave, Prog_CommandKind_LayoutsLoad, Prog_CommandKind_ExplorerOpen, Prog_CommandKind_SetupOpen, Prog_CommandKind_CopyVersion,
    Prog_CommandKind_CheckForUpdates, Prog_CommandKind_UpdateInstall,
    Prog__PaletteGroup(Options),
    Prog_CommandKind_OpenOptions, Prog_CommandKind_IncreaseFontSize, Prog_CommandKind_DecreaseFontSize, Prog_CommandKind_ResetFontSize, Prog_CommandKind_IncreaseSpacing,
    Prog_CommandKind_DecreaseSpacing, Prog_CommandKind_ToggleFileExtension, Prog_CommandKind_ToggleHidden, Prog_CommandKind_ToggleSystemFiles, Prog_CommandKind_ToggleRecents,
    Prog_CommandKind_ToggleSortRecents,
    Prog__PaletteGroup(Rename),
    Prog_CommandKind_RenameInsertIncrementalID, Prog_CommandKind_RenameInsertCurrentDate, Prog_CommandKind_RenameInsertCreateDate,
    Prog_CommandKind_RenameInsertModifiedDate, Prog_CommandKind_RenamePrev, Prog_CommandKind_RenameNext,
    Prog__PaletteGroup(Text),
    Prog_CommandKind_TextMoveLeft, Prog_CommandKind_TextExtendLeft, Prog_CommandKind_TextMoveRight, Prog_CommandKind_TextExtendRight,
    Prog_CommandKind_TextMoveLeftWord, Prog_CommandKind_TextExtendLeftWord, Prog_CommandKind_TextMoveRightWord, Prog_CommandKind_TextExtendRightWord,
    Prog_CommandKind_TextMoveHome, Prog_CommandKind_TextExtendHome, Prog_CommandKind_TextMoveEnd, Prog_CommandKind_TextExtendEnd, Prog_CommandKind_TextDeleteLeft,
    Prog_CommandKind_TextDeleteLeftWord, Prog_CommandKind_TextDeleteRight, Prog_CommandKind_TextDeleteRightWord, Prog_CommandKind_TextSelectAll,
    Prog_CommandKind_TextCopySelection, Prog_CommandKind_TextCutSelection, Prog_CommandKind_TextPaste, Prog_CommandKind_TextUppercase, Prog_CommandKind_TextLowercase,
    Prog_CommandKind_TextAutocomplete, Prog_CommandKind_TextUndo, Prog_CommandKind_TextRedo, Prog_CommandKind_TextDeleteLeftToSpace, Prog_CommandKind_TextDeleteToStart, Prog_CommandKind_TextDeleteToEnd, Prog_CommandKind_TextSwapCharacters,
    Prog_CommandKind_TextSwapWords, Prog_CommandKind_TextUppercaseWord, Prog_CommandKind_TextLowercaseWord, Prog_CommandKind_TextCapitalizeWord,
    Prog__PaletteGroup(Scroll),
    Prog_CommandKind_ScrollSelectPrev, Prog_CommandKind_ScrollSelectNext, Prog_CommandKind_ScrollSelectLeft, Prog_CommandKind_ScrollSelectRight,
    Prog_CommandKind_ScrollSelectFirst, Prog_CommandKind_ScrollSelectLast, Prog_CommandKind_ScrollScrollSelectUp, Prog_CommandKind_ScrollScrollSelectDown,
    Prog_CommandKind_ScrollCenterCamera, Prog_CommandKind_ScrollMoveCursorPrev, Prog_CommandKind_ScrollMoveCursorNext, Prog_CommandKind_ScrollMoveCursorLeft,
    Prog_CommandKind_ScrollMoveCursorRight, Prog_CommandKind_ScrollExtendSelectPrev, Prog_CommandKind_ScrollExtendSelectNext, Prog_CommandKind_ScrollExtendSelectLeft,
    Prog_CommandKind_ScrollExtendSelectRight, Prog_CommandKind_ScrollExtendSelectFirst, Prog_CommandKind_ScrollExtendSelectLast,
    Prog_CommandKind_ScrollScrollExtendSelectUp, Prog_CommandKind_ScrollScrollExtendSelectDown, Prog_CommandKind_SelectAll,
};
static_assert((u32)Prog_CommandKind_Count + (u32)Prog_CommandGroup_Count <= UINT16_MAX, "commands and groups fit Prog_PaletteTree");

// The palette's rows: with nothing typed, each group's title and the commands of the groups open;
// otherwise the commands the text finds by the score of their label and description, best first, then those only their search words find.
// `out` has room for Prog_MaxPaletteRows; returns how many it holds.
// Whether `kind` would do anything now, which the palette dims the rows of those that would not; commands this does not know of always can.
function b32 Prog_CommandAvailable(const Prog_State *state, Prog_CommandKind kind)
{
    b32 item = !state->loading && (state->selectedCount || (state->hasFocus && state->focusedRow < state->directory.count));
    b32 focused = !state->loading && state->hasFocus && state->focusedRow < state->directory.count;
    u64 order[Prog_MaxPanels];
    u32 panels = Prog__PanelsInOrder(state, 0, order, 0);
    switch (kind) {
        case Prog_CommandKind_UpdateInstall: return state->update.version.length && !state->updateJob && !state->updateReady;
        case Prog_CommandKind_OpenFocused: case Prog_CommandKind_OpenInTab: case Prog_CommandKind_OpenInRightSplit: case Prog_CommandKind_OpenInBottomSplit:
        case Prog_CommandKind_OpenInWindow:
        case Prog_CommandKind_CopySelection: case Prog_CommandKind_CutSelection: case Prog_CommandKind_Trash: case Prog_CommandKind_SelectionDelete:
        case Prog_CommandKind_CalcDirsContents:
            return item;
        // What is in the bin is put back or deleted, not copied beside itself or renamed there.
        case Prog_CommandKind_Duplicate: return item && !P_IsTrashFolder(state->directory.path);
        case Prog_CommandKind_CompressZip: case Prog_CommandKind_Compress7z: return item && !P_IsTrashFolder(state->directory.path) && !state->directory.archive;
        case Prog_CommandKind_CompressVideo: return item && !P_IsTrashFolder(state->directory.path) && !state->directory.archive;
        case Prog_CommandKind_GitPull: return Prog_CanPull(state);
        case Prog_CommandKind_DownloadTorrent: return item && !P_IsTrashFolder(state->directory.path) && !state->directory.archive;
        case Prog_CommandKind_ExtractHere: case Prog_CommandKind_ExtractToFolder:
            return focused && !state->directory.archive && Prog_IsArchiveName(state->directory.entries[state->focusedRow].file.name);
        case Prog_CommandKind_Rename: return focused && !P_IsTrashFolder(state->directory.path);
        case Prog_CommandKind_SelectionRestore: return item && P_IsTrashFolder(state->directory.path);
        case Prog_CommandKind_ToggleCursorSelection: case Prog_CommandKind_OpenContainingPath:
        case Prog_CommandKind_OpenContainingPathInTab:
            return focused;
        case Prog_CommandKind_OpenRelativePath: case Prog_CommandKind_OpenRelativePathInTab: return focused && state->directory.expanded;
        case Prog_CommandKind_TextMoveLeft: case Prog_CommandKind_TextExtendLeft: case Prog_CommandKind_TextMoveRight: case Prog_CommandKind_TextExtendRight:
        case Prog_CommandKind_TextMoveLeftWord: case Prog_CommandKind_TextExtendLeftWord: case Prog_CommandKind_TextMoveRightWord:
        case Prog_CommandKind_TextExtendRightWord: case Prog_CommandKind_TextMoveHome: case Prog_CommandKind_TextExtendHome: case Prog_CommandKind_TextMoveEnd:
        case Prog_CommandKind_TextExtendEnd: case Prog_CommandKind_TextDeleteLeft: case Prog_CommandKind_TextDeleteLeftWord: case Prog_CommandKind_TextDeleteRight:
        case Prog_CommandKind_TextDeleteRightWord: case Prog_CommandKind_TextSelectAll: case Prog_CommandKind_TextCopySelection: case Prog_CommandKind_TextCutSelection:
        case Prog_CommandKind_TextPaste: case Prog_CommandKind_TextUppercase: case Prog_CommandKind_TextLowercase: case Prog_CommandKind_TextAutocomplete:
        case Prog_CommandKind_TextUndo: case Prog_CommandKind_TextRedo: case Prog_CommandKind_TextDeleteLeftToSpace: case Prog_CommandKind_TextDeleteToStart: case Prog_CommandKind_TextDeleteToEnd: case Prog_CommandKind_TextSwapCharacters:
        case Prog_CommandKind_TextSwapWords: case Prog_CommandKind_TextUppercaseWord: case Prog_CommandKind_TextLowercaseWord: case Prog_CommandKind_TextCapitalizeWord:
            return state->renaming || state->filtering || state->inspectorFiltering;
        case Prog_CommandKind_SelectionInspect: return item;
        case Prog_CommandKind_SelectionCopyPrevPanel: case Prog_CommandKind_SelectionCopyNextPanel: case Prog_CommandKind_SelectionMovePrevPanel:
        case Prog_CommandKind_SelectionMoveNextPanel:
            return item && panels > 1;
        case Prog_CommandKind_PanelEvenSiblings: case Prog_CommandKind_PanelEvenAll: return panels > 1;
        case Prog_CommandKind_FilesPopupPrev: case Prog_CommandKind_FilesPopupNext: return state->filesOpen;
        case Prog_CommandKind_Back: return state->historyAt > 0;
        case Prog_CommandKind_Forward: return state->historyAt + 1 < state->history.header.count;
        case Prog_CommandKind_Up: return Prog_UpPath(state->directory.path).length != 0;
        case Prog_CommandKind_Undo: return state->undo.header.count != 0;
        case Prog_CommandKind_Redo: return state->redo.header.count != 0;
        case Prog_CommandKind_ApplyFilter: case Prog_CommandKind_RemoveFilter: return state->filter.length || state->filtering;
        case Prog_CommandKind_RecentsClear: return state->recents.header.count != 0;
        case Prog_CommandKind_NoticesUnmuteAll: return state->mutedNotices.header.count != 0;
        case Prog_CommandKind_BookmarksClear: return state->bookmarks.header.count != 0;
        case Prog_CommandKind_FolderViewsClear: return state->folderViews.header.count != 0;
        case Prog_CommandKind_PanelSelectNext: case Prog_CommandKind_PanelSelectPrev: case Prog_CommandKind_PanelLeft: case Prog_CommandKind_PanelRight:
        case Prog_CommandKind_PanelUp: case Prog_CommandKind_PanelDown:
            return panels > 1;
        case Prog_CommandKind_TogglePanelZoom: return panels > 1 || state->zoomedPanel;
        case Prog_CommandKind_TabCloseOthers: case Prog_CommandKind_TabSelectNext: case Prog_CommandKind_TabSelectPrev: case Prog_CommandKind_TabSelectLast:
            return state->tabCount > 1;
        case Prog_CommandKind_TabCloseLeftTabs: case Prog_CommandKind_TabShiftLeft: return state->activeTab > 0;
        case Prog_CommandKind_TabCloseRightTabs: case Prog_CommandKind_TabShiftRight: return state->activeTab + 1 < state->tabCount;
        case Prog_CommandKind_TabReopen: return state->closedTabs.header.count != 0;
        case Prog_CommandKind_TabSelect1: case Prog_CommandKind_TabSelect2: case Prog_CommandKind_TabSelect3: case Prog_CommandKind_TabSelect4:
        case Prog_CommandKind_TabSelect5: case Prog_CommandKind_TabSelect6: case Prog_CommandKind_TabSelect7: case Prog_CommandKind_TabSelect8:
            return state->tabCount > (u32)(kind - Prog_CommandKind_TabSelect1);
        case Prog_CommandKind_InspectorToggleDetails: case Prog_CommandKind_InspectorChangeLayout: case Prog_CommandKind_InspectorIncreaseContentsSize:
        case Prog_CommandKind_InspectorDecreaseContentsSize: case Prog_CommandKind_InspectorToggleHex:
            return state->previewShown;
        case Prog_CommandKind_InspectorToggleWordWrap: case Prog_CommandKind_InspectorToggleLineNumbers: case Prog_CommandKind_InspectorToggleWhitespace:
        case Prog_CommandKind_InspectorStartFilter:
            return state->previewShown && state->previewText.length;
        case Prog_CommandKind_InspectorToggleImageGrid: case Prog_CommandKind_InspectorToggleImageMinimap: case Prog_CommandKind_InspectorToggleImageScale:
        case Prog_CommandKind_InspectorScaleImageUp: case Prog_CommandKind_InspectorScaleImageDown:
            return state->previewShown && state->previewImage;
        case Prog_CommandKind_RenameInsertIncrementalID: case Prog_CommandKind_RenameInsertCurrentDate: case Prog_CommandKind_RenameInsertCreateDate:
        case Prog_CommandKind_RenameInsertModifiedDate: case Prog_CommandKind_RenamePrev: case Prog_CommandKind_RenameNext:
            return state->renaming;
        default: return true;
    }
}

// The command named `name`, or None.
function Prog_CommandKind Prog__CommandNamed(String name)
{
    for (u32 kind = Prog_CommandKind_None + 1; kind < Prog_CommandKind_Count; ++kind) if (StrIsEqual(Prog_Commands[kind].name, name)) return (Prog_CommandKind)kind;
    return Prog_CommandKind_None;
}

function b32 Prog_IsQuick(const Prog_State *state, Prog_CommandKind kind)
{
    for (u64 i = 0; i < state->quickCommands.header.count; ++i) if (StrIsEqual(state->quickCommands.v[i], Prog_Commands[kind].name)) return true;
    return false;
}

function u32 Prog_PaletteMatches(const Prog_State *state, Prog_PaletteRow *out)
{
    String text = Prog_EditText(&state->palette);
    i32 scores[Prog_MaxPaletteRows];
    u32 count = 0, group = 0;
    // The tree, with the starred commands in Quick, its first group, and out of their own.
    u16 items[CArrayCount(Prog_PaletteTree) + 64];
    u32 itemCount = 0;
    for (u32 i = 0; i < CArrayCount(Prog_PaletteTree); ++i) {
        u32 item = Prog_PaletteTree[i];
        if (item < Prog_CommandKind_Count && Prog_IsQuick(state, (Prog_CommandKind)item)) continue;
        // Quick Commands shows that group alone, open.
        if (state->paletteQuick && item != Prog__PaletteGroup(Quick)) continue;
        items[itemCount++] = (u16)item;
        for (u64 q = 0; item == Prog__PaletteGroup(Quick) && q < state->quickCommands.header.count && q < 64; ++q) {
            Prog_CommandKind kind = Prog__CommandNamed(state->quickCommands.v[q]);
            if (kind && Prog_Commands[kind].label.length) items[itemCount++] = (u16)kind;
        }
    }
    for (u32 i = 0; i < itemCount; ++i) {
        u32 item = items[i];
        if (item >= Prog_CommandKind_Count) {
            group = item - Prog_CommandKind_Count;
            if (!text.length && !state->paletteQuick) out[count++] = (Prog_PaletteRow){ (u8)group, Prog_CommandKind_None };
            continue;
        }
        if (!Prog_CommandApplies((Prog_CommandKind)item)) continue;
        if (!text.length) {
            if (state->paletteQuick || (state->paletteExpanded & (1u << group))) out[count++] = (Prog_PaletteRow){ (u8)group, (u16)item };
            continue;
        }
        // The English label, tooltip and aliases find a command in any language.
        i32 score = Max(Prog_FuzzyScore(Prog_CommandLabel((Prog_CommandKind)item), Prog_CommandTooltip((Prog_CommandKind)item), text, NULL, NULL),
                        Prog_FuzzyScore(Prog_Commands[item].label, Prog_Commands[item].tooltip, text, NULL, NULL));
        if (score <= 0) {
            if (!Prog_FilterMatch(Prog_Commands[item].aliases, text, NULL)) continue;
            score = INT32_MIN;
        }
        u32 at = count++;
        for (; at && scores[at - 1] < score; --at) {
            out[at] = out[at - 1];
            scores[at] = scores[at - 1];
        }
        out[at] = (Prog_PaletteRow){ (u8)group, (u16)item };
        scores[at] = score;
    }
    return count;
}

// The tabs the palette's text finds in its tab mode, best first: folder names holding it as typed, then paths that do, then names holding its letters in order.
// `out` has room for Prog_MaxTabs; returns how many it holds.
// The saved layouts the palette's text finds: those whose names hold it, then those whose names have its letters in order.
function u32 Prog_LayoutMatches(const Prog_State *state, u32 *out)
{
    String text = Prog_EditText(&state->palette);
    u32 count = 0;
    for (u32 tier = 0; tier < 2; ++tier) {
        for (u32 i = 0; i < state->layoutNames.header.count && i < Prog_MaxLayouts; ++i) {
            String name = state->layoutNames.v[i];
            b32 named = Prog__ContainsFolded(name, text), letters = !named && Prog_FilterMatch(name, text, NULL);
            if ((tier == 0 && named) || (tier == 1 && letters)) out[count++] = i;
        }
    }
    return count;
}

// The family an option's font is drawn in without one of the settings' own, as the system matches the default.
function String Prog__FontDefault(const Prog_State *state, Prog_Option option)
{
    const Prog_FontName *used = option == Prog_Option_InspectorFont ? &state->inspectorFontUsed : &state->fontUsed;
    return StrMake(used->text, used->length);
}

// The family an option's font is drawn in: the settings' own, or the default's.
function String Prog__FontChosen(const Prog_State *state, Prog_Option option)
{
    const Prog_FontName *font = option == Prog_Option_InspectorFont ? &state->inspectorFont : &state->font;
    return font->length ? StrMake(font->text, font->length) : Prog__FontDefault(state, option);
}

// The font picker's rows: the families whose names hold the text, then those with its letters in order.
function u32 Prog_FontMatches(const Prog_State *state, u32 *out)
{
    String text = Prog_EditText(&state->palette);
    u32 count = 0;
    for (u32 tier = 0; tier < 2; ++tier) {
        for (u64 i = 0; i < state->fontCount; ++i) {
            String name = state->fontNames[i];
            b32 named = Prog__ContainsFolded(name, text), letters = !named && Prog_FilterMatch(name, text, NULL);
            if ((tier == 0 && named) || (tier == 1 && letters)) out[count++] = (u32)i;
        }
    }
    return count;
}

function u32 Prog_TabMatches(const Prog_State *state, u32 *out)
{
    String text = Prog_EditText(&state->palette);
    u32 count = 0;
    for (u32 tier = 0; tier < 3; ++tier) {
        for (u32 i = 0; i < state->tabCount && i < Prog_MaxTabs; ++i) {
            String path = Prog_TabPath(state, i), name = Prog_PathName(state, path);
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
    typed = P_ExpandVariables(&state->frameArena, typed, true);
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
// The drive index's matches rank after all of the short list's, by their own scores.
enum { Prog_GoToShortRanks = 8192, Prog_GoToRanks = 2 * Prog_GoToShortRanks, Prog_GoToPool = 2048 };

function u16 Prog__GoToRank(i32 score)
{
    return (u16)(Prog_GoToShortRanks - 1 - Min(Max(score, 0), (i32)Prog_GoToShortRanks - 1));
}

typedef struct Prog__GoToFound {
    u32 entry;
    i32 score;
} Prog__GoToFound;

enum { Prog_GoToBlock = 2048 };

// A frame's look through the drive index: the lanes take blocks of its entries in turn until the time is up, so that a block costly to
// look through holds up no other lane, and the blocks taken are the first ones.
typedef struct Prog__GoToLook {
    const Prog_State *state;
    const Prog_Index *index;
    String text;
    u64 first, count, until;
    u32 floor;
    atomic_uint_fast64_t next;  // the next block to take
    Prog__GoToFound *found;     // per block, from its first entry's place on
    u32 *foundCount;            // per block
} Prog__GoToLook;

// Whether `name` holds each of the words of `text`, apart from case.
function b32 Prog__HoldsWords(String name, String text)
{
    for (u64 at = 0; at < text.length;) {
        while (at < text.length && text.value[at] == ' ') ++at;
        u64 end = at;
        while (end < text.length && text.value[end] != ' ') ++end;
        if (end > at && !Prog__ContainsFolded(name, StrFromTo(text, at, (i64)end))) return false;
        at = end;
    }
    return true;
}

// Whether the index's `entry` is inside a hidden folder, a name starting with "." counting as hidden.
function b32 Prog__GoToHidden(const Prog_Index *index, u32 entry)
{
    for (u32 at = entry < index->count ? index->parents[entry] : UINT32_MAX; at < index->count && index->parents[at] != UINT32_MAX; at = index->parents[at]) {
        String name = Prog_IndexName(index, at);
        if ((index->flags[at] & Prog_IndexFlag_Hidden) || (name.length && name.value[0] == '.')) return true;
    }
    return false;
}

// The index's entries whose names hold each word typed whole, so that letters scattered through long names do not crowd the rest out;
// what is inside a hidden folder only while the lists show hidden items.
function void Prog__GoToBlock(Prog__GoToLook *look, u64 block)
{
    const Prog_Index *index = look->index;
    u64 from = look->first + block * Prog_GoToBlock, to = Min(from + Prog_GoToBlock, look->first + look->count);
    Prog__GoToFound *found = look->found + block * Prog_GoToBlock;
    u32 count = 0;
    for (u64 at = from; at < to; ++at) {
        u32 entry = (u32)at;
        String name = Prog_IndexName(index, entry);
        // A root's name is its whole path.
        if (index->parents[entry] == UINT32_MAX) name = Prog__FolderName(name);
        if (!Prog__HoldsWords(name, look->text)) continue;
        if (!look->state->showHidden && Prog__GoToHidden(index, entry)) continue;
        char made[1024];
        i32 score = Prog__GoToScore(look->state, name, Prog__IndexPathTo(made, sizeof(made), index, entry), look->text);
        if (score <= 0 || (u32)Prog_GoToShortRanks + Prog__GoToRank(score) >= look->floor) continue;
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
    // Something typed searches a short list, as one list ranked by score, as the reference's Go To does: the drives, the places, the
    // folders the system names (Prog_SearchPlaces), the recents, files among them, the bookmarks, and Filey's own settings files. Each
    // matches by its name and its shown path, and shows whatever its score, as long as it is above 0; the drive is not searched.
    if (cut < 0 && text.length) {
        u64 recents = state->recents.header.count, bookmarks = state->bookmarks.header.count, places = state->places.header.count;
        u64 searched = state->searchPlaces.header.count, nearby = recents + bookmarks + places + searched, drives = state->drives.header.count;
        Arena *arena = &state->gotoFoundArena;
        // With Alt+E the walk's index once it is done, and before then the last run's, unless there is none.
        const Prog_Index *index = !state->gotoDrives ? NULL : state->indexDone || !state->savedIndex.count ? &state->index : &state->savedIndex;
        String last = StrMake((const char *)state->gotoFoundText, state->gotoFoundTextLength);
        if (!state->gotoPool || !StrIsEqual(last, text) || state->gotoNearby != nearby + drives || state->gotoShowHidden != state->showHidden || state->gotoIndex != index) {
            ArenaClear(arena);
            state->gotoPool = ArenaPushArray(arena, Prog_GoToPool, Prog_GoToItem);
            state->gotoRanks = ArenaPushArray(arena, Prog_GoToPool, u16);
            state->gotoPooled = 0;
            state->gotoFloor = Prog_GoToRanks;
            state->gotoNearby = nearby + drives;
            state->gotoShowHidden = state->showHidden;
            state->gotoIndex = index;
            state->gotoScanAt = 0;
            state->gotoScanFrame = UINT64_MAX;
            state->gotoSeen = (Prog_PathSet){0};
            state->gotoFoundTextLength = (u32)Min(text.length, (u64)sizeof(state->gotoFoundText));
            MemCopy(state->gotoFoundText, text.value, state->gotoFoundTextLength);
            // The drives first, by the names they are shown by, as "Samsung SSD 990 PRO 4TB", which the recents know only their folders' names for.
            for (u64 i = 0; i < drives; ++i) {
                const P_Place *drive = &state->drives.v[i];
                // Or by its UUID, pasted from a disk tool, which names it wholly: scored as its name and its path, so that files named after it, as "uuid-<UUID>.log", do not outrank it.
                i32 score = Max(Prog__GoToScore(state, drive->name, drive->path, text), drive->id.length ? Prog_FuzzyScore(drive->id, drive->id, text, NULL, NULL) : 0);
                if (score <= 0 || MapsGetPtrNull(state->gotoSeen, drive->path)) continue;
                String path = StrCopy(arena, drive->path);
                MapsPut(arena, state->gotoSeen, path, true);
                Prog__GoToPool(state, frame, (Prog_GoToItem){ path, Prog_GoToSection_Found, false, false, StrCopy(arena, drive->name), NULL }, Prog__GoToRank(score));
            }
            // Filey's own settings files, as the reference offers its own.
            String settingsFiles[] = { S("Settings.conf"), S("Session.conf") };
            u64 settingsCount = state->configDirectory.length ? CArrayCount(settingsFiles) : 0;
            for (u64 i = 0; i < nearby + settingsCount; ++i) {
                b32 file = false;
                String path = {0}, name = {0};
                if (i < places) {
                    // This PC goes by its name, as its path is no folder's.
                    path = state->places.v[i].path;
                    if (StrIsEqual(path, Prog_ThisPC)) name = Prog_ThisPC;
                } else if (i < places + searched) {
                    path = state->searchPlaces.v[i - places];
                } else if (i < places + searched + recents) {
                    u64 r = i - places - searched;
                    if (Prog_RecentLeftOut(state, r)) continue;
                    path = state->recents.v[r];
                    file = state->recentRanks[r].file;
                } else if (i < nearby) {
                    path = state->bookmarks.v[i - places - searched - recents];
                } else {
                    path = Prog_JoinPath(frame, state->configDirectory, settingsFiles[i - nearby]);
                    file = true;
                }
                // A copy, as the pool outlives lists written anew under the same count, as the recents are on every visit.
                path = StrCopy(arena, path);
                if (!name.length) name = Prog__FolderName(path);
                i32 score = Prog__GoToScore(state, name, path, text);
                if (score <= 0 || MapsGetPtrNull(state->gotoSeen, path)) continue;
                MapsPut(arena, state->gotoSeen, path, true);
                Prog__GoToPool(state, frame, (Prog_GoToItem){ path, Prog_GoToSection_Found, (b8)file, (b8)(name.length && name.value[0] == '.'), {0}, NULL }, Prog__GoToRank(score));
            }
        }
        // Once a frame, so that every look in a frame sees the same rows; the walk's new entries are reached in turn. Each frame looks for
        // about 4 ms.
        u64 entries = index ? index->count : 0;
        if (index && state->gotoScanFrame != state->ui.frameIndex && state->gotoScanAt < entries) ProfileScope("GoToSearch") {
            state->gotoScanFrame = state->ui.frameIndex;
            Prog__GoToLook look = {
                .state = state, .index = index, .text = text, .first = state->gotoScanAt, .count = Min(entries - state->gotoScanAt, (u64)1 << 20),
                .until = PlatformNowMicroseconds() + 4000, .floor = state->gotoFloor,
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
                    u16 rank = (u16)(Prog_GoToShortRanks + Prog__GoToRank(found.score));
                    if (rank >= state->gotoFloor) continue;
                    char made[1024];
                    String path = Prog__IndexPathTo(made, sizeof(made), index, found.entry);
                    // The short list's are not offered again.
                    if (MapsGetPtrNull(state->gotoSeen, path)) continue;
                    u8 flags = index->flags[found.entry];
                    String name = Prog_IndexName(index, found.entry);
                    b32 hidden = (flags & Prog_IndexFlag_Hidden) || (name.length && name.value[0] == '.');
                    Prog__GoToPool(state, frame, (Prog_GoToItem){ StrCopy(arena, path), Prog_GoToSection_Found, (b8)((flags & Prog_IndexFlag_File) != 0), (b8)hidden, {0}, NULL }, rank);
                }
            }
        }
        state->gotoSearching = index && state->gotoScanAt < entries;
        // A counting sort, which keeps the order within a rank.
        u32 pooled = state->gotoPooled;
        u32 *starts = ArenaPushArray(frame, Prog_GoToRanks + 1, u32);
        for (u32 i = 0; i < pooled; ++i) ++starts[state->gotoRanks[i] + 1];
        for (u32 r = 0; r < Prog_GoToRanks; ++r) starts[r + 1] += starts[r];
        u32 *order = ArenaPushArray(frame, Max(pooled, 1u), u32);
        for (u32 i = 0; i < pooled; ++i) order[starts[state->gotoRanks[i]]++] = i;
        *count = Min(pooled, (u32)Prog_MaxGoToFound);
        Prog_GoToItem *result = ArenaPushArray(frame, Max(*count, 1u), Prog_GoToItem);
        for (u32 i = 0; i < *count; ++i) {
            result[i] = state->gotoPool[order[i]];
            // Only drives are pooled with a name; the pool outlives the drive list, so that the drive is found again for its icon and fill.
            for (u64 d = 0; result[i].name.length && d < drives && !result[i].drive; ++d) {
                if (StrIsEqual(state->drives.v[d].path, result[i].path)) result[i].drive = &state->drives.v[d];
            }
        }
        return result;
    }
    if (cut < 0) {
        // The recents the sidebar shows.
        u64 shownRecents[Prog_ShownRecentCount];
        u64 recents = Prog_ShownRecents(state, shownRecents);
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
                for (u64 n = 0; n < limit; ++n) {
                    u64 at = section == Prog_GoToSection_Recents ? shownRecents[n] : n;
                    String path = list->v[at];
                    b32 named = Prog__ContainsFolded(Prog__FolderName(path), text);
                    if ((tier == 0 && named) || (tier == 1 && !named && Prog__ContainsFolded(path, text))) {
                        String name = Prog__FolderName(path);
                        b8 hidden = section == Prog_GoToSection_Recents ? (b8)Prog_RecentFaded(state, at) : (b8)(name.length && name.value[0] == '.');
                        result[(*count)++] = (Prog_GoToItem){ path, (u8)section, false, hidden, {0}, NULL };
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

// The row Go To highlights once its text changes: the first, but none while the text ends in a separator, as a folder's path pasted or gone
// into does, so that Enter goes to that folder rather than into the first one in it; Down still reaches its rows.
function u32 Prog__GoToFirstRow(const Prog_State *state)
{
    String text = Prog_EditText(&state->palette);
    return text.length && (text.value[text.length - 1] == '/' || text.value[text.length - 1] == '\\') ? UINT32_MAX : 0;
}

// Puts `text` in Go To's box, remembered for its back button when `remember` is set.
// Go To's text with the environment variables it names put in, once one is typed whole, as "%TEMP%", so that its folder lists at once.
function void Prog__GoToExpandVariables(Prog_State *state)
{
    if (state->paletteMode != Prog_PaletteMode_GoTo) return;
    String text = Prog_EditText(&state->palette), expanded = P_ExpandVariables(&state->frameArena, text, false);
    if (expanded.value == text.value || expanded.length >= sizeof(state->palette.text)) return;
    state->palette.anchor = 0;
    state->palette.cursor = state->palette.length;
    Prog__EditReplace(&state->palette, (const u8 *)expanded.value, (u32)expanded.length);
    state->paletteHighlight = Prog__GoToFirstRow(state);
    state->gotoScroll = 0;
}

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
    state->paletteHighlight = Prog__GoToFirstRow(state);
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
    Prog_OpenFile(state, item.path);
}

// Whether a command that picks one of a set or turns something on stands for what the panel shows now, as the view, the sort and the
// grouping, which the view menu and the palette check; false for the rest.
function b32 Prog_CommandChecked(const Prog_State *state, Prog_CommandKind kind)
{
    u32 zoom = state->zoom;
    switch (kind) {
        case Prog_CommandKind_ViewDetails: return zoom == 0;
        case Prog_CommandKind_ViewList: return zoom == Prog_ZoomList;
        case Prog_CommandKind_ViewColumns: return zoom == Prog_ZoomColumns;
        case Prog_CommandKind_ViewMediumIcons: return zoom >= Prog_ZoomIcons && zoom < Prog_ZoomLarge;
        case Prog_CommandKind_ViewLargeIcons: return zoom >= Prog_ZoomLarge && zoom < Prog_ZoomMax;
        case Prog_CommandKind_ViewXLIcons: return zoom >= Prog_ZoomMax;
        case Prog_CommandKind_ToggleMillerColumns: return state->miller;
        case Prog_CommandKind_ToggleTreeView: return state->tree;
        case Prog_CommandKind_GroupByNone: return state->groupBy == Prog_GroupBy_None;
        case Prog_CommandKind_GroupByType: return state->groupBy == Prog_GroupBy_Type;
        case Prog_CommandKind_GroupByDateModified: return state->groupBy == Prog_GroupBy_Modified;
        case Prog_CommandKind_GroupBySize: return state->groupBy == Prog_GroupBy_Size;
        case Prog_CommandKind_SortByName: return state->sort == Prog_Sort_Name;
        case Prog_CommandKind_SortBySize: return state->sort == Prog_Sort_Size;
        case Prog_CommandKind_SortByDateCreated: return state->sort == Prog_Sort_Created;
        case Prog_CommandKind_SortByDateModified: return state->sort == Prog_Sort_Modified;
        case Prog_CommandKind_SortByDateAccessed: return state->sort == Prog_Sort_Accessed;
        case Prog_CommandKind_SortByItemCount: return state->sort == Prog_Sort_Items;
        case Prog_CommandKind_SortByType: return state->sort == Prog_Sort_Type;
        default: return false;
    }
}

function String Prog__CrumbsPath(Prog_State *state, u64 *shownEnd);

// The folder of the crumb a path command carries, the length of its path in the crumbs' path, as a crumb's click does: the trail's while it
// goes on from the shown folder; 0 is This PC, before a drive's name.
function String Prog__CrumbTarget(Prog_State *state, u64 row)
{
    String trail = state->noTrail ? (String){0} : Prog_TrailText(state);
    String path = trail.length ? trail : state->directory.path;
    if (row == 0) return Prog_ThisPC;
    if (row > path.length) return (String){0};
    // A drive prefix needs its slash: "C:" is the drive's current directory, "C:/" its root.
    return StrFromTo(path, 0, (i64)(path.value[row - 1] == ':' ? row + 1 : row));
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
            u16 kinds[] = { Prog_CommandKind_ViewDetails, Prog_CommandKind_ViewList, Prog_CommandKind_ViewColumns, Prog_CommandKind_ViewMediumIcons, Prog_CommandKind_ViewLargeIcons,
                            Prog_CommandKind_ViewXLIcons, Prog_CommandKind_None, Prog_CommandKind_ToggleMillerColumns, Prog_CommandKind_ToggleTreeView, Prog_CommandKind_None,
                            Prog_CommandKind_GroupByNone, Prog_CommandKind_GroupByType, Prog_CommandKind_GroupByDateModified, Prog_CommandKind_GroupBySize, Prog_CommandKind_None };
            for (u32 i = 0; i < CArrayCount(kinds); ++i) Prog__Item(.kind = kinds[i], .checked = Prog_CommandChecked(state, (Prog_CommandKind)kinds[i]));
            Prog__Item(.kind = Prog_CommandKind_ZoomIn);
            Prog__Item(.kind = Prog_CommandKind_ZoomOut);
        } break;
        // The shown tab's: moving it, copies of it, and closing tabs.
        case Prog_CommandKind_TabMenu: {
            Prog__Item(.kind = Prog_CommandKind_TabShiftLeft, .icon = Icon_ChevronsLeft, .disabled = !Prog_CommandAvailable(state, Prog_CommandKind_TabShiftLeft));
            Prog__Item(.kind = Prog_CommandKind_TabShiftRight, .icon = Icon_ChevronsRight, .disabled = !Prog_CommandAvailable(state, Prog_CommandKind_TabShiftRight));
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_TabDuplicate, .icon = Icon_Copy);
            Prog__Item(.kind = Prog_CommandKind_NewWindow, .icon = Icon_AppWindow, .label = Tr("Duplicate Tab To New Window"));
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_TabClose, .icon = Icon_SquareX, .mouse = S("MouseMiddle"));
            u16 closers[] = { Prog_CommandKind_TabCloseOthers, Prog_CommandKind_TabCloseLeftTabs, Prog_CommandKind_TabCloseRightTabs, Prog_CommandKind_TabCloseAll };
            for (u32 i = 0; i < CArrayCount(closers); ++i) Prog__Item(.kind = closers[i], .icon = Icon_SquareX, .disabled = !Prog_CommandAvailable(state, (Prog_CommandKind)closers[i]));
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
            if (P_CanInstall()) Prog__Item(.kind = Prog_CommandKind_SetupOpen, .icon = Icon_HardDriveDownload);
            // Last, a newer version to update to, and the version, which a click copies.
            Prog__Item(.kind = Prog_CommandKind_None);
            if (state->update.version.length) {
                Prog__Item(.kind = Prog_CommandKind_UpdateInstall, .label = StrFormat(scratch.arena, TrF("Update to Filey %S"), state->update.version), .icon = Icon_Download,
                           .disabled = state->updateJob != NULL || state->updateReady);
            }
            Prog__Item(.kind = Prog_CommandKind_CopyVersion, .label = StrFormat(scratch.arena, NoTrF("Filey %s"), FILEY_VERSION), .icon = Icon_Info);
        } break;
        // The panel's views, its splits, its tabs, the app, then what the listing shows.
        case Prog_CommandKind_PanelMenu: {
            Prog__Item(.kind = Prog_CommandKind_ToggleExpandFolder, .icon = Icon_FolderTree, .toggle = true, .checked = state->directory.expanded);
            Prog__Item(.kind = Prog_CommandKind_TogglePreview, .icon = Icon_ScanEye, .toggle = true, .checked = state->previewShown);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_SplitRight, .icon = Icon_UnfoldHorizontal);
            Prog__Item(.kind = Prog_CommandKind_SplitBottom, .icon = Icon_UnfoldVertical);
            Prog__Item(.kind = Prog_CommandKind_TogglePanelZoom, .icon = Icon_Maximize2, .toggle = true, .checked = state->zoomedPanel != 0);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_TabNew, .icon = Icon_SquarePlus);
            Prog__Item(.kind = Prog_CommandKind_TabClose, .icon = Icon_SquareX, .mouse = S("MouseMiddle"));
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_OpenOptions, .icon = Icon_Settings);
            Prog__Item(.kind = Prog_CommandKind_CopyAsPath, .icon = Icon_Copy);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_ToggleHidden, .icon = Icon_Eye, .toggle = true, .checked = state->showHidden);
            Prog__Item(.kind = Prog_CommandKind_ToggleQuickAccess, .icon = Icon_PanelLeft, .toggle = true, .checked = !state->sidebarHidden);
            Prog__Item(.kind = Prog_CommandKind_BookmarksToggle, .icon = Icon_Bookmark, .toggle = true, .checked = Prog_IsBookmarked(state, state->directory.path));
        } break;
        // A sidebar group's: its own commands, then moving it among the groups.
        case Prog_CommandKind_GroupMenu: {
            u32 groups = Prog_SidebarGroupCount(state), group = (u32)Min(command->row, (u64)groups - 1), at = 0;
            for (u32 n = 0; n < groups; ++n) if (Prog_SidebarGroupAt(state, n) == group) at = n;
            if (group >= Prog_Group_Count) {
                Prog__Item(.kind = Prog_CommandKind_GroupRename, .row = group, .label = Tr("Rename Group"), .icon = Icon_Pencil);
                Prog__Item(.kind = Prog_CommandKind_GroupDelete, .row = group, .label = Tr("Delete Group"), .icon = Icon_X);
            }
            if (group == Prog_Group_Places) {
                Prog__Item(.kind = Prog_CommandKind_PlacesShowHidden, .label = Tr("Show Hidden Places"), .icon = Icon_Eye, .toggle = true, .checked = state->placesShowHidden,
                           .disabled = (b8)(!state->placesHidden.header.count && !state->placesShowHidden));
                Prog__Item(.kind = Prog_CommandKind_PlacesRestore, .icon = Icon_RotateCcw, .disabled = (b8)(!state->placesHidden.header.count && !state->placesOrder.header.count));
            }
            if (group == Prog_Group_Bookmarks) Prog__Item(.kind = Prog_CommandKind_BookmarksClear, .icon = Icon_X, .disabled = (b8)!state->bookmarks.header.count);
            if (group == Prog_Group_Recents) Prog__Item(.kind = Prog_CommandKind_RecentsClear, .icon = Icon_X, .disabled = (b8)!state->recents.header.count);
            if (group != Prog_Group_Storage) Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_GroupNew, .label = Tr("New Group"), .icon = Icon_Plus, .disabled = (b8)(state->userGroupCount >= Prog_MaxUserGroups));
            Prog__Item(.kind = Prog_CommandKind_GroupShiftUp, .row = (u64)group, .label = Tr("Move Group Up"), .icon = Icon_ChevronUp, .disabled = (b8)(at == 0));
            Prog__Item(.kind = Prog_CommandKind_GroupShiftDown, .row = (u64)group, .label = Tr("Move Group Down"), .icon = Icon_ChevronDown, .disabled = (b8)(at + 1 >= groups));
            Prog__Item(.kind = Prog_CommandKind_GroupsRestore, .icon = Icon_RotateCcw, .disabled = (b8)!state->groupsOrdered);
        } break;
        // The path bar's, for the crumb right-clicked or the shown folder: opening it elsewhere, its path, and how paths show.
        case Prog_CommandKind_PathMenu: {
            u64 at = command->row;
            String target = Prog__CrumbTarget(state, at);
            b32 folder = target.length && !StrIsEqual(target, Prog_ThisPC);
            Prog__Item(.kind = Prog_CommandKind_NavigatePrefix, .row = at, .label = Tr("Open"), .icon = Icon_Folder, .disabled = (b8)(!target.length || StrIsEqual(target, state->directory.path)));
            Prog__Item(.kind = Prog_CommandKind_PanelHeaderDirInTab, .row = at, .label = Tr("Open in New Tab"), .icon = Icon_SquarePlus, .disabled = (b8)!target.length, .mouse = S("MouseMiddle"));
            Prog__Item(.kind = Prog_CommandKind_PathOpenInWindow, .row = at, .label = Tr("Open in New Window"), .icon = Icon_AppWindow, .disabled = (b8)!target.length);
            Prog__Item(.kind = Prog_CommandKind_PathOpenInRightSplit, .row = at, .label = Tr("Open in Right Split"), .icon = Icon_UnfoldHorizontal, .disabled = (b8)!target.length);
            Prog__Item(.kind = Prog_CommandKind_PathOpenInBottomSplit, .row = at, .label = Tr("Open in Bottom Split"), .icon = Icon_UnfoldVertical, .disabled = (b8)!target.length);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_EditPath, .icon = Icon_Pencil);
            Prog__Item(.kind = Prog_CommandKind_PathCopy, .row = at, .label = Tr("Copy Path"), .icon = Icon_Copy, .disabled = (b8)!folder);
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_ToggleCompactPath, .label = Tr("Compact Path"), .toggle = true, .checked = !state->fullPaths);
            Prog__Item(.kind = Prog_CommandKind_ToggleBreadcrumbs, .label = Tr("Path Breadcrumbs"), .toggle = true, .checked = !state->noTrail);
        } break;
        // The header's, for the column right-clicked: moving it, its width, then which columns there are.
        case Prog_CommandKind_ColumnMenu: {
            Prog_ColumnSet set = Prog_PanelColumns(state);
            u64 at = command->row;
            // Name stays first and cannot be closed, and the last column has nowhere to go right.
            b32 name = at < set.count && set.kinds[at] == Prog_ColumnKind_Name;
            Prog__Item(.kind = Prog_CommandKind_ColumnShiftLeft, .row = at, .label = Tr("Shift Column Left"), .icon = Icon_ChevronsLeft);
            Prog__Item(.kind = Prog_CommandKind_ColumnShiftRight, .row = at, .label = Tr("Shift Column Right"), .icon = Icon_ChevronsRight, .disabled = (b8)(name || at + 1 >= set.count));
            Prog__Item(.kind = Prog_CommandKind_ColumnFit, .row = at, .label = Tr("Size Column To Fit"), .icon = Icon_MoveHorizontal);
            Prog__Item(.kind = Prog_CommandKind_ColumnClose, .row = at, .label = Tr("Close Column"), .icon = Icon_X, .disabled = (b8)name, .mouse = S("MouseMiddle"));
            Prog__Item(.kind = Prog_CommandKind_None);
            Prog__Item(.kind = Prog_CommandKind_SizeAllColumnsToFit, .icon = Icon_MoveHorizontal);
            Prog__Item(.kind = Prog_CommandKind_ColumnsRestore, .label = Tr("Restore Default Columns"), .icon = Icon_RotateCcw);
            Prog__Item(.kind = Prog_CommandKind_None);
            local_persist const String names[Prog_FolderColumnKinds] = {
                TrInit("Name"), TrInit("Type"), TrInit("Size"), TrInit("Total Items"), TrInit("Date Created"), TrInit("Date Modified"), TrInit("Date Accessed"), TrInit("Volume Usage"), TrInit("Folder Usage"), TrInit("Git"),
            };
            for (u32 kind = 0; kind < Prog_FolderColumnKinds; ++kind) {
                b32 shown = false;
                for (u32 i = 0; i < set.count; ++i) shown |= set.kinds[i] == kind;
                Prog__Item(.kind = Prog_CommandKind_ColumnToggle, .row = kind, .label = Prog_Tr(names[kind]), .toggle = true, .checked = shown, .disabled = kind == Prog_ColumnKind_Name);
            }
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

// Puts Filey's own `item`, with `icon` when one is given, and a line after it at the top of the open menu.
function void Prog__MenuPrepend(Prog_State *state, P_MenuItem item, String icon)
{
    // A line parts it from the rest, where there is any.
    u32 shift = state->menu.count ? 2 : 1;
    P_MenuItem *items = ArenaPushArray(&state->menuArena, state->menu.count + shift, P_MenuItem);
    items[0] = item;
    if (icon.length) P_LoadFileIcon(&state->menuArena, icon, P_FileKind_Directory, Max(state->iconSize, 16), state->iconScale, true, &items[0].icon);
    if (shift == 2) items[1] = (P_MenuItem){ .separator = true };
    for (u32 i = 0; i < state->menu.count; ++i) {
        items[i + shift] = state->menu.items[i];
        if (items[i + shift].childCount) items[i + shift].firstChild += shift;
    }
    state->menu.items = items;
    state->menu.count += shift;
    state->menu.rootCount += shift;
}

function String Prog__NameStem(String name);

// Adds the Archive submenu to the items' menu, before Properties: packing them into a zip or 7z named as Add To would, and for one archive,
// taking it out here or into a folder named after it.
function void Prog__MenuAddArchive(Prog_State *state)
{
    if (!state->menuRowCount || state->directory.archive || P_IsTrashFolder(state->directory.path) || !P_ArchiveWritable(S("a.zip"))) return;
    Arena *arena = &state->menuArena;
    const Prog_Entry *first = &state->directory.entries[state->menuRows[0]];
    String stem = state->menuRowCount == 1 ? (first->file.kind == P_FileKind_Directory ? first->file.name : Prog__NameStem(first->file.name)) : Prog_EntryLeaf(state->directory.path);
    b32 archive = state->menuRowCount == 1 && first->file.kind != P_FileKind_Directory && Prog_IsArchiveName(first->file.name);
    P_MenuItem children[4];
    u32 childCount = 0;
    if (archive) {
        children[childCount++] = (P_MenuItem){ .label = Tr("Extract Here"), .action = P_MenuAction_ExtractHere };
        children[childCount++] = (P_MenuItem){ .label = StrFormat(arena, TrF("Extract to \"%S\""), Prog__NameStem(first->file.name)), .action = P_MenuAction_ExtractToFolder };
    }
    children[childCount++] = (P_MenuItem){ .label = StrFormat(arena, TrF("Add to \"%S.zip\""), stem), .action = P_MenuAction_CompressZip };
    children[childCount++] = (P_MenuItem){ .label = StrFormat(arena, TrF("Add to \"%S.7z\""), stem), .action = P_MenuAction_Compress7z };
    // At the end of the top level, but before the system's Properties.
    u32 at = state->menu.rootCount;
    if (at && StrIsEqual(state->menu.items[at - 1].verb, S("properties"))) --at;
    u32 count = state->menu.count + 1 + childCount;
    P_MenuItem *items = ArenaPushArray(arena, count, P_MenuItem);
    for (u32 i = 0, j = 0; i < state->menu.count; ++i, ++j) {
        if (i == at) ++j;
        items[j] = state->menu.items[i];
        if (items[j].childCount) items[j].firstChild += 1;
    }
    items[at] = (P_MenuItem){ .label = Tr("Archive"), .firstChild = state->menu.count + 1, .childCount = childCount };
    for (u32 i = 0; i < childCount; ++i) items[state->menu.count + 1 + i] = children[i];
    state->menu.items = items;
    state->menu.count = count;
    state->menu.rootCount += 1;
}

function void Prog__MenuAddLast(Prog_State *state, P_MenuItem item);

function void Prog__StopPlayer(Prog_State *state)
{
    if (state->playerJob) P_RequestCancel(&state->playerJob->cancelToken);
    state->playerJob = NULL;
}

// Plays the audio file at `path` from `start` milliseconds in, in place of what played.
function void Prog__StartPlayer(Prog_State *state, String path, u64 start)
{
    Prog__StopPlayer(state);
    state->playerSlot ^= 1;
    P_Progress *progress = &state->playerProgress[state->playerSlot];
    *progress = (P_Progress){0};
    atomic_store(&progress->doneBytes, start);
    P_Job *job = P_JobCreate(&state->toolJobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Play;
    input->path = StrCopy(&job->arena, path);
    input->offset = start;
    atomic_store(&state->playerVolume, state->audioMuted ? 0u : state->audioVolume);
    input->volume = &state->playerVolume;
    input->progress = progress;
    job->input = input;
    P_JobSubmit(job);
    state->playerJob = job;
    state->playerHash = MapHashBytes(path.value, path.length);
}

// Compress Video after the Archive submenu, when a video is among the items, and Download Torrent for a .torrent.
function void Prog__MenuAddVideo(Prog_State *state)
{
    if (state->directory.archive || P_IsTrashFolder(state->directory.path)) return;
    b32 video = false, torrent = false;
    for (u32 i = 0; i < state->menuRowCount; ++i) video |= Prog_IsVideo(state->directory.entries[state->menuRows[i]].file.name);
    for (u32 i = 0; i < state->menuRowCount; ++i) torrent |= Prog_IsTorrent(state->directory.entries[state->menuRows[i]].file.name);
    if (video) Prog__MenuAddLast(state, (P_MenuItem){ .label = Prog_CommandLabel(Prog_CommandKind_CompressVideo), .action = P_MenuAction_CompressVideo });
    if (torrent) Prog__MenuAddLast(state, (P_MenuItem){ .label = Prog_CommandLabel(Prog_CommandKind_DownloadTorrent), .action = P_MenuAction_DownloadTorrent });
}

function void Prog__MenuAddLast(Prog_State *state, P_MenuItem item);

// Adding the folders the menu is for to each of the user's groups, when they are all folders.
function void Prog__MenuAddGroups(Prog_State *state, b32 folders)
{
    for (u32 g = 0; folders && g < state->userGroupCount; ++g) {
        Prog__MenuAddLast(state, (P_MenuItem){ .label = StrFormat(&state->menuArena, TrF("Add to %S"), state->userGroups[g].name), .action = P_MenuAction_AddToGroup, .id = g });
    }
}

// `item` at the end of the menu's top level, but before the system's Properties.
function void Prog__MenuAddLast(Prog_State *state, P_MenuItem item)
{
    u32 at = state->menu.rootCount;
    if (at && StrIsEqual(state->menu.items[at - 1].verb, S("properties"))) --at;
    P_MenuItem *items = ArenaPushArray(&state->menuArena, state->menu.count + 1, P_MenuItem);
    for (u32 i = 0, j = 0; i < state->menu.count; ++i, ++j) {
        if (i == at) ++j;
        items[j] = state->menu.items[i];
        if (items[j].childCount) items[j].firstChild += 1;
    }
    items[at] = item;
    state->menu.items = items;
    state->menu.count += 1;
    state->menu.rootCount += 1;
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
    // In the bin its items start with putting them back, and its background with putting everything back, as in Explorer.
    b32 restores = false;
    for (u32 i = 0; i < state->menu.rootCount; ++i) restores |= StrIsEqual(state->menu.items[i].verb, S("undelete"));
    if (P_IsTrashFolder(state->directory.path) && !restores && (state->menuRowCount || state->directory.count)) {
        Prog__MenuPrepend(state, state->menuRowCount ? (P_MenuItem){ .label = Tr("Restore"), .action = P_MenuAction_Restore }
                                                     : (P_MenuItem){ .label = Tr("Restore all items"), .action = P_MenuAction_RestoreAll }, (String){0});
    }
    Prog__MenuAddArchive(state);
    Prog__MenuAddVideo(state);
    b32 folders = state->menuRowCount != 0 && !state->directory.archive && !P_IsTrashFolder(state->directory.path);
    for (u32 i = 0; i < state->menuRowCount; ++i) folders &= state->directory.entries[state->menuRows[i]].file.kind == P_FileKind_Directory;
    Prog__MenuAddGroups(state, folders);
    // A menu left with nothing in it, as an empty bin's background is, does not open.
    if (!state->menu.count) {
        Prog_CloseMenu(state);
        return;
    }
    Prog__ApplyMenuPins(state);
    state->menuAt = mouse ? state->ui.mouse
                  : state->menuRowCount ? (UI_Vec2){ state->focusedRowRect.x0 + 24, state->focusedRowRect.y1 }
                  : state->ui.mouse;
    // It starts on its first item, from the mouse too.
    state->menuHighlight = UINT32_MAX;
    Prog__MenuMove(state, 1);
}

function String Prog_EditText(const Prog_TextEdit *edit)
{
    return StrMake((const char *)edit->text, edit->length);
}

// The next codepoint boundary before (`step` < 0) or after `at`.
// Whether `c` parts a path or a name for Ctrl+W: spaces, slashes, dots and the punctuation a command line parts words with.
function b32 Prog__PathSeparator(u8 c)
{
    return c == ' ' || c == '/' || c == '\\' || c == '.' || c == ':' || c == '@' || c == '=' || c == ',' || c == ';' || c == '|' || c == '{' || c == '}' || c == '\'' || c == '"';
}

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
// Replaces the selection with `bytes`; `typed` joins it to the typing before it as one step of undo.
function void Prog__EditReplaceTyped(Prog_TextEdit *edit, const u8 *bytes, u32 count, b32 typed)
{
    u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
    u32 length = edit->length - (to - from) + count;
    if (length >= sizeof(edit->text) || (from == to && !count)) return;
    if (!typed || !edit->typing || edit->undone) {
        MemCopy(edit->other, edit->text, edit->length);
        edit->otherLength = edit->length;
        edit->otherCursor = edit->cursor;
        edit->undoable = true;
    }
    edit->typing = (b8)typed;
    edit->undone = false;
    MemMove(edit->text + from + count, edit->text + to, edit->length - to);
    MemCopy(edit->text + from, bytes, count);
    edit->length = length;
    edit->cursor = edit->anchor = from + count;
    ++edit->version;
}

function void Prog__EditReplace(Prog_TextEdit *edit, const u8 *bytes, u32 count)
{
    Prog__EditReplaceTyped(edit, bytes, count, false);
}

// Undo swaps in the text before the last change, and Redo swaps it back.
function void Prog__EditSwap(Prog_TextEdit *edit, b32 redo)
{
    if (!edit->undoable || edit->undone != redo) return;
    u8 text[sizeof(edit->text)];
    u32 length = edit->length, cursor = edit->cursor;
    MemCopy(text, edit->text, length);
    MemCopy(edit->text, edit->other, edit->otherLength);
    edit->length = edit->otherLength;
    edit->cursor = edit->anchor = Min(edit->otherCursor, edit->length);
    MemCopy(edit->other, text, length);
    edit->otherLength = length;
    edit->otherCursor = cursor;
    edit->undone = (b8)!redo;
    edit->typing = false;
    ++edit->version;
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
    Prog__EditReplaceTyped(edit, bytes, count, true);
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

// Ctrl moves and deletes by word.
// Runs text command `kind` on `edit`: a caret move, a deletion, the clipboard, case or undo. False for the other commands, Autocomplete among
// them, which is the box's own to do.
function b32 Prog__EditCommand(Prog_TextEdit *edit, Prog_CommandKind kind)
{
    u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
    switch (kind) {
        case Prog_CommandKind_TextMoveLeft: case Prog_CommandKind_TextExtendLeft: case Prog_CommandKind_TextMoveRight: case Prog_CommandKind_TextExtendRight:
        case Prog_CommandKind_TextMoveLeftWord: case Prog_CommandKind_TextExtendLeftWord: case Prog_CommandKind_TextMoveRightWord:
        case Prog_CommandKind_TextExtendRightWord: {
            i32 step = kind == Prog_CommandKind_TextMoveLeft || kind == Prog_CommandKind_TextExtendLeft || kind == Prog_CommandKind_TextMoveLeftWord ||
                       kind == Prog_CommandKind_TextExtendLeftWord ? -1 : 1;
            b32 extend = kind == Prog_CommandKind_TextExtendLeft || kind == Prog_CommandKind_TextExtendRight || kind == Prog_CommandKind_TextExtendLeftWord ||
                         kind == Prog_CommandKind_TextExtendRightWord;
            b32 word = kind >= Prog_CommandKind_TextMoveLeftWord && kind <= Prog_CommandKind_TextExtendRightWord;
            // A plain move from a selection goes to its end on that side.
            if (!extend && !word && from != to) edit->cursor = step < 0 ? from : to;
            else edit->cursor = word ? Prog__EditWordStep(edit, edit->cursor, step) : Prog__EditStep(edit, edit->cursor, step);
            if (!extend) edit->anchor = edit->cursor;
            edit->typing = false;
        } return true;
        case Prog_CommandKind_TextMoveHome: case Prog_CommandKind_TextExtendHome: case Prog_CommandKind_TextMoveEnd: case Prog_CommandKind_TextExtendEnd: {
            edit->cursor = kind == Prog_CommandKind_TextMoveHome || kind == Prog_CommandKind_TextExtendHome ? 0 : edit->length;
            if (kind == Prog_CommandKind_TextMoveHome || kind == Prog_CommandKind_TextMoveEnd) edit->anchor = edit->cursor;
            edit->typing = false;
        } return true;
        case Prog_CommandKind_TextDeleteLeft: case Prog_CommandKind_TextDeleteLeftWord: case Prog_CommandKind_TextDeleteRight:
        case Prog_CommandKind_TextDeleteRightWord: {
            i32 step = kind == Prog_CommandKind_TextDeleteLeft || kind == Prog_CommandKind_TextDeleteLeftWord ? -1 : 1;
            b32 word = kind == Prog_CommandKind_TextDeleteLeftWord || kind == Prog_CommandKind_TextDeleteRightWord;
            if (from == to) edit->anchor = word ? Prog__EditWordStep(edit, edit->cursor, step) : Prog__EditStep(edit, edit->cursor, step);
            Prog__EditReplace(edit, NULL, 0);
        } return true;
        case Prog_CommandKind_TextSelectAll: {
            edit->anchor = 0;
            edit->cursor = edit->length;
        } return true;
        case Prog_CommandKind_TextCopySelection: case Prog_CommandKind_TextCutSelection: {
            if (from != to) {
                PlatformClipboardSetText(StrMake((const char *)edit->text + from, to - from));
                if (kind == Prog_CommandKind_TextCutSelection) Prog__EditReplace(edit, NULL, 0);
            }
        } return true;
        case Prog_CommandKind_TextPaste: Prog__EditPaste(edit); return true;
        // ponytail: ASCII letters only; fold the rest too once names in other scripts need it.
        case Prog_CommandKind_TextUppercase: case Prog_CommandKind_TextLowercase: {
            if (from == to) return true;
            u8 bytes[sizeof(edit->text)];
            for (u32 i = from; i < to; ++i) {
                u8 c = edit->text[i];
                if (kind == Prog_CommandKind_TextUppercase && c >= 'a' && c <= 'z') c = (u8)(c - 'a' + 'A');
                if (kind == Prog_CommandKind_TextLowercase && c >= 'A' && c <= 'Z') c = (u8)(c - 'A' + 'a');
                bytes[i - from] = c;
            }
            if (!MemCompare(bytes, edit->text + from, to - from)) return true;
            b32 forward = edit->cursor >= edit->anchor;
            Prog__EditReplace(edit, bytes, to - from);
            edit->anchor = forward ? from : to;
            edit->cursor = forward ? to : from;
        } return true;
        case Prog_CommandKind_TextUndo: case Prog_CommandKind_TextRedo: Prog__EditSwap(edit, kind == Prog_CommandKind_TextRedo); return true;
        // The terminal's: back over spaces and then to the space before, as Ctrl+W does there, and to either end of the line.
        case Prog_CommandKind_TextDeleteLeftToSpace: case Prog_CommandKind_TextDeleteToStart: case Prog_CommandKind_TextDeleteToEnd: {
            if (kind == Prog_CommandKind_TextDeleteToStart) edit->anchor = 0;
            else if (kind == Prog_CommandKind_TextDeleteToEnd) edit->anchor = edit->length;
            else if (from == to) {
                // Back over the separators before the caret, then to the one before them, as fish's Ctrl+W takes a path's last part, or
                // the "png" off a file name.
                u32 at = edit->cursor;
                while (at && Prog__PathSeparator(edit->text[at - 1])) --at;
                while (at && !Prog__PathSeparator(edit->text[at - 1])) --at;
                edit->anchor = at;
            }
            if (edit->anchor != edit->cursor) Prog__EditReplace(edit, NULL, 0);
        } return true;
        // The character before the caret with the one after it, or the last two at the end, and the caret on past them.
        case Prog_CommandKind_TextSwapCharacters: {
            u32 at = edit->cursor == edit->length ? Prog__EditStep(edit, edit->cursor, -1) : edit->cursor;
            u32 before = Prog__EditStep(edit, at, -1), after = Prog__EditStep(edit, at, 1);
            if (before == at || after == at) return true;
            u8 bytes[8];
            MemCopy(bytes, edit->text + at, after - at);
            MemCopy(bytes + (after - at), edit->text + before, at - before);
            edit->anchor = before;
            edit->cursor = after;
            Prog__EditReplace(edit, bytes, after - before);
        } return true;
        // The word before the caret with the one at or after it, or the last two at the end, and the caret after them.
        case Prog_CommandKind_TextSwapWords: {
            u32 secondEnd = Prog__EditWordStep(edit, edit->cursor, 1), second = Prog__EditWordStep(edit, secondEnd, -1);
            u32 first = Prog__EditWordStep(edit, second, -1), firstEnd = first;
            while (firstEnd < second && Prog__IsWordByte(edit->text[firstEnd])) ++firstEnd;
            if (first == second || firstEnd == first || secondEnd == second) return true;
            u8 bytes[sizeof(edit->text)];
            u32 count = 0;
            MemCopy(bytes + count, edit->text + second, secondEnd - second), count += secondEnd - second;
            MemCopy(bytes + count, edit->text + firstEnd, second - firstEnd), count += second - firstEnd;
            MemCopy(bytes + count, edit->text + first, firstEnd - first), count += firstEnd - first;
            edit->anchor = first;
            edit->cursor = secondEnd;
            Prog__EditReplace(edit, bytes, count);
        } return true;
        // From the caret to the end of its word, or of the next one, the caret going there.
        case Prog_CommandKind_TextUppercaseWord: case Prog_CommandKind_TextLowercaseWord: case Prog_CommandKind_TextCapitalizeWord: {
            u32 at = edit->cursor, end = Prog__EditWordStep(edit, at, 1);
            u8 bytes[sizeof(edit->text)];
            b32 first = true;
            for (u32 i = at; i < end; ++i) {
                u8 c = edit->text[i];
                if (Prog__IsWordByte(c)) {
                    b32 upper = kind == Prog_CommandKind_TextUppercaseWord || (kind == Prog_CommandKind_TextCapitalizeWord && first);
                    if (upper && c >= 'a' && c <= 'z') c = (u8)(c - 'a' + 'A');
                    if (!upper && c >= 'A' && c <= 'Z') c = (u8)(c - 'A' + 'a');
                    first = false;
                }
                bytes[i - at] = c;
            }
            edit->anchor = at;
            edit->cursor = end;
            if (MemCompare(bytes, edit->text + at, end - at)) Prog__EditReplace(edit, bytes, end - at);
            else edit->anchor = end;
        } return true;
        default: return false;
    }
}

// Runs the text box hotkey `event` presses on `edit`; false for keys the box does not use, which go on to the hotkeys of where it is. A box
// over the list, as the filter is, leaves it the keys that delete to either end, which page and move its cursor there.
function b32 Prog__EditKeyOver(const Prog_State *state, Prog_TextEdit *edit, const PlatformEvent *event, b32 overList)
{
    const Prog_Hotkey *hotkey = Prog_FindHotkey(state, Prog_HotkeyScope_Text, Key_None, event->key, event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super));
    if (hotkey && overList && (hotkey->command == Prog_CommandKind_TextDeleteToStart || hotkey->command == Prog_CommandKind_TextDeleteToEnd)) return false;
    return hotkey && Prog__EditCommand(edit, (Prog_CommandKind)hotkey->command);
}

function b32 Prog__EditKey(const Prog_State *state, Prog_TextEdit *edit, const PlatformEvent *event)
{
    return Prog__EditKeyOver(state, edit, event, false);
}

// The list's cursor key `event` is, if any: -1 for the previous item, 1 for the next.
function i32 Prog__ListCursorKey(const Prog_State *state, const PlatformEvent *event)
{
    const Prog_Hotkey *hotkey = Prog_FindHotkey(state, Prog_HotkeyScope_Browser, Key_None, event->key, event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super));
    return !hotkey || !(event->mods & KeyMod_Control) ? 0 : hotkey->command == Prog_CommandKind_ScrollMoveCursorPrev ? -1 : hotkey->command == Prog_CommandKind_ScrollMoveCursorNext ? 1 : 0;
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

// Puts at the rename box's caret the date asked for, or the smallest number from 1 up that makes a name no other item in the folder has.
function void Prog__RenameInsert(Prog_State *state, Prog_CommandKind kind)
{
    if (!state->renaming) return;
    Arena *frame = &state->frameArena;
    String renamed = StrMake((const char *)state->renameFrom, state->renameFromLength);
    const Prog_Entry *entry = NULL;
    for (u64 i = 0; i < state->directory.count && !entry; ++i) {
        if (StrIsEqual(state->directory.entries[i].file.name, renamed)) entry = &state->directory.entries[i];
    }
    String text = {0};
    if (kind == Prog_CommandKind_RenameInsertCurrentDate) text = P_FormatFileDate(frame, P_NowUnix());
    else if (kind == Prog_CommandKind_RenameInsertCreateDate && entry && entry->file.createdTimeUnix) text = P_FormatFileDate(frame, entry->file.createdTimeUnix);
    else if (kind == Prog_CommandKind_RenameInsertModifiedDate && entry) text = P_FormatFileDate(frame, entry->file.modifiedTimeUnix);
    else if (kind == Prog_CommandKind_RenameInsertIncrementalID) {
        Prog_TextEdit *edit = &state->rename;
        u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
        String before = StrMake((const char *)edit->text, from), after = StrMake((const char *)edit->text + to, edit->length - to);
        String prefix = Prog_EntryParent(renamed);
        for (u32 id = 1; !text.length; ++id) {
            String number = StrFormat(frame, "%u", id);
            String candidate = StrFormat(frame, "%S%S%S%S%S", prefix, prefix.length ? S("/") : S(""), before, number, after);
            b32 taken = false;
            for (u64 i = 0; i < state->directory.count && !taken; ++i) {
                String name = state->directory.entries[i].file.name;
                taken = name.length == candidate.length && Prog__EqualFolded(name, candidate) && !StrIsEqual(name, renamed);
            }
            if (!taken) text = number;
        }
    }
    if (text.length) Prog__EditReplace(&state->rename, (const u8 *)text.value, (u32)text.length);
}

function void Prog__FileCommand(Prog_State *state, Prog_CommandKind kind);
function void Prog__FileJob(Prog_State *state, Prog_JobKind kind, const String *paths, u64 count, String directory, b32 flag, String name);

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
        if (item.action == P_MenuAction_EmptyTrash) Prog_QueueCommand(state, Prog_CommandKind_EmptyTrash, 0, 0, UINT64_MAX);
        else if (item.action == P_MenuAction_CopyAsPath) PlatformClipboardSetText(P_NativePath(&state->frameArena, target));
        else if (item.action == P_MenuAction_AddToGroup) {
            ArrayStr folders = {0};
            ArrayPush(&state->frameArena, folders, target);
            Prog_UserGroupAdd(state, item.id, &folders, UINT64_MAX);
        }
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
    if (item.action == P_MenuAction_Restore || item.action == P_MenuAction_RestoreAll) {
        Prog_CloseMenu(state);
        Prog_QueueCommand(state, item.action == P_MenuAction_Restore ? Prog_CommandKind_SelectionRestore : Prog_CommandKind_TrashRestoreAll, 0, 0, UINT64_MAX);
        return;
    }
    if (item.action == P_MenuAction_AddToGroup) {
        ArrayStr folders = {0};
        for (u32 i = 0; i < state->menuRowCount; ++i) ArrayPush(&state->frameArena, folders, Prog_JoinPath(&state->frameArena, state->directory.path, state->directory.entries[state->menuRows[i]].file.name));
        Prog_CloseMenu(state);
        Prog_UserGroupAdd(state, item.id, &folders, UINT64_MAX);
        return;
    }
    // Items Filey implements itself, which are all of them on Linux.
    if (item.action) {
        static_assert(P_MenuAction_Count == 20);
        local_persist const Prog_CommandKind actions[P_MenuAction_Count] = {
            [P_MenuAction_CompressZip] = Prog_CommandKind_CompressZip,
            [P_MenuAction_Compress7z] = Prog_CommandKind_Compress7z,
            [P_MenuAction_CompressVideo] = Prog_CommandKind_CompressVideo,
            [P_MenuAction_DownloadTorrent] = Prog_CommandKind_DownloadTorrent,
            [P_MenuAction_ExtractHere] = Prog_CommandKind_ExtractHere,
            [P_MenuAction_ExtractToFolder] = Prog_CommandKind_ExtractToFolder,
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
        // The one item a command creates, such as an archiver's Add to, is selected; the New submenu's, such as New > Folder, are renamed too,
        // as Explorer does.
        u32 owner = Prog_MenuOwner(state, index);
        b32 creates = !StrIsEqual(item.verb, S("paste")) && !StrIsEqual(item.verb, S("openwith"));
        state->renameCreatedUntil = creates ? PlatformNowMicroseconds() + Millions(5) : 0;
        state->createdRenames = owner != UINT32_MAX && StrIsEqual(state->menu.items[owner].verb, S("new"));
        P_FileError error = P_ContextMenuInvoke(&state->frameArena, &state->menu, item.id);
        ArenaClear(&state->errorArena);
        if (error.code) Prog_Notify(state, Prog_NoticeKind_Error, S("menu.failed"), StrFormat(&state->frameArena, TrF("%S failed"), item.label), error.message);
        // The watch would catch the change too; reloading now shows it without waiting.
        Prog_Reload(state);
        // The bin has no watch, and the shell moves its items on after the command returns, so a job waits for them to leave it.
        if (!error.code && state->menuRowCount && P_IsTrashFolder(state->directory.path)) {
            String *paths = ArenaPushArray(&state->frameArena, state->menuRowCount, String);
            for (u64 i = 0; i < state->menuRowCount; ++i) paths[i] = Prog_JoinPath(&state->frameArena, state->directory.path, state->directory.entries[state->menuRows[i]].file.name);
            Prog__FileJob(state, Prog_JobKind_BinWait, paths, state->menuRowCount, state->directory.path, false, (String){0});
        }
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

function void Prog__SubmitPreview(Prog_State *state, String path, b32 folder, b32 archive, u64 size)
{
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Preview;
    input->panel = Prog__PanelId(state);
    input->path = StrCopy(&job->arena, path);
    input->folder = folder;
    input->archive = archive;
    input->showHidden = state->showHidden;
    input->hex = state->inspectorHex;
    input->size = size;
    // A file inside an archive is taken out first, which the inspector shows the progress of.
    String within = {0}, inside = {0};
    if (!folder && !archive && Prog_ArchiveSplit(path, &within, &inside) && inside.length) input->progress = ArenaPushStruct(&job->arena, P_Progress);
    state->hexSize = size;
    state->previewSubmitted = PlatformNowMicroseconds();
    job->input = input;
    state->previewJob = job;
    P_JobSubmit(job);
}

// Starts loading the preview of the current panel's focused item when it is another than the one shown; a folder is listed in the inspector's panel.
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
    b32 folder = false, archive = false;
    u64 size = 0, stamp = 0;
    b32 online = false;
    if (!state->loading && state->hasFocus && state->focusedRow < state->directory.count) {
        const Prog_Entry *entry = &state->directory.entries[state->focusedRow];
        want = Prog_JoinPath(&state->frameArena, state->directory.path, entry->file.name);
        online = entry->file.online;
        folder = entry->file.kind == P_FileKind_Directory;
        archive = Prog_PreviewsArchive(state, entry);
        size = entry->file.size;
        stamp = archive ? entry->file.size ^ (u64)entry->file.modifiedTimeUnix : 0;
    }
    if (StrIsEqual(want, state->previewPath)) {
        // An archive that changed, as when files are dropped into it, is counted again; its list stays where it is and its old totals show
        // until the new ones come.
        if (!archive || stamp == state->previewStamp) return;
        state->previewStamp = stamp;
        if (state->previewJob) P_RequestCancel(&state->previewJob->cancelToken);
        Prog__SubmitPreview(state, want, false, true, size);
        return;
    }
    state->previewStamp = stamp;
    if (state->previewJob) P_RequestCancel(&state->previewJob->cancelToken);
    state->previewJob = NULL;
    Prog__DropModel(&state->model);
    ArenaClear(&state->previewArena);
    state->previewPath = StrCopy(&state->previewArena, want);
    state->previewText = state->previewMedia = (String){0};
    state->previewSeconds = 0;
    state->torrent = (Prog_TorrentInfo){0};
    state->torrentSkip = NULL;
    state->torrentDone = NULL;
    state->torrentRead = false;
    // What played stops once another item is shown.
    if (state->playerJob && state->playerHash != MapHashBytes(want.value, want.length)) Prog__StopPlayer(state);
    state->previewAnchor = state->previewCaret = 0;
    state->previewTextFocus = false;
    state->previewPicture = state->previewImage = false;
    // A new model starts from the view the fixed previews had: turned 30 degrees and tipped 20.
    state->model.yaw = 0.5235988f, state->model.pitch = 0.3490659f, state->model.zoom = 1;
    state->previewFolder = folder || archive;
    state->previewHex = state->previewArchive = state->previewArchiveCounted = state->previewDocument = false;
    state->hexPages = NULL;
    state->hexPageCount = 0;
    state->hexSize = 0;
    if (state->hexPageJob) P_RequestCancel(&state->hexPageJob->cancelToken);
    state->hexPageJob = NULL;
    Prog__DropPdf(state);
    state->imagePan = (UI_Vec2){0};
    state->previewLines = state->previewRows = state->previewKept = NULL;
    state->previewLineStates = NULL;
    state->previewSyntax = Prog_Syntax_None;
    state->previewLineCount = state->previewRowColumns = 0;
    state->inspectorResetScroll = true;
    if (!want.length) return;
    // A file only in the cloud is not read, which would download it; opening it does.
    if (online && !folder) {
        state->previewText = StrCopy(&state->previewArena, Tr("This file is only in the cloud. Open it to download it, and preview it after."));
        return;
    }
    if (folder || archive) Prog_ListInInspector(state, want);
    Prog__SubmitPreview(state, want, folder, archive, size);
}

// Zooms the inspector's image by the ratio a wheel notch does, in or out from the scale it shows, down to no smaller than fitting the view.
// The pan keeps the same middle in sight.
function void Prog__ScaleImage(Prog_State *state, b32 up)
{
    f32 fit = state->imageFit > 0 ? state->imageFit : 1, least = MinF32(fit, 1);
    f32 shown = state->imageScaleMode == Prog_ImageScale_Custom ? state->imageScale : state->imageScaleMode == Prog_ImageScale_Original ? 1 : least;
    f32 scale = MinF32(MaxF32(up ? shown * 1.2f : shown / 1.2f, least), 32.0f);
    state->imagePan.x *= scale / shown, state->imagePan.y *= scale / shown;
    state->imageScale = scale;
    state->imageScaleMode = Prog_ImageScale_Custom;
}

// The transfer slot the job took, NULL without one.
// One that runs a program for minutes or hours, a download or a compression, needs a slot, whose card can pause and stop it, and is not started without one.
function Prog_Transfer *Prog__FileJobOp(Prog_State *state, Prog_JobKind kind, const String *paths, u64 count, String directory, b32 flag, String name, Prog_TransferOp op)
{
    if (!directory.length || (kind != Prog_JobKind_Create && kind != Prog_JobKind_Restore && !count)) return NULL;
    b32 tool = op == Prog_TransferOp_CompressVideo || op == Prog_TransferOp_Download || op == Prog_TransferOp_Torrent || op == Prog_TransferOp_TorrentFetch;
    Prog_Transfer *transfer = NULL;
    // A torrent's download takes one of the torrents' own slots, and the rest the others.
    u32 first = op == Prog_TransferOp_Torrent ? Prog_MaxTransfers : 0, end = op == Prog_TransferOp_Torrent ? Prog_TransferSlots : Prog_MaxTransfers;
    for (u32 i = first; kind == Prog_JobKind_Transfer && i < end && !transfer; ++i) if (!state->transfers[i].job) transfer = &state->transfers[i];
    if (tool && !transfer) {
        Prog_Notify(state, Prog_NoticeKind_Warning, S("transfer.busy"), Tr("Too many transfers at once"), Tr("Wait for one of them to finish, or stop one."));
        return NULL;
    }
    P_Job *job = P_JobCreate(tool ? &state->toolJobs : &state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = (u8)kind;
    input->transferOp = (u8)op;
    input->archiveLevel = state->archiveLevel;
    input->videoCodec = state->videoCodec;
    input->videoQuality = state->videoQuality;
    input->downloadAudio = state->downloadAudio;
    input->path = StrCopy(&job->arena, directory);
    input->paths = ArenaPushArray(&job->arena, Max(count, (u64)1), String);
    for (u64 i = 0; i < count; ++i) input->paths[i] = StrCopy(&job->arena, paths[i]);
    input->pathCount = count;
    input->move = kind == Prog_JobKind_Transfer && flag;
    input->folder = kind == Prog_JobKind_Create && flag;
    input->confirm = (kind == Prog_JobKind_EmptyTrash || kind == Prog_JobKind_Delete) && flag;
    input->name = StrCopy(&job->arena, name);
    // A copy or move takes a free slot for its progress and questions; without one, taken names get numbers.
    if (transfer) {
        String target = Prog_EntryLeaf(directory);
        *transfer = (Prog_Transfer){ .job = job, .move = input->move, .op = (u8)op, .started = PlatformNowMicroseconds(),
                                     .targetLength = (u32)Min(target.length, sizeof(transfer->target)), .targetHash = MapHashBytes(directory.value, directory.length) };
        MemCopy(transfer->target, target.value, transfer->targetLength);
        atomic_store(&transfer->progress.policy, P_Conflict_Ask);
        input->progress = &transfer->progress;
    }
    job->input = input;
    P_JobSubmit(job);
    return transfer;
}

function void Prog__FileJob(Prog_State *state, Prog_JobKind kind, const String *paths, u64 count, String directory, b32 flag, String name)
{
    Prog__FileJobOp(state, kind, paths, count, directory, flag, name, Prog_TransferOp_None);
}

// `name` without its extension, and an archive's both of .tar.gz and the like.
function String Prog__NameStem(String name)
{
    Index dot = StrIndexChar(name, '.', SSF_SearchBackwards);
    if (!dot.exists || !dot.value) return name;
    String stem = StrFromTo(name, 0, (i64)dot.value);
    if (stem.length > 4 && Prog__EqualFolded(StrFrom(stem, stem.length - 4), S(".tar"))) stem.length -= 4;
    return stem;
}

// `stem` and `extension` as a name nothing in `directory` has yet: as it is, then with " (2)", " (3)" and so on.
function String Prog__FreeName(Arena *arena, String directory, String stem, String extension)
{
    String name = StrFormat(arena, "%S%S", stem, extension);
    for (u32 n = 2; n < 1000 && P_PathExists(Prog_JoinPath(arena, directory, name)); ++n) name = StrFormat(arena, "%S (%u)%S", stem, n, extension);
    return name;
}

// Filey's own file commands; Windows context menus run the shell's versions instead.
// A press on row `row` of Miller column `slot` goes to that column's folder with the item selected, as a press on it there would; a double
// press opens the item.
function void Prog__MillerSelect(Prog_State *state, u32 slot, u64 row, b32 open)
{
    if (!state->millerColumns || slot >= Prog_MillerSlots || row >= state->millerColumns->v[slot].count || state->renaming) return;
    const Prog_MillerColumn *column = &state->millerColumns->v[slot];
    const Prog_Entry *entry = &column->items[row];
    String path = Prog_JoinPath(&state->frameArena, column->path, entry->file.name), name = StrCopy(&state->frameArena, entry->file.name);
    String folder = StrCopy(&state->frameArena, column->path), archive = {0}, inner = {0};
    b32 opensAsFolder = entry->file.kind == P_FileKind_Directory || (Prog_IsArchiveName(name) && !Prog_ArchiveSplit(folder, &archive, &inner) && !P_OpensInApp(path));
    if (open && opensAsFolder) {
        Prog_RequestDirectory(state, path, true, 0);
    } else if (open) {
        Prog__AddRecent(state, path, true, entry->file.hidden);
        Prog_OpenFile(state, path);
    } else if (Prog_SelectNext(state, name)) {
        Prog_RequestDirectory(state, folder, true, 0);
    }
}

// `text` without the spaces around it.
function String Prog__TrimSpaces(String text)
{
    while (text.length && text.value[0] == ' ') text = StrFrom(text, 1);
    while (text.length && text.value[text.length - 1] == ' ') --text.length;
    return text;
}

// The font dropdown for `option`, over Options, on the family it has; the families are listed in a job the first time.
function void Prog__OpenFontPicker(Prog_State *state, Prog_Option option)
{
    Prog_CloseMenu(state);
    if (state->paletteMode != Prog_PaletteMode_Fonts) {
        state->optionsPalette = state->palette;
        state->optionsHighlight = state->paletteHighlight;
        state->optionsScroll = state->paletteScroll;
        state->optionsWheeled = state->paletteWheeled;
        state->fontAnchor = (R_Rect){0};
    }
    state->paletteOpen = true;
    state->paletteMode = Prog_PaletteMode_Fonts;
    state->palette = (Prog_TextEdit){0};
    state->fontOption = (u8)option;
    state->paletteHighlight = 0;
    state->paletteWheeled = false;
    String current = Prog__FontChosen(state, option);
    for (u64 i = 0; i < state->fontCount; ++i) {
        if (Prog__EqualFolded(state->fontNames[i], current)) state->paletteHighlight = (u32)i;
    }
    if (state->fontNames || state->fontsJob) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Fonts;
    job->input = input;
    state->fontsJob = job;
    P_JobSubmit(job);
}

// Back to Options from the font dropdown, as they were.
function void Prog__CloseFontPicker(Prog_State *state)
{
    state->paletteMode = Prog_PaletteMode_Options;
    state->palette = state->optionsPalette;
    state->paletteHighlight = state->optionsHighlight;
    state->paletteScroll = state->optionsScroll;
    state->paletteWheeled = state->optionsWheeled;
}

// The color picker for the color of `option`, which only a custom scheme has.
function void Prog__OpenColorPicker(Prog_State *state, Prog_Option option)
{
    if (Prog_ShownScheme(state, NULL) < Prog_BuiltInSchemes || option < Prog_Option_ColorClear || option > Prog_Option_ColorMatchDisabledSelection) return;
    state->colorPicking = true;
    state->colorOption = (u8)option;
    state->pickerOriginal = state->schemeGroups[option - Prog_Option_ColorClear];
    state->pickerAt = UI ? UI->mouse : (UI_Vec2){0};
    state->pickerField = 0;
    state->pickerSquareHue = -1;
    R_Color color = Prog_OptionColor(state, option);
    Prog_RgbToHsb(color, &state->pickerHue, &state->pickerSaturation, &state->pickerBrightness);
}

// Reads the install for the Filey Setup page, whose switches then show it; a first install starts with the PATH on.
function void Prog__ReadSetup(Prog_State *state)
{
    ArenaClear(&state->setupArena);
    state->setup = P_InstallState(&state->setupArena);
    state->setupDesktop = state->setup.installed && state->setup.desktopShortcut;
    state->setupOnPath = !state->setup.installed || state->setup.onPath;
    state->setupDefault = state->setup.installed && state->setup.setDefault;
    state->setupMenu = state->setup.installed && state->setup.contextMenu;
}

// The Filey Setup page, in place of the panels, with the logo drawn from the executable's own icon at the page's size.
function void Prog__OpenSetup(Prog_State *state)
{
    if (!P_CanInstall()) return;
    Prog_CloseMenu(state);
    state->paletteOpen = false;
    state->setupOpen = true;
    Prog__ReadSetup(state);
    i32 size = (i32)floorf(128.0f * (state->iconScale > 0 ? state->iconScale : 1.0f) + 0.5f);
    if (state->setupLogoSize != size) {
        R_TextureRelease(state->setupLogo);
        state->setupLogo = (R_TextureHandle){0};
        state->setupLogoSize = 0;
        Scratch scratch = GetScratch();
        P_Image image = {0};
        if (P_LoadAppIcon(scratch.arena, size, &image)) {
            state->setupLogo = R_TextureCreateRGBA(image.width, image.height, image.pixels);
            state->setupLogoSize = size;
        }
        ReleaseScratch(scratch);
    }
}

// The option highlighted in Options, Prog_Option_Count for none or in another list.
function u32 Prog__HighlightedOption(Prog_State *state)
{
    if (state->paletteMode != Prog_PaletteMode_Options) return Prog_Option_Count;
    u8 found[Prog_Option_Count];
    u32 count = Prog_OptionMatches(state, found);
    return state->paletteHighlight < count ? found[state->paletteHighlight] : Prog_Option_Count;
}

// After the typed text changes, the first row is highlighted, but Options keeps the option `kept` it had where that still shows, and Go To
// may have none.
function void Prog__KeepHighlight(Prog_State *state, u32 kept)
{
    state->paletteHighlight = state->paletteMode == Prog_PaletteMode_GoTo ? Prog__GoToFirstRow(state) : 0;
    if (kept >= Prog_Option_Count) return;
    u8 found[Prog_Option_Count];
    u32 count = Prog_OptionMatches(state, found);
    for (u32 i = 0; i < count; ++i) if (found[i] == kept) state->paletteHighlight = i;
}

// The schemes' dropdown under `option`, highlighting the scheme it shows, or closed when it is open there.
function void Prog__OpenSchemes(Prog_State *state, Prog_Option option)
{
    if (state->schemesOpen == option + 1) {
        state->schemesOpen = 0;
        return;
    }
    state->schemesOpen = (u8)(option + 1);
    state->schemesHighlight = (u32)Prog_OptionValue(state, option);
}

// Asks about the layout named `name` before going on.
function void Prog__LayoutAsk(Prog_State *state, Prog_LayoutAsk ask, String name)
{
    state->layoutAsk = (u8)ask;
    state->layoutAskLength = (u32)Min(name.length, (u64)sizeof(state->layoutAskName));
    MemCopy(state->layoutAskName, name.value, state->layoutAskLength);
}

function b32 Prog__LayoutNamed(const Prog_State *state, String name)
{
    for (u64 i = 0; i < state->layoutNames.header.count; ++i) if (Prog__EqualFolded(state->layoutNames.v[i], name)) return true;
    return false;
}

// The Save Layout box's buttons: Save keeps the layout under the typed name, asking first where a layout has it, Update overwrites the one
// that has it, and Cancel closes the box.
function void Prog__LayoutSaveButton(Prog_State *state, u32 button)
{
    String name = Prog__TrimSpaces(Prog_EditText(&state->layoutName));
    if (button == 2) {
        state->layoutSaving = false;
        state->nameFor = 0;
        return;
    }
    // The name of a group of the user's, new or renamed; a tab would split its line in Groups.conf.
    if (state->nameFor) {
        if (!name.length) return;
        name = StrCopy(&state->frameArena, name);
        for (u64 i = 0; i < name.length; ++i) if (name.value[i] == '\t') ((char *)name.value)[i] = ' ';
        if (state->nameFor == 2) Prog_UserGroupRename(state, state->nameGroup, name);
        else Prog_UserGroupNew(state, name);
        state->layoutSaving = false;
        state->nameFor = 0;
        return;
    }
    b32 taken = Prog__LayoutNamed(state, name);
    if (!name.length || (button == 1 && !taken)) return;
    if (button == 0 && taken) {
        Prog__LayoutAsk(state, Prog_LayoutAsk_Overwrite, name);
        return;
    }
    Prog_SetLayout(state, StrCopy(&state->frameArena, name), Prog_LayoutText(&state->frameArena, state), false);
    state->layoutSaving = false;
}

function void Prog__LayoutAnswer(Prog_State *state, b32 yes)
{
    String name = StrCopy(&state->frameArena, StrMake((const char *)state->layoutAskName, state->layoutAskLength));
    Prog_LayoutAsk ask = (Prog_LayoutAsk)state->layoutAsk;
    state->layoutAsk = Prog_LayoutAsk_None;
    if (!yes) return;
    if (ask == Prog_LayoutAsk_Overwrite) {
        Prog_SetLayout(state, name, Prog_LayoutText(&state->frameArena, state), false);
        state->layoutSaving = false;
    }
    if (ask == Prog_LayoutAsk_Delete) Prog_SetLayout(state, name, (String){0}, true);
    if (ask == Prog_LayoutAsk_SwitchBranch) Prog_SwitchBranch(state, name);
    // A deleted scheme that was shown gives way to the default one.
    if (ask == Prog_LayoutAsk_DeleteScheme) {
        Prog_SetScheme(state, name, (String){0}, true);
        if (Prog__EqualFolded(StrMake(state->schemeName.text, state->schemeName.length), name)) Prog_SetOptionText(state, Prog_Option_ColorScheme, S(Prog_DefaultScheme));
    }
}

// `paths` as the system writes them, a line each, those a command line would split or misread in quotes.
function String Prog_QuotedPaths(Arena *arena, const String *paths, u64 count)
{
    StrBuilder text = {0};
    for (u64 i = 0; i < count; ++i) {
        String path = P_NativePath(arena, paths[i]);
        b32 quote = false;
        for (u64 k = 0; k < path.length && !quote; ++k) {
            u8 c = (u8)path.value[k];
            quote = c == ' ' || c == '\t' || c == '&' || c == '(' || c == ')' || c == '^' || c == ';' || c == ',' || c == '=' || c == '!' || c == '\'' ||
                    c == '`' || c == '$' || c == '[' || c == ']' || c == '{' || c == '}' || c == '+' || c == '~' || c == '#' || c == '%';
        }
        if (i) StrBuildChar(arena, &text, '\n');
        if (quote) StrBuildChar(arena, &text, '"');
        StrBuildStr(arena, &text, path);
        if (quote) StrBuildChar(arena, &text, '"');
    }
    return text.buffer;
}

// The folders the selection, or the focused item without one, stands for: folders themselves and files' containing folders, each once.
function ArrayStr Prog__OpenFolders(Prog_State *state, Arena *arena)
{
    ArrayStr result = {0};
    for (u64 i = 0; !state->loading && i < state->directory.count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        if (!(state->selectedCount ? entry->selected : state->hasFocus && i == state->focusedRow)) continue;
        String name = entry->file.kind == P_FileKind_Directory ? entry->file.name : Prog_EntryParent(entry->file.name);
        String path = name.length ? Prog_JoinPath(arena, state->directory.path, name) : state->directory.path;
        b32 seen = false;
        for (u64 k = 0; k < result.header.count && !seen; ++k) seen = StrIsEqual(result.v[k], path);
        if (!seen) ArrayPush(arena, result, path);
    }
    return result;
}

// The folder a paste or a new item goes into: in a tree's rows the focused folder when it is unfolded, or else the one the focused item is in;
// the shown folder otherwise.
function String Prog__PasteFolder(Prog_State *state, Arena *arena)
{
    if (!state->directory.tree || !state->treeNested || !state->hasFocus || state->focusedRow >= state->directory.count) return state->directory.path;
    const Prog_Entry *entry = &state->directory.entries[state->focusedRow];
    String name = entry->file.kind == P_FileKind_Directory && Prog_TreeIsOpen(state, entry->file.name) ? entry->file.name : Prog_EntryParent(entry->file.name);
    return name.length ? Prog_JoinPath(arena, state->directory.path, name) : state->directory.path;
}

// Where a line up or down from view position `position` goes in a grouped grid: the same column in the line above or below, which may be the
// last line of the group before or the first of the one after, or its last item when that line is shorter.
function u64 Prog__GroupedLineStep(const Prog_State *state, u64 position, u64 step, b32 down)
{
    u32 at = Prog_GroupAt(state, position);
    const Prog_ListGroup *group = &state->groups[at];
    u64 within = position - group->start, column = within % step;
    if (!down && within >= step) return position - step;
    if (down && group->count && within / step < (group->count - 1) / step) return Min(position + step, group->start + group->count - 1);
    for (u32 next = at; down ? ++next < state->groupCount : next-- > 0;) {
        const Prog_ListGroup *other = &state->groups[next];
        if (!other->count) continue;
        u64 line = down ? other->start : other->start + (other->count - 1) / step * step;
        return Min(line + column, other->start + other->count - 1);
    }
    return position;
}

function void Prog__FileCommand(Prog_State *state, Prog_CommandKind kind)
{
    if (state->loading || state->renaming) return;
    Arena *frame = &state->frameArena;
    String *paths = NULL;
    u64 count = Prog__SelectedPaths(state, frame, &paths);
    switch (kind) {
        case Prog_CommandKind_CopySelection:
        case Prog_CommandKind_CutSelection: {
            if (!count) break;
            PlatformClipboardSetFiles(paths, count, kind == Prog_CommandKind_CutSelection);
            ArenaClear(&state->cutArena);
            state->cutPaths = (typeof(state->cutPaths)){0};
            if (kind == Prog_CommandKind_CutSelection) {
                for (u64 i = 0; i < count; ++i) MapsPut(&state->cutArena, state->cutPaths, StrCopy(&state->cutArena, paths[i]), true);
                state->cutSequence = PlatformClipboardSequence();
            }
        } break;
        case Prog_CommandKind_Paste: {
            ArrayStr files = {0};
            b32 cut = false;
            // A web address pasted without files is downloaded into the folder.
            String url = Prog__TrimSpaces(PlatformClipboardGetText(frame)), into = Prog__PasteFolder(state, frame);
            if (PlatformClipboardGetFiles(frame, &files, &cut)) Prog__FileJob(state, Prog_JobKind_Transfer, files.v, files.header.count, into, cut, (String){0});
            else if (Prog_IsTorrent(url) && (Prog_IsWebUrl(url) || Prog_IsMagnet(url)) && !state->directory.archive && !P_IsTrashFolder(state->directory.path) &&
                     !StrIsEqual(state->directory.path, Prog_ThisPC)) {
                // A magnet link or a .torrent's web address, whose .torrent aria2c fetches for its files to be chosen in the inspector.
                Prog__FileJobOp(state, Prog_JobKind_Transfer, &url, 1, into, false, (String){0}, Prog_TransferOp_TorrentFetch);
            } else if (Prog_IsWebUrl(url) && !state->directory.archive && !P_IsTrashFolder(state->directory.path) && !StrIsEqual(state->directory.path, Prog_ThisPC)) {
                Prog__FileJobOp(state, Prog_JobKind_Transfer, &url, 1, into, false, (String){0}, Prog_TransferOp_Download);
            }
            // Cut files pasted are no longer dimmed where they were.
            if (cut) {
                ArenaClear(&state->cutArena);
                state->cutPaths = (typeof(state->cutPaths)){0};
            }
        } break;
        case Prog_CommandKind_PasteLink: {
            ArrayStr files = {0};
            b32 cut = false;
            if (PlatformClipboardGetFiles(frame, &files, &cut)) Prog__FileJob(state, Prog_JobKind_Link, files.v, files.header.count, Prog__PasteFolder(state, frame), false, (String){0});
        } break;
        case Prog_CommandKind_Trash:
            // In the bin, as in Explorer's, the key that trashes deletes for good, after asking.
            if (!P_IsTrashFolder(state->directory.path)) {
                Prog__FileJob(state, Prog_JobKind_Trash, paths, count, state->directory.path, false, (String){0});
                break;
            }
            [[fallthrough]];
        case Prog_CommandKind_SelectionDelete: {
            // Where the system asks itself, the delete starts at once and its own confirmation comes first; elsewhere nothing is deleted
            // until the confirmation here is accepted.
            if (!count) break;
            if (P_DeleteAsks()) {
                Prog__FileJob(state, Prog_JobKind_Delete, paths, count, state->directory.path, true, (String){0});
                break;
            }
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
        case Prog_CommandKind_CompressZip:
        case Prog_CommandKind_Compress7z: {
            // Named after the one item packed, or after the folder they are in, beside them.
            if (!count || state->directory.archive || P_IsTrashFolder(state->directory.path)) break;
            String folder = Prog_ParentPath(paths[0]);
            String stem = count == 1 ? (P_IsDirectory(paths[0]) ? Prog_EntryLeaf(paths[0]) : Prog__NameStem(Prog_EntryLeaf(paths[0]))) : Prog_EntryLeaf(folder);
            String name = Prog__FreeName(frame, folder, stem.length ? stem : S("Archive"), kind == Prog_CommandKind_CompressZip ? S(".zip") : S(".7z"));
            Prog__FileJobOp(state, Prog_JobKind_Transfer, paths, count, folder, false, name, Prog_TransferOp_Pack);
        } break;
        case Prog_CommandKind_ExtractHere:
        case Prog_CommandKind_ExtractToFolder: {
            if (count != 1 || state->directory.archive || !Prog_IsArchiveName(paths[0])) break;
            String folder = Prog_ParentPath(paths[0]);
            b32 here = kind == Prog_CommandKind_ExtractHere;
            String name = here ? (String){0} : Prog__FreeName(frame, folder, Prog__NameStem(Prog_EntryLeaf(paths[0])), (String){0});
            Prog__FileJobOp(state, Prog_JobKind_Transfer, paths, 1, folder, false, name, here ? Prog_TransferOp_ExtractHere : Prog_TransferOp_ExtractToFolder);
        } break;
        case Prog_CommandKind_CompressVideo: {
            // The videos among the items, each compressed beside itself.
            if (!count || state->directory.archive || P_IsTrashFolder(state->directory.path)) break;
            u64 videos = 0;
            for (u64 i = 0; i < count; ++i) if (Prog_IsVideo(paths[i])) paths[videos++] = paths[i];
            if (videos) Prog__FileJobOp(state, Prog_JobKind_Transfer, paths, videos, Prog_ParentPath(paths[0]), false, (String){0}, Prog_TransferOp_CompressVideo);
        } break;
        case Prog_CommandKind_DownloadTorrent: {
            // With aria2c, whatever program the system opens torrents with.
            if (state->directory.archive || P_IsTrashFolder(state->directory.path)) break;
            for (u64 i = 0; i < count; ++i) if (Prog_IsTorrent(paths[i])) Prog_DownloadTorrentFile(state, paths[i]);
        } break;
        case Prog_CommandKind_CopyAsPath: {
            // Without a selection, the shown folder's path.
            if (!state->selectedCount) {
                if (state->directory.path.length && !StrIsEqual(state->directory.path, Prog_ThisPC)) {
                    PlatformClipboardSetText(P_NativePath(frame, state->directory.path));
                    state->pathFlashTime = PlatformNowMicroseconds();
                }
                break;
            }
            StrBuilder text = {0};
            for (u64 i = 0; i < count; ++i) {
                if (i) StrBuildChar(frame, &text, '\n');
                StrBuildStr(frame, &text, P_NativePath(frame, paths[i]));
            }
            if (count) {
                PlatformClipboardSetText(text.buffer);
                state->pathFlashTime = PlatformNowMicroseconds();
            }
        } break;
        case Prog_CommandKind_CopyVersion: PlatformClipboardSetText(S(FILEY_VERSION)); break;
        case Prog_CommandKind_CopyAsPathSmart: {
            String *shown = paths;
            u64 shownCount = count;
            if (!state->selectedCount) {
                if (!state->directory.path.length || StrIsEqual(state->directory.path, Prog_ThisPC)) break;
                shown = &state->directory.path;
                shownCount = 1;
            }
            PlatformClipboardSetText(Prog_QuotedPaths(frame, shown, shownCount));
            state->pathFlashTime = PlatformNowMicroseconds();
        } break;
        case Prog_CommandKind_CreateFolder:
        case Prog_CommandKind_CreateFile: {
            b32 folder = kind == Prog_CommandKind_CreateFolder;
            Prog__FileJob(state, Prog_JobKind_Create, NULL, 0, Prog__PasteFolder(state, frame), folder, folder ? Tr("New folder") : Tr("New file"));
        } break;
        default: InvalidPath;
    }
}

// Whether files dropped on `entry` start it with them, as a program or a script does in Explorer, whose own drop targets do it; elsewhere a drop
// on a file goes to its folder.
function b32 Prog__RunsDropped(const Prog_Entry *entry)
{
#if defined(_WIN32)
    local_persist const String kinds[] = { SInit(".exe"), SInit(".bat"), SInit(".cmd"), SInit(".com") };
    String name = entry->file.name;
    for (u32 i = 0; entry->file.kind == P_FileKind_File && i < CArrayCount(kinds); ++i) {
        if (name.length > kinds[i].length && Prog__EqualFolded(StrFrom(name, name.length - kinds[i].length), kinds[i])) return true;
    }
#endif
    (void)entry;
    return false;
}

// The folder of sidebar item `index` in `group`; empty when there is no such item.
function String Prog__SidebarPath(const Prog_State *state, Prog_Group group, u64 index)
{
    switch (group) {
        case Prog_Group_Recents: return index < state->recents.header.count ? state->recents.v[index] : (String){0};
        case Prog_Group_Bookmarks: return index < state->bookmarks.header.count ? state->bookmarks.v[index] : (String){0};
        case Prog_Group_Storage: {
            // The drives, then the servers' shares.
            if (index < state->drives.header.count) return state->drives.v[index].path;
            index -= state->drives.header.count;
            return index < state->shares.header.count ? state->shares.v[index].path : (String){0};
        }
        case Prog_Group_Places: return index < state->places.header.count ? state->places.v[index].path : (String){0};
        default: {
            u32 own = (u32)group - Prog_Group_Count;
            return own < state->userGroupCount && index < state->userGroups[own].folders.header.count ? state->userGroups[own].folders.v[index] : (String){0};
        }
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
    // A bookmark's folder, from the sidebar.
    if (state->dragPlace.length) {
        Scratch scratch = GetScratch();
        String path = StrCopy(scratch.arena, state->dragPlace), name = Prog__FolderName(path);
        state->dragPlace = (String){0};
        state->dragging = true;
        P_DragFiles(Prog_ParentPath(path), &name, 1);
        state->dragging = false;
        UI_CancelActive();
        ReleaseScratch(scratch);
        return;
    }
    if (state->loading || !state->selectedCount) return;
    Scratch scratch = GetScratch();
    String *names = ArenaPushArray(scratch.arena, state->selectedCount, String);
    u64 *rows = ArenaPushArray(scratch.arena, state->selectedCount, u64);
    u64 count = 0;
    for (u64 i = 0; i < state->directory.count && count < state->selectedCount; ++i) {
        if (state->directory.entries[i].selected) rows[count++] = i;
    }
    String folder = Prog__RowsFolder(state, scratch.arena, rows, &count, names), archive = {0}, inner = {0};
    state->dragging = true;
    if (state->directory.archive && Prog_ArchiveSplit(folder, &archive, &inner)) P_DragArchiveItems(archive, inner, names, count);
    else P_DragFiles(folder, names, count);
    state->dragging = false;
    // The system's drag loop took the button release.
    UI_CancelActive();
    ReleaseScratch(scratch);
}

// The text selected in the inspector's text view, when it has some: with its filter on, only of the lines it shows, in `arena`.
function b32 Prog_InspectorSelection(const Prog_State *state, Arena *arena, String *text)
{
    u64 from = Min(state->previewAnchor, state->previewCaret), to = Min(Max(state->previewAnchor, state->previewCaret), state->previewText.length);
    if (!state->previewShown || state->previewHex || from >= to) return false;
    if (!Prog_EditText(&state->inspectorFilter).length || !state->previewKept) {
        *text = StrFromTo(state->previewText, from, (i64)to);
        return true;
    }
    StrBuilder kept = {0};
    for (u32 i = 0; i < state->previewKeptCount; ++i) {
        u32 line = state->previewKept[i];
        u64 a = Max(from, (u64)state->previewLines[line]), b = Min(to, (u64)state->previewLines[line + 1]);
        if (a < b) StrBuildStr(arena, &kept, StrFromTo(state->previewText, a, (i64)b));
    }
    *text = kept.buffer;
    return text->length > 0;
}

// The folder dropdown's rows the search finds, by their places in its listing, in its order.
function u32 Prog_FilesMatches(const Prog_State *state, u32 *out)
{
    String typed = Prog_EditText(&state->filesSearch);
    u32 count = 0;
    for (u64 i = 0; i < state->filesCount && i < UINT32_MAX; ++i) {
        if (!typed.length || Prog_FuzzyScore(state->filesItems[i].file.name, (String){0}, typed, NULL, NULL) > 0) out[count++] = (u32)i;
    }
    return count;
}

function void Prog__CloseFilesPopup(Prog_State *state)
{
    state->filesOpen = false;
    if (state->filesJob) P_RequestCancel(&state->filesJob->cancelToken);
    state->filesJob = NULL;
}

// The folder of the crumb ending at `end` of the crumbs' `path`: a drive's crumb is its root.
function String Prog__CrumbFolder(String path, u64 end)
{
    String folder = StrFromTo(path, 0, (i64)end);
    return folder.length == 2 && folder.value[1] == ':' ? StrFromTo(path, 0, 3) : folder;
}

// Opens the dropdown of crumb `at` of the `count` ending at `ends` in `path`, listing its folder in a job, with the next crumb's folder marked.
function void Prog__OpenFilesPopup(Prog_State *state, String path, const u64 *ends, u32 count, u32 at)
{
    Prog_CloseMenu(state);
    state->typesOpen = false;
    Prog__CloseFilesPopup(state);
    ArenaClear(&state->filesArena);
    state->filesFolder = StrCopy(&state->filesArena, Prog__CrumbFolder(path, ends[at]));
    state->filesChild = at + 1 < count ? StrCopy(&state->filesArena, StrFromTo(path, ends[at] + 1, (i64)ends[at + 1])) : (String){0};
    state->filesOpen = true;
    state->filesListed = false;
    state->filesItems = NULL;
    state->filesCount = 0;
    state->filesWidest = 0;
    state->filesSearch.length = state->filesSearch.cursor = state->filesSearch.anchor = 0;
    ++state->filesSearch.version;
    state->filesHighlight = 0;
    state->filesLit = state->filesReveal = false;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_FilesList;
    input->path = StrCopy(&job->arena, state->filesFolder);
    input->showHidden = state->showHidden;
    input->showSystem = state->showSystem;
    job->input = input;
    state->filesJob = job;
    P_JobSubmit(job);
}

// Shows `folder` with `item` in it selected, when there is one: in the tab of a panel that shows it, the active ones first, or else in the
// active tab. With `inspect`, the inspector opens on it, as for a torrent, whose files it lists with how far each has come.
function void Prog__Reveal(Prog_State *state, String folder, String item, b32 inspect)
{
    folder = StrCopy(&state->frameArena, folder);
    item = StrCopy(&state->frameArena, item);
    u32 panel = UINT32_MAX, tab = 0;
    for (u32 n = 0; n < state->panelCount && panel == UINT32_MAX; ++n) {
        u32 p = (state->activePanel + n) % state->panelCount;
        if (state->panels[p].inspectorOf) continue;
        Prog__UsePanel(state, p);
        for (u32 t = 0; t < state->tabCount && panel == UINT32_MAX; ++t) {
            u32 at = (state->activeTab + t) % state->tabCount;
            if (Prog__SamePath(Prog_TabPath(state, at), folder)) panel = p, tab = at;
        }
    }
    Prog__ActivatePanel(state, panel != UINT32_MAX ? panel : state->activePanel);
    if (panel != UINT32_MAX) Prog_SelectTab(state, tab);
    if (item.length) Prog_ShowInFolder(state, Prog_JoinPath(&state->frameArena, folder, item), false);
    else if (!Prog__SamePath(Prog_TabPath(state, state->activeTab), folder)) Prog_RequestDirectory(state, folder, true, 0);
    if (inspect) state->previewShown = true;
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
        case Prog_CommandKind_OpenInWindow: {
            P_FileError error = {0};
            ArrayStr folders = Prog__OpenFolders(state, &state->frameArena);
            for (u64 i = 0; i < folders.header.count && !error.code; ++i) error = Prog_OpenWindow(state, folders.v[i], (String){0});
            if (error.code) {
                Prog_Notify(state, Prog_NoticeKind_Error, S("window.failed"), Tr("Couldn't open a new window"), error.message);
            }
        } break;
            case Prog_CommandKind_OpenInRightSplit:
            case Prog_CommandKind_OpenInBottomSplit: {
                // Each folder in a panel of its own, split off the last one; the paths are taken first, as a split moves to the new panel.
                if (state->loading) break;
                Arena *frame = &state->frameArena;
                String *paths = ArenaPushArray(frame, Max(state->directory.count, (u64)1), String);
                u64 count = 0;
                for (u64 i = 0; i < state->directory.count; ++i) {
                    const Prog_Entry *entry = &state->directory.entries[i];
                    if (!(state->selectedCount ? entry->selected : state->hasFocus && i == state->focusedRow)) continue;
                    String name = entry->file.kind == P_FileKind_Directory ? entry->file.name : Prog_EntryParent(entry->file.name);
                    paths[count++] = name.length ? Prog_JoinPath(frame, state->directory.path, name) : state->directory.path;
                }
                Prog_SplitKind kind = command->kind == Prog_CommandKind_OpenInRightSplit ? Prog_SplitKind_Horizontal : Prog_SplitKind_Vertical;
                for (u64 i = 0; i < count; ++i) Prog_SplitPanel(state, state->activePanel, kind, false, paths[i]);
            } break;
            case Prog_CommandKind_SelectionCopyPrevPanel:
            case Prog_CommandKind_SelectionCopyNextPanel:
            case Prog_CommandKind_SelectionMovePrevPanel:
            case Prog_CommandKind_SelectionMoveNextPanel: {
                if (state->loading || state->renaming) break;
                u64 order[Prog_MaxPanels];
                u32 count = Prog__PanelsInOrder(state, 0, order, 0), at = count;
                for (u32 i = 0; i < count; ++i) if (order[i] == state->panels[state->activePanel].id) at = i;
                if (count < 2 || at == count) break;
                b32 next = command->kind == Prog_CommandKind_SelectionCopyNextPanel || command->kind == Prog_CommandKind_SelectionMoveNextPanel;
                b32 move = command->kind == Prog_CommandKind_SelectionMovePrevPanel || command->kind == Prog_CommandKind_SelectionMoveNextPanel;
                u32 target = Prog_PanelIndex(state, order[next ? (at + 1) % count : (at + count - 1) % count]);
                if (target == UINT32_MAX) break;
                String folder = target == state->currentPanel ? state->directory.path : state->panels[target].directory.path;
                String *paths = NULL;
                u64 selected = Prog__SelectedPaths(state, &state->frameArena, &paths);
                if (folder.length && !StrIsEqual(folder, state->directory.path) && !StrIsEqual(folder, Prog_ThisPC)) {
                    Prog__FileJob(state, Prog_JobKind_Transfer, paths, selected, folder, move, (String){0});
                }
            } break;
            case Prog_CommandKind_ToggleCursorSelection: {
                if (state->loading || !state->hasFocus || state->focusedRow >= state->directory.count) break;
                Prog_Entry *entry = &state->directory.entries[state->focusedRow];
                entry->selected = !entry->selected;
                state->selectedCount = entry->selected ? state->selectedCount + 1 : state->selectedCount - 1;
                state->anchorRow = state->focusedRow;
                state->hasAnchor = true;
            } break;
            case Prog_CommandKind_OpenContainingPath:
            case Prog_CommandKind_OpenContainingPathInTab:
            case Prog_CommandKind_OpenRelativePath:
            case Prog_CommandKind_OpenRelativePathInTab: {
                // The focused item shown in its own folder, selected; an expanded listing's items are in subfolders of the one shown.
                // The containing path is where it really is, a shortcut's or link's target, which a job finds, as a slow drive may take a while to say.
                if (state->loading || !state->hasFocus || state->focusedRow >= state->directory.count) break;
                b32 inTab = command->kind == Prog_CommandKind_OpenContainingPathInTab || command->kind == Prog_CommandKind_OpenRelativePathInTab;
                String path = Prog_JoinPath(&state->frameArena, state->directory.path, state->directory.entries[state->focusedRow].file.name);
                if (command->kind == Prog_CommandKind_OpenRelativePath || command->kind == Prog_CommandKind_OpenRelativePathInTab) {
                    Prog_ShowInFolder(state, path, inTab);
                    break;
                }
                P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
                Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
                input->kind = Prog_JobKind_Reveal;
                input->panel = Prog__PanelId(state);
                input->generation = state->generation;
                input->path = StrCopy(&job->arena, path);
                input->inTab = inTab;
                job->input = input;
                P_JobSubmit(job);
            } break;
            case Prog_CommandKind_CalcDirsContents: {
                if (state->loading) break;
                for (u64 i = 0; i < state->directory.count; ++i) {
                    Prog_Entry *entry = &state->directory.entries[i];
                    b32 wanted = state->selectedCount ? entry->selected : state->hasFocus && i == state->focusedRow;
                    if (wanted && entry->file.kind == P_FileKind_Directory && !entry->drive) entry->remeasure = true;
                }
                Prog__MeasureFolders(state);
            } break;
            case Prog_CommandKind_OpenRowInTab: if (command->row < state->directory.count) Prog__OpenInTabs(state, command->row); break;
            case Prog_CommandKind_SelectTab: Prog_SelectTab(state, (u32)command->row); break;
            case Prog_CommandKind_CloseTabAt: Prog_CloseTab(state, (u32)command->row); break;
            case Prog_CommandKind_MoveTab: Prog_MoveTab(state, (u32)command->row, (u32)command->target); break;
            case Prog_CommandKind_TabNew:
            case Prog_CommandKind_TabDuplicate: {
                // A new tab shows what the settings say, and a duplicate the current tab's folder.
                String path = Prog_TabPath(state, state->activeTab);
                if (command->kind == Prog_CommandKind_TabNew) path = Prog_NewTabPath(state, &state->frameArena, path);
                Prog_OpenTab(state, path, state->activeTab + 1, true);
            } break;
            case Prog_CommandKind_TabShiftLeft: if (state->activeTab) Prog_MoveTab(state, state->activeTab, state->activeTab - 1); break;
            case Prog_CommandKind_TabShiftRight: Prog_MoveTab(state, state->activeTab, state->activeTab + 1); break;
            case Prog_CommandKind_TabClose: Prog_CloseTab(state, state->activeTab); break;
            // The panel goes with all its tabs, which Reopen Closed Tab brings back; the last panel takes the window with it.
            case Prog_CommandKind_TabCloseAll: {
                if (state->panelCount <= 1) {
                    PlatformWindowClose();
                    break;
                }
                for (u32 i = 0; i < state->tabCount; ++i) {
                    String path = Prog_TabPath(state, i);
                    if (path.length) ArrayPush(&state->closedArena, state->closedTabs, StrCopy(&state->closedArena, path));
                }
                Prog_ClosePanel(state, state->currentPanel);
            } break;
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
                if (state->sidebarHidden) state->sidebarFiltering = false;
            } break;
            case Prog_CommandKind_StartQuickAccessFilter: {
                Prog_CloseMenu(state);
                state->sidebarHidden = false;
                if (!state->sidebarFiltering) state->sidebarFilterBefore = state->sidebarFilter;
                state->sidebarFiltering = true;
            } break;
            case Prog_CommandKind_QuickAccessFilterRevert: {
                state->sidebarFiltering = false;
                u32 version = state->sidebarFilter.version;
                state->sidebarFilter = state->sidebarFilterBefore;
                state->sidebarFilter.version = version + 1;
            } break;
            case Prog_CommandKind_QuickAccessFilterApply: state->sidebarFiltering = false; break;
            case Prog_CommandKind_QuickAccessFilterCancel: {
                state->sidebarFiltering = false;
                state->sidebarFilter.length = state->sidebarFilter.cursor = state->sidebarFilter.anchor = 0;
                ++state->sidebarFilter.version;
            } break;
            case Prog_CommandKind_TogglePreview: {
                // The shown tab's own; from inside an inspector's folder list, its owner's.
                u64 owner = state->panels[state->currentPanel].inspectorOf;
                u32 back = state->currentPanel;
                if (owner && Prog_PanelIndex(state, owner) != UINT32_MAX) Prog__UsePanel(state, Prog_PanelIndex(state, owner));
                state->previewShown = !state->previewShown;
                // What the inspector played stops with it, as nothing else shows it or pauses it.
                if (!state->previewShown) Prog__StopPlayer(state);
                Prog__UsePanel(state, back);
            } break;
            // A document's prose wraps by its own switch, so that unwrapping one leaves code files as they were.
            case Prog_CommandKind_InspectorToggleWordWrap: {
                if (state->previewDocument) state->documentUnwrapped = !state->documentUnwrapped;
                else state->inspectorWrap = !state->inspectorWrap;
            } break;
            case Prog_CommandKind_InspectorToggleHex: {
                // The file is read again, the other way.
                state->inspectorHex = !state->inspectorHex;
                state->previewPath = (String){0};
            } break;
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
                    state->zoom = Prog_ZoomStepped(state->zoom, up);
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
            case Prog_CommandKind_LanguageCreate: {
                // Named after the language shown, as its own; an existing file of that name is opened as it is.
                if (!state->configDirectory.length) break;
                Arena *frame = &state->frameArena;
                String shown = StrMake(state->language.text, state->language.length);
                // A custom language chosen is opened again, its file made anew if it has gone.
                String name = Prog_LanguageIsCustom(shown) ? shown : StrFormat(frame, "%S (custom)", shown.length ? shown : S("English"));
                String path = Prog_JoinPath(frame, state->configDirectory, StrFormat(frame, "%S.lang", name)), existing = {0};
                if (!P_ReadFile(frame, path, &existing)) P_WriteFile(frame, path, Prog_LanguageTemplate(frame, name));
                Prog_ListLanguages(state);
                Prog_SetOptionText(state, Prog_Option_Language, name);
                // The file selected in its folder in a new tab, to open in the editor of one's choice, as no program opens .lang files yet.
                String file = StrFormat(frame, "%S.lang", name);
                Prog_OpenTab(state, state->configDirectory, state->activeTab + 1, true);
                ArenaClear(&state->pickArena);
                state->pickNames = (ArrayStr){0};
                ArrayPush(&state->pickArena, state->pickNames, StrCopy(&state->pickArena, file));
                Prog_Notify(state, Prog_NoticeKind_Info, S("language.created"), Tr("Custom language ready"),
                            StrFormat(frame, TrF("Open %S in a text editor and write the translations; each save shows at once."), file));
            } break;
            case Prog_CommandKind_OpenSettings: {
                // A first open writes every setting there is, commented out at its default.
                if (!state->configDirectory.length) break;
                String path = Prog_JoinPath(&state->frameArena, state->configDirectory, S("Settings.conf"));
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
            case Prog_CommandKind_Redo: if (!state->renaming) Prog_RedoLast(state); break;
            case Prog_CommandKind_BookmarksToggle: Prog_ToggleBookmark(state, state->directory.path); break;
            case Prog_CommandKind_FolderViewToggle: Prog_ToggleFolderView(state); break;
            case Prog_CommandKind_FolderViewsClear: Prog_ClearFolderViews(state); break;
            case Prog_CommandKind_BookmarksClear: {
                Prog__SetList(&state->bookmarkArena, &state->bookmarks, NULL, 0);
                Prog__SaveList(state, Prog_BookmarksFile, state->bookmarks);
            } break;
            case Prog_CommandKind_RecentsClear: {
                Prog__SetRecents(state, NULL, NULL, 0);
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
                // A drive's root has no folder path above it, so it is named by its own path.
                String parent = Prog_ParentPath(path), name = Prog__FolderName(path);
                b32 root = StrIsEqual(parent, path) || !parent.length;
                if (root) name = path;
                // This PC is the system's computer item, where it has one.
                if (StrIsEqual(path, Prog_ThisPC)) {
                    name = P_ComputerIconPath();
                    root = true;
                    if (!name.length) {
                        Prog_CloseMenu(state);
                        break;
                    }
                }
                state->menuOpen = P_ContextMenuOpen(&state->menuArena, root ? (String){0} : parent, &name, 1, &state->menu);
                if (!state->menuOpen) {
                    Prog_CloseMenu(state);
                    break;
                }
                // The bin's menu starts with emptying it, while it holds anything.
                if (P_IsTrashFolder(path) && P_TrashHasItems()) {
                    Prog__MenuPrepend(state, (P_MenuItem){ .label = StrFormat(&state->menuArena, TrF("Empty %S"), Prog_PathName(state, path)), .action = P_MenuAction_EmptyTrash }, path);
                }
                Prog__MenuAddGroups(state, !StrIsEqual(path, Prog_ThisPC) && !P_IsTrashFolder(path));
                Prog__ApplyMenuPins(state);
                // Filey's own cut, copy, duplicate and delete act on the list's selection, so for a sidebar folder they are shown disabled,
                // and its rename, which edits a list row, is left out.
                for (u32 i = 0; i < state->menu.count; ++i) {
                    P_MenuItem *item = &state->menu.items[i];
                    if (StrIsEqual(item->verb, S("rename"))) item->label = (String){0};
                    b32 selection = StrIsEqual(item->verb, S("cut")) || StrIsEqual(item->verb, S("copy")) || StrIsEqual(item->verb, S("delete")) ||
                                    item->action == P_MenuAction_Cut || item->action == P_MenuAction_Copy || item->action == P_MenuAction_Duplicate ||
                                    item->action == P_MenuAction_Trash || item->action == P_MenuAction_Delete;
                    if (selection) item->disabled = true;
                }
                state->menuAt = state->ui.mouse;
                state->menuHighlight = UINT32_MAX;
                Prog__MenuMove(state, 1);
            } break;
            case Prog_CommandKind_ToggleGroup: {
                if (command->row < Prog_Group_Count) state->groupToggled[command->row] = !state->groupToggled[command->row];
                else Prog_UserGroupFold(state, (u32)(command->row - Prog_Group_Count));
            } break;
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
                String up = Prog_UpPath(state->directory.path);
                if (up.length) Prog__PassToDirectory(state, up);
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
            case Prog_CommandKind_ToggleRecents: Prog_SetOption(state, Prog_Option_ShowRecents, !state->showRecents); break;
            case Prog_CommandKind_IncreaseFontSize: Prog_SetOption(state, Prog_Option_FontSize, Prog_OptionValue(state, Prog_Option_FontSize) + 1); break;
            case Prog_CommandKind_DecreaseFontSize: Prog_SetOption(state, Prog_Option_FontSize, Prog_OptionValue(state, Prog_Option_FontSize) - 1); break;
            case Prog_CommandKind_ResetFontSize: Prog_SetOption(state, Prog_Option_FontSize, 9); break;
            case Prog_CommandKind_IncreaseSpacing: Prog_SetOption(state, Prog_Option_Spacing, Prog_OptionValue(state, Prog_Option_Spacing) + 1); break;
            case Prog_CommandKind_DecreaseSpacing: Prog_SetOption(state, Prog_Option_Spacing, Prog_OptionValue(state, Prog_Option_Spacing) - 1); break;
            case Prog_CommandKind_ToggleFileExtension: Prog_SetOption(state, Prog_Option_ShowFileExtension, state->hideExtensions); break;
            case Prog_CommandKind_ToggleSystemFiles: if (Prog_CommandApplies(Prog_CommandKind_ToggleSystemFiles)) Prog_SetOption(state, Prog_Option_ShowSystemFiles, !state->showSystem); break;
            case Prog_CommandKind_ToggleSortRecents: Prog_SetOption(state, Prog_Option_SortRecents, !state->sortRecents); break;
            case Prog_CommandKind_CycleFolderSort: {
                state->folderSort = (u8)((state->folderSort + 1) % Prog_FolderSort_Count);
                Prog_Reload(state);
            } break;
            case Prog_CommandKind_NavigatePrefix:
            case Prog_CommandKind_PanelHeaderDirInTab: {
                String target = Prog__CrumbTarget(state, command->row);
                if (!target.length) break;
                if (command->kind == Prog_CommandKind_PanelHeaderDirInTab) Prog_OpenTab(state, StrCopy(&state->frameArena, target), state->activeTab + 1, false);
                else if (!StrIsEqual(target, state->directory.path)) Prog__PassToDirectory(state, target);
            } break;
            case Prog_CommandKind_PathOpenInWindow:
            case Prog_CommandKind_PathOpenInRightSplit:
            case Prog_CommandKind_PathOpenInBottomSplit: {
                String target = StrCopy(&state->frameArena, Prog__CrumbTarget(state, command->row));
                if (!target.length) break;
                if (command->kind == Prog_CommandKind_PathOpenInWindow) {
                    P_FileError error = Prog_OpenWindow(state, target, (String){0});
                    if (error.code) Prog_Notify(state, Prog_NoticeKind_Error, S("window.failed"), Tr("Couldn't open a new window"), error.message);
                } else {
                    Prog_SplitPanel(state, state->activePanel, command->kind == Prog_CommandKind_PathOpenInRightSplit ? Prog_SplitKind_Horizontal : Prog_SplitKind_Vertical, false, target);
                }
            } break;
            case Prog_CommandKind_PathCopy: {
                String target = Prog__CrumbTarget(state, command->row);
                if (!target.length || StrIsEqual(target, Prog_ThisPC)) break;
                PlatformClipboardSetText(P_NativePath(&state->frameArena, target));
                state->pathFlashTime = PlatformNowMicroseconds();
            } break;
            case Prog_CommandKind_ToggleCompactPath: Prog_SetOption(state, Prog_Option_ShortenedPaths, state->fullPaths ? 1 : 0); break;
            case Prog_CommandKind_ToggleBreadcrumbs: Prog_SetOption(state, Prog_Option_Breadcrumbs, state->noTrail ? 1 : 0); break;
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
                // A grouped list's groups each start their lines under their heading; one without groups is a single group without one.
                Prog_ListGroup whole = { .count = state->viewCount };
                const Prog_ListGroup *groups = state->groupCount ? state->groups : &whole;
                for (u32 g = 0; pitch.x > 0 && pitch.y > 0 && g < Max(state->groupCount, 1u); ++g) {
                    const Prog_ListGroup *group = &groups[g];
                    f32 below = state->groupCount ? group->top + state->groupHeading : 0;
                    u64 lines = (group->count + columns - 1) / columns;
                    if (!lines) continue;
                    u64 line0 = (u64)MaxF32(0, floorf((box.y0 - below - item.y1) / pitch.y)), line1 = Min(lines - 1, (u64)MaxF32(0, floorf((box.y1 - below - item.y0) / pitch.y)) + 1);
                    for (u64 line = line0; line <= line1 && line < lines; ++line) {
                        for (u64 column = 0; column < columns; ++column) {
                            u64 at = group->start + line * columns + column;
                            if (at >= group->start + group->count) break;
                            f32 x = (f32)column * pitch.x, y = below + (f32)line * pitch.y;
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
            case Prog_CommandKind_PanelEvenSiblings: Prog_EvenPanels(state, false); break;
            case Prog_CommandKind_PanelEvenAll: Prog_EvenPanels(state, true); break;
            case Prog_CommandKind_TogglePanelZoom: {
                // The panel the keyboard is in, or the one whose inspector it is in.
                u64 owner = state->panels[state->activePanel].inspectorOf ? state->panels[state->activePanel].inspectorOf : state->panels[state->activePanel].id;
                state->zoomedPanel = state->zoomedPanel ? 0 : owner;
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
            case Prog_CommandKind_PanelHover: break;
            case Prog_CommandKind_PanelLeft: Prog_FocusPanelToward(state, -1, 0); break;
            case Prog_CommandKind_PanelRight: Prog_FocusPanelToward(state, 1, 0); break;
            case Prog_CommandKind_PanelUp: Prog_FocusPanelToward(state, 0, -1); break;
            case Prog_CommandKind_PanelDown: Prog_FocusPanelToward(state, 0, 1); break;
            case Prog_CommandKind_ResizeSidebar: state->sidebarWidth = UI__Clamp((f32)command->target / 1000.0f, Prog_SidebarMin, Prog_SidebarMax); break;
            case Prog_CommandKind_ResizeSplit: {
                if (command->row < Prog_MaxSplits) state->splits[command->row].ratio = UI__Clamp((f32)command->target / 1e6f, 0.1f, 0.9f);
            } break;
            case Prog_CommandKind_ZoomIn:
            case Prog_CommandKind_ZoomOut: {
                u32 zoom = Prog_ZoomStepped(state->zoom, command->kind == Prog_CommandKind_ZoomIn);
                if (zoom != state->zoom) { state->zoom = zoom; state->revealFocus = true; }
            } break;
            case Prog_CommandKind_ViewDetails: state->zoom = 0; state->revealFocus = true; break;
            case Prog_CommandKind_ZoomSet: if (command->row <= Prog_ZoomMax) { state->zoom = (u32)command->row; state->revealFocus = true; } break;
            case Prog_CommandKind_ViewList: state->zoom = Prog_ZoomList; state->revealFocus = true; break;
            case Prog_CommandKind_ViewColumns: state->zoom = Prog_ZoomColumns; state->revealFocus = true; break;
            case Prog_CommandKind_ViewMediumIcons: state->zoom = Prog_ZoomIcons; state->revealFocus = true; break;
            case Prog_CommandKind_ViewLargeIcons: state->zoom = Prog_ZoomLarge; state->revealFocus = true; break;
            case Prog_CommandKind_ViewXLIcons: state->zoom = Prog_ZoomMax; state->revealFocus = true; break;
            case Prog_CommandKind_ToggleMillerColumns: state->miller = !state->miller; state->revealFocus = true; break;
            case Prog_CommandKind_ToggleTreeView: {
                // Turned off, it folds everything, which turning it on again starts from.
                state->tree = !state->tree;
                if (!state->tree) Prog__SetList(&state->treeArena, &state->treeOpen, NULL, 0);
                state->revealFocus = true;
                Prog_Reload(state);
            } break;
            case Prog_CommandKind_TreeToggle: {
                if (state->loading || command->row >= state->directory.count || state->directory.entries[command->row].file.kind != P_FileKind_Directory) break;
                Prog_TreeToggle(state, state->directory.entries[command->row].file.name);
            } break;
            case Prog_CommandKind_GroupByNone:
            case Prog_CommandKind_GroupByType:
            case Prog_CommandKind_GroupByDateModified:
            case Prog_CommandKind_GroupBySize: {
                // Another grouping starts with every group shown.
                state->groupBy = command->kind == Prog_CommandKind_GroupByType ? Prog_GroupBy_Type : command->kind == Prog_CommandKind_GroupByDateModified ? Prog_GroupBy_Modified
                               : command->kind == Prog_CommandKind_GroupBySize ? Prog_GroupBy_Size : Prog_GroupBy_None;
                Prog__SetList(&state->groupArena, &state->groupsFolded, NULL, 0);
                Prog_ApplyFilter(state, false);
                state->revealFocus = true;
            } break;
            case Prog_CommandKind_GroupToggle: if (!state->loading && command->row < state->groupCount) Prog_GroupToggle(state, (u32)command->row); break;
            case Prog_CommandKind_LayoutsSave: {
                Prog_CloseMenu(state);
                state->paletteOpen = false;
                state->layoutSaving = true;
                state->layoutName = (Prog_TextEdit){0};
            } break;
            case Prog_CommandKind_LayoutsLoad: {
                Prog_CloseMenu(state);
                state->paletteOpen = true;
                state->paletteMode = Prog_PaletteMode_Layouts;
                state->palette = (Prog_TextEdit){0};
                state->paletteHighlight = 0;
                state->paletteWheeled = false;
            } break;
            case Prog_CommandKind_SetupOpen: Prog__OpenSetup(state); break;
            case Prog_CommandKind_SetupToggle: {
                if (command->row == 0) state->setupDesktop = !state->setupDesktop;
                if (command->row == 1) state->setupOnPath = !state->setupOnPath;
                if (command->row == 2) state->setupDefault = !state->setupDefault;
                if (command->row == 3) state->setupMenu = !state->setupMenu;
            } break;
            case Prog_CommandKind_SetupInstall:
            case Prog_CommandKind_SetupUninstall: {
                b32 install = command->kind == Prog_CommandKind_SetupInstall;
                // The right-click entries are written in the language shown.
                P_MenuLabels labels = { Tr("Show in Filey"), Tr("Open in Filey"), Tr("Open Filey here") };
                P_FileError error = install ? P_InstallApply(&state->frameArena, state->setupDesktop, state->setupOnPath, state->setupDefault, state->setupMenu, labels)
                                            : P_Uninstall(&state->frameArena);
                if (error.code) {
                    Prog_Notify(state, Prog_NoticeKind_Error, install ? S("install.failed") : S("uninstall.failed"), install ? Tr("Install failed") : Tr("Uninstall failed"), error.message);
                }
                Prog__ReadSetup(state);
            } break;
            case Prog_CommandKind_SetupBack: state->setupOpen = false; break;
            case Prog_CommandKind_ExplorerOpen: {
                // A folder inside an archive opens the archive's folder, and This PC the system's own start.
                String folder = state->directory.path, archive = {0}, inner = {0};
                if (state->directory.archive && Prog_ArchiveSplit(folder, &archive, &inner)) folder = Prog_ParentPath(archive);
                if (StrIsEqual(folder, Prog_ThisPC)) folder = (String){0};
                P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
                Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
                input->panel = Prog__PanelId(state);
                input->kind = Prog_JobKind_Open;
                input->explorer = true;
                input->path = StrCopy(&job->arena, folder);
                job->input = input;
                P_JobSubmit(job);
            } break;
            case Prog_CommandKind_LayoutSaveButton: Prog__LayoutSaveButton(state, (u32)command->row); break;
            case Prog_CommandKind_LayoutDelete: {
                u32 matches[Prog_MaxLayouts];
                u32 count = Prog_LayoutMatches(state, matches);
                if (command->row < count) Prog__LayoutAsk(state, Prog_LayoutAsk_Delete, state->layoutNames.v[matches[command->row]]);
            } break;
            case Prog_CommandKind_LayoutAnswer: Prog__LayoutAnswer(state, command->row != 0); break;
            case Prog_CommandKind_FontPickerOpen: Prog__OpenFontPicker(state, (Prog_Option)command->row); break;
            case Prog_CommandKind_ColorPickerOpen: Prog__OpenColorPicker(state, (Prog_Option)command->row); break;
            case Prog_CommandKind_ColorPickerSet: if (state->colorPicking) Prog_PickColor(state, (u32)command->row, (f32)((f64)command->target / 1e6)); break;
            case Prog_CommandKind_ColorPickerClose: Prog_CloseColorPicker(state); break;
            case Prog_CommandKind_GitBranchesOpen: Prog_ToggleBranches(state); break;
            case Prog_CommandKind_GitPull: Prog_GitPull(state); break;
            case Prog_CommandKind_NoticeMute: {
                // Its kind joins the muted ones, and every notice of that kind goes.
                String key = {0};
                for (u32 i = 0; i < state->noticeCount; ++i) {
                    if (state->notices[i].id == command->row) key = StrCopy(&state->frameArena, StrMake((const char *)state->notices[i].key, state->notices[i].keyLength));
                }
                if (!key.length) break;
                u64 count = state->mutedNotices.header.count;
                String *names = ArenaPushArray(&state->frameArena, count + 1, String);
                b32 known = false;
                for (u64 i = 0; i < count; ++i) known |= StrIsEqual(names[i] = state->mutedNotices.v[i], key);
                if (!known) names[count++] = key;
                Prog__SetList(&state->mutedArena, &state->mutedNotices, names, count);
                Prog__SaveList(state, Prog_MutedNoticesFile, state->mutedNotices);
                for (u32 i = 0; i < state->noticeCount;) {
                    if (!StrIsEqual(StrMake((const char *)state->notices[i].key, state->notices[i].keyLength), key)) {
                        ++i;
                        continue;
                    }
                    MemMove(&state->notices[i], &state->notices[i + 1], (state->noticeCount - i - 1) * sizeof(Prog_Notice));
                    --state->noticeCount;
                }
            } break;
            case Prog_CommandKind_NoticesUnmuteAll: {
                Prog__SetList(&state->mutedArena, &state->mutedNotices, NULL, 0);
                Prog__SaveList(state, Prog_MutedNoticesFile, state->mutedNotices);
                state->mutedOpen = false;
            } break;
            case Prog_CommandKind_MutedNoticesOpen: state->mutedOpen = !state->mutedOpen && state->mutedNotices.header.count; break;
            case Prog_CommandKind_NoticeUnmute: {
                u64 count = state->mutedNotices.header.count;
                if (command->row >= count) break;
                String *names = ArenaPushArray(&state->frameArena, count, String);
                u64 kept = 0;
                for (u64 i = 0; i < count; ++i) if (i != command->row) names[kept++] = state->mutedNotices.v[i];
                Prog__SetList(&state->mutedArena, &state->mutedNotices, names, kept);
                Prog__SaveList(state, Prog_MutedNoticesFile, state->mutedNotices);
                // The last one shown again closes it.
                if (!kept) state->mutedOpen = false;
            } break;
            case Prog_CommandKind_NoticeCopy:
            case Prog_CommandKind_CheckForUpdates: Prog_CheckForUpdates(state, true); break;
            case Prog_CommandKind_PlaceHiddenToggle: Prog_TogglePlaceHidden(state, command->row); break;
            case Prog_CommandKind_PlaceMove: Prog_MovePlace(state, command->row, command->target); break;
            case Prog_CommandKind_PlacesShowHidden: state->placesShowHidden = !state->placesShowHidden; break;
            case Prog_CommandKind_PlacesRestore: Prog_RestorePlaces(state); break;
            case Prog_CommandKind_GroupShiftUp:
            case Prog_CommandKind_GroupShiftDown: {
                if (command->row < Prog_SidebarGroupCount(state)) Prog_ShiftGroup(state, (u32)command->row, command->kind == Prog_CommandKind_GroupShiftDown);
            } break;
            case Prog_CommandKind_GroupNew:
            case Prog_CommandKind_GroupRename: {
                // The name box, with the group's name to change for a rename.
                b32 rename = command->kind == Prog_CommandKind_GroupRename;
                if ((rename && (command->row < Prog_Group_Count || command->row - Prog_Group_Count >= state->userGroupCount)) || (!rename && state->userGroupCount >= Prog_MaxUserGroups)) break;
                Prog_CloseMenu(state);
                state->paletteOpen = false;
                state->layoutSaving = true;
                state->nameFor = rename ? 2 : 1;
                state->nameGroup = rename ? (u32)(command->row - Prog_Group_Count) : 0;
                state->layoutName = (Prog_TextEdit){0};
                if (rename) {
                    String name = state->userGroups[state->nameGroup].name;
                    u32 length = (u32)Min(name.length, (u64)sizeof(state->layoutName.text) - 1);
                    MemCopy(state->layoutName.text, name.value, length);
                    state->layoutName.length = state->layoutName.cursor = length;
                }
            } break;
            case Prog_CommandKind_GroupDelete: if (command->row >= Prog_Group_Count) Prog_UserGroupDelete(state, (u32)(command->row - Prog_Group_Count)); break;
            case Prog_CommandKind_GroupItemRemove: if (command->row >= Prog_Group_Count) Prog_UserGroupRemove(state, (u32)(command->row - Prog_Group_Count), command->target); break;
            case Prog_CommandKind_GroupItemMove: {
                if (command->row >= Prog_Group_Count) Prog_UserGroupMove(state, (u32)(command->row - Prog_Group_Count), command->target & UINT32_MAX, command->target >> 32);
            } break;
            case Prog_CommandKind_GroupsRestore: state->groupsOrdered = false; break;
            case Prog_CommandKind_UpdateInstall: {
                // From the notice's button, which goes with it.
                for (u32 i = 0; i < state->noticeCount; ++i) {
                    if (state->notices[i].id != command->row || state->notices[i].action != Prog_CommandKind_UpdateInstall) continue;
                    MemMove(&state->notices[i], &state->notices[i + 1], (state->noticeCount - i - 1) * sizeof(Prog_Notice));
                    --state->noticeCount;
                    break;
                }
                Prog_InstallUpdate(state);
            } break;
            case Prog_CommandKind_CrashReport: {
                for (u32 i = 0; i < state->noticeCount; ++i) {
                    if (state->notices[i].id != command->row) continue;
                    MemMove(&state->notices[i], &state->notices[i + 1], (state->noticeCount - i - 1) * sizeof(Prog_Notice));
                    --state->noticeCount;
                    break;
                }
                if (!state->crashReport.length) break;
                Arena *frame = &state->frameArena;
                String folder = Prog_CrashFolder(frame, state), name = state->crashReport;
                // A text report, as Linux writes, goes into the issue itself; a minidump is attached by hand.
                String details = {0};
                if (Prog__ContainsFolded(name, S(".txt")) && P_ReadFile(frame, Prog_JoinPath(frame, folder, name), &details)) details = StrFromTo(details, 0, (i64)Min(details.length, (u64)3000));
#if defined(_WIN32)
                String system = S("Windows");
#else
                String system = S("Linux");
#endif
                // A minidump goes in a zip, as GitHub takes no .dmp files, which is put on the clipboard to paste into the issue and selected in the
                // tab below to drag there instead.
                String attached = {0};
                if (!details.length) {
                    String zip = StrFormat(frame, "%S.zip", name), dump = Prog_JoinPath(frame, folder, name), zipPath = Prog_JoinPath(frame, folder, zip);
                    if (P_PathExists(zipPath) || !P_AddToArchive(frame, zipPath, (String){0}, &dump, 1, 9, NULL, NULL).code) {
                        attached = zip;
                        PlatformClipboardSetFiles(&zipPath, 1, false);
                    }
                }
                String attach = attached.length ? StrFormat(frame, NoTrF("Paste the crash report here (Ctrl+V): Filey copied `%S`. Or drag it here from the tab Filey opened.\n\n"), attached)
                                                : NoTr("Please attach the crash report: drag it here from the tab Filey just opened.\n\n");
                String body = StrFormat(frame, "**Filey version:** %s\n**System:** %S\n\n%S**What were you doing when Filey closed?**\n\n",
                                        FILEY_VERSION, system, details.length ? StrFormat(frame, "```\n%S\n```\n\n", details) : attach);
                String url = StrFormat(frame, "%s?title=%S&body=%S", Prog_IssuesUrl, Prog_UrlEncode(frame, S("Crash: ")), Prog_UrlEncode(frame, body));
                P_FileError error = P_OpenUrl(frame, url);
                if (error.code) Prog_Notify(state, Prog_NoticeKind_Error, S("crash.open"), Tr("Couldn't open the issue page"), error.message);
                // The report's folder in a new tab, the report selected, ready to be dragged onto the issue.
                Prog_OpenTab(state, folder, state->activeTab + 1, true);
                ArenaClear(&state->pickArena);
                state->pickNames = (ArrayStr){0};
                ArrayPush(&state->pickArena, state->pickNames, StrCopy(&state->pickArena, attached.length ? attached : name));
            } break;
            case Prog_CommandKind_NoticeClose: {
                for (u32 i = 0; i < state->noticeCount; ++i) {
                    Prog_Notice *notice = &state->notices[i];
                    if (notice->id != command->row) continue;
                    if (command->kind == Prog_CommandKind_NoticeCopy) {
                        String title = StrMake((const char *)notice->title, notice->titleLength), message = StrMake((const char *)notice->message, notice->messageLength);
                        PlatformClipboardSetText(message.length ? StrFormat(&state->frameArena, "%S: %S", title, message) : title);
                        break;
                    }
                    MemMove(&state->notices[i], &state->notices[i + 1], (state->noticeCount - i - 1) * sizeof(Prog_Notice));
                    --state->noticeCount;
                    break;
                }
            } break;
            case Prog_CommandKind_GitBranchSwitch: {
                if (command->row >= state->branches.header.count) break;
                String branch = StrCopy(&state->frameArena, state->branches.v[command->row]);
                state->branchesOpen = false;
                if (StrIsEqual(branch, state->git.branch)) break;
                // With changes not committed it asks first, as they go along to the other branch.
                if (state->git.changes) Prog__LayoutAsk(state, Prog_LayoutAsk_SwitchBranch, branch);
                else Prog_SwitchBranch(state, branch);
            } break;
            case Prog_CommandKind_ColorPickerCancel: {
                if (state->colorPicking) Prog_PickHex(state, state->pickerOriginal);
                Prog_CloseColorPicker(state);
            } break;
            case Prog_CommandKind_PermissionsSet: {
                // ponytail: chmod on the main thread, a quick call on local disks; a job if a slow network mount ever shows it.
                if (!state->permissionsPath.length) break;
                P_FileError error = P_SetMode(&state->frameArena, state->permissionsPath, (u16)command->target);
                if (error.code) Prog_Notify(state, Prog_NoticeKind_Error, S("permissions.failed"), Tr("Couldn't change permissions"), error.message);
                else Prog_Reload(state);
            } break;
            case Prog_CommandKind_OptionSliderEdit: {
                if (command->row >= Prog_Option_Count) break;
                Prog_BeginSliderEdit(state, (Prog_Option)command->row);
                // Its row takes the highlight, as a press on the row would.
                u8 options[Prog_Option_Count];
                u32 shown = Prog_OptionMatches(state, options);
                for (u32 i = 0; i < shown; ++i) if (options[i] == command->row) state->paletteHighlight = i;
            } break;
            case Prog_CommandKind_ColorHexEdit: {
                Prog_Option option = (Prog_Option)command->row;
                if (Prog_ShownScheme(state, NULL) < Prog_BuiltInSchemes || option < Prog_Option_ColorClear || option > Prog_Option_ColorMatchDisabledSelection) break;
                state->hexOption = (u8)option;
                state->hexEdit = (Prog_TextEdit){0};
                String text = StrFormat(&state->frameArena, "%06X", state->schemeGroups[option - Prog_Option_ColorClear]);
                Prog__EditReplace(&state->hexEdit, (const u8 *)text.value, (u32)text.length);
                state->hexEdit.anchor = state->hexEdit.cursor;
                // Its row takes the highlight, as a press on the row would.
                u8 options[Prog_Option_Count];
                u32 shown = Prog_OptionMatches(state, options);
                for (u32 i = 0; i < shown; ++i) if (options[i] == option) state->paletteHighlight = i;
            } break;
            case Prog_CommandKind_ColorPickerEdit: {
                if (!state->colorPicking || command->row < 1 || command->row > 7) break;
                state->pickerField = (u8)command->row;
                state->pickerEdit = (Prog_TextEdit){0};
                String text = Prog_PickerFieldText(state, &state->frameArena, state->pickerField);
                Prog__EditReplace(&state->pickerEdit, (const u8 *)text.value, (u32)text.length);
                state->pickerEdit.anchor = 0;
            } break;
            case Prog_CommandKind_SchemesOpen: Prog__OpenSchemes(state, (Prog_Option)command->row); break;
            case Prog_CommandKind_SchemesPick: {
                u32 count = 0;
                const String *names = state->schemesOpen ? Prog_OptionChoices(state, (Prog_Option)(state->schemesOpen - 1), &state->frameArena, &count) : NULL;
                if (command->row < count) Prog_SetOptionText(state, (Prog_Option)(state->schemesOpen - 1), names[command->row]);
                state->schemesOpen = 0;
            } break;
            case Prog_CommandKind_SchemeFork: {
                if (state->schemesOpen) Prog_ForkScheme(state, (u32)command->row, (Prog_Option)(state->schemesOpen - 1));
                state->schemesOpen = 0;
            } break;
            case Prog_CommandKind_SchemeDelete: {
                u64 custom = command->row - Min(command->row, (u64)Prog_BuiltInSchemes);
                if (command->row >= Prog_BuiltInSchemes && custom < state->schemeNames.header.count) Prog__LayoutAsk(state, Prog_LayoutAsk_DeleteScheme, state->schemeNames.v[custom]);
                state->schemesOpen = 0;
            } break;
            case Prog_CommandKind_MillerSelect: Prog__MillerSelect(state, (u32)command->target, command->row, command->pressCount >= 2); break;
            case Prog_CommandKind_DeselectAll: {
                for (u64 i = 0; i < state->directory.count; ++i) state->directory.entries[i].selected = false;
                state->selectedCount = 0;
            } break;
            case Prog_CommandKind_SelectAll:
            case Prog_CommandKind_ToggleSelectAll: {
                // All of the inspector's text, after a press on it.
                if (command->kind == Prog_CommandKind_SelectAll && state->previewTextFocus && state->previewShown && !state->previewHex && state->previewText.length) {
                    state->previewAnchor = 0;
                    state->previewCaret = state->previewText.length;
                    break;
                }
                if (!state->loading) {
                    b32 select = command->kind == Prog_CommandKind_SelectAll || state->selectedCount < state->viewCount || !state->viewCount;
                    for (u64 i = 0; i < state->viewCount; ++i) state->directory.entries[state->view[i]].selected = select;
                    state->selectedCount = select ? state->viewCount : 0;
                }
            } break;
            case Prog_CommandKind_ScrollSelectPrev: case Prog_CommandKind_ScrollSelectNext: case Prog_CommandKind_ScrollSelectLeft:
            case Prog_CommandKind_ScrollSelectRight: case Prog_CommandKind_ScrollSelectFirst: case Prog_CommandKind_ScrollSelectLast:
            case Prog_CommandKind_ScrollScrollSelectUp: case Prog_CommandKind_ScrollScrollSelectDown: case Prog_CommandKind_ScrollMoveCursorPrev:
            case Prog_CommandKind_ScrollMoveCursorNext: case Prog_CommandKind_ScrollMoveCursorLeft: case Prog_CommandKind_ScrollMoveCursorRight:
            case Prog_CommandKind_ScrollExtendSelectPrev: case Prog_CommandKind_ScrollExtendSelectNext: case Prog_CommandKind_ScrollExtendSelectLeft:
            case Prog_CommandKind_ScrollExtendSelectRight: case Prog_CommandKind_ScrollExtendSelectFirst: case Prog_CommandKind_ScrollExtendSelectLast:
            case Prog_CommandKind_ScrollScrollExtendSelectUp: case Prog_CommandKind_ScrollScrollExtendSelectDown: {
                // Each command's way, and how it treats the selection, as a click with these modifiers would: Shift extends it, Ctrl moves the
                // file cursor alone. In Commands.txt's order, from ScrollSelectPrev.
                enum { Up, Down, Left, Right, First, Last, PageUp, PageDown };
                local_persist const struct { u8 way; KeyMods how; } moves[] = {
                    { Up, 0 }, { Down, 0 }, { Left, 0 }, { Right, 0 }, { First, 0 }, { Last, 0 }, { PageUp, 0 }, { PageDown, 0 },
                    { Up, KeyMod_Control }, { Down, KeyMod_Control }, { Left, KeyMod_Control }, { Right, KeyMod_Control },
                    { Up, KeyMod_Shift }, { Down, KeyMod_Shift }, { Left, KeyMod_Shift }, { Right, KeyMod_Shift }, { First, KeyMod_Shift }, { Last, KeyMod_Shift },
                    { PageUp, KeyMod_Shift }, { PageDown, KeyMod_Shift },
                };
                static_assert(CArrayCount(moves) == Prog_CommandKind_ScrollScrollExtendSelectDown - Prog_CommandKind_ScrollSelectPrev + 1, "a move for each command");
                u8 way = moves[command->kind - Prog_CommandKind_ScrollSelectPrev].way;
                KeyMods how = moves[command->kind - Prog_CommandKind_ScrollSelectPrev].how;
                // Miller columns go up to the parent with Left and into the focused folder with Right.
                if (state->miller && !how && (way == Left || way == Right) && !state->loading) {
                    if (way == Left) {
                        String up = Prog_UpPath(state->directory.path);
                        if (up.length) Prog__PassToDirectory(state, up);
                    } else if (state->hasFocus && state->focusedRow < state->directory.count) {
                        const Prog_Entry *entry = &state->directory.entries[state->focusedRow];
                        if (entry->file.kind == P_FileKind_Directory || Prog_PreviewsArchive(state, entry)) Prog_OpenRow(state, state->focusedRow);
                    }
                    break;
                }
                // A tree's rows: Right unfolds the focused folder, or goes into its first item once it is open; Left folds it, or goes up to the
                // folder the item is in.
                if (state->directory.tree && !how && (way == Left || way == Right) && !state->loading && Max(state->gridColumns, (u64)1) == 1) {
                    if (!state->hasFocus || state->focusedRow >= state->directory.count) break;
                    String name = state->directory.entries[state->focusedRow].file.name;
                    b32 folder = state->directory.entries[state->focusedRow].file.kind == P_FileKind_Directory, open = folder && Prog_TreeIsOpen(state, name);
                    u64 to = UINT64_MAX;
                    if (way == Right && folder && !open) Prog_TreeToggle(state, name);
                    else if (way == Left && open) Prog_TreeToggle(state, name);
                    else if (way == Right && open && state->focusedRow + 1 < state->directory.count &&
                             StrIsEqual(Prog_EntryParent(state->directory.entries[state->focusedRow + 1].file.name), name)) to = state->focusedRow + 1;
                    else if (way == Left) {
                        String parent = Prog_EntryParent(name);
                        for (u64 i = state->focusedRow; parent.length && i-- > 0;) if (StrIsEqual(state->directory.entries[i].file.name, parent)) { to = i; break; }
                    }
                    if (to != UINT64_MAX && Prog_ViewPosition(state, to) != UINT64_MAX) {
                        Prog_SelectRow(state, to, 0);
                        state->revealFocus = true;
                    }
                    break;
                }
                if (!state->loading && state->viewCount) {
                    // Moves through the rows the filter shows.
                    u64 row = state->hasFocus ? Prog_ViewPosition(state, state->focusedRow) : 0;
                    if (row == UINT64_MAX) row = 0;
                    // In a grid, up and down move a whole line and left and right one item; a line's worth of a page is a line of items.
                    u64 step = Max(state->gridColumns, (u64)1);
                    if ((way == Left || way == Right) && step == 1) break;
                    u64 page = (state->pageRows > 1 ? state->pageRows - 1 : 1) * step;
                    u64 last = state->viewCount - 1;
                    switch (way) {
                        case Up: row = state->groupCount ? Prog__GroupedLineStep(state, row, step, false) : row >= step ? row - step : row; break;
                        // Down from a line with nothing under it goes to the last item, as in Explorer.
                        case Down: {
                            if (!state->hasFocus) break;
                            if (state->groupCount) row = Prog__GroupedLineStep(state, row, step, true);
                            else row = row + step <= last ? row + step : row / step < last / step ? last : row;
                        } break;
                        case Left: if (row) --row; break;
                        case Right: if (state->hasFocus && row < last) ++row; break;
                        case First: row = 0; break;
                        case Last: row = last; break;
                        case PageUp: row = row > page ? row - page : 0; break;
                        case PageDown: row = Min(row + page, last); break;
                        default: InvalidPath;
                    }
                    row = state->view[row];
                    if (how & KeyMod_Control) { state->focusedRow = row; state->hasFocus = true; }
                    else Prog_SelectRow(state, row, how);
                    state->revealFocus = true;
                }
            } break;
            case Prog_CommandKind_ScrollCenterCamera: state->revealFocus = state->centerFocus = true; break;
            case Prog_CommandKind_SelectTyped: {
                if (state->loading || !state->viewCount || !state->typed.length) break;
                // A first character looks from the item after the focused one, so typing it again goes on to the next item it begins.
                String typed = Prog_EditText(&state->typed);
                b32 first = Prog__EditStep(&state->typed, 0, 1) == state->typed.length;
                u64 from = state->hasFocus ? Prog_ViewPosition(state, state->focusedRow) : UINT64_MAX;
                from = from == UINT64_MAX ? 0 : from + (first ? 1 : 0);
                for (u64 i = 0; i < state->viewCount; ++i) {
                    u64 row = state->view[(from + i) % state->viewCount];
                    String name = state->directory.entries[row].file.name;
                    if (name.length < typed.length || !Prog__EqualFolded(StrFromTo(name, 0, (i64)typed.length), typed)) continue;
                    Prog_SelectRow(state, row, 0);
                    state->revealFocus = true;
                    break;
                }
            } break;
            case Prog_CommandKind_ContextMenu:
            case Prog_CommandKind_ContextMenuAt: Prog__OpenMenu(state, command); break;
            case Prog_CommandKind_ContextMenuInvoke: Prog__InvokeMenu(state, (u32)command->row); break;
            case Prog_CommandKind_ContextMenuClose: Prog_CloseMenu(state); break;
            case Prog_CommandKind_ContextMenuExpand: {
                if (state->menuOpen && command->row < state->menu.count) {
                    Prog_MenuToggleExpanded(state, (u32)command->row);
                    state->menuHighlight = (u32)command->row;
                }
            } break;
            // A text command run from the palette works on the box that has the keyboard; Autocomplete is Go To's own.
            case Prog_CommandKind_TextMoveLeft: case Prog_CommandKind_TextExtendLeft: case Prog_CommandKind_TextMoveRight: case Prog_CommandKind_TextExtendRight:
            case Prog_CommandKind_TextMoveLeftWord: case Prog_CommandKind_TextExtendLeftWord: case Prog_CommandKind_TextMoveRightWord:
            case Prog_CommandKind_TextExtendRightWord: case Prog_CommandKind_TextMoveHome: case Prog_CommandKind_TextExtendHome: case Prog_CommandKind_TextMoveEnd:
            case Prog_CommandKind_TextExtendEnd: case Prog_CommandKind_TextDeleteLeft: case Prog_CommandKind_TextDeleteLeftWord: case Prog_CommandKind_TextDeleteRight:
            case Prog_CommandKind_TextDeleteRightWord: case Prog_CommandKind_TextSelectAll: case Prog_CommandKind_TextCopySelection: case Prog_CommandKind_TextCutSelection:
            case Prog_CommandKind_TextPaste: case Prog_CommandKind_TextUppercase: case Prog_CommandKind_TextLowercase: case Prog_CommandKind_TextAutocomplete:
            case Prog_CommandKind_TextUndo: case Prog_CommandKind_TextRedo: case Prog_CommandKind_TextDeleteLeftToSpace: case Prog_CommandKind_TextDeleteToStart: case Prog_CommandKind_TextDeleteToEnd: case Prog_CommandKind_TextSwapCharacters:
            case Prog_CommandKind_TextSwapWords: case Prog_CommandKind_TextUppercaseWord: case Prog_CommandKind_TextLowercaseWord: case Prog_CommandKind_TextCapitalizeWord: {
                Prog_TextEdit *box = state->renaming ? &state->rename : state->filtering ? &state->filter : state->inspectorFiltering ? &state->inspectorFilter : NULL;
                u32 version = box ? box->version : 0;
                if (box && Prog__EditCommand(box, (Prog_CommandKind)command->kind) && box == &state->filter && box->version != version) Prog_ApplyFilter(state, true);
            } break;
            // Go To's key runs its row instead, as Ctrl+Enter does; from the palette it has nothing to select.
            case Prog_CommandKind_GoToSelectInPanel: break;
            case Prog_CommandKind_HotkeyRecord:
            case Prog_CommandKind_HotkeyDeleteAsk: {
                if (command->row <= Prog_CommandKind_None || command->row >= Prog_CommandKind_Count) break;
                state->hotkeyKind = (u16)command->row;
                state->hotkeySlot = (u8)Min(command->target, (u64)UINT8_MAX);
                state->hotkeyAsking = command->kind == Prog_CommandKind_HotkeyDeleteAsk;
                state->hotkeyAskAt = command->at;
            } break;
            case Prog_CommandKind_HotkeyCancel: state->hotkeyKind = Prog_CommandKind_None, state->hotkeyAsking = false; break;
            case Prog_CommandKind_HotkeyBind:
            case Prog_CommandKind_HotkeyDelete:
            case Prog_CommandKind_HotkeyRestore: {
                state->hotkeyKind = Prog_CommandKind_None;
                state->hotkeyAsking = false;
                if (command->row <= Prog_CommandKind_None || command->row >= Prog_CommandKind_Count) break;
                Prog_CommandKind kind = (Prog_CommandKind)command->row;
                // The command's hotkeys with the one rebound in its place, or after them for a new one, or left out.
                const Prog_Hotkey *found[16];
                Prog_Hotkey list[CArrayCount(found) + 1];
                u32 count = Prog_CommandHotkeys(state, kind, found, CArrayCount(found)), n = 0;
                Prog_Hotkey bound = { .key = (Key)command->pressCount, .mods = command->mods, .command = (u16)kind };
                b32 bind = command->kind == Prog_CommandKind_HotkeyBind;
                for (u32 i = 0; i < count; ++i) {
                    if (i != command->target) list[n++] = *found[i];
                    else if (bind) list[n++] = bound;
                }
                if (bind && command->target >= count) list[n++] = bound;
                if (command->kind == Prog_CommandKind_HotkeyRestore) n = Prog_DefaultHotkeysOf(kind, list, CArrayCount(list));
                Prog_SetHotkeys(state, kind, list, n);
            } break;
            case Prog_CommandKind_PaletteQuickToggle: {
                if (command->row <= Prog_CommandKind_None || command->row >= Prog_CommandKind_Count) break;
                // A star adds the command at Quick's end; taken out, it goes back to its own group.
                String name = Prog_Commands[command->row].name;
                b32 was = Prog_IsQuick(state, (Prog_CommandKind)command->row);
                String *names = ArenaPushArray(&state->frameArena, state->quickCommands.header.count + 1, String);
                u64 count = 0;
                for (u64 i = 0; i < state->quickCommands.header.count; ++i) if (!StrIsEqual(state->quickCommands.v[i], name)) names[count++] = state->quickCommands.v[i];
                if (!was) names[count++] = name;
                Prog__SetList(&state->quickArena, &state->quickCommands, names, count);
                Prog__SaveList(state, Prog_QuickCommandsFile, state->quickCommands);
            } break;
            case Prog_CommandKind_MenuEditOpen: {
                // The top level of the menu as the system gave it, each label once, then in the order the editor keeps.
                if (!state->menuOpen || state->menuCommands) break;
                P_ContextMenu base = state->menuBase.items ? state->menuBase : state->menu;
                ArenaClear(&state->menuEditArena);
                state->menuEditLabels = state->menuEditSystem = (ArrayStr){0};
                state->menuEditSubs = ArenaPushArray(&state->menuEditArena, base.rootCount + 1, b8);
                for (u32 i = 0; i < base.rootCount; ++i) {
                    const P_MenuItem *item = &base.items[i];
                    if (item->separator || !item->label.length || Prog__ListPlace(&state->menuEditSystem, item->label) >= 0) continue;
                    state->menuEditSubs[state->menuEditSystem.header.count] = item->childCount != 0;
                    ArrayPush(&state->menuEditArena, state->menuEditSystem, StrCopy(&state->menuEditArena, item->label));
                }
                u64 total = state->menuEditSystem.header.count;
                String *ordered = ArenaPushArray(&state->frameArena, total + 1, String);
                b8 *subs = ArenaPushArray(&state->frameArena, total + 1, b8);
                u64 at = 0;
                for (u64 o = 0; o < state->menuOrder.header.count; ++o) {
                    i64 found = Prog__ListPlace(&state->menuEditSystem, state->menuOrder.v[o]);
                    if (found >= 0 && Prog__ListPlace(&(ArrayStr){ .header.count = at, .v = ordered }, state->menuOrder.v[o]) < 0) subs[at] = state->menuEditSubs[found], ordered[at++] = state->menuEditSystem.v[found];
                }
                for (u64 i = 0; i < total; ++i) {
                    if (Prog__ListPlace(&(ArrayStr){ .header.count = at, .v = ordered }, state->menuEditSystem.v[i]) < 0) subs[at] = state->menuEditSubs[i], ordered[at++] = state->menuEditSystem.v[i];
                }
                for (u64 i = 0; i < at; ++i) {
                    ArrayPush(&state->menuEditArena, state->menuEditLabels, ordered[i]);
                    state->menuEditSubs[i] = subs[i];
                }
                state->menuEditAt = state->menuRects[0];
                state->menuEditDrag = 0;
                Prog_CloseMenu(state);
                state->menuEditing = at != 0;
            } break;
            case Prog_CommandKind_MenuEditClose: state->menuEditing = false, state->menuEditDrag = 0; break;
            case Prog_CommandKind_MenuEditToggle: {
                if (!state->menuEditing || command->row >= state->menuEditLabels.header.count) break;
                String label = state->menuEditLabels.v[command->row];
                String *hidden = ArenaPushArray(&state->frameArena, state->menuHidden.header.count + 1, String);
                u64 count = 0;
                b32 was = Prog__ListPlace(&state->menuHidden, label) >= 0;
                for (u64 i = 0; i < state->menuHidden.header.count; ++i) if (!StrIsEqual(state->menuHidden.v[i], label)) hidden[count++] = state->menuHidden.v[i];
                if (!was) hidden[count++] = label;
                Prog__SetList(&state->menuHiddenArena, &state->menuHidden, hidden, count);
                Prog__SaveList(state, Prog_MenuHiddenFile, state->menuHidden);
            } break;
            case Prog_CommandKind_MenuEditMove: {
                u64 total = state->menuEditLabels.header.count, from = command->row, to = Min(command->target, total);
                if (!state->menuEditing || from >= total || to == from || to == from + 1) break;
                String label = state->menuEditLabels.v[from];
                b8 sub = state->menuEditSubs[from];
                if (to > from) --to;
                for (u64 i = from; i + 1 < total; ++i) state->menuEditLabels.v[i] = state->menuEditLabels.v[i + 1], state->menuEditSubs[i] = state->menuEditSubs[i + 1];
                for (u64 i = total - 1; i > to; --i) state->menuEditLabels.v[i] = state->menuEditLabels.v[i - 1], state->menuEditSubs[i] = state->menuEditSubs[i - 1];
                state->menuEditLabels.v[to] = label, state->menuEditSubs[to] = sub;
                // The whole top level in its new order, which the items of other menus keep places around.
                Prog__SetList(&state->menuOrderArena, &state->menuOrder, state->menuEditLabels.v, total);
                Prog__SaveList(state, Prog_MenuOrderFile, state->menuOrder);
            } break;
            case Prog_CommandKind_MenuEditReset: {
                if (!state->menuEditing) break;
                Prog__SetList(&state->menuHiddenArena, &state->menuHidden, NULL, 0);
                Prog__SetList(&state->menuOrderArena, &state->menuOrder, NULL, 0);
                Prog__SaveList(state, Prog_MenuHiddenFile, state->menuHidden);
                Prog__SaveList(state, Prog_MenuOrderFile, state->menuOrder);
                // The list in the system's order; its submenus' marks follow the labels.
                u64 total = state->menuEditSystem.header.count;
                b8 *subs = ArenaPushArray(&state->frameArena, total + 1, b8);
                for (u64 i = 0; i < total; ++i) {
                    i64 was = Prog__ListPlace(&state->menuEditLabels, state->menuEditSystem.v[i]);
                    subs[i] = was >= 0 ? state->menuEditSubs[was] : false;
                }
                for (u64 i = 0; i < total; ++i) state->menuEditLabels.v[i] = state->menuEditSystem.v[i], state->menuEditSubs[i] = subs[i];
            } break;
            case Prog_CommandKind_MenuPinToggle: {
                if (state->menuOpen && command->row < state->menu.count) {
                    String label = StrCopy(&state->frameArena, state->menu.items[command->row].label);
                    String *pins = ArenaPushArray(&state->frameArena, state->menuPins.header.count + 1, String);
                    u64 count = 0;
                    b32 was = Prog_MenuPinned(state, label);
                    for (u64 i = 0; i < state->menuPins.header.count; ++i) if (!StrIsEqual(state->menuPins.v[i], label)) pins[count++] = state->menuPins.v[i];
                    if (!was) pins[count++] = label;
                    Prog__SetList(&state->menuPinArena, &state->menuPins, pins, count);
                    Prog__SaveList(state, Prog_MenuPinsFile, state->menuPins);
                    // The menu takes its new order at once, the item sliding to its place, and nothing is highlighted until the pointer moves.
                    state->menuMoved = StrCopy(&state->menuArena, label);
                    Prog__ApplyMenuPins(state);
                    state->menuHighlight = UINT32_MAX;
                    state->menuDepth = 0;
                }
            } break;
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
                Prog_Transfer *transfer = command->row < Prog_TransferSlots ? &state->transfers[command->row] : NULL;
                if (transfer && transfer->job) P_RequestCancel(&transfer->job->cancelToken);
            } break;
            case Prog_CommandKind_TransferReveal: {
                Prog_Transfer *transfer = command->row < Prog_TransferSlots ? &state->transfers[command->row] : NULL;
                if (!transfer || !transfer->job || transfer->opening) break;
                // What it writes there: what a copy or move brings, or what it reads beside its target, as a torrent's .torrent file or a video
                // being compressed; a download's file has no name yet.
                const Prog_JobInput *input = transfer->job->input;
                String first = input->pathCount ? input->paths[0] : (String){0};
                b32 shown = first.length && (input->transferOp == Prog_TransferOp_None || Prog__SamePath(Prog_ParentPath(first), input->path));
                Prog__Reveal(state, input->path, shown ? Prog__FolderName(first) : (String){0}, input->transferOp == Prog_TransferOp_Torrent);
            } break;
            case Prog_CommandKind_TorrentReveal: {
                if (command->row >= state->torrents.header.count) break;
                String line = state->torrents.v[command->row];
                String path = StrFromTo(line, 0, (i64)StrIndexChar(line, '\t', SSF_CountOnFail).value);
                Prog__Reveal(state, Prog_ParentPath(path), Prog__FolderName(path), true);
            } break;
            case Prog_CommandKind_TransferPause: {
                Prog_Transfer *transfer = command->row < Prog_TransferSlots ? &state->transfers[command->row] : NULL;
                if (transfer && transfer->job) atomic_store(&transfer->progress.paused, !atomic_load(&transfer->progress.paused));
            } break;
            case Prog_CommandKind_CommandPalette:
            case Prog_CommandKind_QuickCommandsOpen: {
                Prog_CloseMenu(state);
                b32 quick = command->kind == Prog_CommandKind_QuickCommandsOpen;
                state->paletteOpen = !state->paletteOpen || state->paletteMode != Prog_PaletteMode_Commands || state->paletteQuick != quick;
                state->paletteMode = Prog_PaletteMode_Commands;
                state->paletteQuick = (b8)quick;
                state->palette = (Prog_TextEdit){0};
                state->paletteHighlight = 0;
                state->paletteScroll = 0;
                state->paletteWheeled = false;
            } break;
            case Prog_CommandKind_GoTo:
            case Prog_CommandKind_EditPath: {
                Prog_CloseMenu(state);
                b32 edit = command->kind == Prog_CommandKind_EditPath;
                if (!edit) Prog_CheckRecents(state);
                state->gotoDrives = false;
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
            case Prog_CommandKind_PaletteClose: {
                // The font dropdown closes back to Options.
                if (state->paletteOpen && state->paletteMode == Prog_PaletteMode_Fonts) Prog__CloseFontPicker(state);
                else state->paletteOpen = false;
            } break;
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
                    if ((command->mods & KeyMod_Control) && parent.length && !StrIsEqual(parent, item.path) && Prog_SelectNext(state, name)) {
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
                    if (desc->control == Prog_OptionControl_Font) Prog__OpenFontPicker(state, option);
                    if (desc->control == Prog_OptionControl_Expand) state->colorsExpanded = !state->colorsExpanded;
                    if (desc->control == Prog_OptionControl_Color) Prog__OpenColorPicker(state, option);
                    if (desc->control == Prog_OptionControl_Muted) Prog_QueueCommand(state, Prog_CommandKind_MutedNoticesOpen, 0, 0, UINT64_MAX);
                    u32 choices = 0;
                    Prog_OptionChoices(state, option, &state->frameArena, &choices);
                    // A scheme opens the schemes' dropdown.
                    b32 schemes = option == Prog_Option_ColorScheme || option == Prog_Option_LightScheme || option == Prog_Option_DarkScheme;
                    if (desc->control == Prog_OptionControl_Choice && schemes) Prog__OpenSchemes(state, option);
                    else if (desc->control == Prog_OptionControl_Choice && choices) Prog_SetOption(state, option, (value + 1) % (i32)choices);
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
                if (state->paletteMode == Prog_PaletteMode_Fonts) {
                    u32 *found = ArenaPushArray(&state->frameArena, state->fontCount + 1, u32);
                    u32 count = Prog_FontMatches(state, found);
                    u64 chosen = command->row == UINT64_MAX ? state->paletteHighlight : command->row;
                    if (chosen >= count) break;
                    // The default's own family clears the setting, which keeps it the default.
                    Prog_Option option = (Prog_Option)state->fontOption;
                    String name = state->fontNames[found[chosen]];
                    Prog_SetOptionText(state, option, Prog__EqualFolded(name, Prog__FontDefault(state, option)) ? (String){0} : name);
                    Prog__CloseFontPicker(state);
                    break;
                }
                if (state->paletteMode == Prog_PaletteMode_Layouts) {
                    u32 found[Prog_MaxLayouts];
                    u32 count = Prog_LayoutMatches(state, found);
                    u64 chosen = command->row == UINT64_MAX ? state->paletteHighlight : command->row;
                    state->paletteOpen = false;
                    if (chosen < count) Prog_ApplyLayout(state, StrCopy(&state->frameArena, state->layoutNames.v[found[chosen]]));
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
            case Prog_CommandKind_ColumnShiftLeft:
            case Prog_CommandKind_ColumnShiftRight:
            case Prog_CommandKind_ColumnClose:
            case Prog_CommandKind_ColumnToggle:
            case Prog_CommandKind_ColumnMove:
            case Prog_CommandKind_ColumnResize:
            case Prog_CommandKind_ColumnsRestore: Prog_EditColumns(state, command->kind, command->row, command->target); break;
            // Fitting measures the values, which the next build of the header does.
            case Prog_CommandKind_ColumnFit: state->fitColumn = (u32)command->row + 1; break;
            case Prog_CommandKind_SizeAllColumnsToFit: state->fitColumn = UINT32_MAX; break;
            case Prog_CommandKind_ColumnMenu:
            case Prog_CommandKind_ViewMenu:
            case Prog_CommandKind_HistoryMenu:
            case Prog_CommandKind_MainMenu:
            case Prog_CommandKind_PanelMenu:
            case Prog_CommandKind_PathMenu:
            case Prog_CommandKind_GroupMenu:
            case Prog_CommandKind_TabMenu: {
                // From a hotkey they open where their button or tab would open them, the path's for the shown folder.
                if (command->at.x == 0 && command->at.y == 0 && command->kind != Prog_CommandKind_PanelMenu) {
                    command->at = command->kind == Prog_CommandKind_TabMenu ? state->tabMenuAt : command->kind == Prog_CommandKind_PathMenu ? state->pathMenuAt : state->mainMenuAt;
                    if (command->kind == Prog_CommandKind_PathMenu) Prog__CrumbsPath(state, &command->row);
                }
                Prog__OpenProgramMenu(state, command);
            } break;
            case Prog_CommandKind_SelectionInspect: {
                if (state->loading) break;
                Scratch scratch = GetScratch();
                u64 count = 0;
                String *names = ArenaPushArray(scratch.arena, Max(state->directory.count, (u64)1), String);
                for (u64 i = 0; i < state->directory.count; ++i) {
                    b32 wanted = state->selectedCount ? state->directory.entries[i].selected : state->hasFocus && i == state->focusedRow;
                    if (wanted) names[count++] = state->directory.entries[i].file.name;
                }
                // Without a properties window of the system's, the inspector shows the details.
                if (count && !P_ShowProperties(state->directory.path, names, count)) {
                    state->previewShown = true;
                    state->inspectorDetails = true;
                }
                ReleaseScratch(scratch);
            } break;
            case Prog_CommandKind_ConsoleOpen: {
                P_FileError error = state->directory.path.length && !StrIsEqual(state->directory.path, Prog_ThisPC) ? P_OpenConsole(&state->frameArena, state->directory.path) : (P_FileError){0};
                if (error.code) {
                    Prog_Notify(state, Prog_NoticeKind_Error, S("console.failed"), Tr("Couldn't open the console"), error.message);
                }
            } break;
            case Prog_CommandKind_ToggleFullscreen: PlatformWindowToggleFullscreen(); break;
            case Prog_CommandKind_HistoryGo: {
                if (command->row < state->history.header.count) Prog_RequestDirectory(state, state->history.v[command->row], false, command->row);
            } break;
            case Prog_CommandKind_NewWindow: {
                P_FileError error = Prog_OpenWindow(state, Prog_TabPath(state, state->activeTab), Prog__TabTraits(state, state->activeTab));
                if (error.code) {
                    Prog_Notify(state, Prog_NoticeKind_Error, S("window.failed"), Tr("Couldn't open a new window"), error.message);
                }
            } break;
            case Prog_CommandKind_OpenOptions: {
                Prog_CloseMenu(state);
                state->paletteOpen = !(state->paletteOpen && state->paletteMode == Prog_PaletteMode_Options);
                state->paletteMode = Prog_PaletteMode_Options;
                state->palette = (Prog_TextEdit){0};
                state->paletteHighlight = 0;
                state->paletteScroll = 0;
                state->paletteWheeled = false;
                state->paletteKeyed = false;
            } break;
            case Prog_CommandKind_OptionSet: {
                if (command->pressCount) Prog_PreviewOption(state, (Prog_Option)command->row, (i32)(i64)command->target);
                else Prog_SetOption(state, (Prog_Option)command->row, (i32)(i64)command->target);
            } break;
            case Prog_CommandKind_OptionRestore: if (command->row < Prog_Option_Count) Prog_RestoreOption(state, (Prog_Option)command->row); break;
            case Prog_CommandKind_WindowMove: state->moveRequested = true; break;
            case Prog_CommandKind_WindowMinimize: PlatformWindowMinimize(); break;
            case Prog_CommandKind_WindowToggleMaximize: PlatformWindowToggleMaximize(); break;
            case Prog_CommandKind_WindowClose: PlatformWindowClose(); break;
            case Prog_CommandKind_Rename: if (state->hasFocus) Prog_BeginRename(state, state->focusedRow); break;
            case Prog_CommandKind_RenameCommit: Prog__EndRename(state, true); break;
            case Prog_CommandKind_RenamePrev:
            case Prog_CommandKind_RenameNext: {
                if (!state->renaming) break;
                // The row being renamed, by its name, since a reload after the last rename may have moved it.
                String renamed = StrMake((const char *)state->renameFrom, state->renameFromLength);
                u64 at = UINT64_MAX;
                for (u64 i = 0; i < state->viewCount && at == UINT64_MAX; ++i) {
                    if (StrIsEqual(state->directory.entries[state->view[i]].file.name, renamed)) at = i;
                }
                Prog__EndRename(state, true);
                if (at == UINT64_MAX) break;
                b32 next = command->kind == Prog_CommandKind_RenameNext;
                if (next ? at + 1 >= state->viewCount : at == 0) break;
                u64 row = state->view[next ? at + 1 : at - 1];
                Prog_SelectRow(state, row, 0);
                Prog_BeginRename(state, row);
            } break;
            case Prog_CommandKind_RenameInsertIncrementalID:
            case Prog_CommandKind_RenameInsertCurrentDate:
            case Prog_CommandKind_RenameInsertCreateDate:
            case Prog_CommandKind_RenameInsertModifiedDate: Prog__RenameInsert(state, command->kind); break;
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
                // Both toggle: the search ends as it started, back to the folder's own items with the filter kept.
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
            case Prog_CommandKind_TypesPopupOpen: {
                // Its hotkey closes it again.
                if (state->typesOpen || state->loading) {
                    state->typesOpen = false;
                    break;
                }
                Prog_CloseMenu(state);
                state->typesOpen = true;
                state->typesSearch.length = state->typesSearch.cursor = state->typesSearch.anchor = 0;
                ++state->typesSearch.version;
                state->typesHighlight = 0;
                state->typesListing = 0;
                Prog_GatherTypes(state);
            } break;
            case Prog_CommandKind_TypesPopupClose: state->typesOpen = false; break;
            case Prog_CommandKind_TypesToggle:
            case Prog_CommandKind_TypesToggleAll:
            case Prog_CommandKind_TypesUntoggleAll: {
                // Checked types are the ones shown; none checked shows every type.
                if (command->kind == Prog_CommandKind_TypesToggle && command->row < state->typeCount) Prog_ToggleType(&state->types, state->typeNames[command->row]);
                if (command->kind != Prog_CommandKind_TypesToggle) state->types.length = 0;
                for (u32 t = 0; command->kind == Prog_CommandKind_TypesToggleAll && t < state->typeCount; ++t) Prog_ToggleType(&state->types, state->typeNames[t]);
                Prog_ApplyFilter(state, false);
                if (command->target) state->typesOpen = false;
            } break;
            case Prog_CommandKind_FilesPopupOpen:
            case Prog_CommandKind_FilesPopupPrev:
            case Prog_CommandKind_FilesPopupNext: {
                String path;
                u64 shownEnd, ends[128];
                u32 count = state->loading ? 0 : Prog_CrumbEnds(state, &path, &shownEnd, ends, CArrayCount(ends)), open = UINT32_MAX, want = UINT32_MAX;
                for (u32 i = 0; state->filesOpen && i < count; ++i) if (StrIsEqual(Prog__CrumbFolder(path, ends[i]), state->filesFolder)) open = i;
                if (command->kind == Prog_CommandKind_FilesPopupOpen) {
                    // Its hotkey, or a click on the separator after the open crumb, closes it again.
                    for (u32 i = 0; i < count && want == UINT32_MAX; ++i) if (ends[i] == (command->target ? command->target : shownEnd)) want = i;
                    if (!command->target && want == UINT32_MAX && count) want = count - 1;
                    if (state->filesOpen && (!command->target || want == open)) {
                        want = UINT32_MAX;
                        Prog__CloseFilesPopup(state);
                    }
                } else if (open != UINT32_MAX) {
                    want = command->kind == Prog_CommandKind_FilesPopupPrev ? (open ? open - 1 : UINT32_MAX) : (open + 1 < count ? open + 1 : UINT32_MAX);
                }
                if (want != UINT32_MAX) Prog__OpenFilesPopup(state, path, ends, count, want);
            } break;
            case Prog_CommandKind_FilesPopupActivate: {
                if (!state->filesOpen || !state->filesListed) break;
                u32 *rows = ArenaPushArray(&state->frameArena, Max(state->filesCount, (u64)1), u32);
                u32 found = Prog_FilesMatches(state, rows);
                u64 row = command->row != UINT64_MAX ? command->row : state->filesHighlight < found ? rows[state->filesHighlight] : UINT64_MAX;
                if (row >= state->filesCount) break;
                const P_File *file = &state->filesItems[row].file;
                Prog_GoToItem item = { .path = Prog_JoinPath(&state->frameArena, state->filesFolder, file->name), .file = file->kind != P_FileKind_Directory };
                Prog__CloseFilesPopup(state);
                Prog__GoToOpen(state, item);
            } break;
            case Prog_CommandKind_FilesPopupClose: Prog__CloseFilesPopup(state); break;
            case Prog_CommandKind_ToggleFolders:
            case Prog_CommandKind_ToggleFiles: {
                if (command->kind == Prog_CommandKind_ToggleFolders) state->hideFolders = !state->hideFolders;
                else state->hideFiles = !state->hideFiles;
                Prog_ApplyFilter(state, false);
            } break;
            case Prog_CommandKind_CopySelection: {
                // The inspector's selected text, while it has some, rather than the files.
                String selected;
                if (Prog_InspectorSelection(state, &state->frameArena, &selected)) PlatformClipboardSetText(selected);
                else Prog__FileCommand(state, command->kind);
            } break;
            case Prog_CommandKind_CutSelection:
            case Prog_CommandKind_Paste:
            case Prog_CommandKind_Trash:
            case Prog_CommandKind_SelectionDelete:
            case Prog_CommandKind_Duplicate:
            case Prog_CommandKind_CompressZip:
            case Prog_CommandKind_Compress7z:
            case Prog_CommandKind_CompressVideo:
            case Prog_CommandKind_DownloadTorrent:
            case Prog_CommandKind_ExtractHere:
            case Prog_CommandKind_ExtractToFolder:
            case Prog_CommandKind_CopyAsPath:
            case Prog_CommandKind_CopyAsPathSmart:
            case Prog_CommandKind_PasteLink:
            case Prog_CommandKind_CreateFolder:
            case Prog_CommandKind_CreateFile: Prog__FileCommand(state, command->kind); break;
            case Prog_CommandKind_InspectorPlay: {
                // The shown audio file plays, or pauses and goes on where it is; one played to its end starts again.
                if (state->focusedRow >= state->directory.count || !Prog_IsAudio(state->directory.entries[state->focusedRow].file.name)) break;
                String path = Prog_JoinPath(&state->frameArena, state->directory.path, state->directory.entries[state->focusedRow].file.name);
                P_Progress *progress = &state->playerProgress[state->playerSlot];
                if (state->playerJob && state->playerHash == MapHashBytes(path.value, path.length)) atomic_store(&progress->paused, !atomic_load(&progress->paused));
                else Prog__StartPlayer(state, path, 0);
            } break;
            case Prog_CommandKind_InspectorMute:
            case Prog_CommandKind_InspectorVolume: {
                if (command->kind == Prog_CommandKind_InspectorMute) state->audioMuted = !state->audioMuted;
                else {
                    state->audioMuted = false;
                    Prog_SetOption(state, Prog_Option_AudioVolume, (i32)Min(command->target, (u64)100));
                }
                atomic_store(&state->playerVolume, state->audioMuted ? 0u : state->audioVolume);
            } break;
            case Prog_CommandKind_TorrentDrop: {
                if (command->row >= state->torrents.header.count) break;
                String line = state->torrents.v[command->row];
                Index tab = StrIndexChar(line, '\t', SSF_CountOnFail);
                String path = StrCopy(&state->frameArena, StrFromTo(line, 0, (i64)tab.value));
                if (Prog_TorrentSlot(state, path) == UINT32_MAX) Prog__NoteTorrent(state, path, (String){0}, false);
            } break;
            case Prog_CommandKind_InspectorTorrentToggle: {
                // All of them untick once they all are ticked, and tick otherwise.
                const Prog_TorrentInfo *torrent = &state->torrent;
                if (!state->torrentSkip) break;
                if (command->row < torrent->fileCount) {
                    state->torrentSkip[command->row] = !state->torrentSkip[command->row];
                } else if (command->row == UINT64_MAX) {
                    b32 all = true;
                    for (u32 i = 0; i < torrent->fileCount; ++i) all &= torrent->files[i].pad || !state->torrentSkip[i];
                    for (u32 i = 0; i < torrent->fileCount; ++i) if (!torrent->files[i].pad) state->torrentSkip[i] = (b8)all;
                }
            } break;
            case Prog_CommandKind_InspectorSeek: {
                if (state->focusedRow >= state->directory.count || !Prog_IsAudio(state->directory.entries[state->focusedRow].file.name)) break;
                Prog__StartPlayer(state, Prog_JoinPath(&state->frameArena, state->directory.path, state->directory.entries[state->focusedRow].file.name), command->target);
            } break;
            case Prog_CommandKind_InspectorVideoCodec: Prog_SetOption(state, Prog_Option_VideoCodec, (i32)command->row); break;
            case Prog_CommandKind_InspectorVideoQuality: Prog_SetOption(state, Prog_Option_VideoQuality, (i32)state->videoQuality + (command->row ? 1 : -1)); break;
            case Prog_CommandKind_SelectionRestore: {
                // Only the bin's items have somewhere to go back to.
                if (state->loading || !P_IsTrashFolder(state->directory.path)) break;
                String *paths = NULL;
                u64 count = Prog__SelectedPaths(state, &state->frameArena, &paths);
                if (count) Prog__FileJob(state, Prog_JobKind_Restore, paths, count, state->directory.path, false, (String){0});
            } break;
            case Prog_CommandKind_TrashRestoreAll: {
                // The bin shown, else the system drive's.
                String bin = P_IsTrashFolder(state->directory.path) ? state->directory.path : P_TrashFolder(&state->frameArena);
                Prog__FileJob(state, Prog_JobKind_Restore, NULL, 0, bin, false, (String){0});
            } break;
            case Prog_CommandKind_EmptyTrash: {
                // Asked first, as a permanent delete is: by the system where it asks itself, else here.
                if (P_EmptyTrashAsks() || state->noTrashConfirm) {
                    String bin = P_TrashFolder(&state->frameArena);
                    Prog__FileJob(state, Prog_JobKind_EmptyTrash, &bin, 1, bin, !state->noTrashConfirm, (String){0});
                    break;
                }
                ArenaClear(&state->confirmArena);
                state->confirmPaths = ArenaPushArray(&state->confirmArena, 1, String);
                state->confirmPaths[0] = P_TrashFolder(&state->confirmArena);
                state->confirmCount = 1;
                state->confirmEmptyTrash = state->confirmingDelete = true;
                state->filtering = false;
            } break;
            case Prog_CommandKind_DeleteConfirm:
            case Prog_CommandKind_DeleteCancel: {
                if (state->confirmingDelete && command->kind == Prog_CommandKind_DeleteConfirm) {
                    Prog__FileJob(state, state->confirmEmptyTrash ? Prog_JobKind_EmptyTrash : Prog_JobKind_Delete, state->confirmPaths, state->confirmCount,
                                  state->confirmEmptyTrash ? state->confirmPaths[0] : state->directory.path, false, (String){0});
                }
                state->confirmingDelete = state->confirmEmptyTrash = false;
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
    // A view of another kind, as a zoom to the icons, shows a tree's nested items or leaves them out.
    if (state->directory.tree && state->treeNested != Prog_TreeShowsNested(state)) Prog_ApplyFilter(state, false);
    // Files dragged onto the window go into the folder under the pointer, else into the folder of the panel under it.
    u32 dropPanel = Prog_PanelIndex(state, state->hasHoverRow ? state->hoverPanel : state->pointerPanel);
    if (dropPanel != UINT32_MAX) Prog__UsePanel(state, dropPanel);
    String drop = state->loading ? (String){0} : state->directory.path;
    b32 intoPanel = drop.length && !StrIsEqual(drop, Prog_ThisPC), onFile = false;
    if (!state->loading && state->hasHoverRow && state->hoverRow < state->directory.count) {
        const Prog_Entry *entry = &state->directory.entries[state->hoverRow];
        // A dragged folder cannot take itself; a program or a script takes the files to start with, as in Explorer.
        b32 runs = Prog__RunsDropped(entry) && !state->directory.archive;
        if ((entry->file.kind == P_FileKind_Directory || runs) && !(state->dragging && entry->selected)) {
            drop = Prog_JoinPath(&state->frameArena, state->directory.path, entry->file.name);
            intoPanel = false;
            onFile = runs;
        }
    }
    Prog__UsePanel(state, state->activePanel);
    if (state->dropPlace.length) drop = state->dropPlace, intoPanel = false, onFile = false;
    String archive = {0}, inner = {0};
    if (Prog_ArchiveSplit(drop, &archive, &inner) && !P_ArchiveWritable(archive)) drop = (String){0}, intoPanel = false;
    P_SetDropOnFile(onFile);
    P_SetDropDirectory(drop);
    state->dropPanel = intoPanel && dropPanel != UINT32_MAX && P_DropPending() ? state->panels[dropPanel].id : 0;
    // The Types popup lists the active panel's types, the new ones once the listing changes under it.
    if (state->typesOpen) Prog_GatherTypes(state);
    // The folder dropdown closes once its crumb is gone from the path bar, as after going to another folder.
    if (state->filesOpen && !state->loading) {
        String path;
        u64 shownEnd, ends[128];
        u32 count = Prog_CrumbEnds(state, &path, &shownEnd, ends, CArrayCount(ends));
        b32 there = false;
        for (u32 i = 0; i < count && !there; ++i) there = StrIsEqual(Prog__CrumbFolder(path, ends[i]), state->filesFolder);
        if (!there) Prog__CloseFilesPopup(state);
    }
    // Files dropped where the system leaves them to Filey go as a paste does, with its progress, questions about taken names and undo.
    String dropFolder = {0};
    b32 dropMove = false;
    ArrayStr dropped = P_TakeDroppedFiles(&state->frameArena, &dropFolder, &dropMove);
    if (dropped.header.count) {
        Prog__FileJob(state, Prog_JobKind_Transfer, dropped.v, dropped.header.count, dropFolder, dropMove, (String){0});
        changed = true;
    }
    // What the system's shell dropped into a panel's folder, as items dragged out of an archive, is selected once its listing holds it, as a
    // paste's is.
    ArrayStr landed = P_TakeShellDropped(&state->frameArena, &dropFolder);
    if (landed.header.count) {
        u32 current = state->currentPanel;
        for (u32 panel = 0; panel < state->panelCount; ++panel) {
            Prog__UsePanel(state, panel);
            if (!Prog__SamePath(state->directory.path, dropFolder)) continue;
            ArenaClear(&state->pickArena);
            state->pickNames = (ArrayStr){0};
            for (u64 i = 0; i < landed.header.count; ++i) ArrayPush(&state->pickArena, state->pickNames, StrCopy(&state->pickArena, landed.v[i]));
        }
        Prog__UsePanel(state, current);
        changed = true;
    }
    // Folders dropped on the bookmarks, or on a group of the user's, go where the last build showed the drop would go.
    ArrayStr pinned = P_TakeDroppedFolders(&state->frameArena);
    if (pinned.header.count) {
        if (state->userGroupDropIn) Prog_UserGroupAdd(state, state->userGroupDropIn - 1, &pinned, state->userGroupDrop - 1);
        else Prog_PinBookmarks(state, &pinned, state->bookmarkDrop ? state->bookmarkDrop - 1 : state->bookmarks.header.count);
        changed = true;
    }
    state->bookmarkDrop = state->bookmarkDropShown;
    state->userGroupDrop = state->userGroupDropShown;
    state->userGroupDropIn = state->userGroupDropShownIn;
    P_SetDropTakesFolders(state->bookmarkDrop != 0 || state->userGroupDropIn != 0);
    // The schemes' dropdown goes with the Options it opened from.
    if (!state->paletteOpen || state->paletteMode != Prog_PaletteMode_Options) state->schemesOpen = 0, state->mutedOpen = false;
    Prog_SubmitIconRequests(state);
    // Each panel's shown tab previews its own item, or its folder's list's while the keyboard is in that list.
    for (u32 i = 0; i < state->panelCount; ++i) {
        if (state->panels[i].inspectorOf) continue;
        b32 inList = state->panels[state->activePanel].inspectorOf == state->panels[i].id;
        Prog__UsePanel(state, inList ? state->activePanel : i);
        Prog__UpdatePreview(state);
    }
    Prog__UsePanel(state, state->activePanel);
    Prog_KeepLockedView(state);
    for (u32 i = 0; i < state->noticeCount;) {
        if (Prog_NoticeLeft(&state->notices[i], PlatformNowMicroseconds())) {
            ++i;
            continue;
        }
        MemMove(&state->notices[i], &state->notices[i + 1], (state->noticeCount - i - 1) * sizeof(Prog_Notice));
        --state->noticeCount;
        changed = true;
    }
    // Cut files stay dimmed only while the clipboard still holds them.
    if (state->cutPaths.header.count && PlatformClipboardSequence() != state->cutSequence) {
        ArenaClear(&state->cutArena);
        state->cutPaths = (typeof(state->cutPaths)){0};
        changed = true;
    }
    // The recents are checked for folders gone at the start and every minute after; opening Go To checks them at once.
    if (!state->recentsCheckTime || PlatformNowMicroseconds() - state->recentsCheckTime > Millions(60)) Prog_CheckRecents(state);
    // Transfers redraw while they run, for their progress and questions.
    b32 transferring = false;
    for (u32 i = 0; i < Prog_TransferSlots; ++i) transferring |= state->transfers[i].job != NULL;
    return changed || transferring || Prog__AnyMeasuring(state);
}

// The types of the shown listing for the Types popup, in the order its items first show them, gathered again once it is another listing.
// ponytail: compares each item's type with those found so far, which is quick for the few types a folder has.
function void Prog_GatherTypes(Prog_State *state)
{
    u64 listing = (u64)(uintptr_t)state->directory.entries ^ state->directory.count ^ (state->listing << 40);
    if (state->typesListing == listing && state->typeNames) return;
    state->typesListing = listing;
    ArenaClear(&state->typesArena);
    u64 capacity = Max(Min(state->directory.count, (u64)4096), (u64)1);
    state->typeNames = ArenaPushArray(&state->typesArena, capacity, String);
    state->typeCounts = ArenaPushArray(&state->typesArena, capacity, u64);
    state->typeFirsts = ArenaPushArray(&state->typesArena, capacity, u64);
    state->typeCount = 0;
    u32 last = 0;
    for (u64 i = 0; i < state->directory.count; ++i) {
        String type = state->directory.entries[i].type;
        // Items of a type often come together, so the last one found is tried first.
        u32 at = last < state->typeCount && StrIsEqual(state->typeNames[last], type) ? last : UINT32_MAX;
        for (u32 t = 0; t < state->typeCount && at == UINT32_MAX; ++t) if (StrIsEqual(state->typeNames[t], type)) at = t;
        if (at == UINT32_MAX && state->typeCount < capacity) {
            at = state->typeCount++;
            state->typeNames[at] = StrCopy(&state->typesArena, type);
            state->typeFirsts[at] = i;
        }
        if (at != UINT32_MAX) state->typeCounts[at] += 1, last = at;
    }
}

// The types the Types popup lists for what is typed in its box, by their place among Prog_State.typeNames, into `out`; returns how many.
function u32 Prog_TypeMatches(const Prog_State *state, u32 *out)
{
    String typed = Prog_EditText(&state->typesSearch);
    u32 count = 0;
    for (u32 t = 0; t < state->typeCount; ++t) {
        if (!typed.length || Prog_FuzzyScore(state->typeNames[t], (String){0}, typed, NULL, NULL) > 0) out[count++] = t;
    }
    return count;
}

// What a text box types rather than hands to the hotkeys: a character key, and Space or '/' without modifiers.
function b32 Prog__TypesInBox(Key key, KeyMods mods)
{
    mods &= KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super;
    return Prog_IsCharacterKey(key, mods) || ((key == Key_Space || key == Key_Slash) && !mods);
}

// Select Typed Item: adds `codepoint` to what was typed, which starts over after a pause, and queues the selection of the item it begins.
function void Prog__TypeToSelect(Prog_State *state, rune codepoint, u64 order)
{
    u64 now = PlatformNowMicroseconds();
    if (now - Min(state->typedTime, now) > 1000000) state->typed.length = state->typed.cursor = state->typed.anchor = 0;
    state->typedTime = now;
    Prog__EditInsert(&state->typed, codepoint);
    Prog_QueueCommand(state, Prog_CommandKind_SelectTyped, 0, 0, order);
}

// The index of the panel whose inspector holds the point, or UINT32_MAX.
function u32 Prog__InspectorAt(const Prog_State *state, f32 x, f32 y)
{
    for (u32 i = 0; i < state->panelCount; ++i) {
        R_Rect pane = state->panels[i].inspectorRect;
        if (x >= pane.x0 && x < pane.x1 && y >= pane.y0 && y < pane.y1) return i;
    }
    return UINT32_MAX;
}

function void Prog_HandleInput(Prog_State *state, PlatformEventList events)
{
    u64 order = 0;
    for (PlatformEvent *event = events.first; event; event = event->next) {
        ++order;
        if (event->kind == PlatformEventKind_DrivesChanged) state->drivesChanged = true;
        // The settings take the system's new mode, and with it its scheme.
        if (event->kind == PlatformEventKind_ColorsChanged && state->systemScheme) Prog_ReloadSettingsFile(state);
        if (event->kind == PlatformEventKind_OpenPaths) Prog_OpenPaths(state, event->text);
        if (event->kind == PlatformEventKind_TabHover) {
            state->foreignDrag = event->focused;
            state->foreignPoint = (UI_Vec2){ event->x, event->y };
        }
        if (event->kind == PlatformEventKind_FocusChanged) state->windowInactive = !event->focused;
        // A key that ran a command does not also type its character, such as Shift+F's F or Space's space.
        if (event->kind == PlatformEventKind_Text && state->swallowText) {
            state->swallowText = false;
            continue;
        }
        if (event->kind == PlatformEventKind_KeyDown) state->swallowText = false;
        // A press anywhere but on the inspector's text drops what is selected in it, and gives Select All back to the files.
        if (event->kind == PlatformEventKind_MouseDown && !UI__Contains(state->inspectorTextRect, (UI_Vec2){ event->x, event->y })) {
            state->previewAnchor = state->previewCaret;
            state->previewTextFocus = false;
        }
        // A press in another panel's inspector gives that panel the keyboard, as one in its list does.
        u32 inspected = Prog__InspectorAt(state, event->x, event->y);
        if (event->kind == PlatformEventKind_MouseDown && inspected != UINT32_MAX) {
            u64 active = state->panels[state->activePanel].inspectorOf ? state->panels[state->activePanel].inspectorOf : state->panels[state->activePanel].id;
            if (active != state->panels[inspected].id) Prog_QueueCommand(state, Prog_CommandKind_PanelHover, 0, 0, order)->panel = state->panels[inspected].id;
        }
        // A chip being rebound takes the next key with its modifiers, which run nothing; Escape stops it, and stops the delete question.
        if ((state->hotkeyKind || state->hotkeyAsking) && event->kind == PlatformEventKind_KeyDown) {
            b32 modifier = event->key == Key_Shift || event->key == Key_Control || event->key == Key_Alt || event->key == Key_Super;
            if (event->key == Key_Escape) {
                Prog_QueueCommand(state, Prog_CommandKind_HotkeyCancel, 0, 0, order);
            } else if (!modifier && !state->hotkeyAsking && !event->isRepeat) {
                Prog_Command *bind = Prog_QueueCommand(state, Prog_CommandKind_HotkeyBind, state->hotkeyKind, 0, order);
                bind->target = state->hotkeySlot;
                bind->pressCount = event->key;
                bind->mods = event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super);
                // Only one key binds, however many come in this batch.
                state->hotkeyKind = Prog_CommandKind_None;
            }
            if (!modifier) state->swallowText = true;
            continue;
        }
        // A right click outside the open menu closes it before what it is on opens its own, or anywhere without one of its own.
        if (state->menuOpen && event->kind == PlatformEventKind_MouseDown && event->button == MouseButton_Right) {
            b32 inside = false;
            for (u32 i = 0; i < CArrayCount(state->menuRects); ++i) inside |= UI__Contains(state->menuRects[i], (UI_Vec2){ event->x, event->y });
            if (!inside) Prog_CloseMenu(state);
        }
        if (state->tabDrag && event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) {
            state->tabDrag = false;
            state->tabDragCancelled = true;
            continue;
        }
        // Filey Setup takes the keyboard while it shows, and Escape goes back to the panels.
        if (state->setupOpen && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_SetupBack, 0, 0, order);
            continue;
        }
        // The muted notices' dropdown closes with Escape, and keeps the rest of the keyboard from Options while open.
        if (state->mutedOpen && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) state->mutedOpen = false;
            continue;
        }
        // The schemes' dropdown takes the keyboard: Up and Down move its highlight, Enter shows that scheme and Escape closes it.
        if (state->schemesOpen && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            u32 count = 0;
            Prog_OptionChoices(state, (Prog_Option)(state->schemesOpen - 1), &state->frameArena, &count);
            if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) state->schemesOpen = 0;
            else if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Enter && !event->isRepeat) Prog_QueueCommand(state, Prog_CommandKind_SchemesPick, state->schemesHighlight, 0, order);
            // Up and Down go by the order shown; Prog_ChoiceAt is its own inverse.
            else if (event->kind == PlatformEventKind_KeyDown && (event->key == Key_Up || event->key == Key_Down) && count) {
                Prog_Option option = (Prog_Option)(state->schemesOpen - 1);
                u32 place = Prog_ChoiceAt(option, state->schemesHighlight);
                state->schemesHighlight = Prog_ChoiceAt(option, (place + (event->key == Key_Up ? count - 1 : 1)) % count);
            }
            continue;
        }
        // A color group's hex box being typed in takes hex digits, six at most; Enter or a press anywhere sets the color, and Escape leaves it.
        if (state->hexOption && !(state->paletteOpen && state->paletteMode == Prog_PaletteMode_Options)) state->hexOption = 0;
        if (state->hexOption && event->kind == PlatformEventKind_MouseDown) Prog_EndHexEdit(state, true);
        if (state->hexOption && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            Prog_TextEdit *hex = &state->hexEdit;
            if (event->kind == PlatformEventKind_Text) {
                rune c = event->codepoint;
                u32 selected = hex->cursor > hex->anchor ? hex->cursor - hex->anchor : hex->anchor - hex->cursor;
                b32 digit = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                if (digit && hex->length - selected < 6) Prog__EditInsert(hex, c >= 'a' ? c - 32 : c);
            } else if (event->key == Key_Escape || event->key == Key_Enter) {
                Prog_EndHexEdit(state, event->key == Key_Enter);
                state->swallowText = true;
            } else {
                Prog__EditKey(state, hex, event);
            }
            continue;
        }
        // The menu editor takes the keyboard: Enter and Escape close it.
        if (state->menuEditing && (event->kind == PlatformEventKind_KeyDown || event->kind == PlatformEventKind_Text)) {
            if (event->kind == PlatformEventKind_KeyDown && (event->key == Key_Escape || event->key == Key_Enter)) {
                Prog_QueueCommand(state, Prog_CommandKind_MenuEditClose, 0, 0, order);
                state->swallowText = true;
            }
            continue;
        }
        // A slider being typed in takes the keyboard; Enter or a press anywhere sets it to what was typed, worked out, and Escape leaves it.
        // Enter on what does not read keeps it typing, outlined; a press elsewhere then leaves it.
        if (state->sliderOption && !(state->paletteOpen && state->paletteMode == Prog_PaletteMode_Options)) Prog_EndSliderEdit(state, false);
        if (state->sliderOption && event->kind == PlatformEventKind_MouseDown && !Prog_EndSliderEdit(state, true)) Prog_EndSliderEdit(state, false);
        if (state->sliderOption && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            Prog_TextEdit *typed = &state->sliderEdit;
            if (event->kind == PlatformEventKind_Text) {
                if (typed->length < 64) Prog__EditInsert(typed, event->codepoint);
                state->sliderInvalid = false;
            } else if (event->key == Key_Escape || event->key == Key_Enter) {
                Prog_EndSliderEdit(state, event->key == Key_Enter);
                state->swallowText = true;
            } else {
                u32 version = typed->version;
                Prog__EditKey(state, typed, event);
                if (typed->version != version) state->sliderInvalid = false;
            }
            continue;
        }
        // The branches' dropdown takes the keyboard: typing finds a branch, Up and Down move the highlight, Enter switches to it, and Escape
        // clears what was typed, then closes it.
        if (state->branchesOpen && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            u32 *found = ArenaPushArray(&state->frameArena, state->branches.header.count + 1, u32);
            u32 count = Prog_BranchMatches(state, found);
            if (event->kind == PlatformEventKind_Text) {
                Prog__EditInsert(&state->branchesSearch, event->codepoint);
                state->branchesHighlight = 0;
            } else if (event->key == Key_Escape) {
                if (state->branchesSearch.length) state->branchesSearch = (Prog_TextEdit){0};
                else state->branchesOpen = false;
            } else if (event->key == Key_Enter && !event->isRepeat) {
                if (state->branchesHighlight < count) Prog_QueueCommand(state, Prog_CommandKind_GitBranchSwitch, found[state->branchesHighlight], 0, order);
            } else if ((event->key == Key_Up || event->key == Key_Down) && count) {
                state->branchesHighlight = (state->branchesHighlight + (event->key == Key_Up ? count - 1 : 1)) % count;
            } else if (Prog__EditKey(state, &state->branchesSearch, event)) {
                state->branchesHighlight = 0;
            }
            continue;
        }
        // The color picker takes the keyboard: a number box being typed in takes digits, or the hex box hex digits, and changes the color as
        // they come; Tab goes to the next box, and Enter or Escape stop typing. Otherwise Enter keeps the color and Escape puts it back.
        if (state->colorPicking && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            if (state->pickerField) {
                b32 hex = state->pickerField == 7;
                if (event->kind == PlatformEventKind_Text) {
                    rune c = event->codepoint;
                    b32 digit = (c >= '0' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
                    u32 selected = state->pickerEdit.cursor > state->pickerEdit.anchor ? state->pickerEdit.cursor - state->pickerEdit.anchor : state->pickerEdit.anchor - state->pickerEdit.cursor;
                    if (digit && state->pickerEdit.length - selected < (hex ? 6u : 3u)) {
                        Prog__EditInsert(&state->pickerEdit, hex && c >= 'a' ? c - 32 : c);
                        Prog_PickerTyped(state);
                    }
                } else if (event->key == Key_Tab) {
                    Prog_QueueCommand(state, Prog_CommandKind_ColorPickerEdit, (u64)(state->pickerField % 7 + 1), 0, order);
                } else if (event->key == Key_Escape || event->key == Key_Enter) {
                    state->pickerField = 0;
                    state->swallowText = true;
                } else if (Prog__EditKey(state, &state->pickerEdit, event)) {
                    Prog_PickerTyped(state);
                }
                continue;
            }
            if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_ColorPickerCancel, 0, 0, order);
            if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Enter) Prog_QueueCommand(state, Prog_CommandKind_ColorPickerClose, 0, 0, order);
            continue;
        }
        // What the layouts ask takes the keyboard, Enter going ahead and Escape not; the Save Layout box takes typing, with Enter saving and
        // Escape closing it.
        if (state->layoutAsk && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Enter && !event->isRepeat) Prog_QueueCommand(state, Prog_CommandKind_LayoutAnswer, 1, 0, order);
            else if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_LayoutAnswer, 0, 0, order);
            continue;
        }
        if (state->layoutSaving && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            if (event->kind == PlatformEventKind_Text) Prog__EditInsert(&state->layoutName, event->codepoint);
            else if (event->key == Key_Enter && !event->isRepeat) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 0, 0, order);
            else if (event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 2, 0, order);
            else Prog__EditKey(state, &state->layoutName, event);
            continue;
        }
        // The palette takes the keyboard: typing finds commands, Up and Down pick one, Enter runs it and Escape closes.
        if (state->paletteOpen) {
            // A key that alone neither types nor moves, a modifier or one without a code such as Caps Lock, leaves the list as it is.
            b32 modifier = event->key == Key_None || event->key == Key_Shift || event->key == Key_Control || event->key == Key_Alt || event->key == Key_Super;
            b32 unkeyed = !state->paletteKeyed;
            if (event->kind == PlatformEventKind_Text || (event->kind == PlatformEventKind_KeyDown && !modifier)) state->paletteKeyed = true;
            // In Options no row is the keyboard's until a key moves the highlight: the first Up or Down, or Ctrl+J or Ctrl+K, puts it on the
            // first row, and until then Enter, Left and Right, which act on that row, do nothing.
            if (unkeyed && state->paletteMode == Prog_PaletteMode_Options && event->kind == PlatformEventKind_KeyDown) {
                if (event->key == Key_Up || event->key == Key_Down || Prog__ListCursorKey(state, event)) continue;
                if (event->key == Key_Enter || event->key == Key_Left || event->key == Key_Right) {
                    state->paletteKeyed = false;
                    continue;
                }
            }
            // The wheel scrolls the list three rows a notch, leaving the highlight where it is until the keyboard moves it; over a slider
            // that takes the wheel, it is the slider's alone. The list's end is kept when it is built.
            if (event->kind == PlatformEventKind_MouseWheel && state->paletteMode != Prog_PaletteMode_Tabs) {
                UI_Block *hot = UI__Find(UI->hotKey);
                if (hot && FlagExists(hot->flags, UI_Flag_TakesWheel) && !FlagExists(hot->flags, UI_Flag_Floating)) continue;
                f32 turn = state->reverseScroll ? -1.0f : 1.0f;
                i64 rows = (i64)(event->wheelY * 3 * turn);
                if (!rows && event->wheelY != 0) rows = event->wheelY * turn > 0 ? 1 : -1;
                u32 *scroll = state->paletteMode == Prog_PaletteMode_GoTo ? &state->gotoScroll : &state->paletteScroll;
                *scroll = (u32)Max((i64)*scroll - rows, (i64)0);
                state->paletteWheeled = true;
                continue;
            }
            if (event->kind == PlatformEventKind_Text || (event->kind == PlatformEventKind_KeyDown && !modifier)) state->paletteWheeled = false;
            if (event->kind == PlatformEventKind_Text) {
                u32 kept = Prog__HighlightedOption(state);
                Prog__EditInsert(&state->palette, event->codepoint);
                Prog__KeepHighlight(state, kept);
                Prog__GoToExpandVariables(state);
            } else if (event->kind == PlatformEventKind_KeyDown) {
                Prog_PaletteRow matches[Prog_MaxPaletteRows];
                u32 tabs[Prog_MaxTabs];
                b32 goTo = state->paletteMode == Prog_PaletteMode_GoTo;
                u32 count = 0;
                String folder = {0};
                Prog_GoToItem *found = goTo ? Prog_GoToMatches(state, &count, &folder) : NULL;
                u8 options[Prog_Option_Count];
                u32 layouts[Prog_MaxLayouts];
                if (!goTo && state->paletteMode == Prog_PaletteMode_Options) count = Prog_OptionMatches(state, options);
                else if (!goTo && state->paletteMode == Prog_PaletteMode_Layouts) count = Prog_LayoutMatches(state, layouts);
                else if (!goTo && state->paletteMode == Prog_PaletteMode_Fonts) count = Prog_FontMatches(state, ArenaPushArray(&state->frameArena, state->fontCount + 1, u32));
                else if (!goTo) count = state->paletteMode == Prog_PaletteMode_Tabs ? Prog_TabMatches(state, tabs) : Prog_PaletteMatches(state, matches);
                // In Options with nothing typed, Backspace sets the option under the pointer back to its default, else the highlighted one.
                if (state->paletteMode == Prog_PaletteMode_Options && !state->palette.length && event->key == Key_Backspace && !(event->mods & (KeyMod_Control | KeyMod_Alt | KeyMod_Super))) {
                    u32 option = state->optionHovered ? state->optionHovered - 1u : state->paletteKeyed && state->paletteHighlight < count ? options[state->paletteHighlight] : Prog_Option_Count;
                    if (option < Prog_Option_Count) Prog_QueueCommand(state, Prog_CommandKind_OptionRestore, option, 0, order);
                    continue;
                }
                // In Options, Left and Right step the highlighted option down and up, a slider by a twentieth of its range, while nothing is typed.
                if (state->paletteMode == Prog_PaletteMode_Options && !state->palette.length && state->paletteHighlight < count && (event->key == Key_Left || event->key == Key_Right)) {
                    Prog_Option option = (Prog_Option)options[state->paletteHighlight];
                    const Prog_OptionDesc *desc = &Prog_Options[option];
                    i32 step = desc->control == Prog_OptionControl_Slider ? Max((desc->most - desc->least) / 20, 1) : 1, value = Prog_OptionValue(state, option);
                    if (event->key == Key_Left) step = -step;
                    u32 choices = 0;
                    Prog_OptionChoices(state, option, &state->frameArena, &choices);
                    if (desc->control == Prog_OptionControl_Choice && choices) value = (value + step + (i32)choices) % (i32)choices;
                    else value += step;
                    Prog_QueueCommand(state, Prog_CommandKind_OptionSet, (u64)option, 0, order)->target = (u64)(i64)value;
                    continue;
                }
                u32 version = state->palette.version, kept = Prog__HighlightedOption(state);
                // Go To's own hotkeys, such as Alt+Left for its back button.
                const Prog_Hotkey *own = goTo ? Prog_FindHotkey(state, Prog_HotkeyScope_GoTo, Key_None, event->key, event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super)) : NULL;
                if (own) {
                    // Select In Panel runs the highlighted row as Ctrl+Enter does.
                    if (own->command == Prog_CommandKind_GoToSelectInPanel && !event->isRepeat) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, UINT64_MAX, 0, order)->mods = KeyMod_Control;
                    else if (!event->isRepeat || !Prog_Commands[own->command].noRepeat) Prog_QueueCommand(state, own->command, 0, 0, order);
                    continue;
                }
                // Alt+E, which expands a folder in the lists, searches the drives as well, and again only the short list.
                if (goTo && event->key == Key_E && (event->mods & KeyMod_Alt) && !(event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Super))) {
                    state->gotoDrives = !state->gotoDrives;
                    state->paletteHighlight = 0;
                    state->gotoScroll = 0;
                    state->swallowText = true;
                    continue;
                }
                // Tab goes on to the next folder down the list and Shift+Tab back up, around the ends; the character the key also types is
                // not typed.
                b32 tab = event->key == Key_Tab && !(event->mods & (KeyMod_Control | KeyMod_Alt | KeyMod_Super));
                if (goTo && tab) {
                    state->swallowText = true;
                    for (u32 step = 1; step <= count; ++step) {
                        u32 at = (event->mods & KeyMod_Shift) ? (state->paletteHighlight + count - step % count) % count : (state->paletteHighlight + step) % count;
                        if (found[at].file) continue;
                        state->paletteHighlight = at;
                        state->paletteWheeled = false;
                        break;
                    }
                    continue;
                }
                // Right at the end of the text goes into the highlighted folder; anywhere else it moves the caret.
                b32 atEnd = state->palette.cursor == state->palette.length && state->palette.anchor == state->palette.cursor;
                if (goTo && event->key == Key_Right && !(event->mods & (KeyMod_Control | KeyMod_Alt | KeyMod_Shift | KeyMod_Super)) && atEnd &&
                    state->paletteHighlight < count && !found[state->paletteHighlight].file) {
                    Prog__GoToExpand(state, found[state->paletteHighlight].path);
                    continue;
                }
                // Autocomplete in Go To puts the highlighted folder in the box, to go on into it.
                const Prog_Hotkey *boxKey = goTo ? Prog_FindHotkey(state, Prog_HotkeyScope_Text, Key_None, event->key, event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super)) : NULL;
                if (boxKey && boxKey->command == Prog_CommandKind_TextAutocomplete) {
                    state->swallowText = true;
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
                // Escape closes at once, with the text typed, which Ctrl+U clears instead.
                if (event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, order);
                else if (event->key == Key_Enter && !event->isRepeat) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, UINT64_MAX, 0, order)->mods = event->mods;
                // The command list wraps around; Go To stops at its ends.
                else if (event->key == Key_Up || Prog__ListCursorKey(state, event) < 0) state->paletteHighlight = state->paletteHighlight && state->paletteHighlight != UINT32_MAX ? state->paletteHighlight - 1 : (count && !goTo ? count - 1 : 0);
                else if (event->key == Key_Down || Prog__ListCursorKey(state, event) > 0) state->paletteHighlight = state->paletteHighlight + 1 < count ? state->paletteHighlight + 1 : (goTo && count ? count - 1 : 0);
                else if ((event->key == (state->paletteMode == Prog_PaletteMode_Tabs ? Key_A : Key_P)) && (event->mods & KeyMod_Control) && (event->mods & KeyMod_Shift)) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, order);
                else Prog__EditKey(state, &state->palette, event);
                if (state->palette.version != version) Prog__KeepHighlight(state, kept), Prog__GoToExpandVariables(state);
            }
            continue;
        }
        // The Types popup takes the keyboard: typing finds types, Up and Down pick one, Enter toggles it and closes, and Escape or its own hotkey close.
        if (state->typesOpen && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            if (event->kind == PlatformEventKind_Text) {
                Prog__EditInsert(&state->typesSearch, event->codepoint);
                state->typesHighlight = 0;
                continue;
            }
            KeyMods held = event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super);
            const Prog_Hotkey *own = Prog_FindHotkey(state, Prog_HotkeyScope_Browser, Key_None, event->key, held);
            u32 *rows = ArenaPushArray(&state->frameArena, Max(state->typeCount, 1u), u32);
            u32 count = Prog_TypeMatches(state, rows), version = state->typesSearch.version;
            if (event->key == Key_Escape || (own && own->command == Prog_CommandKind_TypesPopupOpen && !event->isRepeat)) {
                Prog_QueueCommand(state, Prog_CommandKind_TypesPopupClose, 0, 0, order);
                state->swallowText = true;
            } else if (event->key == Key_Enter && !event->isRepeat) {
                if (state->typesHighlight < count) Prog_QueueCommand(state, Prog_CommandKind_TypesToggle, rows[state->typesHighlight], 0, order)->target = 1;
            } else if (event->key == Key_Up && count) {
                state->typesHighlight = state->typesHighlight ? state->typesHighlight - 1 : count - 1;
            } else if (event->key == Key_Down && count) {
                state->typesHighlight = state->typesHighlight + 1 < count ? state->typesHighlight + 1 : 0;
            } else {
                Prog__EditKey(state, &state->typesSearch, event);
                if (state->typesSearch.version != version) state->typesHighlight = 0;
            }
            continue;
        }
        // The Quick Access filter's box takes the keyboard: typing finds sidebar items, Enter keeps what it found and hands the keyboard back,
        // and Escape clears it. A press anywhere else hands the keyboard back and goes on.
        if (state->sidebarFiltering) {
            if (event->kind == PlatformEventKind_MouseDown && !UI__Contains(state->sidebarFilterRect, (UI_Vec2){ event->x, event->y })) {
                state->sidebarFiltering = false;
            } else if (event->kind == PlatformEventKind_Text) {
                Prog__EditInsert(&state->sidebarFilter, event->codepoint);
                continue;
            } else if (event->kind == PlatformEventKind_KeyDown) {
                if (event->key == Key_Escape) {
                    Prog_QueueCommand(state, Prog_CommandKind_QuickAccessFilterRevert, 0, 0, order);
                    state->swallowText = true;
                    continue;
                }
                if (event->key == Key_Enter) {
                    if (!event->isRepeat) Prog_QueueCommand(state, Prog_CommandKind_QuickAccessFilterApply, 0, 0, order);
                    continue;
                }
                // Other hotkeys still reach the browser.
                if (Prog__EditKey(state, &state->sidebarFilter, event) || Prog__TypesInBox(event->key, event->mods)) continue;
            }
        }
        // The folder dropdown takes the keyboard: typing finds in it, Up and Down move the highlight, Enter goes to or opens what it lights, its own
        // hotkeys move it to the crumbs beside, and Escape or the hotkey that opened it close it.
        if (state->filesOpen && (event->kind == PlatformEventKind_Text || event->kind == PlatformEventKind_KeyDown)) {
            if (event->kind == PlatformEventKind_Text) {
                Prog__EditInsert(&state->filesSearch, event->codepoint);
                state->filesHighlight = 0;
                state->filesLit = state->filesReveal = true;
                continue;
            }
            KeyMods held = event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super);
            const Prog_Hotkey *own = Prog_FindHotkey(state, Prog_HotkeyScope_Files, Key_None, event->key, held);
            const Prog_Hotkey *browser = Prog_FindHotkey(state, Prog_HotkeyScope_Browser, Key_None, event->key, held);
            u32 *rows = ArenaPushArray(&state->frameArena, Max(state->filesCount, (u64)1), u32);
            u32 count = Prog_FilesMatches(state, rows), version = state->filesSearch.version;
            if (own) {
                Prog_QueueCommand(state, own->command, 0, 0, order);
            } else if (event->key == Key_Escape || (browser && browser->command == Prog_CommandKind_FilesPopupOpen && !event->isRepeat)) {
                Prog_QueueCommand(state, Prog_CommandKind_FilesPopupClose, 0, 0, order);
                state->swallowText = true;
            } else if (event->key == Key_Enter && !event->isRepeat) {
                if (count) Prog_QueueCommand(state, Prog_CommandKind_FilesPopupActivate, UINT64_MAX, 0, order);
            } else if ((event->key == Key_Up || event->key == Key_Down) && count) {
                u32 at = Min(state->filesHighlight, count - 1);
                state->filesHighlight = event->key == Key_Up ? (at ? at - 1 : count - 1) : (at + 1 < count ? at + 1 : 0);
                state->filesLit = state->filesReveal = true;
            } else {
                Prog__EditKey(state, &state->filesSearch, event);
                if (state->filesSearch.version != version) {
                    state->filesHighlight = 0;
                    state->filesLit = state->filesReveal = true;
                }
            }
            continue;
        }
        // The delete confirmation takes the keyboard: Enter deletes, Escape cancels, and nothing else gets through.
        if (state->confirmingDelete) {
            if (event->kind == PlatformEventKind_KeyDown && !event->isRepeat && event->key == Key_Enter) Prog_QueueCommand(state, Prog_CommandKind_DeleteConfirm, 0, 0, order);
            if (event->kind == PlatformEventKind_KeyDown && event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_DeleteCancel, 0, 0, order);
            continue;
        }
        // The rename box takes the keyboard; a click anywhere else commits it, but going back or forward, by the toolbar, the keys or the
        // mouse's side buttons, cancels it and goes.
        if (state->renaming) {
            b32 historyButton = state->hotToolbarCommand == Prog_CommandKind_Back || state->hotToolbarCommand == Prog_CommandKind_Forward;
            if (event->kind == PlatformEventKind_MouseDown) {
                R_Rect box = state->renameRect;
                if (!(event->x >= box.x0 && event->x < box.x1 && event->y >= box.y0 && event->y < box.y1)) {
                    Prog_QueueCommand(state, historyButton ? Prog_CommandKind_RenameCancel : Prog_CommandKind_RenameCommit, 0, 0, order);
                }
            } else if (event->kind == PlatformEventKind_Text) {
                Prog__EditInsert(&state->rename, event->codepoint);
            } else if (event->kind == PlatformEventKind_KeyDown) {
                // Of the hotkeys only the rename box's own reach it.
                KeyMods held = event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super);
                const Prog_Hotkey *own = Prog_FindHotkey(state, Prog_HotkeyScope_Rename, Key_None, event->key, held);
                const Prog_Hotkey *browser = own ? NULL : Prog_FindHotkey(state, Prog_HotkeyScope_Browser, Key_None, event->key, held);
                if (own) {
                    Prog_QueueCommand(state, own->command, 0, 0, order);
                    state->swallowText = true;
                } else if (browser && (browser->command == Prog_CommandKind_Back || browser->command == Prog_CommandKind_Forward)) {
                    Prog_QueueCommand(state, Prog_CommandKind_RenameCancel, 0, 0, order);
                    Prog_QueueCommand(state, browser->command, 0, 0, order);
                    state->swallowText = true;
                }
                else if (event->key == Key_Enter) Prog_QueueCommand(state, Prog_CommandKind_RenameCommit, 0, 0, order);
                else if (event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_RenameCancel, 0, 0, order);
                else Prog__EditKey(state, &state->rename, event);
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
                // Keys the box does not use, such as Up and Down, still reach the list; what types a character is typed.
                b32 used = true;
                if (event->key == Key_Enter) Prog_QueueCommand(state, Prog_CommandKind_InspectorApplyFilter, 0, 0, order);
                else if (event->key == Key_Escape) Prog_QueueCommand(state, Prog_CommandKind_InspectorRemoveFilter, 0, 0, order);
                else used = Prog__EditKey(state, &state->inspectorFilter, event) || Prog__TypesInBox(event->key, event->mods) ||
                            (event->key == Key_Space && !(event->mods & (KeyMod_Control | KeyMod_Alt | KeyMod_Super)));
                if (used) continue;
            }
        }
        // Typing anywhere in the browser filters it, or with Select Typed Item selects what starts with it; Trigger Hotkey leaves it to the hotkeys.
        if (event->kind == PlatformEventKind_Text && !state->menuOpen) {
            u32 version = state->filter.version;
            // '/' outside the box only puts the keyboard in it, like Ctrl+F.
            b32 opens = !state->filtering && event->codepoint == '/';
            if (!state->filtering && !opens && state->characterKeys == Prog_CharacterKeys_TriggerHotkey) continue;
            if (!state->filtering && !opens && state->characterKeys == Prog_CharacterKeys_SelectTypedItem) {
                Prog__TypeToSelect(state, event->codepoint, order);
                continue;
            }
            state->filtering = true;
            if (!opens) Prog__EditInsert(&state->filter, event->codepoint);
            if (state->filter.version != version) Prog_ApplyFilter(state, true);
            continue;
        }
        // Ctrl+wheel zooms instead of scrolling, the inspector's contents under the pointer; fine-grained wheels add up to whole steps.
        if (event->kind == PlatformEventKind_MouseWheel && (event->mods & KeyMod_Control) && !state->menuOpen) {
            b32 inspector = inspected != UINT32_MAX;
            Prog_CommandKind in = inspector ? Prog_CommandKind_InspectorIncreaseContentsSize : Prog_CommandKind_ZoomIn;
            Prog_CommandKind out = inspector ? Prog_CommandKind_InspectorDecreaseContentsSize : Prog_CommandKind_ZoomOut;
            state->zoomWheel += event->wheelY;
            if (!inspector) state->zoomWheelTime = PlatformNowMicroseconds();
            // The inspector under the pointer zooms, its panel taking the keyboard.
            u64 panel = inspector ? state->panels[inspected].id : Prog__PanelId(state);
            for (; state->zoomWheel >= 1; state->zoomWheel -= 1) Prog_QueueCommand(state, in, 0, 0, order)->panel = panel;
            for (; state->zoomWheel <= -1; state->zoomWheel += 1) Prog_QueueCommand(state, out, 0, 0, order)->panel = panel;
            continue;
        }
        // Alt+wheel steps the font size and Alt+Shift+wheel the spacing.
        if (event->kind == PlatformEventKind_MouseWheel && (event->mods & (KeyMod_Control | KeyMod_Alt)) == KeyMod_Alt && !state->menuOpen) {
            b32 spacing = (event->mods & KeyMod_Shift) != 0;
            state->sizeWheel += event->wheelY;
            for (; state->sizeWheel >= 1; state->sizeWheel -= 1) Prog_QueueCommand(state, spacing ? Prog_CommandKind_IncreaseSpacing : Prog_CommandKind_IncreaseFontSize, 0, 0, order);
            for (; state->sizeWheel <= -1; state->sizeWheel += 1) Prog_QueueCommand(state, spacing ? Prog_CommandKind_DecreaseSpacing : Prog_CommandKind_DecreaseFontSize, 0, 0, order);
            continue;
        }
        if (event->kind != PlatformEventKind_KeyDown) continue;
        KeyMods mods = event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super);
        // Escape cancels a copy or move that waits on a question about a taken name, unless a menu or the filter box has the keyboard.
        if (event->key == Key_Escape && !state->menuOpen && !state->filtering) {
            u32 asking = UINT32_MAX;
            for (u32 i = 0; i < Prog_TransferSlots && asking == UINT32_MAX; ++i) if (state->transfers[i].job && atomic_load(&state->transfers[i].progress.asking)) asking = i;
            if (asking != UINT32_MAX) {
                Prog_QueueCommand(state, Prog_CommandKind_TransferCancel, asking, 0, order);
                continue;
            }
        }
        // Escape removes the filter.
        // Keys the box does not use still reach the list: Up and Down move through it and Enter opens the focused item.
        if (!state->menuOpen && event->key == Key_Escape && (state->filtering || state->filter.length)) {
            Prog_QueueCommand(state, Prog_CommandKind_RemoveFilter, 0, 0, order);
            continue;
        }
        if (!state->menuOpen && state->filtering) {
            u32 version = state->filter.version;
            if (Prog__EditKeyOver(state, &state->filter, event, true)) {
                if (state->filter.version != version) Prog_ApplyFilter(state, true);
                continue;
            }
        }
        // An open menu takes the keyboard.
        // Inline, Right and Enter open a submenu under its item and Left closes it, or goes up to the item it is under.
        if (state->menuOpen && Prog_MenuInline(state) && (event->key == Key_Left || event->key == Key_Right || event->key == Key_Enter)) {
            u32 at = state->menuHighlight;
            b32 parent = at < state->menu.count && state->menu.items[at].childCount;
            if (event->key == Key_Left) {
                if (parent && Prog_MenuIsExpanded(state, at)) Prog_MenuToggleExpanded(state, at);
                else if (Prog_MenuOwner(state, at) != UINT32_MAX) state->menuHighlight = Prog_MenuOwner(state, at), state->menuReveal = true;
            } else if (parent && (event->key == Key_Enter || !Prog_MenuIsExpanded(state, at))) {
                Prog_MenuToggleExpanded(state, at);
            } else if (parent) {
                Prog__MenuMove(state, 1);
            } else if (event->key == Key_Enter && at < state->menu.count) {
                Prog_QueueCommand(state, Prog_CommandKind_ContextMenuInvoke, at, 0, order);
            }
            continue;
        }
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
                    if (!state->menuCommands && Prog__EditKey(state, &state->menuSearch, event) && state->menuSearch.version != version) {
                        state->menuDepth = 0;
                        state->menuHighlight = UINT32_MAX;
                        Prog__MenuMove(state, 1);
                    }
                } break;
            }
            continue;
        }
        // While filtering, what types a character is typed; outside the box, a character key types unless Character Key Action runs its hotkey.
        if (state->filtering && Prog__TypesInBox(event->key, mods)) continue;
        if (state->characterKeys != Prog_CharacterKeys_TriggerHotkey && Prog_IsCharacterKey(event->key, mods)) {
            state->hotkeyLead = Key_None;
            continue;
        }
        // A sequence such as "G G" runs on its second key; a key that only starts one waits for the next.
        const Prog_Hotkey *hotkey = state->hotkeyLead && !event->isRepeat ? Prog_FindHotkey(state, Prog_HotkeyScope_Browser, state->hotkeyLead, event->key, mods) : NULL;
        state->hotkeyLead = Key_None;
        if (!hotkey) hotkey = Prog_FindHotkey(state, Prog_HotkeyScope_Browser, Key_None, event->key, mods);
        if (!hotkey && !mods && !event->isRepeat && Prog__HotkeyLeads(state, Prog_HotkeyScope_Browser, event->key)) {
            state->hotkeyLead = event->key;
            state->swallowText = true;
            continue;
        }
        if (hotkey) state->swallowText = true;
        if (hotkey && (!event->isRepeat || !Prog_Commands[hotkey->command].noRepeat)) {
            Prog_Command *command = Prog_QueueCommand(state, hotkey->command, 0, 0, order);
            command->mods = event->mods;
        }
    }
}
