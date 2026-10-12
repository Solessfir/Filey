#pragma once

#include "../Platform/Files.h"

typedef enum Prog_Sort {
    Prog_Sort_Name,
    Prog_Sort_Type,
    Prog_Sort_Modified,
    Prog_Sort_Size,
    Prog_Sort_Count,
} Prog_Sort;

typedef enum Prog_CommandKind {
    Prog_CommandKind_None,
    Prog_CommandKind_SelectRow,
    Prog_CommandKind_OpenRow,
    Prog_CommandKind_Refresh,
    Prog_CommandKind_Back,
    Prog_CommandKind_Forward,
    Prog_CommandKind_Up,
    Prog_CommandKind_Sort,
    Prog_CommandKind_ToggleHidden,
    Prog_CommandKind_SelectAll,
    Prog_CommandKind_MoveFocus,
    Prog_CommandKind_Count,
} Prog_CommandKind;

typedef struct Prog_Command Prog_Command;
struct Prog_Command {
    Prog_Command *next;
    enum8(Prog_CommandKind) kind;
    u64 row;
    u32 pressCount;
    u64 inputOrder;
    KeyMods mods;
};

typedef struct Prog_Entry {
    P_File file;
    String type;
    String modified;
    String size;
    b32 selected;
} Prog_Entry;

typedef enum Prog_JobKind {
    Prog_JobKind_Directory,
    Prog_JobKind_Open,
    Prog_JobKind_Count,
} Prog_JobKind;

typedef struct Prog_JobInput {
    enum8(Prog_JobKind) kind;
    String path;
    u64 generation;
    enum8(Prog_Sort) sort;
    b32 descending;
    b32 showHidden;
    b32 addHistory;
    u64 historyAt;
} Prog_JobInput;

typedef struct Prog_Directory {
    String path;
    Prog_Entry *entries;
    u64 count;
    P_FileError error;
    b32 cancelled;
    enum8(Prog_Sort) sort;
    b32 descending;
    b32 showHidden;
} Prog_Directory;

typedef struct Prog_State {
    Arena frameArena;
    Arena jobsArena;
    Arena directoryArena;
    Arena historyArena;
    Arena errorArena;
    UI_Context ui;
    P_JobQueue jobs;
    P_Job *directoryJob;
    Prog_Directory directory;
    ArrayStr history;
    u64 historyAt;
    u64 generation;
    u64 focusedRow;
    u64 anchorRow;
    u64 selectedCount;
    b32 hasFocus;
    b32 hasAnchor;
    b32 loading;
    b32 showHidden;
    enum8(Prog_Sort) sort;
    b32 descending;
    String error;
    Prog_Command *commandFirst;
    Prog_Command *commandLast;
    b32 revealFocus;
    b32 resetScroll;
    u64 pageRows;  // rows visible in the last layout, for Page Up/Down
} Prog_State;

function b32 Prog_Init(Prog_State *state, String path, P_JobWakeCallback *wake, void *user);
function void Prog_Release(Prog_State *state);
function void Prog_RequestDirectory(Prog_State *state, String path, b32 addHistory, u64 historyAt);
function b32 Prog_TakeCompletions(Prog_State *state);
function String Prog_ParentPath(String path);
function String Prog_JoinPath(Arena *arena, String path, String name);
function i32 Prog_CompareNames(String a, String b);
function void Prog_SelectRow(Prog_State *state, u64 row, KeyMods mods);
function void Prog_OpenRow(Prog_State *state, u64 row);
function Prog_Command *Prog_QueueCommand(Prog_State *state, Prog_CommandKind kind, u64 row, u32 pressCount, u64 inputOrder);
function void Prog_HandleInput(Prog_State *state, PlatformEventList events);
function b32 Prog_ProcessCommands(Prog_State *state);
function void Prog_BuildUI(Prog_State *state, const R_Font *font, f32 scale);
