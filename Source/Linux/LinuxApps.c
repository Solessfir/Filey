// Applications from the freedesktop desktop entries, for Open With and Open in Terminal.
// Requires LinuxIcons.c for the INI parser, the data directories and the MIME table.

typedef struct P__App {
    String id;         // desktop file ID, "org.gnome.TextEditor.desktop"
    String name;
    String exec;
    String mimeTypes;  // ";"-separated, as in the file
    String icon;       // a theme icon's name, as "org.gnome.TextEditor", or a file's path
    b32 terminal;
    b32 noDisplay;
} P__App;

typedef struct P__ArrayApp { ArrayHeader header; P__App *v; } P__ArrayApp;
typedef struct P__ArrayStrMap { ArrayHeader header; P__StrMap *v; } P__ArrayStrMap;

typedef struct P__AppData {
    Arena arena;
    P__ArrayApp apps;
    P__ArrayStrMap associations;  // parsed mimeapps.list files, in lookup order
} P__AppData;

// Loaded once on first use and read-only afterwards.
// ponytail: applications installed while Filey runs show after a restart; watch the applications folders if that matters.
global P__AppData P__apps;
global pthread_once_t P__appsOnce = PTHREAD_ONCE_INIT;

function void P__AddDesktopFiles(P__AppData *data, String folder, String prefix, u32 depth)
{
    Arena *arena = &data->arena;
    Scratch scratch = GetScratchEx(arena);
    char *native = P__FilePathCString(scratch.arena, folder);
    DIR *directory = native ? opendir(native) : NULL;
    for (struct dirent *entry; directory && (entry = readdir(directory)) != NULL;) {
        String name = StrFromCStr(entry->d_name);
        if (!name.length || name.value[0] == '.') continue;
        String path = StrFormat(scratch.arena, "%S/%S", folder, name);
        // Desktop file IDs name files in subfolders with '-' for each '/'.
        if (entry->d_type == DT_DIR || entry->d_type == DT_UNKNOWN) {
            if (depth < 2 && P__IsDirectory(scratch.arena, path)) P__AddDesktopFiles(data, path, StrFormat(scratch.arena, "%S%S-", prefix, name), depth + 1);
            if (entry->d_type == DT_DIR) continue;
        }
        if (name.length <= 8 || !StrIsEqual(StrFrom(name, name.length - 8), S(".desktop"))) continue;
        String id = StrFormat(scratch.arena, "%S%S", prefix, name);
        // Earlier data folders override later ones.
        b32 seen = false;
        for (u64 i = 0; i < data->apps.header.count && !seen; ++i) seen = StrIsEqual(data->apps.v[i].id, id);
        if (seen) continue;
        P__StrMap ini = P__IniParse(scratch.arena, P__ReadFile(scratch.arena, path));
        String section = S("Desktop Entry");
        P__App app = {
            .id = StrCopy(arena, id),
            .name = StrCopy(arena, P__IniGet(ini, section, S("Name"))),
            .exec = StrCopy(arena, P__IniGet(ini, section, S("Exec"))),
            .mimeTypes = StrCopy(arena, P__IniGet(ini, section, S("MimeType"))),
            .icon = StrCopy(arena, P__IniGet(ini, section, S("Icon"))),
            .terminal = StrIsEqual(P__IniGet(ini, section, S("Terminal")), S("true")),
            .noDisplay = StrIsEqual(P__IniGet(ini, section, S("NoDisplay")), S("true")),
        };
        // A hidden entry still claims its ID, so that it masks the same ID in later folders.
        if (!StrIsEqual(P__IniGet(ini, section, S("Type")), S("Application")) || StrIsEqual(P__IniGet(ini, section, S("Hidden")), S("true")) ||
            !app.exec.length || !app.name.length) {
            app.exec = (String){0};
        }
        ArrayPush(arena, data->apps, app);
    }
    if (directory) closedir(directory);
    ReleaseScratch(scratch);
}

// Reads the desktop entries of every data folder and the mimeapps.list files that choose among them.
function void P__LoadApps(P__AppData *data)
{
    Arena *arena = &data->arena;
    ArrayStr dataDirs = P__DataDirs(arena);
    for (u64 i = 0; i < dataDirs.header.count; ++i) P__AddDesktopFiles(data, StrFormat(arena, "%S/applications", dataDirs.v[i]), (String){0}, 0);
    ArrayStr lists = {0};
    const char *configHome = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME"), *configDirs = getenv("XDG_CONFIG_DIRS");
    if (configHome && configHome[0] == '/') ArrayPush(arena, lists, StrFormat(arena, "%s/mimeapps.list", configHome));
    else if (home && home[0] == '/') ArrayPush(arena, lists, StrFormat(arena, "%s/.config/mimeapps.list", home));
    ArrayStr dirs = StrSplit(arena, configDirs && configDirs[0] ? StrFromCStr(configDirs) : S("/etc/xdg"), ':');
    for (u64 i = 0; i < dirs.header.count; ++i) ArrayPush(arena, lists, StrFormat(arena, "%S/mimeapps.list", dirs.v[i]));
    for (u64 i = 0; i < dataDirs.header.count; ++i) ArrayPush(arena, lists, StrFormat(arena, "%S/applications/mimeapps.list", dataDirs.v[i]));
    for (u64 i = 0; i < lists.header.count; ++i) {
        String text = P__ReadFile(arena, lists.v[i]);
        if (text.length) ArrayPush(arena, data->associations, P__IniParse(arena, text));
    }
}

function void P__LoadGlobalApps(void) { P__LoadApps(&P__apps); }

function P__App *P__FindApp(const P__AppData *data, String id)
{
    for (u64 i = 0; i < data->apps.header.count; ++i) {
        if (StrIsEqual(data->apps.v[i].id, id)) return data->apps.v[i].exec.length ? &data->apps.v[i] : NULL;
    }
    return NULL;
}

function b32 P__ListHas(String list, String item)
{
    for (u64 from = 0, to = 0; to <= list.length; ++to) {
        if (to < list.length && list.value[to] != ';') continue;
        if (StrIsEqual(P__Trim(StrFromTo(list, from, (i64)to)), item)) return true;
        from = to + 1;
    }
    return false;
}

// Byte order with ASCII letters folded, for listing application names.
function i32 P__CompareFolded(String a, String b)
{
    for (u64 i = 0; i < a.length && i < b.length; ++i) {
        u8 x = (u8)a.value[i], y = (u8)b.value[i];
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x < y ? -1 : 1;
    }
    return a.length == b.length ? 0 : a.length < b.length ? -1 : 1;
}

// The defaults chosen while Filey runs, which mimeapps.list, read once, does not hold yet: an application's desktop file ID for each type.
// ponytail: a fixed table under a lock; the newest choice for a type replaces the last.
global struct { char mime[128]; char id[256]; } P__chosenDefaults[32];
global u32 P__chosenDefaultCount;
global pthread_mutex_t P__chosenDefaultsLock = PTHREAD_MUTEX_INITIALIZER;

// Makes `id` the default application for `mime`, in the user's mimeapps.list, which other programs follow too, and at once for this run.
// The line for the type in its Default Applications section is replaced, or added, and the rest of the file is kept as it was.
function P_FileError P__SetDefaultApp(Arena *arena, String mime, String id)
{
    pthread_mutex_lock(&P__chosenDefaultsLock);
    u32 slot = P__chosenDefaultCount < CArrayCount(P__chosenDefaults) ? P__chosenDefaultCount++ : CArrayCount(P__chosenDefaults) - 1;
    snprintf(P__chosenDefaults[slot].mime, sizeof(P__chosenDefaults[slot].mime), "%.*s", (i32)mime.length, mime.value);
    snprintf(P__chosenDefaults[slot].id, sizeof(P__chosenDefaults[slot].id), "%.*s", (i32)id.length, id.value);
    pthread_mutex_unlock(&P__chosenDefaultsLock);
    Scratch scratch = GetScratchEx(arena);
    const char *config = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    String path = config && config[0] == '/' ? StrFormat(scratch.arena, "%s/mimeapps.list", config) : StrFormat(scratch.arena, "%s/.config/mimeapps.list", home ? home : "");
    String text = {0}, entry = StrFormat(scratch.arena, "%S=%S;", mime, id), key = StrFormat(scratch.arena, "%S=", mime);
    P_ReadFile(scratch.arena, path, &text);
    StrBuilder out = {0};
    b32 inDefaults = false, placed = false;
    // Line by line, empty ones too, so that the file keeps its look.
    for (u64 at = 0; at < text.length;) {
        u64 end = at;
        while (end < text.length && text.value[end] != '\n') ++end;
        String line = StrFromTo(text, at, (i64)end);
        at = end + 1;
        if (line.length && line.value[0] == '[') {
            // The section ends: the type's line goes at its end when it had none.
            if (inDefaults && !placed) StrBuildStr(scratch.arena, &out, StrFormat(scratch.arena, "%S\n", entry)), placed = true;
            inDefaults = StrIsEqual(P__Trim(line), S("[Default Applications]"));
        } else if (inDefaults && line.length >= key.length && StrIsEqual(StrFromTo(line, 0, (i64)key.length), key)) {
            if (!placed) StrBuildStr(scratch.arena, &out, StrFormat(scratch.arena, "%S\n", entry)), placed = true;
            continue;
        }
        StrBuildStr(scratch.arena, &out, StrFormat(scratch.arena, "%S\n", line));
    }
    if (inDefaults && !placed) StrBuildStr(scratch.arena, &out, StrFormat(scratch.arena, "%S\n", entry)), placed = true;
    if (!placed) StrBuildStr(scratch.arena, &out, StrFormat(scratch.arena, "%s[Default Applications]\n%S\n", out.buffer.length ? "\n" : "", entry));
    P_FileError result = P_WriteFile(arena, path, out.buffer);
    ReleaseScratch(scratch);
    return result;
}

