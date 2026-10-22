// Build-time generator: Program/Commands.txt -> Meta/Commands.meta.h, and the translatable strings of Program/*.c -> Meta/Strings.meta.h.
//
// The grammar is deliberately tiny: `command Name {`, `key = "value"` lines, `}`, blank lines and `//` comment lines.
// Anything else fails the build with file:line.
// The output is only rewritten when its content changes.
//
// Usage: codegen <Source directory>

#if !defined(CodegenNoMain)
#include "Base/Base.h"
#include "Platform/Platform.h"

#if defined(_WIN32)
#include "Win/WinPlatform.c"
#elif defined(__linux__)
#include "Linux/LinuxPlatform.c"
#endif

#include "Base/Base.c"
#endif

typedef struct Codegen_Hotkey {
    String key;    // Key_ name without the prefix
    KeyMods mods;
    String lead;   // a sequence's first key, as in "G G"; empty for one chord
} Codegen_Hotkey;

typedef struct Codegen_ArrayHotkey {
    ArrayHeader header;
    Codegen_Hotkey *v;
} Codegen_ArrayHotkey;

typedef struct Codegen_Command {
    String name;
    String label;
    String aliases;
    String tooltip;
    b32 noRepeat;
    String scope;  // a Prog_HotkeyScope_ name; empty for the browser
    Codegen_ArrayHotkey hotkeys;
} Codegen_Command;

typedef struct Codegen_ArrayCommand {
    ArrayHeader header;
    Codegen_Command *v;
} Codegen_ArrayCommand;

