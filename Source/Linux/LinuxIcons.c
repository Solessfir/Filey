// File icons from the freedesktop icon theme: extension -> MIME type (shared-mime-info globs) -> icon name -> file in the configured theme or the themes it inherits.
// Type names come from the same MIME database.
// Requires LinuxFiles.c.

#include <pthread.h>

// Third-party decoders (Lib/): stb_image v2.30 (nothings/stb 2c980bb) for PNG and nanosvg (memononen/nanosvg 239e102) for SVG.
// Both allocate with libc internally; everything they return is copied into the caller's arena and freed right away.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#pragma push_macro("function")
#pragma push_macro("global")
#undef function
#undef global
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "Lib/stb/stb_image.h"
#define NANOSVG_IMPLEMENTATION
#include "Lib/nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "Lib/nanosvg/nanosvgrast.h"
#pragma pop_macro("global")
#pragma pop_macro("function")
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

typedef enum P__IconDirType {
    P__IconDirType_Threshold,
    P__IconDirType_Fixed,
    P__IconDirType_Scalable,
    P__IconDirType_Count,
} P__IconDirType;

typedef struct P__IconDir {
    String path;  // relative to the theme folder
    i32 size, minSize, maxSize, threshold;  // in logical pixels, of icons drawn at `scale`
    i32 scale;
    enum8(P__IconDirType) type;
} P__IconDir;

typedef struct P__IconTheme {
    ArrayStr folders;  // every <base>/<name> that exists
    P__IconDir *dirs;
    u64 dirCount;
} P__IconTheme;

typedef struct P__StrMap { MapHeader header; struct { String key; String value; } *v; } P__StrMap;

// Read-only once made, so jobs share it without locks; a change of the configured theme makes a whole new one.
typedef struct P__IconData {
    Arena arena;
    String theme;               // the configured theme's name it was made for
    ArrayStr dataDirs;
    P__StrMap mimeByExtension;  // "txt" -> "text/plain"
    P__StrMap genericIcons;     // "text/plain" -> "text-x-generic"
    P__StrMap mimeParents;      // "text/x-patch" -> "text/plain", several separated by ";"
    P__IconTheme *themes;       // lookup order: configured theme, what it inherits, hicolor
    u64 themeCount;
} P__IconData;

// ponytail: the data a theme change replaces is never freed, as a job may still read it; a few hundred kilobytes a change.
global P__IconData *_Atomic P__iconData;
global pthread_once_t P__iconsOnce = PTHREAD_ONCE_INIT;
global atomic_bool P__iconThemeChanged;

function String P__ReadFile(Arena *arena, String path)
{
    String result = {0};
    Scratch scratch = GetScratchEx(arena);
    char *cpath = P__FilePathCString(scratch.arena, path);
    FILE *file = cpath ? fopen(cpath, "rb") : NULL;
    ReleaseScratch(scratch);
    if (file == NULL) return result;
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        if (size >= 0 && fseek(file, 0, SEEK_SET) == 0) {
            // NUL-terminated for nanosvg, which parses in place.
            char *data = ArenaPushArrayNoZero(arena, (u64)size + 1, char);
            u64 read = fread(data, 1, (u64)size, file);
            data[read] = 0;
            result = StrMake(data, read);
        }
    }
    fclose(file);
    return result;
}

// ponytail: nanosvg ignores clip paths, masks and filters, so SVGs using them are skipped and the lookup falls back to other sizes and inherited themes (usually PNG).
// A fuller SVG renderer would remove the fallback.
function b32 P__IconUsable(Arena *scratch, String path)
{
    char *cpath = P__FilePathCString(scratch, path);
    if (!cpath || access(cpath, R_OK) != 0) return false;
    if (!StrIsEqual(StrFrom(path, path.length - 4), S(".svg"))) return true;
    String text = P__ReadFile(scratch, path);
    return !StrIndexStr(text, S("<clipPath"), SSF_None).exists && !StrIndexStr(text, S("<mask"), SSF_None).exists &&
           !StrIndexStr(text, S("<filter"), SSF_None).exists;
}

function String P__Trim(String s)
{
    u64 start = 0, end = s.length;
    while (start < end && (s.value[start] == ' ' || s.value[start] == '\t' || s.value[start] == '\r')) ++start;
    while (end > start && (s.value[end - 1] == ' ' || s.value[end - 1] == '\t' || s.value[end - 1] == '\r')) --end;
    return StrFromTo(s, start, (i64)end);
}

// INI-style file as "section\nkey" -> value; the first occurrence wins.
function P__StrMap P__IniParse(Arena *arena, String text)
{
    P__StrMap result = {0};
    String section = {0};
    for (u64 at = 0; at < text.length;) {
        Index newline = StrIndexChar(StrFrom(text, at), '\n', SSF_CountOnFail);
        String line = P__Trim(StrFromTo(text, at, (i64)(at + newline.value)));
        at += newline.value + 1;
        if (line.length >= 2 && line.value[0] == '[' && line.value[line.length - 1] == ']') {
            section = StrFromTo(line, 1, (i64)line.length - 1);
            continue;
        }
        Index equals = StrIndexChar(line, '=', SSF_None);
        if (!equals.exists || line.value[0] == '#') continue;
        String key = StrFormat(arena, "%S\n%S", section, P__Trim(StrFromTo(line, 0, (i64)equals.value)));
        if (!MapsIndex(result, key).exists) MapsPut(arena, result, key, P__Trim(StrFrom(line, equals.value + 1)));
    }
    return result;
}