// Applications for `mime`, best first: the default, the associations the user added, then every other entry that lists the type, by name,
// unless `chosenOnly` keeps just those that mimeapps.list chooses.
function u64 P__AppsFor(const P__AppData *data, String mime, b32 chosenOnly, P__App **out, u64 capacity)
{
    u64 count = 0;
    // A default chosen in this run first.
    pthread_mutex_lock(&P__chosenDefaultsLock);
    for (u32 i = P__chosenDefaultCount; i > 0 && !count && capacity; --i) {
        if (!StrIsEqual(StrFromCStr(P__chosenDefaults[i - 1].mime), mime)) continue;
        P__App *chosen = P__FindApp(data, StrFromCStr(P__chosenDefaults[i - 1].id));
        if (chosen) out[count++] = chosen;
        break;
    }
    pthread_mutex_unlock(&P__chosenDefaultsLock);
    Scratch scratch = GetScratch();
    StrBuilder removed = {0};
    for (u64 i = 0; i < data->associations.header.count; ++i) {
        StrBuildStr(scratch.arena, &removed, P__IniGet(data->associations.v[i], S("Removed Associations"), mime));
        StrBuildChar(scratch.arena, &removed, ';');
    }
    String sections[2] = { S("Default Applications"), S("Added Associations") };
    for (u32 s = 0; s < 2; ++s) {
        for (u64 i = 0; i < data->associations.header.count; ++i) {
            ArrayStr ids = StrSplit(scratch.arena, P__IniGet(data->associations.v[i], sections[s], mime), ';');
            b32 found = false;
            for (u64 j = 0; j < ids.header.count && count < capacity; ++j) {
                P__App *app = P__FindApp(data, P__Trim(ids.v[j]));
                if (!app || P__ListHas(removed.buffer, app->id)) continue;
                b32 listed = false;
                for (u64 k = 0; k < count && !listed; ++k) listed = out[k] == app;
                if (!listed) out[count++] = app;
                found = true;
                // Only the first usable default counts.
                if (s == 0) break;
            }
            if (s == 0 && found) break;
        }
    }
    u64 sorted = count;
    for (u64 i = 0; !chosenOnly && i < data->apps.header.count && count < capacity; ++i) {
        P__App *app = &data->apps.v[i];
        if (!app->exec.length || app->noDisplay || !P__ListHas(app->mimeTypes, mime) || P__ListHas(removed.buffer, app->id)) continue;
        b32 listed = false;
        for (u64 k = 0; k < count && !listed; ++k) listed = out[k] == app;
        if (listed) continue;
        u64 at = count++;
        while (at > sorted && P__CompareFolded(out[at - 1]->name, app->name) > 0) {
            out[at] = out[at - 1];
            --at;
        }
        out[at] = app;
    }
    ReleaseScratch(scratch);
    return count;
}

// The MIME type a file's name gives it.
function String P__MimeForName(String name, b32 folder)
{
    if (folder) return S("inode/directory");
    pthread_once(&P__iconsOnce, P__LoadIconData);
    Scratch scratch = GetScratch();
    String key = P_FileTypeKey(scratch.arena, name, P_FileKind_File);
    String result = key.length > 1 && key.value[0] == '.' ? MapsGet(P__Icons()->mimeByExtension, String, StrFrom(key, 1)) : (String){0};
    ReleaseScratch(scratch);
    return result.length ? result : S("application/octet-stream");
}

function String P__ShellQuote(Arena *arena, String text);

function String P__ShellQuoteInside(Arena *arena, String text, char quote);

