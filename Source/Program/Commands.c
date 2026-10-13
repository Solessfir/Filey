#define Prog_CommandsTable
#include "Meta/Commands.meta.h"

function Prog_Command *Prog_QueueCommand(Prog_State *state, Prog_CommandKind kind, u64 row, u32 pressCount, u64 inputOrder)
{
    Assert(kind > Prog_CommandKind_None && kind < Prog_CommandKind_Count);
    Prog_Command *command = ArenaPushStruct(&state->frameArena, Prog_Command);
    command->kind = (u8)kind;
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
    state->menuRowCount = 0;
    state->menuDepth = 0;
    state->menuHighlight = UINT32_MAX;
}

// Shown items of a menu level: unlabeled owner-drawn items are dropped, and so
// are separators at the ends or next to another separator.
function u32 Prog_MenuVisible(const P_ContextMenu *menu, u32 first, u32 count, u32 *out)
{
    u32 result = 0;
    for (u32 i = first; i < first + count && i < menu->count; ++i) {
        const P_MenuItem *item = &menu->items[i];
        if (item->separator) {
            if (result && !menu->items[out[result - 1]].separator) out[result++] = i;
        } else if (item->label.length) {
            out[result++] = i;
        }
    }
    while (result && menu->items[out[result - 1]].separator) --result;
    return result;
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
    u32 shown = Prog_MenuVisible(&state->menu, first, count, visible);
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
    for (u64 i = 0; i < state->menuRowCount; ++i) names[i] = state->directory.entries[state->menuRows[i]].file.name;
    ProfileScope("OpenContextMenu") {
        state->menuOpen = P_ContextMenuOpen(&state->menuArena, state->directory.path, names, state->menuRowCount, &state->menu);
    }
    if (!state->menuOpen) {
        Prog_CloseMenu(state);
        return;
    }
    state->menuAt = mouse ? state->ui.mouse
                  : state->menuRowCount ? (UI_Vec2){ state->focusedRowRect.x0 + 24, state->focusedRowRect.y1 }
                  : state->ui.mouse;
    state->menuHighlight = UINT32_MAX;
    if (!mouse) Prog__MenuMove(state, 1);
}

function String Prog_RenameText(const Prog_State *state)
{
    return StrMake((const char *)state->renameText, state->renameLength);
}

function void Prog_BeginRename(Prog_State *state, u64 row)
{
    if (state->loading || row >= state->directory.count) return;
    const Prog_Entry *entry = &state->directory.entries[row];
    String name = entry->file.name;
    if (name.length >= sizeof(state->renameText)) return;
    Prog_CloseMenu(state);
    MemCopy(state->renameText, name.value, name.length);
    MemCopy(state->renameFrom, name.value, name.length);
    state->renameLength = state->renameFromLength = (u32)name.length;
    // Files start with the part before the extension selected, as in Explorer.
    u32 end = state->renameLength;
    if (entry->file.kind != P_FileKind_Directory) {
        for (u32 i = state->renameLength; i > 1; --i) {
            if (state->renameText[i - 1] == '.') { end = i - 1; break; }
        }
    }
    state->renameAnchor = 0;
    state->renameCursor = end;
    state->renaming = true;
    state->focusedRow = row;
    state->hasFocus = true;
    state->revealFocus = true;
}

function void Prog__EndRename(Prog_State *state, b32 commit)
{
    if (!state->renaming) return;
    state->renaming = false;
    String from = StrMake((const char *)state->renameFrom, state->renameFromLength);
    String to = Prog_RenameText(state);
    if (!commit || !to.length || StrIsEqual(from, to) || state->loading) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Rename;
    input->path = StrCopy(&job->arena, state->directory.path);
    input->name = StrCopy(&job->arena, from);
    input->newName = StrCopy(&job->arena, to);
    job->input = input;
    P_JobSubmit(job);
}

// The next codepoint boundary before (`step` < 0) or after `at`.
function u32 Prog__RenameStep(const Prog_State *state, u32 at, i32 step)
{
    if (step < 0) {
        if (at) --at;
        while (at && (state->renameText[at] & 0xC0) == 0x80) --at;
    } else if (at < state->renameLength) {
        ++at;
        while (at < state->renameLength && (state->renameText[at] & 0xC0) == 0x80) ++at;
    }
    return at;
}

// Replaces the selection with `bytes`.
function void Prog__RenameReplace(Prog_State *state, const u8 *bytes, u32 count)
{
    u32 from = Min(state->renameCursor, state->renameAnchor), to = Max(state->renameCursor, state->renameAnchor);
    u32 length = state->renameLength - (to - from) + count;
    if (length >= sizeof(state->renameText)) return;
    MemMove(state->renameText + from + count, state->renameText + to, state->renameLength - to);
    MemCopy(state->renameText + from, bytes, count);
    state->renameLength = length;
    state->renameCursor = state->renameAnchor = from + count;
}