function b32 Codegen__IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
function b32 Codegen__IsIdentifier(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

function String Codegen__Trim(String s)
{
    u64 from = 0, to = s.length;
    while (from < to && Codegen__IsSpace(s.value[from])) ++from;
    while (to > from && Codegen__IsSpace(s.value[to - 1])) --to;
    return StrFromTo(s, from, (i64)to);
}

// Letters and digits only; command names must not start with a digit, key names may (Key_0).
function b32 Codegen__IsName(String s, b32 allowLeadingDigit)
{
    if (s.length == 0 || (!allowLeadingDigit && s.value[0] >= '0' && s.value[0] <= '9')) return false;
    for (u64 i = 0; i < s.length; ++i) {
        if (!Codegen__IsIdentifier(s.value[i]) || s.value[i] == '_') return false;
    }
    return true;
}

// "Ctrl+Shift+Up" -> mods + key name. Modifiers come first, the key last; "G G" is G pressed twice.
function b32 Codegen__ParseHotkey(String text, Codegen_Hotkey *hotkey)
{
    *hotkey = (Codegen_Hotkey){0};
    Index space = StrIndexChar(text, ' ', SSF_None);
    if (space.exists) {
        String lead = StrFromTo(text, 0, (i64)space.value);
        if (!Codegen__IsName(lead, true)) return false;
        b32 parsed = Codegen__ParseHotkey(StrFrom(text, space.value + 1), hotkey);
        hotkey->lead = lead;
        return parsed;
    }
    for (;;) {
        Index plus = StrIndexChar(text, '+', SSF_None);
        if (!plus.exists) break;
        String mod = StrFromTo(text, 0, (i64)plus.value);
        KeyMods bit = StrIsEqual(mod, S("Ctrl")) ? KeyMod_Control : StrIsEqual(mod, S("Shift")) ? KeyMod_Shift :
                      StrIsEqual(mod, S("Alt")) ? KeyMod_Alt : StrIsEqual(mod, S("Super")) ? KeyMod_Super : 0;
        if (bit == 0 || FlagIntersects(hotkey->mods, bit)) return false;
        FlagSet(hotkey->mods, bit);
        text = StrFrom(text, plus.value + 1);
    }
    hotkey->key = text;
    return Codegen__IsName(text, true);
}

// Parses `text`. On failure returns false and sets *error to "line N: message".
function b32 Codegen_ParseCommands(Arena *arena, String text, Codegen_ArrayCommand *commands, String *error)
{
    Codegen_Command *command = NULL;
    u32 lineNumber = 0;
    for (u64 at = 0; at < text.length;) {
        Index newline = StrIndexChar(StrFrom(text, at), '\n', SSF_CountOnFail);
        String line = Codegen__Trim(StrFromTo(text, at, (i64)(at + newline.value)));
        at += newline.value + 1;
        ++lineNumber;

        String message = {0};
        if (line.length == 0 || (line.length >= 2 && line.value[0] == '/' && line.value[1] == '/')) {
            continue;
        } else if (command == NULL) {
            String prefix = S("command ");
            b32 opens = line.length > prefix.length + 1 && StrIsEqual(StrFromTo(line, 0, (i64)prefix.length), prefix) &&
                        line.value[line.length - 1] == '{';
            String name = opens ? Codegen__Trim(StrFromTo(line, prefix.length, -1)) : (String){0};
            if (!opens) {
                message = S("expected `command Name {`");
            } else if (!Codegen__IsName(name, false)) {
                message = S("command names are letters and digits");
            } else {
                for (u64 i = 0; i < commands->header.count && !message.length; ++i) {
                    if (StrIsEqual(commands->v[i].name, name)) message = S("duplicate command");
                }
                if (!message.length) {
                    command = ArrayAddClear(arena, *commands, 1);
                    command->name = name;
                }
            }
        } else if (StrIsEqual(line, S("}"))) {
            command = NULL;
        } else {
            Index equals = StrIndexChar(line, '=', SSF_None);
            String key = equals.exists ? Codegen__Trim(StrFromTo(line, 0, (i64)equals.value)) : (String){0};
            String quoted = equals.exists ? Codegen__Trim(StrFrom(line, equals.value + 1)) : (String){0};
            String value = quoted.length >= 2 && quoted.value[0] == '"' && quoted.value[quoted.length - 1] == '"'
                         ? StrFromTo(quoted, 1, -1) : (String){0};
            // Values are emitted verbatim into C string literals.
            b32 plain = value.value != NULL && !StrIndexChar(value, '"', SSF_None).exists && !StrIndexChar(value, '\\', SSF_None).exists;
            if (!plain) {
                message = S("expected `key = \"value\"` without quotes or backslashes inside the value");
            } else if (StrIsEqual(key, S("label"))) {
                Index caret = StrIndexChar(value, '^', SSF_None);
                command->label = caret.exists ? StrFromTo(value, 0, (i64)caret.value) : value;
                command->aliases = caret.exists ? StrFrom(value, caret.value + 1) : (String){0};
                if (!command->label.length) message = S("empty label");
            } else if (StrIsEqual(key, S("tooltip"))) {
                command->tooltip = value;
            } else if (StrIsEqual(key, S("repeat"))) {
                if (StrIsEqual(value, S("false"))) command->noRepeat = true;
                else if (!StrIsEqual(value, S("true"))) message = S("repeat is \"true\" or \"false\"");
            } else if (StrIsEqual(key, S("hotkey"))) {
                Codegen_Hotkey hotkey = {0};
                if (!Codegen__ParseHotkey(value, &hotkey)) message = S("hotkeys look like \"Ctrl+Shift+Key\"");
                else ArrayPush(arena, command->hotkeys, hotkey);
            } else if (StrIsEqual(key, S("scope"))) {
                if (StrIsEqual(value, S("Text")) || StrIsEqual(value, S("Rename")) || StrIsEqual(value, S("GoTo")) || StrIsEqual(value, S("Files"))) command->scope = value;
                else message = S("scope is \"Text\", \"Rename\", \"GoTo\" or \"Files\"");
            } else {
                message = S("unknown key; expected label, tooltip, hotkey, repeat or scope");
            }
        }

        if (message.length) {
            *error = StrFormat(arena, "line %u: %S", lineNumber, message);
            return false;
        }
    }
    if (command != NULL) {
        *error = StrFormat(arena, "line %u: missing `}` for command %S", lineNumber, command->name);
        return false;
    }

    // One combination must mean one command where it works.
    for (u64 a = 0; a < commands->header.count; ++a) {
        for (u64 i = 0; i < commands->v[a].hotkeys.header.count; ++i) {
            Codegen_Hotkey first = commands->v[a].hotkeys.v[i];
            for (u64 b = a; b < commands->header.count; ++b) {
                if (!StrIsEqual(commands->v[a].scope, commands->v[b].scope)) continue;
                for (u64 j = b == a ? i + 1 : 0; j < commands->v[b].hotkeys.header.count; ++j) {
                    Codegen_Hotkey second = commands->v[b].hotkeys.v[j];
                    if (first.mods == second.mods && StrIsEqual(first.key, second.key) && StrIsEqual(first.lead, second.lead)) {
                        *error = StrFormat(arena, "hotkey %S is bound twice (%S, %S)", first.key, commands->v[a].name, commands->v[b].name);
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// S("...") is a compound literal, which ISO C does not accept as a static initializer, so tables get plain brace initializers with the length baked in.
function void Codegen__EmitString(Arena *arena, StrBuilder *out, const char *field, String value)
{
    StrBuildF(arena, out, ".%s = { \"%S\", ", field, value);
    StrBuildU64(arena, out, value.length);
    StrBuildCStr(arena, out, " }");
}

function String Codegen_EmitCommands(Arena *arena, Codegen_ArrayCommand commands)
{
    StrBuilder out = {0};
    StrBuildCStr(arena, &out,
        "// Generated by Source/Tools/Codegen.c from Source/Program/Commands.txt. Do not edit.\n"
        "//\n"
        "// Included once for the enum; define Prog_CommandsTable before a second include\n"
        "// to get the metadata and default hotkey tables.\n"
        "\n"
        "#if !defined(Prog_CommandsEnum)\n"
        "#define Prog_CommandsEnum\n"
        "typedef enum Prog_CommandKind {\n"
        "    Prog_CommandKind_None,\n");
    for (u64 i = 0; i < commands.header.count; ++i) {
        StrBuildF(arena, &out, "    Prog_CommandKind_%S,\n", commands.v[i].name);
    }
    StrBuildCStr(arena, &out,
        "    Prog_CommandKind_Count,\n"
        "} Prog_CommandKind;\n"
        "#endif\n"
        "\n"
        "#if defined(Prog_CommandsTable)\n"
        "global const Prog_CommandDesc Prog_Commands[Prog_CommandKind_Count] = {\n");
    for (u64 i = 0; i < commands.header.count; ++i) {
        Codegen_Command *command = &commands.v[i];
        StrBuildF(arena, &out, "    [Prog_CommandKind_%S] = { ", command->name);
        Codegen__EmitString(arena, &out, "name", command->name);
        const struct { const char *field; String value; } optional[] = {
            { "label", command->label }, { "aliases", command->aliases }, { "tooltip", command->tooltip },
        };
        for (u32 f = 0; f < CArrayCount(optional); ++f) {
            if (!optional[f].value.length) continue;
            StrBuildCStr(arena, &out, ", ");
            Codegen__EmitString(arena, &out, optional[f].field, optional[f].value);
        }
        if (command->noRepeat) StrBuildCStr(arena, &out, ", .noRepeat = true");
        if (command->scope.length) StrBuildF(arena, &out, ", .scope = Prog_HotkeyScope_%S", command->scope);
        StrBuildCStr(arena, &out, " },\n");
    }
    StrBuildCStr(arena, &out,
        "};\n"
        "\n"
        "global const Prog_Hotkey Prog_DefaultHotkeys[] = {\n");
    const struct { KeyMods mod; const char *name; } mods[] = {
        { KeyMod_Control, "KeyMod_Control" }, { KeyMod_Shift, "KeyMod_Shift" },
        { KeyMod_Alt, "KeyMod_Alt" }, { KeyMod_Super, "KeyMod_Super" },
    };
    for (u64 i = 0; i < commands.header.count; ++i) {
        for (u64 j = 0; j < commands.v[i].hotkeys.header.count; ++j) {
            Codegen_Hotkey hotkey = commands.v[i].hotkeys.v[j];
            StrBuildF(arena, &out, "    { .key = Key_%S, .mods = ", hotkey.key);
            b32 any = false;
            for (u32 m = 0; m < CArrayCount(mods); ++m) {
                if (FlagIntersects(hotkey.mods, mods[m].mod)) {
                    StrBuildF(arena, &out, "%s%s", any ? " | " : "", mods[m].name);
                    any = true;
                }
            }
            StrBuildCStr(arena, &out, any ? "" : "0");
            if (hotkey.lead.length) StrBuildF(arena, &out, ", .lead = Key_%S", hotkey.lead);
            StrBuildF(arena, &out, ", .command = Prog_CommandKind_%S },\n", commands.v[i].name);
        }
    }
    StrBuildCStr(arena, &out,
        "};\n"
        "#endif\n");
    return out.buffer;
}

function void Codegen_AddString(Arena *arena, ArrayStr *strings, String literal)
{
    b32 seen = false;
    for (u64 i = 0; i < strings->header.count && !seen; ++i) seen = StrIsEqual(strings->v[i], literal);
    if (!seen && literal.length) ArrayPush(arena, *strings, literal);
}

// The literal that starts at the quote at `*at`, escapes and all, with `*at` left on its closing quote.
function String Codegen__Literal(String text, u64 *at)
{
    u64 start = *at + 1, end = start;
    while (end < text.length && text.value[end] != '"' && text.value[end] != '\n') end += text.value[end] == '\\' ? 2 : 1;
    *at = Min(end, text.length);
    return StrFromTo(text, start, (i64)*at);
}

// The translatable strings of `text`, a source file, added once each: the literals of Tr("..."), TrF("...") and TrInit("...") to `strings`,
// and the other form of each TrN(count, "one", "other") to `plurals`, as written, escapes and all; a #define line is the macro itself, not a use.
function void Codegen_CollectStrings(Arena *arena, String text, ArrayStr *strings, ArrayStr *plurals)
{
    const String marks[] = { S("Tr(\""), S("TrF(\""), S("TrInit(\""), S("TrN(") };
    for (u64 at = 0; at < text.length; ++at) {
        b32 lineStart = at == 0 || text.value[at - 1] == '\n';
        if (lineStart && text.length - at >= 7 && StrIsEqual(StrFromTo(text, at, (i64)(at + 7)), S("#define"))) {
            while (at < text.length && text.value[at] != '\n') ++at;
            continue;
        }
        if (at && Codegen__IsIdentifier(text.value[at - 1])) continue;
        for (u32 m = 0; m < CArrayCount(marks); ++m) {
            if (text.length - at < marks[m].length || !StrIsEqual(StrFromTo(text, at, (i64)(at + marks[m].length)), marks[m])) continue;
            at += marks[m].length - 1;
            if (m < 3) {
                Codegen_AddString(arena, strings, Codegen__Literal(text, &at));
                break;
            }
            // The count comes first, then the two forms.
            while (at < text.length && text.value[at] != '"') ++at;
            if (at < text.length) Codegen__Literal(text, &at);
            for (++at; at < text.length && text.value[at] != '"'; ++at) {}
            if (at < text.length) Codegen_AddString(arena, plurals, Codegen__Literal(text, &at));
            break;
        }
    }
}

function String Codegen_EmitStrings(Arena *arena, ArrayStr strings, ArrayStr plurals)
{
    StrBuilder out = {0};
    StrBuildCStr(arena, &out,
        "// Generated by Source/Tools/Codegen.c from the Tr(\"...\") strings of Source/Program and the commands' labels and tooltips. Do not edit.\n"
        "// The English text of every translatable string, which a new language file lists, and the counted ones by their other form.\n"
        "\n"
        "global const char *const Prog_Strings[] = {\n");
    for (u64 i = 0; i < strings.header.count; ++i) StrBuildF(arena, &out, "    \"%S\",\n", strings.v[i]);
    StrBuildCStr(arena, &out, "    \"\",\n};\n\nglobal const char *const Prog_PluralStrings[] = {\n");
    for (u64 i = 0; i < plurals.header.count; ++i) StrBuildF(arena, &out, "    \"%S\",\n", plurals.v[i]);
    StrBuildCStr(arena, &out, "    \"\",\n};\n");
    return out.buffer;
}

#if !defined(CodegenNoMain)
#include <stdio.h>

function String Codegen__ReadFile(Arena *arena, const char *path)
{
    String result = {0};
    FILE *file = fopen(path, "rb");
    if (file) {
        StrBuilder builder = {0};
        char chunk[4096];
        for (u64 count; (count = fread(chunk, 1, sizeof(chunk), file)) > 0;) {
            StrBuildBytes(arena, &builder, chunk, count);
        }
        result = builder.buffer;
        if (!result.value) result = StrMake("", 0);
        fclose(file);
    }
    return result;
}

// Writes `output` to `path` unless it already holds it, which leaves its time alone; false when it cannot.
function b32 Codegen__WriteIfChanged(Arena *arena, const char *path, String output)
{
    String existing = Codegen__ReadFile(arena, path);
    if (existing.value && StrIsEqual(existing, output)) return true;
    FILE *file = fopen(path, "wb");
    b32 written = file && fwrite(output.value, 1, output.length, file) == output.length;
    if (file) written = fclose(file) == 0 && written;
    if (!written) fprintf(stderr, "%s: cannot write\n", path);
    return written;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: codegen <Source directory>\n");
        return 1;
    }
    Arena arena = {0};
    // Paths carry their terminator so they can go straight to fopen.
    String source = StrFromCStr(argv[1]);
    String inputPath = StrFormat(&arena, "%S/Program/Commands.txt%c", source, 0);
    String outputPath = StrFormat(&arena, "%S/Meta/Commands.meta.h%c", source, 0);

    String input = Codegen__ReadFile(&arena, inputPath.value);
    if (!input.value) {
        fprintf(stderr, "%s: cannot read\n", inputPath.value);
        return 1;
    }
    Codegen_ArrayCommand commands = {0};
    String error = {0};
    if (!Codegen_ParseCommands(&arena, input, &commands, &error)) {
        fprintf(stderr, "%s: %.*s\n", inputPath.value, (int)error.length, error.value);
        return 1;
    }

    if (!Codegen__WriteIfChanged(&arena, outputPath.value, Codegen_EmitCommands(&arena, commands))) return 1;
    // The strings, in the order the files have them.
    const char *sources[] = { "Program/Program.c", "Program/Commands.c", "Program/Directory.c" };
    ArrayStr strings = {0}, plurals = {0};
    for (u32 i = 0; i < CArrayCount(sources); ++i) {
        String path = StrFormat(&arena, "%S/%s%c", source, sources[i], 0);
        String text = Codegen__ReadFile(&arena, path.value);
        if (!text.value) {
            fprintf(stderr, "%s: cannot read\n", path.value);
            return 1;
        }
        Codegen_CollectStrings(&arena, text, &strings, &plurals);
    }
    for (u64 i = 0; i < commands.header.count; ++i) {
        Codegen_AddString(&arena, &strings, commands.v[i].label);
        Codegen_AddString(&arena, &strings, commands.v[i].tooltip);
    }
    String stringsPath = StrFormat(&arena, "%S/Meta/Strings.meta.h%c", source, 0);
    if (!Codegen__WriteIfChanged(&arena, stringsPath.value, Codegen_EmitStrings(&arena, strings, plurals))) return 1;
    return 0;
}
#endif
