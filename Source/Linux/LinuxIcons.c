// File icons from the freedesktop icon theme: extension -> MIME type (shared-mime-info
// globs) -> icon name -> file in the configured theme or the themes it inherits.
// Requires LinuxFiles.c.

#include <pthread.h>

// Third-party decoders (Lib/): stb_image v2.30 (nothings/stb 2c980bb) for PNG and
// nanosvg (memononen/nanosvg 239e102) for SVG. Both allocate with libc internally;
// everything they return is copied into the caller's arena and freed right away.
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
    i32 size, minSize, maxSize, threshold;
    enum8(P__IconDirType) type;
} P__IconDir;

typedef struct P__IconTheme {
    ArrayStr folders;  // every <base>/<name> that exists
    P__IconDir *dirs;
    u64 dirCount;
} P__IconTheme;

typedef struct P__StrMap { MapHeader header; struct { String key; String value; } *v; } P__StrMap;

// Loaded once on first use and read-only afterwards, so jobs share it without locks.
typedef struct P__IconData {
    Arena arena;
    P__StrMap mimeByExtension;  // "txt" -> "text/plain"
    P__StrMap genericIcons;     // "text/plain" -> "text-x-generic"
    P__IconTheme *themes;       // lookup order: configured theme, what it inherits, hicolor
    u64 themeCount;
} P__IconData;

global P__IconData P__icons;
global pthread_once_t P__iconsOnce = PTHREAD_ONCE_INIT;

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

// ponytail: nanosvg ignores clip paths, masks and filters, so SVGs using them are
// skipped and the lookup falls back to other sizes and inherited themes (usually PNG).
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

function void P__LoadMime(Arena *arena, ArrayStr dataDirs)
{
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
            if (!MapsIndex(P__icons.mimeByExtension, extension).exists) {
                MapsPut(arena, P__icons.mimeByExtension, extension, fields.v[1]);
            }
        }
        String generic = P__ReadFile(arena, StrFormat(arena, "%S/mime/generic-icons", dataDirs.v[d]));
        lines = StrSplit(arena, generic, '\n');
        for (u64 i = 0; i < lines.header.count; ++i) {
            Index colon = StrIndexChar(lines.v[i], ':', SSF_None);
            if (!colon.exists) continue;
            String mime = StrFromTo(lines.v[i], 0, (i64)colon.value);
            if (!MapsIndex(P__icons.genericIcons, mime).exists) {
                MapsPut(arena, P__icons.genericIcons, mime, P__Trim(StrFrom(lines.v[i], colon.value + 1)));
            }
        }
    }
}

// ponytail: GTK settings.ini and kdeglobals only; reading gsettings/dconf would cover GNOME setups without them.
function String P__ConfiguredTheme(Arena *arena)
{
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

function void P__AddTheme(Arena *arena, ArrayStr bases, String name, ArrayStr *pending)
{
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
        entry->size = size * Max(scale, 1);
        entry->minSize = P__ParseI32(P__IniGet(index, dir, S("MinSize")), size) * Max(scale, 1);
        entry->maxSize = P__ParseI32(P__IniGet(index, dir, S("MaxSize")), size) * Max(scale, 1);
        entry->threshold = P__ParseI32(P__IniGet(index, dir, S("Threshold")), 2);
        entry->type = StrIsEqual(type, S("Fixed")) ? P__IconDirType_Fixed
                    : StrIsEqual(type, S("Scalable")) ? P__IconDirType_Scalable : P__IconDirType_Threshold;
    }
    P__icons.themes[P__icons.themeCount++] = theme;
    ArrayStr parents = StrSplit(arena, P__IniGet(index, section, S("Inherits")), ',');
    for (u64 i = 0; i < parents.header.count; ++i) {
        String parent = P__Trim(parents.v[i]);
        if (parent.length) ArrayPush(arena, *pending, parent);
    }
}

function void P__LoadIconData(void)
{
    Arena *arena = &P__icons.arena;
    ArrayStr dataDirs = P__DataDirs(arena);
    P__LoadMime(arena, dataDirs);

    ArrayStr bases = {0};
    const char *home = getenv("HOME");
    if (home && home[0]) ArrayPush(arena, bases, StrFormat(arena, "%s/.icons", home));
    for (u64 i = 0; i < dataDirs.header.count; ++i) ArrayPush(arena, bases, StrFormat(arena, "%S/icons", dataDirs.v[i]));

    // Breadth-first over Inherits, each theme once, hicolor always last.
    enum { MaxThemes = 16 };
    P__icons.themes = ArenaPushArray(arena, MaxThemes, P__IconTheme);
    ArrayStr pending = {0};
    ArrayPush(arena, pending, P__ConfiguredTheme(arena));
    ArrayStr seen = {0};
    for (u64 i = 0; i < pending.header.count && P__icons.themeCount < MaxThemes - 1; ++i) {
        String name = pending.v[i];
        b32 duplicate = StrIsEqual(name, S("hicolor"));
        for (u64 j = 0; j < seen.header.count && !duplicate; ++j) duplicate = StrIsEqual(seen.v[j], name);
        if (duplicate) continue;
        ArrayPush(arena, seen, name);
        P__AddTheme(arena, bases, name, &pending);
    }
    ArrayStr none = {0};
    P__AddTheme(arena, bases, S("hicolor"), &none);
}