function void Prog__RenameKey(Prog_State *state, const PlatformEvent *event, u64 order)
{
    b32 shift = (event->mods & KeyMod_Shift) != 0;
    u32 from = Min(state->renameCursor, state->renameAnchor), to = Max(state->renameCursor, state->renameAnchor);
    switch (event->key) {
        case Key_Left:
        case Key_Right: {
            i32 step = event->key == Key_Left ? -1 : 1;
            if (!shift && from != to) state->renameCursor = step < 0 ? from : to;
            else state->renameCursor = Prog__RenameStep(state, state->renameCursor, step);
            if (!shift) state->renameAnchor = state->renameCursor;
        } break;
        case Key_Home:
        case Key_End: {
            state->renameCursor = event->key == Key_Home ? 0 : state->renameLength;
            if (!shift) state->renameAnchor = state->renameCursor;
        } break;
        case Key_Backspace:
        case Key_Delete: {
            if (from == to) state->renameAnchor = Prog__RenameStep(state, state->renameCursor, event->key == Key_Backspace ? -1 : 1);
            Prog__RenameReplace(state, NULL, 0);
        } break;
        case Key_A: {
            if (event->mods & KeyMod_Control) {
                state->renameAnchor = 0;
                state->renameCursor = state->renameLength;
            }
        } break;
        case Key_Enter: Prog_QueueCommand(state, Prog_CommandKind_RenameCommit, 0, 0, order); break;
        case Key_Escape: Prog_QueueCommand(state, Prog_CommandKind_RenameCancel, 0, 0, order); break;
        default: break;
    }
}

function void Prog__RenameText(Prog_State *state, rune c)
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
    Prog__RenameReplace(state, bytes, count);
}

function void Prog__InvokeMenu(Prog_State *state, u32 index)
{
    if (!state->menuOpen || index >= state->menu.count) return;
    P_MenuItem item = state->menu.items[index];
    if (item.separator || item.disabled || item.childCount) return;
    // "Open" stays inside Filey: folders open here and files go through the same job as double-click.
    // The shell's Rename needs an Explorer view to edit in, so Filey edits the name itself.
    if (StrIsEqual(item.verb, S("rename")) && state->menuRowCount == 1) {
        u64 row = state->menuRows[0];
        Prog_CloseMenu(state);
        Prog_BeginRename(state, row);
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
        b32 creates = index >= state->menu.rootCount && !StrIsEqual(item.verb, S("paste"));
        state->renameCreatedUntil = creates ? PlatformNowMicroseconds() + Millions(5) : 0;
        P_FileError error = P_ContextMenuInvoke(&state->frameArena, &state->menu, item.id);
        ArenaClear(&state->errorArena);
        state->error = error.code ? StrFormat(&state->errorArena, "%S: %S", item.label, error.message) : (String){0};
        // The watch would catch the change too; reloading now shows it without waiting.
        Prog_Reload(state);
    }
    Prog_CloseMenu(state);
}

function void Prog_DragSelection(Prog_State *state)
{
    state->dragRequested = false;
    if (state->loading || !state->selectedCount) return;
    Scratch scratch = GetScratch();
    String *names = ArenaPushArray(scratch.arena, state->selectedCount, String);
    u64 count = 0;
    for (u64 i = 0; i < state->directory.count && count < state->selectedCount; ++i) {
        if (state->directory.entries[i].selected) names[count++] = state->directory.entries[i].file.name;
    }
    state->dragging = true;
    P_DragFiles(state->directory.path, names, count);
    state->dragging = false;
    // The system's drag loop took the button release.
    UI_CancelActive();
    ReleaseScratch(scratch);
}

