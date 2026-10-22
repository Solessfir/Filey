#include <inttypes.h>
#include <math.h>

function String Prog_JoinPath(Arena *arena, String path, String name)
{
    // This PC's and the Recycle Bin's items are named by their whole paths.
    if (StrIsEqual(path, Prog_ThisPC) || P_IsTrashFolder(path)) return StrCopy(arena, name);
    return StrFormat(arena, "%S%s%S", path, path.length && path.value[path.length - 1] == '/' ? "" : "/", name);
}

function String Prog_EntryModified(Arena *arena, const Prog_Entry *entry)
{
    return entry->file.metadataValid ? P_FormatFileTime(arena, entry->file.modifiedTimeUnix) : (String){0};
}

function String Prog__PackedName(const u8 *packed)
{
    u16 length;
    MemCopy(&length, packed, sizeof(length));
    return StrMake((const char *)packed + sizeof(length), length);
}

function String Prog_IndexName(const Prog_Index *index, u64 entry)
{
    return Prog__PackedName(index->names[entry]);
}

function void Prog_IndexAdd(Prog_Index *index, String name, u32 parent, u8 flags)
{
    if (index->count >= Prog_MaxIndexed) return;
    if (!index->names) {
        index->names = ArenaPushArrayNoZero(&index->tables, Prog_MaxIndexed, const u8 *);
        index->parents = ArenaPushArrayNoZero(&index->tables, Prog_MaxIndexed, u32);
        index->flags = ArenaPushArrayNoZero(&index->tables, Prog_MaxIndexed, u8);
    }
    u16 length = (u16)Min(name.length, (u64)UINT16_MAX);
    u8 *packed = _ArenaPush(&index->arena, sizeof(length) + (u64)length, 1, 0);
    MemCopy(packed, &length, sizeof(length));
    MemCopy(packed + sizeof(length), name.value, length);
    index->names[index->count] = packed;
    index->parents[index->count] = parent;
    index->flags[index->count] = flags;
    ++index->count;
}

function void Prog_IndexRelease(Prog_Index *index)
{
    ArenaRelease(&index->arena);
    ArenaRelease(&index->tables);
    *index = (Prog_Index){0};
}

// The path of an index entry written into `text`, cut at `room`.
function String Prog__IndexPathTo(char *text, u64 room, const Prog_Index *index, u32 entry)
{
    // The names from the entry up to its root, then joined from the root down.
    String chain[256];
    u32 depth = 0;
    for (u32 at = entry; at < index->count && depth < CArrayCount(chain); at = index->parents[at]) chain[depth++] = Prog_IndexName(index, at);
    u64 used = 0;
    for (u32 i = depth; i-- > 0;) {
        if (used && used < room && text[used - 1] != '/') text[used++] = '/';
        u64 length = Min(chain[i].length, room - used);
        MemCopy(text + used, chain[i].value, length);
        used += length;
    }
    return StrMake(text, used);
}

function String Prog_IndexPath(Arena *arena, const Prog_Index *index, u32 entry)
{
    u64 length = 0;
    u32 depth = 0;
    for (u32 at = entry; at < index->count && depth < 256; at = index->parents[at], ++depth) length += Prog_IndexName(index, at).length + 1;
    return Prog__IndexPathTo(ArenaPushArray(arena, Max(length, (u64)1), char), Max(length, (u64)1), index, entry);
}

// 1024-based with one decimal, cut rather than rounded, and dropped when zero: "1010 B", "1 kB", "1023.9 kB", "11.7 MB".
function String Prog_FormatSize(Arena *arena, u64 bytes)
{
    local_persist const char *units[] = { "kB", "MB", "GB", "TB" };
    if (bytes < 1024) return StrFormat(arena, "%" PRIu64 " B", bytes);
    // In whole numbers: the whole units, then the tenths the rest holds.
    // A tenth is the unit over ten cut to a whole byte, which makes the rest of a nearly full unit ten tenths, shown as nine.
    u32 unit = 0, shift = 10;
    while (unit + 1 < CArrayCount(units) && bytes >> (shift + 10)) {
        shift += 10;
        ++unit;
    }
    u64 whole = bytes >> shift, tenths = Min((bytes & ((UINT64_C(1) << shift) - 1)) / ((UINT64_C(1) << shift) / 10), UINT64_C(9));
    return tenths ? StrFormat(arena, "%" PRIu64 ".%" PRIu64 " %s", whole, tenths, units[unit]) : StrFormat(arena, "%" PRIu64 " %s", whole, units[unit]);
}

