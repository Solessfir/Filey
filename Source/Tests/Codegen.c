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

    // A sequence keeps its first key, and a scope lets another command take the same key.
    {
        Codegen_ArrayCommand scoped = {0};
        Check(Codegen_ParseCommands(&arena, S("command A {\n    hotkey = \"G G\"\n    hotkey = \"Tab\"\n}\ncommand B {\n    hotkey = \"Tab\"\n    scope = \"Rename\"\n}"), &scoped, &error));
        String emitted = Codegen_EmitCommands(&arena, scoped);
        Check(TestCodegenContains(emitted, S("{ .key = Key_G, .mods = 0, .lead = Key_G, .command = Prog_CommandKind_A }")));
        Check(TestCodegenContains(emitted, S(".scope = Prog_HotkeyScope_Rename }")));
    }

    // The translatable strings are the literals of Tr, TrF and TrInit as written, each once, and TrN's other form; neither a macro's own #define nor a
    // longer name counts.
    {
        ArrayStr strings = {0}, plurals = {0};
        Codegen_CollectStrings(&arena, S("#define Tr(text) Prog_Tr(S(text))\nx = Tr(\"Open\"); y = TrF(\"%u \\\"items\\\"\"); z = TrInit(\"Open\"); w = NoTr(\"Skip\");\n"
                                         "v = TrN(Count(a), \"%S file\", \"%S files\"); u = TrN(1, \"%S file\", \"%S files\");\n"), &strings, &plurals);
        Check(strings.header.count == 2 && StrIsEqual(strings.v[0], S("Open")) && StrIsEqual(strings.v[1], S("%u \\\"items\\\"")));
        Check(plurals.header.count == 1 && StrIsEqual(plurals.v[0], S("%S files")));
        Check(TestCodegenContains(Codegen_EmitStrings(&arena, strings, plurals), S("    \"Open\",\n")));
    }

    // Each malformed input reports the offending line.
    struct { String input; String error; } failures[] = {
        { S("comand Open {\n}"), S("line 1: expected `command Name {`") },
        { S("command 1Open {\n}"), S("line 1: command names are letters and digits") },
        { S("command A {\n}\ncommand A {\n}"), S("line 3: duplicate command") },
        { S("command A {\n    label = Open\n}"), S("line 2: expected `key = \"value\"` without quotes or backslashes inside the value") },
        { S("command A {\n    colour = \"red\"\n}"), S("line 2: unknown key; expected label, tooltip, hotkey, repeat or scope") },
        { S("command A {\n    hotkey = \"Ctrl+Ctrl+A\"\n}"), S("line 2: hotkeys look like \"Ctrl+Shift+Key\"") },
        { S("command A {\n    repeat = \"maybe\"\n}"), S("line 2: repeat is \"true\" or \"false\"") },
        { S("command A {\n"), S("line 1: missing `}` for command A") },
        { S("command A {\n    hotkey = \"F5\"\n}\ncommand B {\n    hotkey = \"F5\"\n}"), S("hotkey F5 is bound twice (A, B)") },
        { S("command A {\n    scope = \"Browser\"\n}"), S("line 2: scope is \"Text\", \"Rename\", \"GoTo\" or \"Files\"") },
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
    Check(command && command->kind == Prog_CommandKind_ScrollExtendSelectPrev && command->mods == KeyMod_Shift && command->inputOrder == 4);
    command = command ? command->next : NULL;
    Check(command && command->kind == Prog_CommandKind_Up && command->inputOrder == 5);
    Check(command && command->next == NULL);

    // A key that runs a command does not also type its character, and the inspector's filter box takes what is typed while it is open.
    {
        Prog_State typing = {0};
        PlatformEvent keys[] = {
            { .kind = PlatformEventKind_KeyDown, .key = Key_Space },
            { .kind = PlatformEventKind_Text, .codepoint = ' ' },
            { .kind = PlatformEventKind_KeyDown, .key = Key_A },
            { .kind = PlatformEventKind_Text, .codepoint = 'a' },
        };
        Prog_HandleInput(&typing, TestUIEvents(keys, CArrayCount(keys)));
        Check(typing.commandFirst && typing.commandFirst->kind == Prog_CommandKind_TogglePreview && StrIsEqual(Prog_EditText(&typing.filter), S("a")));
        typing.inspectorFiltering = true;
        PlatformEvent more[] = { { .kind = PlatformEventKind_Text, .codepoint = 'x' }, { .kind = PlatformEventKind_KeyDown, .key = Key_Space }, { .kind = PlatformEventKind_Text, .codepoint = ' ' } };
        Prog_HandleInput(&typing, TestUIEvents(more, CArrayCount(more)));
        Check(StrIsEqual(Prog_EditText(&typing.inspectorFilter), S("x ")) && StrIsEqual(Prog_EditText(&typing.filter), S("a")));
        ArenaRelease(&typing.frameArena);
    }

    // A character key types into the filter until Character Key Action runs its hotkey; then "G G" runs on its second G, and a key bound to
    // nothing types nothing.
    {
        Prog_State filtering = {0};
        PlatformEvent keys[] = { { .kind = PlatformEventKind_KeyDown, .key = Key_J }, { .kind = PlatformEventKind_Text, .codepoint = 'j' },
                                 { .kind = PlatformEventKind_KeyDown, .key = Key_F, .mods = KeyMod_Shift }, { .kind = PlatformEventKind_Text, .codepoint = 'F' } };
        Prog_HandleInput(&filtering, TestUIEvents(keys, CArrayCount(keys)));
        Check(!filtering.commandFirst && StrIsEqual(Prog_EditText(&filtering.filter), S("jF")));
        ArenaRelease(&filtering.frameArena);

        Prog_State hotkeys = { .characterKeys = Prog_CharacterKeys_TriggerHotkey };
        PlatformEvent more[] = {
            { .kind = PlatformEventKind_KeyDown, .key = Key_J }, { .kind = PlatformEventKind_Text, .codepoint = 'j' },
            { .kind = PlatformEventKind_KeyDown, .key = Key_G }, { .kind = PlatformEventKind_Text, .codepoint = 'g' },
            { .kind = PlatformEventKind_KeyDown, .key = Key_G }, { .kind = PlatformEventKind_Text, .codepoint = 'g' },
            { .kind = PlatformEventKind_KeyDown, .key = Key_X }, { .kind = PlatformEventKind_Text, .codepoint = 'x' },
        };
        Prog_HandleInput(&hotkeys, TestUIEvents(more, CArrayCount(more)));
        Prog_Command *first = hotkeys.commandFirst;
        Check(first && first->kind == Prog_CommandKind_ScrollSelectNext && first->next && first->next->kind == Prog_CommandKind_ScrollSelectFirst && !first->next->next);
        Check(!hotkeys.filter.length && !hotkeys.filtering);
        ArenaRelease(&hotkeys.frameArena);

        Prog_Hotkey parsed = {0};
        Check(Prog_ParseHotkey(S(" z Z "), &parsed) && parsed.lead == Key_Z && parsed.key == Key_Z && !parsed.mods);
        Check(Prog_ParseHotkey(S("Ctrl + Shift + K"), &parsed) && !parsed.lead && parsed.key == Key_K && parsed.mods == (KeyMod_Control | KeyMod_Shift));
        Check(!Prog_ParseHotkey(S("Ctrl+G G"), &parsed) && !Prog_ParseHotkey(S("G Ctrl+G"), &parsed));
        Check(StrIsEqual(Prog_HotkeyString(&state.frameArena, &(Prog_Hotkey){ .key = Key_Comma, .mods = KeyMod_Control }, true), S("Ctrl+Comma")));
        Check(StrIsEqual(Prog_HotkeyString(&state.frameArena, &(Prog_Hotkey){ .key = Key_G, .lead = Key_G }, false), S("G G")));
    }

    // A chip being rebound takes the next key, which runs nothing and types nothing; a delete leaves the hotkey out, and restoring brings the
    // defaults back.
    {
        Prog_State keys = { .hotkeyKind = Prog_CommandKind_CopySelection, .hotkeySlot = 1 };
        PlatformEvent press[] = { { .kind = PlatformEventKind_KeyDown, .key = Key_Control, .mods = KeyMod_Control },
                                  { .kind = PlatformEventKind_KeyDown, .key = Key_F7, .mods = KeyMod_Control }, { .kind = PlatformEventKind_Text, .codepoint = 'x' } };
        Prog_HandleInput(&keys, TestUIEvents(press, CArrayCount(press)));
        Prog_Command *bind = keys.commandFirst;
        Check(bind && bind->kind == Prog_CommandKind_HotkeyBind && bind->row == Prog_CommandKind_CopySelection && bind->target == 1 && bind->pressCount == Key_F7);
        Check(bind && bind->mods == KeyMod_Control && !bind->next && !keys.filter.length);
        Prog_ProcessCommands(&keys);
        const Prog_Hotkey *found = Prog_FindHotkey(&keys, Prog_HotkeyScope_Browser, Key_None, Key_F7, KeyMod_Control);
        Check(found && found->command == Prog_CommandKind_CopySelection && !Prog_FindHotkey(&keys, Prog_HotkeyScope_Browser, Key_None, Key_Y, 0));
        Check(Prog_HotkeysChanged(&keys, Prog_CommandKind_CopySelection) && !keys.hotkeyKind);
        keys.commandFirst = keys.commandLast = NULL;
        Prog_QueueCommand(&keys, Prog_CommandKind_HotkeyDelete, Prog_CommandKind_CopySelection, 0, 0)->target = 0;
        Prog_ProcessCommands(&keys);
        Check(!Prog_FindHotkey(&keys, Prog_HotkeyScope_Browser, Key_None, Key_C, KeyMod_Control) && Prog_FindHotkey(&keys, Prog_HotkeyScope_Browser, Key_None, Key_F7, KeyMod_Control));
        keys.commandFirst = keys.commandLast = NULL;
        Prog_QueueCommand(&keys, Prog_CommandKind_HotkeyRestore, Prog_CommandKind_CopySelection, 0, 0);
        Prog_ProcessCommands(&keys);
        Check(Prog_FindHotkey(&keys, Prog_HotkeyScope_Browser, Key_None, Key_C, KeyMod_Control) && !Prog_HotkeysChanged(&keys, Prog_CommandKind_CopySelection));
        // Ctrl+R is Refresh's too, so both chips show the conflict.
        Prog_ApplySettings(&keys, S("hotkey.CalcDirsContents = Ctrl+R"));
        Prog_HotkeyChip chips[3];
        Check(Prog_HotkeyChips(&keys.frameArena, &keys, Prog_CommandKind_Refresh, chips, 3) == 2 && chips[0].conflict && !chips[1].conflict);
        ArenaRelease(&keys.frameArena);
    }

    // Escape removes a filter first, and then selects nothing.
    {
        Prog_State escape = {0};
        PlatformEvent keys[] = { { .kind = PlatformEventKind_KeyDown, .key = Key_Escape } };
        Prog_HandleInput(&escape, TestUIEvents(keys, CArrayCount(keys)));
        Check(escape.commandFirst && escape.commandFirst->kind == Prog_CommandKind_DeselectAll);
        escape.commandFirst = escape.commandLast = NULL;
        escape.filtering = true;
        Prog_HandleInput(&escape, TestUIEvents(keys, CArrayCount(keys)));
        Check(escape.commandFirst && escape.commandFirst->kind == Prog_CommandKind_RemoveFilter && !escape.commandFirst->next);
        ArenaRelease(&escape.frameArena);
    }

    // Every prompt and popup that takes the keyboard closes with Escape, answering no.
    {
        P_Job waiting = {0};
        PlatformEvent keys[] = { { .kind = PlatformEventKind_KeyDown, .key = Key_Escape } };
        for (u32 which = 0; which < 8; ++which) {
            Prog_State escape = {0};
            Prog_CommandKind expected = Prog_CommandKind_None;
            switch (which) {
                case 0: escape.confirmingDelete = true, expected = Prog_CommandKind_DeleteCancel; break;
                case 1: escape.hotkeyAsking = true, expected = Prog_CommandKind_HotkeyCancel; break;
                case 2: escape.paletteOpen = true, expected = Prog_CommandKind_PaletteClose; break;
                case 3: escape.menuOpen = true, expected = Prog_CommandKind_ContextMenuClose; break;
                case 4: escape.renaming = true, expected = Prog_CommandKind_RenameCancel; break;
                case 5: escape.inspectorFiltering = true, expected = Prog_CommandKind_InspectorRemoveFilter; break;
                case 6: escape.typesOpen = true, expected = Prog_CommandKind_TypesPopupClose; break;
                case 7: {
                    escape.transfers[0].job = &waiting;
                    atomic_store(&escape.transfers[0].progress.asking, true);
                    expected = Prog_CommandKind_TransferCancel;
                } break;
            }
            Prog_HandleInput(&escape, TestUIEvents(keys, CArrayCount(keys)));
            Check(escape.commandFirst && escape.commandFirst->kind == expected && !escape.commandFirst->next);
            ArenaRelease(&escape.frameArena);
        }
    }

    // Ctrl+Comma opens the options.
    {
        Prog_State options = {0};
        PlatformEvent keys[] = { { .kind = PlatformEventKind_KeyDown, .key = Key_Comma, .mods = KeyMod_Control }, { .kind = PlatformEventKind_Text, .codepoint = ',' } };
        Prog_HandleInput(&options, TestUIEvents(keys, CArrayCount(keys)));
        Check(options.commandFirst && options.commandFirst->kind == Prog_CommandKind_OpenOptions && !options.filter.length);
        ArenaRelease(&options.frameArena);
    }

    // Ctrl+wheel over the inspector sizes its contents, and elsewhere zooms the list.
    {
        Prog_State wheel = { .panelCount = 1, .panels[0].inspectorRect = { 100, 0, 200, 100 } };
        PlatformEvent turns[] = {
            { .kind = PlatformEventKind_MouseWheel, .mods = KeyMod_Control, .x = 150, .y = 50, .wheelY = 1 },
            { .kind = PlatformEventKind_MouseWheel, .mods = KeyMod_Control, .x = 50, .y = 50, .wheelY = -1 },
        };
        Prog_HandleInput(&wheel, TestUIEvents(turns, CArrayCount(turns)));
        Prog_Command *first = wheel.commandFirst;
        Check(first && first->kind == Prog_CommandKind_InspectorIncreaseContentsSize);
        Check(first && first->next && first->next->kind == Prog_CommandKind_ZoomOut);
        // An image zooms by a steady ratio from fitting the view, keeping its middle, and no smaller than fitting.
        wheel.imageFit = 0.5f;
        wheel.imagePan = (UI_Vec2){ 10, -4 };
        Prog__ScaleImage(&wheel, true);
        Check(wheel.imageScaleMode == Prog_ImageScale_Custom && fabsf(wheel.imageScale - 0.6f) < 1e-5f && fabsf(wheel.imagePan.x - 12) < 1e-4f);
        Prog__ScaleImage(&wheel, false);
        Prog__ScaleImage(&wheel, false);
        Check(fabsf(wheel.imageScale - 0.5f) < 1e-5f);
        ArenaRelease(&wheel.frameArena);
    }

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