function String P__IniGet(P__StrMap ini, String section, String key)
{
    char buffer[512];
    i32 length = snprintf(buffer, sizeof(buffer), "%.*s\n%.*s", (i32)section.length, section.value, (i32)key.length, key.value);
    if (length < 0 || length >= (i32)sizeof(buffer)) return (String){0};
    return MapsGet(ini, String, StrMake(buffer, (u64)length));
}

function i32 P__ParseI32(String s, i32 fallback)
{
    if (!s.length) return fallback;
    i32 result = 0;
    for (u64 i = 0; i < s.length; ++i) {
        if (s.value[i] < '0' || s.value[i] > '9') return fallback;
        result = result * 10 + (s.value[i] - '0');
    }
    return result;
}

function String P__Lower(Arena *arena, String s)
{
    String result = StrCopy(arena, s);
    for (u64 i = 0; i < result.length; ++i) {
        char c = result.value[i];
        if (c >= 'A' && c <= 'Z') ((char *)result.value)[i] = (char)(c + ('a' - 'A'));
    }
    return result;
}

// XDG data directories in priority order ($XDG_DATA_HOME first).
function ArrayStr P__DataDirs(Arena *arena)
{
    ArrayStr result = {0};
    const char *home = getenv("HOME");
    const char *dataHome = getenv("XDG_DATA_HOME");
    if (dataHome && dataHome[0]) ArrayPush(arena, result, StrCopy(arena, StrFromCStr(dataHome)));
    else if (home && home[0]) ArrayPush(arena, result, StrFormat(arena, "%s/.local/share", home));
    const char *dataDirs = getenv("XDG_DATA_DIRS");
    String dirs = dataDirs && dataDirs[0] ? StrFromCStr(dataDirs) : S("/usr/local/share:/usr/share");
    ArrayStr parts = StrSplit(arena, dirs, ':');
    for (u64 i = 0; i < parts.header.count; ++i) {
        if (parts.v[i].length) ArrayPush(arena, result, StrCopy(arena, parts.v[i]));
    }
    return result;
}

function void P__LoadMime(P__IconData *data, ArrayStr dataDirs)
{
    Arena *arena = &data->arena;
    // Earlier directories win, and globs2 lists higher weights first, so the first entry wins.
    for (u64 d = 0; d < dataDirs.header.count; ++d) {
        String globs = P__ReadFile(arena, StrFormat(arena, "%S/mime/globs2", dataDirs.v[d]));
        ArrayStr lines = StrSplit(arena, globs, '\n');
        for (u64 i = 0; i < lines.header.count; ++i) {
            ArrayStr fields = StrSplit(arena, lines.v[i], ':');
            if (fields.header.count < 3 || fields.v[0].length == 0 || fields.v[0].value[0] == '#') continue;
            String glob = fields.v[2];
            if (glob.length < 3 || glob.value[0] != '*' || glob.value[1] != '.') continue;
            String extension = StrFrom(glob, 2);
            if (StrIndexChar(extension, '.', SSF_None).exists || StrIndexChar(extension, '*', SSF_None).exists ||
                StrIndexChar(extension, '?', SSF_None).exists || StrIndexChar(extension, '[', SSF_None).exists) continue;
            extension = P__Lower(arena, extension);
            if (!MapsIndex(data->mimeByExtension, extension).exists) {
                MapsPut(arena, data->mimeByExtension, extension, fields.v[1]);
            }
        }
        String generic = P__ReadFile(arena, StrFormat(arena, "%S/mime/generic-icons", dataDirs.v[d]));
        lines = StrSplit(arena, generic, '\n');
        for (u64 i = 0; i < lines.header.count; ++i) {
            Index colon = StrIndexChar(lines.v[i], ':', SSF_None);
            if (!colon.exists) continue;
            String mime = StrFromTo(lines.v[i], 0, (i64)colon.value);
            if (!MapsIndex(data->genericIcons, mime).exists) {
                MapsPut(arena, data->genericIcons, mime, P__Trim(StrFrom(lines.v[i], colon.value + 1)));
            }
        }
        String subclasses = P__ReadFile(arena, StrFormat(arena, "%S/mime/subclasses", dataDirs.v[d]));
        lines = StrSplit(arena, subclasses, '\n');
        for (u64 i = 0; i < lines.header.count; ++i) {
            Index space = StrIndexChar(lines.v[i], ' ', SSF_None);
            if (!space.exists) continue;
            String mime = StrFromTo(lines.v[i], 0, (i64)space.value), parent = P__Trim(StrFrom(lines.v[i], space.value + 1));
            String parents = MapsGet(data->mimeParents, String, mime);
            MapsPut(arena, data->mimeParents, mime, parents.length ? StrFormat(arena, "%S;%S", parents, parent) : parent);
        }
    }
}