function u8 Prog__FoldAscii(u8 c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

// The last component of an entry's name, which in an expanded listing is a path.
function String Prog_EntryLeaf(String name)
{
    Index slash = StrIndexChar(name, '/', SSF_SearchBackwards);
    return slash.exists ? StrFromTo(name, slash.value + 1, (i64)name.length) : name;
}

// The folder an entry is in, relative to the shown folder; empty for its own items.
function String Prog_EntryParent(String name)
{
    Index slash = StrIndexChar(name, '/', SSF_SearchBackwards);
    return slash.exists ? StrFromTo(name, 0, (i64)slash.value) : (String){0};
}

// The lowercase form of `c` for matching.
// Accents stay, so "e" does not match "é".
// ponytail: Latin, Greek and Cyrillic only; add a generated case table if other scripts need it.
function rune Prog__FoldRune(rune c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0xC0) return c;
    if (c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x137 && c != 0x130) return c | 1;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c + (c & 1);
    if (c >= 0x14A && c <= 0x177) return c | 1;
    if (c == 0x178) return 0xFF;
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

// Whether the letters of `filter` appear in `name` in order, ignoring case.
// Each letter takes its first occurrence after the previous one.
// `marks`, when not NULL, flags the bytes of `name` that matched.
function b32 Prog_FilterMatch(String name, String filter, u8 *marks)
{
    if (marks) MemZero(marks, name.length);
    u64 at = 0;
    for (u64 from = 0; from < filter.length;) {
        rune want = Prog__FoldRune(StrDecodeUtf8(filter, &from));
        for (;;) {
            if (at >= name.length) return false;
            u64 start = at;
            if (Prog__FoldRune(StrDecodeUtf8(name, &at)) == want) {
                if (marks) for (u64 i = start; i < at; ++i) marks[i] = 1;
                break;
            }
        }
    }
    return true;
}

enum { Prog_FuzzyText = 512, Prog_FuzzyTerm = 64 };

// A text's letters for scoring, decoded once the first term may match it.
typedef struct Prog__FuzzyText {
    String text;
    rune original[Prog_FuzzyText], folded[Prog_FuzzyText];
    u32 offsets[Prog_FuzzyText + 1];  // each letter's first byte, and the end
    u32 count;
    b32 decoded;
} Prog__FuzzyText;

typedef struct Prog__Fuzzy {
    const rune *term, *text, *folded;
    u32 termCount, count, calls;
} Prog__Fuzzy;

// The score for the letters of a term placed at `at` in `text`: placed letters that follow one another, start the text,
// follow a lowercase letter as an uppercase one or follow "_" or a space score; letters before the first, letters left over and gaps cost.
function i32 Prog__FuzzyScore(const Prog__Fuzzy *f, const u32 *at)
{
    i32 score = 100 + ((i32)f->termCount - (i32)f->count) * 3 - (i32)at[0];
    for (u32 i = 0; i < f->termCount; ++i) {
        u32 place = at[i];
        if (i) {
            i32 gap = (i32)(place - at[i - 1]);
            if (gap > 5) score += (5 - gap) * 50;
            if (gap == 1) score += 200;
        }
        if (place == 0) {
            score += 50;
            continue;
        }
        rune previous = f->text[place - 1], current = f->text[place];
        b32 lower = (previous >= 'a' && previous <= 'z') || (previous >= 0xE0 && previous <= 0xFF && previous != 0xF7) || (previous >= 0x430 && previous <= 0x45F);
        if (lower && Prog__FoldRune(current) != current) score += 30;
        if (previous == '_' || previous == ' ') score += 25;
    }
    return score;
}

// Places the term's letters from `placed` on in the text from `from` on, the best of the ways tried.
function b32 Prog__FuzzyPlace(Prog__Fuzzy *f, u32 placed, u32 from, u32 *at, i32 *score)
{
    // It gives up after ten tries.
    if (++f->calls >= 10) return false;
    u32 own[Prog_FuzzyTerm], other[Prog_FuzzyTerm], best[Prog_FuzzyTerm];
    MemCopy(own, at, placed * sizeof(u32));
    b32 retried = false;
    i32 retriedScore = 0;
    for (u32 t = from; placed < f->termCount && t < f->count; ++t) {
        if (f->folded[t] != f->term[placed]) continue;
        // The same letter placed further on instead.
        i32 otherScore = 0;
        MemCopy(other, own, placed * sizeof(u32));
        if (Prog__FuzzyPlace(f, placed, t + 1, other, &otherScore) && (!retried || otherScore > retriedScore)) {
            MemCopy(best, other, f->termCount * sizeof(u32));
            retriedScore = otherScore;
            retried = true;
        }
        own[placed++] = t;
    }
    b32 matched = placed == f->termCount;
    i32 ownScore = matched ? Prog__FuzzyScore(f, own) : 0;
    if (retried && (!matched || retriedScore > ownScore)) {
        MemCopy(at, best, f->termCount * sizeof(u32));
        *score = retriedScore;
        return true;
    }
    if (!matched) return false;
    MemCopy(at, own, f->termCount * sizeof(u32));
    *score = ownScore;
    return true;
}

// The score of one term in `t`, or 0 when it does not hold the term's letters in order; `marks` flags the bytes of the letters placed.
function i32 Prog__FuzzyTerm(Prog__FuzzyText *t, const rune *term, u32 termCount, String termText, u8 *marks)
{
    String text = t->text;
    // Most texts do not hold the letters at all, which this finds without scoring; bytes for a term of ASCII letters, which no other letter folds to.
    b32 ascii = true;
    for (u32 i = 0; i < termCount; ++i) ascii &= term[i] < 0x80;
    if (ascii) {
        u32 found = 0;
        for (u64 i = 0; i < text.length && found < termCount; ++i) found += (rune)Prog__FoldAscii((u8)text.value[i]) == term[found];
        if (found < termCount) return 0;
    } else if (!Prog_FilterMatch(text, termText, NULL)) {
        return 0;
    }
    if (!t->decoded) {
        for (u64 from = 0; from < text.length && t->count < Prog_FuzzyText;) {
            t->offsets[t->count] = (u32)from;
            t->original[t->count] = StrDecodeUtf8(text, &from);
            t->folded[t->count] = Prog__FoldRune(t->original[t->count]);
            t->offsets[++t->count] = (u32)from;
        }
        t->decoded = true;
    }
    Prog__Fuzzy f = { term, t->original, t->folded, termCount, t->count, 0 };
    u32 places[Prog_FuzzyTerm];
    i32 score = 0;
    if (!Prog__FuzzyPlace(&f, 0, 0, places, &score)) return 0;
    if (marks) for (u32 i = 0; i < termCount; ++i) for (u32 b = t->offsets[places[i]]; b < t->offsets[places[i] + 1]; ++b) marks[b] = 1;
    return score;
}

// The score of a name and its path for `typed`: for each of its space-separated terms, the name's score and three quarters
// of the path's, rounded half away from zero, where a text that does not hold the term's letters in order, ignoring case, scores 0.
// `nameMarks` and `pathMarks`, when not NULL, flag the bytes of the letters placed.
function i32 Prog_FuzzyScore(String name, String path, String typed, u8 *nameMarks, u8 *pathMarks)
{
    if (nameMarks) MemZero(nameMarks, name.length);
    if (pathMarks) MemZero(pathMarks, path.length);
    Prog__FuzzyText names, paths;
    names.text = name, names.count = 0, names.decoded = false;
    paths.text = path, paths.count = 0, paths.decoded = false;
    rune term[Prog_FuzzyTerm];
    i32 result = 0;
    for (u64 at = 0; at < typed.length;) {
        u32 termCount = 0;
        while (at < typed.length && typed.value[at] == ' ') ++at;
        u64 termStart = at;
        while (at < typed.length && typed.value[at] != ' ') {
            rune c = Prog__FoldRune(StrDecodeUtf8(typed, &at));
            if (termCount < Prog_FuzzyTerm) term[termCount++] = c;
        }
        if (!termCount) continue;
        String termText = StrFromTo(typed, termStart, (i64)at);
        result += Prog__FuzzyTerm(&names, term, termCount, termText, nameMarks);
        if (path.length) {
            f32 scaled = (f32)Prog__FuzzyTerm(&paths, term, termCount, termText, pathMarks) * 0.75f;
            result += (i32)(scaled < 0 ? scaled - 0.5f : scaled + 0.5f);
        }
    }
    return result;
}

// What the filter scores an entry with: its own name, and the folder it is in for a listing of the subfolders too.
function i32 Prog_EntryScore(String name, String filter, u8 *marks)
{
    String leaf = Prog_EntryLeaf(name);
    u64 prefix = name.length - leaf.length;
    return Prog_FuzzyScore(leaf, StrFromTo(name, 0, prefix ? (i64)prefix - 1 : 0), filter, marks ? marks + prefix : NULL, marks);
}

// Filter scores as ranks for a counting sort, best first; scores of Prog_FilterRanks and above tie, which only long texts of several words reach.
enum { Prog_FilterRanks = 8192 };

function u16 Prog_FilterRank(i32 score)
{
    return (u16)(Prog_FilterRanks - 1 - Min(Max(score, 0), (i32)Prog_FilterRanks - 1));
}

#define Prog_WalkLanes 8

enum { Prog_FilterBlock = 4096, Prog_FilterHidden = 0xFFFF };

// The filter's look through a listing: lanes take blocks of its entries in turn and rank each, or leave it out.
typedef struct Prog__FilterLook {
    const Prog_State *state;
    String filter;
    u16 *ranks;
    atomic_int *parentScores;  // per folder of a listing of the subfolders, its part of the score once a lane has worked it out
    atomic_uint_fast64_t next;
} Prog__FilterLook;

enum { Prog_FilterUnscored = INT32_MIN };

// Whether `filter` names `type`; an empty one names every type.
function b32 Prog_TypeShown(const Prog_TypeFilter *filter, String type)
{
    if (!filter->length) return true;
    for (u32 at = 0; at < filter->length;) {
        u32 end = at;
        while (end < filter->length && filter->names[end] != '\n') ++end;
        if (StrIsEqual(StrMake((const char *)filter->names + at, end - at), type)) return true;
        at = end + 1;
    }
    return false;
}

// Adds `type` to `filter`, or takes it out; false when it has no room for it.
function b32 Prog_ToggleType(Prog_TypeFilter *filter, String type)
{
    for (u32 at = 0; at < filter->length;) {
        u32 end = at;
        while (end < filter->length && filter->names[end] != '\n') ++end;
        if (StrIsEqual(StrMake((const char *)filter->names + at, end - at), type)) {
            MemMove(filter->names + at, filter->names + end + 1, filter->length - end - 1);
            filter->length -= end + 1 - at;
            return true;
        }
        at = end + 1;
    }
    if (filter->length + type.length + 1 > sizeof(filter->names)) return false;
    MemCopy(filter->names + filter->length, type.value, type.length);
    filter->length += (u32)type.length;
    filter->names[filter->length++] = '\n';
    return true;
}

function void Prog__FilterLane(void *user, u32 lane)
{
    (void)lane;
    Prog__FilterLook *look = user;
    const Prog_State *state = look->state;
    for (;;) {
        u64 from = atomic_fetch_add(&look->next, 1) * Prog_FilterBlock;
        if (from >= state->directory.count) break;
        u64 to = Min(from + Prog_FilterBlock, state->directory.count);
        for (u64 i = from; i < to; ++i) {
            const Prog_Entry *entry = &state->directory.entries[i];
            b32 hidden = (entry->file.kind == P_FileKind_Directory ? state->hideFolders : state->hideFiles) || !Prog_TypeShown(&state->types, entry->type);
            i32 score = hidden ? 0 : 1;
            if (!hidden && look->filter.length) {
                String name = entry->file.name, leaf = Prog_EntryLeaf(name);
                score = Prog_FuzzyScore(leaf, (String){0}, look->filter, NULL, NULL);
                if (leaf.length < name.length) {
                    // A listing of the subfolders has many entries in each folder, whose part of the score is the same for all of them;
                    // an entry added since the listing has its folder looked at on its own.
                    String folder = StrFromTo(name, 0, (i64)(name.length - leaf.length - 1));
                    b32 listed = entry->parent < state->directory.parentCount && StrIsEqual(state->directory.parents[entry->parent], folder);
                    i32 folderScore = listed ? atomic_load_explicit(&look->parentScores[entry->parent], memory_order_relaxed) : Prog_FilterUnscored;
                    if (folderScore == Prog_FilterUnscored) {
                        folderScore = Prog_FuzzyScore((String){0}, folder, look->filter, NULL, NULL);
                        if (listed) atomic_store_explicit(&look->parentScores[entry->parent], folderScore, memory_order_relaxed);
                    }
                    score += folderScore;
                }
            }
            look->ranks[i] = score > 0 ? Prog_FilterRank(score) : Prog_FilterHidden;
        }
    }
}

// Rebuilds the view from the filter, the folder and file toggles and the types shown, and drops hidden rows from the selection.
// After an edit the focus moves to the first match, so that Enter opens it; a listing without a focused item puts it on the first one, as a cursor that selects nothing.
function void Prog_ApplyFilter(Prog_State *state, b32 edited)
{
    ProfileScope("ApplyFilter") {
        ArenaClear(&state->viewArena);
        String filter = Prog_EditText(&state->filter);
        state->view = ArenaPushArrayNoZero(&state->viewArena, Max(state->directory.count, (u64)1), u64);
        state->viewCount = state->viewFolders = 0;
        state->selectedCount = 0;
        Scratch scratch = GetScratch();
        // What matches comes best first, folders still before files and in the listing's order within a score;
        // a big listing is ranked on several lanes.
        Prog__FilterLook look = { .state = state, .filter = filter, .ranks = ArenaPushArrayNoZero(scratch.arena, Max(state->directory.count, (u64)1), u16) };
        look.parentScores = ArenaPushArrayNoZero(scratch.arena, Max(state->directory.parentCount, 1u), atomic_int);
        for (u32 i = 0; i < state->directory.parentCount; ++i) atomic_init(&look.parentScores[i], Prog_FilterUnscored);
        u32 lanes = state->directory.count >= 4 * Prog_FilterBlock ? Max(1u, Min(P_ProcessorCount(), (u32)Prog_WalkLanes)) : 1;
        if (lanes > 1) P_RunLanes(lanes, Prog__FilterLane, &look);
        else Prog__FilterLane(&look, 0);
        u16 *ranks = filter.length ? look.ranks : NULL;
        for (u64 i = 0; i < state->directory.count; ++i) {
            Prog_Entry *entry = &state->directory.entries[i];
            b32 folder = entry->file.kind == P_FileKind_Directory;
            if (look.ranks[i] != Prog_FilterHidden) {
                if (ranks) ranks[state->viewCount] = look.ranks[i];
                state->view[state->viewCount++] = i;
                state->viewFolders += folder != 0;
            } else {
                entry->selected = false;
            }
            state->selectedCount += entry->selected != 0;
        }
        if (ranks && state->viewCount > 1) {
            u32 *starts = ArenaPushArray(scratch.arena, 2 * Prog_FilterRanks + 1, u32);
            u64 *sorted = ArenaPushArrayNoZero(scratch.arena, state->viewCount, u64);
            for (u64 i = 0; i < state->viewCount; ++i) {
                ranks[i] = (u16)(ranks[i] + (state->directory.entries[state->view[i]].file.kind == P_FileKind_Directory ? 0 : Prog_FilterRanks));
                ++starts[ranks[i] + 1];
            }
            for (u32 r = 0; r < 2 * Prog_FilterRanks; ++r) starts[r + 1] += starts[r];
            for (u64 i = 0; i < state->viewCount; ++i) sorted[starts[ranks[i]]++] = state->view[i];
            MemCopy(state->view, sorted, state->viewCount * sizeof(u64));
        }
        ReleaseScratch(scratch);
        // Each row's place in the view, as a filter orders it by how well it matches rather than by row.
        state->viewAt = ArenaPushArrayNoZero(&state->viewArena, Max(state->directory.count, (u64)1), u64);
        for (u64 i = 0; i < state->directory.count; ++i) state->viewAt[i] = UINT64_MAX;
        for (u64 p = 0; p < state->viewCount; ++p) state->viewAt[state->view[p]] = p;
        if (edited || !state->hasFocus || Prog_ViewPosition(state, state->focusedRow) == UINT64_MAX) {
            state->hasFocus = state->viewCount != 0;
            state->focusedRow = state->viewCount ? state->view[0] : 0;
            state->revealFocus = true;
        }
        if (state->hasAnchor && Prog_ViewPosition(state, state->anchorRow) == UINT64_MAX) {
            state->hasAnchor = state->hasFocus;
            state->anchorRow = state->focusedRow;
        }
    }
}

function void Prog_ClearFilter(Prog_State *state)
{
    if (state->filter.length) ++state->filter.version;
    state->filter.length = state->filter.cursor = state->filter.anchor = 0;
    state->filtering = false;
}

// Where listing row `row` is in the view, or UINT64_MAX when the filter hides it.
function u64 Prog_ViewPosition(const Prog_State *state, u64 row)
{
    return state->viewAt && row < state->directory.count ? state->viewAt[row] : UINT64_MAX;
}

// The last component of a folder path, or the whole path for a root such as "C:/" or "/".
function String Prog__FolderName(String path)
{
    String trimmed = path;
    while (trimmed.length > 1 && trimmed.value[trimmed.length - 1] == '/' && trimmed.value[trimmed.length - 2] != ':') --trimmed.length;
    Index slash = StrIndexChar(trimmed, '/', SSF_SearchBackwards);
    return slash.exists && slash.value + 1 < trimmed.length ? StrFrom(trimmed, slash.value + 1) : trimmed;
}

// Where Up goes from `path`: its parent, This PC from a drive's root, and nowhere from This PC.
function String Prog_UpPath(String path)
{
    if (StrIsEqual(path, Prog_ThisPC)) return (String){0};
    String parent = Prog_ParentPath(path);
    if (!parent.length || StrIsEqual(parent, path)) return path.length ? Prog_ThisPC : (String){0};
    return parent;
}

function String Prog_ParentPath(String path)
{
    u64 root = 1;
    if (path.length >= 3 && path.value[1] == ':' && path.value[2] == '/') {
        root = 3;
    } else if (path.length >= 2 && path.value[0] == '/' && path.value[1] == '/') {
        u64 start = 2;
        u32 components = 2;
        if (path.length >= 4 && path.value[2] == '?' && path.value[3] == '/') {
            start = 4;
            components = 1;
            if (path.length >= 8 && Prog__FoldAscii((u8)path.value[4]) == 'u' && Prog__FoldAscii((u8)path.value[5]) == 'n' && Prog__FoldAscii((u8)path.value[6]) == 'c' && path.value[7] == '/') {
                start = 8;
                components = 2;
            }
        }
        root = path.length;
        for (u64 i = start; i < path.length; ++i) {
            if (path.value[i] == '/' && --components == 0) {
                root = i + 1;
                break;
            }
        }
    }
    u64 end = path.length;
    while (end > root && path.value[end - 1] == '/') --end;
    while (end > root && path.value[end - 1] != '/') --end;
    if (end > root) --end;
    return StrMake(path.value, end);
}

function i32 Prog_CompareNames(String a, String b)
{
    u64 ai = 0, bi = 0;
    while (ai < a.length && bi < b.length) {
        u8 ac = Prog__FoldAscii((u8)a.value[ai]);
        u8 bc = Prog__FoldAscii((u8)b.value[bi]);
        if (ac >= '0' && ac <= '9' && bc >= '0' && bc <= '9') {
            u64 ae = ai, be = bi;
            while (ae < a.length && a.value[ae] >= '0' && a.value[ae] <= '9') ++ae;
            while (be < b.length && b.value[be] >= '0' && b.value[be] <= '9') ++be;
            u64 az = ai, bz = bi;
            while (az < ae && a.value[az] == '0') ++az;
            while (bz < be && b.value[bz] == '0') ++bz;
            if (ae - az != be - bz) return ae - az < be - bz ? -1 : 1;
            for (u64 i = 0; i < ae - az; ++i) {
                if (a.value[az + i] != b.value[bz + i]) return a.value[az + i] < b.value[bz + i] ? -1 : 1;
            }
            ai = ae;
            bi = be;
        } else {
            if (ac != bc) return ac < bc ? -1 : 1;
            ++ai;
            ++bi;
        }
    }
    if (ai != a.length || bi != b.length) return ai == a.length ? -1 : 1;
    // shortcut: Unicode names use UTF-8 byte order; add locale collation when its behavior is specified.
    u64 count = Min(a.length, b.length);
    for (u64 i = 0; i < count; ++i) {
        if ((u8)a.value[i] != (u8)b.value[i]) return (u8)a.value[i] < (u8)b.value[i] ? -1 : 1;
    }
    return a.length == b.length ? 0 : a.length < b.length ? -1 : 1;
}

#define Prog__Order(a, b) ((a) == (b) ? 0 : (a) < (b) ? -1 : 1)

// Folders first, then by `sort`, then by name: in an expanded listing by the item's own name and then its place.
// Folders sort by their totals once measured, and by zero until then.
function i32 Prog__CompareEntries(const Prog_Entry *a, const Prog_Entry *b, Prog_Sort sort, b32 descending, Prog_FolderSort folders)
{
    if (a->recentFirst != b->recentFirst) return a->recentFirst ? -1 : 1;
    b32 ad = a->file.kind == P_FileKind_Directory;
    b32 bd = b->file.kind == P_FileKind_Directory;
    if (ad != bd && folders != Prog_FolderSort_Mixed) return ad == (folders == Prog_FolderSort_First) ? -1 : 1;
    // Items that show another name than their file's go by it.
    String an = a->label.length ? a->label : Prog_EntryLeaf(a->file.name), bn = b->label.length ? b->label : Prog_EntryLeaf(b->file.name);
    i32 result = 0;
    switch (sort) {
        case Prog_Sort_Name: break;
        case Prog_Sort_Type: result = Prog_CompareNames(a->type, b->type); break;
        case Prog_Sort_Modified:
            result = a->file.modifiedTimeUnix == b->file.modifiedTimeUnix ? 0 : a->file.modifiedTimeUnix < b->file.modifiedTimeUnix ? -1 : 1;
            break;
        case Prog_Sort_Size: result = Prog__Order(ad ? a->bytes : a->file.size, bd ? b->bytes : b->file.size); break;
        case Prog_Sort_Folders: result = Prog__Order(a->folders, b->folders); break;
        case Prog_Sort_Files: result = Prog__Order(a->files, b->files); break;
        case Prog_Sort_Created: result = Prog__Order(a->file.createdTimeUnix, b->file.createdTimeUnix); break;
        case Prog_Sort_Accessed: result = Prog__Order(a->file.accessedTimeUnix, b->file.accessedTimeUnix); break;
        case Prog_Sort_Items: result = Prog__Order(a->folders + a->files, b->folders + b->files); break;
        default: InvalidPath;
    }
    if (!result) result = Prog_CompareNames(an, bn);
    if (!result) result = Prog_CompareNames(a->file.name, b->file.name);
    return descending ? -result : result;
}

// What a sort compares most entries by without reaching into them, which a big listing's entries, spread over far more memory than the
// cache holds, would be slow to: the group it goes in (recents first, then folders before or after files), the sorted value, and the
// first characters of the name (or of the type, when sorting by type) up to its first digit, folded as the names' order folds them.
typedef struct Prog__SortKey {
    u64 group;
    u64 value;
    String name;       // the name shown, which the order is next decided by, after the type when sorting by type
    String type;
    u64 prefix;        // the characters from the top byte down, zero past `prefixLength`
    u32 index;
    u8 prefixLength;
    b8 prefixWhole;    // the prefix is the whole name, which ends there
} Prog__SortKey;

typedef struct Prog__Sort {
    const Prog_Entry *entries;
    Prog__SortKey *keys, *spare;
    u64 count, slice;
    Prog_Sort sort;
    b32 descending;
    Prog_FolderSort folders;
    const P_CancelToken *cancel;
} Prog__Sort;

function Prog__SortKey Prog__MakeSortKey(const Prog_Entry *entry, u32 index, Prog_Sort sort, Prog_FolderSort folders)
{
    b32 folder = entry->file.kind == P_FileKind_Directory;
    Prog__SortKey result = { .index = index };
    result.group = (entry->recentFirst ? 0 : 2) | (folders == Prog_FolderSort_Mixed ? 0 : folder == (folders == Prog_FolderSort_First) ? 0 : 1);
    // Signed times keep their order as unsigned once the sign bit is flipped.
    u64 flip = UINT64_C(1) << 63;
    switch (sort) {
        case Prog_Sort_Name: case Prog_Sort_Type: break;
        case Prog_Sort_Modified: result.value = (u64)entry->file.modifiedTimeUnix ^ flip; break;
        case Prog_Sort_Size: result.value = folder ? entry->bytes : entry->file.size; break;
        case Prog_Sort_Folders: result.value = entry->folders; break;
        case Prog_Sort_Files: result.value = entry->files; break;
        case Prog_Sort_Created: result.value = (u64)entry->file.createdTimeUnix ^ flip; break;
        case Prog_Sort_Accessed: result.value = (u64)entry->file.accessedTimeUnix ^ flip; break;
        case Prog_Sort_Items: result.value = entry->folders + entry->files; break;
        default: InvalidPath;
    }
    result.name = entry->label.length ? entry->label : Prog_EntryLeaf(entry->file.name);
    if (sort == Prog_Sort_Type) result.type = entry->type;
    String name = sort == Prog_Sort_Type ? result.type : result.name;
    u64 length = 0;
    while (length < 8 && length < name.length && !(name.value[length] >= '0' && name.value[length] <= '9')) {
        result.prefix |= (u64)Prog__FoldAscii((u8)name.value[length]) << (56 - 8 * length);
        ++length;
    }
    result.prefixLength = (u8)length;
    result.prefixWhole = length == name.length;
    return result;
}

// Orders two keys as Prog__CompareEntries orders their entries, which it falls back to when the keys alone cannot tell.
function i32 Prog__CompareKeys(const Prog__Sort *job, const Prog__SortKey *a, const Prog__SortKey *b)
{
    if (a->group != b->group) return a->group < b->group ? -1 : 1;
    i32 result = 0;
    if (a->value != b->value) {
        result = a->value < b->value ? -1 : 1;
    } else {
        // Up to the shorter prefix both go character by character; a difference there decides, as does one name ending while the other goes on.
        u64 shared = Min((u64)a->prefixLength, (u64)b->prefixLength);
        u64 mask = shared ? ~UINT64_C(0) << (64 - 8 * shared) : 0;
        if ((a->prefix ^ b->prefix) & mask) result = a->prefix < b->prefix ? -1 : 1;
        else if (a->prefixWhole && a->prefixLength == shared && !(b->prefixWhole && b->prefixLength == shared)) result = -1;
        else if (b->prefixWhole && b->prefixLength == shared && !(a->prefixWhole && a->prefixLength == shared)) result = 1;
        else {
            if (job->sort == Prog_Sort_Type) result = Prog_CompareNames(a->type, b->type);
            if (!result) result = Prog_CompareNames(a->name, b->name);
            if (!result) return Prog__CompareEntries(&job->entries[a->index], &job->entries[b->index], job->sort, job->descending, job->folders);
        }
    }
    return job->descending ? -result : result;
}

// One stable bottom-up merge sort of `keys` from `start` to `end`, runs of `width` already sorted, using `spare` beside it; the result ends in
// `keys`. Returns false when cancelled.
function b32 Prog__SortRun(const Prog__Sort *job, u64 start, u64 end, u64 width)
{
    Prog__SortKey *from = job->keys, *to = job->spare;
    for (; width < end - start; width *= 2) {
        for (u64 run = start; run < end; run += width * 2) {
            u64 middle = Min(run + width, end), stop = Min(run + width * 2, end);
            u64 left = run, right = middle;
            for (u64 out = run; out < stop; ++out) {
                if (job->cancel && (out & 1023) == 0 && P_IsCancelled(job->cancel)) return false;
                b32 takeLeft = right == stop || (left < middle && Prog__CompareKeys(job, &from[left], &from[right]) <= 0);
                to[out] = from[takeLeft ? left++ : right++];
            }
        }
        Prog__SortKey *swap = from;
        from = to;
        to = swap;
    }
    if (from != job->keys) MemCopy(job->keys + start, from + start, (end - start) * sizeof(Prog__SortKey));
    return true;
}

function void Prog__SortLane(void *user, u32 lane)
{
    Prog__Sort *job = user;
    u64 start = Min((u64)lane * job->slice, job->count), end = Min(start + job->slice, job->count);
    for (u64 i = start; i < end; ++i) job->keys[i] = Prog__MakeSortKey(&job->entries[i], (u32)i, job->sort, job->folders);
    if (start < end) Prog__SortRun(job, start, end, 1);
}

// Stable merge sort into `buffer`, which it returns, or NULL when cancelled.
// It sorts keys rather than the entries, so that each entry moves once; a big listing is sorted in slices on several threads at once,
// which are then merged.
function Prog_Entry *Prog__SortEntries(Prog_Entry *entries, Prog_Entry *buffer, u64 count, Prog_Sort sort, b32 descending, Prog_FolderSort folders,
                                       const P_CancelToken *cancel)
{
    Scratch scratch = GetScratch();
    u32 lanes = count >= 65536 ? Max(1u, Min(P_ProcessorCount(), (u32)Prog_WalkLanes)) : 1;
    Prog__Sort job = {
        entries, ArenaPushArrayNoZero(scratch.arena, Max(count, (u64)1), Prog__SortKey), ArenaPushArrayNoZero(scratch.arena, Max(count, (u64)1), Prog__SortKey),
        count, (count + lanes - 1) / lanes, sort, descending, folders, cancel,
    };
    if (lanes > 1) P_RunLanes(lanes, Prog__SortLane, &job);
    else Prog__SortLane(&job, 0);
    b32 done = !(cancel && P_IsCancelled(cancel)) && (lanes == 1 || Prog__SortRun(&job, 0, count, job.slice));
    if (done) for (u64 i = 0; i < count; ++i) buffer[i] = entries[job.keys[i].index];
    ReleaseScratch(scratch);
    return done ? buffer : NULL;
}

// Sorts on folder totals are redone as the totals arrive.
// Focus, anchor and selection stay on the same items.
function void Prog__SortByTotals(Prog_State *state)
{
    Prog_Directory *directory = &state->directory;
    Prog_Sort sort = directory->sort;
    if (sort != Prog_Sort_Size && sort != Prog_Sort_Folders && sort != Prog_Sort_Files && sort != Prog_Sort_Items) return;
    // An open menu or a drag refers to rows by index.
    if (state->menuOpen || state->dragging) {
        state->sortPending = true;
        return;
    }
    state->sortPending = false;
    b32 sorted = true;
    for (u64 i = 1; i < directory->count && sorted; ++i) {
        sorted = Prog__CompareEntries(&directory->entries[i - 1], &directory->entries[i], sort, directory->descending, directory->folderSort) <= 0;
    }
    if (sorted) return;
    String focus = state->hasFocus ? directory->entries[state->focusedRow].file.name : (String){0};
    String anchor = state->hasAnchor ? directory->entries[state->anchorRow].file.name : (String){0};
    Scratch scratch = GetScratch();
    Prog_Entry *buffer = ArenaPushArrayNoZero(scratch.arena, directory->count, Prog_Entry);
    Prog_Entry *result = Prog__SortEntries(directory->entries, buffer, directory->count, sort, directory->descending, directory->folderSort, NULL);
    if (result != directory->entries) MemCopy(directory->entries, result, directory->count * sizeof(Prog_Entry));
    ReleaseScratch(scratch);
    for (u64 i = 0; i < directory->count; ++i) {
        String name = directory->entries[i].file.name;
        if (state->hasFocus && StrIsEqual(name, focus)) state->focusedRow = i;
        if (state->hasAnchor && StrIsEqual(name, anchor)) state->anchorRow = i;
    }
    state->hasHoverRow = false;
    // Measure results for the old order are matched by name.
    ++state->listing;
    Prog_ApplyFilter(state, false);
}

// Whether the start of a file reads as text: no NUL bytes, and nearly all of it UTF-8; the end may cut a character.
// The archives the inspector lists by their extension; the system is asked only for these, since it is slow to say no.
#define Prog_PreviewArchiveItems 50000
function b32 Prog__EqualFolded(String a, String b);

function b32 Prog__IsArchive(String path)
{
    local_persist const String extensions[] = { SInit(".zip"), SInit(".7z"), SInit(".rar"), SInit(".tar"), SInit(".tgz"), SInit(".gz"), SInit(".bz2"), SInit(".xz"), SInit(".zst"), SInit(".cab"), SInit(".jar") };
    for (u32 i = 0; i < CArrayCount(extensions); ++i) {
        String tail = path.length >= extensions[i].length ? StrFrom(path, path.length - extensions[i].length) : (String){0};
        if (Prog__EqualFolded(tail, extensions[i])) return true;
    }
    return false;
}

// Whether the file named `name` is an archive that opens as a folder.
function b32 Prog_IsArchiveName(String name)
{
    local_persist const String endings[] = {
        SInit(".zip"), SInit(".jar"), SInit(".7z"), SInit(".rar"), SInit(".tar"), SInit(".tgz"), SInit(".tar.gz"), SInit(".tbz2"), SInit(".tar.bz2"), SInit(".txz"),
        SInit(".tar.xz"), SInit(".tar.zst"), SInit(".cab"),
    };
    for (u32 i = 0; i < CArrayCount(endings); ++i) {
        if (name.length > endings[i].length && Prog__EqualFolded(StrFrom(name, name.length - endings[i].length), endings[i])) return true;
    }
    return false;
}

// Splits a path that goes into an archive, "C:/x/a.zip/docs", into the archive file and the path inside it, empty at its top; false for any
// other. Only the names are read, so a folder merely named like an archive is told apart by Prog_InArchive, which looks at the disk.
function b32 Prog_ArchiveSplit(String path, String *archive, String *inner)
{
    for (u64 at = 0; at < path.length;) {
        u64 end = at + StrIndexChar(StrFrom(path, at), '/', SSF_CountOnFail).value;
        if (end > at && Prog_IsArchiveName(StrFromTo(path, at, (i64)end))) {
            *archive = StrFromTo(path, 0, (i64)end);
            *inner = StrFrom(path, end + 1);
            while (inner->length && inner->value[inner->length - 1] == '/') --inner->length;
            return true;
        }
        at = end + 1;
    }
    return false;
}

// Prog_ArchiveSplit where the archive is a file rather than a folder of that name. Reads the disk, so for jobs.
function b32 Prog_InArchive(String path, String *archive, String *inner)
{
    return Prog_ArchiveSplit(path, archive, inner) && P_PathExists(*archive) && !P_IsDirectory(*archive);
}

// The folder a panel watches for its listing to change: the shown one, or for a folder inside an archive the one the archive is in, where it
// changes as the archive file is written.
function String Prog__WatchedFolder(Prog_State *state)
{
    String archive = {0}, inner = {0};
    return state->directory.archive && Prog_ArchiveSplit(state->directory.path, &archive, &inner) ? Prog_ParentPath(archive) : state->directory.path;
}

// A copy on disk of the file at `path` inside an archive, taken out into this run's folder the first time and kept for the next; empty for
// a file that is not in one or does not come out. Each archive folder gets a folder of its own there, named by a hash of its path.
// ponytail: a copy is kept while the archive changes under it; its modified time in the hash would take the change.
function String Prog_ArchiveCopy(Arena *arena, String path, P_Progress *progress, const P_CancelToken *cancel)
{
    String archive = {0}, inner = {0};
    if (!Prog_InArchive(path, &archive, &inner) || !inner.length) return (String){0};
    String run = P_RunFolder(arena);
    if (!run.length) return (String){0};
    String name = Prog_EntryLeaf(inner), within = Prog_EntryParent(inner), folder = StrFormat(arena, "%S/%S", archive, within);
    u64 hash = UINT64_C(14695981039346656037);
    for (u64 i = 0; i < folder.length; ++i) hash = (hash ^ (u8)folder.value[i]) * UINT64_C(1099511628211);
    String hex = StrFormat(arena, "%016" PRIx64, hash), into = Prog_JoinPath(arena, run, hex), copy = Prog_JoinPath(arena, into, name);
    if (P_PathExists(copy)) return copy;
    String made = {0};
    if (!P_IsDirectory(into) && (P_CreateItem(arena, run, hex, true, &made).code || !StrIsEqual(made, hex))) return (String){0};
    return !P_ExtractArchive(arena, archive, within, &name, 1, into, progress, cancel).code && P_PathExists(copy) ? copy : (String){0};
}

// Copies or moves as P_TransferItems does, and copies into and out of archives: out of one by taking the items out, into one by adding
// them, where items of another archive go by way of this run's folder. A move into or out of an archive copies, so that the archive and the
// files it was made from both stay whole.
function P_FileError Prog__Transfer(Arena *arena, const Prog_JobInput *input, ArrayStr *placed, const P_CancelToken *cancel)
{
    String toArchive = {0}, toInner = {0}, fromArchive = {0}, fromInner = {0};
    b32 into = Prog_InArchive(input->path, &toArchive, &toInner);
    b32 outOf = input->pathCount && Prog_InArchive(Prog_ParentPath(input->paths[0]), &fromArchive, &fromInner);
    if (!into && !outOf) return P_TransferItems(arena, input->paths, input->pathCount, input->path, input->move, input->progress, placed, cancel);
    Scratch scratch = GetScratchEx(arena);
    String *names = ArenaPushArray(scratch.arena, Max(input->pathCount, (u64)1), String);
    for (u64 i = 0; i < input->pathCount; ++i) names[i] = Prog_EntryLeaf(input->paths[i]);
    P_FileError result = {0};
    if (!into) {
        result = P_ExtractArchive(arena, fromArchive, fromInner, names, input->pathCount, input->path, input->progress, cancel);
    } else {
        const String *sources = input->paths;
        if (outOf) {
            String run = P_RunFolder(scratch.arena), made = {0};
            result = run.length ? P_CreateItem(scratch.arena, run, S("transfer"), true, &made) : (P_FileError){ .code = 1, .message = S("No temporary folder to take the items out into") };
            String through = Prog_JoinPath(scratch.arena, run, made);
            if (!result.code) result = P_ExtractArchive(arena, fromArchive, fromInner, names, input->pathCount, through, input->progress, cancel);
            String *taken = ArenaPushArray(scratch.arena, Max(input->pathCount, (u64)1), String);
            for (u64 i = 0; i < input->pathCount; ++i) taken[i] = Prog_JoinPath(scratch.arena, through, names[i]);
            sources = taken;
        }
        if (!result.code) result = P_AddToArchive(arena, toArchive, toInner, sources, input->pathCount, input->progress, cancel);
    }
    ReleaseScratch(scratch);
    return result;
}

// What an archive holds, a line each in its order: the size right aligned, then the path inside it, a folder's ending in '/'; first a line
// with the counts and the total size, and `cut` adds a last line saying there is more.
// What the items of an archive come to: its folders, with entries of their own or only in the paths of what they hold, each once, then its
// files and their size.
function P_DirectoryTotals Prog_ArchiveTotals(const P_FileArray *items)
{
    P_DirectoryTotals result = { .complete = true };
    Scratch scratch = GetScratch();
    struct { MapHeader header; struct { String key; u64 value; } *v; } folders = {0};
    for (u64 i = 0; i < items->header.count; ++i) {
        String name = items->v[i].name;
        b32 folder = items->v[i].kind == P_FileKind_Directory;
        if (!folder) ++result.files, result.bytes += items->v[i].size;
        for (u64 at = 0; at <= name.length; ++at) {
            if (at < name.length ? name.value[at] != '/' : !folder) continue;
            String above = StrFromTo(name, 0, (i64)at);
            if (MapsIndex(folders, above).exists) continue;
            MapsPut(scratch.arena, folders, above, 1);
            ++result.folders;
        }
    }
    ReleaseScratch(scratch);
    return result;
}

// Whether the inspector lists `entry`, a row of the current panel, as a folder because it is an archive. One inside an archive, which the
// panel cannot go into, and every file while the inspector shows bytes are previewed as files.
function b32 Prog_PreviewsArchive(const Prog_State *state, const Prog_Entry *entry)
{
    return entry->file.kind != P_FileKind_Directory && Prog_IsArchiveName(entry->file.name) && !state->directory.archive && !state->inspectorHex;
}

function String Prog_ArchiveListing(Arena *arena, const P_FileArray *items, b32 cut)
{
    P_DirectoryTotals totals = Prog_ArchiveTotals(items);
    u64 folders = totals.folders, files = totals.files, bytes = totals.bytes;
    StrBuilder text = {0};
    StrBuildStr(arena, &text, StrFormat(arena, "%" PRIu64 " %s, %" PRIu64 " %s, %S\n\n", folders, folders == 1 ? "folder" : "folders", files,
                                        files == 1 ? "file" : "files", Prog_FormatSize(arena, bytes)));
    for (u64 i = 0; i < items->header.count; ++i) {
        const P_File *item = &items->v[i];
        b32 folder = item->kind == P_FileKind_Directory;
        String size = folder ? (String){0} : Prog_FormatSize(arena, item->size);
        for (u64 pad = size.length; pad < 10; ++pad) StrBuildChar(arena, &text, ' ');
        StrBuildStr(arena, &text, StrFormat(arena, "%S  %S%s\n", size, item->name, folder ? "/" : ""));
    }
    if (cut) StrBuildStr(arena, &text, S("...\n"));
    return text.buffer;
}

typedef struct Prog__SyntaxDesc {
    String comment, comment2;      // to the end of the line
    String blockStart, blockEnd;   // comments over several lines
    String keywords;               // separated and surrounded by spaces
} Prog__SyntaxDesc;

global const Prog__SyntaxDesc Prog__Syntaxes[Prog_Syntax_Count] = {
    // Batch keywords are matched lowercased, as the language ignores case.
    [Prog_Syntax_Batch] = { {0}, {0}, {0}, {0},
        SInit(" if else for in do goto call exit set setlocal endlocal echo not exist defined errorlevel equ neq lss leq gtr geq shift pushd popd cd chdir "
          "start pause cls title mkdir md rmdir rd del erase copy move ren rename type choice timeout nul on off enabledelayedexpansion enableextensions ") },
    [Prog_Syntax_C] = { SInit("//"), {0}, SInit("/*"), SInit("*/"),
        SInit(" alignas alignof auto bool break case catch char char8_t char16_t char32_t class const consteval constexpr constinit const_cast continue co_await co_return "
          "co_yield decltype default delete do double dynamic_cast else enum explicit export extern false final float for friend goto if inline int long mutable "
          "namespace new noexcept nullptr operator override private protected public register reinterpret_cast restrict return short signed sizeof static "
          "static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile "
          "wchar_t while _Alignas _Alignof _Atomic _Bool _Generic _Noreturn _Static_assert _Thread_local UCLASS USTRUCT UENUM UFUNCTION UPROPERTY UINTERFACE "
          "UMETA UPARAM GENERATED_BODY GENERATED_UCLASS_BODY GENERATED_USTRUCT_BODY ") },
    [Prog_Syntax_CSharp] = { SInit("//"), {0}, SInit("/*"), SInit("*/"),
        SInit(" abstract as async await base bool break byte case catch char checked class const continue decimal default delegate do double else enum event "
          "explicit extern false finally fixed float for foreach get goto if implicit in init int interface internal is lock long namespace new null object "
          "operator out override params private protected public readonly record ref return sbyte sealed set short sizeof stackalloc static string struct "
          "switch this throw true try typeof uint ulong unchecked unsafe ushort using value var virtual void volatile while yield ") },
    [Prog_Syntax_Script] = { SInit("//"), {0}, SInit("/*"), SInit("*/"),
        SInit(" abstract any as async await boolean break case catch class const continue debugger declare default delete do else enum export extends false "
          "finally for from function get if implements import in instanceof interface keyof let namespace never new null number of private protected public "
          "readonly return set static string super switch this throw true try type typeof undefined unknown var void while with yield ") },
    [Prog_Syntax_Rust] = { SInit("//"), {0}, SInit("/*"), SInit("*/"),
        SInit(" as async await bool break char const continue crate dyn else enum extern f32 f64 false fn for i8 i16 i32 i64 i128 if impl in isize let loop match "
          "mod move mut pub ref return self Self static str struct super trait true type u8 u16 u32 u64 u128 unsafe use usize where while Some None Ok Err ") },
    [Prog_Syntax_Go] = { SInit("//"), {0}, SInit("/*"), SInit("*/"),
        SInit(" any bool break byte case chan const continue default defer else error fallthrough false float32 float64 for func go goto if import int int8 int16 "
          "int32 int64 interface iota map nil package range return rune select string struct switch true type uint uint8 uint16 uint32 uint64 uintptr var ") },
    [Prog_Syntax_Java] = { SInit("//"), {0}, SInit("/*"), SInit("*/"),
        SInit(" abstract assert boolean break byte case catch char class companion const continue data default do double else enum extends false final finally "
          "float for fun if implements import in instanceof int interface is lateinit long native new null object open override package private protected public "
          "return sealed short static super switch synchronized this throw throws transient true try val var void volatile when while ") },
    [Prog_Syntax_Python] = { SInit("#"), {0}, {0}, {0},
        SInit(" and as assert async await break case class continue def del elif else except False finally for from global if import in is lambda match None "
          "nonlocal not or pass raise return self True try while with yield ") },
    [Prog_Syntax_Lua] = { SInit("--"), {0}, SInit("--[["), SInit("]]"),
        SInit(" and break do else elseif end false for function goto if in local nil not or repeat return self then true until while ") },
    [Prog_Syntax_Shell] = { SInit("#"), {0}, {0}, {0},
        SInit(" case declare do done echo elif else esac exit export fi for function if in local readonly return select set source then until unset while ") },
    [Prog_Syntax_PowerShell] = { SInit("#"), {0}, SInit("<#"), SInit("#>"),
        SInit(" begin break catch class continue data do dynamicparam else elseif end exit filter finally for foreach function if in param process return "
          "switch throw trap try until using while ") },
    [Prog_Syntax_Json] = { SInit("//"), {0}, SInit("/*"), SInit("*/"), SInit(" true false null ") },
    [Prog_Syntax_Ini] = { {0}, {0}, {0}, {0}, SInit(" true false yes no on off null ") },
    [Prog_Syntax_Markdown] = { {0}, {0}, {0}, {0}, {0} },
};

// The language of `path`, by its extension or, for a few, its whole name.
function Prog_Syntax Prog_SyntaxForPath(String path)
{
    String name = Prog_EntryLeaf(path);
    Index dot = StrIndexChar(name, '.', SSF_SearchBackwards);
    String extension = dot.exists ? StrFrom(name, dot.value + 1) : (String){0};
    char lower[16];
    if (!extension.length || extension.length > sizeof(lower)) return Prog_Syntax_None;
    for (u64 i = 0; i < extension.length; ++i) lower[i] = extension.value[i] >= 'A' && extension.value[i] <= 'Z' ? (char)(extension.value[i] + 32) : extension.value[i];
    String e = StrMake(lower, extension.length);
    local_persist const struct { const char *extensions; Prog_Syntax syntax; } table[] = {
        { " c h cpp cc cxx c++ hpp hh hxx h++ inl ipp ino m mm cu cuh glsl vert frag geom comp tesc tese hlsl hlsli fx fxh usf ush metal ", Prog_Syntax_C },
        { " cs csx ", Prog_Syntax_CSharp },
        { " js mjs cjs jsx ts mts cts tsx ", Prog_Syntax_Script },
        { " rs ", Prog_Syntax_Rust },
        { " go ", Prog_Syntax_Go },
        { " java kt kts gradle scala ", Prog_Syntax_Java },
        { " py pyw pyi ", Prog_Syntax_Python },
        { " lua ", Prog_Syntax_Lua },
        { " sh bash zsh fish ksh ", Prog_Syntax_Shell },
        { " ps1 psm1 psd1 ", Prog_Syntax_PowerShell },
        { " bat cmd ", Prog_Syntax_Batch },
        { " json jsonc json5 uproject uplugin code-workspace ", Prog_Syntax_Json },
        { " ini toml cfg conf properties yaml yml editorconfig gitignore gitattributes gitconfig gitmodules desktop ", Prog_Syntax_Ini },
        { " md markdown ", Prog_Syntax_Markdown },
    };
    for (u64 i = 0; i < CArrayCount(table); ++i) {
        String list = StrFromCStr(table[i].extensions);
        for (u64 at = 0; at + e.length + 2 <= list.length; ++at) {
            if (list.value[at] == ' ' && list.value[at + e.length + 1] == ' ' && StrIsEqual(StrFromTo(list, at + 1, (i64)(at + 1 + e.length)), e)) return table[i].syntax;
        }
    }
    return Prog_Syntax_None;
}

function b32 Prog__IdentChar(u8 c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

function b32 Prog__StartsAt(String line, u64 at, String with)
{
    return with.length && at + with.length <= line.length && StrIsEqual(StrFromTo(line, at, (i64)(at + with.length)), with);
}

function void Prog__MarkTokens(u8 *classes, u64 from, u64 to, Prog_Token kind)
{
    if (classes) for (u64 i = from; i < to; ++i) classes[i] = (u8)kind;
}

// What a line can leave open for the next ones: a comment over several lines, Python's triple-quoted strings, a Markdown code fence.
enum { Prog__Open_None, Prog__Open_Comment, Prog__Open_Triple2, Prog__Open_Triple1, Prog__Open_Fence };

// Marks what each byte of `line` is in `syntax` into `classes` when it is not NULL (a byte each), starting inside what `open` says the lines
// before left open; returns what this one leaves open. A line at a time, so that only the lines in sight are worked out, after one pass that
// keeps only what each line starts inside of.
function u8 Prog_SyntaxLine(Prog_Syntax syntax, String line, u8 open, u8 *classes)
{
    Prog__MarkTokens(classes, 0, line.length, Prog_Token_Plain);
    if (syntax <= Prog_Syntax_None || syntax >= Prog_Syntax_Count) return Prog__Open_None;
    const Prog__SyntaxDesc *desc = &Prog__Syntaxes[syntax];
    const u8 *t = (const u8 *)line.value;
    u64 n = line.length, lead = 0;
    while (lead < n && (t[lead] == ' ' || t[lead] == '	')) ++lead;
    if (syntax == Prog_Syntax_Markdown) {
        b32 fence = Prog__StartsAt(line, lead, S("```")) || Prog__StartsAt(line, lead, S("~~~"));
        if (open == Prog__Open_Fence || fence) {
            Prog__MarkTokens(classes, 0, n, Prog_Token_String);
            return open == Prog__Open_Fence ? (fence ? Prog__Open_None : Prog__Open_Fence) : Prog__Open_Fence;
        }
        if (lead < n && t[lead] == '#') {
            Prog__MarkTokens(classes, 0, n, Prog_Token_Heading);
            return Prog__Open_None;
        }
        // Inline code between backticks.
        for (u64 at = 0; at < n; ++at) {
            if (t[at] != '`') continue;
            u64 end = at + 1;
            while (end < n && t[end] != '`') ++end;
            if (end >= n) break;
            Prog__MarkTokens(classes, at, end + 1, Prog_Token_String);
            at = end;
        }
        return Prog__Open_None;
    }
    if (syntax == Prog_Syntax_Batch) {
        // REM and :: lines are comments, :name lines labels; a leading @ only keeps the line from being echoed.
        u64 start = lead < n && t[lead] == '@' ? lead + 1 : lead;
        b32 rem = start + 3 <= n && (t[start] | 0x20) == 'r' && (t[start + 1] | 0x20) == 'e' && (t[start + 2] | 0x20) == 'm' &&
                  (start + 3 == n || t[start + 3] == ' ' || t[start + 3] == '\t');
        if (rem || Prog__StartsAt(line, start, S("::"))) {
            Prog__MarkTokens(classes, 0, n, Prog_Token_Comment);
            return Prog__Open_None;
        }
        if (start < n && t[start] == ':') {
            Prog__MarkTokens(classes, 0, n, Prog_Token_Heading);
            return Prog__Open_None;
        }
        for (u64 at = 0; at < n;) {
            u8 c = t[at];
            u64 end = at + 1;
            Prog_Token kind = Prog_Token_Plain;
            if (c == '"') {
                while (end < n && t[end] != '"') ++end;
                end = Min(end + 1, n);
                kind = Prog_Token_String;
            } else if (c == '%' && end < n) {
                // %% before a loop's letter, %1, %* and %~dp0 for arguments, %name% for a variable; a lone % is plain.
                if (t[end] == '%') end = Min(end + 2, n);
                else if ((t[end] >= '0' && t[end] <= '9') || t[end] == '*') end += 1;
                else if (t[end] == '~') {
                    for (++end; end < n && Prog__IdentChar(t[end]) && !(t[end] >= '0' && t[end] <= '9'); ++end) {}
                    end = Min(end + 1, n);
                } else {
                    while (end < n && t[end] != '%' && t[end] != ' ') ++end;
                    end = end < n && t[end] == '%' ? end + 1 : at + 1;
                }
                kind = end > at + 1 ? Prog_Token_Number : Prog_Token_Plain;
            } else if (c == '!') {
                while (end < n && t[end] != '!' && t[end] != ' ') ++end;
                if (end < n && t[end] == '!' && end > at + 1) kind = Prog_Token_Number, ++end;
                else end = at + 1;
            } else if (Prog__IdentChar(c)) {
                while (end < n && Prog__IdentChar(t[end])) ++end;
                char word[34];
                if (classes && end - at < 32) {
                    word[0] = ' ';
                    for (u64 i = at; i < end; ++i) word[i - at + 1] = (char)(t[i] >= 'A' && t[i] <= 'Z' ? t[i] + 32 : t[i]);
                    word[end - at + 1] = ' ';
                    if (StrIndexStr(desc->keywords, StrMake(word, end - at + 2), SSF_None).exists) kind = Prog_Token_Keyword;
                }
            }
            Prog__MarkTokens(classes, at, end, kind);
            at = end;
        }
        return Prog__Open_None;
    }
    u64 at = 0;
    if (syntax == Prog_Syntax_Ini) {
        if (lead < n && (t[lead] == '#' || t[lead] == ';')) {
            Prog__MarkTokens(classes, 0, n, Prog_Token_Comment);
            return Prog__Open_None;
        }
        if (lead < n && t[lead] == '[') {
            Prog__MarkTokens(classes, 0, n, Prog_Token_Heading);
            return Prog__Open_None;
        }
        // The key is plain; what it is set to is read like code.
        while (at < n && t[at] != '=' && t[at] != ':') ++at;
    }
    b32 cLike = syntax == Prog_Syntax_C || syntax == Prog_Syntax_CSharp || syntax == Prog_Syntax_Script || syntax == Prog_Syntax_Rust ||
                syntax == Prog_Syntax_Go || syntax == Prog_Syntax_Java;
    while (at < n) {
        if (open == Prog__Open_Comment || open == Prog__Open_Triple2 || open == Prog__Open_Triple1) {
            String end = open == Prog__Open_Comment ? desc->blockEnd : open == Prog__Open_Triple2 ? S("\"\"\"") : S("'''");
            u64 stop = at;
            while (stop < n && !Prog__StartsAt(line, stop, end)) ++stop;
            b32 closed = stop < n;
            stop = closed ? stop + end.length : n;
            Prog__MarkTokens(classes, at, stop, open == Prog__Open_Comment ? Prog_Token_Comment : Prog_Token_String);
            at = stop;
            if (closed) open = Prog__Open_None;
            continue;
        }
        u8 c = t[at];
        if (Prog__StartsAt(line, at, desc->blockStart)) {
            Prog__MarkTokens(classes, at, at + desc->blockStart.length, Prog_Token_Comment);
            at += desc->blockStart.length;
            open = Prog__Open_Comment;
            continue;
        }
        // A shell's # starts a comment only as a word of its own, not in $# or ${#name}.
        b32 wordStart = at == 0 || t[at - 1] == ' ' || t[at - 1] == '\t';
        if ((Prog__StartsAt(line, at, desc->comment) || Prog__StartsAt(line, at, desc->comment2)) &&
            (wordStart || (syntax != Prog_Syntax_Shell && syntax != Prog_Syntax_PowerShell))) {
            Prog__MarkTokens(classes, at, n, Prog_Token_Comment);
            break;
        }
        if (syntax == Prog_Syntax_Python && (Prog__StartsAt(line, at, S("\"\"\"")) || Prog__StartsAt(line, at, S("'''")))) {
            Prog__MarkTokens(classes, at, at + 3, Prog_Token_String);
            open = c == '"' ? Prog__Open_Triple2 : Prog__Open_Triple1;
            at += 3;
            continue;
        }
        if (c == '"' || c == '\'' || (c == '`' && syntax == Prog_Syntax_Script)) {
            u64 end = at + 1;
            b32 escapes = !(c == '\'' && (syntax == Prog_Syntax_Shell || syntax == Prog_Syntax_PowerShell));
            while (end < n && t[end] != c) end += escapes && t[end] == '\\' ? 2 : 1;
            // In C-like languages a quote starts a string only as a character: one byte, an escape or one character in UTF-8, not a Rust lifetime.
            b32 character = end < n && (end - at == 2 || (t[at + 1] == '\\' && end - at <= 12) || (t[at + 1] >= 0x80 && end - at <= 5));
            if (c == '\'' && cLike && !character) {
                ++at;
                continue;
            }
            end = Min(end + 1, n);
            Prog_Token kind = Prog_Token_String;
            // A JSON key is the string before a colon.
            if (syntax == Prog_Syntax_Json) {
                u64 next = end;
                while (next < n && (t[next] == ' ' || t[next] == '\t')) ++next;
                if (next < n && t[next] == ':') kind = Prog_Token_Keyword;
            }
            Prog__MarkTokens(classes, at, end, kind);
            at = end;
            continue;
        }
        if (((c >= '0' && c <= '9') || (c == '.' && at + 1 < n && t[at + 1] >= '0' && t[at + 1] <= '9')) && (at == 0 || !Prog__IdentChar(t[at - 1]))) {
            u64 end = at + 1;
            while (end < n && (Prog__IdentChar(t[end]) || t[end] == '.' || (t[end] == '\'' && cLike))) ++end;
            Prog__MarkTokens(classes, at, end, Prog_Token_Number);
            at = end;
            continue;
        }
        // A preprocessor line's directive, and an include's <path> as a string.
        if (c == '#' && at == lead && (syntax == Prog_Syntax_C || syntax == Prog_Syntax_CSharp)) {
            u64 end = at + 1;
            while (end < n && (t[end] == ' ' || t[end] == '\t')) ++end;
            while (end < n && Prog__IdentChar(t[end])) ++end;
            Prog__MarkTokens(classes, at, end, Prog_Token_Keyword);
            at = end;
            while (at < n && (t[at] == ' ' || t[at] == '\t')) ++at;
            if (at < n && t[at] == '<') {
                for (end = at + 1; end < n && t[end] != '>'; ++end) {}
                end = Min(end + 1, n);
                Prog__MarkTokens(classes, at, end, Prog_Token_String);
                at = end;
            }
            continue;
        }
        if (Prog__IdentChar(c)) {
            u64 end = at + 1;
            while (end < n && Prog__IdentChar(t[end])) ++end;
            if (classes && desc->keywords.length && end - at < 32) {
                char word[34];
                word[0] = ' ';
                MemCopy(word + 1, t + at, end - at);
                word[end - at + 1] = ' ';
                String needle = StrMake(word, end - at + 2), list = desc->keywords;
                for (u64 i = 0; i + needle.length <= list.length; ++i) {
                    if (list.value[i] == ' ' && StrIsEqual(StrFromTo(list, i, (i64)(i + needle.length)), needle)) {
                        Prog__MarkTokens(classes, at, end, Prog_Token_Keyword);
                        break;
                    }
                }
            }
            at = end;
            continue;
        }
        ++at;
    }
    return open;
}

function b32 Prog__TakeNumber(String *text, u64 *value);

// The branch of a HEAD file's `head`, "ref: refs/heads/main", or the first seven digits of a detached one's hash.
function String Prog_GitBranch(String head)
{
    while (head.length && (head.value[head.length - 1] == '\n' || head.value[head.length - 1] == '\r' || head.value[head.length - 1] == ' ')) --head.length;
    if (Prog__StartsAt(head, 0, S("ref: refs/heads/"))) return StrFrom(head, 16);
    if (Prog__StartsAt(head, 0, S("ref: "))) return StrFrom(head, 5);
    return StrFromTo(head, 0, (i64)Min(head.length, (u64)7));
}

// Marks `name` and the folders on its way, each keeping the most telling of the marks it gets.
function void Prog__GitMark(Arena *arena, Prog_GitMarks *marks, String name, Prog_GitMark mark)
{
    for (u64 at = 0; at <= name.length; ++at) {
        if (at < name.length && name.value[at] != '/') continue;
        String part = StrFromTo(name, 0, (i64)at);
        if (part.length && mark > (Prog_GitMark)MapsGet(*marks, u8, part)) MapsPut(arena, *marks, StrCopy(arena, part), (u8)mark);
    }
}

// Reads `git status --porcelain=v1 -z` of `folder`, whose paths start at the repository's top, `root`: what it counts and marks inside `folder`.
function void Prog_ReadGitChanges(Arena *arena, String root, String folder, String text, Prog_GitStatus *status)
{
    String inside = folder.length > root.length ? StrFrom(folder, root.length + (root.value[root.length - 1] == '/' ? 0 : 1)) : (String){0};
    ArrayStr records = StrSplit(arena, text, '\0');
    for (u64 i = 0; i < records.header.count; ++i) {
        String record = records.v[i];
        if (record.length < 4) continue;
        u8 x = (u8)record.value[0], y = (u8)record.value[1];
        String path = StrFrom(record, 3);
        // A rename or copy names where it came from next.
        if (x == 'R' || x == 'C') ++i;
        b32 conflict = x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
        Prog_GitMark mark = conflict ? Prog_GitMark_Conflict : x == '?' ? Prog_GitMark_Untracked : x == 'A' || x == 'R' || x == 'C' ? Prog_GitMark_Added : Prog_GitMark_Modified;
        ++status->changes;
        ++status->counts[mark];
        while (path.length && path.value[path.length - 1] == '/') --path.length;
        if (inside.length) {
            if (path.length <= inside.length || !Prog__StartsAt(path, 0, inside) || path.value[inside.length] != '/') continue;
            path = StrFrom(path, inside.length + 1);
        }
        // A deleted file is no longer listed; the folders it was in are.
        if (x == 'D' || y == 'D') {
            Index slash = StrIndexChar(path, '/', SSF_SearchBackwards);
            if (!slash.exists) continue;
            path = StrFromTo(path, 0, (i64)slash.value);
            mark = Prog_GitMark_Modified;
        }
        Prog__GitMark(arena, &status->marks, path, mark);
    }
}

// The repository around `folder`: the nearest folder, it or one above it, holding .git, a folder or, for a worktree, a file naming one.
// The counts and changes come from git itself, which may be missing; the branch is read from HEAD either way.
// Reads the HEAD of a repository whose top is `at`, holding .git, a folder or, for a worktree, a file naming one.
function b32 Prog__ReadGitHead(Arena *arena, String at, String *head)
{
    String dotGit = Prog_JoinPath(arena, at, S(".git")), link = {0};
    b32 found = P_ReadFileStart(arena, Prog_JoinPath(arena, dotGit, S("HEAD")), 512, head);
    if (!found && P_ReadFileStart(arena, dotGit, 1024, &link) && Prog__StartsAt(link, 0, S("gitdir: "))) {
        String gitDir = StrFrom(link, 8);
        while (gitDir.length && (gitDir.value[gitDir.length - 1] == '\n' || gitDir.value[gitDir.length - 1] == '\r')) --gitDir.length;
        b32 absolute = (gitDir.length && gitDir.value[0] == '/') || (gitDir.length > 1 && gitDir.value[1] == ':');
        found = P_ReadFileStart(arena, Prog_JoinPath(arena, absolute ? gitDir : Prog_JoinPath(arena, at, gitDir), S("HEAD")), 512, head);
    }
    return found;
}

function Prog_GitStatus Prog_FindGitStatus(Arena *arena, String folder)
{
    Prog_GitStatus result = { .ahead = -1, .behind = -1 };
    if (!folder.length || StrIsEqual(folder, Prog_ThisPC)) return result;
    for (String at = folder; at.length && !StrIsEqual(at, Prog_ThisPC); at = Prog_UpPath(at)) {
        String head = {0};
        if (!Prog__ReadGitHead(arena, at, &head)) continue;
        result.branch = Prog_GitBranch(head);
        String arguments[] = { S("git"), S("rev-list"), S("--left-right"), S("--count"), S("HEAD...@{upstream}") };
        String counts = {0};
        if (P_RunCommand(arena, folder, arguments, CArrayCount(arguments), 3000, &counts)) {
            u64 ahead = 0, behind = 0;
            String rest = counts;
            if (Prog__TakeNumber(&rest, &ahead) && Prog__TakeNumber(&rest, &behind)) {
                result.ahead = (i32)Min(ahead, (u64)INT32_MAX);
                result.behind = (i32)Min(behind, (u64)INT32_MAX);
            }
        }
        String status[] = { S("git"), S("status"), S("--porcelain=v1"), S("-z"), S("--untracked-files=normal") };
        String changes = {0};
        if (P_RunCommand(arena, folder, status, CArrayCount(status), 5000, &changes)) Prog_ReadGitChanges(arena, at, folder, changes, &result);
        break;
    }
    return result;
}

// A hex row: the offset in `places` hex digits and two spaces, three characters a byte with one more space after the eighth, then the
// bytes as characters, printable ones as they are and others as dots.
function u64 Prog_HexByteColumn(u32 places, u64 i)
{
    return places + 2 + i * 3 + (i >= 8 ? 1 : 0);
}

function u64 Prog_HexCharColumn(u32 places, u32 perLine, u64 i)
{
    return places + 2 + (u64)perLine * 3 + Max(perLine / 8, 1u) + i;
}

// As few hex digits as the last offset of a file of `size` needs, but at least four.
function u32 Prog_HexPlaces(u64 size)
{
    u32 places = 4;
    while (places < 16 && (size ? size - 1 : 0) >> (4 * places)) ++places;
    return places;
}

// The most bytes a row holds, 16, 8 or 4, that fit in `cells` characters.
function u32 Prog_HexPerLine(u32 places, f32 cells)
{
    for (u32 perLine = 16; perLine > 4; perLine /= 2) {
        if ((f32)Prog_HexCharColumn(places, perLine, perLine) <= cells) return perLine;
    }
    return 4;
}

// The row of the `count` bytes at `bytes`, which start `offset` bytes into the file.
function String Prog_HexRowText(Arena *arena, const u8 *bytes, u64 count, u64 offset, u32 perLine, u32 places)
{
    local_persist const char digits[] = "0123456789ABCDEF";
    u64 length = Prog_HexCharColumn(places, perLine, count);
    char *text = ArenaPushArrayNoZero(arena, Max(length, (u64)1), char);
    for (u64 i = 0; i < length; ++i) text[i] = ' ';
    for (u32 i = 0; i < places; ++i) text[i] = digits[(offset >> (4 * (places - 1 - i))) & 15];
    for (u64 i = 0; i < count; ++i) {
        u64 column = Prog_HexByteColumn(places, i);
        text[column] = digits[bytes[i] >> 4];
        text[column + 1] = digits[bytes[i] & 15];
        text[Prog_HexCharColumn(places, perLine, i)] = bytes[i] >= 0x20 && bytes[i] < 0x7F ? (char)bytes[i] : '.';
    }
    return StrMake(text, length);
}

function b32 Prog__LooksLikeText(String start)
{
    if (!start.length) return false;
    u64 runes = 0, invalid = 0;
    u64 end = start.length > 4 ? start.length - 4 : start.length;
    for (u64 at = 0; at < end;) {
        if (start.value[at] == 0) return false;
        invalid += StrDecodeUtf8(start, &at) == 0xFFFD;
        ++runes;
    }
    return invalid * 50 <= runes;
}

// A walk through a whole tree: the folders still to list, by their path below the listed one, and what each lane has found.
typedef struct Prog__Walk {
    Prog_JobInput *input;
    const P_CancelToken *cancel;
    P_Mutex mutex;
    P_Condition condition;
    Arena arena;  // `pending`, which only grows; the names in it live in the lanes' arenas
    ArrayStr pending;
    u32 busy;     // lanes listing a folder, which can still add to `pending`
    struct { Arena arena; P_FileArray found; } lanes[Prog_WalkLanes];  // `found` grows under the mutex, so that the first lane can read it
    // The listing so far is handed over now and then: its own folder's items, the result it fills in, and when it is next due.
    P_FileArray top;
    const Prog_Directory *base;
    u64 nextPartial;
} Prog__Walk;

function void Prog__CountFound(Prog_JobInput *input, const P_File *files, u64 count);
function void Prog__FillListing(Arena *arena, const Prog_JobInput *input, P_FileArray files, Prog_Directory *result, const P_CancelToken *cancel);

// The rows found so far, as a listing of their own handed to the window. What each lane has found is read under the lock; an array that grows
// afterwards moves on and leaves the part read here in place.
function void Prog__WalkPartial(Prog__Walk *walk)
{
    u64 started = PlatformNowMicroseconds();
    P_File *found[Prog_WalkLanes];
    u64 counts[Prog_WalkLanes], total = walk->top.header.count;
    P_MutexLock(&walk->mutex);
    for (u32 i = 0; i < Prog_WalkLanes; ++i) {
        found[i] = walk->lanes[i].found.v;
        counts[i] = walk->lanes[i].found.header.count;
        total += counts[i];
    }
    P_MutexUnlock(&walk->mutex);
    Arena arena = {0};
    P_FileArray all = {0};
    ArrayReserve(&arena, all, Max(total, (u64)1));
    for (u64 i = 0; i < walk->top.header.count; ++i) {
        P_File file = walk->top.v[i];
        file.name = StrCopy(&arena, file.name);
        ArrayPush(&arena, all, file);
    }
    for (u32 lane = 0; lane < Prog_WalkLanes; ++lane) {
        for (u64 i = 0; i < counts[lane]; ++i) {
            P_File file = found[lane][i];
            file.name = StrCopy(&arena, file.name);
            ArrayPush(&arena, all, file);
        }
    }
    Prog_Directory listed = *walk->base;
    listed.path = StrCopy(&arena, walk->base->path);
    Prog__FillListing(&arena, walk->input, all, &listed, walk->cancel);
    Prog_Partial *partial = walk->input->partial;
    P_MutexLock(&partial->mutex);
    if (partial->ready) ArenaRelease(&partial->arena);
    partial->arena = arena;
    partial->directory = listed;
    partial->ready = !listed.cancelled;
    if (listed.cancelled) ArenaRelease(&partial->arena);
    P_MutexUnlock(&partial->mutex);
    // Each one takes longer than the last, so they come further apart as the tree grows, and the walk keeps most of the time.
    u64 now = PlatformNowMicroseconds();
    walk->nextPartial = now + Max(UINT64_C(250000), 3 * (now - started));
}

function void Prog__WalkLane(void *user, u32 index)
{
    Prog__Walk *walk = user;
    Arena *arena = &walk->lanes[index].arena;
    for (;;) {
        P_MutexLock(&walk->mutex);
        // Waits while the stack is empty but a lane that is listing may still fill it.
        while (!walk->pending.header.count && walk->busy && !P_IsCancelled(walk->cancel)) P_ConditionWait(&walk->condition, &walk->mutex);
        if (!walk->pending.header.count || P_IsCancelled(walk->cancel)) {
            P_ConditionBroadcast(&walk->condition);
            P_MutexUnlock(&walk->mutex);
            break;
        }
        String folder = walk->pending.v[--walk->pending.header.count];
        ++walk->busy;
        P_MutexUnlock(&walk->mutex);
        P_DirectoryResult inside = {0};
        Scratch scratch = GetScratchEx(arena);
        ProfileScope("WalkList") inside = P_EnumerateDirectory(scratch.arena, Prog_JoinPath(scratch.arena, walk->input->path, folder), walk->cancel);
        ArrayStr below = {0};
        for (u64 i = 0; i < inside.files.header.count; ++i) {
            P_File *file = &inside.files.v[i];
            file->name = StrFormat(arena, "%S/%S", folder, file->name);
            if (file->kind == P_FileKind_Directory && !file->symbolicLink && (!file->hidden || walk->input->showHidden)) ArrayPush(scratch.arena, below, file->name);
        }
        Prog__CountFound(walk->input, inside.files.v, inside.files.header.count);
        P_MutexLock(&walk->mutex);
        for (u64 i = 0; i < inside.files.header.count; ++i) ArrayPush(arena, walk->lanes[index].found, inside.files.v[i]);
        for (u64 i = 0; i < below.header.count; ++i) ArrayPush(&walk->arena, walk->pending, below.v[i]);
        --walk->busy;
        P_ConditionBroadcast(&walk->condition);
        P_MutexUnlock(&walk->mutex);
        ReleaseScratch(scratch);
        if (index == 0 && walk->input->partial && PlatformNowMicroseconds() >= walk->nextPartial) Prog__WalkPartial(walk);
    }
}

// A slice of the drive index's walk: the lanes take the slice's folders in turn until its time is up, each listing into its own arena,
// so that every folder taken is listed and the ones listed are the first ones, in the order the index takes what they hold.
typedef struct Prog__IndexSlice {
    const Prog_JobInput *input;
    const P_CancelToken *cancel;
    u64 until;
    atomic_uint_fast64_t next;
    P_DirectoryResult *listings;
    Arena lanes[Prog_WalkLanes];
} Prog__IndexSlice;

function void Prog__IndexLane(void *user, u32 lane)
{
    Prog__IndexSlice *slice = user;
    while (PlatformNowMicroseconds() < slice->until && !P_IsCancelled(slice->cancel)) {
        u64 at = atomic_fetch_add(&slice->next, 1);
        if (at >= slice->input->pathCount) break;
        slice->listings[at] = P_EnumerateDirectory(&slice->lanes[lane], slice->input->paths[at], slice->cancel);
    }
}

// A measure of several folders' trees at once, sharing the lanes:
// each lane lists one folder at a time, adds what it holds to its tree's totals and leaves its subfolders for whichever lane is free next.
// Each tree keeps its own stack of folders to list, and the lanes take from the trees in turn, so that small trees finish while a big one is walked.
typedef struct Prog__Measure {
    Prog_TreeTotals *trees;
    const P_CancelToken *cancel;
    P_Mutex mutex;
    P_Condition condition;
    Arena arena;      // the stacks and the turn, whose paths live in the lanes' arenas
    ArrayStr *stacks; // by tree
    u32 *turn;        // the trees with folders on their stacks
    b8 *inTurn;
    b8 *started;      // its own folder has been taken, so later ones are its subfolders
    u32 turnCount, next;
    u64 waiting;      // folders on all the stacks
    u32 busy;
    Arena lanes[Prog_WalkLanes];
} Prog__Measure;

function void Prog__MeasureLane(void *user, u32 index)
{
    Prog__Measure *walk = user;
    Arena *arena = &walk->lanes[index];
    for (;;) {
        P_MutexLock(&walk->mutex);
        while (!walk->waiting && walk->busy && !P_IsCancelled(walk->cancel)) P_ConditionWait(&walk->condition, &walk->mutex);
        if (!walk->waiting || P_IsCancelled(walk->cancel)) {
            P_ConditionBroadcast(&walk->condition);
            P_MutexUnlock(&walk->mutex);
            break;
        }
        // The next tree in turn gives its newest folder; a tree whose stack empties leaves the turn until it gets more.
        walk->next %= walk->turnCount;
        u32 at = walk->next, which = walk->turn[at];
        ArrayStr *stack = &walk->stacks[which];
        String path = stack->v[--stack->header.count];
        --walk->waiting;
        if (stack->header.count) {
            ++walk->next;
        } else {
            walk->turn[at] = walk->turn[--walk->turnCount];
            walk->inTurn[which] = false;
        }
        b32 root = !walk->started[which];
        walk->started[which] = true;
        ++walk->busy;
        P_MutexUnlock(&walk->mutex);
        P_DirectoryTotals counted = {0};
        ArrayStr below = {0};
        b32 read = P_CountFolder(arena, path, &counted, &below);
        Prog_TreeTotals *tree = &walk->trees[which];
        if (!read && root) atomic_store(&tree->failed, true);
        atomic_fetch_add(&tree->folders, counted.folders);
        atomic_fetch_add(&tree->files, counted.files);
        atomic_fetch_add(&tree->bytes, counted.bytes);
        P_MutexLock(&walk->mutex);
        for (u64 i = 0; i < below.header.count; ++i) ArrayPush(&walk->arena, *stack, below.v[i]);
        if (below.header.count && !walk->inTurn[which]) {
            walk->turn[walk->turnCount++] = which;
            walk->inTurn[which] = true;
        }
        walk->waiting += below.header.count;
        // Its subfolders are left to list in its place.
        atomic_fetch_add(&tree->pending, below.header.count);
        atomic_fetch_sub(&tree->pending, 1);
        --walk->busy;
        P_ConditionBroadcast(&walk->condition);
        P_MutexUnlock(&walk->mutex);
    }
}

// Measures the trees below `paths` into `trees`, `count` of each, which start zeroed; returns false when cancelled.
function b32 Prog_MeasureTrees(const String *paths, u64 count, Prog_TreeTotals *trees, const P_CancelToken *cancel)
{
    Prog__Measure walk = { .trees = trees, .cancel = cancel };
    if (!P_MutexInit(&walk.mutex)) return false;
    if (!P_ConditionInit(&walk.condition)) {
        P_MutexRelease(&walk.mutex);
        return false;
    }
    u64 room = Max(count, (u64)1);
    walk.stacks = ArenaPushArray(&walk.arena, room, ArrayStr);
    walk.turn = ArenaPushArray(&walk.arena, room, u32);
    walk.inTurn = ArenaPushArray(&walk.arena, room, b8);
    walk.started = ArenaPushArray(&walk.arena, room, b8);
    for (u64 i = 0; i < count; ++i) {
        atomic_store(&trees[i].pending, 1);
        ArrayPush(&walk.arena, walk.stacks[i], paths[i]);
        walk.turn[walk.turnCount++] = (u32)i;
        walk.inTurn[i] = true;
    }
    walk.waiting = count;
    u32 lanes = Max(1u, Min(P_ProcessorCount(), (u32)Prog_WalkLanes));
    P_RunLanes(lanes, Prog__MeasureLane, &walk);
    for (u32 i = 0; i < lanes; ++i) ArenaRelease(&walk.lanes[i]);
    ArenaRelease(&walk.arena);
    P_ConditionRelease(&walk.condition);
    P_MutexRelease(&walk.mutex);
    return !P_IsCancelled(cancel);
}

// A tree's totals, complete when it was measured to the end.
function P_DirectoryTotals Prog_TreeTotalsTaken(Prog_TreeTotals *tree, b32 finished)
{
    return (P_DirectoryTotals){ atomic_load(&tree->folders), atomic_load(&tree->files), atomic_load(&tree->bytes), finished && !atomic_load(&tree->failed) };
}

// Adds `count` more found items to what the listing shows while it works.
function void Prog__CountFound(Prog_JobInput *input, const P_File *files, u64 count)
{
    u64 folders = 0, shownFolders = 0, shownFiles = 0;
    for (u64 i = 0; i < count; ++i) {
        b32 folder = files[i].kind == P_FileKind_Directory, shown = !files[i].hidden || input->showHidden;
        folders += folder ? 1 : 0;
        shownFolders += folder && shown ? 1 : 0;
        shownFiles += !folder && shown ? 1 : 0;
    }
    atomic_fetch_add(&input->foundFolders, folders);
    atomic_fetch_add(&input->foundFiles, count - folders);
    atomic_fetch_add(&input->shownFolders, shownFolders);
    atomic_fetch_add(&input->shownFiles, shownFiles);
}

function b32 Prog__SamePath(String a, String b);

function void Prog__DirectoryJob(P_Job *job)
{
    Prog_JobInput *input = job->input;
    if (input->kind == Prog_JobKind_Open || input->kind == Prog_JobKind_Rename) {
        P_FileError *error = ArenaPushStruct(&job->arena, P_FileError);
        // A file inside an archive opens from its copy taken out.
        String copy = input->kind == Prog_JobKind_Open ? Prog_ArchiveCopy(&job->arena, input->path, input->progress, &job->cancelToken) : (String){0};
        if (!P_IsCancelled(&job->cancelToken)) {
            *error = input->explorer ? P_OpenFileExplorer(&job->arena, input->path)
                   : input->kind == Prog_JobKind_Open ? P_OpenPath(&job->arena, copy.length ? copy : input->path, (P_FileOrder){ input->sort, (b8)input->descending })
                                                      : P_RenamePath(&job->arena, input->path, input->name, input->newName);
        }
        job->result = error;
        return;
    }
    if (input->kind == Prog_JobKind_Index) {
        // Lists folders for a few dozen milliseconds, several at once, and hands back what is inside them, so that it never holds the workers for long.
        Prog_Indexed *result = ArenaPushStruct(&job->arena, Prog_Indexed);
        ArrayStr names = {0};
        Prog_ArrayU32 parents = {0};
        Prog_ArrayFlag flags = {0};
        Prog__IndexSlice slice = { .input = input, .cancel = &job->cancelToken, .until = PlatformNowMicroseconds() + UINT64_C(30000) };
        slice.listings = ArenaPushArray(&job->arena, Max(input->pathCount, (u64)1), P_DirectoryResult);
        u32 lanes = Max(1u, Min(P_ProcessorCount(), (u32)Prog_WalkLanes));
        P_RunLanes(lanes, Prog__IndexLane, &slice);
        u64 taken = Min((u64)atomic_load(&slice.next), input->pathCount);
        while (result->listed < taken && !P_IsCancelled(&job->cancelToken)) {
            String folder = input->paths[result->listed];
            const P_DirectoryResult *listing = &slice.listings[result->listed];
            u32 parent = input->entries[result->listed++];
            Scratch scratch = GetScratchEx(&job->arena);
            for (u64 i = 0; i < listing->files.header.count && names.header.count < input->room; ++i) {
                const P_File *file = &listing->files.v[i];
                b32 directory = file->kind == P_FileKind_Directory;
                // Links are not followed, so that the walk ends; a link to a folder is still found by its name.
                if (directory && !file->symbolicLink && P_IsVirtualFolder(Prog_JoinPath(scratch.arena, folder, file->name))) continue;
                ArrayPush(&job->arena, names, StrCopy(&job->arena, file->name));
                ArrayPush(&job->arena, parents, parent);
                ArrayPush(&job->arena, flags, (b8)((file->hidden ? Prog_IndexFlag_Hidden : 0) | (directory && !file->symbolicLink ? 0 : Prog_IndexFlag_File)));
            }
            ReleaseScratch(scratch);
        }
        for (u32 i = 0; i < lanes; ++i) ArenaRelease(&slice.lanes[i]);
        result->names = names.v;
        result->parents = parents.v;
        result->flags = flags.v;
        result->count = names.header.count;
        job->result = result;
        if (input->name.length && !P_IsCancelled(&job->cancelToken)) {
            // The file grows with every slice, in the order the entries join the index: the roots first, then what each slice found.
            Scratch scratch = GetScratchEx(&job->arena);
            u64 size = input->restart ? Prog_IndexFileMagic.length : 0;
            for (u32 pass = input->restart ? 0 : 1; pass < 2; ++pass) {
                for (u64 i = 0; i < (pass == 0 ? input->pathCount : result->count); ++i) size += 7 + Min((pass == 0 ? input->paths[i] : result->names[i]).length, (u64)UINT16_MAX);
            }
            u8 *text = ArenaPushArray(scratch.arena, Max(size, (u64)1), u8), *at = text;
            if (input->restart) {
                MemCopy(at, Prog_IndexFileMagic.value, Prog_IndexFileMagic.length);
                at += Prog_IndexFileMagic.length;
            }
            for (u32 pass = input->restart ? 0 : 1; pass < 2; ++pass) {
                for (u64 i = 0; i < (pass == 0 ? input->pathCount : result->count); ++i) {
                    String name = pass == 0 ? input->paths[i] : result->names[i];
                    u32 parent = pass == 0 ? UINT32_MAX : result->parents[i];
                    u16 length = (u16)Min(name.length, (u64)UINT16_MAX);
                    MemCopy(at, &parent, sizeof(parent));
                    at[4] = pass == 0 ? 0 : (u8)result->flags[i];
                    MemCopy(at + 5, &length, sizeof(length));
                    MemCopy(at + 7, name.value, length);
                    at += 7 + length;
                }
            }
            P_AppendFile(scratch.arena, input->name, StrMake((const char *)text, size), input->restart);
            ReleaseScratch(scratch);
        }
        return;
    }
    if (input->kind == Prog_JobKind_IndexLoad) {
        Prog_Indexed *result = ArenaPushStruct(&job->arena, Prog_Indexed);
        job->result = result;
        String text = {0};
        if (!P_ReadFile(&job->arena, input->name, &text) || text.length < Prog_IndexFileMagic.length || !StrIsEqual(StrFromTo(text, 0, (i64)Prog_IndexFileMagic.length), Prog_IndexFileMagic)) return;
        // Counted first, so that the entries fill arrays of their size.
        u64 count = 0;
        for (u64 at = Prog_IndexFileMagic.length; at + 7 <= text.length;) {
            u16 length;
            MemCopy(&length, text.value + at + 5, sizeof(length));
            at += 7 + length;
            if (at <= text.length) ++count;
        }
        count = Min(count, (u64)Prog_MaxIndexed);
        result->packed = ArenaPushArray(&job->arena, Max(count, (u64)1), const u8 *);
        result->parents = ArenaPushArray(&job->arena, Max(count, (u64)1), u32);
        result->flags = ArenaPushArray(&job->arena, Max(count, (u64)1), b8);
        for (u64 at = Prog_IndexFileMagic.length; result->count < count;) {
            u32 parent;
            u16 length;
            MemCopy(&parent, text.value + at, sizeof(parent));
            MemCopy(&length, text.value + at + 5, sizeof(length));
            // A parent always comes before what is inside it, so anything else means the file is damaged.
            if (parent != UINT32_MAX && parent >= result->count) break;
            result->parents[result->count] = parent;
            result->flags[result->count] = (b8)text.value[at + 4];
            result->packed[result->count++] = (const u8 *)text.value + at + 5;
            at += 7 + length;
        }
        return;
    }
    if (input->kind == Prog_JobKind_IndexFinish) {
        P_ReplaceFile(&job->arena, input->path, input->name);
        return;
    }
    if (input->kind == Prog_JobKind_GoTo) {
        Prog_GoToListed *result = ArenaPushStruct(&job->arena, Prog_GoToListed);
        P_DirectoryResult listing = P_EnumerateDirectory(&job->arena, input->path, &job->cancelToken);
        result->files = ArenaPushArray(&job->arena, Max(listing.files.header.count, (u64)1), P_File);
        for (u32 pass = 0; pass < 2; ++pass) {
            for (u64 i = 0; i < listing.files.header.count; ++i) {
                const P_File *file = &listing.files.v[i];
                if ((file->kind == P_FileKind_Directory) == (pass == 0) && (input->showHidden || !file->hidden)) result->files[result->count++] = *file;
            }
        }
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Reveal) {
        String *real = ArenaPushStruct(&job->arena, String);
        if (!P_RealPath(&job->arena, input->path, real)) *real = input->path;
        job->result = real;
        return;
    }
    if (input->kind == Prog_JobKind_Fonts) {
        // Sorted by name, each family once, though a system lists some more than once.
        Prog_FontsListed *result = ArenaPushStruct(&job->arena, Prog_FontsListed);
        ArrayStr found = PlatformFontFamilies(&job->arena);
        String *names = ArenaPushArray(&job->arena, Max(found.header.count, (u64)1), String);
        u64 count = 0;
        for (u64 i = 0; i < found.header.count; ++i) {
            String name = found.v[i];
            u64 at = count;
            while (at && Prog_CompareNames(names[at - 1], name) > 0) --at;
            if ((at && Prog__EqualFolded(names[at - 1], name)) || !name.length || name.value[0] == '@') continue;
            MemMove(&names[at + 1], &names[at], (count - at) * sizeof(String));
            names[at] = name;
            ++count;
        }
        result->names = names;
        result->count = count;
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_FilesList) {
        // What the list would show of it, sorted as a list by name sorts it, folders first.
        Prog_FilesListed *result = ArenaPushStruct(&job->arena, Prog_FilesListed);
        String archive = {0}, inner = {0};
        P_DirectoryResult listing = Prog_InArchive(input->path, &archive, &inner) ? P_EnumerateArchive(&job->arena, archive, inner, &job->cancelToken)
                                                                                 : P_EnumerateDirectory(&job->arena, input->path, &job->cancelToken);
        u64 count = listing.files.header.count, kept = 0;
        Prog_Entry *entries = ArenaPushArray(&job->arena, Max(count, (u64)1), Prog_Entry), *sorted = ArenaPushArrayNoZero(&job->arena, Max(count, (u64)1), Prog_Entry);
        for (u64 i = 0; i < count; ++i) {
            const P_File *file = &listing.files.v[i];
            if ((input->showHidden || !file->hidden) && (input->showSystem || !file->system)) entries[kept++].file = *file;
        }
        result->items = Prog__SortEntries(entries, sorted, kept, Prog_Sort_Name, false, Prog_FolderSort_First, &job->cancelToken);
        result->count = result->items ? kept : 0;
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Inspect) {
        Prog_Inspected *result = ArenaPushStruct(&job->arena, Prog_Inspected);
        result->created = P_FileCreatedTime(input->path);
        Prog_TreeTotals tree = {0};
        b32 finished = Prog_MeasureTrees(&input->path, 1, &tree, &job->cancelToken);
        result->totals = Prog_TreeTotalsTaken(&tree, finished);
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Measure) {
        Prog_Measured *result = ArenaPushStruct(&job->arena, Prog_Measured);
        result->totals = ArenaPushArray(&job->arena, Max(input->rowCount, (u64)1), P_DirectoryTotals);
        job->result = result;
        // All of them at once, so that small ones are done while a big one is still walked, and their rows show the totals so far meanwhile.
        String *paths = ArenaPushArray(&job->arena, Max(input->rowCount, (u64)1), String);
        for (u64 i = 0; i < input->rowCount; ++i) paths[i] = Prog_JoinPath(&job->arena, input->path, input->names[i]);
        b32 finished = false;
        ProfileScope("MeasureTrees") finished = Prog_MeasureTrees(paths, input->rowCount, input->trees, &job->cancelToken);
        if (!finished) return;
        for (u64 i = 0; i < input->rowCount; ++i) result->totals[i] = Prog_TreeTotalsTaken(&input->trees[i], true);
        result->count = input->rowCount;
        return;
    }
    if (input->kind == Prog_JobKind_Transfer || input->kind == Prog_JobKind_Trash || input->kind == Prog_JobKind_Delete || input->kind == Prog_JobKind_EmptyTrash ||
        input->kind == Prog_JobKind_Restore || input->kind == Prog_JobKind_BinWait) {
        Prog_Transferred *result = ArenaPushStruct(&job->arena, Prog_Transferred);
        P_FileError *error = &result->error;
        if (P_IsCancelled(&job->cancelToken)) {
        } else if (input->kind == Prog_JobKind_Transfer) {
            ProfileScope("TransferItems") *error = Prog__Transfer(&job->arena, input, &result->placed, &job->cancelToken);
        } else if (input->kind == Prog_JobKind_Delete) {
            ProfileScope("DeleteItems") *error = P_DeleteItems(&job->arena, input->paths, input->pathCount, input->confirm, &job->cancelToken);
        } else if (input->kind == Prog_JobKind_EmptyTrash) {
            ProfileScope("EmptyTrash") *error = P_EmptyTrash(&job->arena, input->confirm);
        } else if (input->kind == Prog_JobKind_Restore) {
            ProfileScope("RestoreTrash") *error = P_RestoreTrash(&job->arena, input->path, input->paths, input->pathCount);
        } else if (input->kind == Prog_JobKind_BinWait) {
            P_WaitGone(input->paths, input->pathCount, 10000);
        } else {
            ProfileScope("TrashItems") *error = P_TrashItems(&job->arena, input->paths, input->pathCount);
        }
        job->result = error;
        return;
    }
    if (input->kind == Prog_JobKind_Undo) {
        // Each step is reversed with the operations that made it; a moved item that went under another name gets its old one back.
        P_FileError *error = ArenaPushStruct(&job->arena, P_FileError);
        const Prog_Undo *undo = &input->undo;
        if (undo->kind == Prog_UndoKind_Rename && undo->count == 1) {
            *error = P_RenamePath(&job->arena, Prog_ParentPath(undo->to[0]), Prog_EntryLeaf(undo->to[0]), Prog_EntryLeaf(undo->from[0]));
        } else if (undo->kind == Prog_UndoKind_Create || undo->kind == Prog_UndoKind_Copy) {
            String *paths = ArenaPushArray(&job->arena, Max(undo->count, (u64)1), String);
            u64 count = 0;
            for (u64 i = 0; i < undo->count; ++i) if (undo->to[i].length) paths[count++] = undo->to[i];
            if (count) *error = P_TrashItems(&job->arena, paths, count);
        } else if (undo->kind == Prog_UndoKind_Trash && input->redo) {
            *error = P_TrashItems(&job->arena, undo->to, undo->count);
        } else if (undo->kind == Prog_UndoKind_Trash) {
            // Each item from the bin by where it was, the one deleted last where it holds several from the same place.
            String bin = P_TrashFolder(&job->arena);
            P_ArrayTrashed binned = P_ListTrash(&job->arena, bin, &job->cancelToken);
            String *names = ArenaPushArray(&job->arena, Max(undo->count, (u64)1), String);
            u64 found = 0;
            for (u64 i = 0; i < undo->count; ++i) {
                const P_Trashed *best = NULL;
                for (u64 b = 0; b < binned.header.count; ++b) {
                    const P_Trashed *item = &binned.v[b];
                    if (!Prog__SamePath(Prog_JoinPath(&job->arena, item->original, item->name), undo->to[i])) continue;
                    if (!best || item->deletedTimeUnix > best->deletedTimeUnix) best = item;
                }
                if (best) names[found++] = best->file.name;
            }
            if (found) *error = P_RestoreTrash(&job->arena, bin, names, found);
            else *error = (P_FileError){ 1, S("They are no longer in the bin") };
        } else if (undo->kind == Prog_UndoKind_Move) {
            for (u64 i = 0; i < undo->count && !error->code; ++i) {
                if (!undo->to[i].length || StrIsEqual(undo->to[i], undo->from[i])) continue;
                String folder = Prog_ParentPath(undo->from[i]);
                ArrayStr placed = {0};
                *error = P_TransferItems(&job->arena, &undo->to[i], 1, folder, true, NULL, &placed, &job->cancelToken);
                String back = placed.header.count ? Prog_EntryLeaf(placed.v[0]) : (String){0}, name = Prog_EntryLeaf(undo->from[i]);
                if (!error->code && back.length && !StrIsEqual(back, name)) *error = P_RenamePath(&job->arena, folder, back, name);
            }
        }
        job->result = error;
        return;
    }
    if (input->kind == Prog_JobKind_Git) {
        Prog_GitStatus *result = ArenaPushStruct(&job->arena, Prog_GitStatus);
        *result = Prog_FindGitStatus(&job->arena, input->path);
        job->result = result;
        return;
    }
    // The repositories among `names`, with their branches, or with `offset` set how many tracked files each has changed: untracked ones
    // are left out, which keeps a status of a big repository quick.
    if (input->kind == Prog_JobKind_Repos) {
        Prog_Repos *result = ArenaPushStruct(&job->arena, Prog_Repos);
        for (u64 i = 0; i < input->rowCount && !P_IsCancelled(&job->cancelToken); ++i) {
            String folder = Prog_JoinPath(&job->arena, input->path, input->names[i]), head = {0};
            if (!Prog__ReadGitHead(&job->arena, folder, &head)) continue;
            Prog_Repo repo = { Prog_GitBranch(head), -1 };
            String status[] = { S("git"), S("status"), S("--porcelain=v1"), S("-z"), S("--untracked-files=no") };
            String changes = {0};
            if (input->offset && P_RunCommand(&job->arena, folder, status, CArrayCount(status), 4000, &changes)) {
                Prog_GitStatus counted = {0};
                Prog_ReadGitChanges(&job->arena, folder, folder, changes, &counted);
                repo.changes = (i32)counted.changes;
            }
            MapsPut(&job->arena, *result, input->names[i], repo);
        }
        job->result = result;
        return;
    }
    // Which of the recents at `names` are gone: kept first in `names` and `rows`, as many as `rowCount` then says.
    if (input->kind == Prog_JobKind_RecentsCheck) {
        u64 gone = 0;
        for (u64 i = 0; i < input->rowCount && !P_IsCancelled(&job->cancelToken); ++i) {
            if (!P_PathExists(input->names[i])) input->names[gone] = input->names[i], input->rows[gone++] = input->rows[i];
        }
        input->rowCount = gone;
        job->result = input;
        return;
    }
    if (input->kind == Prog_JobKind_HexPage) {
        String *bytes = ArenaPushStruct(&job->arena, String), copy = Prog_ArchiveCopy(&job->arena, input->path, NULL, &job->cancelToken);
        if (!P_ReadFileAt(&job->arena, copy.length ? copy : input->path, input->offset, Prog_HexPageBytes, bytes)) *bytes = (String){0};
        job->result = bytes;
        return;
    }
    if (input->kind == Prog_JobKind_PdfPage) {
        P_Image *image = ArenaPushStruct(&job->arena, P_Image);
        String copy = Prog_ArchiveCopy(&job->arena, input->path, NULL, &job->cancelToken);
        if (!P_LoadPdfPage(&job->arena, copy.length ? copy : input->path, (u32)input->offset, (i32)input->size, image)) *image = (P_Image){0};
        job->result = image;
        return;
    }
    if (input->kind == Prog_JobKind_Preview) {
        // A 3D model's picture, else text where the start of the file reads as text, else the system's picture of it.
        // With the inspector set to hex, every file's bytes.
        Prog_Preview *result = ArenaPushStruct(&job->arena, Prog_Preview);
        // A file inside an archive is read from its copy taken out, but for one too large to be worth it.
        String copy = !input->folder && input->size <= Prog_ArchivePreviewBytes ? Prog_ArchiveCopy(&job->arena, input->path, input->progress, &job->cancelToken) : (String){0};
        String path = copy.length ? copy : input->path;
        String start = {0};
        P_Image image = {0};
        P_FileArray items = {0};
        b32 model = !input->hex && !input->folder && !input->archive && Prog_ModelMesh(&job->arena, path, input->size, &job->cancelToken, &result->model);
        if (input->archive) {
            // Counted up to the same limit as a listing; short of it, that is all there is.
            if (!P_IsCancelled(&job->cancelToken)) {
                P_ListArchive(&job->arena, path, Prog_PreviewArchiveItems, &job->cancelToken, &items);
                result->archiveTotals = Prog_ArchiveTotals(&items);
                result->archiveTotals.complete = items.header.count < Prog_PreviewArchiveItems;
                result->archiveCounted = true;
            }
        } else if (input->hex && !input->folder) {
            result->hex = P_ReadFileStart(&job->arena, path, Prog_HexPageBytes, &result->text);
        } else if (!input->folder && Prog__IsArchive(path) && P_ListArchive(&job->arena, path, Prog_PreviewArchiveItems, &job->cancelToken, &items)) {
            result->text = Prog_ArchiveListing(&job->arena, &items, items.header.count >= Prog_PreviewArchiveItems);
            result->archive = true;
        } else if (model) {
            // Before the text, which OBJ and glTF files read as; the mesh goes to the GPU when it arrives.
        } else if (!input->folder && path.length >= 4 && Prog__EqualFolded(StrFrom(path, path.length - 4), S(".pdf")) && !P_IsCancelled(&job->cancelToken) &&
                   P_LoadImageFile(&job->arena, path, Prog_PreviewImageSide, &image, &result->sourceWidth, &result->sourceHeight)) {
            // Its first page, before the text, which a PDF of plain ASCII would read as, and the sizes of all of them, to scroll through.
            result->pdfPages = P_PdfPages(&job->arena, path, Prog_MaxPdfPages, &result->pdfSizes);
            result->pixels = image.pixels;
            result->width = image.width;
            result->height = image.height;
            result->image = true;
        } else if (!input->folder && Prog_IsBlp(path) && Prog_LoadBlp(&job->arena, path, Prog_PreviewImageSide, &image, &result->sourceWidth, &result->sourceHeight)) {
            // A game's texture, which the system's pictures do not read.
            result->pixels = image.pixels;
            result->width = image.width;
            result->height = image.height;
            result->image = true;
        } else if (!input->folder && Prog_IsOffice(path) && (result->text = Prog_OfficeText(&job->arena, path, input->size)).length) {
            // Word's prose wraps; a spreadsheet's columns stay lined up.
            result->document = Prog__EqualFolded(StrFrom(path, path.length - 5), S(".docx")) || Prog__EqualFolded(StrFrom(path, path.length - 5), S(".docm"));
            // A Word document's or a spreadsheet's text, which its zip of XML would otherwise show as bytes.
        } else if (!input->folder && P_ReadFileStart(&job->arena, path, Prog_PreviewBytes, &start) && Prog__LooksLikeText(start)) {
            result->text = start;
        } else if (!input->folder && !P_IsCancelled(&job->cancelToken) &&
                   P_LoadImageFile(&job->arena, path, Prog_PreviewImageSide, &image, &result->sourceWidth, &result->sourceHeight)) {
            result->pixels = image.pixels;
            result->width = image.width;
            result->height = image.height;
            result->image = true;
        } else if (!P_IsCancelled(&job->cancelToken)) {
            // A file without a picture of its own, such as a program or an archive, shows its bytes.
            Scratch scratch = GetScratchEx(&job->arena);
            b32 hex = !input->folder && !P_LoadFileThumbnail(scratch.arena, path, P_FileKind_File, Prog_PreviewSize, true, &job->cancelToken, &image);
            if (hex) {
                result->text = start;
                result->hex = true;
            } else if (image.pixels || P_LoadFileThumbnail(scratch.arena, path, input->folder ? P_FileKind_Directory : P_FileKind_File, Prog_PreviewSize, false, &job->cancelToken, &image)) {
                result->pixels = Prog_ResizeIcon(&job->arena, image, Prog_PreviewSize);
                result->width = result->height = Prog_PreviewSize;
            }
            ReleaseScratch(scratch);
        }
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Create) {
        Prog_Created *result = ArenaPushStruct(&job->arena, Prog_Created);
        result->error = P_CreateItem(&job->arena, input->path, input->name, input->folder, &result->name);
        job->result = result;
        return;
    }
    // Shortcuts to the pasted files, the last one's name kept to be selected.
    if (input->kind == Prog_JobKind_Link) {
        Prog_Created *result = ArenaPushStruct(&job->arena, Prog_Created);
        for (u64 i = 0; i < input->pathCount && !result->error.code && !P_IsCancelled(&job->cancelToken); ++i) {
            result->error = P_CreateLink(&job->arena, input->path, input->paths[i], &result->name);
        }
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Places) {
        Prog_Places *result = ArenaPushStruct(&job->arena, Prog_Places);
        ProfileScope("ListPlaces") {
            result->places = P_UserPlaces(&job->arena);
            result->drives = P_Drives(&job->arena);
            result->roots = P_PathRoots(&job->arena);
            result->searched = P_SearchPlaces(&job->arena);
            for (u64 i = 0; i < input->pathCount; ++i) if (!P_PathExists(input->paths[i])) ArrayPush(&job->arena, result->missing, input->paths[i]);
        }
        job->result = result;
        return;
    }
    if (input->kind == Prog_JobKind_Save) {
        P_FileError *error = ArenaPushStruct(&job->arena, P_FileError);
        *error = P_WriteFile(&job->arena, input->path, input->name);
        job->result = error;
        return;
    }
    if (input->kind == Prog_JobKind_WarmMenu) {
        ProfileScope("WarmMenu") P_ContextMenuWarm(input->path, input->name);
        return;
    }
    Prog_Directory *result = ArenaPushStruct(&job->arena, Prog_Directory);
    job->result = result;
    result->path = input->path;
    result->sort = input->sort;
    result->descending = input->descending;
    result->folderSort = input->folderSort;
    result->showHidden = input->showHidden;
    result->expanded = input->expanded;
    result->driveKind = StrIsEqual(input->path, Prog_ThisPC) ? P_DriveKind_SolidState : (u8)P_PathDriveKind(input->path);
    P_DirectoryResult listing = {0};
    // This PC lists the drives, in the system's order.
    if (StrIsEqual(input->path, Prog_ThisPC)) {
        P_ArrayPlace drives = P_Drives(&job->arena);
        result->entries = ArenaPushArray(&job->arena, Max(drives.header.count, (u64)1), Prog_Entry);
        for (u64 i = 0; i < drives.header.count; ++i) {
            const P_Place *drive = &drives.v[i];
            Prog_Entry *entry = &result->entries[result->count++];
            entry->file = (P_File){ .name = drive->path, .kind = P_FileKind_Directory, .size = drive->totalBytes, .metadataValid = drive->totalBytes != 0 };
            entry->drive = true;
            entry->label = drive->name;
            entry->type = drive->type;
            entry->fileSystem = drive->fileSystem;
            entry->freeBytes = drive->freeBytes;
            entry->measured = true;
        }
        result->totalFolders = result->folderCount = result->count;
        return;
    }
    // The Recycle Bin lists what it holds, each with its own name, where it came from and when it went.
    if (P_IsTrashFolder(input->path)) {
        P_ArrayTrashed binned = P_ListTrash(&job->arena, input->path, &job->cancelToken);
        result->entries = ArenaPushArray(&job->arena, Max(binned.header.count, (u64)1), Prog_Entry);
        for (u64 i = 0; i < binned.header.count; ++i) {
            const P_Trashed *item = &binned.v[i];
            b32 folder = item->file.kind == P_FileKind_Directory;
            if (folder) ++result->totalFolders;
            else ++result->totalFiles;
            if (item->file.hidden && !input->showHidden) continue;
            Prog_Entry *entry = &result->entries[result->count++];
            entry->file = item->file;
            entry->label = item->name;
            entry->original = item->original;
            entry->deletedTimeUnix = item->deletedTimeUnix;
            entry->type = P_FileTypeName(&job->arena, item->name, item->file.kind);
            entry->measured = folder;
            entry->bytes = folder ? item->file.size : 0;
            result->folderCount += folder ? 1 : 0;
        }
        Prog_Entry *buffer = ArenaPushArray(&job->arena, Max(result->count, (u64)1), Prog_Entry);
        Prog_Entry *sorted = Prog__SortEntries(result->entries, buffer, result->count, input->sort, input->descending, input->folderSort, &job->cancelToken);
        if (sorted) result->entries = sorted;
        else result->cancelled = true;
        return;
    }
    // A folder inside an archive is listed from the archive.
    String archive = {0}, inner = {0};
    result->archive = Prog_InArchive(input->path, &archive, &inner);
    ProfileScope("EnumerateDirectory") listing = result->archive ? P_EnumerateArchive(&job->arena, archive, inner, &job->cancelToken)
                                                                 : P_EnumerateDirectory(&job->arena, input->path, &job->cancelToken);
    result->error = (P_FileError){ listing.errorCode, listing.errorString };
    result->cancelled = listing.cancelled;
    result->hidden = listing.hidden;
    // A folder that is gone, deleted or moved away, lists the nearest folder above it that is still there instead: a tab's, one listed again
    // or one gone back or forward to. One newly gone to shows that it is not there, and the panel stays where it was.
    if (result->error.code && !result->cancelled && !result->archive && !input->expanded && !input->addHistory && !P_PathExists(input->path)) {
        for (String up = Prog_UpPath(input->path); up.length && !StrIsEqual(up, Prog_ThisPC) && !P_IsCancelled(&job->cancelToken); up = Prog_UpPath(up)) {
            if (!P_IsDirectory(up)) continue;
            listing = P_EnumerateDirectory(&job->arena, up, &job->cancelToken);
            if (listing.errorCode) break;
            result->path = StrCopy(&job->arena, up);
            result->driveKind = (u8)P_PathDriveKind(up);
            result->error = (P_FileError){0};
            result->cancelled = listing.cancelled;
            result->hidden = listing.hidden;
            break;
        }
    }
    if (result->error.code || result->cancelled) return;
    P_FileArray files = listing.files;
    Prog__CountFound(input, files.v, files.header.count);
    if (input->expanded) ProfileScope("ExpandDirectory") {
        // Lanes on threads of their own list folders from one shared stack, keeping what they find apart; links are not followed and
        // unreadable folders are skipped.
        Prog__Walk walk = { .input = input, .cancel = &job->cancelToken, .top = files, .base = result, .nextPartial = PlatformNowMicroseconds() + UINT64_C(150000) };
        if (!P_MutexInit(&walk.mutex)) {
            result->cancelled = true;
            return;
        }
        if (!P_ConditionInit(&walk.condition)) {
            P_MutexRelease(&walk.mutex);
            result->cancelled = true;
            return;
        }
        for (u64 i = 0; i < files.header.count; ++i) {
            const P_File *folder = &files.v[i];
            if (folder->kind == P_FileKind_Directory && !folder->symbolicLink && (!folder->hidden || input->showHidden)) ArrayPush(&walk.arena, walk.pending, folder->name);
        }
        u32 lanes = Max(1u, Min(P_ProcessorCount(), (u32)Prog_WalkLanes));
        P_RunLanes(lanes, Prog__WalkLane, &walk);
        P_ConditionRelease(&walk.condition);
        P_MutexRelease(&walk.mutex);
        u64 total = files.header.count;
        for (u32 i = 0; i < lanes; ++i) total += walk.lanes[i].found.header.count;
        P_FileArray all = {0};
        ArrayReserve(&job->arena, all, total);
        for (u64 i = 0; i < files.header.count; ++i) ArrayPush(&job->arena, all, files.v[i]);
        for (u32 i = 0; i < lanes; ++i) {
            for (u64 j = 0; j < walk.lanes[i].found.header.count; ++j) {
                P_File file = walk.lanes[i].found.v[j];
                file.name = StrCopy(&job->arena, file.name);
                ArrayPush(&job->arena, all, file);
            }
            ArenaRelease(&walk.lanes[i].arena);
        }
        ArenaRelease(&walk.arena);
        if (P_IsCancelled(&job->cancelToken)) {
            result->cancelled = true;
            return;
        }
        files = all;
    }
    Prog__FillListing(&job->arena, input, files, result, &job->cancelToken);
}

// The rows of `files` in `result`: the shown ones with their type names, an expanded listing's folder totals, sorted.
function void Prog__FillListing(Arena *arena, const Prog_JobInput *input, P_FileArray files, Prog_Directory *result, const P_CancelToken *cancel)
{
    result->entries = ArenaPushArray(arena, Max(files.header.count, (u64)1), Prog_Entry);
    // The walk keeps each folder's items together, so that a folder is a run of them.
    if (input->expanded) result->parents = ArenaPushArrayNoZero(arena, Max(files.header.count, (u64)1), String);
    // Type names repeat per type, so each is asked of the system once per listing.
    struct { MapHeader header; struct { String key; String value; } *v; } types = {0};
    // The keys are made in scratch, and only a type seen for the first time keeps a copy.
    Scratch keys = GetScratchEx(arena);
    ProfileScope("ListEntries") for (u64 i = 0; i < files.header.count; ++i) {
        if (P_IsCancelled(cancel)) {
            result->cancelled = true;
            ReleaseScratch(keys);
            return;
        }
        P_File file = files.v[i];
        if (file.kind == P_FileKind_Directory) ++result->totalFolders;
        else ++result->totalFiles;
        if ((file.hidden && !input->showHidden) || (file.system && !input->showSystem)) continue;
        Prog_Entry *entry = &result->entries[result->count++];
        entry->file = file;
        for (u32 r = 0; r < input->recentCount && !entry->recent; ++r) entry->recent = (b8)StrIsEqual(file.name, input->recentNames[r]);
        entry->recentFirst = (b8)(entry->recent && input->sortRecents);
        result->recentCount += entry->recent ? 1 : 0;
        result->folderCount += file.kind == P_FileKind_Directory;
        if (result->parents) {
            String parent = Prog_EntryParent(file.name);
            if (!result->parentCount || !StrIsEqual(result->parents[result->parentCount - 1], parent)) result->parents[result->parentCount++] = parent;
            entry->parent = result->parentCount - 1;
        }
        u64 mark = ArenaPos(keys.arena);
        String typeKey = P_FileTypeKey(keys.arena, file.name, file.kind);
        String *type = MapsGetPtrNull(types, typeKey);
        if (type) {
            entry->type = *type;
        } else {
            entry->type = P_FileTypeName(arena, file.name, file.kind);
            MapsPut(arena, types, StrCopy(arena, typeKey), entry->type);
        }
        ArenaPopTo(keys.arena, mark);
        if (file.symbolicLink) entry->type = file.kind == P_FileKind_Directory ? S("Folder link") : S("Link");
    }
    ReleaseScratch(keys);
    if (input->expanded) ProfileScope("FolderTotals") {
        // The walk saw everything below each folder, so folder totals come from the listing instead of measure jobs.
        // Hidden items count only when they are shown.
        struct { MapHeader header; struct { String key; u64 value; } *v; } folders = {0};
        for (u64 i = 0; i < result->count; ++i) {
            Prog_Entry *entry = &result->entries[i];
            if (entry->file.kind != P_FileKind_Directory) continue;
            entry->measured = true;
            MapsPut(arena, folders, entry->file.name, i + 1);
        }
        for (u64 i = 0; i < result->count; ++i) {
            const Prog_Entry *entry = &result->entries[i];
            for (String parent = Prog_EntryParent(entry->file.name); parent.length; parent = Prog_EntryParent(parent)) {
                u64 at = MapsGet(folders, u64, parent);
                if (!at) break;
                Prog_Entry *owner = &result->entries[at - 1];
                if (entry->file.kind == P_FileKind_Directory) {
                    ++owner->folders;
                } else {
                    ++owner->files;
                    owner->bytes += entry->file.size;
                }
            }
        }
    }
    Prog_Entry *buffer = ArenaPushArray(arena, Max(result->count, (u64)1), Prog_Entry);
    Prog_Entry *sorted = NULL;
    ProfileScope("SortEntries") sorted = Prog__SortEntries(result->entries, buffer, result->count, input->sort, input->descending, input->folderSort, cancel);
    if (sorted) result->entries = sorted;
    else result->cancelled = true;
}

// Whether the window has what its first frame should show: the folder listed, the sidebar's places, and every file icon the last build drew.
function b32 Prog_ReadyToShow(const Prog_State *state)
{
    return !state->loading && !state->directoryJob && (state->places.header.count || !state->placesWanted) && !state->iconsPending;
}

// Whether the settings have the shown folder's subfolders measured, on its kind of drive.
function b32 Prog_MeasuresFolders(const Prog_State *state)
{
    if (state->noFolderSizes) return false;
    switch (state->directory.driveKind) {
        case P_DriveKind_Spinning: return state->folderSizesSpinning;
        case P_DriveKind_Network: return state->folderSizesNetwork;
        default: return !state->noFolderSizesSolidState;
    }
}

// Whether the settings have the shown folder's icon views draw its files' thumbnails.
function b32 Prog_ShowsThumbnails(const Prog_State *state)
{
    if (state->thumbnailMode == Prog_Thumbnails_LocalDisk) return state->directory.driveKind != P_DriveKind_Network;
    return state->thumbnailMode == Prog_Thumbnails_Always;
}

// Measures the shown folder's subfolders that have no totals yet, one job at a time; when the settings measure none, only those asked for again.
function void Prog__MeasureFolders(Prog_State *state)
{
    b32 all = Prog_MeasuresFolders(state);
    // Drives are not walked to be measured, the bin knows its folders' sizes, and an archive's folders are not on disk.
    if (StrIsEqual(state->directory.path, Prog_ThisPC) || P_IsTrashFolder(state->directory.path) || state->directory.archive) return;
    // Not while a walk through the whole tree is on its way, which counts them itself.
    if (state->measureJob || (state->directoryJob && ((Prog_JobInput *)state->directoryJob->input)->expanded)) return;
    u64 count = 0;
    for (u64 i = 0; i < state->directory.count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        count += entry->file.kind == P_FileKind_Directory && (all ? !entry->measured || entry->remeasure : entry->remeasure);
    }
    if (!count) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Measure;
    input->path = StrCopy(&job->arena, state->directory.path);
    input->listing = state->listing;
    input->trees = ArenaPushArray(&job->arena, count, Prog_TreeTotals);
    input->names = ArenaPushArray(&job->arena, count, String);
    input->rows = ArenaPushArray(&job->arena, count, u64);
    for (u64 i = 0; i < state->directory.count; ++i) {
        const Prog_Entry *entry = &state->directory.entries[i];
        if (entry->file.kind != P_FileKind_Directory || (all ? entry->measured && !entry->remeasure : !entry->remeasure)) continue;
        input->names[input->rowCount] = StrCopy(&job->arena, entry->file.name);
        input->rows[input->rowCount++] = i;
    }
    job->input = input;
    state->measureJob = job;
    P_JobSubmit(job);
}

// Finds out `folder`'s git status in a job, at most one at a time; a request while one runs asks again, for the folder shown then, once it is done.
function void Prog_RequestGitStatus(Prog_State *state, String folder)
{
    if (state->gitJob) {
        state->gitAgain = true;
        return;
    }
    state->gitAgain = false;
    state->gitTime = PlatformNowMicroseconds();
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Git;
    input->path = StrCopy(&job->arena, folder);
    job->input = input;
    state->gitJob = job;
    P_JobSubmit(job);
}

// Whether the panel's columns have Git, which the repositories among its subfolders are looked for only for.
function b32 Prog_ShowsGit(const Prog_State *state)
{
    Prog_ColumnSet set = Prog_PanelColumns(state);
    for (u32 i = 0; i < set.count; ++i) if (set.kinds[i] == Prog_ColumnKind_Git) return true;
    return false;
}

// Finds out the shown folder's repositories among its subfolders `names`, or with `status` how changed one of them is, one job at a time.
function void Prog__RepoJob(Prog_State *state, const String *names, u64 count, b32 status)
{
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Repos;
    input->path = StrCopy(&job->arena, state->directory.path);
    input->offset = status ? 1 : 0;
    input->names = ArenaPushArray(&job->arena, Max(count, (u64)1), String);
    for (u64 i = 0; i < count; ++i) input->names[i] = StrCopy(&job->arena, names[i]);
    input->rowCount = count;
    job->input = input;
    state->reposJob = job;
    P_JobSubmit(job);
}

// Looks for repositories among the shown folder's subfolders while the Git column shows, their branches first; then, but for a network
// drive's, how changed each is, one after another.
function void Prog_RequestRepos(Prog_State *state)
{
    if (state->reposJob) P_RequestCancel(&state->reposJob->cancelToken);
    state->reposJob = NULL;
    ArenaClear(&state->repoArena);
    state->repos = (Prog_Repos){0};
    state->reposNext = 0;
    state->reposTime = PlatformNowMicroseconds();
    if (!Prog_ShowsGit(state) || StrIsEqual(state->directory.path, Prog_ThisPC) || P_IsTrashFolder(state->directory.path) || state->directory.archive || state->directory.expanded) return;
    Scratch scratch = GetScratch();
    String *names = ArenaPushArray(scratch.arena, Max(state->directory.count, (u64)1), String);
    u64 count = 0;
    for (u64 i = 0; i < state->directory.count; ++i) {
        if (state->directory.entries[i].file.kind == P_FileKind_Directory) names[count++] = state->directory.entries[i].file.name;
    }
    if (count) Prog__RepoJob(state, names, count, false);
    ReleaseScratch(scratch);
}

// Starts on the next repository whose changes are not known yet.
function void Prog__NextRepoStatus(Prog_State *state)
{
    if (state->reposJob || state->directory.driveKind == P_DriveKind_Network) return;
    for (; state->reposNext < state->repos.header.count; ++state->reposNext) {
        if (state->repos.v[state->reposNext].value.changes >= 0) continue;
        Prog__RepoJob(state, &state->repos.v[state->reposNext].key, 1, true);
        return;
    }
}

function b32 Prog_PathOnline(const Prog_State *state, String path);

// Checks in a job that the recents are still there, dropping those that are gone. One on a share, or on a drive that is not there now,
// is left alone, as it may only be out of reach for a while.
function void Prog_CheckRecents(Prog_State *state)
{
    state->recentsCheckTime = PlatformNowMicroseconds();
    state->recentsCheckAgain = state->recentsCheckJob != NULL;
    if (state->recentsCheckJob || !state->recents.header.count) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_RecentsCheck;
    u64 count = state->recents.header.count;
    input->names = ArenaPushArray(&job->arena, count, String);
    input->rows = ArenaPushArray(&job->arena, count, u64);
    for (u64 i = 0; i < count; ++i) {
        String path = state->recents.v[i];
        if (Prog__StartsAt(path, 0, S("//")) || !Prog_PathOnline(state, path)) continue;
        input->names[input->rowCount] = StrCopy(&job->arena, path);
        input->rows[input->rowCount++] = i;
    }
    job->input = input;
    state->recentsCheckJob = job;
    P_JobSubmit(job);
}

// Starts finding out what a sidebar tip shows of `path`, unless that is already under way or known.
function void Prog_InspectFolder(Prog_State *state, String path)
{
    if (StrIsEqual(path, state->tipPath)) return;
    if (state->tipJob) P_RequestCancel(&state->tipJob->cancelToken);
    ArenaClear(&state->tipArena);
    state->tipPath = StrCopy(&state->tipArena, path);
    state->tipKnown = false;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Inspect;
    input->path = StrCopy(&job->arena, path);
    job->input = input;
    state->tipJob = job;
    P_JobSubmit(job);
}

function b32 Prog__TakeNumber(String *text, u64 *value);
function String Prog__ApplyWindowLine(String text);
function void Prog__ReleasePreview(Arena *arena, R_TextureHandle texture, Prog_ModelView *model, Prog_PdfView *pdf, P_Job *job, P_Job *hexJob);
function void Prog__ApplyFolderView(Prog_State *state, String path);
function void Prog__SetFolderView(Prog_State *state, String path, const Prog_ViewSettings *view);

// The tab's trail, when it goes on from the folder shown, or This PC shows; empty otherwise.
function String Prog_TrailText(const Prog_State *state)
{
    String trail = StrMake(state->trail.text, state->trail.length), path = state->directory.path;
    while (path.length > 1 && path.value[path.length - 1] == '/' && !(path.length == 3 && path.value[1] == ':')) --path.length;
    if (StrIsEqual(path, Prog_ThisPC)) return trail;
    if (path.length && path.value[path.length - 1] == '/') --path.length;
    b32 below = trail.length >= path.length && StrIsEqual(StrFromTo(trail, 0, (i64)path.length), path) && (trail.length == path.length || trail.value[path.length] == '/');
    return below ? trail : (String){0};
}

// A folder deeper than the trail, or off it, becomes the trail; This PC keeps it.
function void Prog__FollowTrail(Prog_State *state, String path)
{
    if (StrIsEqual(path, Prog_ThisPC)) return;
    String trail = StrMake(state->trail.text, state->trail.length);
    while (path.length > 1 && path.value[path.length - 1] == '/' && !(path.length == 3 && path.value[1] == ':')) --path.length;
    String bare = path.length && path.value[path.length - 1] == '/' ? StrFromTo(path, 0, (i64)path.length - 1) : path;
    b32 within = trail.length > bare.length && StrIsEqual(StrFromTo(trail, 0, (i64)bare.length), bare) && trail.value[bare.length] == '/';
    if (within || path.length > sizeof(state->trail.text)) return;
    MemCopy(state->trail.text, path.value, path.length);
    state->trail.length = (u32)path.length;
}

// Whether `path` is `root` or inside it.
function b32 Prog__IsUnder(String path, String root)
{
    if (!root.length || path.length < root.length || !StrIsEqual(StrFromTo(path, 0, (i64)root.length), root)) return false;
    return path.length == root.length || root.value[root.length - 1] == '/' || path.value[root.length] == '/';
}

function void Prog_RequestDirectory(Prog_State *state, String path, b32 addHistory, u64 historyAt)
{
    Prog__FollowTrail(state, path);
    // Going to a folder that holds the shown one, by Up, Back or the path, selects the folder it came from; This PC
    // selects the drive.
    String from = state->directory.path, child = {0};
    if (StrIsEqual(path, Prog_ThisPC) && StrIsEqual(Prog_UpPath(from), Prog_ThisPC)) child = from;
    else if (!StrIsEqual(from, path) && Prog__IsUnder(from, path)) {
        child = StrFrom(from, path.length + (path.value[path.length - 1] == '/' ? 0 : 1));
        Index slash = StrIndexChar(child, '/', SSF_None);
        if (slash.exists) child = StrFromTo(child, 0, (i64)slash.value);
    }
    if (child.length && !state->selectLength && child.length <= sizeof(state->selectText)) {
        MemCopy(state->selectText, child.value, child.length);
        state->selectLength = (u32)child.length;
    }
    // Another folder brings its kept view, or the panel's own back; a reload keeps what is shown.
    if (!StrIsEqual(path, state->directory.path)) Prog__ApplyFolderView(state, path);
    if (state->directoryJob) P_RequestCancel(&state->directoryJob->cancelToken);
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->path = StrCopy(&job->arena, path);
    input->generation = ++state->generation;
    input->sort = state->sort;
    input->descending = state->descending;
    input->folderSort = state->folderSort;
    input->showSystem = state->showSystem;
    // Another folder starts flat unless the filter is pinned.
    if (state->directory.path.length && !StrIsEqual(path, state->directory.path) && !state->filterSticky) state->expanded = false;
    input->showHidden = state->showHidden;
    input->expanded = state->expanded;
    if (input->expanded) {
        input->partial = ArenaPushStruct(&job->arena, Prog_Partial);
        if (!P_MutexInit(&input->partial->mutex)) input->partial = NULL;
    }
    input->addHistory = addHistory;
    input->historyAt = historyAt;
    input->passing = state->requestPassing;
    state->requestPassing = false;
    // The recents inside the folder, by name, for the listing to mark and sort first.
    if (state->showRecents || state->sortRecents) {
        input->recentNames = ArenaPushArray(&job->arena, Max(state->recents.header.count, (u64)1), String);
        for (u64 i = 0; i < state->recents.header.count; ++i) {
            String recent = state->recents.v[i];
            if (StrIsEqual(Prog_UpPath(recent), path)) input->recentNames[input->recentCount++] = StrCopy(&job->arena, Prog_EntryLeaf(recent));
        }
        input->sortRecents = state->sortRecents;
    }
    job->input = input;
    state->directoryJob = job;
    state->listStarted = PlatformNowMicroseconds();
    // A walk through the whole tree counts the folders itself, so counting them at the same time would only slow it down.
    if (input->expanded && state->measureJob) {
        P_RequestCancel(&state->measureJob->cancelToken);
        state->measureJob = NULL;
    }
    // Refreshing the shown folder keeps its rows usable until the new listing replaces them.
    state->loading = !StrIsEqual(path, state->directory.path);
    ArenaClear(&state->errorArena);
    state->error = (String){0};
    P_JobSubmit(job);
}

function void Prog__EnsureTab(Prog_State *state);
function void Prog__EnsurePanel(Prog_State *state);
function void Prog__EndRename(Prog_State *state, b32 commit);
function void Prog__OpenFontPicker(Prog_State *state, Prog_Option option);

// Bookmarks and recents, one path per line in the config folder.
#define Prog_BookmarksFile S("Bookmarks.conf")
// The window's panels and their tabs, and which sidebar groups are open, kept in the config folder between runs.
#define Prog_SessionFile S("Session.conf")
#define Prog_RecentsFile   S("Recents.conf")
#define Prog_FolderViewsFile S("FolderViews.conf")
#define Prog_MenuPinsFile S("MenuPins.conf")
#define Prog_QuickCommandsFile S("QuickCommands.conf")
#define Prog_MaxRecents    300
#define Prog_RecentScoreMax 10
// What a first visit scores.
#define Prog_RecentScoreNew 6


// The save slot of each list file.
function u32 Prog__SaveSlot(String file)
{
    return StrIsEqual(file, Prog_RecentsFile) ? 1 : StrIsEqual(file, Prog_FolderViewsFile) ? 2 : StrIsEqual(file, Prog_MenuPinsFile) ? 3 : StrIsEqual(file, Prog_QuickCommandsFile) ? 4 : 0;
}

// Replaces `list` with copies of the `count` strings at `items`, in a fresh arena.
function void Prog__SetList(Arena *arena, ArrayStr *list, const String *items, u64 count)
{
    Arena fresh = {0};
    ArrayStr result = {0};
    for (u64 i = 0; i < count; ++i) ArrayPush(&fresh, result, StrCopy(&fresh, items[i]));
    ArenaRelease(arena);
    *arena = fresh;
    *list = result;
}

function String Prog__ListText(Arena *arena, ArrayStr list)
{
    StrBuilder text = {0};
    for (u64 i = 0; i < list.header.count; ++i) {
        StrBuildStr(arena, &text, list.v[i]);
        StrBuildChar(arena, &text, '\n');
    }
    return text.buffer;
}

// A line each of a recent's path, then after a tab its score, decay, ordinal, whether it is a file and whether it is hidden.
function String Prog__RecentsText(Arena *arena, const Prog_State *state)
{
    StrBuilder text = {0};
    for (u64 i = 0; i < state->recents.header.count; ++i) {
        Prog_RecentRank rank = state->recentRanks[i];
        StrBuildF(arena, &text, "%S\t%u %u %u %u %u\n", state->recents.v[i], (u32)rank.score, (u32)rank.decay, rank.ordinal, (u32)rank.file, (u32)rank.hidden);
    }
    return text.buffer;
}

// Replaces the recents with copies of the `count` at `paths` and `ranks`, ordered best first and cut to the most kept.
function void Prog__SetRecents(Prog_State *state, const String *paths, const Prog_RecentRank *ranks, u64 count)
{
    Scratch scratch = GetScratch();
    u64 *order = ArenaPushArray(scratch.arena, Max(count, (u64)1), u64);
    for (u64 i = 0; i < count; ++i) {
        u64 at = i;
        for (; at > 0; --at) {
            Prog_RecentRank above = ranks[order[at - 1]], rank = ranks[i];
            if (above.score > rank.score || (above.score == rank.score && above.ordinal >= rank.ordinal)) break;
            order[at] = order[at - 1];
        }
        order[at] = i;
    }
    count = Min(count, (u64)Prog_MaxRecents);
    Arena fresh = {0};
    ArrayStr result = {0};
    Prog_RecentRank *kept = ArenaPushArray(&fresh, Max(count, (u64)1), Prog_RecentRank);
    u32 ordinal = 0;
    for (u64 i = 0; i < count; ++i) {
        ArrayPush(&fresh, result, StrCopy(&fresh, paths[order[i]]));
        kept[i] = ranks[order[i]];
        ordinal = Max(ordinal, kept[i].ordinal);
    }
    ArenaRelease(&state->recentArena);
    state->recentArena = fresh;
    state->recents = result;
    state->recentRanks = kept;
    state->recentOrdinal = Max(state->recentOrdinal, ordinal);
    ReleaseScratch(scratch);
}

// Whether the recent at `index` is dimmed as the list dims hidden items; one kept from before that was saved still is by its name.
function b32 Prog_RecentFaded(const Prog_State *state, u64 index)
{
    String name = Prog__FolderName(state->recents.v[index]);
    return state->recentRanks[index].hidden || (name.length && name.value[0] == '.');
}

// Whether the recent at `index` is left out of the sidebar and Go To: a hidden one while the active panel hides hidden items.
// Whether `path` is on a drive that is there now. Only a path on a drive letter, or under where Linux mounts removable drives, can be
// missing its drive; before the drives are first listed, none is.
function b32 Prog_PathOnline(const Prog_State *state, String path)
{
    b32 lettered = path.length >= 2 && path.value[1] == ':';
    b32 removable = Prog__StartsAt(path, 0, S("/media/")) || Prog__StartsAt(path, 0, S("/run/media/")) || Prog__StartsAt(path, 0, S("/mnt/"));
    if (!state->drives.header.count || !(lettered || removable)) return true;
    for (u64 i = 0; i < state->drives.header.count; ++i) {
        String root = state->drives.v[i].path;
        while (root.length && root.value[root.length - 1] == '/') --root.length;
        if (path.length >= root.length && Prog__EqualFolded(StrFromTo(path, 0, (i64)root.length), root) && (path.length == root.length || path.value[root.length] == '/')) return true;
    }
    return false;
}

// A recent folder left out of the sidebar and Go To: a hidden one while hidden items are, or one on a drive that is not there now.
function b32 Prog_RecentLeftOut(const Prog_State *state, u64 index)
{
    b32 shows = state->currentPanel == state->activePanel ? state->showHidden : state->panels[state->activePanel].showHidden;
    return (!shows && Prog_RecentFaded(state, index)) || !Prog_PathOnline(state, state->recents.v[index]);
}

// The recents the sidebar and Go To show, as indices into them in their order: the best Prog_ShownRecentCount folders, the three opened last
// always among them, so that a folder just opened shows whatever it ranks.
function u32 Prog_ShownRecents(const Prog_State *state, u64 *indices)
{
    u64 latest[3] = { UINT64_MAX, UINT64_MAX, UINT64_MAX };
    for (u64 i = 0; i < state->recents.header.count; ++i) {
        if (state->recentRanks[i].file || Prog_RecentLeftOut(state, i)) continue;
        u32 ordinal = state->recentRanks[i].ordinal;
        for (u32 at = 0; at < 3; ++at) {
            if (latest[at] != UINT64_MAX && state->recentRanks[latest[at]].ordinal >= ordinal) continue;
            for (u32 move = 2; move > at; --move) latest[move] = latest[move - 1];
            latest[at] = i;
            break;
        }
    }
    u32 count = 0, pending = (u32)((latest[0] != UINT64_MAX) + (latest[1] != UINT64_MAX) + (latest[2] != UINT64_MAX));
    for (u64 i = 0; i < state->recents.header.count && count < Prog_ShownRecentCount; ++i) {
        if (state->recentRanks[i].file || Prog_RecentLeftOut(state, i)) continue;
        b32 last = i == latest[0] || i == latest[1] || i == latest[2];
        if (last) --pending;
        // The ones still to come keep their places.
        if (last || count + pending < Prog_ShownRecentCount) indices[count++] = i;
    }
    return count;
}

function void Prog__SaveList(Prog_State *state, String file, ArrayStr list)
{
    if (!state->configDirectory.length) return;
    u32 which = Prog__SaveSlot(file);
    if (state->saving[which]) {
        state->saveAgain[which] = true;
        return;
    }
    state->saving[which] = true;
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Save;
    input->path = Prog_JoinPath(&job->arena, state->configDirectory, file);
    input->name = which == 1 ? Prog__RecentsText(&job->arena, state) : Prog__ListText(&job->arena, list);
    job->input = input;
    P_JobSubmit(job);
}

function void Prog__LoadList(Prog_State *state, String file, Arena *arena, ArrayStr *list, b32 *found)
{
    Scratch scratch = GetScratch();
    String text = {0};
    *found = P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, file), &text);
    ArrayStr lines = StrSplit(scratch.arena, text, '\n');
    u64 kept = 0;
    for (u64 i = 0; i < lines.header.count; ++i) {
        String line = lines.v[i];
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        if (line.length) lines.v[kept++] = line;
    }
    Prog__SetList(arena, list, lines.v, kept);
    ReleaseScratch(scratch);
}