function b32 Prog_ProcessCommands(Prog_State *state)
{
    b32 changed = state->commandFirst != NULL;
    for (Prog_Command *command = state->commandFirst; command; command = command->next) {
        switch (command->kind) {
            case Prog_CommandKind_SelectRow: {
                if (!state->loading) {
                    Prog_SelectRow(state, command->row, command->mods);
                    if (command->pressCount == 2) Prog_OpenRow(state, command->row);
                }
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
            case Prog_CommandKind_Sort: {
                if (command->row < Prog_Sort_Count) {
                    state->descending = state->sort == command->row ? !state->descending : false;
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
            case Prog_CommandKind_SelectAll: {
                if (!state->loading) {
                    for (u64 i = 0; i < state->directory.count; ++i) state->directory.entries[i].selected = true;
                    state->selectedCount = state->directory.count;
                }
            } break;
            case Prog_CommandKind_FocusUp:
            case Prog_CommandKind_FocusDown:
            case Prog_CommandKind_FocusFirst:
            case Prog_CommandKind_FocusLast:
            case Prog_CommandKind_FocusPageUp:
            case Prog_CommandKind_FocusPageDown: {
                if (!state->loading && state->directory.count) {
                    u64 row = state->hasFocus ? state->focusedRow : 0;
                    u64 page = state->pageRows > 1 ? state->pageRows - 1 : 1;
                    u64 last = state->directory.count - 1;
                    switch (command->kind) {
                        case Prog_CommandKind_FocusUp: if (row) --row; break;
                        case Prog_CommandKind_FocusDown: if (state->hasFocus && row < last) ++row; break;
                        case Prog_CommandKind_FocusFirst: row = 0; break;
                        case Prog_CommandKind_FocusLast: row = last; break;
                        case Prog_CommandKind_FocusPageUp: row = row > page ? row - page : 0; break;
                        case Prog_CommandKind_FocusPageDown: row = Min(row + page, last); break;
                        default: InvalidPath;
                    }
                    if (command->mods & KeyMod_Control) { state->focusedRow = row; state->hasFocus = true; }
                    else Prog_SelectRow(state, row, command->mods);
                    state->revealFocus = true;
                }
            } break;
            case Prog_CommandKind_ContextMenu:
            case Prog_CommandKind_ContextMenuAt: Prog__OpenMenu(state, command); break;
            case Prog_CommandKind_ContextMenuInvoke: Prog__InvokeMenu(state, (u32)command->row); break;
            case Prog_CommandKind_ContextMenuClose: Prog_CloseMenu(state); break;
            case Prog_CommandKind_Rename: if (state->hasFocus) Prog_BeginRename(state, state->focusedRow); break;
            case Prog_CommandKind_RenameCommit: Prog__EndRename(state, true); break;
            case Prog_CommandKind_RenameCancel: Prog__EndRename(state, false); break;
            case Prog_CommandKind_DragSelection: if (!state->dragging && !state->loading) state->dragRequested = true; break;
            default: InvalidPath;
        }
    }
    state->commandFirst = NULL;
    state->commandLast = NULL;
    // Files dragged onto the window go into the folder under the pointer, else into this one.
    String drop = state->loading ? (String){0} : state->directory.path;
    if (!state->loading && state->hasHoverRow && state->hoverRow < state->directory.count) {
        const Prog_Entry *entry = &state->directory.entries[state->hoverRow];
        // A dragged folder cannot take itself.
        if (entry->file.kind == P_FileKind_Directory && !(state->dragging && entry->selected)) {
            drop = Prog_JoinPath(&state->frameArena, state->directory.path, entry->file.name);
        }
    }
    P_SetDropDirectory(drop);
    Prog_SubmitIconRequests(state);
    return changed;
}

function void Prog_HandleInput(Prog_State *state, PlatformEventList events)
{
    u64 order = 0;
    for (PlatformEvent *event = events.first; event; event = event->next) {
        ++order;
        // The rename box takes the keyboard; a click anywhere else commits it.
        if (state->renaming) {
            if (event->kind == PlatformEventKind_MouseDown) {
                R_Rect box = state->renameRect;
                if (!(event->x >= box.x0 && event->x < box.x1 && event->y >= box.y0 && event->y < box.y1)) {
                    Prog_QueueCommand(state, Prog_CommandKind_RenameCommit, 0, 0, order);
                }
            } else if (event->kind == PlatformEventKind_Text) {
                Prog__RenameText(state, event->codepoint);
            } else if (event->kind == PlatformEventKind_KeyDown) {
                Prog__RenameKey(state, event, order);
            }
            continue;
        }
        if (event->kind != PlatformEventKind_KeyDown) continue;
        KeyMods mods = event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super);
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
                default: break;
            }
            continue;
        }
        for (u64 i = 0; i < CArrayCount(Prog_DefaultHotkeys); ++i) {
            const Prog_Hotkey *hotkey = &Prog_DefaultHotkeys[i];
            if (hotkey->key != event->key || hotkey->mods != mods) continue;
            if (!event->isRepeat || !Prog_Commands[hotkey->command].noRepeat) {
                Prog_Command *command = Prog_QueueCommand(state, hotkey->command, 0, 0, order);
                command->mods = event->mods;
            }
            break;
        }
    }
}