// The icon theme the desktop is set to: GSettings', then GTK's settings.ini, then KDE's kdeglobals, then Adwaita.
function String P__ConfiguredTheme(Arena *arena)
{
    // GTK on Wayland takes it from GSettings, which dconf reads back only where something set it, so GNOME's own default does not hide the files.
    String arguments[] = { S("dconf"), S("read"), S("/org/gnome/desktop/interface/icon-theme") }, output = {0};
    if (P_RunCommand(arena, (String){0}, arguments, CArrayCount(arguments), 1000, &output)) {
        // "'Win11-dark'\n"
        while (output.length && (output.value[output.length - 1] == '\n' || output.value[output.length - 1] == '\'')) --output.length;
        while (output.length && output.value[0] == '\'') output = StrFrom(output, 1);
        if (output.length) return output;
    }
    const char *home = getenv("HOME");
    const char *configHome = getenv("XDG_CONFIG_HOME");
    String config = configHome && configHome[0] ? StrFromCStr(configHome) : home && home[0] ? StrFormat(arena, "%s/.config", home) : (String){0};
    if (config.length) {
        const char *gtk[] = { "gtk-4.0", "gtk-3.0" };
        for (u32 i = 0; i < CArrayCount(gtk); ++i) {
            String text = P__ReadFile(arena, StrFormat(arena, "%S/%s/settings.ini", config, gtk[i]));
            String name = P__IniGet(P__IniParse(arena, text), S("Settings"), S("gtk-icon-theme-name"));
            if (name.length) return name;
        }
        String name = P__IniGet(P__IniParse(arena, P__ReadFile(arena, StrFormat(arena, "%S/kdeglobals", config))), S("Icons"), S("Theme"));
        if (name.length) return name;
    }
    return S("Adwaita");
}

function void P__AddTheme(P__IconData *data, ArrayStr bases, String name, ArrayStr *pending)
{
    Arena *arena = &data->arena;
    Scratch scratch = GetScratchEx(arena);
    P__IconTheme theme = {0};
    String indexText = {0};
    for (u64 b = 0; b < bases.header.count; ++b) {
        String folder = StrFormat(arena, "%S/%S", bases.v[b], name);
        struct stat info;
        char *cpath = P__FilePathCString(scratch.arena, folder);
        if (!cpath || stat(cpath, &info) != 0 || !S_ISDIR(info.st_mode)) continue;
        ArrayPush(arena, theme.folders, folder);
        if (!indexText.length) indexText = P__ReadFile(arena, StrFormat(arena, "%S/index.theme", folder));
    }
    ReleaseScratch(scratch);
    if (!indexText.length) return;
    P__StrMap index = P__IniParse(arena, indexText);

    String section = S("Icon Theme");
    String directories = P__IniGet(index, section, S("Directories"));
    String scaled = P__IniGet(index, section, S("ScaledDirectories"));
    String all = scaled.length ? StrFormat(arena, "%S,%S", directories, scaled) : directories;
    ArrayStr names = StrSplit(arena, all, ',');
    theme.dirs = ArenaPushArray(arena, names.header.count, P__IconDir);
    for (u64 i = 0; i < names.header.count; ++i) {
        String dir = P__Trim(names.v[i]);
        if (!dir.length) continue;
        i32 size = P__ParseI32(P__IniGet(index, dir, S("Size")), 0);
        i32 scale = P__ParseI32(P__IniGet(index, dir, S("Scale")), 1);
        if (size <= 0) continue;
        String type = P__IniGet(index, dir, S("Type"));
        P__IconDir *entry = &theme.dirs[theme.dirCount++];
        entry->path = dir;
        entry->size = size;
        entry->scale = Max(scale, 1);
        entry->minSize = P__ParseI32(P__IniGet(index, dir, S("MinSize")), size);
        entry->maxSize = P__ParseI32(P__IniGet(index, dir, S("MaxSize")), size);
        entry->threshold = P__ParseI32(P__IniGet(index, dir, S("Threshold")), 2);
        entry->type = StrIsEqual(type, S("Fixed")) ? P__IconDirType_Fixed
                    : StrIsEqual(type, S("Scalable")) ? P__IconDirType_Scalable : P__IconDirType_Threshold;
    }
    data->themes[data->themeCount++] = theme;
    ArrayStr parents = StrSplit(arena, P__IniGet(index, section, S("Inherits")), ',');
    for (u64 i = 0; i < parents.header.count; ++i) {
        String parent = P__Trim(parents.v[i]);
        if (parent.length) ArrayPush(arena, *pending, parent);
    }
}

function P__IconData *P__MakeIconData(String theme)
{
    P__IconData *data = ArenaBootstrapStruct(P__IconData, arena);
    Arena *arena = &data->arena;
    data->theme = StrCopy(arena, theme);
    ArrayStr dataDirs = P__DataDirs(arena);
    data->dataDirs = dataDirs;
    P__LoadMime(data, dataDirs);

    ArrayStr bases = {0};
    const char *home = getenv("HOME");
    if (home && home[0]) ArrayPush(arena, bases, StrFormat(arena, "%s/.icons", home));
    for (u64 i = 0; i < dataDirs.header.count; ++i) ArrayPush(arena, bases, StrFormat(arena, "%S/icons", dataDirs.v[i]));

    // Breadth-first over Inherits, each theme once, hicolor always last.
    enum { MaxThemes = 16 };
    data->themes = ArenaPushArray(arena, MaxThemes, P__IconTheme);
    ArrayStr pending = {0};
    ArrayPush(arena, pending, data->theme);
    ArrayStr seen = {0};
    for (u64 i = 0; i < pending.header.count && data->themeCount < MaxThemes - 1; ++i) {
        String name = pending.v[i];
        b32 duplicate = StrIsEqual(name, S("hicolor"));
        for (u64 j = 0; j < seen.header.count && !duplicate; ++j) duplicate = StrIsEqual(seen.v[j], name);
        if (duplicate) continue;
        ArrayPush(arena, seen, name);
        P__AddTheme(data, bases, name, &pending);
    }
    ArrayStr none = {0};
    P__AddTheme(data, bases, S("hicolor"), &none);
    return data;
}