// Lines without a rank, from before the recents had one, keep their order as the most opened.
function void Prog__LoadRecents(Prog_State *state)
{
    Scratch scratch = GetScratch();
    Arena lines = {0};
    ArrayStr paths = {0};
    b32 found = false;
    Prog__LoadList(state, Prog_RecentsFile, &lines, &paths, &found);
    u64 count = paths.header.count;
    Prog_RecentRank *ranks = ArenaPushArray(scratch.arena, Max(count, (u64)1), Prog_RecentRank);
    for (u64 i = 0; i < count; ++i) {
        String line = paths.v[i];
        Index tab = StrIndexChar(line, '\t', SSF_None);
        ranks[i] = (Prog_RecentRank){ .ordinal = (u32)(count - i), .score = Prog_RecentScoreMax };
        if (!tab.exists) continue;
        paths.v[i] = StrFromTo(line, 0, (i64)tab.value);
        String numbers = StrFrom(line, tab.value + 1);
        u64 values[5] = {0};
        for (u32 n = 0; n < 5 && Prog__TakeNumber(&numbers, &values[n]); ++n) {}
        ranks[i] = (Prog_RecentRank){ .ordinal = (u32)values[2], .score = (u8)Min(values[0], (u64)Prog_RecentScoreMax), .decay = (u8)Min(values[1], (u64)10),
                                      .file = values[3] != 0, .hidden = values[4] != 0 };
    }
    state->recentOrdinal = 0;
    Prog__SetRecents(state, paths.v, ranks, count);
    ArenaRelease(&lines);
    ReleaseScratch(scratch);
}

