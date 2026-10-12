function void Prog_QueueCommand(Prog_State *state, Prog_CommandKind kind, u32 row, u32 pressCount, u64 inputOrder)
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
}

function b32 Prog_ProcessCommands(Prog_State *state)
{
    b32 changed = state->commandFirst != NULL;
    for (Prog_Command *command = state->commandFirst; command; command = command->next) {
        switch (command->kind) {
            case Prog_CommandKind_SelectRow: {
                state->hasSelection = true;
                state->selectedRow = command->row;
                if (command->pressCount == 2) {
                    ++state->openCount;
                }
            } break;
            case Prog_CommandKind_Refresh: {
                ++state->refreshCount;
                state->hasSelection = false;
            } break;
            default: InvalidPath;
        }
    }
    state->commandFirst = NULL;
    state->commandLast = NULL;
    return changed;
}