function void *P__WatchIconTheme(void *unused);

function void P__LoadIconData(void)
{
    Scratch scratch = GetScratch();
    atomic_store(&P__iconData, P__MakeIconData(P__ConfiguredTheme(scratch.arena)));
    ReleaseScratch(scratch);
    pthread_t thread;
    if (pthread_create(&thread, NULL, P__WatchIconTheme, NULL) == 0) pthread_detach(thread);
}

function P__IconData *P__Icons(void)
{
    pthread_once(&P__iconsOnce, P__LoadIconData);
    return atomic_load(&P__iconData);
}

// The configured theme's files are watched, and when the name they give changes the theme's data is made again and the program told.
function void *P__WatchIconTheme(void *unused)
{
    (void)unused;
    const char *home = getenv("HOME"), *configHome = getenv("XDG_CONFIG_HOME");
    Scratch scratch = GetScratch();
    String config = configHome && configHome[0] ? StrFromCStr(configHome) : home && home[0] ? StrFormat(scratch.arena, "%s/.config", home) : (String){0};
    i32 notify = config.length ? inotify_init1(IN_CLOEXEC) : -1;
    local_persist const char *folders[] = { "gtk-3.0", "gtk-4.0", "dconf", "" };
    for (u32 i = 0; notify >= 0 && i < CArrayCount(folders); ++i) {
        char *path = P__FilePathCString(scratch.arena, folders[i][0] ? StrFormat(scratch.arena, "%S/%s", config, folders[i]) : config);
        if (path) inotify_add_watch(notify, path, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    }
    ReleaseScratch(scratch);
    alignas(struct inotify_event) char buffer[4096];
    for (i64 length; notify >= 0 && (length = read(notify, buffer, sizeof(buffer))) > 0;) {
        // Of the folder holding kdeglobals, every other file's writes come too, and are not the theme's.
        b32 theirs = false;
        for (char *at = buffer; at < buffer + length; at += sizeof(struct inotify_event) + ((struct inotify_event *)at)->len) {
            const struct inotify_event *event = (const struct inotify_event *)at;
            String name = event->len ? StrFromCStr(event->name) : (String){0};
            theirs |= StrIsEqual(name, S("settings.ini")) || StrIsEqual(name, S("user")) || StrIsEqual(name, S("kdeglobals"));
        }
        if (!theirs) continue;
        // A settings tool writes its files one after another; the name is read once they are all written.
        struct timespec settle = { .tv_nsec = 200 * 1000000 };
        nanosleep(&settle, NULL);
        scratch = GetScratch();
        String theme = P__ConfiguredTheme(scratch.arena);
        if (!StrIsEqual(theme, P__Icons()->theme)) {
            atomic_store(&P__iconData, P__MakeIconData(theme));
            atomic_store(&P__iconThemeChanged, true);
            P_WakeWindow();
        }
        ReleaseScratch(scratch);
    }
    if (notify >= 0) close(notify);
    ReleaseThreadScratch();
    return NULL;
}

function b32 P_IconThemeChanged(void) { return atomic_exchange(&P__iconThemeChanged, false); }

// 0 for a directory made for icons `size` logical pixels at `scale`, as the freedesktop spec matches them;
// else how far apart their pixel sizes are, at least 1.
// Themes draw some sizes differently, as Win11's monochrome 22 between its colored 16 and 32,
// so a 16 at 125% is looked up as GTK does, as a 16 at scale 2, rather than as the 20 pixels it is drawn at.
function i32 P__IconDirDistance(const P__IconDir *dir, i32 size, i32 scale)
{
    i32 low = dir->size, high = dir->size;
    if (dir->type == P__IconDirType_Scalable) { low = dir->minSize; high = dir->maxSize; }
    if (dir->type == P__IconDirType_Threshold) { low = dir->size - dir->threshold; high = dir->size + dir->threshold; }
    if (dir->scale == scale && size >= low && size <= high) return 0;
    i32 pixels = size * scale;
    low *= dir->scale;
    high *= dir->scale;
    return Max(pixels < low ? low - pixels : pixels > high ? pixels - high : 0, 1);
}

// An exact size in any theme first, then the closest size, in theme order.
// The freedesktop spec settles each theme before its parents; looking for exact sizes across themes first keeps icons sharp when an SVG had to be skipped.
function String P__FindIcon(Arena *arena, String name, i32 size, i32 scale)
{
    local_persist const char *extensions[] = { "png", "svg" };
    const P__IconData *icons = P__Icons();
    for (u32 pass = 0; pass < 2; ++pass)
    for (u64 t = 0; t < icons->themeCount; ++t) {
        const P__IconTheme *theme = &icons->themes[t];
        String best = {0};
        i32 bestDistance = pass == 0 ? 1 : INT32_MAX;
        for (u64 d = 0; d < theme->dirCount; ++d) {
            i32 distance = P__IconDirDistance(&theme->dirs[d], size, scale);
            if (distance >= bestDistance) continue;
            for (u64 f = 0; f < theme->folders.header.count; ++f) {
                for (u32 e = 0; e < CArrayCount(extensions); ++e) {
                    Scratch scratch = GetScratchEx(arena);
                    String path = StrFormat(scratch.arena, "%S/%S/%S.%s", theme->folders.v[f], theme->dirs[d].path, name, extensions[e]);
                    b32 found = P__IconUsable(scratch.arena, path);
                    if (found) {
                        best = StrCopy(arena, path);
                        bestDistance = distance;
                    }
                    ReleaseScratch(scratch);
                    if (found) goto nextDir;
                }
            }
        nextDir:
            if (bestDistance == 0) return best;
        }
        if (best.length) return best;
    }
    return (String){0};
}

function b32 P__DecodePng(Arena *arena, String data, P_Image *image)
{
    i32 width = 0, height = 0, channels = 0;
    u8 *pixels = data.length && data.length < INT32_MAX ? stbi_load_from_memory((const u8 *)data.value, (i32)data.length, &width, &height, &channels, 4) : NULL;
    if (!pixels) return false;
    image->width = width;
    image->height = height;
    image->pixels = ArenaPushArrayNoZero(arena, (u64)width * (u64)height, u32);
    MemCopy(image->pixels, pixels, (u64)width * (u64)height * 4);
    stbi_image_free(pixels);
    return true;
}

// The bytes of the base64 text at the start of `text`, up to the first character that is neither base64 nor white space.
function String P__DecodeBase64(Arena *arena, String text)
{
    u8 *bytes = ArenaPushArrayNoZero(arena, text.length / 4 * 3 + 3, u8);
    u64 count = 0;
    u32 bits = 0, pending = 0;
    for (u64 i = 0; i < text.length; ++i) {
        char c = text.value[i];
        i32 value = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (value < 0) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            break;
        }
        bits = bits << 6 | (u32)value;
        pending += 6;
        if (pending >= 8) {
            pending -= 8;
            bytes[count++] = (u8)(bits >> pending);
        }
    }
    return StrMake((const char *)bytes, count);
}