// The palette's Quick group before anything is starred: the most used commands.
function void Prog__DefaultQuick(Prog_State *state)
{
    local_persist const String quick[] = {
        SInit("ToggleExpandFolder"), SInit("TogglePreview"), SInit("SplitRight"), SInit("SplitBottom"), SInit("TabNew"), SInit("TabClose"), SInit("OpenOptions"), SInit("ToggleHidden"),
    };
    Prog__SetList(&state->quickArena, &state->quickCommands, quick, CArrayCount(quick));
}

// Loads the bookmarks and recents saved in `configDirectory`, which keeps them from then on; the drive index is kept in `cacheDirectory`.
// Without a bookmarks file, the user's desktop, documents, downloads and pictures start as bookmarks.
function void Prog_LoadSettings(Prog_State *state, String configDirectory, String cacheDirectory)
{
    ArenaClear(&state->settingsArena);
    state->configDirectory = StrCopy(&state->settingsArena, configDirectory);
    state->cacheDirectory = StrCopy(&state->settingsArena, cacheDirectory);
    if (!configDirectory.length) return;
    b32 found = false;
    Prog__LoadList(state, Prog_BookmarksFile, &state->bookmarkArena, &state->bookmarks, &found);
    Prog_LoadLayouts(state);
    // The sidebar's groups stay open or closed from the last run, whatever the window starts with.
    Scratch session = GetScratch();
    String text = {0};
    if (P_ReadFile(session.arena, Prog_JoinPath(session.arena, configDirectory, Prog_SessionFile), &text)) {
        // The window's size comes back whatever the start opens, the last run's panels or the folders it was given.
        text = Prog__ApplyWindowLine(text);
        ArrayStr lines = StrSplit(session.arena, text, '\n');
        for (u64 l = 0; l < lines.header.count; ++l) {
            String line = lines.v[l];
            if (line.length < 7 || !StrIsEqual(StrFromTo(line, 0, 7), S("groups "))) continue;
            line = StrFrom(line, 7);
            for (u32 g = 0; g < Prog_Group_Count; ++g) {
                u64 toggled = 0;
                if (Prog__TakeNumber(&line, &toggled)) state->groupToggled[g] = toggled != 0;
            }
        }
    }
    ReleaseScratch(session);
    if (!found) {
        Scratch scratch = GetScratch();
        P_ArrayPlace places = P_UserPlaces(scratch.arena);
        String *paths = ArenaPushArray(scratch.arena, places.header.count + 1, String);
        u64 count = 0;
        for (u64 i = 0; i < places.header.count; ++i) {
            String name = places.v[i].name;
            if (StrIsEqual(name, S("Desktop")) || StrIsEqual(name, S("Documents")) || StrIsEqual(name, S("Downloads")) || StrIsEqual(name, S("Pictures"))) paths[count++] = places.v[i].path;
        }
        Prog__SetList(&state->bookmarkArena, &state->bookmarks, paths, count);
        ReleaseScratch(scratch);
    }
    Prog__LoadRecents(state);
    Prog__LoadList(state, Prog_FolderViewsFile, &state->folderViewArena, &state->folderViews, &found);
    Prog__LoadList(state, Prog_MenuPinsFile, &state->menuPinArena, &state->menuPins, &found);
    Prog__LoadList(state, Prog_QuickCommandsFile, &state->quickArena, &state->quickCommands, &found);
    if (!found) Prog__DefaultQuick(state);
    Prog_ReloadSettingsFile(state);
    // Recents kept only for a run start empty.
    if (state->storeRecents != Prog_StoreRecents_Persistent) Prog__SetRecents(state, NULL, NULL, 0);
    P_DirectoryWatchStop(&state->configWatch);
    P_DirectoryWatchStart(&state->configWatch, configDirectory, state->jobs.wake, state->jobs.wakeUser);
}

