// Applications from the freedesktop desktop entries, for Open With and Open in Terminal.
// Requires LinuxIcons.c for the INI parser, the data directories and the MIME table.

typedef struct P__App {
    String id;         // desktop file ID, "org.gnome.TextEditor.desktop"
    String name;
    String exec;
    String mimeTypes;  // ";"-separated, as in the file
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

// Applications for `mime`, best first: the default, the associations the user added, then every other entry that lists the type, by name,
// unless `chosenOnly` keeps just those that mimeapps.list chooses.
function u64 P__AppsFor(const P__AppData *data, String mime, b32 chosenOnly, P__App **out, u64 capacity)
{
    u64 count = 0;
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

// Splits a desktop entry's Exec line into arguments, replacing the field codes with `paths`: %f and %u take one, %F and %U all of them.
// Returns how many paths the command took, so that a %f or %u command is run once for each of several files.
function u64 P__ExpandExec(Arena *arena, const P__App *app, const String *paths, u64 count, ArrayStr *args)
{
    *args = (ArrayStr){0};
    u64 used = 0;
    String exec = app->exec;
    for (u64 at = 0; at < exec.length;) {
        while (at < exec.length && exec.value[at] == ' ') ++at;
        if (at >= exec.length) break;
        StrBuilder argument = {0};
        b32 quoted = false, codeOnly = true, any = false;
        for (; at < exec.length && (quoted || exec.value[at] != ' '); ++at) {
            char c = exec.value[at];
            if (c == '"') { quoted = !quoted; codeOnly = false; continue; }
            if (quoted && c == '\\' && at + 1 < exec.length) { StrBuildChar(arena, &argument, exec.value[++at]); codeOnly = false; any = true; continue; }
            if (c == '%' && at + 1 < exec.length) {
                char code = exec.value[++at];
                b32 alone = codeOnly && (at + 1 >= exec.length || exec.value[at + 1] == ' ');
                if (code == '%') { StrBuildChar(arena, &argument, '%'); any = true; }
                else if ((code == 'F' || code == 'U') && alone) {
                    for (u64 i = 0; i < count; ++i) ArrayPush(arena, *args, code == 'U' ? P__FileUri(arena, paths[i]) : paths[i]);
                    used = count;
                } else if ((code == 'f' || code == 'u' || code == 'F' || code == 'U') && count) {
                    StrBuildStr(arena, &argument, code == 'u' || code == 'U' ? P__FileUri(arena, paths[0]) : paths[0]);
                    used = Max(used, (u64)1);
                    any = true;
                } else if (code == 'c') { StrBuildStr(arena, &argument, app->name); any = true; }
                // %i, %k and the deprecated codes expand to nothing.
                continue;
            }
            StrBuildChar(arena, &argument, c);
            codeOnly = false;
            any = true;
        }
        if (any) ArrayPush(arena, *args, argument.buffer);
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

// Starts `app` on `paths`, once per path when its command takes one at a time.
function P_FileError P__LaunchApp(Arena *arena, const P__App *app, const String *paths, u64 count, String directory)
{
    Scratch scratch = GetScratchEx(arena);
    u32 error = 0;
    char *folder = P__FilePathCString(scratch.arena, directory);
    for (u64 done = 0; !error && (done < count || (!count && !done));) {
        ArrayStr args = {0};
        u64 used = P__ExpandExec(scratch.arena, app, paths + done, count - done, &args);
        if (!args.header.count) { error = ENOEXEC; break; }
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
} P__MenuData;

// Every item is something Filey carries out itself: Open and Rename by their verbs, Open With and Open in Terminal through P_ContextMenuInvoke, the rest by their actions.
function b32 P_ContextMenuOpen(Arena *arena, String directory, const String *names, u64 count, P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
    // The computer is an icon here, not an item with a menu of its own.
    if (count == 1 && StrIsEqual(names[0], P_ComputerIconPath())) return false;
    pthread_once(&P__appsOnce, P__LoadGlobalApps);
    enum { MaxApps = 16, MaxItems = 24 + MaxApps };
    P_MenuItem items[MaxItems];
    P__App *itemApps[MaxItems] = {0};
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
    P__App *apps[MaxApps];
    u64 appCount = count && !trash && !inTrash ? P__AppsFor(&P__apps, P__MimeForName(names[0], oneFolder), false, apps, MaxApps) : 0;
    const char *run[2] = {0};
    b32 terminal = !trash && !inTrash && (!count || oneFolder) && P__FindTerminal(scratch.arena, run) != NULL;
    ReleaseScratch(scratch);
    u32 openWith = UINT32_MAX;
    if (inTrash) {
        if (count) items[itemCount++] = (P_MenuItem){ .label = S("Delete"), .action = P_MenuAction_Delete };
    } else if (count) {
        items[itemCount++] = (P_MenuItem){ .label = S("Open"), .verb = S("open"), .isDefault = true };
        if (appCount) {
            openWith = itemCount;
            items[itemCount++] = (P_MenuItem){ .label = S("Open With") };
        }
    }
    if (terminal) {
        itemTerminal[itemCount] = true;
        items[itemCount++] = (P_MenuItem){ .label = S("Open in Terminal"), .verb = S("openterminal") };
    }
    if (count && !trash && !inTrash) {
        items[itemCount++] = separator;
        items[itemCount++] = (P_MenuItem){ .label = S("Cut"), .verb = S("cut"), .action = P_MenuAction_Cut };
        items[itemCount++] = (P_MenuItem){ .label = S("Copy"), .verb = S("copy"), .action = P_MenuAction_Copy };
        items[itemCount++] = (P_MenuItem){ .label = S("Duplicate"), .action = P_MenuAction_Duplicate };
        items[itemCount++] = (P_MenuItem){ .label = S("Copy As Path"), .action = P_MenuAction_CopyAsPath };
        items[itemCount++] = separator;
        if (count == 1) items[itemCount++] = (P_MenuItem){ .label = S("Rename"), .verb = S("rename") };
        items[itemCount++] = (P_MenuItem){ .label = S("Trash"), .verb = S("delete"), .action = P_MenuAction_Trash };
        items[itemCount++] = (P_MenuItem){ .label = S("Delete"), .action = P_MenuAction_Delete };
    } else if (!count && !inTrash) {
        items[itemCount++] = separator;
        items[itemCount++] = (P_MenuItem){ .label = S("Paste"), .verb = S("paste"), .action = P_MenuAction_Paste };
        items[itemCount++] = separator;
        items[itemCount++] = (P_MenuItem){ .label = S("New") };
    }
    u32 rootCount = itemCount;
    if (!count && !inTrash) {
        items[rootCount - 1].firstChild = rootCount;
        items[rootCount - 1].childCount = 2;
        items[itemCount++] = (P_MenuItem){ .label = S("Folder"), .action = P_MenuAction_NewFolder };
        items[itemCount++] = (P_MenuItem){ .label = S("File"), .action = P_MenuAction_NewFile };
    }
    if (openWith != UINT32_MAX) {
        items[openWith].firstChild = itemCount;
        items[openWith].childCount = (u32)appCount;
        for (u64 i = 0; i < appCount; ++i) {
            itemApps[itemCount] = apps[i];
            items[itemCount++] = (P_MenuItem){ .label = apps[i]->name, .verb = S("openwith"), .isDefault = i == 0 };
        }
    }
    menu->items = ArenaPushArray(arena, itemCount, P_MenuItem);
    data->apps = ArenaPushArray(arena, itemCount, P__App *);
    data->terminal = ArenaPushArray(arena, itemCount, b8);
    for (u32 i = 0; i < itemCount; ++i) {
        menu->items[i] = items[i];
        if (!items[i].separator) menu->items[i].id = i + 1;
        data->apps[i] = itemApps[i];
        data->terminal[i] = itemTerminal[i];
    }
    menu->count = itemCount;
    menu->rootCount = rootCount;
    menu->native = data;
    return true;
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
    if (!data || !id || index >= menu->count) return P__FileError(arena, EINVAL);
    if (data->terminal[index]) return P__OpenTerminal(arena, data->paths[0]);
    if (data->apps[index]) return P__LaunchApp(arena, data->apps[index], data->paths, data->count, data->directory);
    return P__FileError(arena, ENOTSUP);
}

function void P_ContextMenuClose(P_ContextMenu *menu)
{
    *menu = (P_ContextMenu){0};
}