function b32 P__DecodeIcon(Arena *arena, String path, i32 size, P_Image *image)
{
    Scratch scratch = GetScratchEx(arena);
    String data = P__ReadFile(scratch.arena, path);
    b32 result = false;
    if (data.length && StrIsEqual(StrFrom(path, path.length - 4), S(".png"))) {
        result = P__DecodePng(arena, data, image);
    } else if (data.length) {
        NSVGimage *svg = nsvgParse((char *)data.value, "px", 96.0f);
        if (svg && svg->width > 0 && svg->height > 0) {
            NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
            if (rasterizer) {
                f32 scale = (f32)size / MaxF32(svg->width, svg->height);
                image->width = image->height = size;
                image->pixels = ArenaPushArray(arena, (u64)size * (u64)size, u32);
                f32 x = ((f32)size - svg->width * scale) * 0.5f, y = ((f32)size - svg->height * scale) * 0.5f;
                nsvgRasterize(rasterizer, svg, x, y, scale, (u8 *)image->pixels, size, size, size * 4);
                nsvgDeleteRasterizer(rasterizer);
                for (u64 i = 0; i < (u64)size * (u64)size && !result; ++i) result = (image->pixels[i] >> 24) != 0;
            }
        }
        if (svg) nsvgDelete(svg);
        // Some themes, as Win11's small folders, wrap a PNG in the SVG, which nanosvg does not draw; an icon that draws nothing fails.
        Index embedded = StrIndexStr(data, S("data:image/png;base64,"), SSF_IndexPlusOne);
        if (!result && embedded.exists) result = P__DecodePng(arena, P__DecodeBase64(scratch.arena, StrFrom(data, embedded.value)), image);
    }
    ReleaseScratch(scratch);
    return result;
}

function String P_FileTypeKey(Arena *arena, String path, P_FileKind kind)
{
    if (kind == P_FileKind_Directory) {
        return S("/folder");
    }
    Index slash = StrIndexChar(path, '/', SSF_SearchBackwards | SSF_IndexPlusOne | SSF_CountOnFail);
    String name = StrFrom(path, slash.exists ? slash.value : 0);
    // Dot files are hidden files without an extension.
    Index dot = StrIndexChar(name, '.', SSF_SearchBackwards);
    if (!dot.exists || dot.value == 0 || dot.value + 1 == name.length) {
        return S("/file");
    }
    return P__Lower(arena, StrFormat(arena, ".%S", StrFrom(name, dot.value + 1)));
}

// ponytail: one access() per candidate file and no icon-theme.cache; add the cache if lookups show up in profiles.
function String P_FileIconKey(Arena *arena, String path, P_FileKind kind)
{
    // Each drive its own, since one with Windows on it is marked so.
    if (kind == P_FileKind_Drive) return StrFormat(arena, "drive:%S", path);
    return P_FileTypeKey(arena, path, kind);
}

// Pictures stb_image decodes are their own thumbnails; every other file shows its type's icon.
function b32 P__IsPicture(String typeKey)
{
    local_persist const String pictures[] = { SInit(".png"), SInit(".jpg"), SInit(".jpeg"), SInit(".jpe"), SInit(".gif"), SInit(".bmp") };
    for (u64 i = 0; i < CArrayCount(pictures); ++i) {
        if (StrIsEqual(typeKey, pictures[i])) return true;
    }
    return false;
}

