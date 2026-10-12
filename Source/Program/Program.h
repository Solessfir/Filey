#pragma once

typedef enum Prog_CommandKind {
    Prog_CommandKind_None,
    Prog_CommandKind_SelectRow,
    Prog_CommandKind_Refresh,
    Prog_CommandKind_Count,
} Prog_CommandKind;

typedef struct Prog_Command Prog_Command;
struct Prog_Command {
    Prog_Command *next;
    enum8(Prog_CommandKind) kind;
    u32 row;
    u32 pressCount;
    u64 inputOrder;
};

typedef struct Prog_State {
    Arena frameArena;
    UI_Context ui;
    Prog_Command *commandFirst;
    Prog_Command *commandLast;
    b32 hasSelection;
    u32 selectedRow;
    u32 refreshCount;
    u32 openCount;
} Prog_State;

function void Prog_QueueCommand(Prog_State *state, Prog_CommandKind kind, u32 row, u32 pressCount, u64 inputOrder);
function b32 Prog_ProcessCommands(Prog_State *state);
function void Prog_BuildUI(Prog_State *state, const R_Font *font, f32 scale);