// What Settings.conf and Schemes.conf hold, as a hash, which tells whether either has changed since they were applied.
function u64 Prog__SettingsHash(Prog_State *state, String *settings, Arena *arena)
{
    String schemes = {0};
    *settings = (String){0};
    if (state->configDirectory.length) {
        P_ReadFile(arena, Prog_JoinPath(arena, state->configDirectory, S("Settings.conf")), settings);
        P_ReadFile(arena, Prog_JoinPath(arena, state->configDirectory, S("Schemes.conf")), &schemes);
    }
    u64 hash = UINT64_C(14695981039346656037);
    for (u64 i = 0; i < settings->length; ++i) hash = (hash ^ (u8)settings->value[i]) * UINT64_C(1099511628211);
    hash = (hash ^ 0xFF) * UINT64_C(1099511628211);
    for (u64 i = 0; i < schemes.length; ++i) hash = (hash ^ (u8)schemes.value[i]) * UINT64_C(1099511628211);
    return hash;
}

// Settings.conf, or the defaults without it, with the custom schemes it can name read again first.
function void Prog_ReloadSettingsFile(Prog_State *state)
{
    Scratch scratch = GetScratch();
    String text = {0};
    state->settingsHash = Prog__SettingsHash(state, &text, scratch.arena);
    Prog_LoadSchemes(state);
    Prog_ApplySettings(state, text);
    ReleaseScratch(scratch);
}

function b32 Prog_IsBookmarked(const Prog_State *state, String path)
{
    for (u64 i = 0; i < state->bookmarks.header.count; ++i) {
        if (StrIsEqual(state->bookmarks.v[i], path)) return true;
    }
    return false;
}

// Adds `path` at the end of the bookmarks, or removes it when it is one.
function void Prog_ToggleBookmark(Prog_State *state, String path)
{
    if (!path.length) return;
    Scratch scratch = GetScratch();
    u64 count = state->bookmarks.header.count;
    String *paths = ArenaPushArray(scratch.arena, count + 1, String);
    u64 kept = 0;
    for (u64 i = 0; i < count; ++i) {
        if (!StrIsEqual(state->bookmarks.v[i], path)) paths[kept++] = state->bookmarks.v[i];
    }
    if (kept == count) paths[kept++] = path;
    Prog__SetList(&state->bookmarkArena, &state->bookmarks, paths, kept);
    ReleaseScratch(scratch);
    Prog__SaveList(state, Prog_BookmarksFile, state->bookmarks);
}

// Puts `paths` into the bookmarks before position `at`; ones already there move.
function void Prog_PinBookmarks(Prog_State *state, const ArrayStr *paths, u64 at)
{
    if (!paths->header.count) return;
    Scratch scratch = GetScratch();
    u64 count = state->bookmarks.header.count;
    String *result = ArenaPushArray(scratch.arena, count + paths->header.count, String);
    u64 kept = 0;
    at = Min(at, count);
    for (u64 i = 0; i <= count; ++i) {
        if (i == at) {
            for (u64 j = 0; j < paths->header.count; ++j) result[kept++] = paths->v[j];
        }
        if (i == count) break;
        b32 moving = false;
        for (u64 j = 0; j < paths->header.count && !moving; ++j) moving = StrIsEqual(state->bookmarks.v[i], paths->v[j]);
        if (!moving) result[kept++] = state->bookmarks.v[i];
    }
    Prog__SetList(&state->bookmarkArena, &state->bookmarks, result, kept);
    ReleaseScratch(scratch);
    Prog__SaveList(state, Prog_BookmarksFile, state->bookmarks);
}

// Adds `path` and the folders listed in it to the folders Go To searches.
#define Prog_MaxKnownFolders 50000
function void Prog__NoteFolders(Prog_State *state, String path, const Prog_Entry *entries, u64 count)
{
    // ponytail: starts over when full; an index that ages out old folders could keep the busy ones.
    if (state->knownFolders.header.count + count + 1 > Prog_MaxKnownFolders) {
        ArenaClear(&state->knownArena);
        state->knownFolders = (ArrayStr){0};
        state->knownSet = (Prog_PathSet){0};
    }
    for (u64 i = 0; i <= count; ++i) {
        // Of an expanded listing only the folder's own, since the drive index has the rest and a big tree has hundreds of thousands.
        if (i < count && (entries[i].file.kind != P_FileKind_Directory || StrIndexChar(entries[i].file.name, '/', SSF_None).exists)) continue;
        String folder = i < count ? Prog_JoinPath(&state->frameArena, path, entries[i].file.name) : path;
        if (MapsGetPtrNull(state->knownSet, folder)) continue;
        folder = StrCopy(&state->knownArena, folder);
        MapsPut(&state->knownArena, state->knownSet, folder, true);
        ArrayPush(&state->knownArena, state->knownFolders, folder);
    }
}

// Bookmarks and recents at or inside items Filey moved or renamed from `from` to `to` follow them, as Explorer's pinned folders do.
function void Prog__FollowMoved(Prog_State *state, const String *from, const String *to, u64 count)
{
    Scratch scratch = GetScratch();
    for (u32 list = 0; list < 2; ++list) {
        ArrayStr paths = list ? state->recents : state->bookmarks;
        String *followed = ArenaPushArray(scratch.arena, Max(paths.header.count, (u64)1), String);
        b32 changed = false;
        for (u64 i = 0; i < paths.header.count; ++i) {
            String path = followed[i] = paths.v[i];
            for (u64 m = 0; m < count; ++m) {
                String old = from[m];
                b32 inside = path.length >= old.length && StrIsEqual(StrFromTo(path, 0, (i64)old.length), old) &&
                             (path.length == old.length || path.value[old.length] == '/' || (old.length && old.value[old.length - 1] == '/'));
                if (!inside) continue;
                followed[i] = StrFormat(scratch.arena, "%S%S", to[m], StrFrom(path, old.length));
                changed = true;
                break;
            }
        }
        if (!changed) continue;
        if (list) {
            Prog__SetRecents(state, followed, state->recentRanks, paths.header.count);
            if (state->storeRecents == Prog_StoreRecents_Persistent) Prog__SaveList(state, Prog_RecentsFile, state->recents);
        } else {
            Prog__SetList(&state->bookmarkArena, &state->bookmarks, followed, paths.header.count);
            Prog__SaveList(state, Prog_BookmarksFile, state->bookmarks);
        }
    }
    ReleaseScratch(scratch);
}

// Counts `path` opened among the recents, and saves them when they are kept across runs.
function void Prog__AddRecent(Prog_State *state, String path, b32 file, b32 hidden)
{
    if (state->storeRecents == Prog_StoreRecents_Never) return;
    Scratch scratch = GetScratch();
    u64 count = state->recents.header.count;
    String *paths = ArenaPushArray(scratch.arena, count + 1, String);
    Prog_RecentRank *ranks = ArenaPushArray(scratch.arena, count + 1, Prog_RecentRank);
    if (count) {
        MemCopy(paths, state->recents.v, count * sizeof(String));
        MemCopy(ranks, state->recentRanks, count * sizeof(Prog_RecentRank));
    }
    u64 at = 0;
    while (at < count && !StrIsEqual(paths[at], path)) ++at;
    if (at == count) ranks[count++] = (Prog_RecentRank){ .score = Prog_RecentScoreNew - 1 };
    paths[at] = path;
    ranks[at].score = (u8)Min(ranks[at].score + 1, Prog_RecentScoreMax);
    ranks[at].decay = 0;
    ranks[at].ordinal = ++state->recentOrdinal;
    ranks[at].file = (b8)file;
    ranks[at].hidden = (b8)hidden;
    // Every tenth open, all the others drop a point, so that those no longer opened make way.
    if (ranks[at].ordinal % 10 == 0) {
        for (u64 i = 0; i < count; ++i) if (i != at && ranks[i].score) --ranks[i].score;
    }
    Prog__SetRecents(state, paths, ranks, count);
    ReleaseScratch(scratch);
    if (state->storeRecents == Prog_StoreRecents_Persistent) Prog__SaveList(state, Prog_RecentsFile, state->recents);
}

// The recents right inside `folder` were shown there without being opened, but for `from`, the one just left.
function void Prog__DecayRecents(Prog_State *state, String folder, String from)
{
    b32 changed = false;
    for (u64 i = 0; i < state->recents.header.count; ++i) {
        String path = state->recents.v[i];
        if (StrIsEqual(path, from) || !StrIsEqual(Prog_UpPath(path), folder)) continue;
        Prog_RecentRank *rank = &state->recentRanks[i];
        if (rank->decay < 10) ++rank->decay;
        else if (rank->score) --rank->score;
        changed = true;
    }
    if (!changed) return;
    Prog__SetRecents(state, state->recents.v, state->recentRanks, state->recents.header.count);
    if (state->storeRecents == Prog_StoreRecents_Persistent) Prog__SaveList(state, Prog_RecentsFile, state->recents);
}

// The user's home folder; empty when there is none.
function String Prog_HomePath(Arena *arena)
{
    Scratch scratch = GetScratchEx(arena);
    P_ArrayPlace places = P_UserPlaces(scratch.arena);
    String result = {0};
    for (u64 i = 0; i < places.header.count && !result.length; ++i) if (places.v[i].home) result = StrCopy(arena, places.v[i].path);
    ReleaseScratch(scratch);
    return result;
}

// A start without folders to open: the last run's panels and tabs, or the place the settings name.
function void Prog_StartUp(Prog_State *state)
{
    // A layout that is gone leaves the last run's panels.
    String layout = StrMake((const char *)state->startupLayout, state->startupLayoutLength);
    if (layout.length && Prog_ApplyLayout(state, layout)) return;
    if (state->startup == Prog_Startup_LastSession || layout.length) {
        Prog_LoadSession(state);
        return;
    }
    Scratch scratch = GetScratch();
    String home = state->startup == Prog_Startup_Home ? Prog_HomePath(scratch.arena) : (String){0};
    Prog_RequestDirectory(state, home.length ? home : Prog_ThisPC, true, 0);
    ReleaseScratch(scratch);
}

function b32 Prog_GroupOpen(const Prog_State *state, Prog_Group group)
{
    b32 open = group == Prog_Group_Bookmarks || group == Prog_Group_Storage;
    return open != (state->groupToggled[group] != 0);
}

function b32 Prog_Init(Prog_State *state, String path, P_JobWakeCallback *wake, void *user)
{
    if (!P_JobsInit(&state->jobs, &state->jobsArena, 0, wake, user)) {
        ArenaRelease(&state->jobsArena);
        return false;
    }
    // Four for thumbnails, which decode large photos side by side; a spinning disk gets one of them at a time.
    if (!P_JobsInit(&state->thumbnailJobs, &state->thumbnailJobsArena, 4, wake, user)) {
        P_JobsShutdown(&state->jobs);
        ArenaRelease(&state->jobsArena);
        ArenaRelease(&state->thumbnailJobsArena);
        return false;
    }
    // Settings loaded after replace it with the starred commands.
    Prog__DefaultQuick(state);
    Prog__EnsureTab(state);
    Arena temporary = {0};
    // This PC is no folder to resolve.
    String absolute = StrIsEqual(path, Prog_ThisPC) ? path : P_AbsolutePath(&temporary, path);
    if (absolute.length) Prog_RequestDirectory(state, absolute, true, 0);
    else state->error = S("Cannot resolve the initial directory");
    ArenaRelease(&temporary);
    return true;
}

// `{0}` state has one tab: the shown one.
function void Prog__EnsureTab(Prog_State *state)
{
    Prog__EnsurePanel(state);
    if (!state->tabs) state->tabs = ArenaPushArray(&state->tabArena, Prog_MaxTabs, Prog_Tab);
    if (state->tabCount) return;
    state->tabCount = 1;
    state->tabs[0].id = ++state->nextTabId;
}

// `{0}` state has one panel, current and active, filling the whole layout.
function void Prog__EnsurePanel(Prog_State *state)
{
    if (state->panelCount) return;
    state->panelCount = 1;
    state->panels[0].id = ++state->nextPanelId;
    state->currentPanel = state->activePanel = 0;
    state->splits[0] = (Prog_Split){ .kind = Prog_SplitKind_Panel, .panel = state->panels[0].id };
}

function u64 Prog__PanelId(const Prog_State *state)
{
    return state->panelCount ? state->panels[state->currentPanel].id : 0;
}

function u32 Prog_PanelIndex(const Prog_State *state, u64 id)
{
    for (u32 i = 0; i < state->panelCount; ++i) {
        if (state->panels[i].id == id) return i;
    }
    return UINT32_MAX;
}

function void Prog__SwapPanel(Prog_State *state, Prog_Panel *panel)
{
#define Prog__SwapField(type, name) { type swap = state->name; state->name = panel->name; panel->name = swap; }
    Prog_TabFields(Prog__SwapField)
    Prog_PanelFields(Prog__SwapField)
#undef Prog__SwapField
}

// Makes panel `index` current: its fields move into Prog_State and the current one's go back to its slot.
// Other panels are current only briefly, to build their UI or take their jobs' results; the active one is current between frames.
function void Prog__UsePanel(Prog_State *state, u32 index)
{
    Prog__EnsurePanel(state);
    if (index >= state->panelCount || index == state->currentPanel) return;
    Prog__SwapPanel(state, &state->panels[state->currentPanel]);
    Prog__SwapPanel(state, &state->panels[index]);
    state->currentPanel = index;
}

// Gives panel `index` the keyboard: a rename commits and a menu closes, since both belong to the panel they were in.
function void Prog__ActivatePanel(Prog_State *state, u32 index)
{
    Prog__EnsurePanel(state);
    if (index >= state->panelCount) return;
    if (index != state->activePanel) {
        if (state->renaming) Prog__EndRename(state, true);
        Prog_CloseMenu(state);
        state->hasHoverRow = false;
    }
    Prog__UsePanel(state, index);
    state->activePanel = index;
}

// Stops and frees what the current panel holds, leaving it empty.
// The current panel's Miller column listing `path` this frame. Its listing is asked for when there is none yet, or when it comes back into view
// a while after the last; the items listed before stay until the new ones come. The one shown least recently is taken for a new folder.
function Prog_MillerColumn *Prog_MillerList(Prog_State *state, String path)
{
    u64 now = PlatformNowMicroseconds();
    if (!state->millerColumns) state->millerColumns = ArenaPushStruct(&state->millerArena, Prog_MillerColumns);
    Prog_MillerColumn *column = NULL, *oldest = NULL;
    for (u32 i = 0; i < Prog_MillerSlots && !column; ++i) {
        Prog_MillerColumn *slot = &state->millerColumns->v[i];
        if (slot->path.length && StrIsEqual(slot->path, path) && slot->hidden == state->showHidden) column = slot;
        else if (!oldest || slot->usedFrame < oldest->usedFrame) oldest = slot;
    }
    if (!column) {
        column = oldest;
        if (column->job) P_RequestCancel(&column->job->cancelToken);
        ArenaRelease(&column->arena);
        ArenaClear(&column->pathArena);
        *column = (Prog_MillerColumn){ .pathArena = column->pathArena, .hidden = state->showHidden };
        column->path = StrCopy(&column->pathArena, path);
    }
    b32 back = column->usedFrame + 1 < UI->frameIndex;
    column->usedFrame = UI->frameIndex;
    if (back) column->revealed = false;
    if (!column->job && (!column->listed || (back && now - Min(column->listedAt, now) > UINT64_C(2000000)))) {
        P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
        input->kind = Prog_JobKind_FilesList;
        input->panel = Prog__PanelId(state);
        input->miller = true;
        input->path = StrCopy(&job->arena, path);
        input->showHidden = state->showHidden;
        input->showSystem = state->showSystem;
        job->input = input;
        column->job = job;
        P_JobSubmit(job);
    }
    return column;
}

function void Prog__ReleaseCurrentPanel(Prog_State *state)
{
    for (u32 i = 0; state->millerColumns && i < Prog_MillerSlots; ++i) {
        Prog_MillerColumn *column = &state->millerColumns->v[i];
        if (column->job) P_RequestCancel(&column->job->cancelToken);
        ArenaRelease(&column->arena);
        ArenaRelease(&column->pathArena);
    }
    ArenaRelease(&state->millerArena);
    if (state->directoryJob) P_RequestCancel(&state->directoryJob->cancelToken);
    if (state->measureJob) P_RequestCancel(&state->measureJob->cancelToken);
    P_DirectoryWatchStop(&state->watch);
    ArenaRelease(&state->watch.arena);
    ArenaRelease(&state->directoryArena);
    ArenaRelease(&state->historyArena);
    ArenaRelease(&state->viewArena);
    ArenaRelease(&state->errorArena);
    ArenaRelease(&state->pickArena);
    Prog__ReleasePreview(&state->previewArena, state->previewTexture, &state->model, &state->pdf, state->previewJob, state->hexPageJob);
    for (u32 i = 0; i < state->tabCount; ++i) {
        if (i != state->activeTab) Prog__ReleaseTab(&state->tabs[i]);
    }
    ArenaRelease(&state->tabArena);
#define Prog__ZeroField(type, name) state->name = (type){0};
    Prog_TabFields(Prog__ZeroField)
    Prog_PanelFields(Prog__ZeroField)
#undef Prog__ZeroField
}

function u32 Prog__SplitOf(const Prog_State *state, u64 panel)
{
    for (u32 i = 0; i < Prog_MaxSplits; ++i) {
        if (state->splits[i].kind == Prog_SplitKind_Panel && state->splits[i].panel == panel) return i;
    }
    return UINT32_MAX;
}

function u32 Prog__FreeSplit(const Prog_State *state)
{
    for (u32 i = 1; i < Prog_MaxSplits; ++i) {
        if (state->splits[i].kind == Prog_SplitKind_None) return i;
    }
    return UINT32_MAX;
}

// The first panel of the subtree at `node`, in reading order.
function u64 Prog__FirstPanelOf(const Prog_State *state, u32 node)
{
    while (node < Prog_MaxSplits && state->splits[node].kind != Prog_SplitKind_Panel && state->splits[node].kind != Prog_SplitKind_None) node = state->splits[node].first;
    return node < Prog_MaxSplits ? state->splits[node].panel : 0;
}

// The panels below layout node `node` in reading order, left to right and top to bottom, into `out`; returns how many there are.
function u32 Prog__PanelsInOrder(const Prog_State *state, u32 node, u64 *out, u32 count)
{
    if (node >= Prog_MaxSplits || count >= Prog_MaxPanels) return count;
    const Prog_Split *split = &state->splits[node];
    if (split->kind == Prog_SplitKind_Panel) {
        out[count++] = split->panel;
        return count;
    }
    if (split->kind != Prog_SplitKind_Horizontal && split->kind != Prog_SplitKind_Vertical) return count;
    count = Prog__PanelsInOrder(state, split->first, out, count);
    return Prog__PanelsInOrder(state, split->second, out, count);
}

// How many panels the splits of `kind` at `node` lay side by side: a split of that kind adds up its two sides, and anything else is one.
function u32 Prog__RunLength(const Prog_State *state, u32 node, Prog_SplitKind kind)
{
    if (node >= Prog_MaxSplits) return 0;
    const Prog_Split *split = &state->splits[node];
    return split->kind == kind ? Prog__RunLength(state, split->first, kind) + Prog__RunLength(state, split->second, kind) : 1;
}

// Gives each of the panels side by side in the run of splits that `node` heads the same room, and with `all` the runs inside them too.
function void Prog__EvenSplits(Prog_State *state, u32 node, b32 all)
{
    if (node >= Prog_MaxSplits) return;
    Prog_Split *split = &state->splits[node];
    if (split->kind != Prog_SplitKind_Horizontal && split->kind != Prog_SplitKind_Vertical) return;
    u32 first = Prog__RunLength(state, split->first, (Prog_SplitKind)split->kind), second = Prog__RunLength(state, split->second, (Prog_SplitKind)split->kind);
    split->ratio = (f32)first / (f32)Max(first + second, 1u);
    if (all || (split->first < Prog_MaxSplits && state->splits[split->first].kind == split->kind)) Prog__EvenSplits(state, split->first, all);
    if (all || (split->second < Prog_MaxSplits && state->splits[split->second].kind == split->kind)) Prog__EvenSplits(state, split->second, all);
}

// The split that holds layout node `node`, or UINT32_MAX for the root.
function u32 Prog__SplitParent(const Prog_State *state, u32 node)
{
    for (u32 i = 0; i < Prog_MaxSplits; ++i) {
        const Prog_Split *split = &state->splits[i];
        if ((split->kind == Prog_SplitKind_Horizontal || split->kind == Prog_SplitKind_Vertical) && (split->first == node || split->second == node)) return i;
    }
    return UINT32_MAX;
}

// Gives the panels beside the active one in its row or column the same room, or with `all` every panel of the layout in its own.
function void Prog_EvenPanels(Prog_State *state, b32 all)
{
    if (all) {
        Prog__EvenSplits(state, 0, true);
        return;
    }
    // A panel's inspector is laid out inside it.
    const Prog_Panel *panel = &state->panels[state->activePanel];
    u32 run = Prog__SplitParent(state, Prog__SplitOf(state, panel->inspectorOf ? panel->inspectorOf : panel->id));
    for (u32 up; run != UINT32_MAX && (up = Prog__SplitParent(state, run)) != UINT32_MAX && state->splits[up].kind == state->splits[run].kind;) run = up;
    if (run != UINT32_MAX) Prog__EvenSplits(state, run, false);
}

// Splits panel `index` in two along `kind`, the new panel `before` it or after it, and makes the new one, which shows `path`, active.
// Returns the new panel's index, or UINT32_MAX when there is no room.
function u32 Prog_SplitPanel(Prog_State *state, u32 index, Prog_SplitKind kind, b32 before, String path)
{
    Prog__EnsureTab(state);
    if (state->panelCount >= Prog_MaxPanels || index >= state->panelCount || !path.length) return UINT32_MAX;
    u32 leaf = Prog__SplitOf(state, state->panels[index].id);
    u32 kept = Prog__FreeSplit(state);
    if (leaf == UINT32_MAX || kept == UINT32_MAX) return UINT32_MAX;
    state->splits[kept] = state->splits[leaf];
    u32 added = Prog__FreeSplit(state);
    if (added == UINT32_MAX) {
        state->splits[kept] = (Prog_Split){0};
        return UINT32_MAX;
    }
    u32 at = state->panelCount++;
    // The new panel starts with the order and hidden-item choice of the one it splits.
    b32 current = index == state->currentPanel;
    state->panels[at] = (Prog_Panel){
        .id = ++state->nextPanelId,
        .splitMicroseconds = PlatformNowMicroseconds(),
        .sort = current ? state->sort : state->panels[index].sort,
        .descending = current ? state->descending : state->panels[index].descending,
        .folderSort = current ? state->folderSort : state->panels[index].folderSort,
        .columnSet = current ? state->columnSet : state->panels[index].columnSet,
        .showHidden = current ? state->showHidden : state->panels[index].showHidden,
        .zoom = current ? state->zoom : state->panels[index].zoom,
    };
    state->splits[added] = (Prog_Split){ .kind = Prog_SplitKind_Panel, .panel = state->panels[at].id };
    state->splits[leaf] = (Prog_Split){ .kind = (u8)kind, .first = (u8)(before ? added : kept), .second = (u8)(before ? kept : added), .ratio = 0.5f };
    Prog__ActivatePanel(state, at);
    Prog__EnsureTab(state);
    Prog_RequestDirectory(state, path, true, 0);
    return at;
}

// The panel the inspector of panel `owner` (an id) lists folders in, made when `make` asks and there is room; UINT32_MAX without one.
// It starts with the owner's order and hidden-item choice, in Details.
function u32 Prog_InspectorPanel(Prog_State *state, u64 owner, b32 make)
{
    for (u32 i = 0; i < state->panelCount; ++i) {
        if (state->panels[i].inspectorOf == owner) return i;
    }
    u32 from = Prog_PanelIndex(state, owner);
    if (!make || from == UINT32_MAX || state->panelCount >= Prog_MaxPanels) return UINT32_MAX;
    b32 current = from == state->currentPanel;
    Prog_ViewSettings view = current ? state->inspectorView : state->panels[from].inspectorView;
    u32 at = state->panelCount++;
    state->panels[at] = (Prog_Panel){
        .id = ++state->nextPanelId,
        .inspectorOf = owner,
        .zoom = view.zoom, .sort = view.sort, .descending = view.descending, .folderSort = view.folders, .columnSet = view.columns,
        .showHidden = current ? state->showHidden : state->panels[from].showHidden,
    };
    return at;
}

// Lists `path`, a folder in the current panel, in its inspector's panel.
function void Prog_ListInInspector(Prog_State *state, String path)
{
    u64 owner = state->panels[state->currentPanel].id;
    u32 inner = Prog_InspectorPanel(state, owner, true);
    if (inner == UINT32_MAX) return;
    Prog__UsePanel(state, inner);
    Prog__EnsureTab(state);
    Prog_RequestDirectory(state, path, true, 0);
    Prog__UsePanel(state, Prog_PanelIndex(state, owner));
}

// Closes the inspector's panel of panel `owner` (an id), if it has one; the keyboard goes back to the owner from it.
function void Prog_DropInspectorPanel(Prog_State *state, u64 owner)
{
    u32 index = Prog_InspectorPanel(state, owner, false);
    if (index == UINT32_MAX) return;
    u64 active = state->panels[state->activePanel].id, current = state->panels[state->currentPanel].id;
    if (active == state->panels[index].id) {
        if (state->renaming) Prog__EndRename(state, false);
        Prog_CloseMenu(state);
        active = owner;
    }
    if (current == state->panels[index].id) current = owner;
    Prog__UsePanel(state, index);
    // The owner keeps how it listed folders, for the next time.
    Prog_ViewSettings view = Prog_CurrentView(state);
    Prog__UsePanel(state, Prog_PanelIndex(state, owner));
    state->inspectorView = view;
    Prog__UsePanel(state, index);
    Prog__ReleaseCurrentPanel(state);
    Prog__UsePanel(state, Prog_PanelIndex(state, current));
    MemMove(&state->panels[index], &state->panels[index + 1], (state->panelCount - index - 1) * sizeof(Prog_Panel));
    --state->panelCount;
    state->panels[state->panelCount] = (Prog_Panel){0};
    state->currentPanel = Prog_PanelIndex(state, current);
    state->activePanel = Prog_PanelIndex(state, active);
}

// Closes panel `index`, whose neighbor in the layout takes its space; the last panel stays.
function void Prog_ClosePanel(Prog_State *state, u32 index)
{
    Prog__EnsurePanel(state);
    if (state->panelCount <= 1 || index >= state->panelCount) return;
    u64 id = state->panels[index].id;
    u32 leaf = Prog__SplitOf(state, id), parent = UINT32_MAX;
    for (u32 i = 0; i < Prog_MaxSplits && leaf != UINT32_MAX; ++i) {
        Prog_Split *split = &state->splits[i];
        if ((split->kind == Prog_SplitKind_Horizontal || split->kind == Prog_SplitKind_Vertical) && (split->first == leaf || split->second == leaf)) parent = i;
    }
    if (parent == UINT32_MAX) return;
    Prog_DropInspectorPanel(state, id);
    index = Prog_PanelIndex(state, id);
    // The sibling's subtree moves up into the parent's node.
    u32 sibling = state->splits[parent].first == leaf ? state->splits[parent].second : state->splits[parent].first;
    state->splits[parent] = state->splits[sibling];
    state->splits[sibling] = state->splits[leaf] = (Prog_Split){0};
    u32 next = Prog_PanelIndex(state, Prog__FirstPanelOf(state, parent));
    // Released while current, so that its fields are live; the slot left behind is empty.
    if (state->renaming && index == state->activePanel) Prog__EndRename(state, false);
    Prog_CloseMenu(state);
    Prog__UsePanel(state, index);
    Prog__ReleaseCurrentPanel(state);
    Prog__UsePanel(state, next);
    MemMove(&state->panels[index], &state->panels[index + 1], (state->panelCount - index - 1) * sizeof(Prog_Panel));
    --state->panelCount;
    state->panels[state->panelCount] = (Prog_Panel){0};
    if (state->currentPanel > index) --state->currentPanel;
    state->activePanel = state->currentPanel;
}

// Moves, or with `copy` copies, a tab to where it was dropped; the tab takes its folder and history there, and the place it lands becomes active.
// Its zoom and focused item come along; ponytail: the rest of the selection and the scroll start over, carry the whole Prog_Tab across when that matters.
function String Prog__TabTraits(Prog_State *state, u32 slot);

function void Prog_DropTab(Prog_State *state, Prog_TabDropTarget drop)
{
    u32 from = Prog_PanelIndex(state, drop.fromPanel), into = Prog_PanelIndex(state, drop.panel);
    if (from == UINT32_MAX) return;
    Prog__ActivatePanel(state, from);
    if (drop.fromSlot >= state->tabCount) return;
    String path = StrCopy(&state->frameArena, Prog_TabPath(state, drop.fromSlot));
    if (!path.length) return;
    String traits = Prog__TabTraits(state, drop.fromSlot);
    b32 shown = drop.fromSlot == state->activeTab;
    ArrayStr history = shown ? state->history : state->tabs[drop.fromSlot].history;
    u64 historyAt = shown ? state->historyAt : state->tabs[drop.fromSlot].historyAt;
    String *saved = ArenaPushArray(&state->frameArena, history.header.count + 1, String);
    for (u64 i = 0; i < history.header.count; ++i) saved[i] = StrCopy(&state->frameArena, history.v[i]);
    // A tab that has not listed its folder yet has no history of it.
    if (historyAt >= history.header.count || !StrIsEqual(saved[historyAt], path)) {
        saved[0] = path;
        history.header.count = 1;
        historyAt = 0;
    }
    b32 alone = state->tabCount == 1;
    switch (drop.kind) {
        case Prog_TabDrop_Strip:
        case Prog_TabDrop_Into: {
            if (into == UINT32_MAX) return;
            if (into == from && !drop.copy) {
                // Within its strip a tab only changes place.
                if (drop.kind == Prog_TabDrop_Strip) Prog_MoveTab(state, drop.fromSlot, drop.slot > drop.fromSlot ? drop.slot - 1 : drop.slot);
                return;
            }
            Prog__ActivatePanel(state, into);
            u32 at = Min(drop.kind == Prog_TabDrop_Into ? state->tabCount : drop.slot, state->tabCount);
            u32 count = state->tabCount;
            Prog_OpenTab(state, path, at, false);
            if (state->tabCount == count) return;
            Prog_Tab *tab = &state->tabs[at];
            Prog__SetList(&tab->historyArena, &tab->history, saved, history.header.count);
            tab->historyAt = historyAt;
            Prog_SelectTab(state, at);
            Prog_ApplyTabTraits(state, traits);
        } break;
        case Prog_TabDrop_Left:
        case Prog_TabDrop_Right:
        case Prog_TabDrop_Top:
        case Prog_TabDrop_Bottom: {
            // A panel's only tab moving beside the panel would only leave it where it is.
            if (into == UINT32_MAX || (into == from && alone && !drop.copy)) return;
            b32 across = drop.kind == Prog_TabDrop_Left || drop.kind == Prog_TabDrop_Right;
            b32 before = drop.kind == Prog_TabDrop_Left || drop.kind == Prog_TabDrop_Top;
            if (Prog_SplitPanel(state, into, across ? Prog_SplitKind_Horizontal : Prog_SplitKind_Vertical, before, path) == UINT32_MAX) return;
            // The listing on its way finds `path` already in the history, where it adds nothing.
            Prog__SetList(&state->historyArena, &state->history, saved, history.header.count);
            state->historyAt = historyAt;
            Prog_ApplyTabTraits(state, traits);
        } break;
        case Prog_TabDrop_Window: {
            // Over another of the program's windows it goes there, and over anything else into a window of its own.
            if (PlatformOtherWindowAtPointer(StrFormat(&state->frameArena, "%S\t%S", path, traits))) break;
            P_FileError error = Prog_OpenWindow(state, path, traits);
            if (error.code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "New window: %S", error.message);
                return;
            }
        } break;
        default: return;
    }
    if (drop.copy) return;
    // The tab leaves where it was; its panel closes when it was the only one there.
    u64 landed = Prog__PanelId(state);
    from = Prog_PanelIndex(state, drop.fromPanel);
    if (from == UINT32_MAX) return;
    Prog__ActivatePanel(state, from);
    Prog_CloseTab(state, drop.fromSlot);
    u32 back = Prog_PanelIndex(state, landed);
    if (back != UINT32_MAX) Prog__ActivatePanel(state, back);
}

// Activates the panel beside the active one toward (`dx`, `dy`) in the last layout: the nearest one past that edge that overlaps it across, the most overlapping among equally near ones.
function void Prog_FocusPanelToward(Prog_State *state, i32 dx, i32 dy)
{
    Prog__ActivatePanel(state, state->activePanel);
    R_Rect from = state->panelRect;
    u32 best = UINT32_MAX;
    f32 bestGap = 0, bestOverlap = 0;
    for (u32 i = 0; i < state->panelCount; ++i) {
        if (i == state->currentPanel || state->panels[i].inspectorOf) continue;
        R_Rect r = state->panels[i].panelRect;
        f32 gap = dx > 0 ? r.x0 - from.x1 : dx < 0 ? from.x0 - r.x1 : dy > 0 ? r.y0 - from.y1 : from.y0 - r.y1;
        f32 overlap = dx ? MinF32(r.y1, from.y1) - MaxF32(r.y0, from.y0) : MinF32(r.x1, from.x1) - MaxF32(r.x0, from.x0);
        if (gap < -1 || overlap <= 0) continue;
        if (best == UINT32_MAX || gap < bestGap - 1 || (gap <= bestGap + 1 && overlap > bestOverlap)) {
            best = i;
            bestGap = gap;
            bestOverlap = overlap;
        }
    }
    if (best != UINT32_MAX) Prog__ActivatePanel(state, best);
}

// Records a file operation for Undo; strings are copied.
function void Prog__PushStep(Arena *arena, Prog_ArrayUndo *stack, Prog_UndoKind kind, const String *from, const String *to, u64 count)
{
    if (!count) return;
    if (stack->header.count >= Prog_MaxUndo) {
        MemMove(&stack->v[0], &stack->v[1], (stack->header.count - 1) * sizeof(Prog_Undo));
        --stack->header.count;
    }
    Prog_Undo step = { .kind = (u8)kind, .count = count, .to = ArenaPushArray(arena, count, String) };
    if (from) step.from = ArenaPushArray(arena, count, String);
    for (u64 i = 0; i < count; ++i) {
        step.to[i] = StrCopy(arena, to[i]);
        if (from) step.from[i] = StrCopy(arena, from[i]);
    }
    ArrayPush(arena, *stack, step);
}

// A new operation ends what Redo could make again.
function void Prog_PushUndo(Prog_State *state, Prog_UndoKind kind, const String *from, const String *to, u64 count)
{
    if (!count) return;
    ArenaClear(&state->redoArena);
    state->redo = (Prog_ArrayUndo){0};
    Prog__PushStep(&state->undoArena, &state->undo, kind, from, to, count);
}

