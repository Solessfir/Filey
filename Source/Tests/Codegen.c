// Command table generator and hotkey dispatch checks.

#define CodegenNoMain
#include "Tools/Codegen.c"

function b32 TestCodegenContains(String haystack, String needle)
{
    return StrIndexStr(haystack, needle, SSF_None).exists;
}

function void TestCodegenParse(void)
{
    Arena arena = {0};
    String input = S(
        "// comment\n"
        "command Open {\r\n"
        "    label   = \"Open^enter,run\"\n"
        "    tooltip = \"Open it.\"\n"
        "    hotkey  = \"Enter\"\n"
        "    hotkey  = \"Ctrl+Shift+0\"\n"
        "    repeat  = \"false\"\n"
        "}\n"
        "\n"
        "command Internal {\n"
        "}");
    Codegen_ArrayCommand commands = {0};
    String error = {0};
    Check(Codegen_ParseCommands(&arena, input, &commands, &error));
    Check(commands.header.count == 2);
    if (commands.header.count == 2) {
        Codegen_Command *open = &commands.v[0];
        Check(StrIsEqual(open->name, S("Open")) && StrIsEqual(open->label, S("Open")) && StrIsEqual(open->aliases, S("enter,run")));
        Check(StrIsEqual(open->tooltip, S("Open it.")) && open->noRepeat && open->hotkeys.header.count == 2);
        Check(StrIsEqual(open->hotkeys.v[1].key, S("0")) && open->hotkeys.v[1].mods == (KeyMod_Control | KeyMod_Shift));
        Check(StrIsEqual(commands.v[1].name, S("Internal")) && commands.v[1].label.length == 0);
    }

    String output = Codegen_EmitCommands(&arena, commands);
    Check(TestCodegenContains(output, S("    Prog_CommandKind_Open,\n    Prog_CommandKind_Internal,\n    Prog_CommandKind_Count,")));
    Check(TestCodegenContains(output, S("[Prog_CommandKind_Open] = { .name = { \"Open\", 4 }, .label = { \"Open\", 4 }, "
                                        ".aliases = { \"enter,run\", 9 }, .tooltip = { \"Open it.\", 8 }, .noRepeat = true },")));
    Check(TestCodegenContains(output, S("[Prog_CommandKind_Internal] = { .name = { \"Internal\", 8 } },")));
    Check(TestCodegenContains(output, S("{ .key = Key_0, .mods = KeyMod_Control | KeyMod_Shift, .command = Prog_CommandKind_Open }")));
    Check(TestCodegenContains(output, S("{ .key = Key_Enter, .mods = 0, .command = Prog_CommandKind_Open }")));

    // Each malformed input reports the offending line.
    struct { String input; String error; } failures[] = {
        { S("comand Open {\n}"), S("line 1: expected `command Name {`") },
        { S("command 1Open {\n}"), S("line 1: command names are letters and digits") },
        { S("command A {\n}\ncommand A {\n}"), S("line 3: duplicate command") },
        { S("command A {\n    label = Open\n}"), S("line 2: expected `key = \"value\"` without quotes or backslashes inside the value") },
        { S("command A {\n    colour = \"red\"\n}"), S("line 2: unknown key; expected label, tooltip, hotkey or repeat") },
        { S("command A {\n    hotkey = \"Ctrl+Ctrl+A\"\n}"), S("line 2: hotkeys look like \"Ctrl+Shift+Key\"") },
        { S("command A {\n    repeat = \"maybe\"\n}"), S("line 2: repeat is \"true\" or \"false\"") },
        { S("command A {\n"), S("line 1: missing `}` for command A") },
        { S("command A {\n    hotkey = \"F5\"\n}\ncommand B {\n    hotkey = \"F5\"\n}"), S("hotkey F5 is bound twice (A, B)") },
    };
    for (u32 i = 0; i < CArrayCount(failures); ++i) {
        Codegen_ArrayCommand ignored = {0};
        String message = {0};
        Check(!Codegen_ParseCommands(&arena, failures[i].input, &ignored, &message));
        Check(StrIsEqual(message, failures[i].error));
    }
    ArenaRelease(&arena);
}

function void TestHotkeyDispatch(void)
{
    Prog_State state = {0};
    PlatformEvent events[] = {
        { .kind = PlatformEventKind_KeyDown, .key = Key_F5 },
        { .kind = PlatformEventKind_KeyDown, .key = Key_F5, .mods = KeyMod_Control },        // no exact binding
        { .kind = PlatformEventKind_KeyDown, .key = Key_Enter, .isRepeat = true },           // OpenFocused ignores repeats
        { .kind = PlatformEventKind_KeyDown, .key = Key_Up, .mods = KeyMod_Shift },
        { .kind = PlatformEventKind_KeyDown, .key = Key_Up, .mods = KeyMod_Alt },
        { .kind = PlatformEventKind_KeyUp, .key = Key_A, .mods = KeyMod_Control },
    };
    Prog_HandleInput(&state, TestUIEvents(events, CArrayCount(events)));
    Prog_Command *command = state.commandFirst;
    Check(command && command->kind == Prog_CommandKind_Refresh && command->inputOrder == 1);
    command = command ? command->next : NULL;
    Check(command && command->kind == Prog_CommandKind_FocusUp && command->mods == KeyMod_Shift && command->inputOrder == 4);
    command = command ? command->next : NULL;
    Check(command && command->kind == Prog_CommandKind_Up && command->inputOrder == 5);
    Check(command && command->next == NULL);

    // Every generated table entry names a real command and binds a key.
    for (u64 i = 0; i < CArrayCount(Prog_DefaultHotkeys); ++i) {
        Check(Prog_DefaultHotkeys[i].command > Prog_CommandKind_None && Prog_DefaultHotkeys[i].command < Prog_CommandKind_Count);
        Check(Prog_DefaultHotkeys[i].key > Key_None && Prog_DefaultHotkeys[i].key < Key_Count);
    }
    for (u32 kind = Prog_CommandKind_None + 1; kind < Prog_CommandKind_Count; ++kind) {
        Check(Prog_Commands[kind].name.length != 0);
    }
    ArenaRelease(&state.frameArena);
}