function String P_FileThumbnailKey(Arena *arena, String path, P_FileKind kind)
{
    String key = P_FileIconKey(arena, path, kind);
    return kind == P_FileKind_File && P__IsPicture(key) ? StrCopy(arena, path) : key;
}

function b32 P_LoadImageFile(Arena *arena, String path, i32 maxSide, P_Image *image, i32 *sourceWidth, i32 *sourceHeight)
{
    Scratch scratch = GetScratchEx(arena);
    b32 result = false;
    // Files past this, and pictures of more than this many pixels, are not read into memory.
    const u64 limit = UINT64_C(256) << 20, most = UINT64_C(256) << 20;
    struct stat info;
    char *cpath = P__FilePathCString(scratch.arena, path);
    if (P__IsPicture(P_FileTypeKey(scratch.arena, path, P_FileKind_File)) && cpath && stat(cpath, &info) == 0 && (u64)info.st_size <= limit) {
        String data = P__ReadFile(scratch.arena, path);
        i32 width = 0, height = 0, channels = 0;
        if (data.length && stbi_info_from_memory((const u8 *)data.value, (i32)data.length, &width, &height, &channels) && (u64)width * (u64)height <= most) {
            u8 *pixels = stbi_load_from_memory((const u8 *)data.value, (i32)data.length, &width, &height, &channels, 4);
            if (pixels) {
                // ponytail: a picture larger than the square is shrunk by picking pixels; average them if big photos look rough when fitted.
                i32 shownWidth = width, shownHeight = height;
                if (width > maxSide || height > maxSide) {
                    f64 shrink = (f64)maxSide / (f64)Max(width, height);
                    shownWidth = Max(1, (i32)((f64)width * shrink));
                    shownHeight = Max(1, (i32)((f64)height * shrink));
                }
                u32 *out = ArenaPushArrayNoZero(arena, (u64)shownWidth * (u64)shownHeight, u32);
                const u32 *in = (const u32 *)pixels;
                for (i32 y = 0; y < shownHeight; ++y) {
                    const u32 *line = in + (u64)((i64)y * height / shownHeight) * (u64)width;
                    for (i32 x = 0; x < shownWidth; ++x) out[(u64)y * (u64)shownWidth + (u64)x] = line[(i64)x * width / shownWidth];
                }
                stbi_image_free(pixels);
                *image = (P_Image){ out, shownWidth, shownHeight };
                *sourceWidth = width;
                *sourceHeight = height;
                result = true;
            }
        }
    }
    ReleaseScratch(scratch);
    return result;
}

// ponytail: decodes the whole picture every time; read the freedesktop cache (~/.cache/thumbnails, md5 of the file URI) first if big photo folders feel slow.
function b32 P_LoadFileThumbnail(Arena *arena, String path, P_FileKind kind, i32 size, b32 thumbnailOnly, P_Image *image)
{
    Scratch scratch = GetScratchEx(arena);
    b32 result = false;
    if (kind == P_FileKind_File && P__IsPicture(P_FileTypeKey(scratch.arena, path, kind))) {
        // Files past this are left to their icon rather than read whole into memory.
        const u64 limit = UINT64_C(64) << 20;
        struct stat info;
        char *cpath = P__FilePathCString(scratch.arena, path);
        if (cpath && stat(cpath, &info) == 0 && (u64)info.st_size <= limit) {
            String data = P__ReadFile(scratch.arena, path);
            i32 width = 0, height = 0, channels = 0;
            u8 *pixels = data.length ? stbi_load_from_memory((const u8 *)data.value, (i32)data.length, &width, &height, &channels, 4) : NULL;
            if (pixels) {
                image->width = width;
                image->height = height;
                image->pixels = ArenaPushArrayNoZero(arena, (u64)width * (u64)height, u32);
                MemCopy(image->pixels, pixels, (u64)width * (u64)height * 4);
                stbi_image_free(pixels);
                result = true;
            }
        }
    }
    ReleaseScratch(scratch);
    return result || (!thumbnailOnly && P_LoadFileIcon(arena, path, kind, size, 1, false, image));
}

// ponytail: `exact` is ignored, so the user's folders show the plain folder icon; map them to the theme's folder-download and kin when that matters.
// The icons of `path` when it is one of the user's places, as GTK shows the home, the desktop or the downloads.
// The home has none of its own, as Windows shows the user's folder as a plain one.
function void P__PlaceIconNames(Arena *arena, String path, ArrayStr *names)
{
    local_persist const struct { String place; String icons[2]; } special[] = {
        { SInit("Desktop"), { SInit("user-desktop"), SInit("folder-desktop") } },
        { SInit("Documents"), { SInit("folder-documents") } },
        { SInit("Downloads"), { SInit("folder-download"), SInit("folder-downloads") } },
        { SInit("Music"), { SInit("folder-music") } },
        { SInit("Pictures"), { SInit("folder-pictures") } },
        { SInit("Videos"), { SInit("folder-videos") } },
    };
    if (StrIsEqual(path, P_ComputerIconPath())) {
        ArrayPush(arena, *names, S("computer"));
        return;
    }
    P_ArrayPlace places = P_UserPlaces(arena);
    String absolute = P_AbsolutePath(arena, path);
    for (u64 i = 0; i < places.header.count; ++i) {
        const P_Place *place = &places.v[i];
        if (!StrIsEqual(place->path, absolute)) continue;
        if (StrIsEqual(place->name, S("Trash"))) {
            if (P_TrashHasItems()) ArrayPush(arena, *names, S("user-trash-full"));
            ArrayPush(arena, *names, S("user-trash"));
        }
        for (u32 s = 0; s < CArrayCount(special); ++s) {
            if (!StrIsEqual(place->name, special[s].place)) continue;
            for (u32 n = 0; n < 2; ++n) if (special[s].icons[n].length) ArrayPush(arena, *names, special[s].icons[n]);
        }
        return;
    }
}