// Whether `a` and `b` name the same place, whichever separators they use, and ignoring case, as Windows does.
function b32 Prog__SamePath(String a, String b)
{
    while (a.length > 1 && (a.value[a.length - 1] == '/' || a.value[a.length - 1] == '\\')) --a.length;
    while (b.length > 1 && (b.value[b.length - 1] == '/' || b.value[b.length - 1] == '\\')) --b.length;
    if (a.length != b.length) return false;
    for (u64 i = 0; i < a.length; ++i) {
        u8 x = (u8)a.value[i], y = (u8)b.value[i];
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}

// Reverses the newest step of the undo or the redo stack in a job; once it is done, the step back again goes on the other one.
function void Prog__ReverseLast(Prog_State *state, b32 redo)
{
    ArenaClear(&state->errorArena);
    state->error = (String){0};
    Prog_ArrayUndo *stack = redo ? &state->redo : &state->undo;
    if (!stack->header.count) {
        state->error = redo ? S("Nothing to redo") : S("Nothing to undo");
        return;
    }
    Prog_Undo undo = stack->v[--stack->header.count];
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Undo;
    input->panel = Prog__PanelId(state);
    input->redo = redo;
    input->undo = (Prog_Undo){ .kind = undo.kind, .count = undo.count, .to = ArenaPushArray(&job->arena, undo.count, String) };
    if (undo.from) input->undo.from = ArenaPushArray(&job->arena, undo.count, String);
    for (u64 i = 0; i < undo.count; ++i) {
        input->undo.to[i] = StrCopy(&job->arena, undo.to[i]);
        if (undo.from) input->undo.from[i] = StrCopy(&job->arena, undo.from[i]);
    }
    if (!stack->header.count) {
        ArenaClear(redo ? &state->redoArena : &state->undoArena);
        *stack = (Prog_ArrayUndo){0};
    }
    job->input = input;
    P_JobSubmit(job);
}

function void Prog_UndoLast(Prog_State *state)
{
    Prog__ReverseLast(state, false);
}

function void Prog_RedoLast(Prog_State *state)
{
    Prog__ReverseLast(state, true);
}

// The panel's columns: its own once it changes them, else the defaults; This PC has its own, for drives.
function Prog_ColumnSet Prog_PanelColumns(const Prog_State *state)
{
    if (StrIsEqual(state->directory.path, Prog_ThisPC)) {
        return (Prog_ColumnSet){ .count = 6, .kinds = { Prog_ColumnKind_Name, Prog_ColumnKind_Type, Prog_ColumnKind_FileSystem, Prog_ColumnKind_FreeSpace, Prog_ColumnKind_Size,
                                                        Prog_ColumnKind_PercentFull }, .shares = { 0, 0.09f, 0.09f, 0.09f, 0.09f, 0.36f } };
    }
    if (P_IsTrashFolder(state->directory.path)) {
        return (Prog_ColumnSet){ .count = 5, .kinds = { Prog_ColumnKind_Name, Prog_ColumnKind_OriginalLocation, Prog_ColumnKind_Type, Prog_ColumnKind_Size, Prog_ColumnKind_DateDeleted },
                                 .shares = { 0, 0.4f, 0.1f, 0.1f, 0.15f } };
    }
    if (state->columnSet.count) return state->columnSet;
    return (Prog_ColumnSet){ .count = 5, .kinds = { Prog_ColumnKind_Name, Prog_ColumnKind_Type, Prog_ColumnKind_Modified, Prog_ColumnKind_Items, Prog_ColumnKind_Size } };
}

// A kind's share of the row until it is resized; Name takes what the others leave.
function f32 Prog_ColumnShare(Prog_ColumnKind kind)
{
    switch (kind) {
        case Prog_ColumnKind_Name: return 0;
        case Prog_ColumnKind_Size:
        case Prog_ColumnKind_VolumeUsage:
        case Prog_ColumnKind_FolderUsage: return 0.1f;
        default: return 0.15f;
    }
}

// The header menu's changes to the panel's columns: `column` is a place among them, or for ColumnToggle a Prog_ColumnKind;
// a resize carries the new share of the row in millionths as `value`, and a shift its direction.
function void Prog_EditColumns(Prog_State *state, Prog_CommandKind kind, u64 column, u64 value)
{
    if (StrIsEqual(state->directory.path, Prog_ThisPC) || P_IsTrashFolder(state->directory.path)) return;
    Prog_ColumnSet set = Prog_PanelColumns(state);
    Prog_ViewSettings before = Prog_CurrentView(state), kept;
    switch (kind) {
        case Prog_CommandKind_ColumnShiftLeft:
        case Prog_CommandKind_ColumnShiftRight: {
            u64 other = kind == Prog_CommandKind_ColumnShiftLeft ? column - 1 : column + 1;
            if (column >= set.count || other >= set.count) return;
            u8 k = set.kinds[column];
            f32 share = set.shares[column];
            set.kinds[column] = set.kinds[other], set.shares[column] = set.shares[other];
            set.kinds[other] = k, set.shares[other] = share;
        } break;
        case Prog_CommandKind_ColumnMove: {
            if (column >= set.count || value >= set.count || column == value) return;
            u8 k = set.kinds[column];
            f32 share = set.shares[column];
            for (u32 i = (u32)column; i + 1 < set.count; ++i) set.kinds[i] = set.kinds[i + 1], set.shares[i] = set.shares[i + 1];
            for (u32 i = set.count - 1; i > value; --i) set.kinds[i] = set.kinds[i - 1], set.shares[i] = set.shares[i - 1];
            set.kinds[value] = k, set.shares[value] = share;
        } break;
        case Prog_CommandKind_ColumnClose: {
            // Name stays.
            if (column >= set.count || set.kinds[column] == Prog_ColumnKind_Name) return;
            for (u32 i = (u32)column; i + 1 < set.count; ++i) set.kinds[i] = set.kinds[i + 1], set.shares[i] = set.shares[i + 1];
            --set.count;
        } break;
        case Prog_CommandKind_ColumnToggle: {
            if (column >= Prog_FolderColumnKinds || column == Prog_ColumnKind_Name) return;
            u32 at = set.count;
            for (u32 i = 0; i < set.count; ++i) if (set.kinds[i] == column) at = i;
            if (at < set.count) {
                Prog_EditColumns(state, Prog_CommandKind_ColumnClose, at, 0);
                return;
            }
            set.kinds[set.count] = (u8)column;
            set.shares[set.count++] = 0;
        } break;
        case Prog_CommandKind_ColumnResize: {
            if (column >= set.count) return;
            set.shares[column] = UI__Clamp((f32)value / 1e6f, 0.02f, 0.8f);
        } break;
        case Prog_CommandKind_ColumnsRestore: set = (Prog_ColumnSet){0}; break;
        default: return;
    }
    state->columnSet = set;
    // A column added from the menu is kept for the folder, which locks its view, while the panel's own view stays as it was.
    if (state->directory.path.length && kind == Prog_CommandKind_ColumnToggle && !Prog_FolderView(state, state->directory.path, &kept)) {
        kept = before;
        kept.columns = set;
        Prog__SetFolderView(state, state->directory.path, &kept);
        Prog__ApplyFolderView(state, state->directory.path);
        state->ownSettings = before;
    }
    if (Prog_ShowsGit(state) && !state->repos.header.count && !state->reposJob) Prog_RequestRepos(state);
}

// The columns' names in the files that keep them.
global const String Prog_ColumnWords[Prog_ColumnKind_Count] = {
    SInit("name"), SInit("type"), SInit("size"), SInit("items"), SInit("created"), SInit("modified"), SInit("accessed"), SInit("volume"), SInit("folder"), SInit("git"), SInit("filesystem"), SInit("free"), SInit("full"), SInit("original"), SInit("deleted"),
};

// `set` as one word: its columns' names, those resized with their share of the row after a colon, between commas; "-" for the defaults.
function String Prog_ColumnsText(Arena *arena, Prog_ColumnSet set)
{
    if (!set.count) return S("-");
    StrBuilder text = {0};
    for (u32 i = 0; i < set.count; ++i) {
        if (i) StrBuildChar(arena, &text, ',');
        StrBuildStr(arena, &text, Prog_ColumnWords[set.kinds[i]]);
        if (set.shares[i] > 0) StrBuildStr(arena, &text, StrFormat(arena, ":%u", (u32)(set.shares[i] * 1e6f + 0.5f)));
    }
    return text.buffer;
}

// The columns of a word Prog_ColumnsText wrote; false, leaving `set` alone, when it is not one.
function b32 Prog_ParseColumns(String word, Prog_ColumnSet *set)
{
    Prog_ColumnSet result = {0};
    if (StrIsEqual(word, S("-"))) {
        *set = result;
        return true;
    }
    for (u64 at = 0; at < word.length;) {
        Index comma = StrIndexChar(StrFrom(word, at), ',', SSF_CountOnFail);
        String part = StrFromTo(word, at, (i64)(at + comma.value));
        at += comma.value + 1;
        Index colon = StrIndexChar(part, ':', SSF_CountOnFail);
        String name = StrFromTo(part, 0, (i64)colon.value), share = StrFrom(part, Min(colon.value + 1, part.length));
        u32 kind = Prog_ColumnKind_Count;
        for (u32 k = 0; k < Prog_ColumnKind_Count; ++k) if (StrIsEqual(name, Prog_ColumnWords[k])) kind = k;
        u64 millionths = 0;
        if (kind == Prog_ColumnKind_Count || result.count >= Prog_ColumnKind_Count || (colon.exists && !Prog__TakeNumber(&share, &millionths))) return false;
        for (u32 i = 0; i < result.count; ++i) if (result.kinds[i] == kind) return false;
        result.kinds[result.count] = (u8)kind;
        result.shares[result.count++] = UI__Clamp((f32)millionths / 1e6f, 0, 0.8f);
    }
    // Name is always there.
    b32 name = false;
    for (u32 i = 0; i < result.count; ++i) name |= result.kinds[i] == Prog_ColumnKind_Name;
    if (!name) return false;
    *set = result;
    return true;
}

function b32 Prog__SameColumns(Prog_ColumnSet a, Prog_ColumnSet b)
{
    if (a.count != b.count) return false;
    for (u32 i = 0; i < a.count; ++i) if (a.kinds[i] != b.kinds[i] || a.shares[i] != b.shares[i]) return false;
    return true;
}

// What the current panel shows a folder with.
function Prog_ViewSettings Prog_CurrentView(const Prog_State *state)
{
    return (Prog_ViewSettings){ state->zoom, state->sort, state->descending, state->folderSort, state->columnSet };
}

function void Prog__SetCurrentView(Prog_State *state, Prog_ViewSettings view)
{
    state->zoom = view.zoom;
    state->sort = view.sort;
    state->descending = view.descending;
    state->folderSort = view.folders;
    state->columnSet = view.columns;
}

// One zoom step in or out: a view at a time up to M Icons, then the icon sizes a step apart up to XL Icons.
function u32 Prog_ZoomStepped(u32 zoom, b32 up)
{
    if (up) return zoom < Prog_ZoomIcons ? Min(zoom + Prog_ZoomList, (u32)Prog_ZoomIcons) : Min(zoom + Prog_ZoomStep, (u32)Prog_ZoomMax);
    if (zoom > Prog_ZoomIcons) return Max(zoom - Prog_ZoomStep, (u32)Prog_ZoomIcons);
    return zoom >= Prog_ZoomList ? zoom - Prog_ZoomList : 0;
}

// A zoom read from Session.conf or FolderViews.conf; files from before zoom counted tenths of a percent hold a level from 0 to 16.
function u32 Prog_ZoomFromSaved(u64 value)
{
    if (value < 3) return (u32)value * Prog_ZoomList;
    if (value <= 16) return (u32)Min((u64)Prog_ZoomIcons + (value - 3) * Prog_ZoomStep, (u64)Prog_ZoomMax);
    return (u32)Min(value, (u64)Prog_ZoomMax);
}

// A FolderViews.conf line: its zoom, sort, whether that is descending, the folder sort and the columns, then the path, which may hold spaces;
// lines from before the folder sort and the columns have only the first three. False when `line` is not one.
function b32 Prog__ParseFolderView(String line, Prog_ViewSettings *view, String *path)
{
    u64 a = 0, b = 0, c = 0, d = 0;
    if (!Prog__TakeNumber(&line, &a) || !Prog__TakeNumber(&line, &b) || !Prog__TakeNumber(&line, &c)) return false;
    *view = (Prog_ViewSettings){ Prog_ZoomFromSaved(a), (u8)Min(b, (u64)Prog_Sort_Count - 1), c != 0, Prog_FolderSort_First, {0} };
    String rest = line;
    if (Prog__TakeNumber(&rest, &d)) {
        Index space = StrIndexChar(rest, ' ', SSF_None);
        if (!space.exists || !Prog_ParseColumns(StrFromTo(rest, 0, (i64)space.value), &view->columns)) return false;
        view->folders = (u8)Min(d, (u64)Prog_FolderSort_Count - 1);
        line = StrFrom(rest, space.value + 1);
    }
    *path = line;
    return true;
}

// The kept view of folder `path`, false when it has none.
function b32 Prog_FolderView(const Prog_State *state, String path, Prog_ViewSettings *view)
{
    for (u64 i = 0; path.length && i < state->folderViews.header.count; ++i) {
        String at;
        Prog_ViewSettings found;
        if (Prog__ParseFolderView(state->folderViews.v[i], &found, &at) && StrIsEqual(at, path)) {
            *view = found;
            return true;
        }
    }
    return false;
}

// Keeps `path`'s view as the current panel shows it, or with `keep` false lets it go, and saves the list.
function void Prog__SetFolderView(Prog_State *state, String path, const Prog_ViewSettings *view)
{
    Scratch scratch = GetScratch();
    u64 count = state->folderViews.header.count, kept = 0;
    String *lines = ArenaPushArray(scratch.arena, count + 1, String);
    for (u64 i = 0; i < count; ++i) {
        String at;
        Prog_ViewSettings view;
        if (!Prog__ParseFolderView(state->folderViews.v[i], &view, &at) || !StrIsEqual(at, path)) lines[kept++] = state->folderViews.v[i];
    }
    if (view) {
        lines[kept++] = StrFormat(scratch.arena, "%u %u %u %u %S %S", view->zoom, (u32)view->sort, view->descending ? 1u : 0u, (u32)view->folders,
                                  Prog_ColumnsText(scratch.arena, view->columns), path);
    }
    Prog__SetList(&state->folderViewArena, &state->folderViews, lines, kept);
    Prog__SaveList(state, Prog_FolderViewsFile, state->folderViews);
    ReleaseScratch(scratch);
}

function void Prog__ApplyFolderView(Prog_State *state, String path)
{
    Prog_ViewSettings view;
    if (Prog_FolderView(state, path, &view)) {
        if (!state->ownView) {
            state->ownView = true;
            state->ownSettings = Prog_CurrentView(state);
        }
        Prog__SetCurrentView(state, view);
    } else if (state->ownView) {
        state->ownView = false;
        Prog__SetCurrentView(state, state->ownSettings);
    }
}

// A locked folder keeps what its view is changed to, once nothing is being dragged, such as a column's edge.
function void Prog_KeepLockedView(Prog_State *state)
{
    Prog_ViewSettings saved;
    if (!state->ownView || (UI && UI->activeKey) || !state->directory.path.length || !Prog_FolderView(state, state->directory.path, &saved)) return;
    Prog_ViewSettings shown = Prog_CurrentView(state);
    Scratch scratch = GetScratch();
    // The columns by their text, which rounds their shares as the file does.
    b32 same = shown.zoom == saved.zoom && shown.sort == saved.sort && shown.descending == saved.descending && shown.folders == saved.folders &&
               StrIsEqual(Prog_ColumnsText(scratch.arena, shown.columns), Prog_ColumnsText(scratch.arena, saved.columns));
    ReleaseScratch(scratch);
    if (!same) Prog__SetFolderView(state, state->directory.path, &shown);
}

// Back to the panel's own view after the shown folder's lets go of its, listed again when its order changes.
function void Prog__RestoreOwnView(Prog_State *state)
{
    if (!state->ownView) return;
    Prog_ViewSettings own = state->ownSettings;
    b32 resort = state->sort != own.sort || state->descending != own.descending || state->folderSort != own.folders;
    Prog__ApplyFolderView(state, state->directory.path);
    if (resort) Prog_Reload(state);
}

// Locks the shown folder's view as the panel shows it, or unlocks it. What the panel shows stays either way; the panel's own comes back in
// the next folder without a lock.
function void Prog_ToggleFolderView(Prog_State *state)
{
    String path = state->directory.path;
    Prog_ViewSettings view;
    if (!path.length) return;
    b32 locked = Prog_FolderView(state, path, &view);
    Prog_ViewSettings shown = Prog_CurrentView(state);
    Prog__SetFolderView(state, path, locked ? NULL : &shown);
    if (!locked) Prog__ApplyFolderView(state, path);
}

function void Prog_ClearFolderViews(Prog_State *state)
{
    Prog__SetList(&state->folderViewArena, &state->folderViews, NULL, 0);
    Prog__SaveList(state, Prog_FolderViewsFile, state->folderViews);
    Prog__RestoreOwnView(state);
}



// The digits at the start of `*text`, which moves past them and one space.
function b32 Prog__TakeNumber(String *text, u64 *value)
{
    u64 at = 0, result = 0;
    while (at < text->length && text->value[at] >= '0' && text->value[at] <= '9' && at < 19) result = result * 10 + (u64)(text->value[at++] - '0');
    if (!at) return false;
    *value = result;
    *text = StrFrom(*text, Min(at + 1, text->length));
    return true;
}

// One record per line: the layout's split nodes, then each panel and its tabs; a tab's path ends its line, so it may hold spaces.
function String Prog_SessionText(Arena *arena, Prog_State *state)
{
    Prog__EnsureTab(state);
    StrBuilder text = {0};
    u32 active = state->activePanel;
    // The inspectors' panels are left out, and the others numbered among themselves.
    u32 numbers[Prog_MaxPanels] = {0}, count = 0;
    for (u32 p = 0; p < state->panelCount; ++p) numbers[p] = state->panels[p].inspectorOf ? UINT32_MAX : count++;
    u32 shown = state->panels[active].inspectorOf ? Prog_PanelIndex(state, state->panels[active].inspectorOf) : active;
    StrBuildStr(arena, &text, StrFormat(arena, "session 1\nactive %u\nsidebar %u\npreview %u\n", shown < state->panelCount ? numbers[shown] : 0,
                                        state->sidebarHidden ? 1u : 0u, state->previewShown ? 1u : 0u));
    if (state->sidebarWidth > 0) StrBuildStr(arena, &text, StrFormat(arena, "sidebarwidth %u\n", (u32)(state->sidebarWidth + 0.5f)));
    StrBuildStr(arena, &text, S("groups"));
    for (u32 g = 0; g < Prog_Group_Count; ++g) StrBuildStr(arena, &text, state->groupToggled[g] ? S(" 1") : S(" 0"));
    StrBuildChar(arena, &text, '\n');
    for (u32 i = 0; i < Prog_MaxSplits; ++i) {
        const Prog_Split *split = &state->splits[i];
        if (split->kind == Prog_SplitKind_None) continue;
        u32 panel = split->kind == Prog_SplitKind_Panel ? Prog_PanelIndex(state, split->panel) : UINT32_MAX;
        StrBuildStr(arena, &text, StrFormat(arena, "split %u %u %u %u %u %u\n", i, (u32)split->kind, (u32)split->first, (u32)split->second,
                                            panel < state->panelCount ? numbers[panel] : 0, (u32)(split->ratio * 1e6f)));
    }
    for (u32 p = 0; p < state->panelCount; ++p) {
        if (numbers[p] == UINT32_MAX) continue;
        Prog__UsePanel(state, p);
        // The panel's own view, not a locked folder's, which comes back with the folder.
        Prog_ViewSettings own = state->ownView ? state->ownSettings : Prog_CurrentView(state);
        StrBuildStr(arena, &text, StrFormat(arena, "panel %u %u %u\n", numbers[p], own.zoom, state->activeTab));
        if (state->miller) StrBuildStr(arena, &text, StrFormat(arena, "miller %u 1\n", numbers[p]));
        StrBuildStr(arena, &text, StrFormat(arena, "order %u %u %u %u\n", numbers[p], (u32)own.sort, own.descending ? 1u : 0u, (u32)own.folders));
        StrBuildStr(arena, &text, StrFormat(arena, "columns %u %S\n", numbers[p], Prog_ColumnsText(arena, own.columns)));
        // Its inspector's, from the inspector's panel while it is open.
        Prog_ViewSettings inspector = state->inspectorView;
        u32 inner = Prog_InspectorPanel(state, state->panels[p].id, false);
        if (inner != UINT32_MAX) {
            Prog__UsePanel(state, inner);
            inspector = Prog_CurrentView(state);
            Prog__UsePanel(state, p);
        }
        StrBuildStr(arena, &text, StrFormat(arena, "inspector %u %u %u %u %u %S\n", numbers[p], inspector.zoom, (u32)inspector.sort, inspector.descending ? 1u : 0u,
                                            (u32)inspector.folders, Prog_ColumnsText(arena, inspector.columns)));
        for (u32 t = 0; t < state->tabCount; ++t) {
            StrBuildStr(arena, &text, StrFormat(arena, "tab %u %S\n", numbers[p], Prog_TabPath(state, t)));
            // Its filter and what it hides, which the tab opens with again.
            b32 shown = t == state->activeTab;
            const Prog_TextEdit *filter = shown ? &state->filter : &state->tabs[t].filter;
            b32 sticky = shown ? state->filterSticky : state->tabs[t].filterSticky, expanded = shown ? state->expanded : state->tabs[t].expanded;
            b32 hideFolders = shown ? state->hideFolders : state->tabs[t].hideFolders, hideFiles = shown ? state->hideFiles : state->tabs[t].hideFiles;
            if (filter->length || sticky || expanded || hideFolders || hideFiles) {
                StrBuildStr(arena, &text, StrFormat(arena, "filter %u %u %u %u %u %S\n", numbers[p], sticky ? 1u : 0u, expanded ? 1u : 0u, hideFolders ? 1u : 0u,
                                                    hideFiles ? 1u : 0u, Prog_EditText(filter)));
            }
        }
    }
    Prog__UsePanel(state, active);
    return text.buffer;
}

// Rebuilds the panels and tabs of `text` in a state that has just started with one panel and one tab; returns false, changing nothing, when `text` does not describe a whole layout.
function b32 Prog_RestoreSession(Prog_State *state, String text)
{
    Prog__EnsureTab(state);
    if (state->panelCount != 1 || state->tabCount != 1) return false;
    Scratch scratch = GetScratch();
    Prog_Split splits[Prog_MaxSplits] = {0};
    u32 splitPanel[Prog_MaxSplits] = {0};
    typedef struct { b32 sticky, expanded, hideFolders, hideFiles; String text; } Filter;
    struct { u32 zoom, activeTab, tabCount; b32 miller; String tabs[Prog_MaxTabs]; Filter filters[Prog_MaxTabs]; Prog_ViewSettings view, inspector; } *panels = ArenaPushArray(scratch.arena, Prog_MaxPanels, typeof(*panels));
    u64 panelCount = 0, active = 0, sidebarHidden = 0, previewShown = 0, sidebarWidth = 0;
    b32 valid = false;
    ArrayStr lines = StrSplit(scratch.arena, text, '\n');
    for (u64 l = 0; l < lines.header.count; ++l) {
        String line = lines.v[l];
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        Index space = StrIndexChar(line, ' ', SSF_CountOnFail);
        String word = StrFromTo(line, 0, (i64)space.value), rest = StrFrom(line, Min(space.value + 1, line.length));
        u64 a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        if (StrIsEqual(word, S("session"))) valid = Prog__TakeNumber(&rest, &a) && a == 1;
        else if (StrIsEqual(word, S("active"))) Prog__TakeNumber(&rest, &active);
        else if (StrIsEqual(word, S("sidebar"))) Prog__TakeNumber(&rest, &sidebarHidden);
        else if (StrIsEqual(word, S("sidebarwidth"))) Prog__TakeNumber(&rest, &sidebarWidth);
        else if (StrIsEqual(word, S("preview"))) Prog__TakeNumber(&rest, &previewShown);
        else if (StrIsEqual(word, S("split"))) {
            if (!(Prog__TakeNumber(&rest, &a) && Prog__TakeNumber(&rest, &b) && Prog__TakeNumber(&rest, &c) && Prog__TakeNumber(&rest, &d) && Prog__TakeNumber(&rest, &e) && Prog__TakeNumber(&rest, &f))
                || a >= Prog_MaxSplits || b >= Prog_SplitKind_Count || c >= Prog_MaxSplits || d >= Prog_MaxSplits || e >= Prog_MaxPanels) valid = false;
            else {
                splits[a] = (Prog_Split){ .kind = (u8)b, .first = (u8)c, .second = (u8)d, .ratio = (f32)f / 1e6f };
                splitPanel[a] = (u32)e;
            }
        } else if (StrIsEqual(word, S("panel"))) {
            if (!(Prog__TakeNumber(&rest, &a) && Prog__TakeNumber(&rest, &b) && Prog__TakeNumber(&rest, &c)) || a != panelCount || a >= Prog_MaxPanels) valid = false;
            else panels[panelCount++] = (typeof(*panels)){ .zoom = Prog_ZoomFromSaved(b), .activeTab = (u32)c };
        } else if (StrIsEqual(word, S("order"))) {
            // A panel's sort, its direction and where its folders go; older sessions have none and keep the defaults.
            if (Prog__TakeNumber(&rest, &a) && a + 1 == panelCount && Prog__TakeNumber(&rest, &b) && Prog__TakeNumber(&rest, &c) && Prog__TakeNumber(&rest, &d)) {
                panels[a].view.sort = (u8)Min(b, (u64)Prog_Sort_Count - 1);
                panels[a].view.descending = c != 0;
                panels[a].view.folders = (u8)Min(d, (u64)Prog_FolderSort_Count - 1);
            }
        } else if (StrIsEqual(word, S("columns"))) {
            if (Prog__TakeNumber(&rest, &a) && a + 1 == panelCount) Prog_ParseColumns(rest, &panels[a].view.columns);
        } else if (StrIsEqual(word, S("miller"))) {
            if (Prog__TakeNumber(&rest, &a) && a + 1 == panelCount && Prog__TakeNumber(&rest, &b)) panels[a].miller = b != 0;
        } else if (StrIsEqual(word, S("inspector"))) {
            u64 g = 0;
            if (Prog__TakeNumber(&rest, &a) && a + 1 == panelCount && Prog__TakeNumber(&rest, &b) && Prog__TakeNumber(&rest, &c) && Prog__TakeNumber(&rest, &d) &&
                Prog__TakeNumber(&rest, &g)) {
                Prog_ViewSettings *view = &panels[a].inspector;
                *view = (Prog_ViewSettings){ Prog_ZoomFromSaved(b), (u8)Min(c, (u64)Prog_Sort_Count - 1), d != 0, (u8)Min(g, (u64)Prog_FolderSort_Count - 1), {0} };
                Prog_ParseColumns(rest, &view->columns);
            }
        } else if (StrIsEqual(word, S("tab"))) {
            if (!Prog__TakeNumber(&rest, &a) || a + 1 != panelCount) valid = false;
            else if (rest.length && panels[a].tabCount < Prog_MaxTabs) panels[a].tabs[panels[a].tabCount++] = rest;
        } else if (StrIsEqual(word, S("filter"))) {
            // The last tab's; older sessions have none.
            if (Prog__TakeNumber(&rest, &a) && a + 1 == panelCount && panels[a].tabCount && Prog__TakeNumber(&rest, &b) && Prog__TakeNumber(&rest, &c) &&
                Prog__TakeNumber(&rest, &d) && Prog__TakeNumber(&rest, &e)) {
                panels[a].filters[panels[a].tabCount - 1] = (Filter){ b != 0, c != 0, d != 0, e != 0, rest };
            }
        }
    }
    // The tree from the root must reach every panel once, each with a tab.
    u32 reached[Prog_MaxPanels] = {0};
    u32 stack[Prog_MaxSplits * 2], depth = 0, visits = 0;
    if (valid && splits[0].kind != Prog_SplitKind_None) stack[depth++] = 0;
    else valid = false;
    while (valid && depth) {
        u32 node = stack[--depth];
        const Prog_Split *split = &splits[node];
        if (++visits > Prog_MaxSplits) valid = false;
        else if (split->kind == Prog_SplitKind_Panel) valid = splitPanel[node] < panelCount && !reached[splitPanel[node]]++;
        else if (split->kind == Prog_SplitKind_None || split->first == node || split->second == node) valid = false;
        else {
            stack[depth++] = split->first;
            stack[depth++] = split->second;
        }
    }
    for (u32 p = 0; valid && p < panelCount; ++p) valid = reached[p] == 1 && panels[p].tabCount;
    if (valid) {
        state->sidebarHidden = sidebarHidden != 0;
        state->sidebarWidth = sidebarWidth ? UI__Clamp((f32)sidebarWidth, Prog_SidebarMin, Prog_SidebarMax) : 0;
        state->previewShown = previewShown != 0;
        for (u32 p = 0; p < panelCount; ++p) {
            if (p) {
                state->panels[p] = (Prog_Panel){ .id = ++state->nextPanelId };
                state->panelCount = p + 1;
            }
            Prog__ActivatePanel(state, p);
            Prog__EnsureTab(state);
            // The saved view is the panel's own, which a locked folder's, from the start's first folder, would otherwise stand in for.
            state->ownView = false;
            state->zoom = panels[p].zoom;
            state->miller = panels[p].miller;
            state->sort = panels[p].view.sort;
            state->descending = panels[p].view.descending;
            state->folderSort = panels[p].view.folders;
            state->columnSet = panels[p].view.columns;
            state->inspectorView = panels[p].inspector;
            // The first tab drops whatever it was about to list and starts over in its folder.
            ArenaClear(&state->directoryArena);
            state->directory = (Prog_Directory){0};
            ArenaClear(&state->historyArena);
            state->history = (ArrayStr){0};
            ArrayPush(&state->historyArena, state->history, StrCopy(&state->historyArena, panels[p].tabs[0]));
            state->historyAt = 0;
            for (u32 t = 1; t < panels[p].tabCount; ++t) Prog_OpenTab(state, panels[p].tabs[t], t, false);
            for (u32 t = 0; t < panels[p].tabCount; ++t) {
                const Filter *saved = &panels[p].filters[t];
                Prog_Tab *tab = &state->tabs[t];
                Prog_TextEdit *filter = t ? &tab->filter : &state->filter;
                filter->length = filter->cursor = filter->anchor = (u32)Min(saved->text.length, (u64)sizeof(filter->text) - 1);
                MemCopy(filter->text, saved->text.value, filter->length);
                ++filter->version;
                *(t ? &tab->filterSticky : &state->filterSticky) = saved->sticky;
                *(t ? &tab->expanded : &state->expanded) = saved->expanded;
                *(t ? &tab->hideFolders : &state->hideFolders) = saved->hideFolders;
                *(t ? &tab->hideFiles : &state->hideFiles) = saved->hideFiles;
            }
            Prog_RequestDirectory(state, panels[p].tabs[0], false, 0);
            if (panels[p].activeTab < state->tabCount) Prog_SelectTab(state, panels[p].activeTab);
        }
        for (u32 i = 0; i < Prog_MaxSplits; ++i) {
            state->splits[i] = splits[i];
            if (splits[i].kind == Prog_SplitKind_Panel) state->splits[i].panel = state->panels[splitPanel[i]].id;
        }
        Prog__ActivatePanel(state, active < panelCount ? (u32)active : 0);
    }
    ReleaseScratch(scratch);
    return valid;
}

function void Prog_LoadSession(Prog_State *state)
{
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    String text = {0};
    if (P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, Prog_SessionFile), &text)) Prog_RestoreSession(state, text);
    ReleaseScratch(scratch);
}

#define Prog_LayoutsFile S("Layouts.conf")

// The layouts saved in Layouts.conf, each a "layout" line with its name, its text, and an "end" line.
function void Prog_LoadLayouts(Prog_State *state)
{
    ArenaClear(&state->layoutsArena);
    state->layoutNames = state->layoutTexts = (ArrayStr){0};
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    String text = {0}, name = {0};
    StrBuilder body = {0};
    b32 inside = false;
    ArrayStr lines = P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, Prog_LayoutsFile), &text) ? StrSplit(scratch.arena, text, '\n') : (ArrayStr){0};
    for (u64 l = 0; l < lines.header.count; ++l) {
        String line = lines.v[l];
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        if (!inside && line.length > 7 && StrIsEqual(StrFromTo(line, 0, 7), S("layout "))) {
            name = StrFrom(line, 7);
            body = (StrBuilder){0};
            inside = true;
        } else if (inside && StrIsEqual(line, S("end"))) {
            if (name.length && state->layoutNames.header.count < Prog_MaxLayouts) {
                ArrayPush(&state->layoutsArena, state->layoutNames, StrCopy(&state->layoutsArena, name));
                ArrayPush(&state->layoutsArena, state->layoutTexts, StrCopy(&state->layoutsArena, body.buffer));
            }
            inside = false;
        } else if (inside) {
            StrBuildStr(scratch.arena, &body, line);
            StrBuildChar(scratch.arena, &body, '\n');
        }
    }
    ReleaseScratch(scratch);
}

// What a layout and the session on exit keep: the window's content size, as it is when not maximized, and whether it is maximized, then
// the session's panels, tabs and sidebar.
function String Prog_LayoutText(Arena *arena, Prog_State *state)
{
    i32 width, height;
    PlatformWindowRestoredSize(&width, &height);
    return StrFormat(arena, "window %d %d %u\n%S", width, height, PlatformWindowIsMaximized() ? 1u : 0u, Prog_SessionText(arena, state));
}

// The window's size and maximized state as the last run's session left them, for the window to open at.
function void Prog_SessionWindow(String configDirectory, PlatformWindowDesc *desc)
{
    if (!configDirectory.length) return;
    Scratch scratch = GetScratch();
    String text = {0};
    if (P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, configDirectory, Prog_SessionFile), &text) && text.length > 7 && StrIsEqual(StrFromTo(text, 0, 7), S("window "))) {
        String rest = StrFrom(text, 7);
        u64 width = 0, height = 0, maximized = 0;
        if (Prog__TakeNumber(&rest, &width) && Prog__TakeNumber(&rest, &height) && Prog__TakeNumber(&rest, &maximized) && width < 65536 && height < 65536) {
            desc->contentWidth = (i32)width, desc->contentHeight = (i32)height, desc->maximized = maximized != 0;
        }
    }
    ReleaseScratch(scratch);
}

// Sizes the window from the "window" line `text` starts with, when it does, and gives the text after it.
function String Prog__ApplyWindowLine(String text)
{
    Index end = StrIndexChar(text, '\n', SSF_CountOnFail);
    String first = StrFromTo(text, 0, (i64)end.value);
    if (first.length <= 7 || !StrIsEqual(StrFromTo(first, 0, 7), S("window "))) return text;
    String rest = StrFrom(first, 7);
    u64 width = 0, height = 0, maximized = 0;
    if (Prog__TakeNumber(&rest, &width) && Prog__TakeNumber(&rest, &height) && Prog__TakeNumber(&rest, &maximized)) PlatformWindowSetSize((i32)width, (i32)height, maximized != 0);
    return StrFrom(text, Min(end.value + 1, text.length));
}

// Gives the layout named `name`, matched without case, the text `text`, after the others when it is new, or with `remove` takes it out;
// Layouts.conf is written again at once, as this happens only from a command.
function void Prog_SetLayout(Prog_State *state, String name, String text, b32 remove)
{
    Arena kept = {0};
    ArrayStr names = {0}, texts = {0};
    b32 placed = false;
    for (u64 i = 0; i < state->layoutNames.header.count; ++i) {
        b32 same = Prog__EqualFolded(state->layoutNames.v[i], name);
        placed |= same;
        if (same && remove) continue;
        ArrayPush(&kept, names, StrCopy(&kept, same ? name : state->layoutNames.v[i]));
        ArrayPush(&kept, texts, StrCopy(&kept, same ? text : state->layoutTexts.v[i]));
    }
    if (!placed && !remove && names.header.count < Prog_MaxLayouts) {
        ArrayPush(&kept, names, StrCopy(&kept, name));
        ArrayPush(&kept, texts, StrCopy(&kept, text));
    }
    ArenaRelease(&state->layoutsArena);
    state->layoutsArena = kept;
    state->layoutNames = names;
    state->layoutTexts = texts;
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    StrBuilder file = {0};
    for (u64 i = 0; i < names.header.count; ++i) StrBuildStr(scratch.arena, &file, StrFormat(scratch.arena, "layout %S\n%Send\n", names.v[i], texts.v[i]));
    P_FileError error = P_WriteFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, Prog_LayoutsFile), file.buffer);
    if (error.code) {
        ArenaClear(&state->errorArena);
        state->error = StrFormat(&state->errorArena, "Save layouts: %S", error.message);
    }
    ReleaseScratch(scratch);
}