function i32 P__IconDirDistance(const P__IconDir *dir, i32 size)
{
    i32 low = dir->size, high = dir->size;
    if (dir->type == P__IconDirType_Scalable) { low = dir->minSize; high = dir->maxSize; }
    if (dir->type == P__IconDirType_Threshold) { low = dir->size - dir->threshold; high = dir->size + dir->threshold; }
    return size < low ? low - size : size > high ? size - high : 0;
}

// An exact size in any theme first, then the closest size, in theme order. The
// freedesktop spec settles each theme before its parents; looking for exact sizes
// across themes first keeps icons sharp when an SVG had to be skipped.
function String P__FindIcon(Arena *arena, String name, i32 size)
{
    local_persist const char *extensions[] = { "png", "svg" };
    for (u32 pass = 0; pass < 2; ++pass)
    for (u64 t = 0; t < P__icons.themeCount; ++t) {
        const P__IconTheme *theme = &P__icons.themes[t];
        String best = {0};
        i32 bestDistance = pass == 0 ? 1 : INT32_MAX;
        for (u64 d = 0; d < theme->dirCount; ++d) {
            i32 distance = P__IconDirDistance(&theme->dirs[d], size);
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

function b32 P__DecodeIcon(Arena *arena, String path, i32 size, P_Image *image)
{
    Scratch scratch = GetScratchEx(arena);
    String data = P__ReadFile(scratch.arena, path);
    b32 result = false;
    if (data.length && data.length < INT32_MAX && StrIsEqual(StrFrom(path, path.length - 4), S(".png"))) {
        i32 width = 0, height = 0, channels = 0;
        u8 *pixels = stbi_load_from_memory((const u8 *)data.value, (i32)data.length, &width, &height, &channels, 4);
        if (pixels) {
            image->width = width;
            image->height = height;
            image->pixels = ArenaPushArrayNoZero(arena, (u64)width * (u64)height, u32);
            MemCopy(image->pixels, pixels, (u64)width * (u64)height * 4);
            stbi_image_free(pixels);
            result = true;
        }
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
                result = true;
            }
        }
        if (svg) nsvgDelete(svg);
    }
    ReleaseScratch(scratch);
    return result;
}

function String P_FileIconKey(Arena *arena, String path, P_FileKind kind)
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
function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, P_Image *image)
{
    pthread_once(&P__iconsOnce, P__LoadIconData);
    Scratch scratch = GetScratchEx(arena);
    ArrayStr names = {0};
    String key = P_FileIconKey(scratch.arena, path, kind);
    if (kind == P_FileKind_Directory) {
        ArrayPush(scratch.arena, names, S("folder"));
    } else {
        String mime = key.length && key.value[0] == '.' ? MapsGet(P__icons.mimeByExtension, String, StrFrom(key, 1)) : (String){0};
        if (mime.length) {
            String icon = StrCopy(scratch.arena, mime);
            for (u64 i = 0; i < icon.length; ++i) {
                if (icon.value[i] == '/') ((char *)icon.value)[i] = '-';
            }
            ArrayPush(scratch.arena, names, icon);
            String generic = MapsGet(P__icons.genericIcons, String, mime);
            Index slash = StrIndexChar(mime, '/', SSF_None);
            if (!generic.length && slash.exists) generic = StrFormat(scratch.arena, "%S-x-generic", StrFromTo(mime, 0, (i64)slash.value));
            if (generic.length) ArrayPush(scratch.arena, names, generic);
        }
        ArrayPush(scratch.arena, names, S("application-x-generic"));
        ArrayPush(scratch.arena, names, S("text-x-generic"));
    }
    b32 result = false;
    for (u64 i = 0; i < names.header.count && !result; ++i) {
        String file = P__FindIcon(scratch.arena, names.v[i], size);
        result = file.length && P__DecodeIcon(arena, file, size, image);
    }
    ReleaseScratch(scratch);
    return result;
}
