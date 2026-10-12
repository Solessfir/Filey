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
            case Prog_CommandKind_OpenRow: if (state->hasFocus) Prog_OpenRow(state, state->focusedRow); break;
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
            case Prog_CommandKind_MoveFocus: {
                if (!state->loading && state->directory.count) {
                    u64 row = state->hasFocus ? state->focusedRow : 0;
                    u64 page = state->pageRows > 1 ? state->pageRows - 1 : 1;
                    if (command->row == Key_Home) row = 0;
                    else if (command->row == Key_End) row = state->directory.count - 1;
                    else if (command->row == Key_Up && row) --row;
                    else if (command->row == Key_Down && state->hasFocus && row + 1 < state->directory.count) ++row;
                    else if (command->row == Key_PageUp) row = row > page ? row - page : 0;
                    else if (command->row == Key_PageDown) row = Min(row + page, state->directory.count - 1);
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
        Prog_CommandKind kind = Prog_CommandKind_None;
        if (event->key == Key_F5) kind = Prog_CommandKind_Refresh;
        else if (event->key == Key_H && (event->mods & KeyMod_Control)) kind = Prog_CommandKind_ToggleHidden;
        else if (event->key == Key_A && (event->mods & KeyMod_Control)) kind = Prog_CommandKind_SelectAll;
        else if (event->key == Key_Left && (event->mods & KeyMod_Alt)) kind = Prog_CommandKind_Back;
        else if (event->key == Key_Right && (event->mods & KeyMod_Alt)) kind = Prog_CommandKind_Forward;
        else if ((event->key == Key_Up && (event->mods & KeyMod_Alt)) || event->key == Key_Backspace) kind = Prog_CommandKind_Up;
        else if (event->key == Key_Enter && !event->isRepeat) kind = Prog_CommandKind_OpenRow;
        else if (event->key == Key_Up || event->key == Key_Down || event->key == Key_Home || event->key == Key_End ||
                 event->key == Key_PageUp || event->key == Key_PageDown) kind = Prog_CommandKind_MoveFocus;
        if (kind != Prog_CommandKind_None) {
            Prog_Command *command = Prog_QueueCommand(state, kind, event->key, 0, order);
            command->mods = event->mods;
        }
    }
}
