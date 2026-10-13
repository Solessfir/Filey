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

function void Prog__Reload(Prog_State *state)
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
                Prog__Reload(state);
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
                    Prog__Reload(state);
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
                Prog__Reload(state);
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
            default: InvalidPath;
        }
    }
    state->commandFirst = NULL;
    state->commandLast = NULL;
    return changed;
}

function void Prog_HandleInput(Prog_State *state, PlatformEventList events)
{
    u64 order = 0;
    for (PlatformEvent *event = events.first; event; event = event->next) {
        ++order;
        if (event->kind != PlatformEventKind_KeyDown) continue;
        KeyMods mods = event->mods & (KeyMod_Shift | KeyMod_Control | KeyMod_Alt | KeyMod_Super);
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