// Whether `word` names a POSIX shell, by its path's last part, whose quoting is the one here.
function b32 P__IsPosixShellWord(String word)
{
    local_persist const String shells[] = {
        SInit("sh"), SInit("bash"), SInit("dash"), SInit("zsh"), SInit("ksh"), SInit("mksh"), SInit("ash"), SInit("yash"), SInit("posh"), SInit("osh"),
    };
    String leaf = StrFrom(word, StrIndexChar(word, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
    for (u32 i = 0; i < CArrayCount(shells); ++i) if (StrIsEqual(leaf, shells[i])) return true;
    return false;
}

// Whether `word` names a shell that quotes otherwise than the POSIX one, to which a value is never handed; `plain` leaves out those whose
// names are also common words, as es and rc, for a word that is not a command.
function b32 P__IsForeignShellWord(String word, b32 plain)
{
    local_persist const String shells[] = {
        SInit("fish"), SInit("csh"), SInit("tcsh"), SInit("elvish"), SInit("xonsh"), SInit("pwsh"), SInit("powershell"), SInit("ysh"), SInit("murex"),
    };
    local_persist const String words[] = { SInit("rc"), SInit("es"), SInit("nu"), SInit("ion") };
    String leaf = StrFrom(word, StrIndexChar(word, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
    for (u32 i = 0; i < CArrayCount(shells); ++i) if (StrIsEqual(leaf, shells[i])) return true;
    for (u32 i = 0; !plain && i < CArrayCount(words); ++i) if (StrIsEqual(leaf, words[i])) return true;
    return false;
}

// Whether `word` names a shell of either kind, or is a variable's value run as one, as $SHELL is.
function b32 P__IsShellWord(String word) { return (word.length && word.value[0] == '$') || P__IsPosixShellWord(word) || P__IsForeignShellWord(word, false); }

// Whether `word` names a program that runs its arguments as a shell's command, or eval, which runs its words again.
function b32 P__IsLauncherWord(String word)
{
    local_persist const String launchers[] = {
        SInit("eval"), SInit("."), SInit("source"), SInit("su"), SInit("runuser"), SInit("watch"), SInit("script"), SInit("flock"), SInit("ssh"), SInit("tmux"), SInit("screen"), SInit("parallel"), SInit("xargs"),
    };
    String leaf = StrFrom(word, StrIndexChar(word, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
    for (u32 i = 0; i < CArrayCount(launchers); ++i) if (StrIsEqual(leaf, launchers[i])) return true;
    return false;
}

// Whether `word` names a terminal whose -e, given one string, runs it in a shell or splits it again; given several words, it runs them as they are.
function b32 P__IsTerminalWord(String word)
{
    local_persist const String terminals[] = {
        SInit("xterm"), SInit("uxterm"), SInit("x-terminal-emulator"), SInit("xfce4-terminal"), SInit("mate-terminal"), SInit("gnome-terminal"), SInit("lxterminal"),
        SInit("terminator"), SInit("tilix"), SInit("konsole"), SInit("urxvt"), SInit("rxvt"), SInit("qterminal"), SInit("sakura"), SInit("terminology"),
    };
    String leaf = StrFrom(word, StrIndexChar(word, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
    for (u32 i = 0; i < CArrayCount(terminals); ++i) if (StrIsEqual(leaf, terminals[i])) return true;
    return false;
}

// The characters that end a shell's word outside quotes, as in "true;sh -c".
function b32 P__IsShellBreak(char c) { return c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '&' || c == '|' || c == '(' || c == ')' || c == '<' || c == '>' || c == '`'; }

// Whether `word` is a program that runs the command named after it, so the real command word follows, past the word's own options: env (its -S aside), sudo, nice and the like.
function b32 P__IsWrapperWord(String word)
{
    local_persist const String wrappers[] = {
        SInit("env"), SInit("sudo"), SInit("doas"), SInit("pkexec"), SInit("run0"), SInit("nice"), SInit("nohup"), SInit("setsid"),
        SInit("ionice"), SInit("chrt"), SInit("exec"), SInit("command"), SInit("builtin"), SInit("time"), SInit("setpriv"), SInit("stdbuf"),
    };
    String leaf = StrFrom(word, StrIndexChar(word, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
    for (u32 i = 0; i < CArrayCount(wrappers); ++i) if (StrIsEqual(leaf, wrappers[i])) return true;
    return false;
}

// Whether `word`, an option of a POSIX shell's, gives it a command to run: a short one holding c, as -c or -lc.
function b32 P__ShellCommandOption(String word)
{
    return word.length >= 2 && word.value[0] == '-' && word.value[1] != '-' && StrIndexChar(word, 'c', SSF_None).exists;
}

// Whether a shell's option `word` takes the word after it as its argument: -o and -O an option's name, as "-o pipefail", and --rcfile and
// --init-file a file.
function b32 P__ShellOptionTakesWord(String word)
{
    if (StrIsEqual(word, S("--rcfile")) || StrIsEqual(word, S("--init-file"))) return true;
    return word.length >= 2 && word.value[1] != '-' && (word.value[word.length - 1] == 'o' || word.value[word.length - 1] == 'O');
}

// Walks the options of a shell, the words from `words` up to `count` or an empty one, which stands for an operator: whether one gives it a
// command, with `*end` at its first operand, past a "--".
function b32 P__ShellOptions(const String *words, u64 count, u64 *end)
{
    b32 command = false;
    u64 at = 0;
    for (; at < count && words[at].length; ++at) {
        String word = words[at];
        if (StrIsEqual(word, S("--"))) {
            ++at;
            break;
        }
        if (word.length < 2 || (word.value[0] != '-' && word.value[0] != '+')) break;
        command |= P__ShellCommandOption(word);
        if (P__ShellOptionTakesWord(word)) ++at;
    }
    *end = Min(at, count);
    return command;
}

// Whether the word after `words` is the command a shell runs: its first operand, after options of which one gives it a command, as "sh -c",
// "bash -lc", "bash -o pipefail -c", "sh -c -e" or "sh -c --".
// The last word naming a shell counts, wherever it is, as in "env sh -c" or "sudo bash -c"; taking a word for a shell's command that is not one is the safe mistake.
function b32 P__ShellScriptNext(const String *words, u64 count)
{
    for (u64 s = count; s-- > 0;) {
        if (!P__IsShellWord(words[s])) continue;
        u64 end = 0;
        b32 command = P__ShellOptions(words + s + 1, count - s - 1, &end);
        return command && s + 1 + end == count;
    }
    return false;
}

// What stands for each value in the line checked: a ( a shell takes for code wherever it reaches one unquoted, which the check refuses,
// and which is refused too where it would name a shell's option, or the program given arguments.
global const String P__standIn = SInit("a (b");

// Whether `text`, a command line a value is put into, is one whose quoting the code here follows whole, so that the value cannot reach a
// second parse that runs it as code.
// It is a list of programs and their arguments, joined by | || ; and &&, and redirected with < > and >> to a file, a descriptor's number before
// them, of plain words and quoted strings with nothing in them that expands: outside quotes only letters, digits and - _ . / , : = + @ % ^ ~,
// and a backslash before one of those or a space, a quote or a backslash; in double quotes a backslash only before " or \; and no $ or
// backtick anywhere. Line breaks, a lone &, groups, comments and here-documents are left out whole, as the shell reads each in ways of its own.
// `shell` is whether a shell parses the line, as a Nemo action's, rather than its words being run as they are, as a desktop entry's.
// A shell may be in it only as the whole line: one of sh, bash, dash, ash or posh, after wrappers with no options, given a script with -c
// that has no shell of its own, and no option that has it read a file as it starts, as -i, -l, --login or --rcfile.
// Any other shell, a shell named as an argument, a program that runs its arguments as a command (eval, watch, script, ssh and the like),
// a shell's own command that evaluates a name or runs a string (trap, unset, read, printf -v and the like), env -S, a wrapper given
// options, and a terminal's -e given one string are refused.
function b32 P__CommandAllowed(Arena *arena, String text, u32 depth, b32 shell)
{
    // The words and operators, the quotes and escapes taken off each word so that a quoted script is one word.
    typedef struct P__Tok { String word; char op; } P__Tok;
    P__Tok *toks = ArenaPushArray(arena, text.length + 1, P__Tok);
    u64 count = 0;
    for (u64 at = 0; at < text.length;) {
        u8 c = (u8)text.value[at];
        if (c == ' ' || c == '\t') { ++at; continue; }
        if (c == '|' || c == ';' || c == '<' || c == '>') { toks[count++] = (P__Tok){ .op = (char)c }; ++at; continue; }
        // && joins commands as ; does; a lone & runs one in the background, which nothing else here waits for.
        if (c == '&') {
            if (at + 1 >= text.length || text.value[at + 1] != '&') return false;
            toks[count++] = (P__Tok){ .op = ';' };
            at += 2;
            continue;
        }
        StrBuilder word = {0};
        b32 digits = true;
        for (; at < text.length;) {
            u8 d = (u8)text.value[at];
            if (d == ' ' || d == '\t' || d == '|' || d == ';' || d == '<' || d == '>' || d == '&') break;
            digits &= d >= '0' && d <= '9';
            if (d == '\'' || d == '"') {
                for (++at; at < text.length && text.value[at] != d; ++at) {
                    char e = text.value[at];
                    if (e == '`' || e == '$') return false;
                    if (d == '"' && e == '\\') {
                        if (at + 1 >= text.length || (text.value[at + 1] != '"' && text.value[at + 1] != '\\')) return false;
                        e = text.value[++at];
                    }
                    StrBuildChar(arena, &word, e);
                }
                if (at >= text.length) return false;
                ++at;
                continue;
            }
            b32 escaped = d == '\\' && at + 1 < text.length;
            if (escaped) d = (u8)text.value[++at];
            b32 plain = d >= 0x80 || (d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9') || StrIndexChar(S("-_./,:=+@%^~"), (char)d, SSF_None).exists ||
                        (escaped && StrIndexChar(S(" \\\"'"), (char)d, SSF_None).exists);
            if (!plain) return false;
            StrBuildChar(arena, &word, (char)d);
            ++at;
        }
        // A descriptor's number joined to a redirection, as 2 in 2>/dev/null, is part of it, not a word.
        if (digits && at < text.length && (text.value[at] == '<' || text.value[at] == '>')) continue;
        toks[count++] = (P__Tok){ .word = StrCopy(arena, word.buffer) };
    }
    // A redirection takes the word after it, a file, out of its command, which goes on after it; >> appends, and anything else after a
    // < or > is refused, as << and >&.
    b32 operators = false;
    u64 kept = 0;
    for (u64 i = 0; i < count; ++i) {
        if (toks[i].op == '<' || toks[i].op == '>') {
            if (toks[i].op == '>' && i + 1 < count && toks[i + 1].op == '>' && (i == 0 || toks[i - 1].op != '>')) continue;
            if (i + 1 >= count || toks[i + 1].op) return false;
            ++i;
            continue;
        }
        operators |= toks[i].op != 0;
        toks[kept++] = toks[i];
    }
    count = kept;
    // The shell's own commands that evaluate a name as an expression, as bash does an array's subscript, or run a string.
    local_persist const String evaluating[] = {
        SInit("trap"), SInit("unset"), SInit("declare"), SInit("typeset"), SInit("local"), SInit("export"), SInit("readonly"), SInit("read"),
        SInit("mapfile"), SInit("readarray"), SInit("getopts"), SInit("alias"), SInit("let"), SInit("test"), SInit("["),
        SInit("compgen"), SInit("complete"), SInit("wait"), SInit("fc"), SInit("hash"), SInit("type"), SInit("enable"), SInit("shopt"),
        SInit("set"), SInit("print"), SInit("integer"), SInit("nameref"), SInit("float"), SInit("coproc"), SInit("function"),
    };
    // The words that start a part of a compound command, after which its command comes.
    local_persist const String reserved[] = {
        SInit("if"), SInit("then"), SInit("else"), SInit("elif"), SInit("fi"), SInit("do"), SInit("done"), SInit("while"), SInit("until"),
        SInit("for"), SInit("case"), SInit("esac"), SInit("in"), SInit("select"),
    };
    for (u64 i = 0; i < count;) {
        if (toks[i].op) {
            ++i;
            continue;
        }
        u64 from = i;
        while (i < count && !toks[i].op) ++i;
        String *words = ArenaPushArray(arena, i - from, String);
        u64 n = 0;
        for (u64 k = from; k < i; ++k) words[n++] = toks[k].word;
        // Wrappers that run the command after them, with no options of their own.
        u64 at = 0;
        for (b32 more = true; more && at < n;) {
            more = false;
            for (u32 r = 0; shell && r < CArrayCount(reserved); ++r) if (StrIsEqual(words[at], reserved[r])) more = true;
            at += more ? 1 : 0;
        }
        // Wrappers, and the variables a shell or env sets for the command, as in env NAME=value app.
        b32 assigned = false;
        for (; at < n; ++at) {
            b32 wrapper = P__IsWrapperWord(words[at]);
            if (wrapper && at + 1 < n && words[at + 1].length && words[at + 1].value[0] == '-') return false;
            if (!wrapper && (!StrIndexChar(words[at], '=', SSF_None).exists || words[at].value[0] == '-')) break;
            assigned |= !wrapper;
        }
        if (at >= n) continue;
        String command = words[at];
        // A value may name the program only when it is given nothing, as the program could be a shell its arguments are code to.
        if (P__IsLauncherWord(command) || P__IsForeignShellWord(command, false) || (at + 1 < n && StrIndexStr(command, P__standIn, SSF_None).exists)) return false;
        // These are programs of their own too on some systems, scripts that hand their arguments to bash's.
        for (u32 e = 0; e < CArrayCount(evaluating); ++e) if (StrIsEqual(command, evaluating[e])) return false;
        if (StrIsEqual(command, S("printf")) && at + 1 < n && words[at + 1].length >= 2 && StrIsEqual(StrFromTo(words[at + 1], 0, 2), S("-v"))) return false;
        if (P__IsPosixShellWord(command)) {
            u64 optionsEnd = 0;
            b32 given = P__ShellOptions(words + at + 1, n - at - 1, &optionsEnd);
            u64 operand = at + 1 + optionsEnd;
            String leaf = StrFrom(command, StrIndexChar(command, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
            local_persist const String plainShells[] = { SInit("sh"), SInit("bash"), SInit("dash"), SInit("ash"), SInit("posh") };
            b32 known = false;
            for (u32 k = 0; k < CArrayCount(plainShells); ++k) known |= StrIsEqual(leaf, plainShells[k]);
            // A variable set for the shell, as ENV or BASH_ENV, could have it read a file as it starts.
            if (!given || depth || operators || assigned || operand >= n || !known) return false;
            for (u64 k = at + 1; k < operand; ++k) {
                String option = words[k];
                if (StrIndexStr(option, P__standIn, SSF_None).exists) return false;
                if (option.length < 2 || option.value[0] != '-') continue;
                b32 plainLong = StrIsEqual(option, S("--")) || StrIsEqual(option, S("--norc")) || StrIsEqual(option, S("--noprofile")) || StrIsEqual(option, S("--posix"));
                if (option.value[1] == '-' ? !plainLong : StrIndexChar(option, 'i', SSF_None).exists || StrIndexChar(option, 'l', SSF_None).exists) return false;
            }
            if (!P__CommandAllowed(arena, words[operand], depth + 1, true)) return false;
            // The words after the script are its name and arguments, values the script cannot use, as it has no $.
            continue;
        }
        if (P__IsTerminalWord(command)) {
            for (u64 k = at + 1; k < n; ++k) {
                String option = words[k];
                if (option.length > 2 && option.value[0] == '-' && option.value[1] == 'e' &&
                    (StrIndexChar(option, ' ', SSF_None).exists || StrIndexChar(option, '\t', SSF_None).exists)) return false;
                if (option.length > 10 && StrIsEqual(StrFromTo(option, 0, 10), S("--command="))) return false;
                if (StrIsEqual(option, S("-x")) || StrIsEqual(option, S("--execute"))) break;
                if (!StrIsEqual(option, S("-e")) && !StrIsEqual(option, S("--command"))) continue;
                // -e takes the words after it as the command; given one, the terminal parses it again or runs it in a shell.
                if (k + 2 >= n || StrIndexChar(words[k + 1], ' ', SSF_None).exists || StrIndexChar(words[k + 1], '\t', SSF_None).exists) return false;
                break;
            }
        }
        // No shell named among a command's arguments, which it could run, and no program that runs its arguments as a command, as after
        // timeout or a terminal's -e. A shell's name that is also a common word, as rc, counts when an option or nothing follows it, as it
        // would read its commands from its input then; script, a common word too, counts when any option comes after it, as it takes its
        // options anywhere and in short forms; and . never does.
        b32 optionLater = false;
        for (u64 k = n; k-- > at + 1;) {
            b32 optionNext = k + 1 < n && words[k + 1].length && words[k + 1].value[0] == '-';
            b32 launcher = P__IsLauncherWord(words[k]) && !StrIsEqual(words[k], S(".")) && (!StrIsEqual(words[k], S("script")) || optionLater);
            optionLater |= words[k].length > 1 && words[k].value[0] == '-';
            if (P__IsPosixShellWord(words[k]) || P__IsForeignShellWord(words[k], true) || (P__IsForeignShellWord(words[k], false) && (optionNext || k + 1 >= n)) || launcher) return false;
        }
    }
    return true;
}

// The quotes of a shell's command as that shell reads them: `quote` is 0, '\'' or '"', and `escaped` whether the last character was a backslash that takes the next.
typedef struct P__ScriptQuotes { char quote; b8 escaped; } P__ScriptQuotes;

function void P__ScriptStep(P__ScriptQuotes *q, char c)
{
    if (q->escaped) q->escaped = false;
    else if (c == '\\' && q->quote != '\'') q->escaped = true;
    else if ((c == '\'' || c == '"') && (!q->quote || q->quote == c)) q->quote = q->quote ? 0 : c;
}

// `value` quoted for where it stands in a shell's command, as `q` tells; a backslash before it is made one of its own, so that it cannot take the quote that opens the value.
function String P__ScriptValue(Arena *arena, P__ScriptQuotes *q, String value)
{
    String result = q->quote ? P__ShellQuoteInside(arena, value, q->quote) : P__ShellQuote(arena, value);
    if (q->escaped) result = StrFormat(arena, "\\%S", result);
    q->escaped = false;
    return result;
}

// Splits a desktop entry's Exec line into arguments, replacing the field codes with `paths`: %f and %u take one, %F and %U all of them.
// Returns how many paths the command took, so that a %f or %u command is run once for each of several files.
// In a shell's command, as "sh -c \"foo %f\"", which the spec forbids but some write, a value goes in quoted for that shell, as GLib does.
function u64 P__SplitExec(Arena *arena, const P__App *app, const String *paths, u64 count, ArrayStr *args, b32 *valued, String name)
{
    *args = (ArrayStr){0};
    u64 used = 0;
    *valued = false;
    String exec = app->exec;
    for (u64 at = 0; at < exec.length;) {
        while (at < exec.length && exec.value[at] == ' ') ++at;
        if (at >= exec.length) break;
        StrBuilder argument = {0};
        b32 quoted = false, codeOnly = true, any = false, script = P__ShellScriptNext(args->v, args->header.count);
        // The quotes of the argument, as the shell whose command it may be reads them.
        P__ScriptQuotes inner = {0};
        for (; at < exec.length && (quoted || exec.value[at] != ' '); ++at) {
            char c = exec.value[at];
            if (c == '"') { quoted = !quoted; codeOnly = false; continue; }
            if (quoted && c == '\\' && at + 1 < exec.length) {
                c = exec.value[++at];
                StrBuildChar(arena, &argument, c);
                P__ScriptStep(&inner, c);
                codeOnly = false, any = true;
                continue;
            }
            if (c == '%' && at + 1 < exec.length) {
                char code = exec.value[++at];
                b32 alone = codeOnly && (at + 1 >= exec.length || exec.value[at + 1] == ' ');
                if (code == '%') { StrBuildChar(arena, &argument, '%'); any = true; }
                else if ((code == 'F' || code == 'U') && alone && !script) {
                    for (u64 i = 0; i < count; ++i) ArrayPush(arena, *args, code == 'U' ? P__FileUri(arena, paths[i]) : paths[i]);
                    used = count;
                    *valued = *valued || count;
                } else if ((code == 'f' || code == 'u' || code == 'F' || code == 'U') && count) {
                    String value = code == 'u' || code == 'U' ? P__FileUri(arena, paths[0]) : paths[0];
                    StrBuildStr(arena, &argument, script ? P__ScriptValue(arena, &inner, value) : value);
                    used = Max(used, (u64)1);
                    any = *valued = true;
                } else if (code == 'c') {
                    StrBuildStr(arena, &argument, script ? P__ScriptValue(arena, &inner, name) : name);
                    any = true;
                    // The app's name is checked as a value is.
                    *valued = true;
                }
                // %i, %k and the deprecated codes expand to nothing.
                continue;
            }
            StrBuildChar(arena, &argument, c);
            P__ScriptStep(&inner, c);
            codeOnly = false;
            any = true;
        }
        if (any) ArrayPush(arena, *args, argument.buffer);
    }
    return used;
}

// No arguments come back when a value goes in where it could reach a shell's second parse: the arguments, as the entry splits them with a
// stand-in for each value, are checked as a command line of their own, each quoted whole (P__CommandAllowed).
function u64 P__ExpandExec(Arena *arena, const P__App *app, const String *paths, u64 count, ArrayStr *args)
{
    b32 valued = false, ignored = false;
    u64 used = P__SplitExec(arena, app, paths, count, args, &valued, app->name);
    if (!valued) return used;
    // The app's name, which %c gives as a plain argument, is a stand-in too.
    String *stand = ArenaPushArray(arena, count, String);
    for (u64 i = 0; i < count; ++i) stand[i] = P__standIn;
    ArrayStr template = {0};
    P__SplitExec(arena, app, stand, count, &template, &ignored, P__standIn);
    StrBuilder line = {0};
    for (u64 i = 0; i < template.header.count; ++i) StrBuildF(arena, &line, "%s%S", i ? " " : "", P__ShellQuote(arena, template.v[i]));
    if (!P__CommandAllowed(arena, line.buffer, 0, false)) {
        *args = (ArrayStr){0};
        return 0;
    }
    return used;
}

// Terminals that run a command given after their own arguments, most specific first.
// xdg-terminal-exec picks the user's preferred terminal itself.
global const struct { const char *name; const char *run[2]; } P__terminals[] = {
    { "xdg-terminal-exec", { NULL } }, { "kitty", { NULL } }, { "foot", { NULL } }, { "alacritty", { "-e" } }, { "ghostty", { "-e" } },
    { "wezterm", { "start", "--" } }, { "konsole", { "-e" } }, { "gnome-terminal", { "--" } }, { "xfce4-terminal", { "-x" } }, { "xterm", { "-e" } },
};

// The user's terminal: $TERMINAL when it names one, else the first known one on PATH.
// Fills `run` with the arguments that come before a command to run in it; unknown terminals get the common "-e".
function char *P__FindTerminal(Arena *arena, const char **run)
{
    u32 error = 0;
    const char *chosen = getenv("TERMINAL");
    run[0] = run[1] = NULL;
    char *executable = chosen && chosen[0] ? P__FindExecutable(arena, StrFromCStr(chosen), &error) : NULL;
    if (executable) {
        String name = StrFromCStr(chosen);
        Index slash = StrIndexChar(name, '/', SSF_SearchBackwards);
        if (slash.exists) name = StrFrom(name, slash.value + 1);
        run[0] = "-e";
        for (u32 i = 0; i < CArrayCount(P__terminals); ++i) {
            if (StrIsEqual(name, StrFromCStr(P__terminals[i].name))) {
                run[0] = P__terminals[i].run[0];
                run[1] = P__terminals[i].run[1];
            }
        }
        return executable;
    }
    for (u32 i = 0; i < CArrayCount(P__terminals); ++i) {
        char *executable = P__FindExecutable(arena, StrFromCStr(P__terminals[i].name), &error);
        if (executable) {
            run[0] = P__terminals[i].run[0];
            run[1] = P__terminals[i].run[1];
            return executable;
        }
    }
    return NULL;
}

// The family a terminal's settings name, by the line that names it: kitty's "font_family X" or "font_family family=\"X\"", foot's
// "font=X:size=11", alacritty's "family = \"X\"" and ghostty's "font-family = X". The terminal in use is asked first, then the others.
function String P_TerminalFont(Arena *arena)
{
    String result = {0};
    Scratch scratch = GetScratchEx(arena);
    const char *home = getenv("HOME"), *config = getenv("XDG_CONFIG_HOME");
    String folder = config && config[0] == '/' ? StrFromCStr(config) : home && home[0] == '/' ? StrFormat(scratch.arena, "%s/.config", home) : (String){0};
    local_persist const struct { const char *name, *file, *key; } settings[] = {
        { "kitty", "kitty/kitty.conf", "font_family" }, { "foot", "foot/foot.ini", "font" },
        { "alacritty", "alacritty/alacritty.toml", "family" }, { "ghostty", "ghostty/config", "font-family" },
    };
    const char *run[2];
    char *terminal = folder.length ? P__FindTerminal(scratch.arena, run) : NULL;
    String used = terminal ? StrFromCStr(terminal) : (String){0};
    Index slash = StrIndexChar(used, '/', SSF_SearchBackwards);
    if (slash.exists) used = StrFrom(used, slash.value + 1);
    for (u32 pass = 0; pass < 2 && !result.length; ++pass) {
        for (u32 i = 0; i < CArrayCount(settings) && !result.length && folder.length; ++i) {
            if ((pass == 0) != StrIsEqual(used, StrFromCStr(settings[i].name))) continue;
            String text = {0};
            if (!P_ReadFile(scratch.arena, StrFormat(scratch.arena, "%S/%s", folder, settings[i].file), &text)) continue;
            ArrayStr lines = StrSplit(scratch.arena, text, '\n');
            String key = StrFromCStr(settings[i].key);
            for (u64 l = 0; l < lines.header.count && !result.length; ++l) {
                String line = P__Trim(lines.v[l]);
                if (line.length <= key.length || !StrIsEqual(StrFromTo(line, 0, (i64)key.length), key)) continue;
                String value = StrFrom(line, key.length);
                if (value.value[0] != ' ' && value.value[0] != '\t' && value.value[0] != '=') continue;
                value = P__Trim(value);
                if (value.length && value.value[0] == '=') value = P__Trim(StrFrom(value, 1));
                Index named = StrIndexStr(value, S("family="), SSF_IndexPlusOne);
                if (named.exists) value = StrFrom(value, named.value);
                if (value.length && (value.value[0] == '"' || value.value[0] == '\'')) {
                    value = StrFrom(value, 1);
                    value = StrFromTo(value, 0, (i64)StrIndexChar(value, '"', SSF_CountOnFail).value);
                    value = StrFromTo(value, 0, (i64)StrIndexChar(value, '\'', SSF_CountOnFail).value);
                }
                // foot's size and other attributes follow a colon, and a fallback font a comma.
                value = StrFromTo(value, 0, (i64)StrIndexChar(value, ':', SSF_CountOnFail).value);
                value = P__Trim(StrFromTo(value, 0, (i64)StrIndexChar(value, ',', SSF_CountOnFail).value));
                // "auto" and "monospace" leave it to the system's matching.
                if (value.length && !StrIsEqual(value, S("auto")) && !StrIsEqual(value, S("monospace"))) result = StrCopy(arena, value);
            }
        }
    }
    ReleaseScratch(scratch);
    return result;
}

// Starts `app` on `paths`, once per path when its command takes one at a time.
function P_FileError P__LaunchApp(Arena *arena, const P__App *app, const String *paths, u64 count, String directory)
{
    Scratch scratch = GetScratchEx(arena);
    u32 error = 0;
    b32 refused = false;
    char *folder = P__FilePathCString(scratch.arena, directory);
    for (u64 done = 0; !error && (done < count || (!count && !done));) {
        ArrayStr args = {0};
        u64 used = P__ExpandExec(scratch.arena, app, paths + done, count - done, &args);
        // A command that is there but gave nothing was refused for how it hands the file to a shell, which is said rather than a format error.
        if (!args.header.count) { error = ENOEXEC; refused = P__Trim(app->exec).length != 0; break; }
        ArrayStr command = {0};
        const char *run[2] = {0};
        char *terminal = app->terminal ? P__FindTerminal(scratch.arena, run) : NULL;
        if (app->terminal && !terminal) { error = ENOENT; break; }
        if (terminal) {
            ArrayPush(scratch.arena, command, StrFromCStr(terminal));
            for (u32 i = 0; i < 2 && run[i]; ++i) ArrayPush(scratch.arena, command, StrFromCStr(run[i]));
        }
        for (u64 i = 0; i < args.header.count; ++i) ArrayPush(scratch.arena, command, args.v[i]);
        char *executable = terminal ? terminal : P__FindExecutable(scratch.arena, command.v[0], &error);
        if (!executable) break;
        char **argv = ArenaPushArray(scratch.arena, command.header.count + 1, char *);
        for (u64 i = 0; i < command.header.count; ++i) argv[i] = P__FilePathCString(scratch.arena, command.v[i]);
        error = P__Spawn(executable, argv, folder);
        done += used ? used : Max(count - done, (u64)1);
    }
    ReleaseScratch(scratch);
    if (refused) return (P_FileError){ .code = error, .message = P_Message("The app's command could run a file's name as a command, so it is not run.") };
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P__OpenTerminal(Arena *arena, String directory)
{
    Scratch scratch = GetScratchEx(arena);
    const char *run[2] = {0};
    char *terminal = P__FindTerminal(scratch.arena, run);
    u32 error = ENOENT;
    if (terminal) {
        char *argv[] = { terminal, NULL };
        error = P__Spawn(terminal, argv, P__FilePathCString(scratch.arena, directory));
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

// The application mimeapps.list chooses for `mime`, or else for the nearest type it is a kind of, as text/x-patch is of text/plain.
// xdg-open in the generic mode of desktops such as Hyprland looks at the type itself only, so it finds no editor for a patch.
function P__App *P__ChosenApp(const P__AppData *data, String mime)
{
    pthread_once(&P__iconsOnce, P__LoadIconData);
    String types[16] = { mime };
    u32 count = 1;
    P__App *app = NULL;
    for (u32 i = 0; i < count && !app; ++i) {
        P__AppsFor(data, types[i], true, &app, 1);
        String parents = MapsGet(P__Icons()->mimeParents, String, types[i]);
        for (u64 from = 0, to = 0; to <= parents.length; ++to) {
            if (to < parents.length && parents.value[to] != ';') continue;
            String parent = StrFromTo(parents, from, (i64)to);
            from = to + 1;
            b32 seen = !parent.length;
            for (u32 j = 0; j < count && !seen; ++j) seen = StrIsEqual(types[j], parent);
            if (!seen && count < CArrayCount(types)) types[count++] = parent;
        }
    }
    // Every text type is also text/plain.
    if (!app && StrIsEqual(StrFromTo(mime, 0, 5), S("text/"))) P__AppsFor(data, S("text/plain"), true, &app, 1);
    return app;
}

// An executable ELF file, which runs rather than opens; xdg-open has no handler for programs.
// Executable scripts still open in their editor, and so do the files of mounts that mark every file executable.
function b32 P__IsProgram(const char *file, const struct stat *metadata)
{
    if (!S_ISREG(metadata->st_mode) || access(file, X_OK) != 0) return false;
    u8 magic[4] = {0};
    i32 descriptor = open(file, O_RDONLY | O_CLOEXEC);
    b32 result = descriptor >= 0 && read(descriptor, magic, sizeof(magic)) == sizeof(magic) && MemCompare(magic, "\x7f" "ELF", sizeof(magic)) == 0;
    if (descriptor >= 0) close(descriptor);
    return result;
}

// Opens `path` as a double click does: a program runs, and a file starts the application mimeapps.list chooses for its type among `data`'s.
// Any other file goes to xdg-open, which can look inside a file whose name tells no type and has its own order for unchosen applications.
function P_FileError P__OpenPath(Arena *arena, const P__AppData *data, String path)
{
    Scratch scratch = GetScratchEx(arena);
    String absolute = P_AbsolutePath(scratch.arena, path);
    char *file = absolute.length ? P__FilePathCString(scratch.arena, absolute) : NULL;
    u32 error = file ? 0 : (u32)errno;
    P_FileError result = {0};
    if (file) {
        // Programs and applications start in the file's folder, as Windows starts them.
        Index slash = StrIndexChar(absolute, '/', SSF_SearchBackwards);
        String directory = slash.value ? StrFromTo(absolute, 0, (i64)slash.value) : S("/");
        struct stat metadata = {0};
        b32 found = stat(file, &metadata) == 0;
        String mime = P__MimeForName(absolute, found && S_ISDIR(metadata.st_mode));
        P__App *app = StrIsEqual(mime, S("application/octet-stream")) ? NULL : P__ChosenApp(data, mime);
        if (found && P__IsProgram(file, &metadata)) {
            char *args[] = {file, NULL};
            error = P__Spawn(file, args, P__FilePathCString(scratch.arena, directory));
        } else if (app) {
            result = P__LaunchApp(arena, app, &absolute, 1, directory);
        } else {
            char *executable = P__FindExecutable(scratch.arena, S("xdg-open"), &error);
            char *args[] = {"xdg-open", file, NULL};
            if (executable) error = P__Spawn(executable, args, NULL);
        }
    }
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : result;
}

// ponytail: `order` is unused until a Linux launcher protocol carries neighboring files.
function P_FileError P_OpenUrl(Arena *arena, String url)
{
    Scratch scratch = GetScratchEx(arena);
    u32 error = 0;
    char *opener = P__FindExecutable(scratch.arena, S("xdg-open"), &error);
    char *address = P__FilePathCString(scratch.arena, url);
    char *args[] = { opener, address, NULL };
    if (opener && address) error = P__Spawn(opener, args, NULL);
    ReleaseScratch(scratch);
    return error ? P__FileError(arena, error) : (P_FileError){0};
}

function P_FileError P_OpenPath(Arena *arena, String path, P_FileOrder order)
{
    (void)order;
    pthread_once(&P__appsOnce, P__LoadGlobalApps);
    return P__OpenPath(arena, &P__apps, path);
}

// Only a program mimeapps.list chooses counts, as any archive tool installed lists the types it can open.
function b32 P_OpensInApp(String path)
{
    pthread_once(&P__appsOnce, P__LoadGlobalApps);
    Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
    const P__App *app = P__ChosenApp(&P__apps, P__MimeForName(slash.exists ? StrFrom(path, slash.value + 1) : path, false));
    return app && !StrIsEqual(app->id, S("filey.desktop"));
}

function P_FileError P_OpenFileExplorer(Arena *arena, String path)
{
    pthread_once(&P__appsOnce, P__LoadGlobalApps);
    P__App *chosen = P__ChosenApp(&P__apps, S("inode/directory")), *apps[32];
    if (chosen && StrIsEqual(chosen->id, S("filey.desktop"))) chosen = NULL;
    u64 count = P__AppsFor(&P__apps, S("inode/directory"), false, apps, CArrayCount(apps));
    for (u64 i = 0; i < count && !chosen; ++i) if (!StrIsEqual(apps[i]->id, S("filey.desktop"))) chosen = apps[i];
    if (chosen) return P__LaunchApp(arena, chosen, &path, 1, path);
    return P__OpenPath(arena, &P__apps, path);
}

function P_FileError P_OpenConsole(Arena *arena, String directory)
{
    return P__OpenTerminal(arena, directory);
}

// Installing is Windows' alone; on Linux a package or `./Build.sh install` does it.
function b32 P_CanInstall(void) { return false; }
function P_Install P_InstallState(Arena *arena) { (void)arena; return (P_Install){0}; }
function P_FileError P_InstallApply(Arena *arena, b32 desktopShortcut, b32 onPath, b32 setDefault, b32 contextMenu, P_MenuLabels labels)
{
    (void)desktopShortcut, (void)onPath, (void)setDefault, (void)contextMenu, (void)labels;
    return P__FileError(arena, ENOTSUP);
}
function P_FileError P_Uninstall(Arena *arena) { return P__FileError(arena, ENOTSUP); }
function b32 P_LoadAppIcon(Arena *arena, i32 size, P_Image *image) { (void)arena; (void)size; (void)image; return false; }

// Desktops share no properties window.
function b32 P_ShowProperties(String directory, const String *names, u64 count)
{
    (void)directory, (void)names, (void)count;
    return false;
}

// The menu's items that start applications, by item index.
typedef struct P__MenuData {
    String directory;
    String *paths;     // the items the menu is for, or the directory for its background
    u64 count;
    P__App **apps;     // the application an Open With item starts
    b8 *terminal;      // Open in Terminal items
    String *templates; // the template file a New item copies, by item; empty for the rest
    String mime;       // the type the first item is of, which Set as Default chooses for
    b8 *setDefault;    // Set as Default items, whose application becomes the type's default
    String *commands;  // a Nemo action's shell command, by item; empty for the rest
    b8 *inTerminal;    // and whether it runs in a terminal
    u32 itemCount;     // the items these are by, which the program may show fewer of
    String *scripts;   // a Nemo script, by item, run on the items
    u32 rootItem;      // Open as Root's index, past the items when there is none
    String eject;      // where Eject takes the drive the menu is for from, as GIO names it; empty when it cannot be ejected
    u32 ejectItem;     // the Eject item's index, past the items when there is none
} P__MenuData;

// Where GIO ejects the drive whose folder is `path` from: a disk's mount under /run/media or /media, and a phone's or camera's GVFS
// address, as "mtp://SAMSUNG_SAMSUNG_Android_RFCX10M60QT/"; empty for a folder that is not such a drive's own.
function String P__EjectTarget(Arena *arena, String path)
{
    while (path.length > 1 && path.value[path.length - 1] == '/') --path.length;
    Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
    String name = slash.exists ? StrFrom(path, slash.value + 1) : path, parent = slash.exists ? StrFromTo(path, 0, (i64)slash.value) : (String){0};
    // In GVFS's folder, a phone or camera by its address, and a server connected to by its folder, which GIO knows it by too.
    if (StrIsEqual(StrFrom(parent, parent.length - 5), S("/gvfs"))) {
        Index host = StrIndexStr(name, S(":host="), SSF_None);
        b32 device = StrIsEqual(StrFromTo(name, 0, 4), S("mtp:")) || StrIsEqual(StrFromTo(name, 0, 8), S("gphoto2:"));
        if (device && host.exists) return StrFormat(arena, "%S://%S/", StrFromTo(name, 0, (i64)host.value), StrFrom(name, host.value + 6));
        return StrIndexChar(name, ':', SSF_None).exists ? StrCopy(arena, path) : (String){0};
    }
    b32 removable = StrIsEqual(StrFromTo(path, 0, 11), S("/run/media/")) || StrIsEqual(StrFromTo(path, 0, 7), S("/media/"));
    if (!removable) return (String){0};
    Scratch scratch = GetScratchEx(arena);
    String mounts = {0}, result = {0};
    P_ReadFile(scratch.arena, S("/proc/self/mounts"), &mounts);
    ArrayStr lines = StrSplit(scratch.arena, mounts, '\n');
    // Only one on removable hardware, as Windows offers it: on USB, or marked removable, never an internal disk mounted there too.
    for (u64 i = 0; i < lines.header.count && !result.length; ++i) {
        ArrayStr fields = StrSplit(scratch.arena, lines.v[i], ' ');
        if (fields.header.count < 2 || !StrIsEqual(P__MountPoint(scratch.arena, fields.v[1]), path)) continue;
        String device = fields.v[0];
        if (!StrIsEqual(StrFromTo(device, 0, 5), S("/dev/"))) continue;
        char *block = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "/sys/class/block/%S", StrFrom(device, 5)));
        char real[PATH_MAX];
        if (!block || !realpath(block, real)) continue;
        String hardware = StrFromCStr(real), disk = hardware, removable = {0};
        // The partition's disk holds the flag.
        Index up = StrIndexChar(hardware, '/', SSF_SearchBackwards);
        if (up.exists && P_ReadFile(scratch.arena, StrFormat(scratch.arena, "%S/partition", hardware), &removable)) disk = StrFromTo(hardware, 0, (i64)up.value);
        removable = (String){0};
        P_ReadFile(scratch.arena, StrFormat(scratch.arena, "%S/removable", disk), &removable);
        if (StrIndexStr(hardware, S("/usb"), SSF_None).exists || (removable.length && removable.value[0] == '1')) result = StrCopy(arena, path);
    }
    ReleaseScratch(scratch);
    return result;
}

// Ejects `target` with GIO on a thread of its own, as a disk may take a while to write out what it holds before it goes.
// ponytail: a failure, as of a drive still in use, only reaches stderr; report it as a notice through the program if it happens in practice.
function void *P__EjectThread(void *user)
{
    char *target = user;
    Scratch scratch = GetScratch();
    // A server's mount cannot be ejected, only unmounted.
    String arguments[] = { S("gio"), S("mount"), S("-e"), StrFromCStr(target) }, output = {0};
    b32 done = P_RunCommand(scratch.arena, S("/"), arguments, CArrayCount(arguments), 60000, &output, true);
    arguments[2] = S("-u");
    if (!done && !P_RunCommand(scratch.arena, S("/"), arguments, CArrayCount(arguments), 60000, &output, true)) {
        fprintf(stderr, "Filey: ejecting %s failed: %.*s\n", target, (i32)output.length, output.value);
    }
    ReleaseScratch(scratch);
    free(target);
    return NULL;
}

// Whether `name` (a program, or a path) is a program that runs.
function b32 P__HasProgram(Arena *arena, String name)
{
    u32 error = 0;
    return P__FindExecutable(arena, name, &error) != NULL;
}

// `text` in single quotes for the shell, its own single quotes escaped.
function String P__ShellQuote(Arena *arena, String text)
{
    StrBuilder out = {0};
    StrBuildChar(arena, &out, '\'');
    for (u64 i = 0; i < text.length; ++i) {
        if (text.value[i] == '\'') StrBuildStr(arena, &out, S("'\\''"));
        else StrBuildChar(arena, &out, text.value[i]);
    }
    StrBuildChar(arena, &out, '\'');
    return out.buffer;
}

// `text` for inside the quotes `quote` of a shell command line, which takes it as it is: in double quotes `\`, `"`, `$` and the backtick escaped, in single quotes each `'` closing them, escaped and opening them again.
function String P__ShellQuoteInside(Arena *arena, String text, char quote)
{
    StrBuilder out = {0};
    for (u64 i = 0; i < text.length; ++i) {
        char c = text.value[i];
        if (quote == '\'' && c == '\'') StrBuildStr(arena, &out, S("'\\''"));
        else {
            if (quote == '"' && (c == '\\' || c == '"' || c == '$' || c == '`')) StrBuildChar(arena, &out, '\\');
            StrBuildChar(arena, &out, c);
        }
    }
    return out.buffer;
}

// A Nemo action that applies to `paths` (`count` of them, or none for the folder's background) inside `directory`: its name and the shell
// command its Exec line makes, by the action file format Nemo documents. Actions with Conditions are left out, as those Nemo ships need
// Cinnamon's desktop or its D-Bus services, and so are those whose Dependencies are not met.
// ponytail: Files, Locations, UriScheme, Quote, EscapeSpaces and the %D and %X tokens are not read; Name[xx] follows only the language code.
typedef struct P__Action { String name; String command; b32 terminal; } P__Action;

function b32 P__ReadAction(Arena *arena, String file, String folder, String directory, const String *paths, u64 count, String language, P__Action *action)
{
    Scratch scratch = GetScratchEx(arena);
    P__StrMap ini = P__IniParse(scratch.arena, P__ReadFile(scratch.arena, file));
    String section = S("Nemo Action"), exec = P__IniGet(ini, section, S("Exec"));
    String name = language.length ? P__IniGet(ini, section, StrFormat(scratch.arena, "Name[%S]", language)) : (String){0};
    if (!name.length) name = P__IniGet(ini, section, S("Name"));
    b32 usable = exec.length && name.length && !StrIsEqual(P__Lower(scratch.arena, P__IniGet(ini, section, S("Active"))), S("false")) &&
             !P__IniGet(ini, section, S("Conditions")).length;
    // Which selections: one (s), several (m), any, none (the background), notnone, or an exact count.
    String selection = P__Lower(scratch.arena, P__IniGet(ini, section, S("Selection")));
    if (!selection.length) selection = S("s");
    if (StrIsEqual(selection, S("s"))) usable &= count == 1;
    else if (StrIsEqual(selection, S("m"))) usable &= count > 1;
    else if (StrIsEqual(selection, S("none"))) usable &= count == 0;
    else if (StrIsEqual(selection, S("notnone"))) usable &= count > 0;
    else if (!StrIsEqual(selection, S("any"))) usable &= count == (u64)P__ParseI32(selection, -1);
    // The programs it needs, and those it must not find, as "!gedit".
    ArrayStr needs = StrSplit(scratch.arena, P__IniGet(ini, section, S("Dependencies")), ';');
    for (u64 i = 0; i < needs.header.count && usable; ++i) {
        String need = P__Trim(needs.v[i]);
        if (!need.length) continue;
        b32 absent = need.value[0] == '!';
        usable = P__HasProgram(scratch.arena, absent ? StrFrom(need, 1) : need) != absent;
    }
    // Each selected item matches its extensions (any, dir, nodirs, none, or names such as "jpg") or its MIME types (as "image/*").
    ArrayStr extensions = StrSplit(scratch.arena, P__Lower(scratch.arena, P__IniGet(ini, section, S("Extensions"))), ';');
    ArrayStr types = StrSplit(scratch.arena, P__IniGet(ini, section, S("Mimetypes")), ';');
    for (u64 i = 0; i < count && usable && (extensions.header.count || types.header.count); ++i) {
        b32 folder = P__IsDirectory(scratch.arena, paths[i]), matches = false;
        String leaf = StrFrom(paths[i], StrIndexChar(paths[i], '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
        String extension = {0};
        Index dot = StrIndexChar(leaf, '.', SSF_SearchBackwards);
        if (dot.exists && dot.value) extension = P__Lower(scratch.arena, StrFrom(leaf, dot.value + 1));
        for (u64 e = 0; e < extensions.header.count && !matches; ++e) {
            String want = P__Trim(extensions.v[e]);
            matches = StrIsEqual(want, S("any")) || (StrIsEqual(want, S("dir")) && folder) || (StrIsEqual(want, S("nodirs")) && !folder) ||
                      (StrIsEqual(want, S("none")) && !folder && !extension.length) || (want.length && !folder && StrIsEqual(want, extension));
        }
        String mime = P__MimeForName(leaf, folder);
        for (u64 t = 0; t < types.header.count && !matches; ++t) {
            String want = P__Trim(types.v[t]);
            u64 star = want.length && want.value[want.length - 1] == '*' ? want.length - 1 : want.length;
            matches = want.length && (star < want.length ? mime.length >= star && StrIsEqual(StrFromTo(mime, 0, (i64)star), StrFromTo(want, 0, (i64)star)) : StrIsEqual(want, mime));
        }
        usable = matches;
    }
    if (!usable) {
        ReleaseScratch(scratch);
        return false;
    }
    // The tokens: the paths and the URIs of the selection, the folder, the first item's name and that name without its extension.
    String separator = P__IniGet(ini, section, S("Separator"));
    if (!separator.length) separator = S(" ");
    String first = count ? StrFrom(paths[0], StrIndexChar(paths[0], '/', SSF_SearchBackwards | SSF_IndexPlusOne).value) : (String){0};
    Index firstDot = StrIndexChar(first, '.', SSF_SearchBackwards);
    String stem = firstDot.exists && firstDot.value ? StrFromTo(first, 0, (i64)firstDot.value) : first;
    String parentName = StrFrom(directory, StrIndexChar(directory, '/', SSF_SearchBackwards | SSF_IndexPlusOne).value);
    // Whether a value went into the command, which then is not offered where it would reach a shell that the quoting cannot follow.
    b32 valued = false;
    // The name, then the command with stand-ins for the values, checked as the shell will read it, then the command itself.
    for (u32 pass = 0; pass < 3; ++pass) {
        b32 part = pass > 0, stand = pass == 1;
        if (stand && !StrIndexChar(exec, '%', SSF_None).exists) continue;
        String text = part ? exec : name;
        // A script beside the action, as "<convert.sh %F>", runs from the actions folder.
        if (part && text.length > 2 && text.value[0] == '<') {
            Index close = StrIndexChar(text, '>', SSF_None);
            String inside = close.exists ? StrFromTo(text, 1, (i64)close.value) : StrFrom(text, 1);
            // The folder quoted for the shell, its % kept from being taken for field codes.
            StrBuilder escaped = {0};
            String quotedFolder = P__ShellQuote(scratch.arena, folder);
            for (u64 i = 0; i < quotedFolder.length; ++i) {
                if (quotedFolder.value[i] == '%') StrBuildChar(scratch.arena, &escaped, '%');
                StrBuildChar(scratch.arena, &escaped, quotedFolder.value[i]);
            }
            text = StrFormat(scratch.arena, "%S/%S", escaped.buffer, inside);
        }
        StrBuilder out = {0};
        // The quotes the command line has open, and its words so far, the one under way as the shell takes it; a line with anything that
        // expands, as a command substituted, takes no value (P__CommandAllowed), so none is followed here.
        char open = 0;
        ArrayStr words = {0};
        StrBuilder word = {0};
        b32 inWord = false;
        // Whether the word under way is a shell's command, as the quoted one in sh -c "echo %f", and its quotes as that shell reads them.
        b32 script = false;
        P__ScriptQuotes inner = {0};
        for (u64 at = 0; at <= text.length; ++at) {
            char c = at < text.length ? text.value[at] : ' ', code = at + 1 < text.length ? text.value[at + 1] : 0;
            if (part && ((!open && P__IsShellBreak(c)) || at == text.length)) {
                if (inWord) ArrayPush(scratch.arena, words, word.buffer);
                word = (StrBuilder){0}, inWord = false;
                if (at < text.length) StrBuildChar(scratch.arena, &out, c);
                continue;
            }
            if (at == text.length) break;
            if (part && !inWord) {
                inWord = true;
                script = P__ShellScriptNext(words.v, words.header.count);
                inner = (P__ScriptQuotes){0};
            }
            // A backslash before a field code is dropped and the code expanded.
            if (part && c == '\\' && open != '\'' && code == '%') continue;
            if (c != '%' || !code) {
                StrBuildChar(scratch.arena, &out, c);
                if (!part) continue;
                if (c == '\\' && open != '\'' && code) {
                    char next = text.value[++at];
                    StrBuildChar(scratch.arena, &out, next);
                    // In double quotes a backslash escapes only these, and before anything else stays in the word.
                    if (open == '"' && next != '$' && next != '`' && next != '"' && next != '\\' && next != '\n') {
                        StrBuildChar(scratch.arena, &word, '\\');
                        P__ScriptStep(&inner, '\\');
                    }
                    StrBuildChar(scratch.arena, &word, next);
                    P__ScriptStep(&inner, next);
                } else if ((c == '"' || c == '\'') && (!open || open == c)) {
                    open = open ? 0 : c;
                } else {
                    StrBuildChar(scratch.arena, &word, c);
                    P__ScriptStep(&inner, c);
                }
                continue;
            }
            ++at;
            if (code == '%') {
                StrBuildChar(scratch.arena, &out, '%');
                if (part) StrBuildChar(scratch.arena, &word, '%'), P__ScriptStep(&inner, '%');
                continue;
            }
            // The files' list in quotes or a shell's command stands for the first file alone.
            b32 listed = (code == 'F' || code == 'U') && part && !open && !script;
            String value = {0};
            switch (code) {
                case 'F': value = count ? paths[0] : (String){0}; break;
                case 'U': value = count ? P__FileUri(scratch.arena, paths[0]) : (String){0}; break;
                case 'P': value = directory; break;
                case 'f': case 'N': value = first; break;
                case 'p': value = parentName; break;
                case 'e': value = stem; break;
                default: continue;
            }
            if (stand) value = P__standIn;
            // The name takes the values as they are; the command each quoted for the shell, or escaped for the quotes it is in.
            if (!part) {
                StrBuildStr(scratch.arena, &out, value);
                continue;
            }
            valued = true;
            if (listed) {
                for (u64 i = 0; i < count; ++i) {
                    if (i) StrBuildStr(scratch.arena, &out, separator);
                    StrBuildStr(scratch.arena, &out, P__ShellQuote(scratch.arena, stand ? P__standIn : code == 'U' ? P__FileUri(scratch.arena, paths[i]) : paths[i]));
                }
                continue;
            }
            // In a shell's command, as in sh -c "echo %f", the value is quoted for that shell first.
            if (script) value = P__ScriptValue(scratch.arena, &inner, value);
            StrBuildStr(scratch.arena, &out, open ? P__ShellQuoteInside(scratch.arena, value, open) : P__ShellQuote(scratch.arena, value));
        }
        // The command is not offered when a value goes into a line whose quoting the code here does not follow whole (P__CommandAllowed).
        if (stand) {
            if (valued && !P__CommandAllowed(scratch.arena, out.buffer, 0, true)) {
                ReleaseScratch(scratch);
                return false;
            }
            continue;
        }
        if (part) action->command = StrCopy(arena, out.buffer);
        else action->name = StrCopy(arena, out.buffer);
    }
    action->terminal = StrIsEqual(P__Lower(scratch.arena, P__IniGet(ini, section, S("Terminal"))), S("true"));
    ReleaseScratch(scratch);
    return true;
}

// Every item is something Filey carries out itself: Open and Rename by their verbs, Open With and Open in Terminal through P_ContextMenuInvoke, the rest by their actions.
function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
    // The computer is an icon here, not an item with a menu of its own.
    if (count == 1 && StrIsEqual(names[0], P_ComputerIconPath())) return false;
    pthread_once(&P__appsOnce, P__LoadGlobalApps);
    enum { MaxApps = 16, MaxExtras = 16, MaxItems = 28 + 2 * MaxApps + 2 * MaxExtras };
    P_MenuItem items[MaxItems];
    P__App *itemApps[MaxItems] = {0};
    String templateFor[MaxItems] = {0};
    b8 itemDefault[MaxItems] = {0}, itemInTerminal[MaxItems] = {0};
    String itemCommand[MaxItems] = {0}, itemScript[MaxItems] = {0};
    b8 itemTerminal[MaxItems] = {0};
    u32 itemCount = 0;
    P_MenuItem separator = { .separator = true };
    P__MenuData *data = ArenaPushStruct(arena, P__MenuData);
    data->directory = StrCopy(arena, directory);
    data->count = count ? count : 1;
    data->paths = ArenaPushArray(arena, data->count, String);
    for (u64 i = 0; i < count; ++i) {
        data->paths[i] = !directory.length ? StrCopy(arena, names[i])
                       : StrFormat(arena, "%S%s%S", directory, directory.value[directory.length - 1] == '/' ? "" : "/", names[i]);
    }
    if (!count) data->paths[0] = data->directory;
    // Folders open in Filey itself, so Open With and the terminal are offered for one folder or for files.
    // The trash is a place to look in, as Windows' Recycle Bin is, rather than a folder to copy or delete; the program adds emptying it.
    Scratch scratch = GetScratchEx(arena);
    b32 trash = count == 1 && P_IsTrashFolder(data->paths[0]);
    // What is in it, named by its place under the trash's files folder, is only deleted for good; the program adds putting it back.
    Index slash = StrIndexChar(directory, '/', SSF_SearchBackwards);
    b32 inTrash = P_IsTrashFolder(directory) ||
                  (slash.exists && StrIsEqual(StrFrom(directory, slash.value + 1), S("files")) && P_IsTrashFolder(StrFromTo(directory, 0, (i64)slash.value)));
    b32 oneFolder = count == 1 && P__IsDirectory(scratch.arena, data->paths[0]);
    data->eject = count == 1 ? P__EjectTarget(arena, data->paths[0]) : (String){0};
    P__App *apps[MaxApps];
    u64 appCount = count && !trash && !inTrash ? P__AppsFor(&P__apps, P__MimeForName(names[0], oneFolder), false, apps, MaxApps) : 0;
    const char *run[2] = {0};
    b32 terminal = !trash && !inTrash && (!count || oneFolder) && P__FindTerminal(scratch.arena, run) != NULL;
    ReleaseScratch(scratch);
    u32 openWith = UINT32_MAX, newItem = UINT32_MAX;
    if (inTrash) {
        if (count) items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Delete"), .action = P_MenuAction_Delete };
    } else if (count) {
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Open"), .verb = S("open"), .isDefault = true };
        if (appCount) {
            openWith = itemCount;
            items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Open With") };
        }
    }
    if (terminal) {
        itemTerminal[itemCount] = true;
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Open in Terminal"), .verb = S("openterminal") };
    }
    // A folder, or the one shown, in a window of Filey's own run as root, as Nemo offers, where pkexec is there to ask for the password.
    data->rootItem = UINT32_MAX;
    if (!trash && !inTrash && (!count || oneFolder) && geteuid() != 0) {
        Scratch probe = GetScratchEx(arena);
        u32 error = 0;
        if (P__FindExecutable(probe.arena, S("pkexec"), &error)) {
            data->rootItem = itemCount;
            items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Open as Root"), .verb = S("openroot") };
        }
        ReleaseScratch(probe);
    }
    if (count && !trash && !inTrash) {
        items[itemCount++] = separator;
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Cut"), .verb = S("cut"), .action = P_MenuAction_Cut };
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Copy"), .verb = S("copy"), .action = P_MenuAction_Copy };
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Duplicate"), .action = P_MenuAction_Duplicate };
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Copy As Path"), .action = P_MenuAction_CopyAsPath };
        items[itemCount++] = separator;
        if (count == 1) items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Rename"), .verb = S("rename") };
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Trash"), .verb = S("delete"), .action = P_MenuAction_Trash };
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Delete"), .action = P_MenuAction_Delete };
        // A removable drive's own folder, or a phone's, can be ejected, as Windows' menus offer.
        if (data->eject.length) {
            items[itemCount++] = separator;
            data->ejectItem = itemCount;
            b32 server = StrIsEqual(data->eject, data->paths[0]) && StrIndexStr(data->eject, S("/gvfs/"), SSF_None).exists;
            items[itemCount++] = (P_MenuItem){ .own = true, .label = server ? S("Disconnect") : S("Eject"), .verb = S("eject") };
        }
    } else if (!count && !inTrash) {
        items[itemCount++] = separator;
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Paste"), .verb = S("paste"), .action = P_MenuAction_Paste };
        items[itemCount++] = separator;
        newItem = itemCount;
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("New"), .verb = S("new") };
    }
    // The user's Nemo actions and scripts, after the rest, as Nemo shows them: the actions folders' in the order of their file names, the
    // user's own over the system's of the same name, then the scripts folder's programs in a submenu of their own.
    u32 scriptsItem = UINT32_MAX;
    String scriptFiles[MaxExtras];
    u32 scriptCount = 0;
    if (!trash && !inTrash && directory.length) {
        Scratch extras = GetScratchEx(arena);
        const P__TypeWords *words = atomic_load(&P__typeWords);
        String language = words ? words->language : (String){0};
        ArrayStr dataDirs = P__DataDirs(extras.arena);
        String seen[64];
        u32 seenCount = 0, actions = 0;
        b32 separated = false;
        for (u64 d = 0; d < dataDirs.header.count && actions < MaxExtras; ++d) {
            String folder = StrFormat(extras.arena, "%S/nemo/actions", dataDirs.v[d]);
            P_DirectoryResult listing = P_EnumerateDirectory(extras.arena, folder, NULL);
            String files[64];
            u32 fileCount = 0;
            for (u64 i = 0; i < listing.files.header.count && fileCount < CArrayCount(files); ++i) {
                String name = listing.files.v[i].name, suffix = S(".nemo_action");
                if (name.length <= suffix.length || !StrIsEqual(StrFrom(name, name.length - suffix.length), suffix)) continue;
                b32 taken = false;
                for (u32 k = 0; k < seenCount && !taken; ++k) taken = StrIsEqual(seen[k], name);
                if (taken) continue;
                if (seenCount < CArrayCount(seen)) seen[seenCount++] = name;
                u32 at = fileCount++;
                while (at > 0 && P__CompareFolded(files[at - 1], name) > 0) files[at] = files[at - 1], --at;
                files[at] = name;
            }
            for (u32 i = 0; i < fileCount && actions < MaxExtras; ++i) {
                P__Action action = {0};
                if (!P__ReadAction(arena, StrFormat(extras.arena, "%S/%S", folder, files[i]), folder, data->directory, data->paths, count, language, &action)) continue;
                if (!separated) items[itemCount++] = separator, separated = true;
                itemCommand[itemCount] = action.command;
                itemInTerminal[itemCount] = (b8)action.terminal;
                items[itemCount++] = (P_MenuItem){ .label = action.name, .verb = S("action") };
                ++actions;
            }
        }
        // Scripts run on what is selected, so the background has none to run.
        String scripts = StrFormat(extras.arena, "%S/nemo/scripts", dataDirs.header.count ? dataDirs.v[0] : S(""));
        P_DirectoryResult listing = count ? P_EnumerateDirectory(extras.arena, scripts, NULL) : (P_DirectoryResult){0};
        for (u64 i = 0; i < listing.files.header.count && scriptCount < MaxExtras; ++i) {
            const P_File *file = &listing.files.v[i];
            String path = StrFormat(extras.arena, "%S/%S", scripts, file->name);
            if (file->kind != P_FileKind_File || !P__HasProgram(extras.arena, path)) continue;
            u32 at = scriptCount++;
            while (at > 0 && P__CompareFolded(scriptFiles[at - 1], path) > 0) scriptFiles[at] = scriptFiles[at - 1], --at;
            scriptFiles[at] = StrCopy(arena, path);
        }
        if (scriptCount) {
            if (!separated) items[itemCount++] = separator;
            scriptsItem = itemCount;
            items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Scripts") };
        }
        ReleaseScratch(extras);
    }
    u32 rootCount = itemCount;
    if (scriptsItem != UINT32_MAX) {
        items[scriptsItem].firstChild = itemCount;
        items[scriptsItem].childCount = scriptCount;
        for (u32 i = 0; i < scriptCount; ++i) {
            itemScript[itemCount] = scriptFiles[i];
            items[itemCount++] = (P_MenuItem){ .label = StrFrom(scriptFiles[i], StrIndexChar(scriptFiles[i], '/', SSF_SearchBackwards | SSF_IndexPlusOne).value), .verb = S("script") };
        }
    }
    if (newItem != UINT32_MAX) {
        items[newItem].firstChild = itemCount;
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Folder"), .action = P_MenuAction_NewFolder };
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("File"), .action = P_MenuAction_NewFile };
        // Then a copy of each file in the templates folder, by its name without the extension, as Nemo offers them.
        Scratch scratch = GetScratchEx(arena);
        String folder = P__TemplatesDir(scratch.arena);
        P_DirectoryResult listing = folder.length ? P_EnumerateDirectory(scratch.arena, folder, NULL) : (P_DirectoryResult){0};
        u32 first = itemCount;
        for (u64 i = 0; i < listing.files.header.count && itemCount < MaxItems - 1; ++i) {
            const P_File *file = &listing.files.v[i];
            if (file->kind != P_FileKind_File || file->hidden || (file->name.length && file->name.value[0] == '.')) continue;
            if (itemCount == first) items[itemCount++] = separator;
            Index dot = StrIndexChar(file->name, '.', SSF_SearchBackwards);
            String label = dot.exists && dot.value ? StrFromTo(file->name, 0, (i64)dot.value) : file->name;
            templateFor[itemCount] = StrFormat(arena, "%S/%S", folder, file->name);
            items[itemCount++] = (P_MenuItem){ .label = StrCopy(arena, label), .verb = S("template") };
        }
        // In the order of their names.
        for (u32 i = first + 2; i < itemCount; ++i) {
            for (u32 j = i; j > first + 1 && P__CompareFolded(items[j - 1].label, items[j].label) > 0; --j) {
                P_MenuItem item = items[j]; items[j] = items[j - 1]; items[j - 1] = item;
                String path = templateFor[j]; templateFor[j] = templateFor[j - 1]; templateFor[j - 1] = path;
            }
        }
        ReleaseScratch(scratch);
        items[newItem].childCount = itemCount - items[newItem].firstChild;
    }
    if (openWith != UINT32_MAX) {
        // The applications, then Set as Default with them again, which makes the one chosen open the type from then on.
        items[openWith].firstChild = itemCount;
        items[openWith].childCount = (u32)appCount + 2;
        // Each by its icon from the theme, or the file its entry names, as other file managers show them.
        P_Image *icons = ArenaPushArray(arena, appCount, P_Image);
        for (u64 i = 0; i < appCount; ++i) {
            String name = apps[i]->icon;
            if (!name.length) continue;
            Scratch scratch = GetScratchEx(arena);
            String path = name.value[0] == '/' ? name : P__FindIcon(scratch.arena, name, 32, 1);
            if (path.length) P__DecodeIcon(arena, path, 32, &icons[i]);
            ReleaseScratch(scratch);
        }
        for (u64 i = 0; i < appCount; ++i) {
            itemApps[itemCount] = apps[i];
            items[itemCount++] = (P_MenuItem){ .label = apps[i]->name, .verb = S("openwith"), .isDefault = i == 0, .icon = icons[i] };
        }
        items[itemCount++] = separator;
        u32 setDefault = itemCount;
        items[itemCount++] = (P_MenuItem){ .own = true, .label = S("Set as Default") };
        items[setDefault].firstChild = itemCount;
        items[setDefault].childCount = (u32)appCount;
        for (u64 i = 0; i < appCount; ++i) {
            itemApps[itemCount] = apps[i];
            itemDefault[itemCount] = true;
            items[itemCount++] = (P_MenuItem){ .label = apps[i]->name, .verb = S("setdefault"), .isDefault = i == 0, .icon = icons[i] };
        }
    }
    if (!data->eject.length) data->ejectItem = itemCount;
    menu->items = ArenaPushArray(arena, itemCount, P_MenuItem);
    data->apps = ArenaPushArray(arena, itemCount, P__App *);
    data->terminal = ArenaPushArray(arena, itemCount, b8);
    data->templates = ArenaPushArray(arena, itemCount, String);
    data->setDefault = ArenaPushArray(arena, itemCount, b8);
    data->commands = ArenaPushArray(arena, itemCount, String);
    data->inTerminal = ArenaPushArray(arena, itemCount, b8);
    data->scripts = ArenaPushArray(arena, itemCount, String);
    data->itemCount = itemCount;
    if (count && appCount) {
        Scratch types = GetScratchEx(arena);
        data->mime = StrCopy(arena, P__MimeForName(names[0], P__IsDirectory(types.arena, data->paths[0])));
        ReleaseScratch(types);
    }
    for (u32 i = 0; i < itemCount; ++i) {
        menu->items[i] = items[i];
        if (!items[i].separator) menu->items[i].id = i + 1;
        data->apps[i] = itemApps[i];
        data->terminal[i] = itemTerminal[i];
        data->templates[i] = templateFor[i];
        data->setDefault[i] = itemDefault[i];
        data->commands[i] = itemCommand[i];
        data->inTerminal[i] = itemInTerminal[i];
        data->scripts[i] = itemScript[i];
    }
    menu->count = itemCount;
    menu->rootCount = rootCount;
    menu->native = data;
    return true;
}

// Linux's menus are built from the application database, which P_ContextMenuWarm loads, so there is nothing to build ahead.
function void P_ContextMenuPrepare(String directory, const String *names, u64 count)
{
    (void)directory;
    (void)names;
    (void)count;
}

// Loads the application database off the main thread, so that the first menu does not wait for it.
function void P_ContextMenuWarm(String directory, String name)
{
    (void)directory;
    (void)name;
    pthread_once(&P__appsOnce, P__LoadGlobalApps);
}

function P_FileError P_ContextMenuInvoke(Arena *arena, P_ContextMenu *menu, u32 id)
{
    P__MenuData *data = menu->native;
    u32 index = id - 1;
    if (!data || !id || index >= data->itemCount) return P__FileError(arena, EINVAL);
    if (data->terminal[index]) return P__OpenTerminal(arena, data->paths[0]);
    // pkexec gives root a clean environment, so the window's own session comes with it: Wayland's socket, the runtime folder it is in, and X's.
    if (index == data->rootItem) {
        Scratch scratch = GetScratchEx(arena);
        u32 error = 0;
        char *pkexec = P__FindExecutable(scratch.arena, S("pkexec"), &error);
        String self = P_ExecutablePath(scratch.arena);
        const char *names[] = { "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "DISPLAY", "XAUTHORITY" };
        char *args[16] = { pkexec, "env" };
        u32 argc = 2;
        for (u32 i = 0; i < CArrayCount(names); ++i) {
            const char *value = getenv(names[i]);
            if (value) args[argc++] = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "%s=%s", names[i], value));
        }
        args[argc++] = P__FilePathCString(scratch.arena, self);
        args[argc++] = "--new-window";
        args[argc++] = P__FilePathCString(scratch.arena, data->paths[0]);
        error = pkexec && self.length ? P__Spawn(pkexec, args, NULL) : ENOENT;
        ReleaseScratch(scratch);
        return error ? P__FileError(arena, error) : (P_FileError){0};
    }
    // A template is copied in under a free name, which the program then renames, as a new file.
    if (data->templates[index].length) {
        P_CancelToken token = {0};
        return P_TransferItems(arena, &data->templates[index], 1, data->directory, false, NULL, NULL, &token);
    }
    if (index == data->ejectItem) {
        char *target = strndup(data->eject.value, data->eject.length);
        pthread_t thread;
        if (target && pthread_create(&thread, NULL, P__EjectThread, target) == 0) pthread_detach(thread);
        else free(target);
        return (P_FileError){0};
    }
    // A Nemo action's command runs in the shell, in the folder, or in a terminal when it asks; a script gets the items' names as its
    // arguments and Nemo's variables for them, as Nemo runs it.
    if (data->commands[index].length || data->scripts[index].length) {
        Scratch scratch = GetScratchEx(arena);
        u32 error = 0;
        char *folder = P__FilePathCString(scratch.arena, data->directory);
        if (data->scripts[index].length) {
            StrBuilder selected = {0}, uris = {0};
            char **args = ArenaPushArray(scratch.arena, data->count + 2, char *);
            args[0] = P__FilePathCString(scratch.arena, data->scripts[index]);
            for (u64 i = 0; i < data->count; ++i) {
                StrBuildStr(scratch.arena, &selected, StrFormat(scratch.arena, "%S\n", data->paths[i]));
                StrBuildStr(scratch.arena, &uris, StrFormat(scratch.arena, "%S\n", P__FileUri(scratch.arena, data->paths[i])));
                args[i + 1] = P__FilePathCString(scratch.arena, StrFrom(data->paths[i], StrIndexChar(data->paths[i], '/', SSF_SearchBackwards | SSF_IndexPlusOne).value));
            }
            // The script's own environment is Filey's with Nemo's variables, rather than Filey's changed while other threads read it.
            u64 own = 0;
            while (environ[own]) ++own;
            char **variables = ArenaPushArray(scratch.arena, own + 4, char *);
            u64 kept = 0;
            variables[kept++] = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "NEMO_SCRIPT_SELECTED_FILE_PATHS=%S", selected.buffer));
            variables[kept++] = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "NEMO_SCRIPT_SELECTED_URIS=%S", uris.buffer));
            variables[kept++] = P__FilePathCString(scratch.arena, StrFormat(scratch.arena, "NEMO_SCRIPT_CURRENT_URI=%S", P__FileUri(scratch.arena, data->directory)));
            for (u64 i = 0; i < own; ++i) if (strncmp(environ[i], "NEMO_SCRIPT_", 12) != 0) variables[kept++] = environ[i];
            variables[kept] = NULL;
            error = P__SpawnWith(args[0], args, folder, false, variables);
        } else {
            const char *run[2] = {0};
            char *terminal = data->inTerminal[index] ? P__FindTerminal(scratch.arena, run) : NULL;
            char *shell = "/bin/sh", *command = P__FilePathCString(scratch.arena, data->commands[index]);
            char *args[8] = {0};
            u32 argc = 0;
            if (terminal) {
                args[argc++] = terminal;
                for (u32 i = 0; i < 2 && run[i]; ++i) args[argc++] = (char *)run[i];
            }
            args[argc++] = shell, args[argc++] = "-c", args[argc++] = command;
            error = P__Spawn(args[0], args, folder);
        }
        ReleaseScratch(scratch);
        return error ? P__FileError(arena, error) : (P_FileError){0};
    }
    if (data->setDefault[index] && data->apps[index]) return P__SetDefaultApp(arena, data->mime, data->apps[index]->id);
    if (data->apps[index]) return P__LaunchApp(arena, data->apps[index], data->paths, data->count, data->directory);
    return P__FileError(arena, ENOTSUP);
}

function void P_ContextMenuClose(P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
}