// Puts the window in the layout named `name`: its size, then its panels and tabs in place of the shown ones, which are closed first.
function b32 Prog_ApplyLayout(Prog_State *state, String name)
{
    String text = {0};
    for (u64 i = 0; i < state->layoutNames.header.count && !text.length; ++i) if (Prog__EqualFolded(state->layoutNames.v[i], name)) text = state->layoutTexts.v[i];
    if (!text.length) return false;
    Scratch scratch = GetScratch();
    text = Prog__ApplyWindowLine(StrCopy(scratch.arena, text));
    // Every inspector's panel, then the panels but the shown one, then its tabs but the shown one.
    Prog_CloseMenu(state);
    if (state->renaming) Prog__EndRename(state, false);
    u64 keep = state->panels[state->activePanel].inspectorOf ? state->panels[state->activePanel].inspectorOf : state->panels[state->activePanel].id;
    u64 owners[Prog_MaxPanels] = {0};
    u32 ownerCount = 0;
    for (u32 i = 0; i < state->panelCount; ++i) if (!state->panels[i].inspectorOf) owners[ownerCount++] = state->panels[i].id;
    for (u32 i = 0; i < ownerCount; ++i) Prog_DropInspectorPanel(state, owners[i]);
    for (u32 guard = 0; state->panelCount > 1 && guard < Prog_MaxPanels; ++guard) Prog_ClosePanel(state, Prog_PanelIndex(state, keep) == 0 ? 1 : 0);
    Prog__ActivatePanel(state, 0);
    for (u32 guard = 0; state->tabCount > 1 && guard < Prog_MaxTabs; ++guard) Prog_CloseTab(state, state->activeTab ? 0 : 1);
    b32 result = Prog_RestoreSession(state, text);
    ReleaseScratch(scratch);
    return result;
}

function b32 Prog__AnyMeasuring(const Prog_State *state)
{
    b32 result = state->measureJob != NULL;
    for (u32 i = 0; i < state->panelCount; ++i) result |= i != state->currentPanel && state->panels[i].measureJob != NULL;
    return result;
}

function void Prog__SwapTab(Prog_State *state, Prog_Tab *tab)
{
#define Prog__SwapField(type, name) { type swap = state->name; state->name = tab->name; tab->name = swap; }
    Prog_TabFields(Prog__SwapField)
#undef Prog__SwapField
}

// Lets go of a model's mesh and its pictures' textures, before the preview's arena that lists them is cleared.
function void Prog__DropModel(Prog_ModelView *model)
{
    R_MeshRelease(model->mesh);
    for (u32 i = 0; i < model->imageCount; ++i) R_TextureRelease(model->images[i]);
    model->mesh = (R_MeshHandle){0};
    model->images = NULL;
    model->parts = NULL;
    model->imageCount = model->partCount = 0;
}

// Lets go of an inspector's preview: its jobs, its textures, its model's mesh and its arena.
function void Prog__ReleasePreview(Arena *arena, R_TextureHandle texture, Prog_ModelView *model, Prog_PdfView *pdf, P_Job *job, P_Job *hexJob)
{
    if (job) P_RequestCancel(&job->cancelToken);
    if (hexJob) P_RequestCancel(&hexJob->cancelToken);
    if (pdf->job) P_RequestCancel(&pdf->job->cancelToken);
    for (u32 i = 0; i < Prog_PdfDrawn; ++i) R_TextureRelease(pdf->drawn[i].texture);
    R_TextureRelease(texture);
    R_TextureRelease(model->texture);
    Prog__DropModel(model);
    ArenaRelease(arena);
}

function void Prog__ReleaseTab(Prog_Tab *tab)
{
    Prog__ReleasePreview(&tab->previewArena, tab->previewTexture, &tab->model, &tab->pdf, tab->previewJob, tab->hexPageJob);
    ArenaRelease(&tab->directoryArena);
    ArenaRelease(&tab->historyArena);
    ArenaRelease(&tab->viewArena);
    *tab = (Prog_Tab){0};
}

// The folder tab `index` shows, or is about to show when it has not listed one yet.
function String Prog_TabPath(const Prog_State *state, u32 index)
{
    b32 shown = index == state->activeTab;
    const Prog_Directory *directory = shown ? &state->directory : &state->tabs[index].directory;
    const ArrayStr *history = shown ? &state->history : &state->tabs[index].history;
    u64 at = shown ? state->historyAt : state->tabs[index].historyAt;
    if (directory->path.length) return directory->path;
    if (shown && state->directoryJob) return ((Prog_JobInput *)state->directoryJob->input)->path;
    return at < history->header.count ? history->v[at] : (String){0};
}

// Where a new tab opens, by the settings; `current` stands in for the shown folder.
function String Prog_NewTabPath(Prog_State *state, Arena *arena, String current)
{
    if (state->newTab == Prog_NewTab_Root) return Prog_ThisPC;
    String home = state->newTab == Prog_NewTab_Home ? Prog_HomePath(arena) : (String){0};
    return home.length ? home : current;
}


// A drive with a medium to read: its size, or its file system, which an audio CD shows without one; a busy drive is taken to have one.
function b32 Prog__HasMedium(const P_Place *drive) { return drive->totalBytes || drive->fileSystem.length || drive->busy; }

// The listed drive `path` is on, or NULL.
function const P_Place *Prog_PathDrive(const Prog_State *state, String path)
{
    for (u64 i = 0; i < state->drives.header.count; ++i) if (Prog__IsUnder(path, state->drives.v[i].path)) return &state->drives.v[i];
    return NULL;
}

// What a tab calls `path`: a drive's root by the drive's name, as "DVD RW Drive (E:)", and other folders by their own.
function String Prog_PathName(const Prog_State *state, String path)
{
    for (u64 i = 0; P_IsTrashFolder(path) && i < state->places.header.count; ++i) {
        if (P_IsTrashFolder(state->places.v[i].path)) return state->places.v[i].name;
    }
    const P_Place *drive = Prog_PathDrive(state, path);
    return drive && drive->name.length && StrIsEqual(drive->path, path) ? drive->name : Prog__FolderName(path);
}

// This PC is listed again in the panels that show it once a drive comes, goes, or changes its name, medium or state; free space alone, which changes all the time, waits for a reload.
function void Prog__RelistDrives(Prog_State *state, P_ArrayPlace before, P_ArrayPlace after)
{
    b32 same = before.header.count == after.header.count;
    for (u64 i = 0; i < before.header.count && same; ++i) {
        const P_Place *a = &before.v[i], *b = &after.v[i];
        same = StrIsEqual(a->path, b->path) && StrIsEqual(a->name, b->name) && StrIsEqual(a->fileSystem, b->fileSystem) && a->busy == b->busy &&
               a->totalBytes == b->totalBytes;
    }
    if (same) return;
    u32 panel = state->currentPanel;
    for (u32 p = 0; p < state->panelCount; ++p) {
        Prog__UsePanel(state, p);
        if (StrIsEqual(state->directory.path, Prog_ThisPC) && !state->directoryJob) Prog_Reload(state);
    }
    Prog__UsePanel(state, panel);
}

// Tabs on drives that went away or have no medium, such as an ejected disc, go where a new tab would; there is nothing to show there.
function void Prog__LeaveLostDrives(Prog_State *state, P_ArrayPlace before, P_ArrayPlace after)
{
    // A listing that found no drives at all failed rather than saw them all go.
    if (!after.header.count) return;
    u32 panel = state->currentPanel;
    for (u64 d = 0; d < before.header.count; ++d) {
        String root = before.v[d].path;
        b32 kept = false;
        for (u64 i = 0; i < after.header.count && !kept; ++i) kept = StrIsEqual(after.v[i].path, root) && Prog__HasMedium(&after.v[i]);
        if (kept) continue;
        for (u32 p = 0; p < state->panelCount; ++p) {
            Prog__UsePanel(state, p);
            for (u32 t = 0; t < state->tabCount; ++t) {
                if (!Prog__IsUnder(Prog_TabPath(state, t), root)) continue;
                String path = Prog_NewTabPath(state, &state->frameArena, Prog_ThisPC);
                if (Prog__IsUnder(path, root)) path = Prog_ThisPC;
                if (t == state->activeTab) {
                    Prog_RequestDirectory(state, path, true, 0);
                } else {
                    // A hidden tab is put back in its slot with the new folder.
                    Prog_CloseTab(state, t);
                    Prog_OpenTab(state, path, t, false);
                }
            }
        }
    }
    Prog__UsePanel(state, panel);
}

// Opens `path` in a new tab at slot `at`.
function void Prog_OpenTab(Prog_State *state, String path, u32 at, b32 activate)
{
    Prog__EnsureTab(state);
    if (state->tabCount >= Prog_MaxTabs || !path.length) return;
    at = Min(at, state->tabCount);
    MemMove(&state->tabs[at + 1], &state->tabs[at], (state->tabCount - at) * sizeof(Prog_Tab));
    ++state->tabCount;
    if (at <= state->activeTab) ++state->activeTab;
    Prog_Tab *tab = &state->tabs[at];
    // It starts with the shown tab's own view, not a locked folder's, which its folder brings back if it has one.
    Prog_ViewSettings own = state->ownView ? state->ownSettings : Prog_CurrentView(state);
    *tab = (Prog_Tab){ .id = ++state->nextTabId, .sort = own.sort, .descending = own.descending, .folderSort = own.folders, .zoom = own.zoom, .columnSet = own.columns,
                       .showHidden = state->showHidden, .previewShown = state->previewShown };
    // The tab lists its folder when it is first shown.
    ArrayPush(&tab->historyArena, tab->history, StrCopy(&tab->historyArena, path));
    if (activate) Prog_SelectTab(state, at);
}

// A window of its own for `path`, the run started for it told This PC by a flag, as it is no folder, and the tab's `traits` by another.
function P_FileError Prog_OpenWindow(Prog_State *state, String path, String traits)
{
    String option = traits.length ? StrFormat(&state->frameArena, "--tab=%S", traits) : (String){0};
    return P_OpenWindow(&state->frameArena, StrIsEqual(path, Prog_ThisPC) ? S("--this-pc") : path, option);
}

// The most selected names a moved tab takes along, which keeps the command line of a window started for it within the system's limit.
#define Prog_TabTraitNames 500

// What a tab takes along besides its folder when it moves, as "zoom=300\tscroll=120\tfocus=name\tselect=name\tselect=other": its zoom, how
// far its list is scrolled while it is shown, the item it has focused and the items selected.
function String Prog__TabTraits(Prog_State *state, u32 slot)
{
    b32 shown = slot == state->activeTab;
    const Prog_Tab *tab = &state->tabs[slot];
    u32 zoom = shown ? state->zoom : tab->zoom;
    const Prog_Directory *directory = shown ? &state->directory : &tab->directory;
    b32 focused = shown ? state->hasFocus : tab->hasFocus;
    u64 row = shown ? state->focusedRow : tab->focusedRow;
    StrBuilder text = {0};
    StrBuildStr(&state->frameArena, &text, StrFormat(&state->frameArena, "zoom=%u", zoom));
    UI_Block *list = shown && UI ? UI__Find(UI_KeyFromStr(Prog_PanelKeySeed(Prog__PanelId(state)), S("Files"))) : NULL;
    if (list && list->scrollOffset.y > 0) StrBuildStr(&state->frameArena, &text, StrFormat(&state->frameArena, "\tscroll=%u", (u32)list->scrollOffset.y));
    if (focused && row < directory->count) StrBuildStr(&state->frameArena, &text, StrFormat(&state->frameArena, "\tfocus=%S", directory->entries[row].file.name));
    u32 names = 0;
    for (u64 i = 0; i < directory->count && names < Prog_TabTraitNames; ++i) {
        if (!directory->entries[i].selected) continue;
        StrBuildStr(&state->frameArena, &text, StrFormat(&state->frameArena, "\tselect=%S", directory->entries[i].file.name));
        ++names;
    }
    return text.buffer;
}

// Gives the shown tab the `traits` Prog__TabTraits wrote, as it lands where it was moved: its zoom at once, and its scroll, focused item and
// selection once listed.
function void Prog_ApplyTabTraits(Prog_State *state, String traits)
{
    ArrayStr parts = StrSplit(&state->frameArena, traits, '\t');
    ArenaClear(&state->pickArena);
    state->pickNames = (ArrayStr){0};
    state->pickScrollSet = false;
    for (u64 i = 0; i < parts.header.count; ++i) {
        String part = parts.v[i];
        if (StrIsEqual(StrFromTo(part, 0, 5), S("zoom="))) {
            u64 zoom = 0;
            String digits = StrFrom(part, 5);
            if (Prog__TakeNumber(&digits, &zoom)) state->zoom = Prog_ZoomFromSaved(zoom);
        } else if (StrIsEqual(StrFromTo(part, 0, 7), S("scroll="))) {
            u64 scroll = 0;
            String digits = StrFrom(part, 7);
            if (Prog__TakeNumber(&digits, &scroll)) state->pickScroll = (f32)scroll, state->pickScrollSet = true;
        } else if (StrIsEqual(StrFromTo(part, 0, 7), S("select="))) {
            ArrayPush(&state->pickArena, state->pickNames, StrCopy(&state->pickArena, StrFrom(part, 7)));
        } else if (StrIsEqual(StrFromTo(part, 0, 6), S("focus=")) && part.length - 6 <= sizeof(state->selectText)) {
            MemCopy(state->selectText, part.value + 6, part.length - 6);
            state->selectLength = (u32)(part.length - 6);
        }
    }
}

// Folders a later run of the program handed this one, a line each, each in a new tab of the active panel after its shown one, the last shown.
// A tab dragged over from another window follows its folder with its traits, after a tab character.
function void Prog_OpenPaths(Prog_State *state, String text)
{
    // A tab dragged over from another window goes into the strip slot it made room in, else after the active panel's shown tab.
    u32 into = state->foreignDrag && state->foreignOver ? Prog_PanelIndex(state, state->foreignPanel) : UINT32_MAX;
    state->foreignDrag = false;
    if (into != UINT32_MAX) Prog__ActivatePanel(state, into);
    Prog__UsePanel(state, state->activePanel);
    ArrayStr lines = StrSplit(&state->frameArena, text, '\n');
    for (u64 i = 0; i < lines.header.count; ++i) {
        Index split = StrIndexChar(lines.v[i], '\t', SSF_None);
        String path = split.exists ? StrFromTo(lines.v[i], 0, (i64)split.value) : lines.v[i];
        if (!path.length) continue;
        u32 count = state->tabCount;
        Prog_OpenTab(state, path, into != UINT32_MAX ? Min(state->foreignSlot, state->tabCount) : state->activeTab + 1, true);
        into = UINT32_MAX;
        if (split.exists && state->tabCount > count) Prog_ApplyTabTraits(state, StrFrom(lines.v[i], split.value + 1));
    }
}

// Shows the item at `path` selected in its folder, in a new tab after the active one with `inTab`.
function void Prog_ShowInFolder(Prog_State *state, String path, b32 inTab)
{
    String parent = Prog_ParentPath(path), name = Prog__FolderName(path);
    if (!parent.length || StrIsEqual(parent, path) || name.length > sizeof(state->selectText)) return;
    if (inTab) Prog_OpenTab(state, parent, state->activeTab + 1, true);
    MemCopy(state->selectText, name.value, name.length);
    state->selectLength = (u32)name.length;
    if (!inTab) {
        state->expanded = false;
        Prog_RequestDirectory(state, parent, !StrIsEqual(parent, state->directory.path), 0);
    }
}

function void Prog_SelectTab(Prog_State *state, u32 index)
{
    Prog__EnsureTab(state);
    if (index >= state->tabCount || index == state->activeTab) return;
    // Work tied to the rows of the tab being left stops; a listing on its way is dropped, so that tab keeps the folder it showed.
    Prog_CloseMenu(state);
    state->renaming = state->filtering = false;
    state->hasHoverRow = false;
    if (state->directoryJob) {
        P_RequestCancel(&state->directoryJob->cancelToken);
        state->directoryJob = NULL;
        ++state->generation;
    }
    state->loading = false;
    if (state->measureJob) {
        P_RequestCancel(&state->measureJob->cancelToken);
        state->measureJob = NULL;
    }
    P_DirectoryWatchStop(&state->watch);
    state->watchDirty = false;
    ArenaClear(&state->errorArena);
    state->error = (String){0};

    // A preview on its way is dropped with the tab hidden, and asked for again once it shows; what it already shows stays.
    if (state->previewJob) {
        P_RequestCancel(&state->previewJob->cancelToken);
        state->previewJob = NULL;
        state->previewPath = (String){0};
    }
    if (state->hexPageJob) P_RequestCancel(&state->hexPageJob->cancelToken);
    if (state->pdf.job) P_RequestCancel(&state->pdf.job->cancelToken);
    state->hexPageJob = state->pdf.job = NULL;
    Prog__SwapTab(state, &state->tabs[state->activeTab]);
    state->activeTab = index;
    Prog__SwapTab(state, &state->tabs[index]);
    // The inspector's views share their blocks across a panel's tabs, so the tab's starts at its top; its folder list lists the tab's folder.
    state->inspectorResetScroll = true;
    if (!state->panels[state->currentPanel].inspectorOf) {
        if (state->previewShown && state->previewFolder && state->previewPath.length) Prog_ListInInspector(state, state->previewPath);
        else Prog_DropInspectorPanel(state, state->panels[state->currentPanel].id);
    }

    // Totals of the old listing are matched by name, as after a refresh.
    ++state->listing;
    state->restoreScroll = true;
    if (state->directory.path.length) {
        // The tab's folder may have changed while it was hidden.
        P_DirectoryWatchStart(&state->watch, Prog__WatchedFolder(state), state->jobs.wake, state->jobs.wakeUser);
        Prog_Reload(state);
    } else if (state->historyAt < state->history.header.count) {
        Prog_RequestDirectory(state, state->history.v[state->historyAt], false, state->historyAt);
    }
}

function void Prog_CloseTab(Prog_State *state, u32 index)
{
    Prog__EnsureTab(state);
    // A panel's only tab closes the panel, and the last panel's last tab the window.
    if (index < state->tabCount && state->tabCount == 1 && state->panelCount <= 1) {
        PlatformWindowClose();
        return;
    }
    if (index < state->tabCount && state->tabCount == 1 && state->panelCount > 1) {
        String path = Prog_TabPath(state, index);
        if (path.length) ArrayPush(&state->closedArena, state->closedTabs, StrCopy(&state->closedArena, path));
        Prog_ClosePanel(state, state->currentPanel);
        return;
    }
    if (index >= state->tabCount || state->tabCount == 1) return;
    String path = Prog_TabPath(state, index);
    if (path.length) ArrayPush(&state->closedArena, state->closedTabs, StrCopy(&state->closedArena, path));
    // The neighbor to the right takes over a closed shown tab, else the one to the left.
    if (index == state->activeTab) Prog_SelectTab(state, index + 1 < state->tabCount ? index + 1 : index - 1);
    Prog__ReleaseTab(&state->tabs[index]);
    MemMove(&state->tabs[index], &state->tabs[index + 1], (state->tabCount - index - 1) * sizeof(Prog_Tab));
    --state->tabCount;
    state->tabs[state->tabCount] = (Prog_Tab){0};
    if (state->activeTab > index) --state->activeTab;
}

function void Prog_MoveTab(Prog_State *state, u32 from, u32 to)
{
    Prog__EnsureTab(state);
    if (from >= state->tabCount || to >= state->tabCount || from == to) return;
    Prog_Tab moved = state->tabs[from];
    if (from < to) MemMove(&state->tabs[from], &state->tabs[from + 1], (to - from) * sizeof(Prog_Tab));
    else MemMove(&state->tabs[to + 1], &state->tabs[to], (from - to) * sizeof(Prog_Tab));
    state->tabs[to] = moved;
    u32 active = state->activeTab;
    if (active == from) active = to;
    else if (from < active && active <= to) --active;
    else if (to <= active && active < from) ++active;
    state->activeTab = active;
}

// Coalesces bursts such as a copy or a build into a few reloads.
#define Prog_WatchInterval UINT64_C(100000)

// What a finished job's input still holds for the window: an expanded listing's last rows, if they were never taken.
function void Prog__ReleaseJob(P_Job *job)
{
    Prog_JobInput *input = job->input;
    if (input && input->partial) {
        if (input->partial->ready) ArenaRelease(&input->partial->arena);
        P_MutexRelease(&input->partial->mutex);
        input->partial = NULL;
    }
    P_JobRelease(job);
}

// Shows the rows an expanded listing has found so far in place of the shown ones, keeping the focus and the selection by name.
function void Prog__TakePartial(Prog_State *state, Prog_Directory *listed, Arena arena)
{
    if (!StrIsEqual(state->directory.path, listed->path)) {
        ArenaRelease(&arena);
        return;
    }
    String focus = state->hasFocus && state->focusedRow < state->directory.count ? state->directory.entries[state->focusedRow].file.name : (String){0};
    String anchor = state->hasAnchor && state->anchorRow < state->directory.count ? state->directory.entries[state->anchorRow].file.name : (String){0};
    struct { MapHeader header; struct { String key; b32 value; } *v; } selected = {0};
    for (u64 i = 0; i < state->directory.count; ++i) {
        if (state->directory.entries[i].selected) MapsPut(&state->frameArena, selected, state->directory.entries[i].file.name, true);
    }
    state->hasFocus = state->hasAnchor = false;
    state->selectedCount = 0;
    for (u64 i = 0; i < listed->count; ++i) {
        Prog_Entry *entry = &listed->entries[i];
        entry->selected = selected.header.count && MapsGet(selected, b32, entry->file.name);
        state->selectedCount += entry->selected != 0;
        if (focus.length && StrIsEqual(focus, entry->file.name)) { state->hasFocus = true; state->focusedRow = i; }
        if (anchor.length && StrIsEqual(anchor, entry->file.name)) { state->hasAnchor = true; state->anchorRow = i; }
    }
    // The menu's rows index the listing being replaced; Filey's own menus have none.
    if (!state->menuCommands) Prog_CloseMenu(state);
    ArenaRelease(&state->directoryArena);
    state->directoryArena = arena;
    state->directory = *listed;
    ++state->listing;
    Prog_ApplyFilter(state, false);
}

// Keeps the drawn `page` of the inspector's PDF in place of the same page at another width, else of the page in sight least recently.
function void Prog__KeepPdfPage(Prog_State *state, u32 page, P_Image image)
{
    Prog_PdfPage *slot = &state->pdf.drawn[0];
    for (u32 i = 0; i < Prog_PdfDrawn; ++i) {
        Prog_PdfPage *at = &state->pdf.drawn[i];
        if (at->texture.u64[0] && at->page == page) { slot = at; break; }
        if (!at->texture.u64[0] ? slot->texture.u64[0] != 0 : slot->texture.u64[0] && at->used < slot->used) slot = at;
    }
    if (slot->texture.u64[0]) R_TextureRelease(slot->texture);
    *slot = (Prog_PdfPage){ page, image.width, image.height, R_TextureCreateRGBA(image.width, image.height, image.pixels), UI->frameIndex };
    R_TextureSmooth(slot->texture);
}

// Lets go of the inspector's PDF, as another item shows.
function void Prog__DropPdf(Prog_State *state)
{
    for (u32 i = 0; i < Prog_PdfDrawn; ++i) R_TextureRelease(state->pdf.drawn[i].texture);
    MemZero(state->pdf.drawn, sizeof(state->pdf.drawn));
    if (state->pdf.job) P_RequestCancel(&state->pdf.job->cancelToken);
    state->pdf.job = NULL;
    state->pdf.pageCount = 0;
    state->pdf.sizes = NULL;
}