// Windows' logo, four squares in its blue, over the top left corner of an icon, where Windows marks its own drive.
function void P__BadgeWindows(P_Image *image)
{
    i32 size = Min(image->width, image->height);
    i32 logo = Max(size * 9 / 20, 4), gap = Max(logo / 9, 1), square = (logo - gap) / 2;
    for (i32 q = 0; q < 4; ++q) {
        i32 x0 = (q & 1) * (square + gap), y0 = (q >> 1) * (square + gap);
        for (i32 y = y0; y < y0 + square; ++y) {
            for (i32 x = x0; x < x0 + square; ++x) image->pixels[(u64)y * (u64)image->width + (u64)x] = 0xFFD47800u;
        }
    }
}

// A small penguin over the top left corner of the root drive's icon, as Windows marks its own drive with its logo.
function void P__BadgeTux(Arena *arena, P_Image *image)
{
    // Drawn here rather than taken from a theme, few of which have one in color; a light rim keeps its black body seen on dark sidebars.
    // At a dozen pixels only its body, belly and feet read, so it has nothing else.
    local_persist const char tux[] =
        "<svg width=\"16\" height=\"16\"><ellipse cx=\"8\" cy=\"8.2\" rx=\"5.2\" ry=\"6.6\" fill=\"#1a1a1a\" stroke=\"#d8d8d8\" stroke-width=\"1\"/>"
        "<ellipse cx=\"8\" cy=\"10\" rx=\"3.5\" ry=\"4.6\" fill=\"#f2f2f2\"/>"
        "<ellipse cx=\"5\" cy=\"14.7\" rx=\"2.6\" ry=\"1.2\" fill=\"#f6b40e\"/><ellipse cx=\"11\" cy=\"14.7\" rx=\"2.6\" ry=\"1.2\" fill=\"#f6b40e\"/></svg>";
    i32 size = Max(Min(image->width, image->height) * 3 / 5, 6);
    // nanosvg parses in place.
    char *text = ArenaPushArray(arena, sizeof(tux), char);
    MemCopy(text, tux, sizeof(tux));
    NSVGimage *svg = nsvgParse(text, "px", 96.0f);
    NSVGrasterizer *rasterizer = svg ? nsvgCreateRasterizer() : NULL;
    if (rasterizer) {
        u32 *badge = ArenaPushArray(arena, (u64)size * (u64)size, u32);
        nsvgRasterize(rasterizer, svg, 0, 0, (f32)size / 16.0f, (u8 *)badge, size, size, size * 4);
        nsvgDeleteRasterizer(rasterizer);
        for (i32 y = 0; y < size && y < image->height; ++y) {
            for (i32 x = 0; x < size && x < image->width; ++x) {
                u32 over = badge[(u64)y * (u64)size + (u64)x], *under = &image->pixels[(u64)y * (u64)image->width + (u64)x];
                u32 alpha = over >> 24, keep = 255 - alpha, result = 0;
                for (u32 shift = 0; shift < 24; shift += 8) result |= ((((over >> shift) & 0xFF) * alpha + ((*under >> shift) & 0xFF) * keep) / 255) << shift;
                *under = result | ((alpha + ((*under >> 24) * keep) / 255) << 24);
            }
        }
    }
    if (svg) nsvgDelete(svg);
}

// Whether an icon is drawn in grays alone, as the small symbolic designs of some themes are.
function b32 P__IsGray(const P_Image *image)
{
    for (u64 i = 0; i < (u64)image->width * (u64)image->height; ++i) {
        u32 pixel = image->pixels[i];
        i32 red = (i32)(pixel & 0xFF), green = (i32)(pixel >> 8 & 0xFF), blue = (i32)(pixel >> 16 & 0xFF);
        i32 spread = Max(Max(red, green), blue) - Min(Min(red, green), blue);
        if ((pixel >> 24) > 32 && spread > 24) return false;
    }
    return true;
}

function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, f32 scale, b32 exact, P_Image *image)
{
    (void)exact;
    i32 wholeScale = scale > 1 ? (i32)ceilf(scale - 0.01f) : 1;
    i32 logical = scale > 0 ? Max((i32)roundf((f32)size / scale), 1) : size;
    pthread_once(&P__iconsOnce, P__LoadIconData);
    Scratch scratch = GetScratchEx(arena);
    ArrayStr names = {0};
    u64 places = 0;  // the names before this are a place's own
    b32 windows = false;
    String key = P_FileIconKey(scratch.arena, path, kind);
    if (kind == P_FileKind_Drive) {
        // A drive with Windows on it takes a theme's icon for one, as some themes name it, or else the usual one marked with Windows' logo.
        // Only a local drive is looked into, since a network share's lookup could hold up the icons behind it.
        windows = P__LocalMount(scratch.arena, path) && P_PathExists(StrFormat(scratch.arena, "%S/Windows/System32", path));
        if (windows) {
            ArrayPush(scratch.arena, names, S("drive-harddisk-windows"));
            ArrayPush(scratch.arena, names, S("drive-windows"));
        }
        ArrayPush(scratch.arena, names, S("drive-harddisk"));
        ArrayPush(scratch.arena, names, S("folder"));
    } else if (kind == P_FileKind_Directory) {
        if (exact) P__PlaceIconNames(scratch.arena, path, &names);
        places = names.header.count;
        ArrayPush(scratch.arena, names, S("folder"));
    } else {
        String mime = key.length && key.value[0] == '.' ? MapsGet(P__Icons()->mimeByExtension, String, StrFrom(key, 1)) : (String){0};
        if (mime.length) {
            String icon = StrCopy(scratch.arena, mime);
            for (u64 i = 0; i < icon.length; ++i) {
                if (icon.value[i] == '/') ((char *)icon.value)[i] = '-';
            }
            ArrayPush(scratch.arena, names, icon);
            String generic = MapsGet(P__Icons()->genericIcons, String, mime);
            Index slash = StrIndexChar(mime, '/', SSF_None);
            if (!generic.length && slash.exists) generic = StrFormat(scratch.arena, "%S-x-generic", StrFromTo(mime, 0, (i64)slash.value));
            if (generic.length) ArrayPush(scratch.arena, names, generic);
        }
        ArrayPush(scratch.arena, names, S("application-x-generic"));
        ArrayPush(scratch.arena, names, S("text-x-generic"));
    }
    b32 result = false;
    for (u64 i = 0; i < names.header.count && !result; ++i) {
        String file = P__FindIcon(scratch.arena, names.v[i], logical, wholeScale);
        result = file.length && P__DecodeIcon(arena, file, size, image);
        // Places show in color, as Windows shows them; where the theme draws this size as a gray symbol, its largest artwork stands in.
        P_Image colored = {0};
        String large = result && i < places && P__IsGray(image) ? P__FindIcon(scratch.arena, names.v[i], 512, 1) : (String){0};
        if (large.length && P__DecodeIcon(arena, large, size, &colored) && !P__IsGray(&colored)) *image = colored;
        if (result && windows && i >= 2) P__BadgeWindows(image);
        if (result && kind == P_FileKind_Drive && StrIsEqual(path, S("/"))) P__BadgeTux(scratch.arena, image);
    }
    ReleaseScratch(scratch);
    return result;
}

// ponytail: the untranslated <comment> only; pick the xml:lang one matching LANG when localization lands.
function String P__MimeComment(Arena *arena, String mime)
{
    const P__IconData *icons = P__Icons();
    for (u64 d = 0; d < icons->dataDirs.header.count; ++d) {
        Scratch scratch = GetScratchEx(arena);
        String xml = P__ReadFile(scratch.arena, StrFormat(scratch.arena, "%S/mime/%S.xml", icons->dataDirs.v[d], mime));
        Index open = StrIndexStr(xml, S("<comment>"), SSF_None);
        Index close = open.exists ? StrIndexStr(StrFrom(xml, open.value), S("</comment>"), SSF_None) : (Index){0};
        String result = {0};
        if (close.exists) {
            String raw = StrFromTo(xml, open.value + 9, (i64)(open.value + close.value));
            StrBuilder builder = {0};
            for (u64 i = 0; i < raw.length; ++i) {
                local_persist const struct { String entity; char c; } entities[] = {
                    { SInit("&amp;"), '&' }, { SInit("&lt;"), '<' }, { SInit("&gt;"), '>' }, { SInit("&quot;"), '"' }, { SInit("&apos;"), '\'' },
                };
                char c = raw.value[i];
                for (u32 e = 0; c == '&' && e < CArrayCount(entities); ++e) {
                    String rest = StrFrom(raw, i);
                    if (rest.length >= entities[e].entity.length && StrIsEqual(StrFromTo(rest, 0, (i64)entities[e].entity.length), entities[e].entity)) {
                        c = entities[e].c;
                        i += entities[e].entity.length - 1;
                        break;
                    }
                }
                StrBuildChar(arena, &builder, c);
            }
            result = builder.buffer;
            if (result.length && result.value[0] >= 'a' && result.value[0] <= 'z') ((char *)result.value)[0] = (char)(result.value[0] - ('a' - 'A'));
        }
        ReleaseScratch(scratch);
        if (result.length) return result;
    }
    return (String){0};
}

function String P_FileTypeName(Arena *arena, String name, P_FileKind kind)
{
    pthread_once(&P__iconsOnce, P__LoadIconData);
    if (kind == P_FileKind_Directory) return S("Folder");
    Scratch scratch = GetScratchEx(arena);
    String key = P_FileTypeKey(scratch.arena, name, kind);
    String result = S("File");
    if (key.length > 1 && key.value[0] == '.') {
        String mime = MapsGet(P__Icons()->mimeByExtension, String, StrFrom(key, 1));
        String comment = mime.length ? P__MimeComment(arena, mime) : (String){0};
        result = comment.length ? comment : StrFormat(arena, "%S file", StrFrom(key, 1));
    }
    ReleaseScratch(scratch);
    return result;
}