function b32 Prog_TakeCompletions(Prog_State *state)
{
    b32 changed = false;
    u64 now = PlatformNowMicroseconds();
    Prog__EnsurePanel(state);
    u32 active = state->activePanel;
    // Saved settings apply at once.
    // The index is written into the same folder while a walk goes on, so only a change to the settings or the schemes applies them again.
    if (P_DirectoryWatchTakeChanged(&state->configWatch)) {
        Scratch scratch = GetScratch();
        String text = {0};
        if (Prog__SettingsHash(state, &text, scratch.arena) != state->settingsHash) {
            Prog_ReloadSettingsFile(state);
            changed = true;
        }
        ReleaseScratch(scratch);
    }
    // Every panel watches its folder.
    for (u32 panel = 0; panel < state->panelCount; ++panel) {
        Prog__UsePanel(state, panel);
        if (P_DirectoryWatchTakeChanged(&state->watch)) state->watchDirty = true;
        if (state->watchDirty && !state->directoryJob && now - state->watchReloadTime >= Prog_WatchInterval) {
            state->watchDirty = false;
            state->watchReloadTime = now;
            Prog_Reload(state);
        }
        // A deferred reload needs frames until it runs.
        changed |= state->watchDirty;
        // An expanded listing shows the rows found so far while its walk goes on.
        Prog_JobInput *walking = state->directoryJob ? state->directoryJob->input : NULL;
        if (walking && walking->partial) {
            Prog_Partial *partial = walking->partial;
            P_MutexLock(&partial->mutex);
            b32 ready = partial->ready;
            Arena arena = partial->arena;
            Prog_Directory listed = partial->directory;
            partial->ready = false;
            partial->arena = (Arena){0};
            P_MutexUnlock(&partial->mutex);
            if (ready) {
                Prog__TakePartial(state, &listed, arena);
                changed = true;
            }
        }
    }
    Prog__UsePanel(state, active);
    // Drives are listed again every few seconds while the sidebar shows them, and at once when the system tells of one coming or going.
    if ((state->placesWanted || state->drivesChanged) && !state->placesJob && (state->drivesChanged || !state->placesTime || now - state->placesTime >= Millions(5))) {
        state->drivesChanged = false;
        state->placesTime = now;
        P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
        input->panel = Prog__PanelId(state);
        input->kind = Prog_JobKind_Places;
        input->pathCount = state->bookmarks.header.count;
        input->paths = ArenaPushArray(&job->arena, Max(input->pathCount, (u64)1), String);
        for (u64 i = 0; i < input->pathCount; ++i) input->paths[i] = StrCopy(&job->arena, state->bookmarks.v[i]);
        job->input = input;
        state->placesJob = job;
        P_JobSubmit(job);
    }
    // The index starts from the drives once they are known and walks on a slice at a time, while the last run's is read to search meanwhile.
    Prog_Index *index = &state->index;
    if (!state->indexStarted && state->drives.header.count) {
        state->indexStarted = true;
        for (u64 i = 0; i < state->drives.header.count; ++i) {
            String root = state->drives.v[i].path;
            if (!root.length) continue;
            Prog_IndexAdd(index, root, UINT32_MAX, 0);
        }
        if (state->cacheDirectory.length) {
            P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
            Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
            input->kind = Prog_JobKind_IndexLoad;
            input->name = Prog_JoinPath(&job->arena, state->cacheDirectory, Prog_IndexFile);
            job->input = input;
            state->indexLoadJob = job;
            P_JobSubmit(job);
        }
    }
    // The index waits while a panel walks a tree, which wants the disk to itself.
    b32 walking = state->directoryJob && ((Prog_JobInput *)state->directoryJob->input)->expanded;
    for (u32 i = 0; i < state->panelCount; ++i) {
        if (i != state->currentPanel && state->panels[i].directoryJob) walking |= ((Prog_JobInput *)state->panels[i].directoryJob->input)->expanded;
    }
    if (state->indexStarted && !state->indexDone && !state->indexJob && !walking) {
        // The next folders to list, past the files among them.
        u32 batch[4096];
        u64 batchCount = 0, at = state->indexAt;
        for (; at < index->count && batchCount < CArrayCount(batch); ++at) {
            if (!(index->flags[at] & Prog_IndexFlag_File)) batch[batchCount++] = (u32)at;
        }
        state->indexAt = batchCount ? batch[0] : at;
        if (!batchCount || index->count >= Prog_MaxIndexed) {
            state->indexDone = true;
            // The walk replaces the last run's, on disk too.
            Prog_IndexRelease(&state->savedIndex);
            if (state->cacheDirectory.length) {
                P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
                Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
                input->kind = Prog_JobKind_IndexFinish;
                input->name = Prog_JoinPath(&job->arena, state->cacheDirectory, Prog_IndexFile);
                input->path = StrFormat(&job->arena, "%S.walk", input->name);
                job->input = input;
                P_JobSubmit(job);
            }
        } else {
            P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
            Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
            input->kind = Prog_JobKind_Index;
            input->pathCount = batchCount;
            input->paths = ArenaPushArray(&job->arena, batchCount, String);
            input->entries = ArenaPushArray(&job->arena, batchCount, u32);
            for (u64 i = 0; i < batchCount; ++i) {
                input->entries[i] = batch[i];
                input->paths[i] = Prog_IndexPath(&job->arena, index, batch[i]);
            }
            input->room = Prog_MaxIndexed - index->count;
            input->restart = batch[0] == 0;
            if (state->cacheDirectory.length) input->name = StrFormat(&job->arena, "%S.walk", Prog_JoinPath(&job->arena, state->cacheDirectory, Prog_IndexFile));
            job->input = input;
            state->indexJob = job;
            P_JobSubmit(job);
        }
    }
    for (P_Job *job; (job = P_JobTakeCompleted(&state->jobs)) != NULL || (job = P_JobTakeCompleted(&state->thumbnailJobs)) != NULL;) {
        Prog_JobInput *input = job->input;
        // Results about a panel's folder go to that panel, and are dropped once it is closed.
        b32 windowWide = input->kind == Prog_JobKind_Icons || input->kind == Prog_JobKind_Inspect || input->kind == Prog_JobKind_GoTo || input->kind == Prog_JobKind_Index || input->kind == Prog_JobKind_Places || input->kind == Prog_JobKind_Save || input->kind == Prog_JobKind_WarmMenu ||
                         (input->kind == Prog_JobKind_FilesList && !input->miller) || input->kind == Prog_JobKind_Fonts;
        u32 owner = windowWide || !input->panel ? state->activePanel : Prog_PanelIndex(state, input->panel);
        if (owner == UINT32_MAX) {
            Prog__ReleaseJob(job);
            continue;
        }
        Prog__UsePanel(state, owner);
        if (input->kind == Prog_JobKind_Icons) {
            Prog__TakeIcons(state, job);
            changed = true;
        } else if (input->kind == Prog_JobKind_Preview) {
            // Only the latest one is shown; the texture is made on first use and refilled after.
            const Prog_Preview *preview = job->result;
            if (job == state->previewJob && preview && preview->hex) {
                // Its first page, and room for the rest as they are read.
                state->previewJob = NULL;
                u64 shown = Min(state->hexSize, (u64)Prog_HexRows * 16);
                state->hexPageCount = (shown + Prog_HexPageBytes - 1) / Prog_HexPageBytes;
                state->hexPages = ArenaPushArray(&state->previewArena, Max(state->hexPageCount, (u64)1), u8 *);
                if (state->hexPageCount) {
                    state->hexPages[0] = ArenaPushArrayNoZero(&state->previewArena, Prog_HexPageBytes, u8);
                    MemCopy(state->hexPages[0], preview->text.value, Min(preview->text.length, (u64)Prog_HexPageBytes));
                }
                state->previewHex = true;
                changed = true;
            } else if (job == state->previewJob && preview) {
                state->previewJob = NULL;
                state->previewText = StrCopy(&state->previewArena, preview->text);
                state->previewAnchor = state->previewCaret = 0;
                state->previewTextFocus = false;
                state->previewHex = false;
                state->previewArchive = preview->archive;
                state->previewDocument = preview->document;
                if (preview->model.indexCount) {
                    const Prog_ModelShape *shape = &preview->model;
                    state->model.mesh = R_MeshCreate(shape->positions, shape->uvs, shape->colors, shape->vertexCount, shape->indices, shape->indexCount);
                    state->model.images = ArenaPushArray(&state->previewArena, Max(shape->imageCount, 1u), R_TextureHandle);
                    state->model.imageCount = shape->imageCount;
                    for (u32 i = 0; i < shape->imageCount; ++i) {
                        state->model.images[i] = R_TextureCreateRGBA(shape->images[i].width, shape->images[i].height, shape->images[i].pixels);
                        R_TextureSmooth(state->model.images[i]);
                    }
                    state->model.parts = ArenaPushArray(&state->previewArena, Max(shape->partCount, 1u), R_MeshPart);
                    state->model.partCount = shape->partCount;
                    for (u32 i = 0; i < shape->partCount; ++i) {
                        const Prog_MeshPart *part = &shape->parts[i];
                        u32 c = part->color;
                        state->model.parts[i] = (R_MeshPart){ .first = part->first, .count = part->count,
                                                              .color = { (f32)(c & 0xFF) / 255, (f32)((c >> 8) & 0xFF) / 255, (f32)((c >> 16) & 0xFF) / 255, 1 } };
                        if (part->image < shape->imageCount) state->model.parts[i].texture = state->model.images[part->image];
                    }
                    MemCopy(state->model.center, preview->model.center, sizeof(state->model.center));
                    state->model.radius = preview->model.radius;
                    state->model.zUp = preview->model.zUp;
                    state->model.stale = true;
                }
                state->previewArchiveTotals = preview->archiveTotals;
                state->previewArchiveCounted = preview->archiveCounted;
                // Where its lines start, for the inspector; a last newline ends a line rather than starting one.
                String text = state->previewText;
                u32 lines = 0;
                for (u64 i = 0; i < text.length; ++i) lines += text.value[i] == '\n';
                if (text.length && text.value[text.length - 1] != '\n') ++lines;
                state->previewLines = ArenaPushArray(&state->previewArena, lines + 1, u32);
                state->previewLineCount = lines;
                u32 line = 0;
                if (lines) state->previewLines[line++] = 0;
                for (u64 i = 0; i + 1 < text.length; ++i) if (text.value[i] == '\n') state->previewLines[line++] = (u32)(i + 1);
                state->previewLines[lines] = (u32)text.length;
                // A code file's colors: what each line starts inside of, in one pass, and the rest for the lines in sight as they are drawn.
                state->previewSyntax = (u8)(preview->archive ? Prog_Syntax_None : Prog_SyntaxForPath(state->previewPath));
                state->previewLineStates = NULL;
                if (state->previewSyntax) {
                    state->previewLineStates = ArenaPushArray(&state->previewArena, lines + 1, u8);
                    u8 open = 0;
                    for (u32 i = 0; i < lines; ++i) {
                        state->previewLineStates[i] = open;
                        open = Prog_SyntaxLine(state->previewSyntax, StrFromTo(text, state->previewLines[i], (i64)state->previewLines[i + 1]), open, NULL);
                    }
                }
                if (preview->pixels) {
                    // The texture is made again for a picture of another size.
                    if (state->previewTexture.u64[0] && (state->previewTextureWidth != preview->width || state->previewTextureHeight != preview->height)) {
                        R_TextureRelease(state->previewTexture);
                        state->previewTexture = (R_TextureHandle){0};
                    }
                    if (!state->previewTexture.u64[0]) state->previewTexture = R_TextureCreateRGBA(preview->width, preview->height, preview->pixels);
                    else R_TextureUpdateRGBA(state->previewTexture, 0, 0, preview->width, preview->height, preview->pixels);
                    if (state->previewTexture.u64[0]) R_TextureSmooth(state->previewTexture);
                    state->previewTextureWidth = preview->width;
                    state->previewTextureHeight = preview->height;
                    state->previewPicture = state->previewTexture.u64[0] != 0;
                    state->previewImage = state->previewPicture && preview->image;
                    if (preview->pdfPages && state->previewPicture) {
                        state->pdf.sizes = ArenaPushArray(&state->previewArena, (u64)preview->pdfPages * 2, f32);
                        MemCopy(state->pdf.sizes, preview->pdfSizes, (u64)preview->pdfPages * 2 * sizeof(f32));
                        state->pdf.pageCount = preview->pdfPages;
                    }
                    state->previewImageWidth = preview->sourceWidth;
                    state->previewImageHeight = preview->sourceHeight;
                }
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_Git) {
            if (state->gitJob == job) state->gitJob = NULL;
            const Prog_GitStatus *status = job->result;
            if (status && StrIsEqual(input->path, state->directory.path)) {
                ArenaClear(&state->gitArena);
                state->git = (Prog_GitStatus){ .branch = StrCopy(&state->gitArena, status->branch), .ahead = status->ahead, .behind = status->behind, .changes = status->changes };
                MemCopy(state->git.counts, status->counts, sizeof(state->git.counts));
                for (u64 i = 0; i < status->marks.header.count; ++i) {
                    MapsPut(&state->gitArena, state->git.marks, StrCopy(&state->gitArena, status->marks.v[i].key), status->marks.v[i].value);
                }
                changed = true;
            }
            if (state->gitAgain) Prog_RequestGitStatus(state, state->directory.path);
        } else if (input->kind == Prog_JobKind_RecentsCheck) {
            if (state->recentsCheckJob == job) state->recentsCheckJob = NULL;
            // The recents left out by path, as they may have changed since the check started.
            if (input->rowCount) {
                Scratch scratch = GetScratch();
                u64 count = state->recents.header.count, kept = 0;
                String *paths = ArenaPushArray(scratch.arena, Max(count, (u64)1), String);
                Prog_RecentRank *ranks = ArenaPushArray(scratch.arena, Max(count, (u64)1), Prog_RecentRank);
                for (u64 i = 0; i < count; ++i) {
                    b32 gone = false;
                    for (u64 g = 0; g < input->rowCount && !gone; ++g) gone = StrIsEqual(state->recents.v[i], input->names[g]);
                    if (gone) continue;
                    paths[kept] = state->recents.v[i];
                    ranks[kept++] = state->recentRanks[i];
                }
                if (kept < count) {
                    Prog__SetRecents(state, paths, ranks, kept);
                    if (state->storeRecents == Prog_StoreRecents_Persistent) Prog__SaveList(state, Prog_RecentsFile, state->recents);
                    changed = true;
                }
                ReleaseScratch(scratch);
            }
            if (state->recentsCheckAgain) Prog_CheckRecents(state);
        } else if (input->kind == Prog_JobKind_Repos) {
            const Prog_Repos *found = job->result;
            if (state->reposJob == job) {
                state->reposJob = NULL;
                for (u64 i = 0; found && StrIsEqual(input->path, state->directory.path) && i < found->header.count; ++i) {
                    Prog_Repo repo = found->v[i].value, *known = MapsGetPtrNull(state->repos, found->v[i].key);
                    if (known) known->changes = input->offset && repo.changes < 0 ? 0 : repo.changes;
                    else MapsPut(&state->repoArena, state->repos, StrCopy(&state->repoArena, found->v[i].key), ((Prog_Repo){ StrCopy(&state->repoArena, repo.branch), -1 }));
                    changed = true;
                }
                // A status that failed or timed out counts as nothing to show, and the next one goes on.
                if (input->offset && state->reposNext < state->repos.header.count && state->repos.v[state->reposNext].value.changes < 0) state->repos.v[state->reposNext].value.changes = 0;
                Prog__NextRepoStatus(state);
            }
        } else if (input->kind == Prog_JobKind_PdfPage) {
            if (state->pdf.job == job) state->pdf.job = NULL;
            const P_Image *image = job->result;
            if (image && image->pixels && state->pdf.pageCount && StrIsEqual(input->path, state->previewPath)) Prog__KeepPdfPage(state, (u32)input->offset, *image);
            changed = true;
        } else if (input->kind == Prog_JobKind_HexPage) {
            if (state->hexPageJob == job) state->hexPageJob = NULL;
            const String *bytes = job->result;
            u64 page = input->offset / Prog_HexPageBytes;
            if (bytes && state->previewHex && StrIsEqual(input->path, state->previewPath) && page < state->hexPageCount && !state->hexPages[page]) {
                state->hexPages[page] = ArenaPushArray(&state->previewArena, Prog_HexPageBytes, u8);
                MemCopy(state->hexPages[page], bytes->value, Min(bytes->length, (u64)Prog_HexPageBytes));
            }
            changed = true;
        } else if (input->kind == Prog_JobKind_Open) {
            for (u32 i = 0; i < Prog_MaxTransfers; ++i) {
                if (state->transfers[i].job == job) state->transfers[i].job = NULL;
            }
            P_FileError *error = job->result;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrCopy(&state->errorArena, error->message);
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_Rename) {
            P_FileError *error = job->result;
            changed = true;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "Rename %S: %S", input->name, error->message);
            } else {
                String from = Prog_JoinPath(&state->frameArena, input->path, input->name), to = Prog_JoinPath(&state->frameArena, input->path, input->newName);
                Prog_PushUndo(state, Prog_UndoKind_Rename, &from, &to, 1);
                Prog__FollowMoved(state, &from, &to, 1);
                state->placesTime = 0;
                // Reloading now, rather than on the watch, selects the new name without waiting.
                String listed = input->prefix.length ? StrFormat(&state->frameArena, "%S/%S", input->prefix, input->newName) : input->newName;
                String folder = input->prefix.length ? Prog_JoinPath(&state->frameArena, state->directory.path, input->prefix) : state->directory.path;
                // While the next item is being renamed, the selection stays with it.
                if (StrIsEqual(folder, input->path) && state->renaming) Prog_Reload(state);
                else if (StrIsEqual(folder, input->path) && listed.length <= sizeof(state->selectText)) {
                    MemCopy(state->selectText, listed.value, listed.length);
                    state->selectLength = (u32)listed.length;
                    Prog_Reload(state);
                }
            }
        } else if (input->kind == Prog_JobKind_Transfer || input->kind == Prog_JobKind_Trash || input->kind == Prog_JobKind_Delete || input->kind == Prog_JobKind_Create ||
                   input->kind == Prog_JobKind_Link || input->kind == Prog_JobKind_EmptyTrash || input->kind == Prog_JobKind_Restore || input->kind == Prog_JobKind_BinWait) {
            changed = true;
            // The bin's listing has no watcher to see it emptied, and its icon shows whether it holds anything.
            b32 binChanged = input->kind == Prog_JobKind_EmptyTrash || input->kind == Prog_JobKind_Restore || input->kind == Prog_JobKind_BinWait;
            if (binChanged && P_IsTrashFolder(state->directory.path)) Prog_Reload(state);
            if (binChanged || input->kind == Prog_JobKind_Trash || (input->kind == Prog_JobKind_Delete && P_IsTrashFolder(input->path))) {
                Prog_ReloadFolderIcon(state, P_TrashFolder(&state->frameArena));
            }
            for (u32 i = 0; i < Prog_MaxTransfers; ++i) {
                if (state->transfers[i].job == job) state->transfers[i].job = NULL;
            }
            b32 created = input->kind == Prog_JobKind_Create || input->kind == Prog_JobKind_Link;
            P_FileError error = created ? (job->result ? ((Prog_Created *)job->result)->error : (P_FileError){0})
                              : job->result ? *(P_FileError *)job->result : (P_FileError){0};
            String verb = input->kind == Prog_JobKind_Trash ? S("Trash") : input->kind == Prog_JobKind_Delete || input->kind == Prog_JobKind_EmptyTrash ? S("Delete") : input->kind == Prog_JobKind_Create ? S("Create") : input->kind == Prog_JobKind_Link ? S("Paste shortcut") :
                          input->kind == Prog_JobKind_Restore ? S("Restore") : input->move ? S("Move") : S("Copy");
            // One cancelled from its card or with Escape stops without an error.
            if (error.code && !P_IsCancelled(&job->cancelToken)) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "%S: %S", verb, error.message);
            } else if (created && StrIsEqual(state->directory.path, input->path)) {
                // The new item is selected once the listing shows it, and a new file or folder renamed.
                String name = ((Prog_Created *)job->result)->name;
                if (name.length <= sizeof(state->selectText)) {
                    MemCopy(state->selectText, name.value, name.length);
                    state->selectLength = (u32)name.length;
                    state->selectRename = input->kind == Prog_JobKind_Create;
                }
            }
            // Copies, moves and new items that went through can be undone.
            if (!error.code && input->kind == Prog_JobKind_Transfer && job->result) {
                const Prog_Transferred *result = job->result;
                // What was pasted into the folder shown is selected once its listing shows it, all of it.
                if (result->placed.header.count && StrIsEqual(state->directory.path, input->path)) {
                    ArenaClear(&state->pickArena);
                    state->pickNames = (ArrayStr){0};
                    for (u64 i = 0; i < result->placed.header.count; ++i) {
                        ArrayPush(&state->pickArena, state->pickNames, StrCopy(&state->pickArena, Prog__FolderName(result->placed.v[i])));
                    }
                }
                if (result->placed.header.count == input->pathCount) {
                    Prog_PushUndo(state, input->move ? Prog_UndoKind_Move : Prog_UndoKind_Copy, input->move ? input->paths : NULL, result->placed.v, input->pathCount);
                    if (input->move) Prog__FollowMoved(state, input->paths, result->placed.v, input->pathCount);
                }
            }
            // Items sent to the bin can be taken back out.
            if (!error.code && input->kind == Prog_JobKind_Trash) Prog_PushUndo(state, Prog_UndoKind_Trash, NULL, input->paths, input->pathCount);
            if (!error.code && input->kind == Prog_JobKind_Create && job->result) {
                String created = Prog_JoinPath(&state->frameArena, input->path, ((Prog_Created *)job->result)->name);
                Prog_PushUndo(state, Prog_UndoKind_Create, NULL, &created, 1);
            }
            // Reloading now shows the result without waiting for the watch, and the places, one of whose folders may be gone, are listed again.
            if (!error.code) Prog_Reload(state);
            state->placesTime = 0;
        } else if (input->kind == Prog_JobKind_Undo) {
            P_FileError *error = job->result;
            changed = true;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, input->redo ? "Redo: %S" : "Undo: %S", error->message);
            } else if (input->undo.kind == Prog_UndoKind_Trash) {
                // Back out of the bin, or into it again, which the other stack then reverses; what came back is selected where it shows.
                const Prog_Undo *done = &input->undo;
                Prog__PushStep(input->redo ? &state->undoArena : &state->redoArena, input->redo ? &state->undo : &state->redo, Prog_UndoKind_Trash, NULL, done->to, done->count);
                Prog_ReloadFolderIcon(state, P_TrashFolder(&state->frameArena));
                if (!input->redo) {
                    ArenaClear(&state->pickArena);
                    state->pickNames = (ArrayStr){0};
                    for (u64 i = 0; i < done->count; ++i) {
                        if (Prog__SamePath(Prog_ParentPath(done->to[i]), state->directory.path)) ArrayPush(&state->pickArena, state->pickNames, StrCopy(&state->pickArena, Prog_EntryLeaf(done->to[i])));
                    }
                }
            } else if (input->undo.kind == Prog_UndoKind_Rename || input->undo.kind == Prog_UndoKind_Move) {
                // The items are back at `from`, so the way back to `to` is the same step the other way round. New items and copies went to the
                // trash, which Redo does not take them out of.
                const Prog_Undo *done = &input->undo;
                Prog__FollowMoved(state, done->to, done->from, done->count);
                if (input->redo) Prog__PushStep(&state->undoArena, &state->undo, (Prog_UndoKind)done->kind, done->to, done->from, done->count);
                else Prog__PushStep(&state->redoArena, &state->redo, (Prog_UndoKind)done->kind, done->to, done->from, done->count);
            }
            Prog_Reload(state);
        } else if (input->kind == Prog_JobKind_Index) {
            if (state->indexJob == job) state->indexJob = NULL;
            const Prog_Indexed *result = job->result;
            if (result && !P_IsCancelled(&job->cancelToken)) {
                Prog_Index *index = &state->index;
                // Past the folders listed, and the files between them.
                state->indexAt = result->listed < input->pathCount ? input->entries[result->listed] : input->entries[input->pathCount - 1] + (u64)1;
                for (u64 i = 0; i < result->count; ++i) Prog_IndexAdd(index, result->names[i], result->parents[i], (u8)result->flags[i]);
                // Another frame hands out the next slice.
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_IndexLoad) {
            if (state->indexLoadJob == job) state->indexLoadJob = NULL;
            const Prog_Indexed *result = job->result;
            // Kept in the job's arena, where its names point into the file read.
            if (result && result->count && !state->indexDone && !P_IsCancelled(&job->cancelToken)) {
                Prog_Index *saved = &state->savedIndex;
                Prog_IndexRelease(saved);
                *saved = (Prog_Index){ .arena = job->arena, .names = result->packed, .parents = result->parents, .flags = (u8 *)result->flags, .count = result->count };
                job->arena = (Arena){0};
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_GoTo) {
            if (state->gotoJob == job) state->gotoJob = NULL;
            String wanted = StrMake((const char *)state->gotoFolder, state->gotoFolderLength);
            if (job->result && StrIsEqual(input->path, wanted) && !P_IsCancelled(&job->cancelToken)) {
                ArenaRelease(&state->gotoArena);
                state->gotoArena = job->arena;
                job->arena = (Arena){0};
                state->gotoListing = *(Prog_GoToListed *)job->result;
                state->gotoListed = true;
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_Reveal) {
            if (job->result && input->generation == state->generation) Prog_ShowInFolder(state, *(String *)job->result, input->inTab);
            changed = true;
        } else if (input->kind == Prog_JobKind_Fonts) {
            if (state->fontsJob == job) state->fontsJob = NULL;
            if (job->result && !P_IsCancelled(&job->cancelToken)) {
                ArenaRelease(&state->fontsArena);
                state->fontsArena = job->arena;
                job->arena = (Arena){0};
                const Prog_FontsListed *listed = job->result;
                state->fontNames = listed->names;
                state->fontCount = listed->count;
                // The picker, open meanwhile, goes to the family the option has.
                if (state->paletteOpen && state->paletteMode == Prog_PaletteMode_Fonts && !state->palette.length) Prog__OpenFontPicker(state, (Prog_Option)state->fontOption);
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_FilesList && input->miller) {
            for (u32 i = 0; state->millerColumns && i < Prog_MillerSlots; ++i) {
                Prog_MillerColumn *column = &state->millerColumns->v[i];
                if (column->job != job) continue;
                column->job = NULL;
                if (!job->result || P_IsCancelled(&job->cancelToken)) continue;
                ArenaRelease(&column->arena);
                column->arena = job->arena;
                job->arena = (Arena){0};
                const Prog_FilesListed *listed = job->result;
                column->items = listed->items;
                column->count = listed->count;
                column->listed = true;
                column->listedAt = PlatformNowMicroseconds();
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_FilesList) {
            if (state->filesJob == job) state->filesJob = NULL;
            if (job->result && state->filesOpen && StrIsEqual(input->path, state->filesFolder) && !P_IsCancelled(&job->cancelToken)) {
                ArenaRelease(&state->filesListArena);
                state->filesListArena = job->arena;
                job->arena = (Arena){0};
                const Prog_FilesListed *listed = job->result;
                state->filesItems = listed->items;
                state->filesCount = listed->count;
                state->filesListed = true;
                state->filesWidest = 0;
                // The folder the path goes on into is lit, until something is typed.
                for (u64 i = 0; state->filesChild.length && !Prog_EditText(&state->filesSearch).length && i < listed->count; ++i) {
                    if (StrIsEqual(listed->items[i].file.name, state->filesChild)) {
                        state->filesHighlight = (u32)i;
                        state->filesLit = state->filesReveal = true;
                    }
                }
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_Inspect) {
            if (state->tipJob == job) state->tipJob = NULL;
            if (job->result && StrIsEqual(input->path, state->tipPath) && !P_IsCancelled(&job->cancelToken)) {
                state->tip = *(Prog_Inspected *)job->result;
                state->tipKnown = true;
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_Places) {
            if (state->placesJob == job) state->placesJob = NULL;
            if (job->result) {
                Prog_Places *result = job->result;
                Prog__LeaveLostDrives(state, state->drives, result->drives);
                Prog__RelistDrives(state, state->drives, result->drives);
                // A busy drive keeps the name it last had, which it may give only once it answers.
                for (u64 i = 0; i < result->drives.header.count; ++i) {
                    P_Place *drive = &result->drives.v[i];
                    const P_Place *known = drive->busy ? Prog_PathDrive(state, drive->path) : NULL;
                    if (known && StrIsEqual(known->path, drive->path)) drive->name = StrCopy(&job->arena, known->name);
                }
                ArenaRelease(&state->placesArena);
                state->placesArena = job->arena;
                job->arena = (Arena){0};
                // This PC leads the places.
                state->places = (P_ArrayPlace){0};
                ArrayPush(&state->placesArena, state->places, ((P_Place){ .name = Prog_ThisPC, .path = Prog_ThisPC }));
                for (u64 i = 0; i < result->places.header.count; ++i) ArrayPush(&state->placesArena, state->places, result->places.v[i]);
                state->drives = result->drives;
                state->pathRoots = result->roots;
                state->searchPlaces = result->searched;
                state->missingBookmarks = result->missing;
                changed = true;
            }
        } else if (input->kind == Prog_JobKind_Save) {
            P_FileError *error = job->result;
            if (error && error->code) {
                ArenaClear(&state->errorArena);
                state->error = StrFormat(&state->errorArena, "Save %S: %S", input->path, error->message);
                changed = true;
            }
            const String files[] = { Prog_BookmarksFile, Prog_RecentsFile, Prog_FolderViewsFile, Prog_MenuPinsFile, Prog_QuickCommandsFile };
            u32 slot = Prog__SaveSlot(Prog_EntryLeaf(input->path));
            state->saving[slot] = false;
            if (state->saveAgain[slot]) {
                state->saveAgain[slot] = false;
                Prog__SaveList(state, files[slot], slot == 1 ? state->recents : slot == 2 ? state->folderViews : slot == 3 ? state->menuPins : slot == 4 ? state->quickCommands : state->bookmarks);
            }
        } else if (input->kind == Prog_JobKind_WarmMenu) {
        } else if (input->kind == Prog_JobKind_Measure) {
            Prog_Measured *result = job->result;
            if (state->measureJob == job) state->measureJob = NULL;
            if (result && StrIsEqual(input->path, state->directory.path) && !P_IsCancelled(&job->cancelToken)) {
                // A refresh may have replaced the listing while the job ran.
                b32 sameListing = input->listing == state->listing;
                struct { MapHeader header; struct { String key; u64 value; } *v; } rows = {0};
                if (!sameListing) {
                    for (u64 i = 0; i < state->directory.count; ++i) MapsPut(&state->frameArena, rows, state->directory.entries[i].file.name, i + 1);
                }
                for (u64 i = 0; i < result->count; ++i) {
                    u64 row = sameListing ? input->rows[i] : MapsGet(rows, u64, input->names[i]) - 1;
                    if (row >= state->directory.count || state->directory.entries[row].file.kind != P_FileKind_Directory) continue;
                    Prog_Entry *entry = &state->directory.entries[row];
                    entry->measured = true;
                    entry->remeasure = false;
                    entry->folders = result->totals[i].folders;
                    entry->files = result->totals[i].files;
                    entry->bytes = result->totals[i].bytes;
                    // Kept for the next time the folder is shown.
                    if (state->sizes.header.count >= Prog_MaxCachedSizes) {
                        ArenaClear(&state->sizeArena);
                        state->sizes = (Prog_SizeMap){0};
                    }
                    String path = Prog_JoinPath(&state->frameArena, input->path, input->names[i]);
                    P_DirectoryTotals *known = MapsGetPtrNull(state->sizes, path);
                    if (known) *known = result->totals[i];
                    else MapsPut(&state->sizeArena, state->sizes, StrCopy(&state->sizeArena, path), result->totals[i]);
                }
                changed = true;
                Prog__SortByTotals(state);
            }
            Prog__MeasureFolders(state);
        } else if (input->generation == state->generation) {
            state->directoryJob = NULL;
            state->loading = false;
            state->listTook = PlatformNowMicroseconds() - Min(state->listStarted, PlatformNowMicroseconds());
            changed = true;
            Prog_Directory *result = job->result;
            if (result && !result->cancelled && !P_IsCancelled(&job->cancelToken)) {
                ArenaClear(&state->errorArena);
                state->error = (String){0};
                if (result->error.code) {
                    state->error = StrFormat(&state->errorArena, "%S: %S", input->path, result->error.message);
                    state->sort = state->directory.sort;
                    state->descending = state->directory.descending;
                    state->folderSort = state->directory.folderSort;
                    state->showHidden = state->directory.showHidden;
                } else {
                    b32 samePath = StrIsEqual(state->directory.path, result->path);
                    String focus = state->hasFocus ? state->directory.entries[state->focusedRow].file.name : (String){0};
                    u64 oldFocusedRow = state->focusedRow;
                    String anchor = state->hasAnchor ? state->directory.entries[state->anchorRow].file.name : (String){0};
                    struct { MapHeader header; struct { String key; b32 value; } *v; } selected = {0};
                    if (samePath) {
                        for (u64 i = 0; i < state->directory.count; ++i) {
                            if (state->directory.entries[i].selected) MapsPut(&state->frameArena, selected, state->directory.entries[i].file.name, true);
                        }
                    }
                    // After a shell command, the one item it created is selected, and renamed after one of New's, as in Explorer.
                    // Commands such as Send to a zip may finish after this listing, so the wait spans a few.
                    b32 renameCreated = samePath && now < state->renameCreatedUntil;
                    if (!renameCreated) state->renameCreatedUntil = 0;
                    struct { MapHeader header; struct { String key; b32 value; } *v; } known = {0};
                    if (renameCreated) {
                        for (u64 i = 0; i < state->directory.count; ++i) MapsPut(&state->frameArena, known, state->directory.entries[i].file.name, true);
                    }
                    // Refreshes keep the totals of folders that were not modified, so a busy folder is not walked again and again; an expanded
                    // listing counted them itself.
                    struct { MapHeader header; struct { String key; Prog_Entry *value; } *v; } totals = {0};
                    if (samePath && !result->expanded) {
                        for (u64 i = 0; i < state->directory.count; ++i) {
                            Prog_Entry *old = &state->directory.entries[i];
                            if (old->measured) MapsPut(&state->frameArena, totals, old->file.name, old);
                        }
                    }
                    // A name to select applies in another folder too, as Go To's Ctrl+Enter shows an item in its own.
                    String pick = StrMake((const char *)state->selectText, state->selectLength);
                    u64 created = 0, pickRow = UINT64_MAX;
                    b32 renameStillThere = false;
                    String renameFrom = StrMake((const char *)state->renameFrom, state->renameFromLength);
                    state->hasFocus = state->hasAnchor = false;
                    state->selectedCount = 0;
                    for (u64 i = 0; i < result->count; ++i) {
                        Prog_Entry *entry = &result->entries[i];
                        // Each lookup hashes the name, which a big tree has millions of, so empty maps are not asked.
                        entry->selected = selected.header.count && MapsGet(selected, b32, entry->file.name);
                        state->selectedCount += entry->selected != 0;
                        if (samePath && StrIsEqual(focus, entry->file.name)) { state->hasFocus = true; state->focusedRow = i; }
                        if (samePath && StrIsEqual(anchor, entry->file.name)) { state->hasAnchor = true; state->anchorRow = i; }
                        if (renameCreated && !(known.header.count && MapsGet(known, b32, entry->file.name)) && ++created == 1) pickRow = i;
                        if (pick.length && StrIsEqual(pick, entry->file.name)) pickRow = i;
                        renameStillThere |= samePath && StrIsEqual(renameFrom, entry->file.name);
                        Prog_Entry **old = totals.header.count ? MapsGetPtrNull(totals, entry->file.name) : NULL;
                        if (old && (*old)->file.kind == entry->file.kind && (*old)->file.modifiedTimeUnix == entry->file.modifiedTimeUnix) {
                            entry->measured = true;
                            entry->folders = (*old)->folders;
                            entry->files = (*old)->files;
                            entry->bytes = (*old)->bytes;
                        } else if (!samePath && entry->file.kind == P_FileKind_Directory && !entry->measured && state->sizes.header.count) {
                            P_DirectoryTotals *known = MapsGetPtrNull(state->sizes, Prog_JoinPath(&state->frameArena, result->path, entry->file.name));
                            if (known) {
                                entry->measured = entry->remeasure = true;
                                entry->folders = known->folders;
                                entry->files = known->files;
                                entry->bytes = known->bytes;
                            }
                        }
                    }
                    // The focused item gone, deleted or moved away, the one that took its place has the focus, so that the view stays where it is
                    // rather than going back to the top for the first row.
                    if (samePath && focus.length && !state->hasFocus && result->count) {
                        state->hasFocus = true;
                        state->focusedRow = Min(oldFocusedRow, result->count - 1);
                        if (!state->hasAnchor) state->hasAnchor = true, state->anchorRow = state->focusedRow;
                    }
                    if (renameCreated && created != 1 && !pick.length) pickRow = UINT64_MAX;
                    // The filter resets in another folder unless pinned, though not on a tab's first listing, and gives way to an item that was just created or renamed.
                    if ((!samePath && state->directory.path.length && !state->filterSticky) || (pickRow < result->count && Prog_EntryScore(result->entries[pickRow].file.name, Prog_EditText(&state->filter), NULL) <= 0)) {
                        Prog_ClearFilter(state);
                    }
                    // So do the types shown, which the Types popup set for this folder's.
                    if (!samePath && state->directory.path.length && !state->filterSticky) state->types.length = 0;
                    b32 renamePicked = (renameCreated && created == 1 && !pick.length && state->createdRenames) || (state->selectRename && pick.length && pickRow < result->count);
                    state->selectRename = false;
                    if (created) state->renameCreatedUntil = 0;
                    state->selectLength = 0;
                    if (!renameStillThere) state->renaming = false;
                    if (pickRow < result->count) {
                        for (u64 i = 0; i < result->count; ++i) result->entries[i].selected = i == pickRow;
                        state->selectedCount = 1;
                        state->hasFocus = state->hasAnchor = true;
                        state->focusedRow = state->anchorRow = pickRow;
                        state->revealFocus = !state->pickScrollSet;
                    }
                    // A moved tab's selection, all of it, by name.
                    if (state->pickNames.header.count) {
                        struct { MapHeader header; struct { String key; b32 value; } *v; } picked = {0};
                        for (u64 i = 0; i < state->pickNames.header.count; ++i) MapsPut(&state->frameArena, picked, state->pickNames.v[i], true);
                        state->selectedCount = 0;
                        for (u64 i = 0; i < result->count; ++i) {
                            result->entries[i].selected = MapsGet(picked, b32, result->entries[i].file.name);
                            state->selectedCount += result->entries[i].selected != 0;
                        }
                        ArenaClear(&state->pickArena);
                        state->pickNames = (ArrayStr){0};
                    }
                    // Another folder's repository is looked for at once; the same folder's, listed again, now and then, for commits made meanwhile.
                    if (!samePath || PlatformNowMicroseconds() - state->gitTime > Millions(3)) {
                        if (!samePath) state->git = (Prog_GitStatus){ .ahead = -1, .behind = -1 };
                        Prog_RequestGitStatus(state, result->path);
                    }
                    // Its repositories at once for another folder; for the same one listed again, every half minute at most.
                    b32 lookForRepos = !samePath || PlatformNowMicroseconds() - state->reposTime > Millions(30);
                    // The folder a panel starts on is not one opened, nor one gone back or forward to, or up to on the way somewhere.
                    if (!samePath) {
                        state->resetScroll = true;
                        Prog__DecayRecents(state, result->path, state->directory.path);
                        if (state->directory.path.length && input->addHistory && !input->passing) Prog__AddRecent(state, result->path, false, result->hidden);
                    }
                    Prog__NoteFolders(state, result->path, result->entries, result->count);
                    if (input->addHistory && (!state->history.header.count || !StrIsEqual(state->history.v[state->historyAt], result->path))) {
                        if (state->history.header.count) state->history.header.count = state->historyAt + 1;
                        ArrayPush(&state->historyArena, state->history, StrCopy(&state->historyArena, result->path));
                        state->historyAt = state->history.header.count - 1;
                    } else if (!input->addHistory && state->history.header.count) {
                        state->historyAt = input->historyAt;
                        // A folder gone, listed as the one above it, is that one in the history too.
                        if (state->historyAt < state->history.header.count && !StrIsEqual(result->path, input->path)) {
                            state->history.v[state->historyAt] = StrCopy(&state->historyArena, result->path);
                        }
                    }
                    // The menu's rows index the listing being replaced; Filey's own menus have none.
                    if (!state->menuCommands) Prog_CloseMenu(state);
                    ArenaRelease(&state->directoryArena);
                    state->directoryArena = job->arena;
                    state->directory = *result;
                    if (lookForRepos) Prog_RequestRepos(state);
                    job->arena = (Arena){0};
                    ++state->listing;
                    Prog_ApplyFilter(state, false);
                    Prog__SortByTotals(state);
                    // A walk under way in this folder carries on; refreshes would otherwise restart it forever.
                    if (!samePath && state->measureJob) {
                        P_RequestCancel(&state->measureJob->cancelToken);
                        state->measureJob = NULL;
                    }
                    Prog__MeasureFolders(state);
                    // ponytail: changes between enumerating and starting the watch are missed; start it before the listing if that shows.
                    if (!samePath || !state->watch.native) {
                        P_DirectoryWatchStart(&state->watch, Prog__WatchedFolder(state), state->jobs.wake, state->jobs.wakeUser);
                        state->watchDirty = false;
                    }
                    if (renamePicked) Prog_BeginRename(state, pickRow);
                    if (state->warmMenus && state->directory.count) {
                        state->warmMenus = false;
                        u64 file = 0;
                        while (file + 1 < state->directory.count && state->directory.entries[file].file.kind == P_FileKind_Directory) ++file;
                        P_Job *warm = P_JobCreate(&state->jobs, Prog__DirectoryJob);
                        Prog_JobInput *warmInput = ArenaPushStruct(&warm->arena, Prog_JobInput);
                        warmInput->kind = Prog_JobKind_WarmMenu;
                        warmInput->path = StrCopy(&warm->arena, state->directory.path);
                        warmInput->name = StrCopy(&warm->arena, state->directory.entries[file].file.name);
                        warm->input = warmInput;
                        P_JobSubmit(warm);
                    }
                }
            }
        }
        Prog__ReleaseJob(job);
    }
    Prog__UsePanel(state, state->activePanel);
    return changed;
}

function void Prog_SelectRow(Prog_State *state, u64 row, KeyMods mods)
{
    u64 at = Prog_ViewPosition(state, row), anchorAt = state->hasAnchor ? Prog_ViewPosition(state, state->anchorRow) : UINT64_MAX;
    if (at == UINT64_MAX) return;
    if (!(mods & KeyMod_Control)) {
        for (u64 i = 0; i < state->directory.count; ++i) state->directory.entries[i].selected = false;
        state->selectedCount = 0;
    }
    if ((mods & KeyMod_Shift) && anchorAt != UINT64_MAX) {
        // The range runs over the rows the filter shows.
        for (u64 i = Min(at, anchorAt); i <= Max(at, anchorAt); ++i) {
            Prog_Entry *entry = &state->directory.entries[state->view[i]];
            if (!entry->selected) ++state->selectedCount;
            entry->selected = true;
        }
    } else {
        state->directory.entries[row].selected = (mods & KeyMod_Control) ? !state->directory.entries[row].selected : true;
        if (state->directory.entries[row].selected) ++state->selectedCount;
        else --state->selectedCount;
        state->anchorRow = row;
        state->hasAnchor = true;
    }
    state->focusedRow = row;
    state->hasFocus = true;
}

// Opens the file at `path` in its program, from a job. One inside an archive is taken out first, with a card of how far that is when there is
// a free slot for it.
function void Prog_OpenFile(Prog_State *state, String path)
{
    P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->panel = Prog__PanelId(state);
    input->kind = Prog_JobKind_Open;
    input->generation = state->generation;
    input->sort = state->directory.sort;
    input->descending = state->directory.descending;
    input->path = StrCopy(&job->arena, path);
    String archive = {0}, inner = {0};
    for (u32 i = 0; Prog_ArchiveSplit(path, &archive, &inner) && inner.length && i < Prog_MaxTransfers; ++i) {
        Prog_Transfer *transfer = &state->transfers[i];
        if (transfer->job) continue;
        String name = Prog_EntryLeaf(path);
        *transfer = (Prog_Transfer){ .job = job, .opening = true, .started = PlatformNowMicroseconds(), .targetLength = (u32)Min(name.length, sizeof(transfer->target)) };
        MemCopy(transfer->target, name.value, transfer->targetLength);
        input->progress = &transfer->progress;
        break;
    }
    job->input = input;
    P_JobSubmit(job);
}

function void Prog_OpenRow(Prog_State *state, u64 row)
{
    if (state->loading || row >= state->directory.count) return;
    Prog_Entry *entry = &state->directory.entries[row];
    String path = Prog_JoinPath(&state->frameArena, state->directory.path, entry->file.name);
    // An archive opens as a folder when no program of its own is set to open it, but for one inside another, which opens in its program.
    if (entry->file.kind == P_FileKind_Directory || (Prog_IsArchiveName(entry->file.name) && !state->directory.archive && !P_OpensInApp(path))) {
        Prog_RequestDirectory(state, path, true, 0);
    } else {
        Prog__AddRecent(state, path, true, entry->file.hidden);
        Prog_OpenFile(state, path);
    }
}

function void Prog_Release(Prog_State *state)
{
    Prog_CloseMenu(state);
    // The session is written while the panels still hold it; a window started beside the main one leaves it to that one.
    if (state->configDirectory.length && state->panelCount && !state->separateWindow) {
        Scratch scratch = GetScratch();
        P_WriteFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, Prog_SessionFile), Prog_LayoutText(scratch.arena, state));
        ReleaseScratch(scratch);
    }
    // Watch threads wake the window, like the workers, so they stop first.
    for (u32 panel = 0; panel < state->panelCount; ++panel) {
        Prog__UsePanel(state, panel);
        Prog__ReleaseCurrentPanel(state);
    }
    ArenaRelease(&state->menuArena);
    P_DirectoryWatchStop(&state->configWatch);
    ArenaRelease(&state->configWatch.arena);
    P_JobsShutdown(&state->thumbnailJobs);
    P_JobsShutdown(&state->jobs);
    // Shutting down drops saves still waiting in the queue, and the workers are done, so the lists are written once more here.
    if (state->configDirectory.length) {
        Scratch scratch = GetScratch();
        ArrayStr lists[2] = { state->bookmarks, state->recents };
        String files[2] = { Prog_BookmarksFile, Prog_RecentsFile };
        for (u32 i = 0; i < 2; ++i) {
            String text = i == 1 ? Prog__RecentsText(scratch.arena, state) : Prog__ListText(scratch.arena, lists[i]);
            P_WriteFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, files[i]), text);
        }
        ReleaseScratch(scratch);
    }
    UI_Release(&state->ui);
    ArenaRelease(&state->frameArena);
    ArenaRelease(&state->iconArena);
    for (u32 i = 0; i < Prog_ThumbnailSlots; ++i) ArenaRelease(&state->thumbnailSlots[i].arena);
    ArenaRelease(&state->folderPictureArena);
    ArenaRelease(&state->jobsArena);
    ArenaRelease(&state->thumbnailJobsArena);
    ArenaRelease(&state->thumbnailWaitArena);
    ArenaRelease(&state->warmArena);
    ArenaRelease(&state->closedArena);
    ArenaRelease(&state->confirmArena);
    ArenaRelease(&state->placesArena);
    ArenaRelease(&state->settingsArena);
    ArenaRelease(&state->bookmarkArena);
    ArenaRelease(&state->recentArena);
    ArenaRelease(&state->undoArena);
    ArenaRelease(&state->redoArena);
    ArenaRelease(&state->menuPinArena);
    ArenaRelease(&state->quickArena);
    ArenaRelease(&state->sizeArena);
    ArenaRelease(&state->tipArena);
    ArenaRelease(&state->gotoArena);
    ArenaRelease(&state->gotoHistoryArena);
    ArenaRelease(&state->filesArena);
    ArenaRelease(&state->filesListArena);
    ArenaRelease(&state->knownArena);
    Prog_IndexRelease(&state->index);
    Prog_IndexRelease(&state->savedIndex);
    ArenaRelease(&state->gotoFoundArena);
    ArenaRelease(&state->previewArena);
    *state = (Prog_State){0};
}
