// Browser UI.
// Sizes derive from the font's line height so the layout keeps its proportions at any font size and display scale.

#include <math.h>

#include "Meta/Icons.meta.h"
#include "Meta/Strings.meta.h"

// The languages built into Filey: their names as they call themselves, and their text, as a language file has it.
global const u8 Prog__GermanLanguage[] = {
#embed "../Resources/Languages/German.lang"
};
global const u8 Prog__SpanishLanguage[] = {
#embed "../Resources/Languages/Spanish.lang"
};
global const u8 Prog__FrenchLanguage[] = {
#embed "../Resources/Languages/French.lang"
};
global const u8 Prog__PolishLanguage[] = {
#embed "../Resources/Languages/Polish.lang"
};
global const u8 Prog__PortugueseLanguage[] = {
#embed "../Resources/Languages/Portuguese.lang"
};
global const u8 Prog__RussianLanguage[] = {
#embed "../Resources/Languages/Russian.lang"
};
global const u8 Prog__ChineseLanguage[] = {
#embed "../Resources/Languages/Chinese.lang"
};
global const struct { String name; String code; const u8 *text; u64 size; } Prog_BuiltInLanguages[] = {
    { NoTrInit("Deutsch"), SInit("de"), Prog__GermanLanguage, sizeof(Prog__GermanLanguage) },
    { SInit("Espa\u00F1ol"), SInit("es"), Prog__SpanishLanguage, sizeof(Prog__SpanishLanguage) },
    { SInit("Fran\u00E7ais"), SInit("fr"), Prog__FrenchLanguage, sizeof(Prog__FrenchLanguage) },
    { NoTrInit("Polski"), SInit("pl"), Prog__PolishLanguage, sizeof(Prog__PolishLanguage) },
    { SInit("Portugu\u00EAs"), SInit("pt"), Prog__PortugueseLanguage, sizeof(Prog__PortugueseLanguage) },
    { SInit("\u0420\u0443\u0441\u0441\u043A\u0438\u0439"), SInit("ru"), Prog__RussianLanguage, sizeof(Prog__RussianLanguage) },
    { SInit("\u7B80\u4F53\u4E2D\u6587"), SInit("zh"), Prog__ChineseLanguage, sizeof(Prog__ChineseLanguage) },
};

// The built-in language the system shows its text in, which is the one chosen until the settings choose another; English for none.
function String Prog_SystemLanguage(void)
{
    local_persist String result;
    local_persist b32 known;
    if (!known) {
        Scratch scratch = GetScratch();
        String code = P_SystemLanguage(scratch.arena);
        for (u64 i = 0; i < CArrayCount(Prog_BuiltInLanguages); ++i) if (StrIsEqual(code, Prog_BuiltInLanguages[i].code)) result = Prog_BuiltInLanguages[i].name;
        ReleaseScratch(scratch);
        known = true;
    }
    return result.length ? result : NoTr("English");
}

// The chosen language's translations by their English text, each ending in a zero for TrF.
global Arena Prog_LanguageArena;
global struct { MapHeader header; struct { String key; String value; } *v; } Prog_LanguageMap;
// The English text of each translation, for what is saved by the text shown, as the menus' items are.
// ponytail: two English texts translated alike come back as the one put last; files of names, not texts, if that ever matters.
global struct { MapHeader header; struct { String key; String value; } *v; } Prog_LanguageEnglish;

function String Prog_Tr(String english)
{
    if (!Prog_LanguageMap.header.count) return english;
    String *found = MapsGetPtrNull(Prog_LanguageMap, english);
    return found ? *found : english;
}

function String Prog_Untr(String shown)
{
    if (!Prog_LanguageEnglish.header.count) return shown;
    String *found = MapsGetPtrNull(Prog_LanguageEnglish, shown);
    return found ? *found : shown;
}

function const char *Prog_TrFormat(const char *english)
{
    String found = Prog_Tr(StrFromCStr(english));
    return found.value;
}

// How the chosen language picks a counted text's form, which its file names on a line "# plural: french" or "# plural: polish".
typedef enum Prog_Plural {
    Prog_Plural_Forms,   // by how many forms there are: one for every count, two as English's, three as Russian's for one, a few and many
    Prog_Plural_French,  // two, the first for 0 and 1, as French and Brazilian Portuguese have
    Prog_Plural_Polish,  // three, for 1 alone, a few (2 to 4, 22 to 24, but not 12 to 14) and many, as Polish has
    Prog_Plural_Count,
} Prog_Plural;
global Prog_Plural Prog_LanguagePlural;
global const char *const Prog_PluralNames[Prog_Plural_Count] = { "", "french", "polish" };

// The format of a counted text for `count`: a translation lists its forms apart by zeros, as its file separates them by " | ", and the
// language's rule picks one.
function const char *Prog_TrPlural(u64 count, const char *one, const char *other)
{
    String found = Prog_Tr(StrFromCStr(other));
    if (found.value == other) return count == 1 ? one : other;
    u32 forms = 1;
    for (u64 i = 0; i < found.length; ++i) forms += found.value[i] == 0;
    u64 last = count % 10, tens = count % 100;
    b32 few = last >= 2 && last <= 4 && (tens < 12 || tens > 14);
    u32 form = forms == 1 ? 0
             : forms == 2 ? (Prog_LanguagePlural == Prog_Plural_French ? count > 1 : count != 1)
             : Prog_LanguagePlural == Prog_Plural_Polish ? (count == 1 ? 0 : few ? 1 : 2)
             : (last == 1 && tens != 11 ? 0 : few ? 1 : 2);
    const char *result = found.value;
    while (form--) result += strlen(result) + 1;
    return result;
}

// The next conversion of a format from `*at`, as "%u" or "%.2f", past any "%%"; false at the end.
function b32 Prog__NextConversion(String s, u64 *at, String *conversion)
{
    while (*at < s.length) {
        if (s.value[*at] != '%') {
            ++*at;
            continue;
        }
        if (*at + 1 < s.length && s.value[*at + 1] == '%') {
            *at += 2;
            continue;
        }
        u64 end = *at + 1;
        while (end < s.length && !((s.value[end] >= 'a' && s.value[end] <= 'z') || (s.value[end] >= 'A' && s.value[end] <= 'Z'))) ++end;
        *conversion = StrFromTo(s, *at, (i64)Min(end + 1, s.length));
        *at = end + 1;
        return true;
    }
    return false;
}

// Whether `a` and `b` have the same conversions in the same order, which a translated format must keep.
function b32 Prog__SameConversions(String a, String b)
{
    u64 i = 0, j = 0;
    for (;;) {
        String x = {0}, y = {0};
        b32 more = Prog__NextConversion(a, &i, &x), other = Prog__NextConversion(b, &j, &y);
        if (more != other) return false;
        if (!more) return true;
        if (!StrIsEqual(x, y)) return false;
    }
}

// `text` with each "\\n" a line break, as a language file writes those of its one-line entries.
function String Prog__Unescape(Arena *arena, String text)
{
    char *out = ArenaPushArray(arena, text.length + 1, char);
    u64 length = 0;
    for (u64 i = 0; i < text.length; ++i) {
        b32 brk = text.value[i] == '\\' && i + 1 < text.length && text.value[i + 1] == 'n';
        out[length++] = brk ? '\n' : text.value[i];
        i += brk ? 1 : 0;
    }
    out[length] = 0;
    return StrMake(out, length);
}

// `text` with each line break written "\\n", for a language file.
function String Prog__Escape(Arena *arena, String text)
{
    StrBuilder out = {0};
    for (u64 i = 0; i < text.length; ++i) {
        if (text.value[i] == '\n') StrBuildStr(arena, &out, S("\\n"));
        else StrBuildChar(arena, &out, text.value[i]);
    }
    return out.buffer.value ? out.buffer : S("");
}

// Adds the entries of a language file's `text` over those there: each an English line, then its translation on a line of its own after "= ";
// lines starting with '#' are comments. A translation left empty, or one whose conversions differ from its English text's, is left out; a
// counted text's forms are separated by " | ", each with those conversions.
function void Prog_AddLanguageText(String text)
{
    String english = {0};
    for (u64 at = 0; at < text.length;) {
        Index end = StrIndexChar(StrFrom(text, at), '\n', SSF_CountOnFail);
        String line = StrFromTo(text, at, (i64)(at + end.value));
        at += end.value + 1;
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        if (line.length > 9 && StrIsEqual(StrFromTo(line, 0, 9), S("# plural:"))) {
            String rule = StrFrom(line, 9);
            while (rule.length && rule.value[0] == ' ') rule = StrFrom(rule, 1);
            while (rule.length && rule.value[rule.length - 1] == ' ') --rule.length;
            for (u32 r = 0; r < Prog_Plural_Count; ++r) if (StrIsEqual(rule, StrFromCStr(Prog_PluralNames[r]))) Prog_LanguagePlural = (Prog_Plural)r;
        }
        if (!line.length || line.value[0] == '#') continue;
        if (line.length >= 1 && line.value[0] == '=') {
            String translation = Prog__Unescape(&Prog_LanguageArena, StrFrom(line, line.length >= 2 && line.value[1] == ' ' ? 2 : 1));
            char *copy = ArenaPushArray(&Prog_LanguageArena, translation.length + 1, char);
            u64 length = 0;
            b32 kept = english.length && translation.length;
            for (u64 from = 0; kept && from <= translation.length;) {
                Index split = StrIndexStr(StrFrom(translation, from), S(" | "), SSF_None);
                String form = StrFromTo(translation, from, split.exists ? (i64)(from + split.value) : (i64)translation.length);
                kept = form.length && Prog__SameConversions(english, form);
                MemCopy(copy + length, form.value, form.length);
                length += form.length;
                copy[length] = 0;
                length += split.exists ? 1 : 0;
                from = split.exists ? from + split.value + 3 : translation.length + 1;
            }
            if (kept) {
                String key = Prog__Unescape(&Prog_LanguageArena, english);
                MapsPut(&Prog_LanguageArena, Prog_LanguageMap, key, StrMake(copy, length));
                if (length == translation.length) MapsPut(&Prog_LanguageArena, Prog_LanguageEnglish, StrMake(copy, length), key);
            }
            english = (String){0};
        } else {
            english = line;
        }
    }
}

// The menu items pinned, hidden and moved, which are kept by the text shown, from the shown language to English or back.
function void Prog__TranslateMenuLists(Prog_State *state, b32 toEnglish)
{
    struct { Arena *arena; ArrayStr *list; } lists[] = { { &state->menuPinArena, &state->menuPins }, { &state->menuHiddenArena, &state->menuHidden }, { &state->menuOrderArena, &state->menuOrder } };
    Scratch scratch = GetScratch();
    for (u32 l = 0; l < CArrayCount(lists); ++l) {
        String *items = ArenaPushArray(scratch.arena, lists[l].list->header.count + 1, String);
        for (u64 i = 0; i < lists[l].list->header.count; ++i) items[i] = toEnglish ? Prog_Untr(lists[l].list->v[i]) : Prog_Tr(lists[l].list->v[i]);
        Prog__SetList(lists[l].arena, lists[l].list, items, lists[l].list->header.count);
    }
    ReleaseScratch(scratch);
}

function void Prog__LoadLanguage(Prog_State *state, String name);

// Makes `name` the language shown: a built-in one, then a custom file of that name in the settings folder over it; English for none.
function void Prog_UseLanguage(Prog_State *state, String name)
{
    Prog__TranslateMenuLists(state, true);
    Prog__LoadLanguage(state, name);
    Prog__TranslateMenuLists(state, false);
    Prog_UseUnits();
    String code = {0};
    for (u64 i = 0; i < CArrayCount(Prog_BuiltInLanguages); ++i) if (StrIsEqual(name, Prog_BuiltInLanguages[i].name)) code = Prog_BuiltInLanguages[i].code;
    P_SetTypeWords(code, Tr("Folder"), Tr("File"), StrFromCStr(TrF("%S file")));
}

function void Prog__LoadLanguage(Prog_State *state, String name)
{
    ArenaClear(&Prog_LanguageArena);
    Prog_LanguageMap = (typeof(Prog_LanguageMap)){0};
    Prog_LanguageEnglish = (typeof(Prog_LanguageEnglish)){0};
    Prog_LanguagePlural = Prog_Plural_Forms;
    ArenaClear(&Prog__CommandTextArena);
    MemZero(Prog__CommandTexts, sizeof(Prog__CommandTexts));
    if (!name.length || Prog__EqualFolded(name, S("English"))) return;
    for (u64 i = 0; i < CArrayCount(Prog_BuiltInLanguages); ++i) {
        if (StrIsEqual(name, Prog_BuiltInLanguages[i].name)) Prog_AddLanguageText(StrMake((const char *)Prog_BuiltInLanguages[i].text, Prog_BuiltInLanguages[i].size));
    }
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    String text = {0};
    if (P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, StrFormat(scratch.arena, "%S.lang", name)), &text)) Prog_AddLanguageText(text);
    ReleaseScratch(scratch);
}

// Any other name than English's and a built-in language's is a custom language's.
function b32 Prog_LanguageIsCustom(String name)
{
    b32 result = name.length && !Prog__EqualFolded(name, S("English"));
    for (u64 i = 0; i < CArrayCount(Prog_BuiltInLanguages); ++i) result &= !StrIsEqual(name, Prog_BuiltInLanguages[i].name);
    return result;
}

// The custom languages in the settings folder by name, those named as a built-in one left out, as they only add to it.
function void Prog_ListLanguages(Prog_State *state)
{
    ArenaClear(&state->languagesArena);
    state->languageNames = (ArrayStr){0};
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    P_DirectoryResult listing = P_EnumerateDirectory(scratch.arena, state->configDirectory, NULL);
    for (u64 i = 0; i < listing.files.header.count; ++i) {
        String file = listing.files.v[i].name;
        if (listing.files.v[i].kind != P_FileKind_File || file.length <= 5 || !Prog__EqualFolded(StrFrom(file, file.length - 5), S(".lang"))) continue;
        String name = StrFromTo(file, 0, (i64)file.length - 5);
        if (Prog_LanguageIsCustom(name)) ArrayPush(&state->languagesArena, state->languageNames, StrCopy(&state->languagesArena, name));
    }
    ReleaseScratch(scratch);
}

// A language file of every translatable string, each with the shown language's translation where it has one, for a translation of one's own.
function String Prog_LanguageTemplate(Arena *arena, String name)
{
    StrBuilder text = {0};
    StrBuildStr(arena, &text, StrFormat(arena, "# Filey language: %S\n"
                                               "# Each entry is Filey's English text on one line and its translation on the next, after \"= \".\n"
                                               "# An entry left empty shows in English. Keep the %%u, %%S and other conversions of a line, in their order.\n"
                                               "# Saved, the file applies at once while this language is chosen in Options.\n", name));
    // The rule the shown language counts by, which a translation into another may change: "french", for two forms with 0 counted as one,
    // or "polish", for three told apart as Polish does.
    if (Prog_LanguagePlural != Prog_Plural_Forms) StrBuildStr(arena, &text, StrFormat(arena, "# plural: %s\n", Prog_PluralNames[Prog_LanguagePlural]));
    StrBuildStr(arena, &text, S("\n"));
    for (u64 i = 0; i + 1 < CArrayCount(Prog_Strings); ++i) {
        String english = StrFromCStr(Prog_Strings[i]), shown = Prog_Tr(english);
        StrBuildStr(arena, &text, StrFormat(arena, "%S\n= %S\n\n", Prog__Escape(arena, english), shown.value == english.value ? S("") : Prog__Escape(arena, shown)));
    }
    StrBuildStr(arena, &text, S("# Counted texts: one form for every count, or the forms for one and for the rest, or for one, a few and many as Russian has, separated by \" | \".\n"
                                "# A \"# plural: french\" line at the top counts 0 as one; \"# plural: polish\" tells three forms apart as Polish does.\n\n"));
    for (u64 i = 0; i + 1 < CArrayCount(Prog_PluralStrings); ++i) {
        String english = StrFromCStr(Prog_PluralStrings[i]), shown = Prog_Tr(english);
        StrBuildStr(arena, &text, StrFormat(arena, "%S\n= ", english));
        for (u64 c = 0; shown.value != english.value && c < shown.length; ++c) {
            if (shown.value[c]) StrBuildChar(arena, &text, shown.value[c]);
            else StrBuildStr(arena, &text, S(" | "));
        }
        StrBuildStr(arena, &text, S("\n\n"));
    }
    return text.buffer;
}

#define Prog_Rgb(r, g, b) { (r) / 255.0f, (g) / 255.0f, (b) / 255.0f, 1 }

typedef struct Prog_Theme {
    R_Color window;        // cleared behind everything
    R_Color background;    // toolbar and list base
    R_Color inner;         // fields inside panels, such as the palette's
    R_Color caption;       // the tab strip and the bar above the sidebar
    R_Color menu;          // context menus, which cover other content
    R_Color panel;         // sidebar and bottom pills
    R_Color alternateRow;
    R_Color separator;
    R_Color hover;
    R_Color selection;
    R_Color focus;         // outline of the focused row
    R_Color tabBorder;     // outline of the shown tab, which the strip's bottom line and the sidebar's edge continue
    R_Color text;
    R_Color textDim;
    R_Color textHeading;   // sidebar section titles
    R_Color textMatch;     // letters a filter matched
    R_Color marquee;       // the box drawn by dragging across rows
    R_Color accent;        // the filled part of the zoom bar
    R_Color track;         // the zoom bar behind its fill, and hotkey chips
    R_Color outline;       // the edge of hotkey chips
    R_Color danger;        // actions that cannot be undone
    R_Color icon;
    R_Color iconDisabled;
    R_Color file;          // the names of files in the lists
    R_Color folder;        // and of folders
    R_Color contentHover;  // the text of an item under the pointer
    R_Color contentSelection;
    R_Color contentDisabledSelection;  // of a selected item that would do nothing now
    R_Color outlineHover;  // the edge of a control under the pointer
    R_Color outlineSelection;          // of one on a selected row
    R_Color outlineDisabledSelection;
    R_Color matchHover;    // letters a filter matched, in an item under the pointer
    R_Color matchSelection;
    R_Color matchDisabledSelection;
    R_Color codeKeyword;   // the inspector's code: keywords, strings, numbers and headings; comments take textDim
    R_Color codeString;
    R_Color codeNumber;
    R_Color codeHeading;
    // Grays whose brightness is an amount: how much of the selection color washes an item under the pointer, and how much shows of a
    // hidden item and of one that would do nothing now.
    R_Color hoverAmount;
    R_Color hiddenAmount;
    R_Color disabledAmount;
} Prog_Theme;

#define Prog_Hex(v) { (((v) >> 16) & 255) / 255.0f, (((v) >> 8) & 255) / 255.0f, ((v) & 255) / 255.0f, 1 }

// The default scheme, the first built-in one, as Prog__ThemeFromScheme makes it from its colors.
global const Prog_Theme Prog_ThemeCustom = {
    .window                   = Prog_Hex(0x191919),
    .background               = Prog_Hex(0x191919),
    .inner                    = Prog_Hex(0x191919),
    .caption                  = Prog_Hex(0x1F1F1F),
    .menu                     = Prog_Hex(0x1F1F1F),
    .panel                    = Prog_Hex(0x1F1F1F),
    .alternateRow             = Prog_Hex(0x1C1C1C),
    .separator                = Prog_Hex(0x1C1C1C),
    .hover                    = Prog_Hex(0x595959),
    .selection                = Prog_Hex(0x505050),
    .focus                    = Prog_Hex(0x505050),
    .tabBorder                = Prog_Hex(0x383838),
    .text                     = Prog_Hex(0xDDDEE0),
    .textDim                  = Prog_Hex(0x6B7078),
    .textHeading              = Prog_Hex(0xA1A1A1),
    .textMatch                = Prog_Hex(0x00AAFF),
    .marquee                  = Prog_Hex(0x00ADEE),
    .accent                   = Prog_Hex(0x0079A6),
    .track                    = Prog_Hex(0x383C3D),
    .outline                  = Prog_Hex(0x1F1F1F),
    .danger                   = Prog_Hex(0xFF6358),
    .icon                     = Prog_Hex(0x9A9A9A),
    .iconDisabled             = Prog_Hex(0x464646),
    .file                     = Prog_Hex(0xDDDEE0),
    .folder                   = Prog_Hex(0xDDDEE0),
    .contentHover             = Prog_Hex(0xDDDEE0),
    .contentSelection         = Prog_Hex(0xDDDEE0),
    .contentDisabledSelection = Prog_Hex(0xA4A5A6),
    .outlineHover             = Prog_Hex(0x757C80),
    .outlineSelection         = Prog_Hex(0xA5AEB3),
    .outlineDisabledSelection = Prog_Hex(0x464A4D),
    .matchHover               = Prog_Hex(0x00B9FF),
    .matchSelection           = Prog_Hex(0x00AAFF),
    .matchDisabledSelection   = Prog_Hex(0x44B8E3),
    .codeKeyword              = Prog_Hex(0xC678DD),
    .codeString               = Prog_Hex(0x98C379),
    .codeNumber               = Prog_Hex(0xD19A66),
    .codeHeading              = Prog_Hex(0x61AFEF),
    .hoverAmount              = Prog_Hex(0x595959),
    .hiddenAmount             = Prog_Hex(0x595959),
    .disabledAmount           = Prog_Hex(0x595959),
};

// The built-in schemes' colors, popular themes' palettes and Filey's own dark one, in Prog_OptionSchemes' order, each by Prog_Option_ColorClear's
// order of the groups.
global const u32 Prog_SchemeColors[Prog_BuiltInSchemes][Prog_ColorGroupCount] = {
    { 0x191919, 0x1F1F1F, 0x191919, 0x1F1F1F, 0x383C3D, 0x191919, 0x383838, 0x1F1F1F, 0x1C1C1C, 0x1C1C1C, 0x9A9A9A, 0xDDDEE0, 0x6B7078, 0x999999, 0xDDDEE0, 0xDDDEE0, 0xFF6358, 0x0079A6, 0x505050, 0x00ADEE, 0x00AAFF, 0x595959, 0x595959, 0x595959, 0xDDDEE0, 0xDDDEE0, 0xA4A5A6, 0x757C80, 0xA5AEB3, 0x464A4D, 0x00B9FF, 0x00AAFF, 0x44B8E3, 0xC678DD, 0x98C379, 0xD19A66, 0x61AFEF },
    { 0x232634, 0x232634, 0x303446, 0x292C3C, 0x414559, 0x303446, 0x414559, 0x414559, 0x232634, 0x34384A, 0x949CBB, 0xC6D0F5, 0x838BA7, 0xA5ADCE, 0xC6D0F5, 0xC6D0F5, 0xE78284, 0x8CAAEE, 0x475270, 0x8CAAEE, 0x99D1DB, 0x595959, 0x595959, 0x595959, 0xC6D0F5, 0xC6D0F5, 0xA1A9C9, 0x737994, 0x949CBB, 0x626880, 0x99D1DB, 0x99D1DB, 0x7AA2AE, 0xCA9EE6, 0xA6D189, 0xEF9F76, 0x8CAAEE },
    { 0xDCE0E8, 0xDCE0E8, 0xEFF1F5, 0xE6E9EF, 0xCCD0DA, 0xEFF1F5, 0xCCD0DA, 0xCCD0DA, 0xDCE0E8, 0xE7EAEF, 0x7C7F93, 0x4C4F69, 0x8C8FA1, 0x6C6F85, 0x4C4F69, 0x4C4F69, 0xD20F39, 0x1E66F5, 0xC1D2F5, 0x1E66F5, 0x04A5E5, 0x808080, 0x666666, 0x666666, 0x4C4F69, 0x4C4F69, 0x75788C, 0x9CA0B0, 0x7C7F93, 0xACB0BE, 0x04A5E5, 0x04A5E5, 0x4BBCEA, 0x8839EF, 0x40A02B, 0xFE640B, 0x1E66F5 },
    { 0x242933, 0x242933, 0x2E3440, 0x292E39, 0x3B4252, 0x2E3440, 0x3B4252, 0x3B4252, 0x242933, 0x313744, 0x8A93A5, 0xD8DEE9, 0x616E88, 0xB8C0CE, 0xD8DEE9, 0xD8DEE9, 0xBF616A, 0x81A1C1, 0x434F60, 0x81A1C1, 0x88C0D0, 0x595959, 0x595959, 0x595959, 0xD8DEE9, 0xD8DEE9, 0xAEB4BF, 0x4C566A, 0x8A93A5, 0x4C566A, 0x88C0D0, 0x88C0D0, 0x6D96A5, 0x81A1C1, 0xA3BE8C, 0xB48EAD, 0x88C0D0 },
    { 0x1D2021, 0x1D2021, 0x282828, 0x222424, 0x3C3836, 0x282828, 0x3C3836, 0x3C3836, 0x1D2021, 0x2C2C2B, 0xA89984, 0xEBDBB2, 0x928374, 0xBDAE93, 0xEBDBB2, 0xEBDBB2, 0xFB4934, 0x83A598, 0x3F4744, 0x83A598, 0xFABD2F, 0x595959, 0x595959, 0x595959, 0xEBDBB2, 0xEBDBB2, 0xBAAE90, 0x7C6F64, 0xA89984, 0x665C54, 0xFABD2F, 0xFABD2F, 0xBB902D, 0xFB4934, 0xB8BB26, 0xD3869B, 0xFABD2F },
    { 0x13141C, 0x16161E, 0x1A1B26, 0x16161E, 0x292E42, 0x1A1B26, 0x292E42, 0x292E42, 0x13141C, 0x1D1F2C, 0x8F96BC, 0xC0CAF5, 0x737AA2, 0xA9B1D6, 0xC0CAF5, 0xC0CAF5, 0xF7768E, 0x7AA2F7, 0x323D5A, 0x7AA2F7, 0x7DCFFF, 0x595959, 0x595959, 0x595959, 0xC0CAF5, 0xC0CAF5, 0x979EC1, 0x545C7E, 0x8F96BC, 0x414868, 0x7DCFFF, 0x7DCFFF, 0x5F99BE, 0xBB9AF7, 0x9ECE6A, 0xFF9E64, 0x7AA2F7 },
    { 0x1E2227, 0x21252B, 0x282C34, 0x21252B, 0x31353F, 0x282C34, 0x31353F, 0x31353F, 0x1E2227, 0x2A2E36, 0x9DA5B4, 0xD7DAE0, 0x7F848E, 0xABB2BF, 0xD7DAE0, 0xD7DAE0, 0xE06C75, 0x61AFEF, 0x364D63, 0x61AFEF, 0x56B6C2, 0x595959, 0x595959, 0x595959, 0xD7DAE0, 0xD7DAE0, 0xABAFB5, 0x5C6370, 0x9DA5B4, 0x4B5263, 0x56B6C2, 0x56B6C2, 0x488D97, 0xC678DD, 0x98C379, 0xD19A66, 0x61AFEF },
};


function R_Color Prog__HexColor(u32 hex) { return (R_Color){ (f32)((hex >> 16) & 255) / 255.0f, (f32)((hex >> 8) & 255) / 255.0f, (f32)(hex & 255) / 255.0f, 1 }; }

// `from` moved toward `to` by `amount`, from 0 to 1, to the nearest byte.
function u32 Prog__ShiftHex(u32 from, u32 to, f32 amount)
{
    u32 result = 0;
    for (u32 shift = 0; shift < 24; shift += 8) {
        f32 a = (f32)((from >> shift) & 255), b = (f32)((to >> shift) & 255);
        result |= (u32)floorf(UI__Clamp(a + (b - a) * amount, 0, 255) + 0.5f) << shift;
    }
    return result;
}

// Each channel of `hex` raised by `levels`, up to 255.
function u32 Prog__RaiseHex(u32 hex, u32 levels)
{
    u32 result = 0;
    for (u32 shift = 0; shift < 24; shift += 8) result |= Min(((hex >> shift) & 255) + levels, 255u) << shift;
    return result;
}

// The theme a scheme's colors make: the popups have the surface's, the focus outline the selection's, and the disabled icons the
// icons' faded into the background by the disabled amount; the sections' titles show 8 levels brighter than their color, as measured in
// three schemes, within a level.
function Prog_Theme Prog__ThemeFromScheme(const u32 *groups)
{
#define Prog__Group(name) groups[Prog_Option_Color##name - Prog_Option_ColorClear]
#define Prog__GroupColor(name) Prog__HexColor(Prog__Group(name))
    Prog_Theme theme = {
        .window = Prog__GroupColor(Clear), .background = Prog__GroupColor(Background), .inner = Prog__GroupColor(Inner), .caption = Prog__GroupColor(Caption),
        .menu = Prog__GroupColor(Surface), .panel = Prog__GroupColor(Surface), .alternateRow = Prog__GroupColor(AlternatingRow), .separator = Prog__GroupColor(Separator),
        .hover = Prog__GroupColor(Hover), .selection = Prog__GroupColor(Selection), .focus = Prog__GroupColor(Selection), .tabBorder = Prog__GroupColor(Border),
        .text = Prog__GroupColor(Text), .textDim = Prog__GroupColor(Secondary), .textHeading = Prog__HexColor(Prog__RaiseHex(Prog__Group(Group), 8)),
        .textMatch = Prog__GroupColor(Match), .marquee = Prog__GroupColor(RectSelection), .accent = Prog__GroupColor(Progress), .track = Prog__GroupColor(Foreground),
        .outline = Prog__GroupColor(Outline), .danger = Prog__GroupColor(Warning), .icon = Prog__GroupColor(IconTint),
        .iconDisabled = Prog__HexColor(Prog__ShiftHex(Prog__Group(Background), Prog__Group(IconTint), (f32)(Prog__Group(Disabled) & 255) / 255.0f)),
        .file = Prog__GroupColor(File), .folder = Prog__GroupColor(Folder), .contentHover = Prog__GroupColor(ContentHover), .contentSelection = Prog__GroupColor(ContentSelection),
        .contentDisabledSelection = Prog__GroupColor(ContentDisabledSelection), .outlineHover = Prog__GroupColor(OutlineHover), .outlineSelection = Prog__GroupColor(OutlineSelection),
        .outlineDisabledSelection = Prog__GroupColor(OutlineDisabledSelection), .matchHover = Prog__GroupColor(MatchHover), .matchSelection = Prog__GroupColor(MatchSelection),
        .matchDisabledSelection = Prog__GroupColor(MatchDisabledSelection), .hoverAmount = Prog__GroupColor(Hover), .hiddenAmount = Prog__GroupColor(Hidden),
        .disabledAmount = Prog__GroupColor(Disabled), .codeKeyword = Prog__GroupColor(CodeKeyword), .codeString = Prog__GroupColor(CodeString),
        .codeNumber = Prog__GroupColor(CodeNumber), .codeHeading = Prog__GroupColor(CodeHeading),
    };
#undef Prog__GroupColor
#undef Prog__Group
    return theme;
}

global const Prog_Theme *Prog_Theme_ = &Prog_ThemeCustom;

function const Prog_Theme *Prog_CurrentTheme(void) { return Prog_Theme_; }

// The title strip's color, which dims to the panels' while another window has the focus; a see-through strip stays as it is.
function R_Color Prog__CaptionColor(const Prog_State *state)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    return state->windowInactive && theme->caption.a >= 1 ? theme->panel : theme->caption;
}

// The title bar's icons, brighter than the toolbar's: the text color at three quarters.
function R_Color Prog__CaptionInk(const Prog_Theme *theme)
{
    R_Color result = theme->text;
    result.a *= 0.75f;
    return result;
}

// The custom theme with the settings' colors.
global Prog_Theme Prog_ThemeUser = Prog_ThemeCustom;

// The theme's colors by the names the settings give them.
global const struct { const char *name; u64 offset; } Prog_ThemeColors[] = {
#define Prog__ThemeColor(field) { #field, offsetof(Prog_Theme, field) }
    Prog__ThemeColor(window), Prog__ThemeColor(background), Prog__ThemeColor(inner), Prog__ThemeColor(caption), Prog__ThemeColor(menu), Prog__ThemeColor(panel), Prog__ThemeColor(alternateRow),
    Prog__ThemeColor(separator), Prog__ThemeColor(hover), Prog__ThemeColor(selection), Prog__ThemeColor(focus), Prog__ThemeColor(tabBorder),
    Prog__ThemeColor(text), Prog__ThemeColor(textDim), Prog__ThemeColor(textHeading), Prog__ThemeColor(textMatch),
    Prog__ThemeColor(marquee), Prog__ThemeColor(accent), Prog__ThemeColor(track), Prog__ThemeColor(outline), Prog__ThemeColor(danger), Prog__ThemeColor(icon), Prog__ThemeColor(iconDisabled),
    Prog__ThemeColor(file), Prog__ThemeColor(folder), Prog__ThemeColor(contentHover), Prog__ThemeColor(contentSelection), Prog__ThemeColor(contentDisabledSelection),
    Prog__ThemeColor(outlineHover), Prog__ThemeColor(outlineSelection), Prog__ThemeColor(outlineDisabledSelection), Prog__ThemeColor(matchHover), Prog__ThemeColor(matchSelection),
    Prog__ThemeColor(matchDisabledSelection), Prog__ThemeColor(codeKeyword), Prog__ThemeColor(codeString), Prog__ThemeColor(codeNumber), Prog__ThemeColor(codeHeading),
    Prog__ThemeColor(hoverAmount), Prog__ThemeColor(hiddenAmount), Prog__ThemeColor(disabledAmount),
#undef Prog__ThemeColor
};
static_assert(sizeof(Prog_ThemeColors) / sizeof(Prog_ThemeColors[0]) * sizeof(R_Color) == sizeof(Prog_Theme));

// The transparent theme's tints as the settings give them, each at its opacity: the window's over what is behind it, and the floating
// boxes' over the blur under them. Their colors are the scheme's unless the settings give their own.
#define Prog_DefaultGlassOpacity 0.55f
#define Prog_DefaultPopupOpacity 0.75f
global R_Color Prog_GlassWindow = { 0, 0, 0, Prog_DefaultGlassOpacity }, Prog_GlassPopup = { 0, 0, 0, Prog_DefaultPopupOpacity };
global b32 Prog_GlassWindowOwn, Prog_GlassPopupOwn;

// The transparent theme, and how far its floating boxes blur what is under them, in logical pixels.
global Prog_Theme Prog_ThemeGlassUser;
global f32 Prog_GlassBlur = 24;

// The scheme over a see-through window: the window is one translucent layer of its color and the surfaces on it faint tints of its text,
// so that they do not stack into an opaque color, on a dark scheme or a light one. The floating boxes blur what is under them, so their
// tint lets it show through softened.
function Prog_Theme Prog__GlassTheme(const Prog_Theme *scheme)
{
    Prog_Theme result = *scheme;
    R_Color ink = scheme->text;
#define Prog__Tint(alpha) ((R_Color){ ink.r, ink.g, ink.b, (alpha) })
    result.window = Prog_GlassWindowOwn ? Prog_GlassWindow : (R_Color){ scheme->window.r, scheme->window.g, scheme->window.b, Prog_GlassWindow.a };
    result.menu = Prog_GlassPopupOwn ? Prog_GlassPopup : (R_Color){ scheme->menu.r, scheme->menu.g, scheme->menu.b, Prog_GlassPopup.a };
    // The strip is clear like the panels, as the shown tab cannot hide a tint under it the way an opaque tab does, and would show lighter
    // than its panel.
    result.background = result.caption = (R_Color){0};
    result.inner = Prog__Tint(0.02f);
    result.panel = Prog__Tint(0.025f);
    result.alternateRow = Prog__Tint(0.012f);
    result.separator = Prog__Tint(0.03f);
    result.tabBorder = Prog__Tint(0.04f);
    result.hover = Prog__Tint(0.2f);
    result.outline = Prog__Tint(0.08f);
    result.outlineHover = Prog__Tint(0.3f);
    result.outlineSelection = Prog__Tint(0.5f);
    result.outlineDisabledSelection = Prog__Tint(0.15f);
    // The selection keeps the scheme's color, a little see-through.
    result.selection.a = result.focus.a = 0.8f;
#undef Prog__Tint
    return result;
}

function void Prog__ApplyTheme(Prog_State *state)
{
    Prog_ThemeGlassUser = Prog__GlassTheme(&Prog_ThemeUser);
    Prog_Theme_ = state->transparent ? &Prog_ThemeGlassUser : &Prog_ThemeUser;
    state->ui.defaultColors = (UI_Colors){ .background = Prog_Theme_->background, .border = Prog_Theme_->separator, .text = Prog_Theme_->text };
}

// The hex digits of `text`, "#rrggbb" or "#rrggbbaa".
function b32 Prog__ParseColor(String text, R_Color *color)
{
    if (!text.length || text.value[0] != '#' || (text.length != 7 && text.length != 9)) return false;
    u32 channels[4] = { 0, 0, 0, 255 };
    for (u64 i = 1; i < text.length; ++i) {
        u8 c = Prog__FoldAscii((u8)text.value[i]);
        u32 digit = c >= '0' && c <= '9' ? (u32)(c - '0') : c >= 'a' && c <= 'f' ? (u32)(c - 'a' + 10) : 16;
        if (digit > 15) return false;
        channels[(i - 1) / 2] = (i - 1) % 2 ? channels[(i - 1) / 2] + digit : digit * 16;
    }
    *color = (R_Color){ (f32)channels[0] / 255.0f, (f32)channels[1] / 255.0f, (f32)channels[2] / 255.0f, (f32)channels[3] / 255.0f };
    return true;
}

// The built-in schemes, before the custom ones, by their names in the settings and as the choices show them.
// The first is the default, and Prog_LightScheme the light one that "light" names.
global const String Prog_OptionSchemes[] = { SInit(Prog_DefaultScheme), SInit("catppuccin-frappe"), SInit("catppuccin-latte"), SInit("nord"), SInit("gruvbox"), SInit("tokyo-night"),
                                             SInit("one-dark") };
global const char *const Prog_OptionSchemeLabels[] = { NoTrF("Filey Dark"), NoTrF("Catppuccin Frappé"), NoTrF("Catppuccin Latte"), NoTrF("Nord"), NoTrF("Gruvbox"),
                                                        NoTrF("Tokyo Night"), NoTrF("One Dark") };
static_assert(Prog_LightScheme < Prog_BuiltInSchemes, "the light scheme built in");
static_assert(CArrayCount(Prog_OptionSchemeLabels) == Prog_BuiltInSchemes, "the built-in schemes labeled");
static_assert(CArrayCount(Prog_OptionSchemes) == Prog_BuiltInSchemes, "the built-in schemes counted");
static_assert(Prog_Option_ColorCodeHeading - Prog_Option_ColorClear + 1 == Prog_ColorGroupCount, "the color groups counted");

// The groups' names in a custom scheme's lines.
global const char *const Prog_ColorGroupNames[Prog_ColorGroupCount] = {
    "Clear", "Caption", "Background", "Surface", "Foreground", "Inner", "Border", "Outline", "Separator", "AlternatingRow", "IconTint", "Text", "Secondary", "Group",
    "File", "Folder", "Warning", "Progress", "Selection", "RectSelection", "Match", "Hidden", "Hover", "Disabled", "ContentHover", "ContentSelection",
    "ContentDisabledSelection", "OutlineHover", "OutlineSelection", "OutlineDisabledSelection", "MatchHover", "MatchSelection", "MatchDisabledSelection",
    "CodeKeyword", "CodeString", "CodeNumber", "CodeHeading",
};

// Gives `groups` the colors of the built-in scheme named `name`, or the default scheme's for an unknown one; "light" is the light one. Returns its place among the built-in schemes and UINT32_MAX for an unknown name.
function u32 Prog__BuiltInScheme(String name, u32 *groups)
{
    MemCopy(groups, Prog_SchemeColors[0], sizeof(Prog_SchemeColors[0]));
    if (Prog__EqualFolded(name, S("light"))) name = Prog_OptionSchemes[Prog_LightScheme];
    for (u32 i = 0; i < CArrayCount(Prog_OptionSchemes); ++i) {
        if (!Prog__EqualFolded(name, Prog_OptionSchemes[i])) continue;
        MemCopy(groups, Prog_SchemeColors[i], sizeof(Prog_SchemeColors[i]));
        return i;
    }
    return UINT32_MAX;
}

// Gives `groups` the colors a custom scheme's lines `text` set, "Group = #rrggbb" each, leaving the others as they are.
function void Prog__ReadSchemeText(String text, u32 *groups)
{
    for (u64 at = 0; at < text.length;) {
        u64 end = at + StrIndexChar(StrFrom(text, at), '\n', SSF_CountOnFail).value;
        String line = StrFromTo(text, at, (i64)end);
        at = end + 1;
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        Index equals = StrIndexChar(line, '=', SSF_None);
        if (!equals.exists) continue;
        String field = StrFromTo(line, 0, (i64)equals.value), value = StrFrom(line, equals.value + 1);
        while (field.length && field.value[field.length - 1] == ' ') --field.length;
        while (value.length && value.value[0] == ' ') value = StrFrom(value, 1);
        R_Color color;
        for (u32 g = 0; g < Prog_ColorGroupCount; ++g) {
            if (!StrIsEqual(field, StrFromCStr(Prog_ColorGroupNames[g])) || !Prog__ParseColor(value, &color)) continue;
            groups[g] = (u32)(color.r * 255 + 0.5f) << 16 | (u32)(color.g * 255 + 0.5f) << 8 | (u32)(color.b * 255 + 0.5f);
        }
    }
}

// Gives `groups` the colors of the scheme named `name`: a built-in one, or a custom one's over the default scheme's.
// Returns its place among the built-in schemes and then the custom ones, 0 for an unknown name.
function u8 Prog__UseScheme(const Prog_State *state, String name, u32 *groups)
{
    u32 builtIn = Prog__BuiltInScheme(name, groups);
    if (builtIn != UINT32_MAX) return (u8)builtIn;
    for (u64 i = 0; i < state->schemeNames.header.count; ++i) {
        if (!Prog__EqualFolded(state->schemeNames.v[i], name)) continue;
        // A light scheme from before the code colors takes the light built-in scheme's, as the default dark one's are too pale on it.
        String text = state->schemeTexts.v[i];
        Prog__ReadSchemeText(text, groups);
        R_Color under = Prog__HexColor(groups[Prog_Option_ColorBackground - Prog_Option_ColorClear]);
        if (!StrIndexStr(text, S("Code"), SSF_None).exists && under.r + under.g + under.b > 1.5f) {
            u32 first = Prog_Option_ColorCodeKeyword - Prog_Option_ColorClear;
            MemCopy(groups + first, Prog_SchemeColors[Prog_LightScheme] + first, (Prog_ColorGroupCount - first) * sizeof(u32));
        }
        return (u8)(CArrayCount(Prog_OptionSchemes) + i);
    }
    return 0;
}

// The colors `groups` as a custom scheme's lines.
function String Prog_SchemeText(Arena *arena, const u32 *groups)
{
    StrBuilder text = {0};
    for (u32 g = 0; g < Prog_ColorGroupCount; ++g) StrBuildStr(arena, &text, StrFormat(arena, "%s = #%06X\n", Prog_ColorGroupNames[g], groups[g]));
    return text.buffer;
}

// What Windows draws behind a see-through window, by its names in the settings, in PlatformBackdrop's order.
global const String Prog_OptionBackdrops[PlatformBackdrop_Count] = { SInit("acrylic"), SInit("mica"), SInit("tabbed"), SInit("none") };

// A switch setting's words: on, true or 1, and off, false or 0.
function b32 Prog__SettingOn(String value) { return Prog__EqualFolded(value, S("on")) || Prog__EqualFolded(value, S("true")) || StrIsEqual(value, S("1")); }
function b32 Prog__SettingOff(String value) { return Prog__EqualFolded(value, S("off")) || Prog__EqualFolded(value, S("false")) || StrIsEqual(value, S("0")); }

// The window's look as the settings in `configDirectory` choose it, read before anything else is, so that the window opens so rather than
// turning so with the first frame: see-through over its backdrop, or in its color.
// That color is the scheme's, the system's light one where the settings follow the system, a color.window of their own, or glass.window's
// while the window is see-through.
function void Prog_StartupWindow(String configDirectory, PlatformWindowDesc *desc)
{
    Scratch scratch = GetScratch();
    String text = {0}, theme = {0}, light = {0};
    b32 system = true, transparent = false, own = false, glassOwn = false;
    u8 backdrop = PlatformBackdrop_Acrylic;
    R_Color glass = {0}, window = {0}, color;
    if (configDirectory.length) P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, configDirectory, S("Settings.conf")), &text);
    ArrayStr lines = StrSplit(scratch.arena, text, '\n');
    for (u64 l = 0; l < lines.header.count; ++l) {
        String line = lines.v[l];
        while (line.length && (line.value[line.length - 1] == '\r' || line.value[line.length - 1] == ' ')) --line.length;
        while (line.length && line.value[0] == ' ') line = StrFrom(line, 1);
        Index equals = StrIndexChar(line, '=', SSF_None);
        if (!line.length || line.value[0] == '#' || !equals.exists) continue;
        String name = StrFromTo(line, 0, (i64)equals.value), value = StrFrom(line, equals.value + 1);
        while (name.length && name.value[name.length - 1] == ' ') --name.length;
        while (value.length && value.value[0] == ' ') value = StrFrom(value, 1);
        // A theme line replaces the colors set before it.
        if (StrIsEqual(name, S("theme"))) theme = value, own = false, system = false;
        else if (StrIsEqual(name, S("theme.light"))) light = value;
        else if (StrIsEqual(name, S("transparent"))) transparent = Prog__SettingOn(value);
        else if (StrIsEqual(name, S("glass.window")) && Prog__ParseColor(value, &color)) glass = color, glassOwn = true;
        else if (StrIsEqual(name, S("glass.backdrop"))) {
            for (u32 i = 0; i < PlatformBackdrop_Count; ++i) if (Prog__EqualFolded(value, Prog_OptionBackdrops[i])) backdrop = (u8)i;
        }
        else if (StrIsEqual(name, S("color.window")) && Prog__ParseColor(value, &color)) window = color, own = true;
    }
    b32 lightMode = system && !P_PrefersDarkColors();
    String scheme = lightMode ? (light.length ? light : S("light")) : theme;
    u32 groups[Prog_ColorGroupCount];
    if (Prog__BuiltInScheme(scheme, groups) == UINT32_MAX && scheme.length) {
        // A custom one's file, by its name in either case.
        P_DirectoryResult listing = P_EnumerateDirectory(scratch.arena, configDirectory, NULL);
        for (u64 i = 0; i < listing.files.header.count; ++i) {
            String file = listing.files.v[i].name, colors = {0};
            if (file.length != scheme.length + Prog_SchemeExtension.length || !Prog__EqualFolded(file, StrFormat(scratch.arena, "%S%S", scheme, Prog_SchemeExtension))) continue;
            if (P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, configDirectory, file), &colors)) Prog__ReadSchemeText(colors, groups);
            break;
        }
    }
    u32 result = groups[Prog_Option_ColorClear - Prog_Option_ColorClear];
    b32 tinted = transparent && glassOwn;
    R_Color chosen = tinted ? glass : own && !lightMode ? window : (R_Color){0};
    if (tinted || (own && !lightMode)) result = (u32)(chosen.r * 255 + 0.5f) << 16 | (u32)(chosen.g * 255 + 0.5f) << 8 | (u32)(chosen.b * 255 + 0.5f);
    desc->background = result;
    desc->transparent = transparent;
    desc->backdrop = backdrop;
    ReleaseScratch(scratch);
}

// A setting's number, such as "1.25"; -1 when it is not one.
function f32 Prog__ParseNumber(String value)
{
    f32 number = 0, fraction = 0;
    for (u64 i = 0; i < value.length; ++i) {
        char c = value.value[i];
        if (c == '.' && fraction == 0) fraction = 0.1f;
        else if (c >= '0' && c <= '9' && fraction == 0) number = number * 10 + (f32)(c - '0');
        else if (c >= '0' && c <= '9') { number += (f32)(c - '0') * fraction; fraction *= 0.1f; }
        else return -1;
    }
    return value.length ? number : -1;
}

// The words Settings.conf has for the choices of the start, new tabs and recents.
global const String Prog_OptionStoreRecents[Prog_StoreRecents_Count] = { SInit("persistent"), SInit("session"), SInit("never") };
// The notices shown, by Prog_NoticeKind: all of them, warnings and errors, errors, or none.
global const String Prog_OptionVideoCodecs[] = { SInit("hevc"), SInit("h264"), SInit("av1") };
global const String Prog_OptionNotices[Prog_NoticeKind_Count + 1] = { SInit("all"), SInit("warnings"), SInit("errors"), SInit("off") };
global const String Prog_OptionStartups[Prog_Startup_Count] = { SInit("session"), SInit("root"), SInit("home") };
global const String Prog_OptionNewTabs[Prog_NewTab_Count] = { SInit("root"), SInit("home"), SInit("current") };
global const String Prog_OptionCharacterKeys[Prog_CharacterKeys_Count] = { SInit("filter"), SInit("hotkey"), SInit("select") };

// Applies settings `text`, lines of "name = value", over the defaults; unknown names and bad values are skipped, and a line starting with '#' is a comment.
//   scale = 1.25                  the UI's size beyond the display's own scale
//   theme = nord                  a built-in scheme, under the colors below
//   animations = false            menus, scrolling and the caret change at once
//   color.<theme field> = #rrggbb[aa]
//   glass.window = #rrggbbaa      the transparent theme's tint over what is behind the window, the scheme's window color without it
//   glass.popup = #rrggbbaa       its tint over the blur under menus, the palette and the other floating boxes, the scheme's surface color without it
//   glass.opacity = 55            how much of glass.window's tint covers what is behind, in percent, where there is no backdrop to choose
//   glass.popupOpacity = 75       how much of glass.popup's tint covers that blur, in percent, over its own opacity
//   glass.blur = 24               how far that blur reaches, in logical pixels; 0 leaves them unblurred
//   glass.backdrop = acrylic      what Windows draws behind the window: acrylic, mica, tabbed or none
//   transparent = true            the transparent theme
//   inspector.fontSize = 10       the inspector's text size in points, and inspector.layout = auto, right or below where it sits
//   hotkey.<Command> = Ctrl+K, F6  replaces the command's default hotkeys; "none" leaves it without
// Whether the settings in `configDirectory` give each run a window of its own, read before the window opens.
function b32 Prog_SeparateInstances(String configDirectory)
{
    if (!configDirectory.length) return false;
    Scratch scratch = GetScratch();
    String text = {0};
    b32 result = false;
    if (P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, configDirectory, S("Settings.conf")), &text)) {
        ArrayStr lines = StrSplit(scratch.arena, text, '\n');
        for (u64 l = 0; l < lines.header.count; ++l) {
            String line = lines.v[l];
            while (line.length && (line.value[line.length - 1] == '\r' || line.value[line.length - 1] == ' ')) --line.length;
            while (line.length && line.value[0] == ' ') line = StrFrom(line, 1);
            Index equals = StrIndexChar(line, '=', SSF_None);
            if (!equals.exists || line.value[0] == '#') continue;
            String name = StrFromTo(line, 0, (i64)equals.value), value = StrFrom(line, equals.value + 1);
            while (name.length && name.value[name.length - 1] == ' ') --name.length;
            while (value.length && value.value[0] == ' ') value = StrFrom(value, 1);
            if (StrIsEqual(name, S("instances.single"))) result = Prog__SettingOff(value);
        }
    }
    ReleaseScratch(scratch);
    return result;
}

function void Prog_ApplySettings(Prog_State *state, String text)
{
    Prog_ThemeUser = Prog_ThemeCustom;
    b8 colorGiven[CArrayCount(Prog_ThemeColors)] = {0};
    MemCopy(state->schemeGroups, Prog_SchemeColors[0], sizeof(state->schemeGroups));
    state->scheme = 0;
    state->lightScheme = Prog_LightScheme;
    state->schemeName.length = state->lightSchemeName.length = 0;
    Prog_GlassWindow = (R_Color){ 0, 0, 0, Prog_DefaultGlassOpacity };
    Prog_GlassPopup = (R_Color){ 0, 0, 0, Prog_DefaultPopupOpacity };
    Prog_GlassWindowOwn = Prog_GlassPopupOwn = false;
    Prog_GlassBlur = 24;
    state->glassBackdrop = PlatformBackdrop_Acrylic;
    state->uiScale = 1;
    state->animationsOff = false;
    state->transparent = false;
    state->scheme = 0;
    state->systemScheme = true;
    state->spacingShift = 0;
    state->borderSize = 0;
    state->squareCorners = false;
    state->alternatingRows = false;
    state->inactiveShift = 0;
    state->actOnRelease = state->singleClick = state->singleClickFoldersOnly = state->hoverToSelect = false;
    state->clickToFocus = true;
    state->underlineAlways = state->doubleClickToParent = state->noClickRename = state->noOverscroll = state->reverseScroll = false;
    state->characterKeys = Prog_CharacterKeys_TypeInFilter;
    state->hideExtensions = state->showSystem = false;
    state->fullPaths = state->noTrail = false;
    state->thumbnailMode = Prog_Thumbnails_LocalDisk;
    state->showCheckboxes = state->noTrashConfirm = state->menuInline = state->separateInstances = state->gitFetch = false;
    state->startup = Prog_Startup_LastSession;
    state->newTab = Prog_NewTab_Root;
    state->storeRecents = Prog_StoreRecents_Persistent;
    state->archiveLevel = 9;
    state->videoCodec = 0;
    state->videoQuality = 20;
    state->downloadAudio = state->downloadPlaylist = state->deleteFinishedTorrents = false;
    state->audioVolume = 100;
    state->noticeLevel = Prog_NoticeKind_Info;
    state->noticesInSidebar = true;
    state->checkUpdates = true;
    state->showRecents = state->sortRecents = state->noVSync = state->noHighlight = false;
    state->font.length = state->inspectorFont.length = 0;
    String system = Prog_SystemLanguage();
    state->language.length = (u32)system.length;
    MemCopy(state->language.text, system.value, system.length);
    state->noFolderSizes = state->noFolderSizesSolidState = state->folderSizesSpinning = state->folderSizesNetwork = false;
    b32 showHidden = false;
    u32 inspectorFontSize = Prog_InspectorFontSize;
    u8 inspectorLayout = Prog_InspectorLayout_Auto;
    state->userHotkeyCount = 0;
    MemZero(state->hotkeyOverridden, sizeof(state->hotkeyOverridden));
    Scratch scratch = GetScratch();
    ArrayStr lines = StrSplit(scratch.arena, text, '\n');
    for (u64 l = 0; l < lines.header.count; ++l) {
        String line = lines.v[l];
        while (line.length && (line.value[line.length - 1] == '\r' || line.value[line.length - 1] == ' ')) --line.length;
        while (line.length && line.value[0] == ' ') line = StrFrom(line, 1);
        Index equals = StrIndexChar(line, '=', SSF_None);
        if (!line.length || line.value[0] == '#' || !equals.exists) continue;
        String name = StrFromTo(line, 0, (i64)equals.value), value = StrFrom(line, equals.value + 1);
        while (name.length && name.value[name.length - 1] == ' ') --name.length;
        while (value.length && value.value[0] == ' ') value = StrFrom(value, 1);
        R_Color color;
        f32 number = Prog__ParseNumber(value);
        if (StrIsEqual(name, S("scale"))) {
            if (number >= 0.5f && number <= 3) state->uiScale = number;
        } else if (StrIsEqual(name, S("glass.window")) && Prog__ParseColor(value, &color)) {
            Prog_GlassWindow = color;
            Prog_GlassWindowOwn = true;
        } else if (StrIsEqual(name, S("glass.popup")) && Prog__ParseColor(value, &color)) {
            Prog_GlassPopup = color;
            Prog_GlassPopupOwn = true;
        } else if (StrIsEqual(name, S("glass.opacity"))) {
            if (number >= 0 && number <= 100) Prog_GlassWindow.a = number / 100.0f;
        } else if (StrIsEqual(name, S("glass.popupOpacity"))) {
            if (number >= 0 && number <= 100) Prog_GlassPopup.a = number / 100.0f;
        } else if (StrIsEqual(name, S("glass.blur"))) {
            if (number >= 0 && number <= 100) Prog_GlassBlur = number;
        } else if (StrIsEqual(name, S("glass.backdrop"))) {
            for (u32 i = 0; i < PlatformBackdrop_Count; ++i) if (Prog__EqualFolded(value, Prog_OptionBackdrops[i])) state->glassBackdrop = (u8)i;
        } else if (StrIsEqual(name, S("theme"))) {
            // Colors given before the theme line are replaced with the rest.
            state->scheme = Prog__UseScheme(state, value, state->schemeGroups);
            Prog_ThemeUser = Prog__ThemeFromScheme(state->schemeGroups);
            state->schemeName.length = (u32)Min(value.length, (u64)sizeof(state->schemeName.text));
            MemCopy(state->schemeName.text, value.value, state->schemeName.length);
            // One chosen is kept, whatever the system's mode.
            state->systemScheme = false;
        } else if (StrIsEqual(name, S("theme.light"))) {
            u32 unused[Prog_ColorGroupCount];
            state->lightScheme = Prog__UseScheme(state, value, unused);
            state->lightSchemeName.length = (u32)Min(value.length, (u64)sizeof(state->lightSchemeName.text));
            MemCopy(state->lightSchemeName.text, value.value, state->lightSchemeName.length);
        } else if (StrIsEqual(name, S("borders"))) {
            // In half pixels from 1 to 3; anything else is 1.
            f32 halves = floorf(number * 2 + 0.5f);
            state->borderSize = halves >= 2 && halves <= 6 ? (u8)(halves - 2) : 0;
        } else if (StrIsEqual(name, S("spacing"))) {
            if (number >= 0 && number <= 13) state->spacingShift = (i32)number - 4;
        } else if (StrIsEqual(name, S("corners"))) {
            state->squareCorners = Prog__EqualFolded(value, S("square"));
        } else if (StrIsEqual(name, S("rows.alternate"))) {
            state->alternatingRows = Prog__EqualFolded(value, S("on")) || Prog__EqualFolded(value, S("true")) || StrIsEqual(value, S("1"));
        } else if (StrIsEqual(name, S("files.extensions"))) {
            state->hideExtensions = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("files.hidden"))) {
            showHidden = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("paths.short"))) {
            state->fullPaths = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("paths.trail"))) {
            state->noTrail = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("menus.inline"))) {
            state->menuInline = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("startup"))) {
            // Any other value names a saved layout.
            state->startupLayoutLength = 0;
            b32 known = false;
            for (u32 i = 0; i < Prog_Startup_Count; ++i) if (Prog__EqualFolded(value, Prog_OptionStartups[i])) state->startup = (u8)i, known = true;
            if (!known && value.length <= sizeof(state->startupLayout)) {
                MemCopy(state->startupLayout, value.value, value.length);
                state->startupLayoutLength = (u32)value.length;
            }
        } else if (StrIsEqual(name, S("newtab"))) {
            for (u32 i = 0; i < Prog_NewTab_Count; ++i) if (Prog__EqualFolded(value, Prog_OptionNewTabs[i])) state->newTab = (u8)i;
        } else if (StrIsEqual(name, S("language"))) {
            state->language.length = (u32)Min(value.length, (u64)sizeof(state->language.text));
            MemCopy(state->language.text, value.value, state->language.length);
        } else if (StrIsEqual(name, S("archive.level"))) {
            if (number >= 0 && number <= 9) state->archiveLevel = (u8)number;
        } else if (StrIsEqual(name, S("video.codec"))) {
            for (u32 i = 0; i < CArrayCount(Prog_OptionVideoCodecs); ++i) if (Prog__EqualFolded(value, Prog_OptionVideoCodecs[i])) state->videoCodec = (u8)i;
        } else if (StrIsEqual(name, S("download.audio"))) {
            state->downloadAudio = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("download.playlist"))) {
            state->downloadPlaylist = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("torrents.delete"))) {
            state->deleteFinishedTorrents = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("audio.volume"))) {
            if (number >= 0 && number <= 100) state->audioVolume = (u8)number;
        } else if (StrIsEqual(name, S("video.quality"))) {
            if (number >= 0 && number <= 51) state->videoQuality = (u8)number;
        } else if (StrIsEqual(name, S("notices"))) {
            for (u32 i = 0; i <= Prog_NoticeKind_Count; ++i) if (Prog__EqualFolded(value, Prog_OptionNotices[i])) state->noticeLevel = (u8)i;
        } else if (StrIsEqual(name, S("notices.sidebar"))) {
            state->noticesInSidebar = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("updates.check"))) {
            state->checkUpdates = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("recents.store"))) {
            for (u32 i = 0; i < Prog_StoreRecents_Count; ++i) if (Prog__EqualFolded(value, Prog_OptionStoreRecents[i])) state->storeRecents = (u8)i;
        } else if (StrIsEqual(name, S("recents.highlight"))) {
            state->showRecents = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("recents.sort"))) {
            state->sortRecents = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("font")) || StrIsEqual(name, S("inspector.font"))) {
            Prog_FontName *font = StrIsEqual(name, S("font")) ? &state->font : &state->inspectorFont;
            font->length = (u32)Min(value.length, (u64)sizeof(font->text));
            MemCopy(font->text, value.value, font->length);
        } else if (StrIsEqual(name, S("vsync"))) {
            state->noVSync = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("inspector.highlight"))) {
            state->noHighlight = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("checkboxes"))) {
            state->showCheckboxes = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("git.fetch"))) {
            state->gitFetch = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("trash.confirm"))) {
            state->noTrashConfirm = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("instances.single"))) {
            state->separateInstances = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("thumbnails"))) {
            if (Prog__EqualFolded(value, S("never"))) state->thumbnailMode = Prog_Thumbnails_Never;
            if (Prog__EqualFolded(value, S("always"))) state->thumbnailMode = Prog_Thumbnails_Always;
        } else if (StrIsEqual(name, S("folders.size"))) {
            state->noFolderSizes = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("folders.size.ssd"))) {
            state->noFolderSizesSolidState = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("folders.size.hdd"))) {
            state->folderSizesSpinning = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("folders.size.network"))) {
            state->folderSizesNetwork = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("files.system"))) {
            state->showSystem = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("click.onPress"))) {
            state->actOnRelease = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("click.single"))) {
            state->singleClick = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("click.singleFoldersOnly"))) {
            state->singleClickFoldersOnly = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("hover.select"))) {
            state->hoverToSelect = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("hover.focus"))) {
            state->clickToFocus = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("hover.underline"))) {
            state->underlineAlways = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("click.doubleToParent"))) {
            state->doubleClickToParent = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("click.rename"))) {
            state->noClickRename = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("scroll.over"))) {
            state->noOverscroll = Prog__SettingOff(value);
        } else if (StrIsEqual(name, S("scroll.reverse"))) {
            state->reverseScroll = Prog__SettingOn(value);
        } else if (StrIsEqual(name, S("keys.character"))) {
            for (u32 i = 0; i < Prog_CharacterKeys_Count; ++i) if (Prog__EqualFolded(value, Prog_OptionCharacterKeys[i])) state->characterKeys = (u8)i;
        } else if (StrIsEqual(name, S("inactive.opacity"))) {
            if (number >= 0 && number <= 100) state->inactiveShift = (i32)number - 60;
        } else if (StrIsEqual(name, S("transparent"))) {
            state->transparent = Prog__EqualFolded(value, S("on")) || Prog__EqualFolded(value, S("true")) || StrIsEqual(value, S("1"));
        } else if (StrIsEqual(name, S("inspector.fontSize"))) {
            if (number >= Prog_InspectorFontMin && number <= Prog_InspectorFontMax) inspectorFontSize = (u32)number;
        } else if (StrIsEqual(name, S("inspector.layout"))) {
            local_persist const String layouts[Prog_InspectorLayout_Count] = { SInit("auto"), SInit("right"), SInit("below") };
            for (u32 i = 0; i < Prog_InspectorLayout_Count; ++i) if (Prog__EqualFolded(value, layouts[i])) inspectorLayout = (u8)i;
        } else if (StrIsEqual(name, S("animations"))) {
            state->animationsOff = Prog__EqualFolded(value, S("off")) || Prog__EqualFolded(value, S("false")) || StrIsEqual(value, S("0"));
        } else if (name.length > 6 && StrIsEqual(StrFromTo(name, 0, 6), S("color."))) {
            String field = StrFrom(name, 6);
            for (u64 i = 0; i < CArrayCount(Prog_ThemeColors); ++i) {
                if (StrIsEqual(field, StrFromCStr(Prog_ThemeColors[i].name)) && Prog__ParseColor(value, &color)) {
                    *(R_Color *)((u8 *)&Prog_ThemeUser + Prog_ThemeColors[i].offset) = color;
                    colorGiven[i] = true;
                }
            }
        } else if (name.length > 7 && StrIsEqual(StrFromTo(name, 0, 7), S("hotkey."))) {
            String command = StrFrom(name, 7);
            for (u32 k = Prog_CommandKind_None + 1; k < Prog_CommandKind_Count; ++k) {
                if (!StrIsEqual(command, Prog_Commands[k].name)) continue;
                state->hotkeyOverridden[k] = true;
                if (Prog__EqualFolded(value, S("none"))) break;
                for (u64 at = 0; at < value.length;) {
                    Index comma = StrIndexChar(StrFrom(value, at), ',', SSF_CountOnFail);
                    Prog_Hotkey hotkey = { .command = (u16)k };
                    if (Prog_ParseHotkey(StrFromTo(value, at, (i64)(at + comma.value)), &hotkey) && state->userHotkeyCount < CArrayCount(state->userHotkeys)) {
                        state->userHotkeys[state->userHotkeyCount++] = hotkey;
                    }
                    at += comma.value + 1;
                }
                break;
            }
        }
    }
    ReleaseScratch(scratch);
    // Until a scheme is chosen, the system's light mode brings the light one, and its dark mode the default dark one.
    state->systemDark = !state->systemScheme || P_PrefersDarkColors();
    if (!state->systemDark) {
        Prog__UseScheme(state, state->lightSchemeName.length ? StrMake(state->lightSchemeName.text, state->lightSchemeName.length) : S("light"), state->schemeGroups);
        // The settings' own colors stay over the light scheme's.
        Prog_Theme given = Prog_ThemeUser;
        Prog_ThemeUser = Prog__ThemeFromScheme(state->schemeGroups);
        for (u64 i = 0; i < CArrayCount(Prog_ThemeColors); ++i) {
            if (colorGiven[i]) *(R_Color *)((u8 *)&Prog_ThemeUser + Prog_ThemeColors[i].offset) = *(R_Color *)((u8 *)&given + Prog_ThemeColors[i].offset);
        }
    }
    // The inspector's text size and layout change with the file, and keep what its controls set otherwise.
    if (inspectorFontSize != state->settingInspectorFontSize) state->inspectorFontSize = state->settingInspectorFontSize = inspectorFontSize;
    // Hidden items too, which the shown tab takes when the file changes them, listing its folder again; that includes a listing still on
    // its way, as the first one is when the settings load.
    if ((showHidden ? 2u : 1u) != state->settingHidden) {
        b32 changed = state->showHidden != showHidden;
        state->settingHidden = showHidden ? 2 : 1;
        state->showHidden = showHidden;
        if (changed) Prog_Reload(state);
    }
    if (inspectorLayout + 1u != state->settingInspectorLayout) {
        state->inspectorLayout = inspectorLayout;
        state->settingInspectorLayout = (u8)(inspectorLayout + 1);
    }
    Prog__ApplyTheme(state);
    Prog_UseLanguage(state, StrMake(state->language.text, state->language.length));
}

// Every setting, commented out at its default, for a first edit.
function String Prog_SettingsTemplate(Arena *arena, const Prog_State *state)
{
    StrBuilder text = {0};
    StrBuildStr(arena, &text, S("# Filey settings, applied as soon as this file is saved.\n"
                                "# Lines are \"name = value\", switches true or false; remove the # in front of a setting to change it.\n\n"
                                "# The size of everything, beyond the display's own scale.\n# scale = 1\n\n"
                                "# A scheme by its name: filey-dark, catppuccin-frappe, catppuccin-latte, nord, gruvbox, tokyo-night, one-dark\n"
                                "# or a custom one; colors set before this line are replaced.\n# theme = filey-dark\n"
                                "# Without a theme line, the system's light mode brings theme.light, and its dark mode filey-dark.\n# theme.light = catppuccin-latte\n\n"
                                "# How the lists look: the space between items from 0 to 13, the corners, every other row tinted,\n"
                                "# and how much of inactive panels shows, from 0 to 100.\n# spacing = 4\n# corners = round\n# rows.alternate = false\n# inactive.opacity = 60\n\n"
                                "# The thickness of the outlines, in pixels: 1, 1.5, 2, 2.5 or 3; the lines between areas take the nearest whole pixels.\n# borders = 1\n\n"
                                "# The pointer: select on press or release, open with one click (folders only), select by hovering,\n"
                                "# give the keyboard to the panel it moves over, underline only the pointed name in single-click mode,\n"
                                "# go up with a double click on the background, rename the selected item with a slow second click on its name, scroll past the last item, and turn the wheel around.\n"
                                "# click.onPress = true\n# click.single = false\n# click.singleFoldersOnly = false\n# hover.select = false\n# hover.focus = false\n"
                                "# hover.underline = true\n"
                                "# click.doubleToParent = false\n# click.rename = true\n# scroll.over = true\n# scroll.reverse = false\n\n"
                                "# What a letter or other character key does in the lists, without modifiers but Shift: type into the filter (filter),\n"
                                "# run its hotkey, such as J to go down (hotkey), or select the item whose name starts with what is typed (select).\n"
                                "# keys.character = filter\n\n"
                                "# What the lists show: file extensions, hidden and dot files, and the system's own files.\n"
                                "# files.extensions = true\n# files.hidden = false\n# files.system = false\n\n"
                                "# The address bar: from the first recognized folder rather than the drive, and with the visited subfolders past the shown one.\n"
                                "# paths.short = true\n# paths.trail = true\n\n"
                                "# Boxes that check items in place of their icons while several are selected.\n# checkboxes = false\n\n"
                                "# Submenus that open inside their menu, under their item, rather than beside it.\n# menus.inline = false\n\n"
                                "# Whether emptying the Recycle Bin (the Trash) asks first.\n# trash.confirm = true\n\n"
                                "# How hard the archives Filey writes are compressed, from 0 (stored) to 9 (smallest, slowest).\n# archive.level = 9\n\n"));
    StrBuildStr(arena, &text, S("# What a start without folders to open shows: the last session, root (This PC) or home; what a new tab shows: root, home or current.\n"
                                "# startup = session\n# newtab = root\n\n"
                                "# Whether starting Filey, or opening a folder in it from elsewhere, brings the open window forward, the folder in a new tab, rather\n"
                                "# than opening another window.\n# instances.single = true\n\n"
                                "# The notices in the window's corner: all, warnings (and errors), errors or off.\n# notices = all\n"
                                "# Whether they and the progress of copies and downloads stack at the bottom of the sidebar while it shows, not in a panel's corner.\n"
                                "# notices.sidebar = true\n\n"
                                "# Whether GitHub is asked once a day for a newer Filey, which the main menu and a notice then offer to update to.\n# updates.check = true\n\n"
                                "# How long recently visited folders are kept: persistent, session or never; whether lists mark them and sort them first.\n"
                                "# recents.store = persistent\n# recents.highlight = false\n# recents.sort = false\n\n"
                                "# Frames wait for the display's refresh, which saves power at the cost of slight input lag.\n# vsync = true\n\n"
                                "# Thumbnails in the icon views: never, local (not on network drives) or always.\n# thumbnails = local\n\n"
                                "# Subfolder sizes, measured by walking them, and on which drives: solid-state, spinning and network.\n"
                                "# folders.size = true\n# folders.size.ssd = true\n# folders.size.hdd = false\n# folders.size.network = false\n\n"
                                "# Easing for menus and scrolling, and the fading caret.\n# animations = true\n\n"
                                "# The inspector's text size in points, 8 to 16, where it sits (auto, right or below), and whether it colors code by its language.\n"
                                "# inspector.fontSize = 10\n# inspector.layout = auto\n# inspector.highlight = true\n\n"
                                "# The font families of the UI and of the inspector's text, by name; a family the system lacks falls back to its default.\n"
                                "# font = Segoe UI\n# inspector.font = Consolas\n\n"
                                "# Colors of the default theme, as #rrggbb or #rrggbbaa.\n"));
    for (u64 i = 0; i < CArrayCount(Prog_ThemeColors); ++i) {
        R_Color c = *(const R_Color *)((const u8 *)&Prog_ThemeCustom + Prog_ThemeColors[i].offset);
        u32 r = (u32)(c.r * 255 + 0.5f), g = (u32)(c.g * 255 + 0.5f), b = (u32)(c.b * 255 + 0.5f), a = (u32)(c.a * 255 + 0.5f);
        StrBuildStr(arena, &text, a == 255 ? StrFormat(arena, "# color.%s = #%02x%02x%02x\n", Prog_ThemeColors[i].name, r, g, b)
                                           : StrFormat(arena, "# color.%s = #%02x%02x%02x%02x\n", Prog_ThemeColors[i].name, r, g, b, a));
    }
    StrBuildStr(arena, &text, S("\n# Whether the shown folder's git repository is fetched now and then, as far as that needs no sign-in, to count the commits to pull.\n"
                                "# git.fetch = false\n"));
    StrBuildStr(arena, &text, S("\n# What Compress Video encodes to (hevc, h264 or av1), on the graphics card when it can, and its constant quality, from 0 (best,\n"
                                "# largest) to 51; 18 to 23 keeps a video looking as it did.\n# video.codec = hevc\n# video.quality = 20\n"
                                "\n# Whether a web address pasted into a folder, downloaded with yt-dlp, comes down as its sound alone, as MP3.\n# download.audio = false\n"
                                "\n# Whether the address of a video in a playlist downloads the whole playlist rather than the video alone.\n# download.playlist = false\n"
                                "\n# Whether a torrent downloaded whole, every file in it, sends its .torrent file to the bin.\n# torrents.delete = false\n"
                                "\n# How loud the inspector plays audio files, from 0 to 100.\n# audio.volume = 100\n"));
    StrBuildStr(arena, &text, StrFormat(arena, "\n# The language of Filey's text by its name: English, a built-in one, or a custom <name>.lang file in this folder.\n"
                                               "# Unset, the system's language when Filey has it built in, and English otherwise.\n# language = %S\n", Prog_SystemLanguage()));
    StrBuildStr(arena, &text, S("\n# The transparent theme (the \"Transparency\" command), in the scheme's colors. glass.window and glass.popup give tints of\n"
                                "# their own, as #rrggbbaa whose last pair is the opacity: glass.window for what is behind the window, glass.popup for the blur\n"
                                "# under menus, the palette and the other floating boxes. glass.opacity is the window tint's opacity alone in percent, where there\n"
                                "# is no backdrop to choose, glass.popupOpacity the popups', and glass.blur is how far that blur reaches in pixels, 0 for none.\n"
                                "# glass.backdrop is what Windows draws behind the window: acrylic (its blur), mica, tabbed or none.\n"
                                "# transparent = false\n# glass.opacity = 55\n# glass.popupOpacity = 75\n# glass.blur = 24\n# glass.backdrop = acrylic\n"));
    StrBuildStr(arena, &text, S("\n# Hotkeys, several separated by commas, and \"G G\" for G pressed twice; \"none\" removes a command's.\n"));
    Prog_State defaults = {0};
    (void)state;
    for (u32 k = Prog_CommandKind_None + 1; k < Prog_CommandKind_Count; ++k) {
        if (!Prog_Commands[k].label.length) continue;
        StrBuildStr(arena, &text, StrFormat(arena, "# hotkey.%S = %S\n", Prog_Commands[k].name, Prog_HotkeysSetting(arena, &defaults, (Prog_CommandKind)k)));
    }
    return text.buffer;
}

// The options of the Options popup, in its order.
global const String Prog_OptionSwitch[] = { SInit("false"), SInit("true") };
// English, then the built-in languages; the custom ones follow.
global const String Prog_OptionLanguages[] = { NoTrInit("English"), NoTrInit("Deutsch"), SInit("Espa\u00F1ol"), SInit("Fran\u00E7ais"), NoTrInit("Polski"), SInit("Portugu\u00EAs"), SInit("\u0420\u0443\u0441\u0441\u043A\u0438\u0439"), SInit("\u7B80\u4F53\u4E2D\u6587") };
global const String Prog_OptionCorners[] = { SInit("square"), SInit("round") };
global const String Prog_OptionLayouts[] = { SInit("auto"), SInit("right"), SInit("below") };
global const String Prog_OptionThumbnails[] = { SInit("never"), SInit("local"), SInit("always") };
global const String Prog_OptionBorders[] = { SInit("1"), SInit("1.5"), SInit("2"), SInit("2.5"), SInit("3") };
global const Prog_OptionDesc Prog_Options[Prog_Option_Count] = {
    [Prog_Option_Language] = { TrInit("Language"), TrInit("The language of Filey's text. Create Custom Language starts a translation of your own, as a file to fill in."),
                               Prog_OptionControl_Choice, 0, 0, Prog_OptionLanguages, CArrayCount(Prog_OptionLanguages), SInit("language") },
    [Prog_Option_Font] = { TrInit("Font"), TrInit("The system-installed font name that is applied to the entire UI."), Prog_OptionControl_Font, 0, 0, NULL, 0, SInit("font") },
    [Prog_Option_InspectorFont] = { TrInit("Inspector Font"), TrInit("The system-installed font used when previewing text contents in the inspector."), Prog_OptionControl_Font, 0, 0,
                                    NULL, 0, SInit("inspector.font") },
    [Prog_Option_FontSize] = { TrInit("Font Size"), TrInit("Determines the scale of the whole UI."), Prog_OptionControl_Stepper, 6, 24, NULL, 0, SInit("scale") },
    [Prog_Option_InspectorFontSize] = { TrInit("Inspector Font Size"), TrInit("Specifies the font size used to display text contents in the inspector."), Prog_OptionControl_Stepper,
                                        Prog_InspectorFontMin, Prog_InspectorFontMax, NULL, 0, SInit("inspector.fontSize") },
    [Prog_Option_Borders] = { TrInit("Border Size"), TrInit("Thickness of the lines between areas and of the outlines of panels, tabs and popups."),
                              Prog_OptionControl_Choice, 0, 0, Prog_OptionBorders, CArrayCount(Prog_OptionBorders), SInit("borders") },
    [Prog_Option_Spacing] = { TrInit("Spacing"), TrInit("Defines the space between items in a list."), Prog_OptionControl_Slider, 0, 13, NULL, 0, SInit("spacing") },
    [Prog_Option_Animations] = { TrInit("Animations"), TrInit("Applies animations to new popups, view scrolling, text cursor, etc."), Prog_OptionControl_Switch, 0, 1,
                                 Prog_OptionSwitch, 2, SInit("animations") },
    [Prog_Option_RoundCorners] = { TrInit("Round Corners"), TrInit("Applies rounding to the corners of UI elements such as popups, buttons, and selections."), Prog_OptionControl_Switch,
                                   0, 1, Prog_OptionCorners, 2, SInit("corners") },
    [Prog_Option_InlineSubmenus] = { TrInit("Inline Submenus"), TrInit("Opens a menu's submenus inside it, under their item, rather than beside it."), Prog_OptionControl_Switch, 0, 1,
                                     Prog_OptionSwitch, 2, SInit("menus.inline") },
    [Prog_Option_SystemScheme] = { TrInit("System Light / Dark Scheme"), TrInit("Automatically adapt between light and dark color schemes based on system settings."),
                                   Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("theme.system") },
    [Prog_Option_ColorScheme] = { TrInit("Color Scheme"), TrInit("Applies a color palette to all UI elements."),
                                  Prog_OptionControl_Choice, 0, 0, Prog_OptionSchemes, CArrayCount(Prog_OptionSchemes), SInit("theme") },
    [Prog_Option_LightScheme] = { TrInit("Light Color Scheme"), TrInit("Color theme applied when the system uses bright colors."), Prog_OptionControl_Choice, 0, 0,
                                  Prog_OptionSchemes, CArrayCount(Prog_OptionSchemes), SInit("theme.light") },
    [Prog_Option_DarkScheme] = { TrInit("Dark Color Scheme"), TrInit("Color theme applied when the system uses dark colors."), Prog_OptionControl_Choice, 0, 0,
                                 Prog_OptionSchemes, CArrayCount(Prog_OptionSchemes), SInit("theme") },
    [Prog_Option_ColorGroups] = { TrInit("Color Scheme"), TrInit("Exposes the individual components of the selected color scheme."), Prog_OptionControl_Expand, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorClear] = { TrInit("Clear"), TrInit("Background color visible only between panel borders."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorCaption] = { TrInit("Caption"), TrInit("Background color for caption bar."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorBackground] = { TrInit("Background"), TrInit("Background color for panels and windows."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorSurface] = { TrInit("Surface"), TrInit("Background color for popups, sidebar, and floating regions like the footer."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorForeground] = { TrInit("Foreground"), TrInit("Background color for scrollbar, dropdowns and other buttons."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorInner] = { TrInit("Inner"), TrInit("Background color for text field."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorBorder] = { TrInit("Border"), TrInit("Border color for panels, windows, and popups."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorOutline] = { TrInit("Outline"), TrInit("Outline color for UI elements like buttons and toggles."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorSeparator] = { TrInit("Separator"), TrInit("Color for line separators between item rows."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorAlternatingRow] = { TrInit("Alternating Row"), TrInit("Background color for alternating rows in lists and columns."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorIconTint] = { TrInit("Icon"), TrInit("Color of all non-system icons in the UI."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorText] = { TrInit("Text"), TrInit("Color of primary UI text."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorSecondary] = { TrInit("Secondary"), TrInit("Color for secondary text."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorGroup] = { TrInit("Group"), TrInit("Color for sections that can be opened or closed."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorFile] = { TrInit("File"), TrInit("Text color for file items."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorFolder] = { TrInit("Folder"), TrInit("Text color for folder items."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorWarning] = { TrInit("Warning"), TrInit("Background color for buttons requiring extra attention."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorProgress] = { TrInit("Progress"), TrInit("Fill color for slider and toggle."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorSelection] = { TrInit("Selection"), TrInit("Background color of selected items."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorRectSelection] = { TrInit("Rect selection"), TrInit("Color of the rectangle when selecting multiple items in a panel."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorMatch] = { TrInit("Match"), TrInit("Color for matching letters in filtered text."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorHidden] = { TrInit("Hidden Opacity"), TrInit("Controls the opacity of hidden/system files using the brightness value."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorHover] = { TrInit("Hover Opacity"), TrInit("Blends the selection with the background when hovering using the brightness value."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorDisabled] = { TrInit("Disabled Opacity"), TrInit("Controls the opacity of disabled items using the brightness value."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorContentHover] = { TrInit("Content Hover"), TrInit("Color of the content when items are hovered."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorContentSelection] = { TrInit("Content Selection"), TrInit("Color of the content when items are selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorContentDisabledSelection] = { TrInit("Content Disabled Selection"), TrInit("Color of the content when items are both disabled and selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorOutlineHover] = { TrInit("Outline Hover"), TrInit("Color of the outlines when items are hovered."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorOutlineSelection] = { TrInit("Outline Selection"), TrInit("Color of the outlines when items are selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorOutlineDisabledSelection] = { TrInit("Outline Disabled Selection"), TrInit("Color of the outlines when items are both disabled and selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorMatchHover] = { TrInit("Match Hover"), TrInit("Color for matching letters in filtered text when items are hovered."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorMatchSelection] = { TrInit("Match Selection"), TrInit("Color for matching letters in filtered text when items are selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorMatchDisabledSelection] = { TrInit("Match Disabled Selection"), TrInit("Color for matching letters in filtered text when items are both disabled and selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorCodeKeyword] = { TrInit("Code Keyword"), TrInit("Color of keywords and keys in the inspector's code."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorCodeString] = { TrInit("Code String"), TrInit("Color of strings and quoted text in the inspector's code."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorCodeNumber] = { TrInit("Code Number"), TrInit("Color of numbers and variables in the inspector's code."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorCodeHeading] = { TrInit("Code Heading"), TrInit("Color of headings, sections and tags in the inspector's text."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_AlternatingRows] = { TrInit("Alternating Row Colors"), TrInit("Applies alternating row colors to improve readability and distinguish adjacent rows."),
                                      Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("rows.alternate") },
    [Prog_Option_InactiveOpacity] = { TrInit("Inactive Panel Contents Opacity"), TrInit("Applies dimming to inactive panel contents to make the active panel stand out."), Prog_OptionControl_Slider,
                                      0, 100, NULL, 0, SInit("inactive.opacity") },
    [Prog_Option_ShowFileExtension] = { TrInit("Show File Extension"), TrInit("Toggle the visibility of file extensions."), Prog_OptionControl_Switch, 0, 1,
                                        Prog_OptionSwitch, 2, SInit("files.extensions") },
    [Prog_Option_ShowHiddenFiles] = { TrInit("Show Hidden Files"), TrInit("Toggle the visibility of hidden and dot files (e.g., .gitignore, .config)."), Prog_OptionControl_Switch, 0, 1,
                                      Prog_OptionSwitch, 2, SInit("files.hidden") },
    [Prog_Option_ShowSystemFiles] = { TrInit("Show System Files"), TrInit("Toggle the visibility of system files."), Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("files.system") },
    [Prog_Option_LongPaths] = { TrInit("Long Paths Support"), TrInit("Enable support for paths longer than 260 characters in every program (admin access required to change)."),
                                Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, {0} },
    [Prog_Option_ShortenedPaths] = { TrInit("Display Compact Paths"), TrInit("Trim paths from the first recognized folder."), Prog_OptionControl_Switch, 0, 1,
                                     Prog_OptionSwitch, 2, SInit("paths.short") },
    [Prog_Option_Breadcrumbs] = { TrInit("Display Path Breadcrumbs"), TrInit("Show previously visited subfolders in the address bar."), Prog_OptionControl_Switch, 0, 1,
                                  Prog_OptionSwitch, 2, SInit("paths.trail") },
    [Prog_Option_ShowCheckboxes] = { TrInit("Show Selection Boxes"), TrInit("Toggle visibility of checkboxes when selecting multiple items in a panel."), Prog_OptionControl_Switch, 0, 1,
                                     Prog_OptionSwitch, 2, SInit("checkboxes") },
    [Prog_Option_ConfirmEmptyTrash] = { TrInit("Confirm Emptying Recycle Bin"), TrInit("Ask before permanently deleting everything in the Recycle Bin."),
                                        Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("trash.confirm") },
    [Prog_Option_GitFetch] = { TrInit("Fetch Repositories"),
                               TrInit("Fetch the shown folder's git repository now and then, at most every five minutes, to count its commits to pull; one that needs a sign-in is left alone."),
                               Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("git.fetch") },
    [Prog_Option_CompressionLevel] = { TrInit("Compression Level"), TrInit("How hard archives Filey makes or adds to are compressed: 0 stores, 9 is smallest and slowest."),
                                       Prog_OptionControl_Slider, 0, 9, NULL, 0, SInit("archive.level") },
    [Prog_Option_VideoCodec] = { TrInit("Video Codec"), TrInit("What Compress Video encodes to, on the graphics card when it has an encoder for it: NVENC, AMF or Quick Sync."),
                                 Prog_OptionControl_Choice, 0, 0, Prog_OptionVideoCodecs, CArrayCount(Prog_OptionVideoCodecs), SInit("video.codec") },
    [Prog_Option_VideoQuality] = { TrInit("Video Quality"), TrInit("The constant quality Compress Video keeps: lower is better and larger; 18 to 23 looks as the original does."),
                                   Prog_OptionControl_Slider, 0, 51, NULL, 0, SInit("video.quality") },
    [Prog_Option_DownloadAudio] = { TrInit("Download Audio Only"), TrInit("A web address pasted into a folder is downloaded with yt-dlp; this takes its sound alone, as MP3."),
                                    Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("download.audio") },
    [Prog_Option_DownloadPlaylist] = { TrInit("Download Playlists"), TrInit("The web address of a video in a playlist downloads every video of the playlist, rather than that one alone."),
                                       Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("download.playlist") },
    [Prog_Option_DeleteFinishedTorrents] = { TrInit("Delete Finished Torrents"), TrInit("Sends a .torrent file to the Recycle Bin once every file in it has downloaded."),
                                             Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("torrents.delete") },
    [Prog_Option_AudioVolume] = { TrInit("Audio Volume"), TrInit("How loud the inspector plays audio files."), Prog_OptionControl_Slider, 0, 100, NULL, 0, SInit("audio.volume") },
    [Prog_Option_Notices] = { TrInit("Notifications"), TrInit("Which notices show in the window's corner: all, warnings and errors, errors only, or none."),
                              Prog_OptionControl_Choice, 0, 0, Prog_OptionNotices, CArrayCount(Prog_OptionNotices), SInit("notices") },
    [Prog_Option_MutedNotices] = { TrInit("Muted Notifications"), TrInit("Kinds of notice muted with their bell, kept in MutedNotices.conf; the list shows any of them again."),
                                   Prog_OptionControl_Muted, 0, 0, NULL, 0, {0} },
    [Prog_Option_NoticesInSidebar] = { TrInit("Notifications In Sidebar"),
                                       TrInit("Shows notices and the progress of copies, downloads and other work at the bottom of the sidebar, rather than in a panel's corner."),
                                       Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("notices.sidebar") },
    [Prog_Option_CheckUpdates] = { TrInit("Update Check"), TrInit("Asks GitHub once a day whether a newer Filey is out, and offers to update to it."),
                                   Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("updates.check") },
    [Prog_Option_StoreRecents] = { TrInit("Store Recents"), TrInit("Determines how recently accessed files are stored."), Prog_OptionControl_Choice, 0, 0,
                                   Prog_OptionStoreRecents, CArrayCount(Prog_OptionStoreRecents), SInit("recents.store") },
    [Prog_Option_ShowRecents] = { TrInit("Highlight Recents"), TrInit("Mark recently accessed files and directories."), Prog_OptionControl_Switch, 0, 1,
                                  Prog_OptionSwitch, 2, SInit("recents.highlight") },
    [Prog_Option_SortRecents] = { TrInit("Sort Recents On Top"), TrInit("Sort recently accessed files and directories to the top."), Prog_OptionControl_Switch, 0, 1,
                                  Prog_OptionSwitch, 2, SInit("recents.sort") },
    [Prog_Option_LoadThumbnails] = { TrInit("Load Thumbnails"), TrInit("Criteria for loading thumbnails of listed directories."), Prog_OptionControl_Choice, 0, 0,
                                     Prog_OptionThumbnails, CArrayCount(Prog_OptionThumbnails), SInit("thumbnails") },
    [Prog_Option_CalcFolderSizes] = { TrInit("Calculate Folder Sizes"), TrInit("Calculates folder sizes by scanning their contents."), Prog_OptionControl_Switch, 0, 1,
                                      Prog_OptionSwitch, 2, SInit("folders.size") },
    [Prog_Option_CalcFolderSizesSSD] = { TrInit("SSDs"), TrInit("Calculate folder sizes on local SSD drives."), Prog_OptionControl_Switch, 0, 1,
                                         Prog_OptionSwitch, 2, SInit("folders.size.ssd") },
    [Prog_Option_CalcFolderSizesHDD] = { TrInit("HDDs"), TrInit("Calculate folder sizes on local HDD drives."), Prog_OptionControl_Switch, 0, 1,
                                         Prog_OptionSwitch, 2, SInit("folders.size.hdd") },
    [Prog_Option_CalcFolderSizesNetwork] = { TrInit("Network Locations"), TrInit("Calculate folder sizes on network drives and shared locations."), Prog_OptionControl_Switch,
                                             0, 1, Prog_OptionSwitch, 2, SInit("folders.size.network") },
    [Prog_Option_ActOnPress] = { TrInit("Act On Press"), TrInit("Whether interactions register on mouse press or release."), Prog_OptionControl_Switch, 0, 1,
                                 Prog_OptionSwitch, 2, SInit("click.onPress") },
    [Prog_Option_SingleClick] = { TrInit("Single-click To Open"), TrInit("Open items with a single click."), Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("click.single") },
    [Prog_Option_SingleClickFoldersOnly] = { TrInit("Folders Only"), TrInit("Single click applies to folders only."), Prog_OptionControl_Switch, 0, 1,
                                             Prog_OptionSwitch, 2, SInit("click.singleFoldersOnly") },
    [Prog_Option_HoverToSelect] = { TrInit("Point To Select"), TrInit("Select items by hovering over them."), Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("hover.select") },
    [Prog_Option_HoverToFocus] = { TrInit("Point To Focus Panel"), TrInit("Give the keyboard to the panel the pointer moves over."), Prog_OptionControl_Switch, 0, 1,
                                   Prog_OptionSwitch, 2, SInit("hover.focus") },
    [Prog_Option_UnderlineOnHover] = { TrInit("Underline On Point"), TrInit("Underlines items only when pointing at them."), Prog_OptionControl_Switch, 0, 1,
                                       Prog_OptionSwitch, 2, SInit("hover.underline") },
    [Prog_Option_DoubleClickToParent] = { TrInit("Double-click To Parent"), TrInit("Go to parent folder by double clicking background."), Prog_OptionControl_Switch, 0, 1,
                                          Prog_OptionSwitch, 2, SInit("click.doubleToParent") },
    [Prog_Option_ClickToRename] = { TrInit("Slow Click To Rename"), TrInit("Rename the selected item by clicking its name again after a pause."), Prog_OptionControl_Switch, 0, 1,
                                    Prog_OptionSwitch, 2, SInit("click.rename") },
    [Prog_Option_Overscroll] = { TrInit("Scroll Beyond Last Item"), TrInit("Allows scrolling past the last item for extra space."), Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("scroll.over") },
    [Prog_Option_ReverseScroll] = { TrInit("Reverse Scroll Direction"), TrInit("Reverses the scroll direction in scrollable views."), Prog_OptionControl_Switch, 0, 1,
                                    Prog_OptionSwitch, 2, SInit("scroll.reverse") },
    [Prog_Option_CharacterKeyAction] = { TrInit("Character Key Action"), TrInit("Determines the action when you press raw character keys without modifiers."),
                                         Prog_OptionControl_Choice, 0, 0, Prog_OptionCharacterKeys, Prog_CharacterKeys_Count, SInit("keys.character") },
    [Prog_Option_InspectorLayout] = { TrInit("Inspector Layout"), TrInit("Default layout for opening the new inspector."), Prog_OptionControl_Choice, 0, 0,
                                      Prog_OptionLayouts, CArrayCount(Prog_OptionLayouts), SInit("inspector.layout") },
    [Prog_Option_HighlightCode] = { TrInit("Highlight Code"), TrInit("Color keywords, strings, comments and numbers of code in the inspector."), Prog_OptionControl_Switch, 0, 1,
                                    Prog_OptionSwitch, 2, SInit("inspector.highlight") },
    [Prog_Option_LoadOnStartup] = { TrInit("Load On Startup"), TrInit("Previous session, location, or custom layout to load on startup."), Prog_OptionControl_Choice, 0, 0,
                                    Prog_OptionStartups, CArrayCount(Prog_OptionStartups), SInit("startup") },
    [Prog_Option_NewTabShow] = { TrInit("New Tab Show"), TrInit("Location to open in a new tab."), Prog_OptionControl_Choice, 0, 0,
                                 Prog_OptionNewTabs, CArrayCount(Prog_OptionNewTabs), SInit("newtab") },
    [Prog_Option_SingleWindow] = { TrInit("Open In One Window"), TrInit("Starting Filey or opening a folder from elsewhere uses the open window, in a new tab."),
                                   Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("instances.single") },
    [Prog_Option_VSync] = { TrInit("VSync"), TrInit("Reduces hardware demands during high activity, such as rapid mouse movements. This comes at the cost of slight input lag."),
                            Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("vsync") },
    [Prog_Option_Transparency] = { TrInit("Transparency"), TrInit("Shows what is behind the window through it, blurred."), Prog_OptionControl_Switch, 0, 1,
                                   Prog_OptionSwitch, 2, SInit("transparent") },
    [Prog_Option_WindowOpacity] = { TrInit("Window Opacity"), TrInit("How much the window's color covers what is behind it."),
                                    Prog_OptionControl_Slider, 0, 100, NULL, 0, SInit("glass.opacity") },
    [Prog_Option_PopupBlur] = { TrInit("Popup Blur"), TrInit("How far menus and popups blur what is under them."), Prog_OptionControl_Slider, 0, 100,
                                NULL, 0, SInit("glass.blur") },
    [Prog_Option_PopupOpacity] = { TrInit("Popup Opacity"), TrInit("How much the color of menus and popups covers the blur under them."),
                                   Prog_OptionControl_Slider, 0, 100, NULL, 0, SInit("glass.popupOpacity") },
    [Prog_Option_Backdrop] = { TrInit("Backdrop"), TrInit("What Windows draws behind the transparent window."), Prog_OptionControl_Choice, 0, 0,
                               Prog_OptionBackdrops, CArrayCount(Prog_OptionBackdrops), SInit("glass.backdrop") },
};

// The options whose name or description holds what is typed into the popup, in their order, into `found`; returns how many.
// Whether `option` sets anything on this system: the backdrop, VSync and system files are Windows' alone.
// Without a backdrop to choose, as on Linux, the window's opacity is set instead.
function b32 Prog_OptionApplies(Prog_Option option)
{
    if (option == Prog_Option_Backdrop) return PlatformHasBackdrops();
    if (option == Prog_Option_WindowOpacity) return !PlatformHasBackdrops();
    if (option == Prog_Option_VSync) return PlatformHasVSync();
    if (option == Prog_Option_ShowSystemFiles) return P_HasSystemFiles();
    if (option == Prog_Option_LongPaths) return P_HasPathLimit();
    // Torrents download with aria2c, looked for once a run.
    if (option == Prog_Option_DeleteFinishedTorrents) {
        local_persist i32 aria = -1;
        if (aria < 0) aria = P_ProgramFound(S("aria2c"));
        return aria > 0;
    }
    return true;
}

// The scheme shown, by its place as `scheme` counts them, and its name: with the system's choice in its light mode, the light one.
function u8 Prog_ShownScheme(const Prog_State *state, String *name)
{
    b32 light = state->systemScheme && !state->systemDark;
    const Prog_FontName *named = light ? &state->lightSchemeName : &state->schemeName;
    if (name) *name = StrMake(named->text, named->length);
    return light ? state->lightScheme : state->scheme;
}

function u32 Prog_OptionMatches(Prog_State *state, u8 *found)
{
    String typed = Prog_EditText(&state->palette);
    u32 count = 0;
    for (u32 i = 0; i < Prog_Option_Count; ++i) {
        if (!Prog_OptionApplies((Prog_Option)i)) continue;
        // One scheme to choose, which starts as the system's mode's.
        if (i == Prog_Option_SystemScheme || i == Prog_Option_LightScheme || i == Prog_Option_DarkScheme) continue;
        // The options that narrow the one above them only while it is on.
        // That is the see-through window's own, the drives whose folders are measured, and what a single click opens.
        if (i >= Prog_Option_WindowOpacity && i <= Prog_Option_Backdrop && !state->transparent) continue;
        if (i >= Prog_Option_CalcFolderSizesSSD && i <= Prog_Option_CalcFolderSizesNetwork && state->noFolderSizes) continue;
        if (i == Prog_Option_SingleClickFoldersOnly && !state->singleClick) continue;
        // The muted kinds of notice once there are any.
        if (i == Prog_Option_MutedNotices && !state->mutedNotices.header.count) continue;
        b32 colors = i == Prog_Option_ColorGroups || Prog_Options[i].control == Prog_OptionControl_Color;
        if (colors && (Prog_ShownScheme(state, NULL) < Prog_BuiltInSchemes || typed.length || (i != Prog_Option_ColorGroups && !state->colorsExpanded))) continue;
        if (!typed.length || Prog_FuzzyScore(Prog_Tr(Prog_Options[i].label), Prog_Tr(Prog_Options[i].tooltip), typed, NULL, NULL) > 0) found[count++] = (u8)i;
    }
    return count;
}

// What `option` is set to: a switch's 0 or 1, a stepper's or slider's number, a choice's place among its names.
function i32 Prog_OptionValue(const Prog_State *state, Prog_Option option)
{
    switch (option) {
        case Prog_Option_FontSize: return (i32)floorf((state->uiScale > 0 ? state->uiScale : 1) * 9.0f + 0.5f);
        case Prog_Option_InspectorFontSize: return (i32)(state->inspectorFontSize ? state->inspectorFontSize : Prog_InspectorFontSize);
        case Prog_Option_InspectorLayout: return state->inspectorLayout;
        case Prog_Option_Animations: return !state->animationsOff;
        case Prog_Option_ColorScheme: return Prog_ShownScheme(state, NULL);
        case Prog_Option_DarkScheme: return state->scheme;
        case Prog_Option_LightScheme: return state->lightScheme;
        case Prog_Option_Spacing: return 4 + state->spacingShift;
        case Prog_Option_Borders: return (i32)state->borderSize;
        case Prog_Option_RoundCorners: return !state->squareCorners;
        case Prog_Option_InlineSubmenus: return state->menuInline;
        case Prog_Option_SystemScheme: return state->systemScheme;
        case Prog_Option_AlternatingRows: return state->alternatingRows;
        case Prog_Option_InactiveOpacity: return 60 + state->inactiveShift;
        case Prog_Option_ShowFileExtension: return !state->hideExtensions;
        case Prog_Option_ShowHiddenFiles: return state->showHidden;
        case Prog_Option_ShowSystemFiles: return state->showSystem;
        case Prog_Option_LongPaths: return state->longPaths;
        case Prog_Option_ShortenedPaths: return !state->fullPaths;
        case Prog_Option_Breadcrumbs: return !state->noTrail;
        case Prog_Option_ShowCheckboxes: return state->showCheckboxes;
        case Prog_Option_ConfirmEmptyTrash: return !state->noTrashConfirm;
        case Prog_Option_GitFetch: return state->gitFetch;
        case Prog_Option_CompressionLevel: return state->archiveLevel;
        case Prog_Option_VideoCodec: return state->videoCodec;
        case Prog_Option_VideoQuality: return state->videoQuality;
        case Prog_Option_DownloadAudio: return state->downloadAudio;
        case Prog_Option_DownloadPlaylist: return state->downloadPlaylist;
        case Prog_Option_DeleteFinishedTorrents: return state->deleteFinishedTorrents;
        case Prog_Option_AudioVolume: return state->audioVolume;
        case Prog_Option_StoreRecents: return state->storeRecents;
        case Prog_Option_Notices: return state->noticeLevel;
        case Prog_Option_NoticesInSidebar: return state->noticesInSidebar;
        case Prog_Option_CheckUpdates: return state->checkUpdates;
        case Prog_Option_Language: {
            String name = StrMake(state->language.text, state->language.length);
            for (u32 i = 1; name.length && i < CArrayCount(Prog_OptionLanguages); ++i) if (StrIsEqual(name, Prog_OptionLanguages[i])) return (i32)i;
            for (u64 i = 0; name.length && i < state->languageNames.header.count; ++i) if (StrIsEqual(name, state->languageNames.v[i])) return (i32)(CArrayCount(Prog_OptionLanguages) + i);
            return 0;
        }
        case Prog_Option_MutedNotices: return (i32)state->mutedNotices.header.count;
        case Prog_Option_ShowRecents: return state->showRecents;
        case Prog_Option_SortRecents: return state->sortRecents;
        case Prog_Option_LoadOnStartup: {
            String layout = StrMake((const char *)state->startupLayout, state->startupLayoutLength);
            for (u64 i = 0; layout.length && i < state->layoutNames.header.count; ++i) if (Prog__EqualFolded(state->layoutNames.v[i], layout)) return Prog_Startup_Count + (i32)i;
            return state->startup;
        }
        case Prog_Option_NewTabShow: return state->newTab;
        case Prog_Option_SingleWindow: return !state->separateInstances;
        case Prog_Option_VSync: return !state->noVSync;
        case Prog_Option_HighlightCode: return !state->noHighlight;
        case Prog_Option_LoadThumbnails: return state->thumbnailMode == Prog_Thumbnails_Never ? 0 : state->thumbnailMode == Prog_Thumbnails_Always ? 2 : 1;
        case Prog_Option_CalcFolderSizes: return !state->noFolderSizes;
        case Prog_Option_CalcFolderSizesSSD: return !state->noFolderSizesSolidState;
        case Prog_Option_CalcFolderSizesHDD: return state->folderSizesSpinning;
        case Prog_Option_CalcFolderSizesNetwork: return state->folderSizesNetwork;
        case Prog_Option_ActOnPress: return !state->actOnRelease;
        case Prog_Option_SingleClick: return state->singleClick;
        case Prog_Option_SingleClickFoldersOnly: return state->singleClickFoldersOnly;
        case Prog_Option_HoverToSelect: return state->hoverToSelect;
        case Prog_Option_HoverToFocus: return !state->clickToFocus;
        case Prog_Option_UnderlineOnHover: return !state->underlineAlways;
        case Prog_Option_DoubleClickToParent: return state->doubleClickToParent;
        case Prog_Option_ClickToRename: return !state->noClickRename;
        case Prog_Option_Overscroll: return !state->noOverscroll;
        case Prog_Option_ReverseScroll: return state->reverseScroll;
        case Prog_Option_CharacterKeyAction: return state->characterKeys;
        case Prog_Option_Transparency: return state->transparent != 0;
        case Prog_Option_PopupBlur: return (i32)floorf(Prog_GlassBlur + 0.5f);
        case Prog_Option_PopupOpacity: return (i32)floorf(Prog_GlassPopup.a * 100 + 0.5f);
        case Prog_Option_WindowOpacity: return (i32)floorf(Prog_GlassWindow.a * 100 + 0.5f);
        case Prog_Option_Backdrop: return state->glassBackdrop;
        default: return 0;
    }
}

// Sets `name = value` in Settings.conf, over the line that sets it or that names it commented out, else at its end, and applies the file.
function void Prog__WriteSetting(Prog_State *state, String name, String value)
{
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    String path = Prog_JoinPath(scratch.arena, state->configDirectory, S("Settings.conf"));
    String text = {0};
    if (!P_ReadFile(scratch.arena, path, &text)) text = Prog_SettingsTemplate(scratch.arena, state);
    ArrayStr lines = StrSplit(scratch.arena, text, '\n');
    u64 set = UINT64_MAX, commented = UINT64_MAX;
    for (u64 i = 0; i < lines.header.count; ++i) {
        String line = lines.v[i];
        b32 comment = line.length && line.value[0] == '#';
        while (line.length && (line.value[0] == '#' || line.value[0] == ' ')) line = StrFrom(line, 1);
        Index equals = StrIndexChar(line, '=', SSF_None);
        if (!equals.exists) continue;
        String key = StrFromTo(line, 0, (i64)equals.value);
        while (key.length && key.value[key.length - 1] == ' ') --key.length;
        if (!StrIsEqual(key, name)) continue;
        if (!comment) set = i;
        else if (commented == UINT64_MAX) commented = i;
    }
    String entry = StrFormat(scratch.arena, "%S = %S", name, value);
    u64 at = set != UINT64_MAX ? set : commented;
    StrBuilder out = {0};
    for (u64 i = 0; i < lines.header.count; ++i) {
        // The text's own last newline leaves an empty last piece.
        if (i + 1 == lines.header.count && !lines.v[i].length) break;
        StrBuildStr(scratch.arena, &out, i == at ? entry : lines.v[i]);
        StrBuildChar(scratch.arena, &out, '\n');
    }
    if (at == UINT64_MAX) {
        StrBuildStr(scratch.arena, &out, entry);
        StrBuildChar(scratch.arena, &out, '\n');
    }
    P_WriteFile(scratch.arena, path, out.buffer);
    ReleaseScratch(scratch);
    Prog_ReloadSettingsFile(state);
}

// Sets `option` to `value`, kept within what it takes, in Settings.conf; without a config folder only for this run.
// Shows `option` at `value` for now, without saving it, as while a slider is dragged; only the numbers have this.
function void Prog_PreviewOption(Prog_State *state, Prog_Option option, i32 value)
{
    const Prog_OptionDesc *desc = option < Prog_Option_Count ? &Prog_Options[option] : NULL;
    if (!desc || desc->choices) return;
    value = Min(Max(value, desc->least), desc->most);
    if (option == Prog_Option_FontSize) state->uiScale = (f32)value / 9.0f;
    if (option == Prog_Option_InspectorFontSize) state->inspectorFontSize = (u32)value;
    if (option == Prog_Option_PopupBlur) Prog_GlassBlur = (f32)value;
    if (option == Prog_Option_PopupOpacity) Prog_GlassPopup.a = (f32)value / 100.0f;
    if (option == Prog_Option_WindowOpacity) Prog_GlassWindow.a = (f32)value / 100.0f;
    if (option == Prog_Option_PopupOpacity || option == Prog_Option_WindowOpacity) Prog__ApplyTheme(state);
    if (option == Prog_Option_Spacing) state->spacingShift = value - 4;
    if (option == Prog_Option_InactiveOpacity) state->inactiveShift = value - 60;
}

// The choices of `option`: its table's, and for Load On Startup the saved layouts after them.
function const String *Prog_OptionChoices(const Prog_State *state, Prog_Option option, Arena *arena, u32 *count)
{
    const Prog_OptionDesc *desc = &Prog_Options[option];
    *count = desc->choiceCount;
    // The schemes take the custom ones after the built-in ones.
    b32 schemes = option == Prog_Option_ColorScheme || option == Prog_Option_LightScheme || option == Prog_Option_DarkScheme;
    const ArrayStr *more = option == Prog_Option_LoadOnStartup ? &state->layoutNames : schemes ? &state->schemeNames : option == Prog_Option_Language ? &state->languageNames : NULL;
    if (!more || !more->header.count) return desc->choices;
    String *all = ArenaPushArray(arena, desc->choiceCount + more->header.count, String);
    for (u32 i = 0; i < desc->choiceCount; ++i) all[i] = desc->choices[i];
    for (u64 i = 0; i < more->header.count; ++i) all[desc->choiceCount + i] = more->v[i];
    *count = desc->choiceCount + (u32)more->header.count;
    return all;
}

// Whether `a` comes before `b`, letters of either case together.
function b32 Prog__BeforeFolded(String a, String b)
{
    for (u64 i = 0; i < Min(a.length, b.length); ++i) {
        u8 x = Prog__FoldAscii((u8)a.value[i]), y = Prog__FoldAscii((u8)b.value[i]);
        if (x != y) return x < y;
    }
    return a.length < b.length;
}

// Adds the scheme `name` with the colors `text` among `names` and `texts`, in its place by name.
function void Prog__PlaceScheme(Arena *arena, ArrayStr *names, ArrayStr *texts, String name, String text)
{
    u64 at = 0;
    while (at < names->header.count && Prog__BeforeFolded(names->v[at], name)) ++at;
    ArrayPush(arena, *names, (String){0});
    ArrayPush(arena, *texts, (String){0});
    for (u64 i = names->header.count - 1; i > at; --i) names->v[i] = names->v[i - 1], texts->v[i] = texts->v[i - 1];
    names->v[at] = StrCopy(arena, name);
    texts->v[at] = StrCopy(arena, text);
}

// The custom schemes, a file each in the settings folder, "<name>.scheme" with its colors' lines, in the order of their names.
// Matugen renders one from the wallpaper over its file, which then applies again.
function void Prog_LoadSchemes(Prog_State *state)
{
    ArenaClear(&state->schemesArena);
    state->schemeNames = state->schemeTexts = (ArrayStr){0};
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    P_DirectoryResult listing = P_EnumerateDirectory(scratch.arena, state->configDirectory, NULL);
    u64 extension = Prog_SchemeExtension.length;
    for (u64 i = 0; i < listing.files.header.count; ++i) {
        String file = listing.files.v[i].name, text = {0};
        if (listing.files.v[i].kind != P_FileKind_File || file.length <= extension || !Prog__EqualFolded(StrFrom(file, file.length - extension), Prog_SchemeExtension)) continue;
        String name = StrFromTo(file, 0, (i64)(file.length - extension));
        if (name.length > sizeof(state->schemeName.text) || !P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, file), &text)) continue;
        // Without the carriage returns an editor or Matugen on Windows ends its lines with.
        StrBuilder colors = {0};
        ArrayStr lines = StrSplit(scratch.arena, text, '\n');
        for (u64 l = 0; l < lines.header.count; ++l) {
            String line = lines.v[l];
            if (line.length && line.value[line.length - 1] == '\r') --line.length;
            StrBuildStr(scratch.arena, &colors, line);
            StrBuildChar(scratch.arena, &colors, '\n');
        }
        Prog__PlaceScheme(&state->schemesArena, &state->schemeNames, &state->schemeTexts, name, colors.buffer);
    }
    ReleaseScratch(scratch);
}

// Gives the custom scheme named `name` the colors `text`, in its place by name when it is new, or with `remove` takes it out; its file is
// written or deleted at once, as this happens only from a command.
function void Prog_SetScheme(Prog_State *state, String name, String text, b32 remove)
{
    Arena kept = {0};
    ArrayStr names = {0}, texts = {0};
    for (u64 i = 0; i < state->schemeNames.header.count; ++i) {
        if (Prog__EqualFolded(state->schemeNames.v[i], name)) continue;
        ArrayPush(&kept, names, StrCopy(&kept, state->schemeNames.v[i]));
        ArrayPush(&kept, texts, StrCopy(&kept, state->schemeTexts.v[i]));
    }
    if (!remove) Prog__PlaceScheme(&kept, &names, &texts, name, text);
    ArenaRelease(&state->schemesArena);
    state->schemesArena = kept;
    state->schemeNames = names;
    state->schemeTexts = texts;
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    String path = Prog_JoinPath(scratch.arena, state->configDirectory, StrFormat(scratch.arena, "%S%S", name, Prog_SchemeExtension));
    P_FileError error = remove ? P_DeleteItems(scratch.arena, &path, 1, false, NULL) : P_WriteFile(scratch.arena, path, text);
    if (error.code) {
        Prog_Notify(state, Prog_NoticeKind_Error, S("schemes.failed"), Tr("Couldn't save the color schemes"), error.message);
    }
    ReleaseScratch(scratch);
}

function String Prog__OptionName(Arena *arena, Prog_Option option, String name);

// An editable copy of the scheme `scheme`, by its place among the built-in and custom ones, named "Custom" with the first number not
// taken, which `option` then shows.
function b32 Prog_ForkScheme(Prog_State *state, u32 scheme, Prog_Option option)
{
    Scratch scratch = GetScratch();
    String name = {0};
    for (u32 n = 1; n < 1000 && !name.length; ++n) {
        String candidate = StrFormat(scratch.arena, NoTrF("Custom%u"), n);
        b32 taken = false;
        for (u64 i = 0; i < state->schemeNames.header.count && !taken; ++i) taken = Prog__EqualFolded(state->schemeNames.v[i], candidate);
        if (!taken) name = candidate;
    }
    u32 groups[Prog_ColorGroupCount];
    String shown = scheme < Prog_BuiltInSchemes ? Prog_OptionSchemes[scheme] : scheme - Prog_BuiltInSchemes < state->schemeNames.header.count ? state->schemeNames.v[scheme - Prog_BuiltInSchemes] : S("");
    Prog__UseScheme(state, shown, groups);
    if (name.length) {
        Prog_SetScheme(state, name, Prog_SchemeText(scratch.arena, groups), false);
        Prog_SetOptionText(state, option, name);
    }
    ReleaseScratch(scratch);
    return name.length != 0;
}

// The color the group of `option` has now.
function R_Color Prog_OptionColor(const Prog_State *state, Prog_Option option)
{
    return Prog__HexColor(state->schemeGroups[option - Prog_Option_ColorClear]);
}

function void Prog_RgbToHsb(R_Color color, f32 *hue, f32 *saturation, f32 *brightness)
{
    f32 high = MaxF32(color.r, MaxF32(color.g, color.b)), low = MinF32(color.r, MinF32(color.g, color.b)), span = high - low;
    f32 h = 0;
    if (span > 0 && high == color.r) h = (color.g - color.b) / span;
    else if (span > 0 && high == color.g) h = 2 + (color.b - color.r) / span;
    else if (span > 0) h = 4 + (color.r - color.g) / span;
    h /= 6;
    *hue = h < 0 ? h + 1 : h;
    *saturation = high > 0 ? span / high : 0;
    *brightness = high;
}

function R_Color Prog_HsbToRgb(f32 hue, f32 saturation, f32 brightness, f32 alpha)
{
    f32 h = (hue >= 1 ? 0 : hue) * 6, f = h - floorf(h), p = brightness * (1 - saturation), q = brightness * (1 - saturation * f), t = brightness * (1 - saturation * (1 - f));
    switch ((u32)h) {
        case 0: return (R_Color){ brightness, t, p, alpha };
        case 1: return (R_Color){ q, brightness, p, alpha };
        case 2: return (R_Color){ p, brightness, t, alpha };
        case 3: return (R_Color){ p, q, brightness, alpha };
        case 4: return (R_Color){ t, p, brightness, alpha };
        default: return (R_Color){ brightness, p, q, alpha };
    }
}

// Sets channel `channel` of the picker's color, hue, saturation and brightness then red, green and blue, to `value` from 0 to 1, and shows it.
function void Prog_PickColor(Prog_State *state, u32 channel, f32 value)
{
    u32 *group = &state->schemeGroups[state->colorOption - Prog_Option_ColorClear];
    R_Color color = Prog__HexColor(*group);
    value = UI__Clamp(value, 0, 1);
    if (channel < 3) {
        f32 *hsb[3] = { &state->pickerHue, &state->pickerSaturation, &state->pickerBrightness };
        *hsb[channel] = value;
        color = Prog_HsbToRgb(state->pickerHue, state->pickerSaturation, state->pickerBrightness, 1);
    } else if (channel < 6) {
        f32 *rgb[3] = { &color.r, &color.g, &color.b };
        *rgb[channel - 3] = value;
        // A gray keeps the hue it had, which RGB alone has none of.
        f32 hue = state->pickerHue;
        Prog_RgbToHsb(color, &state->pickerHue, &state->pickerSaturation, &state->pickerBrightness);
        if (state->pickerSaturation == 0) state->pickerHue = hue;
    }
    *group = (u32)(color.r * 255 + 0.5f) << 16 | (u32)(color.g * 255 + 0.5f) << 8 | (u32)(color.b * 255 + 0.5f);
    Prog_ThemeUser = Prog__ThemeFromScheme(state->schemeGroups);
    Prog__ApplyTheme(state);
}

// Closes the color picker, writing the scheme it changed once.
function void Prog_CloseColorPicker(Prog_State *state)
{
    String name;
    if (state->colorPicking && Prog_ShownScheme(state, &name) >= Prog_BuiltInSchemes) {
        Scratch scratch = GetScratch();
        Prog_SetScheme(state, StrCopy(scratch.arena, name), Prog_SchemeText(scratch.arena, state->schemeGroups), false);
        ReleaseScratch(scratch);
    }
    state->colorPicking = false;
    state->pickerField = 0;
    R_TextureRelease(state->pickerSquare);
    R_TextureRelease(state->pickerStrip);
    state->pickerSquare = state->pickerStrip = (R_TextureHandle){0};
}

// Sets the picker's color to `rgb`, as 0xRRGGBB, and shows it; a gray keeps the hue it had.
function void Prog_PickHex(Prog_State *state, u32 rgb)
{
    state->schemeGroups[state->colorOption - Prog_Option_ColorClear] = rgb & 0xFFFFFF;
    f32 hue = state->pickerHue;
    Prog_RgbToHsb(Prog__HexColor(rgb), &state->pickerHue, &state->pickerSaturation, &state->pickerBrightness);
    if (state->pickerSaturation == 0) state->pickerHue = hue;
    Prog_ThemeUser = Prog__ThemeFromScheme(state->schemeGroups);
    Prog__ApplyTheme(state);
}

// What number box `field` of the picker shows: H, S, B, R, G and B as whole numbers, then the hex.
function String Prog_PickerFieldText(Prog_State *state, Arena *arena, u32 field)
{
    u32 rgb = state->schemeGroups[state->colorOption - Prog_Option_ColorClear];
    switch (field) {
        case 1: return StrFormat(arena, "%d", (i32)(state->pickerHue * 360 + 0.5f));
        case 2: return StrFormat(arena, "%d", (i32)(state->pickerSaturation * 100 + 0.5f));
        case 3: return StrFormat(arena, "%d", (i32)(state->pickerBrightness * 100 + 0.5f));
        case 4: return StrFormat(arena, "%u", (rgb >> 16) & 0xFF);
        case 5: return StrFormat(arena, "%u", (rgb >> 8) & 0xFF);
        case 6: return StrFormat(arena, "%u", rgb & 0xFF);
        case 7: return StrFormat(arena, "%06X", rgb);
        default: return (String){0};
    }
}

// Stops typing in a color group's hex box; `keep` sets the group to its six digits, saved in the shown custom scheme.
function void Prog_EndHexEdit(Prog_State *state, b32 keep)
{
    String text = Prog_EditText(&state->hexEdit), name;
    Prog_Option option = (Prog_Option)state->hexOption;
    state->hexOption = 0;
    if (!keep || text.length != 6 || Prog_ShownScheme(state, &name) < Prog_BuiltInSchemes) return;
    u32 rgb = 0;
    for (u64 i = 0; i < text.length; ++i) {
        u8 c = (u8)text.value[i];
        rgb = rgb * 16 + (u32)(c <= '9' ? c - '0' : c - 'A' + 10);
    }
    state->schemeGroups[option - Prog_Option_ColorClear] = rgb;
    Prog_ThemeUser = Prog__ThemeFromScheme(state->schemeGroups);
    Prog__ApplyTheme(state);
    Scratch scratch = GetScratch();
    Prog_SetScheme(state, StrCopy(scratch.arena, name), Prog_SchemeText(scratch.arena, state->schemeGroups), false);
    ReleaseScratch(scratch);
}

// Arithmetic typed into a number's box, as in Unreal's: numbers, + - * / and parentheses, with the usual precedence and signs in front.
typedef struct Prog__Expression {
    String text;
    u64 at;
    u32 depth;
} Prog__Expression;

function void Prog__ExpressionSkip(Prog__Expression *e)
{
    while (e->at < e->text.length && (e->text.value[e->at] == ' ' || e->text.value[e->at] == '\t')) ++e->at;
}

function b32 Prog__ExpressionSum(Prog__Expression *e, f64 *value);

function b32 Prog__ExpressionAtom(Prog__Expression *e, f64 *value)
{
    Prog__ExpressionSkip(e);
    b32 negative = false;
    while (e->at < e->text.length && (e->text.value[e->at] == '+' || e->text.value[e->at] == '-')) {
        negative ^= e->text.value[e->at++] == '-';
        Prog__ExpressionSkip(e);
    }
    if (e->at < e->text.length && e->text.value[e->at] == '(') {
        if (++e->depth > 32) return false;
        ++e->at;
        if (!Prog__ExpressionSum(e, value)) return false;
        Prog__ExpressionSkip(e);
        --e->depth;
        if (e->at >= e->text.length || e->text.value[e->at] != ')') return false;
        ++e->at;
    } else {
        // Digits with a point or a comma between them, as the decimal separator is typed either way.
        f64 number = 0, scale = 0;
        u64 start = e->at;
        for (; e->at < e->text.length; ++e->at) {
            char c = e->text.value[e->at];
            if (c >= '0' && c <= '9') {
                if (scale > 0) number += (f64)(c - '0') * scale, scale *= 0.1;
                else number = number * 10 + (f64)(c - '0');
            } else if ((c == '.' || c == ',') && scale == 0) {
                scale = 0.1;
            } else {
                break;
            }
        }
        if (e->at == start || (e->at == start + 1 && scale > 0)) return false;
        *value = number;
    }
    if (negative) *value = -*value;
    return true;
}

function b32 Prog__ExpressionTerm(Prog__Expression *e, f64 *value)
{
    if (!Prog__ExpressionAtom(e, value)) return false;
    for (;;) {
        Prog__ExpressionSkip(e);
        if (e->at >= e->text.length || (e->text.value[e->at] != '*' && e->text.value[e->at] != '/')) return true;
        b32 divide = e->text.value[e->at++] == '/';
        f64 right = 0;
        if (!Prog__ExpressionAtom(e, &right) || (divide && right == 0)) return false;
        *value = divide ? *value / right : *value * right;
    }
}

function b32 Prog__ExpressionSum(Prog__Expression *e, f64 *value)
{
    if (!Prog__ExpressionTerm(e, value)) return false;
    for (;;) {
        Prog__ExpressionSkip(e);
        if (e->at >= e->text.length || (e->text.value[e->at] != '+' && e->text.value[e->at] != '-')) return true;
        b32 subtract = e->text.value[e->at++] == '-';
        f64 right = 0;
        if (!Prog__ExpressionTerm(e, &right)) return false;
        *value = subtract ? *value - right : *value + right;
    }
}

function b32 Prog_EvaluateExpression(String text, f64 *value)
{
    Prog__Expression e = { .text = text };
    f64 result = 0;
    if (text.length > 256 || !Prog__ExpressionSum(&e, &result)) return false;
    Prog__ExpressionSkip(&e);
    if (e.at != text.length || !isfinite(result)) return false;
    *value = result;
    return true;
}

// Starts typing in `option`'s slider, its value there selected to be typed over.
function void Prog_BeginSliderEdit(Prog_State *state, Prog_Option option)
{
    if (option >= Prog_Option_Count || Prog_Options[option].control != Prog_OptionControl_Slider) return;
    state->sliderOption = (u8)(option + 1);
    state->sliderInvalid = false;
    state->sliderEdit = (Prog_TextEdit){0};
    String text = StrFormat(&state->frameArena, "%d", Prog_OptionValue(state, option));
    Prog__EditReplace(&state->sliderEdit, (const u8 *)text.value, (u32)text.length);
    state->sliderEdit.anchor = 0;
}

// Stops typing in a slider; `keep` sets its option to what was typed, worked out and rounded, which fails, typing on, while it does not read.
function b32 Prog_EndSliderEdit(Prog_State *state, b32 keep)
{
    if (!state->sliderOption) return true;
    Prog_Option option = (Prog_Option)(state->sliderOption - 1);
    f64 value = 0;
    if (keep && !Prog_EvaluateExpression(Prog_EditText(&state->sliderEdit), &value)) {
        state->sliderInvalid = true;
        return false;
    }
    state->sliderOption = 0;
    state->sliderInvalid = false;
    if (keep) Prog_SetOption(state, option, (i32)floor(value + 0.5));
    return true;
}

// The box being typed in changes the color as soon as it reads as one: a number up to its channel's greatest, or six hex digits.
function void Prog_PickerTyped(Prog_State *state)
{
    String text = Prog_EditText(&state->pickerEdit);
    if (!text.length) return;
    u32 value = 0;
    for (u64 i = 0; i < text.length; ++i) {
        u8 c = (u8)text.value[i];
        value = value * (state->pickerField == 7 ? 16 : 10) + (u32)(c <= '9' ? c - '0' : c - 'A' + 10);
    }
    if (state->pickerField == 7) {
        if (text.length == 6) Prog_PickHex(state, value);
        return;
    }
    local_persist const u32 greatest[6] = { 360, 100, 100, 255, 255, 255 };
    u32 channel = state->pickerField - 1u;
    Prog_PickColor(state, channel, (f32)Min(value, greatest[channel]) / (f32)greatest[channel]);
}


// Sets an option that takes a name, such as a font's, to `text`; an empty one goes back to the default.
function void Prog_SetOptionText(Prog_State *state, Prog_Option option, String text)
{
    if (option >= Prog_Option_Count) return;
    const Prog_OptionDesc *desc = &Prog_Options[option];
    Scratch scratch = GetScratch();
    if (state->configDirectory.length) Prog__WriteSetting(state, desc->setting, text);
    else Prog_ApplySettings(state, StrFormat(scratch.arena, "%S = %S", desc->setting, text));
    ReleaseScratch(scratch);
}

function void Prog_SetOption(Prog_State *state, Prog_Option option, i32 value)
{
    if (option >= Prog_Option_Count) return;
    // The system's, not Settings.conf's.
    if (option == Prog_Option_LongPaths) {
        Prog_SetLongPaths(state, value != 0);
        return;
    }
    const Prog_OptionDesc *desc = &Prog_Options[option];
    Scratch scratch = GetScratch();
    u32 choiceCount = 0;
    const String *choices = Prog_OptionChoices(state, option, scratch.arena, &choiceCount);
    i32 least = choices ? 0 : desc->least, most = choices ? (i32)choiceCount - 1 : desc->most;
    value = Min(Max(value, least), most);
    String text = choices ? choices[value] : option == Prog_Option_FontSize ? StrFormat(scratch.arena, "%.4f", (f64)value / 9.0) : StrFormat(scratch.arena, "%d", value);
    if (state->configDirectory.length) {
        Prog__WriteSetting(state, desc->setting, text);
    } else {
        String line = StrFormat(scratch.arena, "%S = %S", desc->setting, text);
        Prog_ApplySettings(state, line);
    }
    ReleaseScratch(scratch);
    // What the folder lists, marks or measures changes with these, so it is listed again; applying the hidden files' does that itself.
    if (option == Prog_Option_ShowSystemFiles || option == Prog_Option_ShowRecents || option == Prog_Option_SortRecents ||
        (option >= Prog_Option_CalcFolderSizes && option <= Prog_Option_CalcFolderSizesNetwork)) {
        Prog_Reload(state);
    }
}

// What `option` is while Settings.conf leaves it out.
function i32 Prog__OptionDefault(Prog_Option option)
{
    switch (option) {
        case Prog_Option_FontSize: return 9;
        case Prog_Option_InspectorFontSize: return Prog_InspectorFontSize;
        case Prog_Option_InspectorLayout: return Prog_InspectorLayout_Auto;
        case Prog_Option_LightScheme: return Prog_LightScheme;
        case Prog_Option_Spacing: return 4;
        case Prog_Option_InactiveOpacity: return 60;
        case Prog_Option_LoadOnStartup: return Prog_Startup_LastSession;
        case Prog_Option_NewTabShow: return Prog_NewTab_Root;
        case Prog_Option_StoreRecents: return Prog_StoreRecents_Persistent;
        case Prog_Option_Notices: return Prog_NoticeKind_Info;
        case Prog_Option_CharacterKeyAction: return Prog_CharacterKeys_TypeInFilter;
        case Prog_Option_PopupBlur: return 24;
        case Prog_Option_CompressionLevel: return 9;
        case Prog_Option_VideoQuality: return 20;
        case Prog_Option_AudioVolume: return 100;
        case Prog_Option_NoticesInSidebar: return 1;
        case Prog_Option_CheckUpdates: return 1;
        case Prog_Option_Language: {
            for (u32 i = 1; i < CArrayCount(Prog_OptionLanguages); ++i) if (StrIsEqual(Prog_SystemLanguage(), Prog_OptionLanguages[i])) return (i32)i;
            return 0;
        }
        case Prog_Option_PopupOpacity: return (i32)floorf(Prog_DefaultPopupOpacity * 100 + 0.5f);
        case Prog_Option_WindowOpacity: return (i32)floorf(Prog_DefaultGlassOpacity * 100 + 0.5f);
        case Prog_Option_Backdrop: return PlatformBackdrop_Acrylic;
        case Prog_Option_LoadThumbnails: case Prog_Option_Animations: case Prog_Option_RoundCorners: case Prog_Option_ShowFileExtension: case Prog_Option_SystemScheme:
        case Prog_Option_ShortenedPaths: case Prog_Option_Breadcrumbs: case Prog_Option_ConfirmEmptyTrash: case Prog_Option_SingleWindow:
        case Prog_Option_VSync: case Prog_Option_HighlightCode: case Prog_Option_CalcFolderSizes: case Prog_Option_CalcFolderSizesSSD:
        case Prog_Option_ActOnPress: case Prog_Option_UnderlineOnHover: case Prog_Option_ClickToRename: case Prog_Option_Overscroll: return 1;
        default: return 0;
    }
}

// Whether `option` is at its default; the colors, which follow the scheme, and the muted notices always are.
function b32 Prog_OptionIsDefault(const Prog_State *state, Prog_Option option)
{
    if (option >= Prog_Option_Count || option == Prog_Option_LongPaths) return true;
    // A scheme not chosen yet, which follows the system's mode.
    if (option == Prog_Option_ColorScheme && state->systemScheme) return true;
    switch (Prog_Options[option].control) {
        case Prog_OptionControl_Font: return !(option == Prog_Option_InspectorFont ? state->inspectorFont : state->font).length;
        case Prog_OptionControl_Switch: case Prog_OptionControl_Stepper: case Prog_OptionControl_Slider: case Prog_OptionControl_Choice:
            return Prog_OptionValue(state, option) == Prog__OptionDefault(option);
        default: return true;
    }
}

function void Prog_RestoreOption(Prog_State *state, Prog_Option option)
{
    if (Prog_OptionIsDefault(state, option)) return;
    if (Prog_Options[option].control == Prog_OptionControl_Font) Prog_SetOptionText(state, option, (String){0});
    else Prog_SetOption(state, option, Prog__OptionDefault(option));
}

// Gives `kind` the hotkeys `hotkeys` in Settings.conf, in place of its defaults; without a config folder only for this run.
function void Prog_SetHotkeys(Prog_State *state, Prog_CommandKind kind, const Prog_Hotkey *hotkeys, u32 count)
{
    Scratch scratch = GetScratch();
    String name = StrFormat(scratch.arena, "hotkey.%S", Prog_Commands[kind].name), value = Prog_HotkeyListSetting(scratch.arena, hotkeys, count);
    if (state->configDirectory.length) Prog__WriteSetting(state, name, value);
    else Prog_ApplySettings(state, StrFormat(scratch.arena, "%S = %S", name, value));
    ReleaseScratch(scratch);
}

function void Prog_SetTransparent(Prog_State *state, b32 transparent)
{
    state->transparent = transparent;
    Prog__ApplyTheme(state);
}

typedef struct Prog_Metrics {
    f32 line;        // font line height
    f32 row;         // list rows and the column header
    f32 bar;         // tab strip and toolbar
    f32 pill;        // bottom filter and status pills
    f32 separator;
    f32 stroke;      // the outlines of panels, tabs, boxes and popups
    f32 sidebar;
    f32 inset;       // list rows to the list edges
    f32 gap;         // text padding inside cells and buttons
    f32 iconSlot;    // reserved for file icons
    f32 radius;
    f32 scale;
    const R_Font *font;
    const R_Font *detail;  // the details columns beside the names
    const R_Font *icons;
    const R_Font *iconsSmall;
    const R_Font *iconsMedium;
} Prog_Metrics;

function Prog_Metrics Prog__Metrics(const R_Font *font, const R_Font *icons, f32 scale)
{
    f32 line = font->lineHeight;
    return (Prog_Metrics){
        .font = font,
        .detail = font,
        .icons = icons,
        .iconsSmall = icons,
        .iconsMedium = icons,
        .line = line,
        .row = floorf(line * 1.6f + 0.5f),
        .bar = floorf(line * 2.1f + 0.5f),
        .pill = floorf(line * 2.0f + 0.5f),
        .separator = MaxF32(1.0f, floorf(1.6f * scale + 0.5f)),
        .stroke = 1.2f * scale,
        .sidebar = floorf(Prog_SidebarDefault * scale + 0.5f),
        .inset = floorf(15.0f * scale + 0.5f),
        .gap = floorf(8.0f * scale + 0.5f),
        .iconSlot = floorf(30.0f * scale + 0.5f),
        .radius = 3.0f * scale,
        .scale = scale,
    };
}

function f32 Prog_RowHeight(const R_Font *font)
{
    return floorf(font->lineHeight * 1.6f + 0.5f);
}

function f32 Prog__CaretWidth(const Prog_Metrics *m)
{
    return MaxF32(1.0f, floorf(1.6f * m->scale + 0.5f));
}

function UI_Block *Prog__Box(UI_Size width, UI_Size height, UI_Layout layout, R_Color background, UI_Flags flags)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(height);
    UI_SetNextChildrenLayout(layout);
    // Horizontal strips center their items vertically; vertical stacks start at the top.
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, layout == UI_Layout_Horizontal ? 0.5f : 0 });
    UI_SetNextColors((UI_Colors){ .background = background });
    return UI_AddBlock(UI_Flag_DrawBackground | flags);
}

function void Prog__Spacer(UI_Size width, UI_Size height)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(height);
    UI_AddBlock(0);
}

function void Prog__LabelPadded(String text, UI_Size width, f32 height, f32 left, f32 right, R_Color color, f32 alignX)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){ left, 0, right, 0 });
    UI_SetNextTextAlignment((UI_Vec2){ alignX, 0.5f });
    UI_SetNextColors((UI_Colors){ .text = color });
    UI_SetNextText(text);
    UI_AddBlock(UI_Flag_DrawText | UI_Flag_Clip);
}

// The fill of a box floating over the window, such as the palette: the background, or the menus' fill where a translucent theme leaves it clear,
// so that what is under the box does not show through.
// Asks for a frame at `at` without input, the earliest of those asked for in a frame.
function void Prog_WakeAt(Prog_State *state, u64 at)
{
    state->wakeAt = state->wakeAt ? Min(state->wakeAt, at) : at;
}

function R_Color Prog__FloatingFill(void)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    return theme->background.a < 1 ? theme->menu : theme->background;
}

// How far a floating box blurs what is under it: only where its fill lets that show through, as in the transparent theme.
function f32 Prog__FloatingBlur(const Prog_Metrics *m)
{
    return Prog_CurrentTheme()->menu.a < 1 ? floorf(Prog_GlassBlur * m->scale + 0.5f) : 0;
}

function void Prog__Label(String text, UI_Size width, f32 height, f32 padding, R_Color color, f32 alignX)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){ padding, 0, padding, 0 });
    UI_SetNextTextAlignment((UI_Vec2){ alignX, 0.5f });
    UI_SetNextColors((UI_Colors){ .text = color });
    UI_SetNextText(text);
    UI_AddBlock(UI_Flag_DrawText | UI_Flag_Clip);
}

// An icon at the smaller size, for the breadcrumbs' separators.
function void Prog__SmallIcon(String icon, f32 width, f32 height, R_Color color, f32 alignX, const Prog_Metrics *m)
{
    UI_SetNextFont(m->iconsSmall);
    Prog__Label(icon, width > 0 ? UI_Pixels(width) : UI_TextContent(), height, 0, color, alignX);
}

// The header's sort arrow in the middle of a `width` by `height` slot: a solid head on a short stem, pointing up or `down`, in whole pixels.
function void Prog__SortArrow(b32 down, f32 width, f32 height, R_Color color, const Prog_Metrics *m)
{
    f32 rows = MaxF32(2, floorf(3.2f * m->scale + 0.5f)), stem = MaxF32(1, floorf(1.6f * m->scale + 0.5f)), tail = floorf(4.8f * m->scale + 0.5f);
    f32 top = floorf((height - rows - tail) * 0.5f), middle = floorf(width * 0.5f);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_Block *slot = UI_AddBlock(0);
    UI_Parent(slot) {
        // The head widens a pixel on each side per row, from the stem's width at its point.
        for (f32 r = 0; r < rows; ++r) {
            f32 across = stem + 2 * r;
            UI_SetNextFloatingOffset((UI_Vec2){ middle - floorf(across * 0.5f), down ? top + tail + rows - 1 - r : top + r });
            UI_SetNextPrefWidth(UI_Pixels(across));
            UI_SetNextPrefHeight(UI_Pixels(1));
            UI_SetNextColors((UI_Colors){ .background = color });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
        UI_SetNextFloatingOffset((UI_Vec2){ middle - floorf(stem * 0.5f), down ? top : top + rows });
        UI_SetNextPrefWidth(UI_Pixels(stem));
        UI_SetNextPrefHeight(UI_Pixels(tail));
        UI_SetNextColors((UI_Colors){ .background = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
}

function void Prog__Icon(String icon, f32 width, f32 height, R_Color color, f32 alignX, const Prog_Metrics *m)
{
    UI_SetNextFont(m->icons);
    Prog__Label(icon, width > 0 ? UI_Pixels(width) : UI_TextContent(), height, 0, color, alignX);
}

// The search glass where Prog__Icon would put its glyph, `down` lower, drawn as a ring and a handle: the glyph's ring is off the center of
// its square, so that it lands unevenly on the pixels at most sizes, while this one's is a whole number of them across, at whole pixels.
function void Prog__SearchIcon(f32 width, f32 height, R_Color color, f32 alignX, f32 down, const Prog_Metrics *m)
{
    // The glyph's square is 24 units: its ring is 9 across from the center at 11, 11, its handle reaches 21, 21, and both are 2 thick.
    f32 em = R_TextWidth(m->icons, Icon_Search), unit = em / 24.0f, stroke = MaxF32(1, floorf(2 * unit + 0.5f));
    f32 slot = width > 0 ? width : em, ring = floorf(18 * unit + 0.5f);
    f32 x0 = floorf(alignX * (slot - em) + 11 * unit - ring * 0.5f + 0.5f), y0 = floorf((height - em) * 0.5f + down + 11 * unit - ring * 0.5f + 0.5f);
    f32 cx = x0 + ring * 0.5f, cy = y0 + ring * 0.5f, from = (ring - stroke) * 0.5f * 0.7071f, to = 10 * unit;
    UI_SetNextPrefWidth(UI_Pixels(slot));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_Parent(UI_AddBlock(0)) {
        UI_SetNextFloatingOffset((UI_Vec2){ x0, y0 });
        UI_SetNextPrefWidth(UI_Pixels(ring));
        UI_SetNextPrefHeight(UI_Pixels(ring));
        UI_SetNextCornerRadius(ring * 0.5f);
        UI_SetNextBorderThickness(stroke);
        UI_SetNextColors((UI_Colors){ .border = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(UI_Pixels(slot));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextLine((UI_Vec2){ cx + from, cy + from }, (UI_Vec2){ cx + to, cy + to }, stroke);
        UI_SetNextColors((UI_Colors){ .background = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawLine);
    }
}

// A button's wash of `wash`, the selection color for most: faint under the pointer and fading out after it, and solid while pressed,
// easing both ways. Its animations are kept only while there is something to show.
function R_Color Prog__ButtonWash(UI_Key key, b32 hot, b32 pressed, R_Color wash)
{
    UI_Key hoverKey = UI_KeyFromStr(key, S("Hover")), pressKey = UI_KeyFromStr(key, S("Press"));
    f32 hover = hot || pressed || UI_Kept(hoverKey) > 0.01f ? UI_Fall(hoverKey, hot || pressed ? 1.0f : 0.0f) : 0;
    f32 press = pressed || UI_Kept(pressKey) > 0.01f ? UI_Animate(pressKey, pressed ? 1.0f : 0.0f, 0) : 0;
    f32 amount = Prog_CurrentTheme()->hoverAmount.r;
    wash.a *= amount * hover + (1 - amount * hover) * press;
    return wash;
}

// Text button that tints its text when hot and greys out when disabled, washed in `washColor` under the pointer.
// A NULL font uses the default one.
function UI_Interaction Prog__WashedButton(String identity, String text, const R_Font *font, UI_Size width, f32 height, f32 padding,
                                           R_Color color, R_Color hotColor, R_Color disabledColor, R_Color washColor, b32 enabled, const Prog_Metrics *m)
{
    UI_Key key = UI_KeyFromStr(0, identity);
    b32 hot = enabled && UI->hotKey == key, pressed = enabled && UI->activeKey == key;
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){ padding, 0, padding, 0 });
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    R_Color wash = Prog__ButtonWash(key, hot, pressed, washColor);
    UI_SetNextCornerRadius(floorf(4.0f * m->scale + 0.5f));
    UI_SetNextColors((UI_Colors){ .background = wash, .text = !enabled ? disabledColor : hot || pressed ? hotColor : color });
    UI_SetNextText(text);
    if (font) UI_SetNextFont(font);
    UI_Block *block = UI_AddBlockFromKey(key, UI_Flag_DrawText | (wash.a > 0 ? UI_Flag_DrawBackground : 0) | (enabled ? UI_Flag_LeftMouseClickable : 0));
    UI_Interaction result = UI_InteractionFromBlock(block);
    if (!enabled) result.clicked = false;
    return result;
}

// Under the pointer, the same wash of the selection color as a hovered row.
function UI_Interaction Prog__TextButton(String identity, String text, const R_Font *font, UI_Size width, f32 height, f32 padding,
                                         R_Color color, R_Color hotColor, R_Color disabledColor, b32 enabled, const Prog_Metrics *m)
{
    return Prog__WashedButton(identity, text, font, width, height, padding, color, hotColor, disabledColor, Prog_CurrentTheme()->selection, enabled, m);
}

function f32 Prog__StripHeight(const Prog_Metrics *m) { return m->bar - floorf(0.8f * m->scale + 0.5f); }

// A strip along the window's top: a little shorter while the window is maximized, so that the tabs reach the top edge.
function f32 Prog__TitleStripHeight(const Prog_Metrics *m) { return Prog__StripHeight(m) - (PlatformWindowIsMaximized() ? floorf(1.6f * m->scale + 0.5f) : 0); }

// Toolbar buttons are a little wider than tall.
function f32 Prog__ToolbarButtonWidth(const Prog_Metrics *m) { return floorf(28.8f * m->scale + 0.5f); }
function f32 Prog__ToolbarButtonHeight(const Prog_Metrics *m) { return floorf(24.8f * m->scale + 0.5f); }

// The wheel's turn this frame over `block`, in whole notches and at least one, or 0 when it is not under the pointer; Ctrl+wheel zooms instead.
function i32 Prog__WheelNotches(const UI_Block *block)
{
    if (UI->hotKey != block->key || UI->wheel.y == 0 || (UI->mods & KeyMod_Control)) return 0;
    i32 notches = (i32)roundf(UI->wheel.y);
    return notches ? notches : UI->wheel.y > 0 ? 1 : -1;
}

// How far a popup has faded in under `key`: evenly over 135 ms.
function f32 Prog__FadeIn(UI_Key key)
{
    if (!UI->animate) return 1;
    f32 shown = MinF32(1, (f32)UI_TimeSince(key) / 135000.0f);
    if (shown < 1) UI_KeepAnimating();
    return shown;
}

// How far from its place a slide starts at most, whatever the distance.
function f32 Prog__SlideCap(const Prog_Metrics *m) { return floorf(30.0f * m->scale + 0.5f); }

// Notes the button just added as the one under the pointer, for Prog__Tooltip.
function void Prog__NoteTooltip(Prog_State *state, Prog_CommandKind kind, String label)
{
    UI_Block *button = UI->parent->last;
    if (!button || UI->hotKey != button->key || UI->activeKey) return;
    state->tooltipKey = button->key;
    state->tooltipRect = button->rect;
    state->tooltipKind = (u16)kind;
    state->tooltipLabel = label;
    // The history buttons say where they go.
    String to = {0};
    if (kind == Prog_CommandKind_Back) to = Prog_BackPath(state);
    if (kind == Prog_CommandKind_Forward && state->historyAt + 1 < state->history.header.count) to = state->history.v[state->historyAt + 1];
    if (kind == Prog_CommandKind_Up) to = Prog_UpPath(state->directory.path);
    state->tooltipDetail = to.length ? P_NativePath(&state->frameArena, to) : (String){0};
    state->tooltipDescribes = false;
    state->tooltipChip = (String){0};
}

// Gives the tip of the button just added, when it is under the pointer, `words` over its command `kind`, a line each; with kind None only the words.
function void Prog__DescribeTooltip(Prog_State *state, Prog_CommandKind kind, String words)
{
    Prog__NoteTooltip(state, kind, (String){0});
    UI_Block *button = UI->parent->last;
    if (!button || state->tooltipKey != button->key) return;
    state->tooltipDetail = words;
    state->tooltipDescribes = true;
}

// The same for a button inside a menu or the palette, whose tip shows over them.
function void Prog__PopupTooltip(Prog_State *state, String words)
{
    Prog__DescribeTooltip(state, Prog_CommandKind_None, words);
    UI_Block *button = UI->parent->last;
    if (button && state->tooltipKey == button->key) state->tooltipAbove = true;
}

// The same with `key`, which does what the button does, in a chip after the words.
function void Prog__PopupTooltipKey(Prog_State *state, String words, String key)
{
    Prog__PopupTooltip(state, words);
    UI_Block *button = UI->parent->last;
    if (button && state->tooltipKey == button->key) state->tooltipChip = key;
}

// The Type column header's button for the Types popup: a funnel, solid while some types are picked.
function void Prog__TypesFunnel(Prog_State *state, f32 width, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, S("Header/Types"));
    b32 solid = state->types.length != 0;
    R_Color color = UI->hotKey == key || (state->typesOpen && state->currentPanel == state->activePanel) ? theme->text : theme->icon;
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextFont(m->icons);
    UI_SetNextColors((UI_Colors){ .text = color });
    UI_SetNextText(Icon_Funnel);
    UI_Block *button = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | (solid ? 0 : UI_Flag_DrawText));
    UI_Interaction interaction = UI_InteractionFromBlock(button);
    if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_TypesPopupOpen, 0, 0, interaction.clickOrder);
    // The icons are strokes, so the solid one is drawn in whole pixels: a flat top that narrows a pixel a side per row to its stem,
    // and the stem's foot cut away from the right.
    if (solid) UI_Parent(button) {
        f32 s = m->scale, top = floorf(12.8f * s + 0.5f), stem = floorf(3.2f * s + 0.5f), stemRows = floorf(4.0f * s + 0.5f);
        if ((((i32)top - (i32)stem) & 1) != 0) top += 1;
        f32 narrowing = (top - stem) * 0.5f - 1, rows = 2 + narrowing + stemRows + stem - 1;
        f32 y = floorf((m->row - rows) * 0.5f), middle = floorf(width * 0.5f);
        for (f32 r = 0; r < rows; ++r, ++y) {
            f32 across = r < 2 ? top : r < 2 + narrowing ? top - 2 * (r - 1) : r < 2 + narrowing + stemRows ? stem : stem - (r - 1 - narrowing - stemRows);
            f32 left = middle - floorf((r < 2 + narrowing + stemRows ? across : stem) * 0.5f);
            UI_SetNextFloatingOffset((UI_Vec2){ left, y });
            UI_SetNextPrefWidth(UI_Pixels(across));
            UI_SetNextPrefHeight(UI_Pixels(1));
            UI_SetNextColors((UI_Colors){ .background = color });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
    }
    Prog__DescribeTooltip(state, Prog_CommandKind_TypesPopupOpen, Prog_Commands[Prog_CommandKind_TypesPopupOpen].tooltip);
}

function void Prog__ToolbarButton(Prog_State *state, String identity, String icon, Prog_CommandKind kind, b32 enabled, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Interaction interaction = Prog__TextButton(identity, icon, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                  theme->icon, theme->text, theme->iconDisabled, enabled, m);
    if (enabled) Prog__NoteTooltip(state, kind, (String){0});
    if (interaction.hot) state->hotToolbarCommand = (u16)kind;
    if (interaction.clicked) Prog_QueueCommand(state, kind, 0, 0, interaction.clickOrder);
}

// The close button of popups, cards and lists, one size and look everywhere: a toolbar button's, with the icon font's cross.
function UI_Interaction Prog__CloseButton(String identity, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    return Prog__TextButton(identity, Icon_X, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0, theme->icon, theme->text, theme->iconDisabled, true, m);
}

// Toolbar button that opens one of Filey's menus below it, lit in the selection color while that menu is open, `height` tall or the
// toolbar's at 0. A button in the window's right half lines its menu up with its right edge.
function void Prog__MenuButton(Prog_State *state, String identity, String icon, Prog_CommandKind kind, b32 enabled, f32 height, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    b32 tabs = kind == Prog_CommandKind_TabList;
    b32 lit = tabs ? state->paletteOpen && state->paletteMode == Prog_PaletteMode_Tabs : state->menuOpen && state->menuOpener == kind;
    UI_Key key = UI_KeyFromStr(0, identity);
    b32 hot = enabled && UI->hotKey == key, pressed = enabled && UI->activeKey == key;
    R_Color wash = Prog__ButtonWash(key, hot, pressed, theme->selection);
    // The tab list's and the main menu's, in the title bar, are rounder than the toolbar's, as the window buttons beside them, and the
    // main menu's wider.
    b32 main = kind == Prog_CommandKind_MainMenu;
    f32 width = main ? floorf(36.0f * m->scale + 0.5f) : Prog__ToolbarButtonWidth(m);
    height = height > 0 ? height : Prog__ToolbarButtonHeight(m);
    R_Color ink = !enabled ? theme->iconDisabled : hot || lit || pressed ? theme->text : tabs || main ? Prog__CaptionInk(theme) : theme->icon;
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextCornerRadius(floorf((tabs || main ? 5.6f : 4.0f) * m->scale + 0.5f));
    UI_SetNextColors((UI_Colors){ .background = lit ? theme->selection : wash, .text = ink });
    UI_SetNextText(icon);
    UI_SetNextFont(m->icons);
    UI_Block *block = UI_AddBlockFromKey(key, (main ? 0 : UI_Flag_DrawText) | (lit || wash.a > 0 ? UI_Flag_DrawBackground : 0) | (enabled ? UI_Flag_LeftMouseClickable : 0));
    // The main menu's three bars are shapes, since the icon font's strokes are too thin for them.
    if (main) {
        f32 barWidth = 12.0f * m->scale, bar = 2.0f * m->scale, pitch = 4.3f * m->scale;
        UI_Parent(block) {
            for (i32 i = -1; i <= 1; ++i) {
                UI_SetNextFloatingOffset((UI_Vec2){ (width - barWidth) * 0.5f, (height - bar) * 0.5f + (f32)i * pitch });
                UI_SetNextPrefWidth(UI_Pixels(barWidth));
                UI_SetNextPrefHeight(UI_Pixels(bar));
                UI_SetNextColors((UI_Colors){ .background = ink });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
            // A dot in the accent color over its corner while a newer version waits, which the menu offers.
            if (state->update.version.length) {
                f32 dot = floorf(6.4f * m->scale + 0.5f), inset = floorf(5.6f * m->scale + 0.5f);
                UI_SetNextFloatingOffset((UI_Vec2){ width - inset - dot, inset });
                UI_SetNextPrefWidth(UI_Pixels(dot));
                UI_SetNextPrefHeight(UI_Pixels(dot));
                UI_SetNextCornerRadius(dot * 0.5f);
                UI_SetNextColors((UI_Colors){ .background = theme->accent });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        }
    }
    // These open menus rather than run commands, so they name themselves; the tab list runs Search Tabs.
    String tip = kind == Prog_CommandKind_PanelMenu ? Tr("Panel Menu") : kind == Prog_CommandKind_MainMenu ? Tr("Main Menu")
               : kind == Prog_CommandKind_HistoryMenu ? Tr("History") : tabs ? (String){0} : Tr("View");
    if (enabled && !lit) Prog__NoteTooltip(state, kind, tip);
    UI_Interaction interaction = UI_InteractionFromBlock(block);
    R_Rect rect = block->rect;
    UI_Vec2 at = { rect.x0, rect.y1 + floorf(4.0f * m->scale + 0.5f) };
    // The main menu hangs just under the title bar, also when its hotkey opens it.
    if (main) {
        at.y = UI->root->rect.y0 + m->separator + Prog__TitleStripHeight(m) + floorf((PlatformWindowIsMaximized() ? 3.2f : 2.4f) * m->scale + 0.5f);
        state->mainMenuAt = at;
    }
    if (enabled && interaction.clicked) {
        Prog_Command *command = Prog_QueueCommand(state, kind, 0, 0, interaction.clickOrder);
        command->at = at;
        if (rect.x0 > (UI->root->rect.x0 + UI->root->rect.x1) * 0.5f) command->atRight = rect.x1;
    }
}

// Icon button inside a bottom pill; `on` shows a toggle that is set.
function void Prog__PillButton(Prog_State *state, String identity, String icon, Prog_CommandKind kind, b32 on, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Interaction interaction = Prog__TextButton(identity, icon, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                  on ? theme->text : theme->icon, theme->text, theme->iconDisabled, true, m);
    Prog__NoteTooltip(state, kind, (String){0});
    if (interaction.clicked) Prog_QueueCommand(state, kind, 0, 0, interaction.clickOrder);
}

// The expand button's glyph, two folders each with a dash at its side; no icon in the font has its shape.
function void Prog__ExpandButton(Prog_State *state, b32 on, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 width = Prog__ToolbarButtonWidth(m);
    Prog__PillButton(state, S("FilterExpand"), S(""), Prog_CommandKind_ToggleExpandFolder, on, m);
    UI_Block *button = UI->parent->last;
    // Its pixels at 125%: "#" is ink, and "+" and "-" the softer pixels of its smoothed edges. Expanded, the second folder hangs off the
    // first, as in a tree; the pointer over it, not its state, lights it.
    local_persist const char *const flat[] = {
        ".......+++++-.......",
        "......+######-......",
        "......+#+--+#######.",
        "......+#+...-######-",
        "-++++-+#+........+#-",
        "######+#+........+#-",
        "-++++-+#+........+#-",
        "......+#+........+#-",
        "......+############-",
        ".......++++++++++++.",
        "....................",
        ".......+++++-.......",
        "......+######-......",
        "......+#+..-#######-",
        "......+#+...-++++##-",
        "-++++-+#+........+#-",
        "######+#+........+#-",
        ".----.+#+........+#-",
        "......+#+--------+#-",
        "......+############-",
        ".......++++++++++++.",
    };
    local_persist const char *const tree[] = {
        ".....-++++-..........",
        ".....######+.........",
        ".....#+---#######+...",
        ".....#+...-######+...",
        "-++-.#+........-#+...",
        "####-#+........-#+...",
        "-++-.#+........-#+...",
        ".....#+........-#+...",
        ".....############+...",
        ".....++++++++++++-...",
        ".....................",
        ".+-....-+++++........",
        "-#+....+######.......",
        "-#+....+#-..+#######.",
        "-#+....+#-...-++++##.",
        "-#++++-+#-........+#.",
        "-#####++#-........+#.",
        "..----.+#-........+#.",
        ".......+#---------+#.",
        ".......+############.",
        "........+++++++++++-.",
    };
    static_assert(CArrayCount(flat) == CArrayCount(tree));
    const char *const *pixels = on ? tree : flat;
    f32 unit = m->scale / 1.25f;
    R_Color ink = UI->hotKey == button->key ? theme->text : theme->icon;
    // On whole pixels from where the button and the filter's pill were laid out last, since they can sit between them: a pixel right of the
    // button's middle and a pixel above the pill's, which keeps it in place while the button moves as the filter opens, as there.
    f32 x = floorf(button->rect.x0 + (width - 20 * unit) * 0.5f + unit) - button->rect.x0;
    f32 y = floorf(state->filterRect.y0 + (m->pill - 21 * unit) * 0.5f - unit + 0.5f) - button->rect.y0;
    UI_Parent(button) {
        for (u32 row = 0; row < CArrayCount(flat); ++row) {
            for (u32 at = 0; pixels[row][at];) {
                char level = pixels[row][at];
                u32 from = at;
                while (pixels[row][at] == level) ++at;
                if (level == '.') continue;
                f32 left = floorf((f32)from * unit + 0.5f), top = floorf((f32)row * unit + 0.5f);
                R_Color color = ink;
                color.a *= level == '#' ? 1.0f : level == '+' ? 0.45f : 0.2f;
                UI_SetNextFloatingOffset((UI_Vec2){ x + left, y + top });
                UI_SetNextPrefWidth(UI_Pixels(MaxF32(1, floorf((f32)at * unit + 0.5f) - left)));
                UI_SetNextPrefHeight(UI_Pixels(MaxF32(1, floorf((f32)(row + 1) * unit + 0.5f) - top)));
                UI_SetNextColors((UI_Colors){ .background = color });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        }
    }
}

typedef enum Prog_WindowButton {
    Prog_WindowButton_Minimize,
    Prog_WindowButton_Maximize,
    Prog_WindowButton_Restore,
    Prog_WindowButton_Close,
} Prog_WindowButton;

// A window button at the strip's right end, taking presses over its full height and washed under the pointer like the other buttons, in a
// box `washHeight` tall from `washTop`, as round as the tab list's beside it; the close button in the danger color.
// The glyphs are thin shapes the size of the system's caption glyphs, which the icon font's strokes are too heavy for; only the close cross comes from the font.
function void Prog__WindowButton(Prog_State *state, String identity, Prog_WindowButton kind, Prog_CommandKind command, f32 width, f32 height, f32 washTop, f32 washHeight,
                                 f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, identity);
    b32 hot = UI->hotKey == key;
    b32 close = kind == Prog_WindowButton_Close;
    R_Color background = close ? theme->danger : theme->selection;
    background.a *= theme->hoverAmount.r;
    R_Color ink = hot ? theme->text : Prog__CaptionInk(theme);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_Block *button = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable);
    UI_Interaction interaction = UI_InteractionFromBlock(button);
    if (interaction.clicked) Prog_QueueCommand(state, command, 0, 0, interaction.clickOrder);
    // Apart from its neighbors' washes by a gap.
    f32 inset = floorf(2.4f * scale + 0.5f);
    width -= inset * 2;
    height = washHeight;
    UI_Block *face = NULL;
    UI_Parent(button) {
        UI_SetNextFloatingOffset((UI_Vec2){ inset, washTop });
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_SetNextCornerRadius(floorf(5.6f * scale + 0.5f));
        UI_SetNextColors((UI_Colors){ .background = background, .text = ink });
        if (close) {
            UI_SetNextFont(m->icons);
            UI_SetNextText(Icon_X);
        }
        face = UI_AddBlock(UI_Flag_Floating | (close ? UI_Flag_DrawText : 0) | (hot ? UI_Flag_DrawBackground : 0));
    }
    // The glyphs sit in the middle of the button's wash, their boxes on whole pixels. Their strokes are a little over a pixel at 100%,
    // and smoothed where they end between pixels.
    f32 glyph = floorf(9.6f * scale + 0.5f);
    f32 stroke = 1.2f * scale;
    f32 gx = ceilf((width - glyph) * 0.5f), gy = floorf((height - glyph) * 0.5f);
    UI_Parent(face) {
        if (kind == Prog_WindowButton_Minimize) {
            UI_SetNextFloatingOffset((UI_Vec2){ gx, (height - stroke) * 0.5f });
            UI_SetNextPrefWidth(UI_Pixels(glyph));
            UI_SetNextPrefHeight(UI_Pixels(stroke));
            UI_SetNextColors((UI_Colors){ .background = ink });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        } else if (kind == Prog_WindowButton_Maximize) {
            UI_SetNextFloatingOffset((UI_Vec2){ gx, gy });
            UI_SetNextPrefWidth(UI_Pixels(glyph));
            UI_SetNextPrefHeight(UI_Pixels(glyph + 1));
            UI_SetNextBorderThickness(stroke);
            UI_SetNextColors((UI_Colors){ .border = ink });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
        } else if (kind == Prog_WindowButton_Restore) {
            // A square in front of another one, of which only the top and right edges show; drawn as lines, since covering it would show through a translucent theme.
            f32 offset = floorf(2.0f * scale + 0.5f);
            f32 size = glyph - offset;
            UI_SetNextFloatingOffset((UI_Vec2){ gx, gy });
            UI_SetNextPrefWidth(UI_Pixels(glyph));
            UI_SetNextPrefHeight(UI_Pixels(glyph));
            UI_Block *squares = UI_AddBlock(UI_Flag_Floating);
            UI_Parent(squares) {
                R_Rect edges[] = { { offset, 0, glyph, stroke }, { glyph - stroke, 0, glyph, size } };
                for (u32 i = 0; i < CArrayCount(edges); ++i) {
                    UI_SetNextFloatingOffset((UI_Vec2){ edges[i].x0, edges[i].y0 });
                    UI_SetNextPrefWidth(UI_Pixels(edges[i].x1 - edges[i].x0));
                    UI_SetNextPrefHeight(UI_Pixels(edges[i].y1 - edges[i].y0));
                    UI_SetNextColors((UI_Colors){ .background = ink });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                }
                UI_SetNextFloatingOffset((UI_Vec2){ 0, offset });
                UI_SetNextPrefWidth(UI_Pixels(size));
                UI_SetNextPrefHeight(UI_Pixels(size));
                UI_SetNextBorderThickness(stroke);
                UI_SetNextColors((UI_Colors){ .border = ink });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
            }
        }
    }
}

function UI_Key Prog__TabKey(Prog_State *state, u32 slot)
{
    return UI_KeyFromStr(0, StrFormat(&state->frameArena, "Tab/%" PRIu64, state->tabs[slot].id));
}

// Under the pointer, itself or its close button; false past either end of the strip.
function b32 Prog__TabHot(Prog_State *state, i64 slot)
{
    if (slot < 0 || slot >= state->tabCount) return false;
    UI_Key key = Prog__TabKey(state, (u32)slot);
    return UI->hotKey == key || UI->hotKey == UI_KeyFromStr(key, S("Close"));
}

// The tab strip's and the toolbar's height, a pixel under the bar's; the title bar's strip has the window's top border over it too.

// The strip's line under it from `x0` to `x1`, which the shown tab opens into the toolbar; it is as thick as the sidebar's edge it meets.
function void Prog__StripLine(f32 x0, f32 x1, f32 y, const Prog_Metrics *m)
{
    if (x1 <= x0) return;
    UI_SetNextFloatingOffset((UI_Vec2){ x0, y - m->separator });
    UI_SetNextPrefWidth(UI_Pixels(x1 - x0));
    UI_SetNextPrefHeight(UI_Pixels(m->separator));
    UI_SetNextColors((UI_Colors){ .background = Prog_CurrentTheme()->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
}

// Empty title bar space, under everything else on it, which moves the window and maximizes it on a double click.
function void Prog__Caption(Prog_State *state, String identity, f32 width, f32 height, f32 y)
{
    UI_SetNextFloatingOffset((UI_Vec2){ 0, y });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_Interaction caption = UI_InteractionFromBlock(UI_AddBlockFromKey(UI_KeyFromStr(0, identity), UI_Flag_Floating | UI_Flag_LeftMouseClickable));
    if (caption.pressed && caption.button == MouseButton_Left) {
        Prog_Command *command = Prog_QueueCommand(state, caption.pressCount == 2 ? Prog_CommandKind_WindowToggleMaximize : Prog_CommandKind_WindowMove, 0, 0, caption.pressOrder);
        command->panel = 0;
    }
}

function void Prog__SidebarToggle(Prog_State *state, f32 button, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Interaction toggle = Prog__TextButton(S("ToggleQuickAccess"), Icon_PanelLeft, m->icons, UI_Pixels(button), button, 0, Prog__CaptionInk(theme), theme->text, theme->iconDisabled, true, m);
    if (toggle.clicked) Prog_QueueCommand(state, Prog_CommandKind_ToggleQuickAccess, 0, 0, toggle.clickOrder)->panel = 0;
}

// Where a dragged tab would go in a strip, at the floating offset set before: a tab-shaped slot washed in the selection color and outlined with it.
function void Prog__TabPlaceholder(f32 width, f32 height, f32 radius, f32 stroke, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_SetNextPrefWidth(UI_Pixels(width + m->separator));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){0});
    UI_Block *slot = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
    UI_Parent(slot) {
        R_Color fill = theme->selection;
        fill.a *= theme->hoverAmount.r;
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height + radius));
        UI_SetNextCornerRadius(radius);
        UI_SetNextBorderThickness(stroke);
        UI_SetNextColors((UI_Colors){ .background = fill, .border = theme->selection });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
    }
}

// A panel's tab strip `width` wide: one tab per slot, the shown one filled like the toolbar it joins, the others parted by faint lines.
// Tabs share the strip's width up to a fixed size, and dragging one moves it between the others.
// In the title bar (`caption`) the strip's empty space moves the window; `lead` puts the sidebar button first while the sidebar is hidden, and `controls` the window buttons last.
// The tab's close cross, centered in the block being built, `width` by `height`: two crisp diagonal strokes drawn a pixel at a time, each row two
// solid pixels with a faint one on either side, rather than the icon font's softer glyph.
function void Prog__CloseCross(f32 width, f32 height, R_Color color, const Prog_Metrics *m)
{
    f32 s = m->scale, size = floorf(8.0f * s + 0.5f), half = 0.628f * s, reach = 5.6f * s;
    i32 columns = (i32)size, rows = (i32)size + 1;
    f32 left = floorf((width - size) * 0.5f), top = floorf((height - (f32)rows) * 0.5f), cx = size * 0.5f, cy = (f32)rows * 0.5f;
    for (i32 y = 0; y < rows; ++y) {
        for (i32 x = 0; x < columns; ++x) {
            f32 px = (f32)x + 0.5f - cx, py = (f32)y + 0.5f - cy, ink = 0;
            // Each stroke by its distance across from the pixel's middle, faded where it ends.
            for (u32 stroke = 0; stroke < 2; ++stroke) {
                f32 across = fabsf(stroke ? px + py : px - py) * 0.70710678f, along = fabsf(stroke ? px - py : px + py) * 0.70710678f;
                ink += UI__Clamp((half - across) * 1.16f + 0.5f, 0, 1) * UI__Clamp(reach - along, 0, 1);
            }
            if (ink < 0.04f) continue;
            R_Color shade = color;
            shade.a *= MinF32(ink, 1);
            UI_SetNextFloatingOffset((UI_Vec2){ left + (f32)x, top + (f32)y });
            UI_SetNextPrefWidth(UI_Pixels(1));
            UI_SetNextPrefHeight(UI_Pixels(1));
            UI_SetNextColors((UI_Colors){ .background = shade });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
    }
}

function void Prog__IconRect(f32 x, f32 y, f32 width, f32 height, R_Color color, f32 border, f32 radius);

function void Prog__TabStrip(Prog_State *state, f32 width, b32 caption, b32 lead, b32 controls, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog__EnsureTab(state);
    b32 flush = caption && PlatformWindowIsMaximized();
    f32 bar = caption ? Prog__TitleStripHeight(m) : Prog__StripHeight(m);
    f32 top = flush ? 0 : floorf(m->bar * 0.15f) - floorf(0.8f * scale + 0.5f);
    UI_SetNextChildrenPadding((UI_Padding){ 0, top, 0, 0 });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(bar));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    // Children start at the top, so that tabs taller than the strip hang below it.
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0 });
    UI_SetNextColors((UI_Colors){ .background = Prog__CaptionColor(state) });
    // Keyed, so that its last layout tells whether a dragged tab is over it.
    UI_Block *strip = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TabStrip")), UI_Flag_DrawBackground);
    UI_Parent(strip) {
        if (caption) Prog__Caption(state, NoTr("Caption"), width, bar, -top);
        // The tab's outline, a little thinner than the strip's line, runs a pixel in from its sides.
        f32 stroke = m->stroke, edge = floorf(0.8f * scale + 0.5f);
        f32 button = floorf(m->bar * 0.75f);
        f32 leadWidth = lead ? button + m->inset : floorf(6.0f * scale + 0.5f);
        UI_SetNextPrefWidth(UI_Pixels(leadWidth));
        UI_SetNextPrefHeight(UI_Pixels(bar - top));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
        // Centered on the whole strip rather than on the tabs.
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, 0, top });
        UI_Block *leading = UI_AddBlock(0);
        if (lead) UI_Parent(leading) Prog__SidebarToggle(state, button, m);
        controls = controls && PlatformWindowHasButtons();
        f32 controlWidth = floorf(40.8f * scale + 0.5f);
        f32 plus = floorf(m->bar * 0.9f);
        f32 room = MaxF32(0, width - leadWidth - plus - button - m->inset - (controls ? controlWidth * 3 : 0));
        // A tab dragged over the strip goes into the slot nearest the pointer, which a placeholder marks unless it would stay where it is.
        u64 panelId = Prog__PanelId(state);
        // Another window's tab makes room the same way, where that window says the pointer is.
        b32 foreign = !state->tabDrag && state->foreignDrag;
        UI_Vec2 pointer = foreign ? state->foreignPoint : UI->mouse;
        b32 dropHere = (state->tabDrag || foreign) && UI__Contains(strip->rect, pointer);
        f32 slotGuess = floorf(MinF32(m->line * 12.25f, room / (f32)(state->tabCount + 1)));
        u32 dropSlot = dropHere && slotGuess > 0 ? (u32)UI__Clamp(floorf((pointer.x - strip->rect.x0 - leadWidth) / slotGuess + 0.5f), 0, (f32)state->tabCount) : 0;
        b32 stays = !foreign && state->tabDrop.fromPanel == panelId && (dropSlot == state->tabDrop.fromSlot || dropSlot == state->tabDrop.fromSlot + 1);
        b32 placeholder = dropHere && !stays;
        if (dropHere && foreign) {
            state->foreignOver = true;
            state->foreignPanel = panelId;
            state->foreignSlot = dropSlot;
        } else if (dropHere) {
            state->tabDrop.kind = Prog_TabDrop_Strip;
            state->tabDrop.panel = panelId;
            state->tabDrop.slot = dropSlot;
        }
        UI_Key stripKey = UI_KeyFromStr(0, StrFormat(&state->frameArena, "TabStrip/%" PRIu64, panelId));
        f32 slots = (f32)(state->tabCount + (placeholder ? 1 : 0));
        f32 slide = Prog__SlideCap(m);
        f32 slot = floorf(UI_Slide(UI_KeyFromStr(stripKey, S("Slot")), MinF32(m->line * 12.25f, room / slots), slide));
        f32 slotSlide = slide / MaxF32(slot, 1);
        f32 tabWidth = MaxF32(0, slot - m->separator);
        f32 height = bar - top;
        // The shown tab is cut off at the strip's bottom, with its lower corners and border past it.
        f32 tabRadius = floorf(9.6f * scale + 0.5f);
        f32 hang = tabRadius + stroke + 1;
        f32 flare = floorf(m->radius - 0.5f) + stroke;
        // Tabs sit at eased slot positions, so that they slide aside for a dragged tab and close up after one leaves.
        UI_SetNextPrefWidth(UI_Pixels(floorf(UI_Slide(UI_KeyFromStr(stripKey, S("Slots")), slots, slotSlide) * slot)));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_Block *tabs = UI_AddBlock(0);
        f32 shownX = -1;
        UI_Parent(tabs) for (u32 i = 0; i <= state->tabCount; ++i) {
            if (placeholder && i == dropSlot) {
                f32 at = UI_Slide(UI_KeyFromStr(stripKey, S("Placeholder")), (f32)dropSlot, slotSlide);
                UI_SetNextFloatingOffset((UI_Vec2){ floorf(at * slot), 0 });
                Prog__TabPlaceholder(tabWidth, height, tabRadius, stroke, m);
            }
            if (i == state->tabCount) break;
            b32 shown = i == state->activeTab;
            String path = Prog_TabPath(state, i);
            String name = Prog_PathName(state, path);
            if (shown ? state->expanded : state->tabs[i].expanded) name = StrFormat(&state->frameArena, "%S [...]", name);
            UI_Key key = Prog__TabKey(state, i);
            UI_Key closeKey = UI_KeyFromStr(key, S("Close"));
            b32 hot = Prog__TabHot(state, i);
            f32 at = (f32)(i + (placeholder && i >= dropSlot ? 1 : 0));
            f32 x = floorf(UI_Slide(UI_KeyFromStr(key, S("Slot")), at, slotSlide) * slot);
            if (shown) shownX = x;
            UI_SetNextFloatingOffset((UI_Vec2){ x, 0 });
            UI_SetNextPrefWidth(UI_Pixels(tabWidth));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            f32 tabInset = floorf(14.0f * scale);
            // The tab's contents sit a little below its middle.
            f32 tabTop = floorf(0.8f * scale + 0.5f);
            UI_SetNextChildrenPadding((UI_Padding){ tabInset, tabTop, floorf(5.6f * scale + 0.5f), 0 });
            UI_Block *tab = UI_AddBlockFromKey(key, UI_Flag_Floating | UI_Flag_MouseClickable);
            UI_Interaction interaction = UI_InteractionFromBlock(tab);
            if (interaction.pressed && interaction.button != MouseButton_Middle) Prog_QueueCommand(state, Prog_CommandKind_SelectTab, i, 0, interaction.pressOrder);
            if (interaction.clicked && interaction.button == MouseButton_Middle) Prog_QueueCommand(state, Prog_CommandKind_CloseTabAt, i, 0, interaction.clickOrder);
            // Its menu hangs under it, from a right click or, for the shown tab of the active panel, its hotkey.
            UI_Vec2 menuAt = { tab->rect.x0, tab->rect.y1 + floorf(4.0f * scale + 0.5f) };
            if (shown && state->currentPanel == state->activePanel) state->tabMenuAt = menuAt;
            if (interaction.clicked && interaction.button == MouseButton_Right) Prog_QueueCommand(state, Prog_CommandKind_TabMenu, 0, 0, interaction.clickOrder)->at = menuAt;
            b32 lit = shown && state->menuOpen && state->menuOpener == Prog_CommandKind_TabMenu && state->currentPanel == state->activePanel;
            if (interaction.active && interaction.isDragging && interaction.button == MouseButton_Left && !state->tabDrag && !state->tabDragCancelled) {
                state->tabDrag = true;
                state->tabDragKey = key;
                state->tabDrop = (Prog_TabDropTarget){ .fromPanel = panelId, .fromSlot = i };
            }
            UI_Parent(tab) {
                if (shown || hot) {
                    // Floating offsets start inside the padding.
                    // A hovered tab has the shown tab's shape cut off square above the strip's bottom line.
                    // Under a clear shown tab nothing covers its sides where they curve out, so they stop where the curves start; a side that stays
                    // straight, next to a hovered tab, is drawn on down by itself.
                    b32 clear = shown && theme->background.a <= 0;
                    UI_SetNextFloatingOffset((UI_Vec2){ -tabInset, -tabTop });
                    UI_SetNextPrefWidth(UI_Pixels(tabWidth));
                    UI_SetNextPrefHeight(UI_Pixels(clear ? height - flare : shown ? height : height - stroke));
                    UI_Block *outline = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
                    for (u32 side = 0; clear && side < 2; ++side) {
                        if (!Prog__TabHot(state, side ? (i64)i + 1 : (i64)i - 1)) continue;
                        UI_SetNextFloatingOffset((UI_Vec2){ side ? tabWidth - tabInset - stroke : -tabInset, height - flare - tabTop });
                        UI_SetNextPrefWidth(UI_Pixels(stroke));
                        UI_SetNextPrefHeight(UI_Pixels(flare));
                        UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    }
                    UI_Parent(outline) {
                        UI_SetNextFloatingOffset((UI_Vec2){ edge, 0 });
                        UI_SetNextPrefWidth(UI_Pixels(tabWidth - 2 * edge));
                        UI_SetNextPrefHeight(UI_Pixels(height + hang));
                        UI_SetNextCornerRadius(tabRadius);
                        UI_SetNextBorderThickness(stroke);
                        // The shown tab is not washed, which also keeps its curved sides the same color as it.
                        b32 washed = hot && !shown && UI->hotKey != closeKey;
                        UI_SetNextColors((UI_Colors){ .background = lit ? theme->selection : shown || washed ? theme->background : (R_Color){0}, .border = theme->tabBorder });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                        // Under the pointer another tab is washed in the selection color, unless the pointer is on its close button.
                        if (washed) {
                            R_Color wash = theme->selection;
                            wash.a *= theme->hoverAmount.r;
                            UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                            UI_SetNextPrefWidth(UI_Pixels(tabWidth));
                            UI_SetNextPrefHeight(UI_Pixels(height + hang));
                            UI_SetNextCornerRadius(tabRadius);
                            UI_SetNextColors((UI_Colors){ .background = wash });
                            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                        }
                    }
                }
                // The shown tab's sides curve out into the strip's bottom line: a square of the tab's color with a quarter of a strip-colored circle cut from it.
                // Each square also covers the tab's own border columns, which the circle's border takes over.
                // Next to a hovered tab the side stays straight, so that the curve does not cut into the hovered tab's corner.
                if (shown) {
                    for (u32 side = 0; side < 2; ++side) {
                        if (Prog__TabHot(state, side ? (i64)i + 1 : (i64)i - 1)) continue;
                        f32 cover = ceilf(stroke) + edge;
                        UI_SetNextFloatingOffset((UI_Vec2){ side ? tabWidth - tabInset - cover : -tabInset - flare, height - flare - tabTop });
                        UI_SetNextPrefWidth(UI_Pixels(flare + cover));
                        UI_SetNextPrefHeight(UI_Pixels(flare));
                        UI_SetNextColors((UI_Colors){ .background = lit ? theme->selection : theme->background });
                        UI_Block *corner = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip);
                        UI_Parent(corner) {
                            UI_SetNextFloatingOffset((UI_Vec2){ side ? cover - ceilf(stroke) : stroke - flare, -flare });
                            UI_SetNextPrefWidth(UI_Pixels(flare * 2));
                            UI_SetNextPrefHeight(UI_Pixels(flare * 2));
                            UI_SetNextCornerRadius(flare);
                            UI_SetNextBorderThickness(stroke);
                            // Under a clear tab there is nothing to cut away, and the strip's color drawn again would show.
                            UI_SetNextColors((UI_Colors){ .background = theme->background.a > 0 ? Prog__CaptionColor(state) : (R_Color){0}, .border = theme->tabBorder });
                            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                        }
                    }
                }
                // A spinner stands in for the folder while the shown tab's listing is on its way, or while its drive spins up a disc; for a
                // refresh, as while a download writes into the folder, only once it has taken a second.
                const P_Place *drive = Prog_PathDrive(state, path);
                u64 listing = shown && state->directoryJob ? UI->nowMicroseconds - Min(state->listStarted, UI->nowMicroseconds) : 0;
                b32 slow = shown && state->directoryJob && (state->loading || listing >= 1000000);
                if (shown && state->directoryJob && !slow) Prog_WakeAt(state, state->listStarted + 1000000);
                const Prog_Icon *spinner = slow || (drive && drive->busy) ? Prog_SpinnerIcon(state, UI->nowMicroseconds) : NULL;
                if (spinner) UI_KeepAnimating();
                const Prog_Icon *icon = spinner ? spinner : path.length ? Prog_FolderIcon(state, path) : NULL;
                if (icon) {
                    UI_SetNextPrefWidth(UI_Pixels((f32)state->iconSize + floorf(4.8f * scale + 0.5f)));
                    UI_SetNextPrefHeight(UI_Pixels(height));
                    UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
                    UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, spinner ? theme->icon : (R_Color){ 1, 1, 1, 1 });
                    UI_AddBlock(UI_Flag_DrawImage);
                } else {
                    Prog__Icon(StrIsEqual(path, Prog_ThisPC) ? Icon_Monitor : Icon_Folder, (f32)state->iconSize + floorf(4.8f * scale + 0.5f), height, theme->icon, 0, m);
                }
                Prog__Label(name, UI_Flex(), height, 0, theme->text, 0);
                // The close button shows on the shown and the hovered tab.
                if (shown || hot) {
                    // Washed under the pointer like the toolbar's buttons, and as wide.
                    b32 closeHot = UI->hotKey == closeKey;
                    R_Color wash = theme->selection;
                    wash.a *= theme->hoverAmount.r;
                    f32 closeWidth = Prog__ToolbarButtonWidth(m), closeHeight = floorf(23.2f * scale + 0.5f);
                    UI_SetNextPrefWidth(UI_Pixels(closeWidth));
                    UI_SetNextPrefHeight(UI_Pixels(closeHeight));
                    UI_SetNextCornerRadius(floorf(5.6f * scale + 0.5f));
                    UI_SetNextColors((UI_Colors){ .background = wash });
                    UI_Block *close = UI_AddBlockFromKey(closeKey, UI_Flag_LeftMouseClickable | (closeHot ? UI_Flag_DrawBackground : 0));
                    UI_Interaction closing = UI_InteractionFromBlock(close);
                    if (closing.clicked) Prog_QueueCommand(state, Prog_CommandKind_CloseTabAt, i, 0, closing.clickOrder);
                    UI_Parent(close) Prog__CloseCross(closeWidth, closeHeight, closeHot ? theme->text : theme->icon, m);
                }
            }
            // Lines part the tabs that are neither shown nor hovered, centered on the tabs.
            b32 nextLit = i + 1 == state->activeTab || Prog__TabHot(state, (i64)i + 1);
            if (!shown && !hot && !nextLit) {
                f32 part = floorf(height * 0.62f);
                UI_SetNextFloatingOffset((UI_Vec2){ x + tabWidth, floorf((height - part) * 0.5f) });
                UI_SetNextPrefWidth(UI_Pixels(m->separator));
                UI_SetNextPrefHeight(UI_Pixels(part));
                UI_SetNextColors((UI_Colors){ .background = theme->alternateRow });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        }
        // Not under the shown tab and its curved sides, which would show it through a translucent theme.
        f32 lineY = bar - top;
        if (shownX < 0) {
            Prog__StripLine(0, width, lineY, m);
        } else {
            // Under a clear tab nothing covers where the line and a curve meet, so the line stops where the curve's own stroke starts.
            b32 leftFlare = !Prog__TabHot(state, (i64)state->activeTab - 1), rightFlare = !Prog__TabHot(state, (i64)state->activeTab + 1);
            f32 overlap = theme->background.a > 0 ? stroke : 0;
            Prog__StripLine(0, leadWidth + shownX + (leftFlare ? overlap - flare : 0), lineY, m);
            Prog__StripLine(leadWidth + shownX + tabWidth + (rightFlare ? flare - overlap : 0), width, lineY, m);
        }
        Prog__Spacer(UI_Pixels(floorf(4.0f * scale + 0.5f)), UI_Pixels(height));
        // The buttons' washes past the tabs are a tab's close button's height and level with it, clear of the strip's line.
        f32 wash = floorf(23.2f * scale + 0.5f), washGap = floorf((height - wash) * 0.5f + 0.5f), washTop = top + washGap;
        UI_SetNextPrefWidth(UI_Pixels(plus));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0 });
        UI_SetNextChildrenPadding((UI_Padding){ 0, washGap, 0, 0 });
        UI_Block *plusBox = UI_AddBlock(0);
        UI_Parent(plusBox) {
            // As tall as a tab's close button and level with it; an even margin either side keeps it, and the bars in it, on whole pixels.
            f32 addWidth = floorf(plus * 0.8f), addHeight = wash;
            addWidth -= fmodf(plus - addWidth, 2);
            UI_Interaction add = Prog__TextButton(S("TabNew"), S(""), m->icons, UI_Pixels(addWidth), addHeight, 0, Prog__CaptionInk(theme), theme->text, theme->iconDisabled,
                                                  state->tabCount < Prog_MaxTabs, m);
            if (add.clicked) Prog_QueueCommand(state, Prog_CommandKind_TabNew, 0, 0, add.clickOrder);
            // The plus as two bars in the middle of its box, as the icon font's glyph sits a pixel above it, the size and weight of Lucide's.
            UI_Block *button = UI->parent->last;
            f32 length = floorf(9.6f * scale + 0.5f), weight = MaxF32(1, floorf(1.6f * scale + 0.5f));
            length -= fmodf(length - weight, 2);
            f32 x = floorf((addWidth - length) * 0.5f), y = floorf((addHeight - length) * 0.5f), across = (length - weight) * 0.5f;
            UI_Parent(button) {
                // The upright bar in two, above and below the other, which a see-through color would show doubled where they cross.
                Prog__IconRect(x, y + across, length, weight, button->props.colors.text, 0, 0);
                Prog__IconRect(x + across, y, weight, across, button->props.colors.text, 0, 0);
                Prog__IconRect(x + across, y + across + weight, weight, across, button->props.colors.text, 0, 0);
            }
        }
        // The tab list, left of the window buttons; in the title bar level with the tabs and the plus, as tall as the plus.
        b32 maximized = PlatformWindowIsMaximized();
        UI_SetNextFloatingOffset((UI_Vec2){ width - (controls ? controlWidth * 3 : 0) - button - m->gap - floorf((maximized ? 2.4f : 4.8f) * scale + 0.5f),
                                            caption ? 0 : -floorf(0.8f * scale + 0.5f) });
        UI_SetNextPrefWidth(UI_Pixels(button));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, caption ? 0 : 0.5f });
        if (caption) UI_SetNextChildrenPadding((UI_Padding){ 0, washGap, 0, 0 });
        UI_Block *listBox = UI_AddBlock(UI_Flag_Floating);
        UI_Parent(listBox) {
            Prog__MenuButton(state, S("TabList"), Icon_ChevronDown, Prog_CommandKind_TabList, true, caption ? wash : floorf(26.4f * scale + 0.5f), m);
            if (state->currentPanel == state->activePanel) state->tabListRect = listBox->last->rect;
        }
        // The window buttons take presses over the strip's full height at its right end, washed as the plus is.
        if (controls) {
            UI_SetNextFloatingOffset((UI_Vec2){ width - controlWidth * 3, -top });
            UI_SetNextPrefWidth(UI_Pixels(controlWidth * 3));
            UI_SetNextPrefHeight(UI_Pixels(bar - m->separator));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_Block *buttons = UI_AddBlock(UI_Flag_Floating);
            // Their washes end level with the others' and reach up as close to the top as they stay from the line.
            f32 below = bar - m->separator - washTop - wash;
            washTop = below;
            wash = bar - m->separator - below * 2;
            UI_Parent(buttons) {
                Prog_WindowButton maximize = PlatformWindowIsMaximized() ? Prog_WindowButton_Restore : Prog_WindowButton_Maximize;
                Prog__WindowButton(state, S("WindowMinimize"), Prog_WindowButton_Minimize, Prog_CommandKind_WindowMinimize, controlWidth, bar - m->separator, washTop, wash, scale, m);
                Prog__WindowButton(state, S("WindowMaximize"), maximize, Prog_CommandKind_WindowToggleMaximize, controlWidth, bar - m->separator, washTop, wash, scale, m);
                Prog__WindowButton(state, S("WindowClose"), Prog_WindowButton_Close, Prog_CommandKind_WindowClose, controlWidth, bar - m->separator, washTop, wash, scale, m);
            }
        }
    }
}

// A crumb goes to its folder, or opens it in a new tab with the middle button or Ctrl.
function Prog_CommandKind Prog__CrumbCommand(UI_Interaction interaction)
{
    b32 inTab = interaction.button == MouseButton_Middle || (interaction.button == MouseButton_Left && (UI->mods & KeyMod_Control));
    return inTab ? Prog_CommandKind_PanelHeaderDirInTab : Prog_CommandKind_NavigatePrefix;
}

// Where a folder's crumbs start in `path`: the deepest path root it is inside, from that root's own folder name on, else the whole path, with `drive`
// set for a drive letter's.
function u64 Prog__CrumbStart(const Prog_State *state, String path, b32 *drive)
{
    u64 rootLength = 0;
    // Without shortened paths, every crumb from the drive shows.
    for (u64 i = 0; !state->fullPaths && i < state->pathRoots.header.count; ++i) {
        String root = state->pathRoots.v[i];
        while (root.length > 1 && root.value[root.length - 1] == '/') --root.length;
        if (root.length > rootLength && path.length > root.length + 1 && path.value[root.length] == '/' && StrIsEqual(StrFromTo(path, 0, (i64)root.length), root)) {
            rootLength = root.length;
        }
    }
    *drive = !rootLength && path.length >= 2 && path.value[1] == ':';
    if (!rootLength) return path.length && path.value[0] == '/' ? 1 : 0;
    Index slash = StrIndexChar(StrFromTo(path, 0, (i64)rootLength), '/', SSF_SearchBackwards);
    return slash.exists ? slash.value + 1 : 0;
}

// The path the crumbs show: the trail of folders visited below the shown one goes on past it.
function String Prog__CrumbsPath(Prog_State *state, u64 *shownEnd)
{
    String shown = state->directory.path;
    b32 thisPC = StrIsEqual(shown, Prog_ThisPC);
    String trail = state->noTrail ? (String){0} : Prog_TrailText(state);
    *shownEnd = thisPC ? 0 : shown.length;
    while (*shownEnd > 2 && shown.value[*shownEnd - 1] == '/') --*shownEnd;
    return trail.length ? trail : thisPC ? (String){0} : shown;
}

function u32 Prog_CrumbEnds(Prog_State *state, String *path, u64 *shownEnd, u64 *ends, u32 max)
{
    *path = Prog__CrumbsPath(state, shownEnd);
    b32 drive;
    u32 count = 0;
    for (u64 at = Prog__CrumbStart(state, *path, &drive); at < path->length && count < max;) {
        u64 end = at + StrIndexChar(StrFrom(*path, at), '/', SSF_CountOnFail).value;
        if (end > at) ends[count++] = end;
        at = end + 1;
    }
    return count;
}

// The name the sidebar gives the drive `path` is on, or its letter.
function String Prog__DriveName(const Prog_State *state, String path)
{
    for (u64 i = 0; i < state->drives.header.count; ++i) {
        const P_Place *place = &state->drives.v[i];
        if (place->path.length >= 2 && Prog__FoldAscii((u8)place->path.value[0]) == Prog__FoldAscii((u8)path.value[0]) && place->path.value[1] == ':') return place->name;
    }
    return StrFromTo(path, 0, 2);
}

// A folder as its crumbs read, "Local\Temp\Fixture", in the system's separators.
function String Prog__CrumbPath(Prog_State *state, String path)
{
    if (!path.length) return path;
    b32 drive;
    u64 start = Prog__CrumbStart(state, path, &drive);
    String rest = StrFrom(path, drive ? 2 : start);
    while (rest.length && rest.value[rest.length - 1] == '/') --rest.length;
    String text = drive ? StrFormat(&state->frameArena, "%S%S", Prog__DriveName(state, path), rest) : rest;
    return P_NativePath(&state->frameArena, text);
}

// Go To writes a folder outside the path roots from its drive letter, "C:\Program Files".
function String Prog__GoToWhere(Prog_State *state, String path)
{
    b32 drive;
    Prog__CrumbStart(state, path, &drive);
    if (!drive) return Prog__CrumbPath(state, path);
    // A drive's own root is just its letter, "C:".
    while (path.length > 2 && path.value[path.length - 1] == '/') --path.length;
    return P_NativePath(&state->frameArena, path);
}

// "C:/Git/Filey" -> "This PC" › "Local Disk (C:)" › "Git" › "Filey", and a folder inside one of the path roots from that root on, as "Pictures" › "Screenshots".
// Every segment but the current one opens its folder. The crumb under the pointer comes back, as the length of its path, 0 for This PC, or
// UINT64_MAX for none.
function u64 Prog__Breadcrumbs(Prog_State *state, const Prog_Metrics *m)
{
    u64 hovered = UINT64_MAX;
    const Prog_Theme *theme = Prog_CurrentTheme();
    b32 thisPC = StrIsEqual(state->directory.path, Prog_ThisPC);
    u64 shownEnd;
    String path = Prog__CrumbsPath(state, &shownEnd);
    b32 drive;
    u64 start = Prog__CrumbStart(state, path, &drive);
    // The slot of the chevron between two crumbs.
    f32 chevron = floorf(m->line * 1.6f - 4.0f * m->scale);
    // This PC before a drive's crumbs, whose dropdown, from the separator after it, lists the drives; while open, it is lit with the
    // separator turned down in it.
    b32 thisOpen = drive && state->filesOpen && StrIsEqual(state->filesFolder, Prog_ThisPC);
    if (thisOpen) {
        String name = Tr("This PC");
        UI_SetNextPrefWidth(UI_Pixels(m->gap * 0.5f + R_TextWidth(m->font, name) + chevron));
        UI_SetNextPrefHeight(UI_Pixels(m->bar * 0.75f));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextChildrenPadding((UI_Padding){ m->gap * 0.5f, 0, 0, 0 });
        UI_SetNextCornerRadius(floorf(6.4f * m->scale + 0.5f));
        UI_SetNextColors((UI_Colors){ .background = theme->selection });
        UI_Block *lit = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Crumb/ThisPC")), UI_Flag_DrawBackground);
        UI_Parent(lit) {
            Prog__Label(name, UI_TextContent(), m->bar * 0.75f, 0, theme->text, 0);
            Prog__SmallIcon(Icon_ChevronDown, chevron, m->bar * 0.75f, theme->text, 0.5f, m);
        }
        state->filesAnchor = lit->rect;
    } else if (drive || thisPC) {
        UI_Interaction button = Prog__TextButton(S("Crumb/ThisPC"), Tr("This PC"), NULL, UI_TextContent(), m->bar * 0.75f, m->gap * 0.5f, theme->textDim, theme->text,
                                                 thisPC ? theme->text : theme->textDim, !thisPC, m);
        if (button.clicked) Prog_QueueCommand(state, Prog__CrumbCommand(button), 0, 0, button.clickOrder);
        if (UI->hotKey == UI_KeyFromStr(0, S("Crumb/ThisPC"))) hovered = 0;
    }
    // A server's crumbs follow the Network's, where there is one, which goes to it.
    b32 server = Prog_PathServer(path).length && P_NetworkFolder().length;
    if (server) {
        UI_Interaction button = Prog__TextButton(S("Crumb/Network"), Tr("Network"), NULL, UI_TextContent(), m->bar * 0.75f, m->gap * 0.5f, theme->textDim, theme->text,
                                                 theme->textDim, true, m);
        if (button.clicked) Prog_QueueCommand(state, Prog__CrumbCommand(button), Prog_NetworkCrumb, 0, button.clickOrder);
        Prog__SmallIcon(Icon_ChevronRight, chevron, m->bar * 0.75f, theme->textDim, 0.5f, m);
        start = 2;
    }
    if (drive || thisPC) {
        drive = drive || (thisPC && path.length);
    } else if (start == 1) {
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(Prog__StripHeight(m)));
    }
    for (u64 at = start; at < path.length;) {
        Index slash = StrIndexChar(StrFrom(path, at), '/', SSF_CountOnFail);
        u64 end = at + slash.value;
        String segment = StrFromTo(path, at, (i64)end);
        b32 current = end == shownEnd;
        if (segment.length) {
            // The separator after a crumb opens that crumb's folder dropdown; while it is open, it is drawn turned down inside the crumb.
            b32 afterOpen = at > start && state->filesOpen && StrIsEqual(Prog__CrumbFolder(path, at - 1), state->filesFolder);
            if (at > start && !afterOpen) {
                UI_Interaction separator = Prog__TextButton(StrFormat(&state->frameArena, "CrumbSeparator/%S", StrFromTo(path, 0, (i64)at - 1)), Icon_ChevronRight,
                                                            m->iconsSmall, UI_Pixels(chevron), m->bar * 0.75f, 0, theme->textDim, theme->text,
                                                            theme->textDim, true, m);
                if (separator.clicked) Prog_QueueCommand(state, Prog_CommandKind_FilesPopupOpen, 0, 0, separator.clickOrder)->target = at - 1;
            } else if (drive && at == start && !thisOpen) {
                UI_Interaction separator = Prog__TextButton(S("CrumbSeparator/ThisPC"), Icon_ChevronRight, m->iconsSmall, UI_Pixels(chevron), m->bar * 0.75f, 0, theme->textDim, theme->text,
                                                            theme->textDim, true, m);
                if (separator.clicked) Prog_QueueCommand(state, Prog_CommandKind_FilesPopupOpen, 0, 0, separator.clickOrder)->target = Prog_ThisPCCrumb;
            }
            if (drive && at == 0) segment = Prog__DriveName(state, path);
            else if (StrIsEqual(StrFromTo(path, 0, (i64)end), P_NetworkFolder())) segment = Tr("Network");
            else if (P_IsTrashFolder(StrFromTo(path, 0, (i64)end))) segment = Prog_PathName(state, StrFromTo(path, 0, (i64)end));
            // A walk through the whole tree is marked after the folder walked.
            if (current && state->expanded) segment = StrFormat(&state->frameArena, "%S [...]", segment);
            String identity = StrFormat(&state->frameArena, "Crumb/%S", StrFromTo(path, 0, (i64)end));
            if (state->filesOpen && StrIsEqual(Prog__CrumbFolder(path, end), state->filesFolder)) {
                b32 more = end < path.length;
                UI_SetNextPrefWidth(UI_Pixels(m->gap * 0.5f + R_TextWidth(m->font, segment) + (more ? chevron : m->gap * 0.5f)));
                UI_SetNextPrefHeight(UI_Pixels(m->bar * 0.75f));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextChildrenPadding((UI_Padding){ m->gap * 0.5f, 0, more ? 0 : m->gap * 0.5f, 0 });
                UI_SetNextCornerRadius(floorf(6.4f * m->scale + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = theme->selection });
                UI_Block *lit = UI_AddBlockFromKey(UI_KeyFromStr(0, identity), UI_Flag_DrawBackground);
                UI_Parent(lit) {
                    Prog__Label(segment, UI_TextContent(), m->bar * 0.75f, 0, theme->text, 0);
                    if (more) Prog__SmallIcon(Icon_ChevronDown, chevron, m->bar * 0.75f, theme->text, 0.5f, m);
                }
                state->filesAnchor = lit->rect;
            } else {
                UI_Interaction interaction = Prog__TextButton(identity, segment, NULL, UI_TextContent(), m->bar * 0.75f, m->gap * 0.5f,
                                                              theme->textDim, theme->text, current ? theme->text : theme->textDim, !current, m);
                if (interaction.clicked) Prog_QueueCommand(state, Prog__CrumbCommand(interaction), end, 0, interaction.clickOrder);
                // The shown folder's crumb takes no clicks, so it is found under the pointer by its place.
                UI_Block *crumb = UI->parent->last;
                if (UI->hotKey == UI_KeyFromStr(0, identity) || (UI->mouseKnown && UI->mouse.x >= crumb->rect.x0 && UI->mouse.x < crumb->rect.x1 &&
                                                                  UI->mouse.y >= crumb->rect.y0 && UI->mouse.y < crumb->rect.y1)) hovered = end;
            }
        }
        at = end + 1;
    }
    return hovered;
}

// The buttons that keep the shown folder's view and bookmark it, `apart` from each other, each lit while it is so.
function void Prog__FolderButtons(Prog_State *state, f32 apart, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String path = state->directory.path;
    Prog_ViewSettings view;
    b32 kept = Prog_FolderView(state, path, &view);
    // A little narrower than the other toolbar buttons and further apart.
    f32 width = floorf(25.6f * m->scale + 0.5f), height = Prog__ToolbarButtonHeight(m);
    UI_Interaction lock = Prog__TextButton(S("FolderView"), kept ? Icon_LockKeyhole : Icon_LockKeyholeOpen, m->icons, UI_Pixels(width), height, 0,
                                           theme->icon, theme->text, theme->iconDisabled, path.length != 0, m);
    if (path.length) Prog__DescribeTooltip(state, Prog_CommandKind_FolderViewToggle, kept ? Tr("Folder view settings saved and applied.\nIcons, sort, folders first, list columns.")
                                                                                        : Tr("No folder view settings saved.\nUsing panel's view settings."));
    if (lock.clicked) Prog_QueueCommand(state, Prog_CommandKind_FolderViewToggle, 0, 0, lock.clickOrder);
    Prog__Spacer(UI_Pixels(apart), UI_Pixels(Prog__StripHeight(m)));
    b32 bookmarked = Prog_IsBookmarked(state, path);
    UI_Interaction bookmark = Prog__TextButton(NoTr("Bookmark"), Icon_Bookmark, m->icons, UI_Pixels(width), height, 0,
                                               bookmarked ? theme->textMatch : theme->icon, bookmarked ? theme->textMatch : theme->text, theme->iconDisabled,
                                               path.length != 0, m);
    if (path.length) Prog__DescribeTooltip(state, Prog_CommandKind_BookmarksToggle, bookmarked ? Tr("This path is bookmarked.") : Tr("This path is not bookmarked."));
    if (bookmark.clicked) Prog_QueueCommand(state, Prog_CommandKind_BookmarksToggle, 0, 0, bookmark.clickOrder);
}

function R_Color Prog__GitInk(Prog_GitMark mark);

// `color` softened toward the secondary text, as the git status's counts take it beside the plain branch name.
function R_Color Prog__Muted(R_Color color)
{
    R_Color dim = Prog_CurrentTheme()->textDim;
    return (R_Color){ color.r + (dim.r - color.r) * 0.45f, color.g + (dim.g - color.g) * 0.45f, color.b + (dim.b - color.b) * 0.45f, color.a };
}

// The branch of the git repository the shown folder is in, in the path's font: the commits to push and to pull beside it, then the changes
// not yet committed by kind, each in the color its files take in the list.
function void Prog__GitStatus(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->git.branch.length) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    f32 bar = Prog__StripHeight(m), gap = floorf(8.0f * m->scale + 0.5f), close = floorf(3.2f * m->scale + 0.5f), pad = floorf(6.4f * m->scale + 0.5f);
    // Its parts' texts first, for the width of the button they make, which opens the dropdown of the repository's branches.
    String counts[2] = {0}, changes[Prog_GitMark_Count] = {0};
    local_persist const char *const signs[Prog_GitMark_Count] = { 0, "?", "+", "~", "!" };
    f32 width = 2 * pad + R_TextWidth(m->icons, Icon_GitBranch) + close + R_TextWidth(m->font, state->git.branch);
    for (u32 side = 0; side < 2; ++side) {
        i32 count = side ? state->git.behind : state->git.ahead;
        if (count <= 0) continue;
        counts[side] = StrFormat(frame, "%d", count);
        width += gap + R_TextWidth(m->iconsSmall, side ? Icon_ArrowDown : Icon_ArrowUp) + R_TextWidth(m->font, counts[side]);
    }
    for (u32 mark = Prog_GitMark_Untracked; mark < Prog_GitMark_Count; ++mark) {
        if (!state->git.counts[mark]) continue;
        changes[mark] = StrFormat(frame, "%s%u", signs[mark], state->git.counts[mark]);
        width += gap + R_TextWidth(m->font, changes[mark]);
    }
    UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "GitStatus/%" PRIu64, Prog__PanelId(state)));
    b32 open = state->branchesOpen && state->branchesPanel == Prog__PanelId(state), hot = UI->hotKey == key;
    // The panel's keys are its own, so the dropdown, built over everything, finds the button by the key it had.
    if (open) state->branchesAnchor = key;
    R_Color washed = theme->selection;
    washed.a *= theme->hoverAmount.r;
    Prog__Spacer(UI_Pixels(m->gap - pad), UI_Pixels(bar));
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(Prog__ToolbarButtonHeight(m)));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ pad, 0, pad, 0 });
    UI_SetNextCornerRadius(floorf(4.0f * m->scale + 0.5f));
    UI_SetNextColors((UI_Colors){ .background = open ? theme->selection : washed });
    UI_Block *button = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | ((open || hot) ? UI_Flag_DrawBackground : 0));
    UI_Interaction press = UI_InteractionFromBlock(button);
    if (press.clicked) Prog_QueueCommand(state, Prog_CommandKind_GitBranchesOpen, 0, 0, press.clickOrder);
    Prog__PopupTooltip(state, Tr("Switch branch."));
    UI_Parent(button) {
        f32 inner = Prog__ToolbarButtonHeight(m);
        Prog__Icon(Icon_GitBranch, 0, inner, theme->icon, 0.5f, m);
        Prog__Spacer(UI_Pixels(close), UI_Pixels(inner));
        Prog__Label(state->git.branch, UI_TextContent(), inner, 0, open ? theme->contentSelection : theme->text, 0);
        for (u32 side = 0; side < 2; ++side) {
            if (!counts[side].length) continue;
            Prog__Spacer(UI_Pixels(gap), UI_Pixels(inner));
            Prog__SmallIcon(side ? Icon_ArrowDown : Icon_ArrowUp, 0, inner, Prog__Muted(theme->codeHeading), 0.5f, m);
            Prog__Label(counts[side], UI_TextContent(), inner, 0, Prog__Muted(theme->codeHeading), 0);
        }
        for (u32 mark = Prog_GitMark_Untracked; mark < Prog_GitMark_Count; ++mark) {
            if (!changes[mark].length) continue;
            Prog__Spacer(UI_Pixels(gap), UI_Pixels(inner));
            Prog__Label(changes[mark], UI_TextContent(), inner, 0, Prog__Muted(Prog__GitInk((Prog_GitMark)mark)), 0);
        }
    }
    // Commits to pull are pulled by the button beside it, as git can without merging.
    if (state->git.behind > 0) {
        UI_Interaction pull = Prog__TextButton(StrFormat(frame, "GitPull/%" PRIu64, Prog__PanelId(state)), Icon_Download, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)),
                                               Prog__ToolbarButtonHeight(m), 0, theme->icon, theme->text, theme->iconDisabled, Prog_CanPull(state), m);
        Prog__NoteTooltip(state, Prog_CommandKind_GitPull, (String){0});
        if (pull.clicked && Prog_CanPull(state)) Prog_QueueCommand(state, Prog_CommandKind_GitPull, 0, 0, pull.clickOrder);
    }
    Prog__Spacer(UI_Pixels(m->gap - pad), UI_Pixels(bar));
}

function void Prog__Toolbar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 bar = Prog__StripHeight(m);
    UI_Block *toolbar = Prog__Box(UI_Flex(), UI_Pixels(bar), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(toolbar) {
        Prog__Spacer(UI_Pixels(floorf(4.8f * m->scale + 0.5f)), UI_Pixels(bar));
        String up = Prog_UpPath(state->directory.path);
        Prog__ToolbarButton(state, NoTr("Back"), Icon_ArrowLeft, Prog_CommandKind_Back, Prog_BackPath(state).length != 0, m);
        Prog__ToolbarButton(state, NoTr("Forward"), Icon_ArrowRight, Prog_CommandKind_Forward, state->historyAt + 1 < state->history.header.count, m);
        Prog__ToolbarButton(state, S("Up"), Icon_ArrowUp, Prog_CommandKind_Up, up.length != 0, m);
        Prog__MenuButton(state, S("HistoryMenu"), Icon_ChevronDown, Prog_CommandKind_HistoryMenu, state->history.header.count > 1, 0, m);
        Prog__Spacer(UI_Pixels(floorf(6.4f * m->scale + 0.5f)), UI_Pixels(bar));
        Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
        Prog__FolderButtons(state, floorf(5.6f * m->scale + 0.5f), m);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
        Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
        Prog__Spacer(UI_Pixels(floorf(7.2f * m->scale + 0.5f)), UI_Pixels(bar));
        // A path wider than the room shows its end, cut off at the left.
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(bar));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextScrollOffset((UI_Vec2){ 1e9f, 0 });
        UI_Block *crumbs = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "Breadcrumbs/%" PRIu64, Prog__PanelId(state))), UI_Flag_Clip | UI_Flag_RightMouseClickable);
        // A right click opens the path's menu for the crumb under the pointer, or the shown folder away from the crumbs.
        UI_Interaction strip = UI_InteractionFromBlock(crumbs);
        u64 hovered = UINT64_MAX;
        if (state->currentPanel == state->activePanel) state->pathMenuAt = (UI_Vec2){ crumbs->rect.x0, crumbs->rect.y1 };
        UI_Parent(crumbs) {
            hovered = Prog__Breadcrumbs(state, m);
            // A path copied flashes the path bar: the selection color washes in and out over it.
            u64 since = UI->nowMicroseconds > state->pathFlashTime ? UI->nowMicroseconds - state->pathFlashTime : 0;
            if (state->pathFlashTime && since < 450000 && crumbs->rect.x1 > crumbs->rect.x0) {
                R_Color flash = theme->selection;
                flash.a *= 0.45f * (UI->animate ? sinf(3.14159265f * (f32)since / 450000.0f) : 1);
                f32 inset = floorf(3.2f * m->scale + 0.5f);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, inset });
                UI_SetNextPrefWidth(UI_Pixels(crumbs->rect.x1 - crumbs->rect.x0));
                UI_SetNextPrefHeight(UI_Pixels(bar - 2 * inset));
                UI_SetNextCornerRadius(floorf(4.0f * m->scale + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = flash });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                UI_KeepAnimating();
            }
        }
        if (strip.pressed && strip.button == MouseButton_Right) {
            u64 shownEnd = 0;
            Prog__CrumbsPath(state, &shownEnd);
            Prog_QueueCommand(state, Prog_CommandKind_PathMenu, hovered != UINT64_MAX ? hovered : shownEnd, 0, strip.pressOrder)->at = UI->mouse;
        }
        Prog__GitStatus(state, m);
        Prog__MenuButton(state, S("PanelMenu"), Icon_EllipsisVertical, Prog_CommandKind_PanelMenu, true, 0, m);
        Prog__Spacer(UI_Pixels(m->inset * 0.4f), UI_Pixels(bar));
    }
}

// A column as the header and the rows lay it out: what it shows, how wide it is (0 takes the room left) and where its values sit.
typedef struct Prog_Column {
    String title;
    f32 width;
    f32 alignX;
    enum8(Prog_ColumnId) id;
    u8 place;      // its place among the panel's columns, which the Folders and Files of Items share
    b8 lineAfter;  // a line parts it from the next; Folders and Files share their slot, and the last has none
} Prog_Column;

typedef struct Prog_Columns {
    u32 count;
    f32 room;      // the rows' width the shares are of
    Prog_Column v[Prog_ColumnKind_Count + 1];
} Prog_Columns;

// The sort a column's title selects; the usage columns sort by size, and Git by nothing (Prog_Sort_Count).
global const Prog_Sort Prog_ColumnSort[Prog_ColumnId_Count] = {
    Prog_Sort_Name, Prog_Sort_Type, Prog_Sort_Modified, Prog_Sort_Folders, Prog_Sort_Files, Prog_Sort_Size, Prog_Sort_Created, Prog_Sort_Accessed, Prog_Sort_Size, Prog_Sort_Size,
    Prog_Sort_FileSystem, Prog_Sort_FreeSpace, Prog_Sort_PercentFull, Prog_Sort_Original, Prog_Sort_Deleted, Prog_Sort_Count,
};

global const String Prog_ColumnTitles[Prog_ColumnId_Count] = {
    TrInit("Name"), TrInit("Type"), TrInit("Date Modified"), TrInit("Folders"), TrInit("Files"), TrInit("Size"), TrInit("Date Created"), TrInit("Date Accessed"), TrInit("Folder Usage"), TrInit("Volume Usage"),
    TrInit("File System"), TrInit("Free Space"), TrInit("Percent Full"), TrInit("Original Location"), TrInit("Date Deleted"), TrInit("Git"),
};

// The panel's columns laid out for rows `room` wide: each its share of it, however narrow,
// Name what the others leave of it rounded down, and the last column the pixels left over.
// The column before the rows' icons that marks the recents, there only while the folder holds any.
function f32 Prog__RecentGutter(f32 scale) { return floorf(19.2f * scale + 0.5f); }
function b32 Prog__MarksRecents(const Prog_State *state) { return state->showRecents && state->directory.recentCount; }

function Prog_Columns Prog__Columns(const Prog_State *state, f32 scale, f32 room)
{
    local_persist const Prog_ColumnId ids[Prog_ColumnKind_Count] = {
        Prog_ColumnId_Name, Prog_ColumnId_Type, Prog_ColumnId_Size, Prog_ColumnId_Folders, Prog_ColumnId_Created, Prog_ColumnId_Modified, Prog_ColumnId_Accessed,
        Prog_ColumnId_VolumeUsage, Prog_ColumnId_FolderUsage, Prog_ColumnId_Git, Prog_ColumnId_FileSystem, Prog_ColumnId_FreeSpace, Prog_ColumnId_PercentFull,
        Prog_ColumnId_OriginalLocation, Prog_ColumnId_DateDeleted,
    };
    // This PC's values read from the left, its size too.
    b32 drives = StrIsEqual(state->directory.path, Prog_ThisPC);
    if (room <= 0) room = 918.0f * scale;
    // The shares are of the rows inside their outlines.
    room -= floorf(1.6f * scale + 0.5f);
    if (Prog__MarksRecents(state)) room -= Prog__RecentGutter(scale);
    Prog_ColumnSet set = Prog_PanelColumns(state);
    Prog_Columns result = { .room = room };
    // Shares that add up to more than nine tenths shrink together, so that Name keeps a tenth.
    f32 others = 0;
    for (u32 i = 0; i < set.count; ++i) if (set.kinds[i] != Prog_ColumnKind_Name) others += set.shares[i] > 0 ? set.shares[i] : Prog_ColumnShare((Prog_ColumnKind)set.kinds[i]);
    f32 shrink = others > 0.9f ? 0.9f / others : 1;
    for (u32 i = 0; i < set.count; ++i) {
        Prog_ColumnKind kind = (Prog_ColumnKind)set.kinds[i];
        f32 share = (set.shares[i] > 0 ? set.shares[i] : Prog_ColumnShare(kind)) * shrink;
        u32 parts = kind == Prog_ColumnKind_Items ? 2 : 1;
        f32 slot = floorf(share * room + 0.5f);
        for (u32 part = 0; part < parts; ++part) {
            Prog_ColumnId id = (Prog_ColumnId)((u32)ids[kind] + part);
            // Of the slot they share, Folders takes a little more than half and Files the rest.
            f32 folders = floorf(slot * 0.5f + 4.8f * scale + 0.5f);
            f32 width = id == Prog_ColumnId_Name ? floorf((1 - others * shrink) * room) : parts == 1 ? slot : part == 0 ? folders : slot - folders;
            // The last takes what the others leave, so that the rows end where the room does.
            if (i + 1 == set.count && part + 1 == parts) {
                f32 used = 0;
                for (u32 c = 0; c < result.count; ++c) used += result.v[c].width;
                width = MaxF32(0, room - used);
            }
            b32 right = (id == Prog_ColumnId_Size && !drives) || id == Prog_ColumnId_FolderUsage || id == Prog_ColumnId_VolumeUsage;
            result.v[result.count++] = (Prog_Column){ Prog_Tr(Prog_ColumnTitles[id]), width, right ? 1.0f : 0, (u8)id, (u8)i, (b8)(part + 1 == parts && i + 1 < set.count) };
        }
    }
    return result;
}

// The space inside a details column before and after its title and values; Files sits close after Folders, and sizes keep clear of the row's end.
function f32 Prog__ColumnLeft(u32 column, const Prog_Metrics *m)
{
    return column == Prog_ColumnId_Name ? m->gap : floorf((column == Prog_ColumnId_Files ? 6.4f : 17.6f) * m->scale + 0.5f);
}

function f32 Prog__ColumnRight(u32 column, const Prog_Metrics *m)
{
    return column == Prog_ColumnId_Size ? floorf(16.0f * m->scale + 0.5f) : column == Prog_ColumnId_Folders ? 0 : m->gap;
}

function String Prog__Ellipsize(Arena *arena, const R_Font *font, String text, f32 width);
function void Prog__EntryValues(Prog_State *state, const Prog_Entry *entry, String *values);

// Sizes the panel's columns marked by fitColumn to their widest value among the items shown and their title, at most 5000 items in.
// ponytail: the first 5000 items only; measure in a job if huge folders need every one.
function void Prog__FitColumns(Prog_State *state, const Prog_Columns *columns, const Prog_Metrics *m)
{
    u32 fit = state->fitColumn;
    state->fitColumn = 0;
    if (!fit || columns->room <= 0) return;
    f32 widths[Prog_ColumnKind_Count] = {0};
    u64 rows = Min(state->viewCount, (u64)5000);
    for (u32 c = 0; c < columns->count; ++c) {
        const Prog_Column *column = &columns->v[c];
        if (fit != UINT32_MAX && column->place + 1u != fit) continue;
        f32 widest = R_TextWidth(m->font, column->title) + floorf(m->line * 0.6f);
        for (u64 r = 0; r < rows; ++r) {
            String values[Prog_ColumnId_Count];
            Prog__EntryValues(state, &state->directory.entries[state->view[r]], values);
            widest = MaxF32(widest, R_TextWidth(m->detail, values[column->id]));
        }
        widths[column->place] += widest + Prog__ColumnLeft(column->id, m) + Prog__ColumnRight(column->id, m) + (column->lineAfter ? m->separator : 0);
    }
    Prog_ColumnSet set = Prog_PanelColumns(state);
    for (u32 i = 0; i < set.count; ++i) {
        if (widths[i] <= 0 || set.kinds[i] == Prog_ColumnKind_Name) continue;
        Prog_QueueCommand(state, Prog_CommandKind_ColumnResize, i, 0, 0)->target = (u64)(widths[i] / columns->room * 1e6f);
    }
}

function void Prog__Header(Prog_State *state, const Prog_Columns *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog__FitColumns(state, columns, m);
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
    UI_Block *header = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->background, 0);
    // Where each column ends in the last layout, for the edges that resize them, from where the first starts.
    f32 ends[Prog_ColumnKind_Count + 1] = {0}, x0 = 0;
    // A title dragged along the header takes its column with it, as a tab along the strip: the others slide aside to make room where it
    // would go, and close up behind it; let go, it goes there. A place is a column, Folders and Files together, and Name stays where it is.
    f32 base = Prog__MarksRecents(state) ? Prog__RecentGutter(m->scale) : 0;
    f32 placeWidth[Prog_ColumnKind_Count] = {0}, target[Prog_ColumnKind_Count] = {0};
    u32 places = 0;
    b32 fixed = true;
    for (u32 c = 0; c < columns->count; ++c) {
        places = Max(places, columns->v[c].place + 1u);
        placeWidth[columns->v[c].place] += columns->v[c].width;
        fixed &= columns->v[c].width > 0;
    }
    u32 dragged = state->columnDrag && fixed ? state->columnDrag - 1 : UINT32_MAX;
    if (dragged >= places) dragged = UINT32_MAX, state->columnDrag = 0;
    f32 dragX = 0;
    b32 dropped = false;
    if (dragged != UINT32_MAX) {
        // Where it is held, kept off the header's ends, and the place among the others its middle is nearest.
        f32 total = 0;
        for (u32 p = 0; p < places; ++p) total += placeWidth[p];
        dragX = UI__Clamp(UI->mouse.x - header->rect.x0 - m->inset - base - state->columnGrab, 0, MaxF32(0, total - placeWidth[dragged]));
        f32 middle = dragX + placeWidth[dragged] * 0.5f, x = 0;
        u32 at = 0;
        for (u32 p = 0; p < places; ++p) {
            if (p == dragged) continue;
            if (x + placeWidth[p] * 0.5f < middle) ++at;
            x += placeWidth[p];
        }
        state->columnDropAt = at;
        x = 0;
        u32 other = 0;
        for (u32 p = 0; p < places; ++p) {
            if (p == dragged) continue;
            if (other++ == at) target[dragged] = x, x += placeWidth[dragged];
            target[p] = x;
            x += placeWidth[p];
        }
        if (at == other) target[dragged] = x;
        // Let go: it goes to its place, sliding there from where it was held.
        if (UI->activeKey != state->columnDragKey) {
            if (at != dragged) Prog_QueueCommand(state, Prog_CommandKind_ColumnMove, dragged, 0, 0)->target = at;
            state->columnDrag = 0;
            state->columnSlideUntil = UI->nowMicroseconds + 400000;
            dragX = target[dragged];
            dropped = true;
        }
    } else {
        for (u32 p = 1; p < places; ++p) target[p] = target[p - 1] + placeWidth[p - 1];
    }
    b32 sliding = fixed && (state->columnDrag || UI->nowMicroseconds < state->columnSlideUntil);
    f32 inside[Prog_ColumnKind_Count] = {0};
    // A sort by size shows its arrow on the Size column, else on the first usage column shown, which sort the same.
    u32 sizeArrow = Prog_ColumnId_Count;
    for (u32 c = 0; c < columns->count; ++c) {
        u32 id = columns->v[c].id;
        if (id == Prog_ColumnId_Size || (sizeArrow == Prog_ColumnId_Count && Prog_ColumnSort[id] == Prog_Sort_Size)) sizeArrow = id;
    }
    UI_Parent(header) {
        if (Prog__MarksRecents(state)) Prog__Spacer(UI_Pixels(base), UI_Pixels(m->row));
        // The held column is built last, over the others.
        for (u32 pass = 0; pass < 2; ++pass) for (u32 c = 0; c < columns->count; ++c) {
            const Prog_Column *column = &columns->v[c];
            b32 held = state->columnDrag && column->place == dragged;
            if (held != (pass == 1)) continue;
            u32 id = column->id;
            Prog_Sort sort = Prog_ColumnSort[id];
            b32 sorted = sort < Prog_Sort_Count && state->sort == sort && (sort != Prog_Sort_Size || id == sizeArrow);
            UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Header/%S", column->title));
            b32 sortable = sort < Prog_Sort_Count && !state->loading;
            b32 hot = UI->hotKey == key && sortable;
            b32 movable = id != Prog_ColumnId_Name && fixed;
            UI_SetNextPrefWidth(column->width > 0 ? UI_Pixels(column->width) : UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            if (sliding) {
                f32 x = held ? dragX : UI_Slide(UI_KeyFromStr(0, StrFormat(&state->frameArena, "HeaderSlide/%" PRIu64 "/%S", Prog__PanelId(state), column->title)), target[column->place],
                                               Prog__SlideCap(m) * 4);
                // The held one keeps its slide where it is held, for the slide to its place once let go.
                if (held) UI_Slide(UI_KeyFromStr(0, StrFormat(&state->frameArena, "HeaderSlide/%" PRIu64 "/%S", Prog__PanelId(state), column->title)), dragX, 1e9f);
                UI_SetNextFloatingOffset((UI_Vec2){ floorf(base + x + inside[column->place] + 0.5f), 0 });
                inside[column->place] += column->width;
            }
            if (held) {
                // Washed as a hovered tab, but solid, so that the titles it passes over do not show through.
                R_Color under = theme->background, wash = theme->selection;
                f32 a = wash.a * theme->hoverAmount.r;
                R_Color solid = { under.r + (wash.r - under.r) * a, under.g + (wash.g - under.g) * a, under.b + (wash.b - under.b) * a, 1 };
                UI_SetNextCornerRadius(floorf(4.0f * m->scale + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = under.a >= 1 ? solid : wash });
            }
            UI_Block *cell = UI_AddBlockFromKey(key, UI_Flag_RightMouseClickable | UI_Flag_MiddleMouseClickable | (sortable || movable ? UI_Flag_LeftMouseClickable : 0) |
                                                         (sliding ? UI_Flag_Floating : 0) | (held ? UI_Flag_DrawBackground : 0));
            ends[c] = cell->rect.x1;
            if (c == 0) x0 = cell->rect.x0;
            UI_Interaction interaction = UI_InteractionFromBlock(cell);
            if (movable && interaction.active && interaction.isDragging && interaction.button == MouseButton_Left && !state->columnDrag) {
                state->columnDrag = column->place + 1;
                state->columnDragKey = key;
                f32 placeX = 0;
                for (u32 p = 0; p < column->place; ++p) placeX += placeWidth[p];
                state->columnGrab = UI->mouse.x - header->rect.x0 - m->inset - base - placeX;
                state->columnDropAt = column->place;
            }
            if (interaction.clicked && interaction.button == MouseButton_Left && sortable && !dropped && !state->columnDrag) {
                Prog_QueueCommand(state, Prog_CommandKind_Sort, (u64)sort, 0, interaction.clickOrder);
            }
            // A right click opens the header's menu for the column; a middle click closes it.
            // It opens at the pointer across, and down from the header's top whatever the pointer's height.
            if (interaction.pressed && interaction.button == MouseButton_Right) {
                Prog_QueueCommand(state, Prog_CommandKind_ColumnMenu, column->place, 0, interaction.pressOrder)->at = (UI_Vec2){ UI->mouse.x, cell->rect.y0 + floorf(4.8f * m->scale + 0.5f) };
            }
            if (interaction.clicked && interaction.button == MouseButton_Middle) Prog_QueueCommand(state, Prog_CommandKind_ColumnClose, column->place, 0, interaction.clickOrder);
            UI_Parent(cell) {
                b32 name = id == Prog_ColumnId_Name;
                f32 left = Prog__ColumnLeft(id, m), right = Prog__ColumnRight(id, m), line = column->lineAfter ? m->separator : 0;
                if (name) {
                    Prog__Spacer(UI_Pixels(m->iconSlot - m->gap), UI_Pixels(m->row));
                }
                // A box over the icons' column selects everything, and is checked once everything is; the inspector's list has none.
                if (name && !state->panels[state->currentPanel].inspectorOf) {
                    f32 box = floorf(16.0f * m->scale + 0.5f);
                    b32 all = state->viewCount && state->selectedCount == state->viewCount;
                    UI_Key boxKey = UI_KeyFromStr(0, S("Header/SelectAll"));
                    b32 boxHot = UI->hotKey == boxKey;
                    UI_SetNextFloatingOffset((UI_Vec2){ floorf(7.2f * m->scale + 0.5f), floorf((m->row - box) * 0.5f) });
                    UI_SetNextPrefWidth(UI_Pixels(box));
                    UI_SetNextPrefHeight(UI_Pixels(box));
                    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                    UI_SetNextCornerRadius(floorf(3.2f * m->scale + 0.5f));
                    UI_SetNextBorderThickness(m->stroke);
                    UI_SetNextFont(m->icons);
                    UI_SetNextColors((UI_Colors){ .background = theme->selection, .border = all ? theme->text : boxHot ? theme->textDim : theme->tabBorder, .text = theme->text });
                    UI_SetNextText(Icon_Check);
                    UI_Block *selectAll = UI_AddBlockFromKey(boxKey, UI_Flag_Floating | UI_Flag_LeftMouseClickable | UI_Flag_DrawBorder | (all ? UI_Flag_DrawBackground | UI_Flag_DrawText : 0));
                    UI_Interaction toggle = UI_InteractionFromBlock(selectAll);
                    if (toggle.clicked && !state->loading) Prog_QueueCommand(state, Prog_CommandKind_ToggleSelectAll, 0, 0, toggle.clickOrder);
                }
                R_Color color = sorted || hot ? theme->text : theme->textDim;
                // Titles start at the left even over right-aligned values, and end in an ellipsis where the column is too narrow; the sort arrow sits at the column's end.
                // The Type column's funnel opens the Types popup, at the column's end after any sort arrow.
                b32 funnel = id == Prog_ColumnId_Type && !state->panels[state->currentPanel].inspectorOf;
                f32 arrow = sorted ? floorf(m->line * 0.6f) : 0, funnelSlot = funnel ? floorf(24.0f * m->scale + 0.5f) : 0, funnelGap = funnel ? floorf(8.8f * m->scale + 0.5f) : 0;
                f32 room = column->width > 0 ? column->width - left - right - line - arrow - funnelSlot - funnelGap : 0;
                Prog__LabelPadded(room > 0 ? Prog__Ellipsize(&state->frameArena, m->font, column->title, room) : column->title, UI_Flex(), m->row, left, sorted || funnel ? 0 : right, color, 0);
                if (sorted) Prog__SortArrow(state->descending, arrow, m->row, theme->textDim, m);
                if (sorted && !funnel) Prog__Spacer(UI_Pixels(MaxF32(right, floorf(12.0f * m->scale + 0.5f))), UI_Pixels(m->row));
                if (funnel) {
                    Prog__TypesFunnel(state, funnelSlot, m);
                    Prog__Spacer(UI_Pixels(funnelGap), UI_Pixels(m->row));
                    if (state->currentPanel == state->activePanel) state->typesAnchor = cell->rect;
                }
                // The line starts the next column, so that it sits on the boundary.
                if (line > 0) {
                    f32 tall = floorf(m->line * 1.15f + 0.5f);
                    UI_SetNextFloatingOffset((UI_Vec2){ column->width > 0 ? column->width : cell->rect.x1 - cell->rect.x0, floorf((m->row - tall) * 0.5f) });
                    UI_SetNextPrefWidth(UI_Pixels(line));
                    UI_SetNextPrefHeight(UI_Pixels(tall));
                    UI_SetNextCornerRadius(line * 0.5f);
                    UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                }
                // While folder totals are being counted, the columns they fill show a spinner at their right end.
                const Prog_Icon *spinner = state->measureJob && (id == Prog_ColumnId_Files || id == Prog_ColumnId_Size) ? Prog_SpinnerIcon(state, UI->nowMicroseconds) : NULL;
                if (spinner) {
                    f32 slot = m->line + m->gap;
                    UI_SetNextFloatingOffset((UI_Vec2){ cell->rect.x1 - cell->rect.x0 - slot - (column->lineAfter ? m->separator + m->gap : 0), 0 });
                    UI_SetNextPrefWidth(UI_Pixels(slot));
                    UI_SetNextPrefHeight(UI_Pixels(m->row));
                    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                    UI_SetNextImage(spinner->texture, spinner->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, theme->icon);
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawImage);
                }
            }
        }
        // The edges between columns drag to resize: the column before an edge, or after it where the one before is Name, which takes what is left.
        f32 grip = floorf(8.0f * m->scale + 0.5f);
        for (u32 c = 0; c + 1 < columns->count && !sliding; ++c) {
            const Prog_Column *column = &columns->v[c];
            if (!column->lineAfter || ends[c] <= x0) continue;
            b32 afterName = column->id == Prog_ColumnId_Name;
            u32 place = afterName ? column->place + 1u : column->place;
            UI_SetNextFloatingOffset((UI_Vec2){ ends[c] - x0 - grip * 0.5f, 0 });
            UI_SetNextPrefWidth(UI_Pixels(grip));
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_Block *edge = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "HeaderEdge/%u", c)), UI_Flag_Floating | UI_Flag_LeftMouseClickable);
            UI_Interaction drag = UI_InteractionFromBlock(edge);
            if (!drag.active || !drag.isDragging) continue;
            // The resized column's other edge stays where it is.
            f32 from = 0, to = 0;
            for (u32 k = 0; k < columns->count; ++k) {
                if (columns->v[k].place != place) continue;
                to = ends[k];
                if (from == 0) from = ends[k] - columns->v[k].width;
            }
            f32 width = afterName ? to - UI->mouse.x : UI->mouse.x - from;
            // Name keeps room for a name; the others' widths are what they are now.
            f32 others = 0;
            for (u32 k = 0; k < columns->count; ++k) if (columns->v[k].place != place) others += columns->v[k].width;
            width = MinF32(width, columns->room - others - floorf(160.0f * m->scale + 0.5f));
            Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_ColumnResize, place, 0, 0);
            command->target = (u64)(MaxF32(width, 1) / columns->room * 1e6f);
        }
    }
}

// The caret blinks once every 1.05 s: lit for 325 ms, fading from 90% to almost nothing over 340 ms and back over 385 ms.
// A change starts it 150 ms into the lit part, so that it stays lit a moment after every keystroke.
function f32 Prog__CaretOpacity(const Prog_TextEdit *edit)
{
    UI_Key key = UI_KeyFromStr(0, StrFormat(UI->frameArena, "Caret/%p/%u/%u/%llu", (const void *)edit, edit->cursor, edit->anchor, (unsigned long long)Prog_EditText(edit).length));
    u64 since = (UI_TimeSince(key) + 150000) % 1050000;
    UI_KeepAnimating();
    if (!UI->animate || since < 325000) return 1;
    f32 high = 0.9f, low = 0.04f;
    if (since < 665000) return high + (low - high) * (f32)(since - 325000) / 340000.0f;
    return low + (high - low) * (f32)(since - 665000) / 385000.0f;
}

// The text of `edit` with its selection and, when `caret` is set, its caret, floating at `x`, `y` in the current parent.
// How far the text of `edit` is scrolled left in its box, which keeps the caret in sight once the text is wider than the box.
function UI_Key Prog__EditScrollKey(const Prog_TextEdit *edit) { return UI_KeyFromStr(1, StrFormat(UI->frameArena, "EditScroll/%p", (const void *)edit)); }
function f32 Prog__EditScroll(const Prog_TextEdit *edit) { return UI_Kept(Prog__EditScrollKey(edit)); }

function void Prog__EditContents(const Prog_TextEdit *edit, f32 x, f32 y, b32 caret, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String text = Prog_EditText(edit);
    // A text wider than its box scrolls as far as keeps the caret in sight, and back once it fits; the box keeps the inset the text starts
    // at on its other side too. The box is measured by a block of its own across it, as the boxes holding the texts have no keys of theirs.
    UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
    UI_SetNextPrefWidth(UI_Percent(1));
    UI_SetNextPrefHeight(UI_Pixels(1));
    // A box given its width in pixels, as the rename box growing with its text is, has this frame's width to go by rather than the last's,
    // which would scroll the text for a frame as each letter widens it.
    UI_Block *box = UI->parent;
    UI_Block *across = UI_AddBlockFromKey(UI_KeyFromStr(1, StrFormat(UI->frameArena, "EditBox/%p", (const void *)edit)), UI_Flag_Floating);
    f32 boxWidth = box->props.width.kind == UI_SizeKind_Pixels ? box->props.width.value : across->rect.x1 - across->rect.x0;
    f32 room = boxWidth - x - MaxF32(x, Prog__CaretWidth(m)), scroll = 0;
    if (room > 0) {
        f32 caretAt = R_TextWidth(m->font, StrMake(text.value, edit->cursor));
        scroll = Prog__EditScroll(edit);
        if (caretAt - scroll > room) scroll = caretAt - room;
        if (caretAt < scroll) scroll = caretAt;
        scroll = MaxF32(0, floorf(MinF32(scroll, MaxF32(0, R_TextWidth(m->font, text) - room))));
    }
    UI_Slide(Prog__EditScrollKey(edit), scroll, 1e9f);
    x -= scroll;
    u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
    f32 x0 = floorf(x + R_TextWidth(m->font, StrMake(text.value, from)) + 0.5f);
    f32 target = floorf(R_TextWidth(m->font, StrMake(text.value, to)) - R_TextWidth(m->font, StrMake(text.value, from)) + 0.5f);
    // The caret jumps where it goes, but the selection's width eases, so that clearing the text draws the selection back to its start;
    // the caret waits for that. Typed over, it goes at once, rather than sliding back over the text after what was typed.
    UI_Key selection = UI_KeyFromStr(0, StrFormat(UI->frameArena, "EditSelection/%p", (const void *)edit));
    f32 width = floorf((edit->typing ? UI_Slide(selection, target, 0) : UI_Animate(selection, target, target)) + 0.5f);
    f32 x1 = x0 + width;
    b32 collapsing = to == from && width > 0;
    f32 caretX = edit->cursor == from ? x0 : x0 + target;
    if (caret && width > 0) {
        UI_SetNextFloatingOffset((UI_Vec2){ x0, y });
        UI_SetNextPrefWidth(UI_Pixels(x1 - x0));
        UI_SetNextPrefHeight(UI_Pixels(m->line));
        UI_SetNextColors((UI_Colors){ .background = theme->selection });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextPrefHeight(UI_Pixels(m->line));
    UI_SetNextColors((UI_Colors){ .text = theme->text });
    UI_SetNextText(text);
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
    if (caret && !collapsing) {
        UI_SetNextFloatingOffset((UI_Vec2){ caretX, y });
        UI_SetNextPrefWidth(UI_Pixels(Prog__CaretWidth(m)));
        UI_SetNextPrefHeight(UI_Pixels(m->line));
        UI_SetNextColors((UI_Colors){ .background = theme->text });
        UI_SetNextOpacity(Prog__CaretOpacity(edit));
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
}

// The caret position in `edit` nearest to `x`, measured from where its text starts.
function u32 Prog__EditOffsetAt(const Prog_TextEdit *edit, const R_Font *font, f32 x)
{
    String text = Prog_EditText(edit);
    u32 result = 0;
    f32 previous = 0;
    for (u64 at = 0; at < text.length;) {
        u64 next = at;
        StrDecodeUtf8(text, &next);
        f32 width = R_TextWidth(font, StrMake(text.value, next));
        if (x < (previous + width) * 0.5f) break;
        result = (u32)next;
        previous = width;
        at = next;
    }
    return result;
}

// Clicks and drags in `block`, where the text of `edit` starts `x` in, become `kind` commands that move the caret.
function void Prog__EditMouse(Prog_State *state, UI_Block *block, const Prog_TextEdit *edit, f32 x, Prog_CommandKind kind, const Prog_Metrics *m)
{
    UI_Interaction interaction = UI_InteractionFromBlock(block);
    // A drag once the pointer moves; held still, as between a double click's press and its release, the word it selected stays selected.
    b32 drag = interaction.active && !interaction.pressed && interaction.isDragging;
    if (interaction.button != MouseButton_Left || (!interaction.pressed && !drag)) return;
    u32 at = Prog__EditOffsetAt(edit, m->font, UI->mouse.x - block->rect.x0 - x + Prog__EditScroll(edit));
    Prog_Command *command = Prog_QueueCommand(state, kind, at, interaction.pressCount, interaction.pressed ? interaction.pressOrder : 0);
    command->mods = interaction.mods | (drag ? KeyMod_Shift : 0);
}

// The name being edited, with its selection and caret, in place of the name label: in a row's cell at its left, or in an icon view's
// cell, `width` pixels wide, in the middle, its text where the name's was.
function void Prog__RenameBox(Prog_State *state, UI_Size width, b32 icons, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String text = Prog_EditText(&state->rename);
    f32 pad = floorf(m->gap * 0.5f);
    f32 height = m->line + pad, above = floorf((height - m->line) * 0.5f);
    f32 boxWidth = R_TextWidth(m->font, text) + 2 * pad + Prog__CaretWidth(m);
    UI_SetNextPrefWidth(width);
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    if (icons) {
        // As tall as the name's line, with the box running past it above and below; the box is kept inside the cell's width instead.
        boxWidth = MinF32(boxWidth, width.value - 2 * (m->gap - pad));
        UI_SetNextPrefHeight(UI_Pixels(m->line));
        UI_SetNextChildrenPadding((UI_Padding){ m->gap - pad, -above, m->gap - pad, 0 });
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0 });
    } else {
        UI_SetNextPrefHeight(UI_Pixels(m->row));
        UI_SetNextChildrenPadding((UI_Padding){ m->gap - pad, 0, m->gap, 0 });
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    }
    UI_Block *cell = UI_AddBlockFromKey(UI_KeyFromStr(0, S("RenameCell")), icons ? 0 : UI_Flag_Clip);
    // In a row, the box grows with its text as far as the cell goes, the name column's or, in the list view, the last layout's, and its text
    // scrolls past that.
    if (!icons) {
        f32 cellWidth = width.kind == UI_SizeKind_Pixels ? width.value : cell->rect.x1 - cell->rect.x0;
        if (cellWidth > 0) boxWidth = MaxF32(0, MinF32(boxWidth, cellWidth - (m->gap - pad) - m->gap));
    }
    UI_Parent(cell) {
        UI_SetNextPrefWidth(UI_Pixels(boxWidth));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextCornerRadius(m->radius);
        UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->focus });
        UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("RenameBox")), UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        state->renameRect = box->rect;
        Prog__EditMouse(state, box, &state->rename, pad, Prog_CommandKind_RenameCaret, m);
        UI_Parent(box) Prog__EditContents(&state->rename, pad, above, true, m);
    }
}

// The bytes from `start` to `end` of `name`, those a filter matched, flagged in `marks` when not NULL, in the match color.
function void Prog__NameRuns(String name, u64 start, u64 end, const u8 *marks, R_Color color, R_Color matched, const R_Font *font, f32 height, const Prog_Metrics *m)
{
    for (u64 at = start; at < end; start = at) {
        b32 match = marks && marks[at];
        while (at < end && (marks && marks[at]) == match) ++at;
        if (font) UI_SetNextFont(font);
        Prog__Label(StrFromTo(name, start, (i64)at), UI_TextContent(), height, 0, match ? matched : color, 0);
    }
}

// A name whose first `prefix` bytes are the folder it is in, in an expanded listing: the item's own name, then a chevron and that folder, smaller and dimmed.
// It is the row's `height`, without the line under it, so that the name sits where the other columns' text does.
function void Prog__NameLabel(String name, u64 prefix, const u8 *marks, UI_Size width, f32 height, R_Color color, R_Color dim, R_Color matched, const Prog_Metrics *m)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, m->gap, 0 });
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *cell = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(cell) {
        Prog__NameRuns(name, prefix, name.length, marks, color, matched, NULL, height, m);
        if (prefix > 1) {
            Prog__Icon(Icon_ChevronRight, floorf(24.0f * m->scale + 0.5f), height, dim, 0.6f, m);
            Prog__NameRuns(name, 0, prefix - 1, marks, dim, matched, m->detail, height, m);
        }
    }
}

// What a row or cell shows in each column: folders show their totals once measured; zero counts and files' counts read "--".
function String Prog__FormatCount(Arena *arena, u64 count);

// The totals counted so far of the shown folder's subfolder `entry` while it is being measured; `complete` once its tree is all counted.
function b32 Prog__LiveTotals(const Prog_State *state, const Prog_Entry *entry, P_DirectoryTotals *totals)
{
    Prog_JobInput *input = state->measureJob ? state->measureJob->input : NULL;
    if (!input || input->listing != state->listing || entry < state->directory.entries || entry >= state->directory.entries + state->directory.count) return false;
    // The rows measured are in listing order.
    u64 row = (u64)(entry - state->directory.entries), low = 0, high = input->rowCount;
    while (low < high) {
        u64 middle = (low + high) / 2;
        if (input->rows[middle] < row) low = middle + 1;
        else high = middle;
    }
    if (low >= input->rowCount || input->rows[low] != row) return false;
    Prog_TreeTotals *tree = &input->trees[low];
    *totals = (P_DirectoryTotals){ atomic_load(&tree->folders), atomic_load(&tree->files), atomic_load(&tree->bytes), atomic_load(&tree->pending) == 0 };
    return true;
}

// The name an entry shows: a drive's or a binned item's own, and a file's without its extension when the settings ask.
function String Prog_EntryName(const Prog_State *state, const Prog_Entry *entry)
{
    // A drive goes by the sidebar's name for it, which is the latest, and which a drive spinning up a disc keeps from before.
    const P_Place *drive = entry->drive ? Prog_PathDrive(state, entry->file.name) : NULL;
    if (drive && drive->name.length && StrIsEqual(drive->path, entry->file.name)) return drive->name;
    if (entry->label.length) return entry->label;
    String name = entry->file.name;
    if (state->hideExtensions && entry->file.kind != P_FileKind_Directory) {
        Index dot = StrIndexChar(name, '.', SSF_SearchBackwards);
        Index slash = StrIndexChar(name, '/', SSF_SearchBackwards);
        u64 start = slash.exists ? slash.value + 1 : 0;
        if (dot.exists && dot.value > start) name = StrFromTo(name, 0, (i64)dot.value);
    }
    return name;
}

function void Prog__EntryValues(Prog_State *state, const Prog_Entry *entry, String *values)
{
    Arena *frame = &state->frameArena;
    if (entry->drive) {
        for (u32 i = 0; i < Prog_ColumnId_Count; ++i) values[i] = (String){0};
        u64 total = entry->file.size, used = total - Min(entry->freeBytes, total);
        values[Prog_ColumnId_Name] = Prog_EntryName(state, entry);
        values[Prog_ColumnId_Type] = Prog_Tr(entry->type);
        values[Prog_ColumnId_FileSystem] = entry->fileSystem;
        if (total) {
            values[Prog_ColumnId_Size] = Prog_FormatSize(frame, total);
            values[Prog_ColumnId_FreeSpace] = Prog_FormatSize(frame, entry->freeBytes);
            values[Prog_ColumnId_PercentFull] = StrFormat(frame, "%.1f%%", (f64)used * 100.0 / (f64)total);
        }
        return;
    }
    String none = S("--");
    String folders = none, files = none, size = {0};
    P_DirectoryTotals live = {0};
    if (entry->file.kind == P_FileKind_Directory) {
        // Counts read short, as "172.5K"; a folder being measured shows what has been counted so far.
        b32 counting = !entry->measured && Prog__LiveTotals(state, entry, &live);
        u64 folderCount = counting ? live.folders : entry->folders, fileCount = counting ? live.files : entry->files, bytes = counting ? live.bytes : entry->bytes;
        if (entry->measured || counting) {
            if (folderCount) folders = Prog__FormatCount(frame, folderCount);
            if (fileCount) files = Prog__FormatCount(frame, fileCount);
            size = fileCount ? Prog_FormatSize(frame, bytes) : none;
        } else {
            folders = files = (String){0};
        }
    } else if (entry->file.metadataValid) {
        size = Prog_FormatSize(frame, entry->file.size);
    }
    // The share of the folder's or the drive's bytes, as a whole percent.
    u64 bytes = entry->file.kind == P_FileKind_Directory ? (entry->measured ? entry->bytes : 0) : entry->file.size;
    String folderUsage = {0}, volumeUsage = {0};
    if (state->listBytes && (entry->file.kind != P_FileKind_Directory || entry->measured)) folderUsage = StrFormat(frame, "%u%%", (u32)((f64)bytes * 100.0 / (f64)state->listBytes + 0.5));
    if (state->volumeBytes && (entry->file.kind != P_FileKind_Directory || entry->measured)) volumeUsage = StrFormat(frame, "%u%%", (u32)((f64)bytes * 100.0 / (f64)state->volumeBytes + 0.5));
    String created = entry->file.metadataValid && entry->file.createdTimeUnix ? P_FormatFileTime(frame, entry->file.createdTimeUnix) : (String){0};
    String accessed = entry->file.metadataValid && entry->file.accessedTimeUnix ? P_FormatFileTime(frame, entry->file.accessedTimeUnix) : (String){0};
    String result[Prog_ColumnId_Count] = { entry->file.name, Prog_Tr(entry->type), Prog_EntryModified(frame, entry), folders, files, size, created, accessed, folderUsage, volumeUsage };
    for (u32 i = 0; i < Prog_ColumnId_Count; ++i) values[i] = result[i];
    values[Prog_ColumnId_Name] = Prog_EntryName(state, entry);
    // A binned item shows where it came from and when it went.
    if (entry->original.length) values[Prog_ColumnId_OriginalLocation] = P_NativePath(frame, entry->original);
    if (entry->deletedTimeUnix) values[Prog_ColumnId_DateDeleted] = P_FormatFileTime(frame, entry->deletedTimeUnix);
    // A repository's branch and changes, with room at its front for the branch icon, for sizing the column to fit.
    const Prog_Repo *repo = state->repos.header.count && entry->file.kind == P_FileKind_Directory ? MapsGetPtrNull(state->repos, entry->file.name) : NULL;
    if (repo) values[Prog_ColumnId_Git] = repo->changes > 0 ? StrFormat(frame, "     %S  ~%d", repo->branch, repo->changes) : StrFormat(frame, "     %S", repo->branch);
}

// `text` cut at a codepoint boundary to fit `width`, with an ellipsis when anything was cut.
// `text` in at most `most` lines no wider than `width`, into `lines`: each broken after the last space, dash, dot or underscore that fits, or
// where the room runs out without one, and the last ellipsized while text is left over. Measured once through, as every visible icon's name
// is each frame.
function u32 Prog__WrapName(Arena *arena, const R_Font *font, String text, f32 width, String *lines, u32 most)
{
    u32 count = 0;
    for (u64 start = 0; start < text.length && count < most;) {
        String rest = StrFrom(text, start);
        if (count + 1 == most) {
            lines[count++] = Prog__Ellipsize(arena, font, rest, width);
            break;
        }
        u64 fit = 0, broken = 0;
        f32 taken = 0;
        for (u64 at = 0; at < rest.length;) {
            u64 next = at;
            StrDecodeUtf8(rest, &next);
            taken += R_TextWidth(font, StrFromTo(rest, at, (i64)next));
            if (taken > width) break;
            char c = rest.value[at];
            if (c == ' ' || c == '-' || c == '.' || c == '_') broken = next;
            fit = at = next;
        }
        if (fit == rest.length) {
            lines[count++] = rest;
            break;
        }
        u64 take = broken ? broken : Max(fit, (u64)1);
        String line = StrFromTo(rest, 0, (i64)take);
        while (line.length && line.value[line.length - 1] == ' ') --line.length;
        lines[count++] = line;
        start += take;
        while (start < text.length && text.value[start] == ' ') ++start;
    }
    return count;
}

function String Prog__Ellipsize(Arena *arena, const R_Font *font, String text, f32 width)
{
    if (R_TextWidth(font, text) <= width) return text;
    String dots = S("...");
    f32 room = width - R_TextWidth(font, dots);
    u64 fit = 0;
    // ponytail: measures every prefix; names are short, so this stays cheap.
    for (u64 at = 0; at < text.length;) {
        u64 next = at + 1;
        while (next < text.length && (text.value[next] & 0xC0) == 0x80) ++next;
        if (R_TextWidth(font, StrMake(text.value, next)) > room) break;
        fit = at = next;
    }
    return StrFormat(arena, "%S%S", StrMake(text.value, fit), dots);
}

function b32 Prog__EntryFaded(const Prog_Entry *entry);
function b32 Prog__EntryCut(const Prog_State *state, const Prog_Entry *entry);

// The color of a name git marks, as code editors show them: new in green, changed in amber and conflicting in red, darker on a light
// scheme.
function R_Color Prog__GitInk(Prog_GitMark mark)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    b32 light = theme->background.r + theme->background.g + theme->background.b > 1.5f;
    local_persist const u32 dark[Prog_GitMark_Count] = { 0, 0x73C991, 0x81B88B, 0xE2C08D, 0xE4676B };
    local_persist const u32 bright[Prog_GitMark_Count] = { 0, 0x2F8F3A, 0x3C7D45, 0x9A6B00, 0xC03030 };
    return Prog__HexColor((light ? bright : dark)[mark]);
}

// The color of an item's text: the content colors while it is selected or under the pointer, else its name's by its kind, and the
// secondary one for its other values and a hidden item's name; and the color of the letters a filter matched in it.
function R_Color Prog__ItemInk(const Prog_State *state, const Prog_Entry *entry, b32 hot, b32 name)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    if (entry->selected) return theme->contentSelection;
    if (hot) return theme->contentHover;
    if (!name || Prog__EntryFaded(entry) || Prog__EntryCut(state, entry)) return theme->textDim;
    Prog_GitMark mark = state->git.marks.header.count ? (Prog_GitMark)MapsGet(state->git.marks, u8, entry->file.name) : Prog_GitMark_None;
    if (mark) return Prog__GitInk(mark);
    // A file anyone may run, where the system says, in the scheme's green, its code strings' color, as terminals color them.
    if (entry->file.kind == P_FileKind_File && (entry->file.mode & 0111)) return theme->codeString;
    return entry->file.kind == P_FileKind_Directory ? theme->folder : theme->file;
}

function R_Color Prog__ItemMatch(const Prog_Entry *entry, b32 hot)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    return entry->selected ? theme->matchSelection : hot ? theme->matchHover : theme->textMatch;
}

// How an item's box shows: filled with the selection color when selected and the keyboard's item or pointed at, washed with it when selected otherwise or under the pointer,
// outlined under the pointer, and outlined as the keyboard's item.
typedef struct Prog_ItemLook { R_Color fill, border; UI_Flags flags; } Prog_ItemLook;

function Prog_ItemLook Prog__ItemLook(b32 selected, b32 hot, b32 focused)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog_ItemLook result = { .fill = theme->selection, .border = hot ? theme->selection : theme->focus };
    if (!selected || !(focused || hot)) result.fill.a *= theme->hoverAmount.r;
    if (selected || hot) result.flags |= UI_Flag_DrawBackground;
    if (!selected && (hot || focused)) result.flags |= UI_Flag_DrawBorder;
    return result;
}

// The line under each row of the list.
function f32 Prog__RowLine(const Prog_Metrics *m) { return m->separator; }

// A line-height item's box behind what comes after it in the current block, `width` wide, with the line under it.
function void Prog__ItemBox(Prog_ItemLook look, UI_Size width, const Prog_Metrics *m)
{
    f32 line = Prog__RowLine(m);
    if (look.flags) {
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(width);
        UI_SetNextPrefHeight(UI_Pixels(m->row - line));
        UI_SetNextCornerRadius(m->radius);
        UI_SetNextBorderThickness(m->stroke);
        UI_SetNextColors((UI_Colors){ .background = look.fill, .border = look.border });
        UI_AddBlock(UI_Flag_Floating | look.flags);
    }
    UI_SetNextFloatingOffset((UI_Vec2){ 0, m->row - line });
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(line));
    UI_SetNextColors((UI_Colors){ .background = Prog_CurrentTheme()->separator });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
}

// Whether the item is among the files cut to the clipboard, which are dimmed as hidden ones are until they are pasted.
function b32 Prog__EntryCut(const Prog_State *state, const Prog_Entry *entry)
{
    if (!state->cutPaths.header.count) return false;
    Scratch scratch = GetScratch();
    b32 result = MapsGet(state->cutPaths, b32, Prog_JoinPath(scratch.arena, state->directory.path, entry->file.name));
    ReleaseScratch(scratch);
    return result;
}

// Hidden and dot-named items are faded whether or not the OS marks them hidden.
function b32 Prog__EntryFaded(const Prog_Entry *entry)
{
    return entry->file.hidden || (entry->file.name.length && entry->file.name.value[0] == '.');
}

// The item's system icon drawn at `size` in a `width` x `height` slot, or a glyph while it loads or when it has none.
// Above the list icon size it is the item's thumbnail, with its type's icon at that size while the thumbnail loads, since a stretched list icon would blur.
// Where the lists' icons start in their slot.
function f32 Prog__ListIconInset(const Prog_Metrics *m) { return floorf(7.2f * m->scale + 0.5f); }

// The picture sits `inset` from the slot's left when that is not 0, else as `alignX` places it, as the fallback glyph always does.
function void Prog__ItemIcon(Prog_State *state, const Prog_Entry *entry, f32 size, f32 width, f32 height, f32 alignX, f32 inset, b32 highlighted, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    b32 faded = Prog__EntryFaded(entry) || Prog__EntryCut(state, entry);
    b32 thumbnail = size > (f32)state->iconSize;
    // A drive spinning up a disc shows a spinner until it answers.
    const P_Place *drive = entry->drive ? Prog_PathDrive(state, entry->file.name) : NULL;
    const Prog_Icon *spinner = drive && drive->busy ? Prog_SpinnerIcon(state, UI->nowMicroseconds) : NULL;
    if (spinner) UI_KeepAnimating();
    const Prog_Icon *icon = spinner ? spinner : !thumbnail ? Prog_FileIcon(state, state->directory.path, entry)
                          : Prog_ShowsThumbnails(state) ? Prog_ThumbnailIcon(state, state->directory.path, entry, (i32)size) : NULL;
    if (thumbnail && !icon) icon = Prog_ThumbnailStandIn(state, state->directory.path, entry, (i32)size);
    if (!thumbnail) Prog_WarmThumbnail(state, state->directory.path, entry);
    if (thumbnail && !icon) {
        Prog__Spacer(UI_Pixels(width), UI_Pixels(height));
    } else if (icon) {
        // A wide picture takes the room its cell has across, as long as it stays no taller than the icon size and its pixels go.
        R_Rect uv = icon->uv;
        UI_Vec2 drawn = { size, size };
        if (thumbnail && !spinner && icon->aspect > 1) {
            f32 room = width - 2 * floorf(8.0f * m->scale + 0.5f);
            drawn.x = floorf(MaxF32(size, MinF32(MinF32(room, size * icon->aspect), (f32)Prog__ThumbnailTier((i32)size))));
            drawn.y = floorf(drawn.x / icon->aspect + 0.5f);
            uv = icon->pictureUv;
        }
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextTextAlignment((UI_Vec2){ inset > 0 ? 0 : alignX, 0.5f });
        if (inset > 0) UI_SetNextChildrenPadding((UI_Padding){ inset, 0, 0, 0 });
        UI_SetNextImage(icon->texture, uv, drawn, spinner ? theme->icon : (R_Color){ 1, 1, 1, faded ? theme->hiddenAmount.r : 1 });
        UI_AddBlock(UI_Flag_DrawImage);
    } else {
        Prog__Icon(entry->file.kind == P_FileKind_Directory ? Icon_Folder : Icon_File, width, height,
                   highlighted ? theme->text : faded ? theme->iconDisabled : theme->icon, alignX, m);
    }
}

// A repository's cell in the Git column, from `x` to `limit`: the branch icon, the branch cut to fit, and the tracked files it has changed
// in the color of a changed file.
function void Prog__RepoTag(Prog_State *state, const Prog_Repo *repo, UI_Block *cell, f32 x, f32 limit, f32 height, R_Color ink, const Prog_Metrics *m)
{
    // After the branch: the tracked files changed, then the commits to push and to pull, each an arrow and a count, as the path bar shows them.
    Arena *frame = &state->frameArena;
    String count = repo->changes > 0 ? StrFormat(frame, "~%d", repo->changes) : (String){0};
    String ahead = repo->ahead > 0 ? StrFormat(frame, "%d", repo->ahead) : (String){0}, behind = repo->behind > 0 ? StrFormat(frame, "%d", repo->behind) : (String){0};
    String texts[7] = { Icon_GitBranch, repo->branch, count, ahead.length ? Icon_ArrowUp : (String){0}, ahead, behind.length ? Icon_ArrowDown : (String){0}, behind };
    const R_Font *fonts[7] = { m->iconsSmall, m->detail, m->detail, m->iconsSmall, m->detail, m->iconsSmall, m->detail };
    R_Color match = Prog_CurrentTheme()->textMatch;
    R_Color inks[7] = { ink, ink, Prog__GitInk(Prog_GitMark_Modified), match, match, match, match };
    f32 snug = floorf(3.2f * m->scale + 0.5f), apart = floorf(6.4f * m->scale + 0.5f);
    f32 befores[7] = { 0, snug, apart, apart, 0, apart, 0 }, widths[7], others = 0;
    for (u32 i = 0; i < 7; ++i) {
        widths[i] = texts[i].length ? R_TextWidth(fonts[i], texts[i]) : 0;
        if (i != 1 && texts[i].length) others += befores[i] + widths[i];
    }
    f32 room = limit - x - others - befores[1];
    if (!repo->branch.length || room <= 0) return;
    if (widths[1] > room) {
        texts[1] = Prog__Ellipsize(frame, m->detail, texts[1], room);
        widths[1] = R_TextWidth(m->detail, texts[1]);
    }
    UI_Parent(cell) {
        for (u32 i = 0; i < 7; ++i) {
            if (!texts[i].length) continue;
            x += befores[i];
            UI_SetNextFloatingOffset((UI_Vec2){ x, 0 });
            UI_SetNextPrefWidth(UI_Pixels(widths[i]));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextFont(fonts[i]);
            UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextColors((UI_Colors){ .text = inks[i] });
            UI_SetNextText(texts[i]);
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
            x += widths[i];
        }
    }
}

// What every view does with an item under the pointer: a right press opens the menu, presses select, and drags move the files.
// A drag that began away from the item's icon and name (`onName` false) instead asks, through the result, for a box selection.
function b32 Prog__ItemInput(Prog_State *state, u64 index, UI_Block *block, UI_Key key, b32 onName, b32 renaming)
{
    Prog_Entry *entry = &state->directory.entries[index];
    if (state->hasFocus && state->focusedRow == index) state->focusedRowRect = block->rect;
    if (UI->hotKey == key && !state->loading) {
        state->hoverRow = index;
        state->hoverPanel = Prog__PanelId(state);
        state->hasHoverRow = true;
    }
    UI_Interaction interaction = UI_InteractionFromBlock(block);
    // A plain press inside a multi-selection keeps it so that it can be dragged; the click narrows it, unless the press became a box selection.
    if (interaction.pressed && state->marqueeKey == key) state->marqueeKey = 0;
    b32 keepSelection = entry->selected && state->selectedCount > 1 && !interaction.mods && interaction.pressCount == 1;
    b32 result = false;
    // A plain click on the name of the item that alone is selected and focused renames it once the double-click time passes without another
    // press, as in Explorer; a drag or a double click does not.
    if (interaction.pressed) {
        b32 slow = interaction.button == MouseButton_Left && interaction.pressCount == 1 && !interaction.mods && onName && !renaming && !state->singleClick && !state->noClickRename &&
                   entry->selected && state->selectedCount == 1 && state->hasFocus && state->focusedRow == index;
        state->listSlowRow = slow ? index + 1 : 0;
    }
    if (interaction.clicked && interaction.button == MouseButton_Left && state->listSlowRow == index + 1 && state->marqueeKey != key) {
        state->listRenameAt = UI->nowMicroseconds + (UI->doubleClickMicroseconds ? UI->doubleClickMicroseconds : UINT64_C(500000));
        state->listRenamePress = UI->lastPressTime;
        state->listRenamePanel = Prog__PanelId(state);
        state->listRenameListing = state->listing;
        state->listRenameRow = index;
        state->listSlowRow = 0;
    }
    // Clicks inside the name being edited stay in the box instead of reopening or dragging the item.
    if (renaming && UI__Contains(state->renameRect, UI->mouse)) {
    } else if (!state->loading && interaction.pressed && interaction.button == MouseButton_Right) {
        Prog_QueueCommand(state, Prog_CommandKind_ContextMenuAt, index, interaction.pressCount, interaction.pressOrder);
    } else if (!state->loading && state->singleClick && interaction.clicked && interaction.button == MouseButton_Left && !interaction.mods &&
               state->marqueeKey != key && (!state->singleClickFoldersOnly || entry->file.kind == P_FileKind_Directory)) {
        // A single click opens it, as a double click would.
        Prog_QueueCommand(state, Prog_CommandKind_SelectRow, index, 2, interaction.clickOrder);
    } else if (!state->loading && (((state->actOnRelease ? interaction.clicked : interaction.pressed) && !keepSelection) ||
                                   (interaction.clicked && keepSelection && interaction.button == MouseButton_Left && state->marqueeKey != key))) {
        b32 onPress = interaction.pressed && !state->actOnRelease;
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SelectRow, index, interaction.pressCount, onPress ? interaction.pressOrder : interaction.clickOrder);
        command->mods = interaction.mods;
    } else if (!state->loading && state->hoverToSelect && UI->hotKey == key && !UI->activeKey && !entry->selected && !state->marquee) {
        Prog_QueueCommand(state, Prog_CommandKind_SelectRow, index, 1, 0);
    }
    if (!state->loading && !renaming && interaction.active && interaction.isDragging && interaction.button == MouseButton_Left) {
        if (!onName) result = !state->marquee;
        else if (entry->selected) Prog_QueueCommand(state, Prog_CommandKind_DragSelection, index, 0, 0);
    }
    return result;
}

function b32 Prog__ItemRenaming(const Prog_State *state, const Prog_Entry *entry)
{
    return state->renaming && StrIsEqual(entry->file.name, StrMake((const char *)state->renameFrom, state->renameFromLength));
}

// A usage column's cell: a thin bar filled to the percent `value` holds, and the percent after it.
function void Prog__UsageCell(String value, f32 width, f32 height, f32 left, f32 right, R_Color color, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 percent = 0;
    for (u64 i = 0; i < value.length && value.value[i] >= '0' && value.value[i] <= '9'; ++i) percent = percent * 10 + (f32)(value.value[i] - '0');
    f32 text = R_TextWidth(m->detail, S("100%")), gap = floorf(4.8f * m->scale + 0.5f);
    f32 bar = MaxF32(0, width - left - right - text - gap), thick = floorf(4.0f * m->scale + 0.5f);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ left, 0, right, 0 });
    UI_Block *cell = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(cell) {
        if (value.length && bar > 0) {
            UI_SetNextPrefWidth(UI_Pixels(bar));
            UI_SetNextPrefHeight(UI_Pixels(thick));
            UI_SetNextCornerRadius(thick * 0.5f);
        UI_SetNextColors((UI_Colors){ .background = theme->track });
            UI_Block *track = UI_AddBlock(UI_Flag_DrawBackground);
            UI_Parent(track) {
                UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                UI_SetNextPrefWidth(UI_Pixels(MaxF32(thick, floorf(bar * MinF32(percent, 100) / 100.0f))));
                UI_SetNextPrefHeight(UI_Pixels(thick));
                UI_SetNextCornerRadius(thick * 0.5f);
                UI_SetNextColors((UI_Colors){ .background = theme->selection });
                if (percent > 0) UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
            Prog__Spacer(UI_Pixels(gap), UI_Pixels(height));
        }
        UI_SetNextFont(m->detail);
        Prog__LabelPadded(value, UI_Flex(), height, 0, 0, color, 1);
    }
}

// The inspector's scroll bar at the right edge of `view`, a block in the current parent: a thumb as long as the share in sight, where that is,
// between arrows that scroll three rows of `pitch`, dimmed at their end.
function void Prog__ScrollBar(UI_Block *view, f32 pitch, String name, f32 opacity, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale;
    f32 viewHeight = view->rect.y1 - view->rect.y0, content = view->contentSize.y;
    if (content > viewHeight && viewHeight > 0) {
        f32 head = floorf(31.2f * s + 0.5f), foot = floorf(13.6f * s + 0.5f), track = MaxF32(0, viewHeight - head - foot);
        f32 thumb = MaxF32(floorf(track * viewHeight / content), floorf(16.0f * s + 0.5f)), thick = floorf(11.2f * s + 0.5f);
        f32 at = floorf((track - thumb) * MinF32(1, view->scrollShown.y / (content - viewHeight)));
        f32 x = view->rect.x1 - UI->parent->rect.x0 - thick - floorf(2.4f * s + 0.5f), y = view->rect.y0 - UI->parent->rect.y0;
        UI_SetNextFloatingOffset((UI_Vec2){ x, y + head + at });
        UI_SetNextPrefWidth(UI_Pixels(thick));
        UI_SetNextPrefHeight(UI_Pixels(thumb));
        UI_SetNextCornerRadius(floorf(thick * 0.5f));
        UI_SetNextOpacity(opacity);
        UI_SetNextColors((UI_Colors){ .background = theme->track });
        UI_Block *bar = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(UI->frameArena, "%S/ScrollThumb", name)), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_LeftMouseClickable);
        // Dragged, the thumb follows the pointer from where it was taken, and the view scrolls with it at once.
        local_persist f32 grab;
        UI_Interaction drag = UI_InteractionFromBlock(bar);
        if (drag.pressed) grab = UI->mouse.y - (view->rect.y0 + head + at);
        if (drag.active && track > thumb) {
            f32 share = MinF32(MaxF32((UI->mouse.y - grab - view->rect.y0 - head) / (track - thumb), 0), 1);
            view->scrollOffset.y = view->scrollShown.y = floorf(share * (content - viewHeight) + 0.5f);
        }
        f32 size = floorf(16.0f * s + 0.5f);
        for (u32 down = 0; down < 2; ++down) {
            b32 end = down ? view->scrollOffset.y >= content - viewHeight - 0.5f : view->scrollOffset.y <= 0.5f;
            UI_Key key = UI_KeyFromStr(0, StrFormat(UI->frameArena, down ? "%S/ScrollDown" : "%S/ScrollUp", name));
            UI_SetNextFloatingOffset((UI_Vec2){ x + floorf((thick - size) * 0.5f), down ? y + viewHeight - floorf(10.4f * s + 0.5f) - floorf(size * 0.5f) : y + floorf(20.0f * s + 0.5f) - floorf(size * 0.5f) });
            UI_SetNextPrefWidth(UI_Pixels(size));
            UI_SetNextPrefHeight(UI_Pixels(size));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextFont(m->iconsSmall);
            UI_SetNextOpacity(opacity);
            UI_SetNextColors((UI_Colors){ .text = end ? theme->tabBorder : UI->hotKey == key ? theme->text : theme->icon });
            UI_SetNextText(down ? Icon_ChevronDown : Icon_ChevronUp);
            UI_Block *arrow = UI_AddBlockFromKey(key, UI_Flag_Floating | UI_Flag_DrawText | UI_Flag_LeftMouseClickable);
            if (UI_InteractionFromBlock(arrow).clicked) view->scrollOffset.y = MinF32(MaxF32(view->scrollOffset.y + (down ? 3 : -3) * pitch, 0), content - viewHeight);
        }
    }
}

// The room on the right of a list that scrolls, for its scroll bar.
function f32 Prog__ScrollGutter(const Prog_Metrics *m) { return floorf(12.8f * m->scale + 0.5f); }

// A drive's fullness in This PC: an outlined bar as wide as the column, filled to the percent `value` holds,
// with the percent at the fill's end.
function void Prog__FullnessCell(String value, f32 width, f32 height, f32 left, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 percent = 0, fraction = 0;
    for (u64 i = 0; i < value.length; ++i) {
        char c = value.value[i];
        if (c == '.') fraction = 0.1f;
        else if (c < '0' || c > '9') break;
        else if (fraction > 0) { percent += (f32)(c - '0') * fraction; fraction *= 0.1f; }
        else percent = percent * 10 + (f32)(c - '0');
    }
    f32 tall = floorf(17.6f * m->scale + 0.5f), bar = MaxF32(0, width - left - floorf(8.0f * m->scale + 0.5f));
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ left, 0, 0, 0 });
    UI_Block *cell = UI_AddBlock(0);
    if (!value.length) return;
    UI_Parent(cell) {
        UI_SetNextPrefWidth(UI_Pixels(bar));
        UI_SetNextPrefHeight(UI_Pixels(tall));
        UI_SetNextBorderThickness(MaxF32(1, floorf(0.8f * m->scale + 0.5f)));
        UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->outline });
        UI_Block *track = UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip);
        UI_Parent(track) {
            f32 fill = floorf(bar * MinF32(percent, 100) / 100.0f);
            UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
            UI_SetNextPrefWidth(UI_Pixels(fill));
            UI_SetNextPrefHeight(UI_Pixels(tall));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 1, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ 0, 0, floorf(6.4f * m->scale + 0.5f), 0 });
            UI_SetNextColors((UI_Colors){ .background = theme->selection });
            UI_Block *filled = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            UI_Parent(filled) {
                UI_SetNextFont(m->detail);
                Prog__LabelPadded(value, UI_TextContent(), tall, 0, 0, theme->text, 1);
            }
        }
    }
}

// In place of a selected item's icon while several are selected, when asked: a checked box that takes it out of the selection.
function b32 Prog__ItemCheckbox(Prog_State *state, u64 index, f32 width, f32 height, const Prog_Metrics *m)
{
    const Prog_Entry *entry = &state->directory.entries[index];
    if (!state->showCheckboxes || !entry->selected || state->selectedCount < 2 || state->panels[state->currentPanel].inspectorOf) return false;
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 box = floorf(16.0f * m->scale + 0.5f);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenAlignment((UI_Vec2){ 1, 0.5f });
    UI_Parent(UI_AddBlock(0)) {
        UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Check/%S", entry->file.name));
        UI_SetNextPrefWidth(UI_Pixels(box));
        UI_SetNextPrefHeight(UI_Pixels(box));
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_SetNextCornerRadius(floorf(3.2f * m->scale + 0.5f));
        UI_SetNextBorderThickness(m->stroke);
        UI_SetNextFont(m->icons);
        UI_SetNextColors((UI_Colors){ .border = theme->text, .text = theme->text });
        UI_SetNextText(Icon_Check);
        UI_Block *check = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_DrawBorder | UI_Flag_DrawText);
        UI_Interaction toggle = UI_InteractionFromBlock(check);
        if (toggle.clicked && !state->loading) Prog_QueueCommand(state, Prog_CommandKind_SelectRow, index, 1, toggle.clickOrder)->mods = KeyMod_Control;
    }
    return true;
}

// A Details or List row; List shows only the icon and the name.
function b32 Prog__Row(Prog_State *state, u64 index, u64 position, UI_Key key, const Prog_Columns *columns, b32 nameOnly, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog_Entry *entry = &state->directory.entries[index];
    b32 focused = state->hasFocus && state->focusedRow == index;
    b32 hot = UI->hotKey == key && !state->loading;
    b32 highlighted = entry->selected || hot;
    f32 height = m->row - Prog__RowLine(m);
    // Every other row tinted, when asked.
    b32 tinted = state->alternatingRows && (position & 1);
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = theme->alternateRow });
    UI_Block *row = UI_AddBlockFromKey(key, (tinted ? UI_Flag_DrawBackground : 0) | (!state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0));
    b32 renaming = Prog__ItemRenaming(state, entry);
    // A tree's item stands in by its depth, after an arrow that unfolds a folder; the name shows without the folders it is in.
    b32 tree = state->directory.tree;
    u64 depth = 0;
    for (u64 c = 0; tree && c < entry->file.name.length; ++c) depth += entry->file.name.value[c] == '/';
    f32 arrow = tree ? floorf(16.0f * m->scale + 0.5f) : 0, treeIndent = tree ? (f32)depth * floorf(16.0f * m->scale + 0.5f) + arrow : 0;
    // Presses on the icon and the name drag the files; elsewhere on the row they draw a selection box, as in Explorer.
    b32 onName = UI->pressOrigin.x < row->rect.x0 + (Prog__MarksRecents(state) ? Prog__RecentGutter(m->scale) : 0) + treeIndent + m->iconSlot +
                                     R_TextWidth(m->font, tree ? Prog_EntryLeaf(entry->file.name) : entry->file.name) + m->gap;
    b32 result = Prog__ItemInput(state, index, row, key, onName, renaming);
    String values[Prog_ColumnId_Count];
    Prog__EntryValues(state, entry, values);
    if (tree) values[Prog_ColumnId_Name] = Prog_EntryLeaf(values[Prog_ColumnId_Name]);
    P_DirectoryTotals live;
    b32 counting = entry->file.kind == P_FileKind_Directory && !entry->measured && Prog__LiveTotals(state, entry, &live) && !live.complete;
    UI_Parent(row) {
        Prog__ItemBox(Prog__ItemLook(entry->selected, hot, focused), UI_Percent(1), m);
        if (Prog__MarksRecents(state) && entry->recent) Prog__Icon(Icon_Clock, Prog__RecentGutter(m->scale), height, theme->text, 0.5f, m);
        else if (Prog__MarksRecents(state)) Prog__Spacer(UI_Pixels(Prog__RecentGutter(m->scale)), UI_Pixels(height));
        if (tree) {
            Prog__Spacer(UI_Pixels(treeIndent - arrow), UI_Pixels(height));
            if (entry->file.kind == P_FileKind_Directory) {
                UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "TreeArrow/%S", entry->file.name));
                UI_SetNextPrefWidth(UI_Pixels(arrow));
                UI_SetNextPrefHeight(UI_Pixels(height));
                UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                UI_SetNextFont(m->iconsSmall);
                UI_SetNextColors((UI_Colors){ .text = UI->hotKey == key ? theme->text : theme->icon });
                UI_SetNextText(Prog_TreeIsOpen(state, entry->file.name) ? Icon_ChevronDown : Icon_ChevronRight);
                UI_Interaction unfold = UI_InteractionFromBlock(UI_AddBlockFromKey(key, UI_Flag_DrawText | UI_Flag_LeftMouseClickable));
                if (unfold.clicked) Prog_QueueCommand(state, Prog_CommandKind_TreeToggle, index, 0, unfold.clickOrder);
            } else {
                Prog__Spacer(UI_Pixels(arrow), UI_Pixels(height));
            }
        }
        if (!Prog__ItemCheckbox(state, index, m->iconSlot - m->gap, height, m)) Prog__ItemIcon(state, entry, (f32)state->iconSize, m->iconSlot - m->gap, height, 1, Prog__ListIconInset(m), highlighted, m);
        for (u32 c = 0; c < (nameOnly ? 1u : columns->count); ++c) {
            const Prog_Column *column = &columns->v[c];
            u32 i = nameOnly ? (u32)Prog_ColumnId_Name : column->id;
            // The icon before the name is inside Name's width, as the header counts it, and so is a tree's depth.
            f32 columnWidth = i == Prog_ColumnId_Name ? MaxF32(0, column->width - (m->iconSlot - m->gap) - treeIndent) : column->width;
            UI_Size width = column->width > 0 && !nameOnly ? UI_Pixels(columnWidth) : UI_Flex();
            if (i == Prog_ColumnId_Name && renaming) {
                Prog__RenameBox(state, width, false, m);
                continue;
            }
            R_Color color = Prog__ItemInk(state, entry, hot, i == Prog_ColumnId_Name);
            if (i == Prog_ColumnId_Name && (state->filter.length || state->directory.expanded)) {
                String leaf = Prog_EntryLeaf(entry->file.name);
                u64 prefix = entry->file.name.length - leaf.length;
                u8 *marks = state->filter.length ? ArenaPushArrayNoZero(&state->frameArena, Max(entry->file.name.length, (u64)1), u8) : NULL;
                if (marks) Prog_EntryScore(entry->file.name, Prog_EditText(&state->filter), marks);
                // A tree shows where an item is by its depth instead.
                if (tree) Prog__NameLabel(leaf, 0, marks ? marks + prefix : NULL, width, height, color, theme->textDim, Prog__ItemMatch(entry, hot), m);
                else Prog__NameLabel(entry->file.name, prefix, marks, width, height, color, theme->textDim, Prog__ItemMatch(entry, hot), m);
                continue;
            }
            // Values too wide for a fixed column end in an ellipsis.
            f32 left = Prog__ColumnLeft(i, m), right = Prog__ColumnRight(i, m);
            // Right-aligned values may run into the space before them.
            if (column->alignX > 0) left = m->gap;
            String value = values[i];
            // A date too wide for its column drops its time before it is cut.
            b32 date = i == Prog_ColumnId_Modified || i == Prog_ColumnId_Created || i == Prog_ColumnId_Accessed;
            Index space = StrIndexChar(value, ' ', SSF_None);
            if (date && column->width > 0 && space.exists && R_TextWidth(m->detail, value) > columnWidth - left - right) value = StrFromTo(value, 0, (i64)space.value);
            if (column->width > 0 && !nameOnly) value = Prog__Ellipsize(&state->frameArena, i == Prog_ColumnId_Name ? m->font : m->detail, value, columnWidth - left - right);
            if (i != Prog_ColumnId_Name) UI_SetNextFont(m->detail);
            // A usage is a bar filled to its share, before the percent; a drive's fullness is a tall bar with the percent in it.
            if (i == Prog_ColumnId_FolderUsage || i == Prog_ColumnId_VolumeUsage) {
                Prog__UsageCell(value, column->width, height, left, right, color, m);
                continue;
            }
            if (i == Prog_ColumnId_PercentFull) {
                Prog__FullnessCell(values[i], column->width, height, Prog__ColumnLeft(i, m), m);
                continue;
            }
            if (i == Prog_ColumnId_Git) {
                Prog__LabelPadded((String){0}, width, height, left, right, color, 0);
                const Prog_Repo *repo = state->repos.header.count && entry->file.kind == P_FileKind_Directory ? MapsGetPtrNull(state->repos, entry->file.name) : NULL;
                if (repo) Prog__RepoTag(state, repo, UI->parent->last, 0, columnWidth - left - right, height, color, m);
                continue;
            }
            Prog__LabelPadded(value, width, height, left, right, color, column->alignX);
            // In single-click mode names are links: underlined under the pointer, or all of them when asked.
            if (i == Prog_ColumnId_Name && state->singleClick && (hot || state->underlineAlways) &&
                (!state->singleClickFoldersOnly || entry->file.kind == P_FileKind_Directory)) {
                UI_Block *cell = UI->parent->last;
                f32 thick = MaxF32(1, floorf(0.8f * m->scale + 0.5f));
                UI_Parent(cell) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((height + m->font->lineHeight) * 0.5f) });
                    UI_SetNextPrefWidth(UI_Pixels(R_TextWidth(m->font, value)));
                    UI_SetNextPrefHeight(UI_Pixels(thick));
                    UI_SetNextColors((UI_Colors){ .background = color });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                }
            }
            // Totals still being counted are underlined.
            b32 total = i == Prog_ColumnId_Folders || i == Prog_ColumnId_Files || i == Prog_ColumnId_Size;
            if (counting && total && value.length && !StrIsEqual(value, S("--"))) {
                UI_Block *cell = UI->parent->last;
                f32 textWidth = R_TextWidth(m->detail, value), cellWidth = column->width, thick = MaxF32(1, floorf(0.8f * m->scale + 0.5f));
                UI_Parent(cell) {
                    // Offsets count from inside the cell's padding.
                    UI_SetNextFloatingOffset((UI_Vec2){ column->alignX > 0 ? cellWidth - left - right - textWidth : 0, floorf((height + m->detail->lineHeight) * 0.5f) });
                    UI_SetNextPrefWidth(UI_Pixels(textWidth));
                    UI_SetNextPrefHeight(UI_Pixels(thick));
                    UI_SetNextColors((UI_Colors){ .background = color });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                }
            }
        }
    }
    return result;
}

// A Columns cell: a row-height item, its icon and name, on a background that alternates by line.
function b32 Prog__ColumnCell(Prog_State *state, u64 index, u64 line, UI_Key key, f32 width, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog_Entry *entry = &state->directory.entries[index];
    b32 focused = state->hasFocus && state->focusedRow == index;
    b32 hot = UI->hotKey == key && !state->loading;
    b32 highlighted = entry->selected || hot;
    f32 height = m->row - Prog__RowLine(m);
    b32 tinted = state->alternatingRows && (line & 1);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = theme->alternateRow });
    UI_Block *cell = UI_AddBlockFromKey(key, (tinted ? UI_Flag_DrawBackground : 0) | (!state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0));
    b32 renaming = Prog__ItemRenaming(state, entry);
    b32 onName = UI->pressOrigin.x < cell->rect.x0 + m->iconSlot + R_TextWidth(m->font, entry->file.name) + m->gap;
    b32 result = Prog__ItemInput(state, index, cell, key, onName, renaming);
    UI_Parent(cell) {
        Prog__ItemBox(Prog__ItemLook(entry->selected, hot, focused), UI_Pixels(width), m);
        if (!Prog__ItemCheckbox(state, index, m->iconSlot - m->gap, height, m)) Prog__ItemIcon(state, entry, (f32)state->iconSize, m->iconSlot - m->gap, height, 1, Prog__ListIconInset(m), highlighted, m);
        if (renaming) Prog__RenameBox(state, UI_Flex(), false, m);
        else Prog__Label(Prog_EntryName(state, entry), UI_Flex(), height, m->gap, Prog__ItemInk(state, entry, hot, true), 0);
    }
    return result;
}

// The height of a drive's fullness bar in an icon view, which grows a little with the icons.
function f32 Prog__DriveBarHeight(f32 iconSize, const Prog_Metrics *m)
{
    return floorf((15.2f + 0.045f * (iconSize / m->scale - 48.0f)) * m->scale + 0.5f);
}

// How much taller a drive's icon view cell is than a file's, for the bar between its name and its free space.
function f32 Prog__DriveCellExtra(f32 iconSize, const Prog_Metrics *m)
{
    return floorf(10.4f * m->scale + 0.5f) + Prog__DriveBarHeight(iconSize, m);
}

// An icon view cell: the icon over the name and the size, centered, on a rounded box that shows only when selected, hovered or focused.
// A drive's shows how full it is instead of its size, as a bar over its free space.
// How many lines a name takes in the icon views at most.
#define Prog_IconNameLines 2

function void Prog__IconCell(Prog_State *state, u64 index, UI_Key key, f32 width, f32 height, f32 iconSize, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog_Entry *entry = &state->directory.entries[index];
    b32 focused = state->hasFocus && state->focusedRow == index;
    b32 hot = UI->hotKey == key && !state->loading;
    b32 highlighted = entry->selected || hot;
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0 });
    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf(9.0f * m->scale) + floorf(8.0f * m->scale) - floorf(4.8f * m->scale + 0.5f), 0, 0 });
    UI_SetNextCornerRadius(m->radius * 2);
    Prog_ItemLook look = Prog__ItemLook(entry->selected, hot, focused);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .background = look.fill, .border = look.border });
    UI_Block *cell = UI_AddBlockFromKey(key, look.flags | (!state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0));
    b32 renaming = Prog__ItemRenaming(state, entry);
    // The whole cell is the item, so presses on it drag the files; the box selection starts from the space between cells.
    Prog__ItemInput(state, index, cell, key, true, renaming);
    String values[Prog_ColumnId_Count];
    Prog__EntryValues(state, entry, values);
    f32 text = width - m->gap;
    UI_Parent(cell) {
        Prog__ItemIcon(state, entry, iconSize, width, iconSize, 0.5f, 0, highlighted, m);
        // While several are selected, when asked, a checked box in the cell's corner takes it out of the selection, as the rows' does.
        f32 box = floorf(16.0f * m->scale + 0.5f), corner = floorf(6.4f * m->scale + 0.5f);
        UI_SetNextFloatingOffset((UI_Vec2){ corner, corner });
        UI_SetNextPrefWidth(UI_Pixels(box));
        UI_SetNextPrefHeight(UI_Pixels(box));
        UI_Parent(UI_AddBlock(UI_Flag_Floating)) Prog__ItemCheckbox(state, index, box, box, m);
        if (renaming) {
            Prog__RenameBox(state, UI_Pixels(width), true, m);
        } else if (entry->drive) {
            u64 total = entry->file.size;
            Prog__Spacer(UI_Pixels(width), UI_Pixels(floorf(5.6f * m->scale + 0.5f)));
            Prog__Label(Prog__Ellipsize(&state->frameArena, m->font, Prog_EntryName(state, entry), text), UI_Pixels(width), m->line, 0, theme->text, 0.5f);
            if (total) {
                Prog__Spacer(UI_Pixels(width), UI_Pixels(floorf(4.8f * m->scale + 0.5f)));
                f32 bar = MaxF32(0, width - 2 * floorf(12.8f * m->scale + 0.5f)), tall = Prog__DriveBarHeight(iconSize, m);
                UI_SetNextPrefWidth(UI_Pixels(bar));
                UI_SetNextPrefHeight(UI_Pixels(tall));
                UI_SetNextBorderThickness(m->stroke);
                UI_SetNextCornerRadius(floorf(1.6f * m->scale + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = theme->track, .border = theme->outline });
                UI_Block *track = UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip);
                UI_Parent(track) {
                    u64 used = total - Min(entry->freeBytes, total);
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                    UI_SetNextPrefWidth(UI_Pixels(floorf(bar * (f32)((f64)used / (f64)total))));
                    UI_SetNextPrefHeight(UI_Pixels(tall));
                    UI_SetNextColors((UI_Colors){ .background = theme->accent });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                }
                Prog__Spacer(UI_Pixels(width), UI_Pixels(floorf(3.2f * m->scale + 0.5f)));
                String free = values[Prog_ColumnId_FileSystem].length
                            ? StrFormat(&state->frameArena, TrF("%S free of %S (%S)"), values[Prog_ColumnId_FreeSpace], values[Prog_ColumnId_Size], values[Prog_ColumnId_FileSystem])
                            : StrFormat(&state->frameArena, TrF("%S free of %S"), values[Prog_ColumnId_FreeSpace], values[Prog_ColumnId_Size]);
                UI_SetNextFont(m->detail);
                Prog__Label(Prog__Ellipsize(&state->frameArena, m->detail, free, text), UI_Pixels(width), m->line, 0, theme->textDim, 0.5f);
            }
        } else {
            // A long name takes a second line, as in Explorer.
            String lines[Prog_IconNameLines];
            u32 count = Prog__WrapName(&state->frameArena, m->font, Prog_EntryName(state, entry), text, lines, Prog_IconNameLines);
            for (u32 i = 0; i < count; ++i) Prog__Label(lines[i], UI_Pixels(width), m->line, 0, Prog__ItemInk(state, entry, hot, true), 0.5f);
            Prog__Label(Prog__Ellipsize(&state->frameArena, m->font, values[Prog_ColumnId_Size], text), UI_Pixels(width), m->line, 0, theme->textDim, 0.5f);
        }
    }
}

// A dashed outline around the current parent, `width` x `height`, its dashes starting `inset` from the corners.
function void Prog__DashedOutline(f32 width, f32 height, f32 inset, R_Color color, const Prog_Metrics *m)
{
    f32 thickness = MaxF32(1, floorf(m->scale));
    f32 dash = floorf(5.0f * m->scale + 0.5f), gap = floorf(2.5f * m->scale + 0.5f);
    for (u32 edge = 0; edge < 4; ++edge) {
        b32 horizontal = edge < 2;
        f32 length = (horizontal ? width : height) - inset;
        for (f32 at = inset; at < length; at += dash + gap) {
            f32 piece = MinF32(dash, length - at);
            UI_Vec2 offset = horizontal ? (UI_Vec2){ at, edge == 0 ? 0 : height - thickness } : (UI_Vec2){ edge == 2 ? 0 : width - thickness, at };
            UI_SetNextFloatingOffset(offset);
            UI_SetNextPrefWidth(UI_Pixels(horizontal ? piece : thickness));
            UI_SetNextPrefHeight(UI_Pixels(horizontal ? thickness : piece));
            UI_SetNextColors((UI_Colors){ .background = color });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
    }
}

// The selection box: a faint fill with a dashed outline, in the list's padded coordinates.
function void Prog__Marquee(R_Rect box, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Color fill = theme->marquee;
    fill.a *= 0.16f;
    f32 width = box.x1 - box.x0, height = box.y1 - box.y0;
    UI_SetNextFloatingOffset((UI_Vec2){ box.x0, box.y0 });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextColors((UI_Colors){ .background = fill });
    UI_Block *area = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    UI_Parent(area) Prog__DashedOutline(width, height, 0, theme->marquee, m);
}

function Prog_View Prog_ZoomView(u32 zoom)
{
    return zoom < Prog_ZoomIcons ? (Prog_View)Min(zoom / Prog_ZoomList, (u32)Prog_View_Columns) : Prog_View_Icons;
}

// Zoom as a fraction: 0 for Details up to 1 for XL Icons.
function f32 Prog__ZoomFraction(u32 zoom) { return MinF32(1, (f32)zoom / (f32)Prog_ZoomMax); }

// The view's name, as the status bar and the zoom popup call it.
function String Prog__ZoomName(u32 zoom)
{
    local_persist const String names[Prog_View_Count] = { TrInit("Details"), TrInit("List"), TrInit("Columns"), {0} };
    Prog_View view = Prog_ZoomView(zoom);
    return view != Prog_View_Icons ? Prog_Tr(names[view]) : zoom < Prog_ZoomLarge ? Tr("M Icons") : zoom < Prog_ZoomMax ? Tr("L Icons") : Tr("XL Icons");
}

// Icon views draw their icons from 48 logical pixels at 30% to 205 at 100%, in physical pixels.
function f32 Prog__ZoomIconSize(u32 zoom, f32 scale)
{
    f32 t = (Prog__ZoomFraction(zoom) - 0.3f) / 0.7f;
    return floorf((48.0f + t * (204.8f - 48.0f)) * scale + 0.5f);
}

// A grouped list's heading: its name and how many items it has, before a line, which a press folds the group away or shows again.
function void Prog__GroupHeading(Prog_State *state, UI_Key seed, u32 index, f32 height, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const Prog_ListGroup *group = &state->groups[index];
    UI_Key key = UI_KeyFromStr(seed, group->label);
    b32 hot = UI->hotKey == key && !state->loading;
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ 0, 0, m->gap, 0 });
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = theme->alternateRow });
    UI_Block *heading = UI_AddBlockFromKey(key, (!state->loading ? UI_Flag_LeftMouseClickable : 0) | (hot ? UI_Flag_DrawBackground : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(heading);
    if (interaction.clicked && interaction.button == MouseButton_Left) Prog_QueueCommand(state, Prog_CommandKind_GroupToggle, index, 0, interaction.clickOrder);
    UI_Parent(heading) {
        R_Color color = hot ? theme->text : theme->textHeading;
        // Folded as soon as it is clicked, while its items ease shut.
        Prog__Icon(!Prog__GroupFolded(state, group->label) || !group->total ? Icon_ChevronDown : Icon_ChevronRight, m->iconSlot, height, color, 0.5f, m);
        Prog__Label(Prog_Tr(group->label), UI_TextContent(), height, 0, color, 0);
        Prog__Label(StrFormat(&state->frameArena, "%" PRIu64, group->total), UI_TextContent(), height, m->gap, theme->textDim, 0);
        UI_Block *rule = Prog__Box(UI_Flex(), UI_Pixels(height), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(rule) {
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->stroke));
            UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
            UI_AddBlock(UI_Flag_DrawBackground);
        }
    }
}

function void Prog__List(Prog_State *state, const Prog_Columns *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    // The rename a slow click asked for, unless another press came, the listing changed or the item is no longer the one focused.
    if (state->listRenameAt && state->listRenamePanel == Prog__PanelId(state)) {
        b32 stale = UI->lastPressTime != state->listRenamePress || state->listing != state->listRenameListing || state->renaming ||
                    !state->hasFocus || state->focusedRow != state->listRenameRow;
        if (stale) {
            state->listRenameAt = 0;
        } else if (UI->nowMicroseconds >= state->listRenameAt) {
            Prog_QueueCommand(state, Prog_CommandKind_Rename, 0, 0, 0);
            state->listRenameAt = 0;
        } else {
            UI_KeepAnimating();
        }
    }
    // The usage columns' totals, while one of them shows: the measured items' bytes, and the size of the drive the folder is on.
    state->listBytes = state->volumeBytes = 0;
    b32 usage = false;
    for (u32 c = 0; c < columns->count; ++c) usage |= columns->v[c].id == Prog_ColumnId_FolderUsage || columns->v[c].id == Prog_ColumnId_VolumeUsage;
    if (usage) {
        for (u64 i = 0; i < state->directory.count; ++i) {
            const Prog_Entry *entry = &state->directory.entries[i];
            state->listBytes += entry->file.kind == P_FileKind_Directory ? (entry->measured ? entry->bytes : 0) : entry->file.size;
        }
        String path = state->directory.path;
        u64 longest = 0;
        for (u64 i = 0; i < state->drives.header.count; ++i) {
            String root = state->drives.v[i].path;
            if (root.length > longest && root.length <= path.length && Prog__EqualFolded(StrFromTo(path, 0, (i64)root.length), root)) {
                longest = root.length;
                state->volumeBytes = state->drives.v[i].totalBytes;
            }
        }
    }
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    // While the items run past the bottom, the rows leave room on the right for the scroll bar.
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset + (state->listScrolls ? Prog__ScrollGutter(m) : 0), 0 });
    // While a drag over the window would drop into the panel's folder, the list fades into the selection color's wash, as a hovered row's.
    R_Color background = theme->background, wash = theme->selection;
    f32 dropping = UI_Animate(UI_KeyFromStr(0, StrFormat(&state->frameArena, "DropWash/%" PRIu64, Prog__PanelId(state))), state->dropPanel == Prog__PanelId(state) ? 1.0f : 0.0f, 0);
    f32 into = theme->hoverAmount.r * wash.a * dropping;
    background = (R_Color){ background.r + (wash.r - background.r) * into, background.g + (wash.g - background.g) * into, background.b + (wash.b - background.b) * into,
                            background.a + (1 - background.a) * into };
    UI_SetNextColors((UI_Colors){ .background = background });
    UI_SetNextEdgeFade(floorf(m->row * 0.75f));
    UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Files")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable |
                                        (!state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0));
    UI_Interaction listInteraction = UI_InteractionFromBlock(list);
    if (UI->hotKey == list->key && !state->loading) state->hoverSpacePanel = Prog__PanelId(state);
    // A box drawn from the empty space leaves it the marquee's block, which only that drag's release is kept from clicking.
    if (listInteraction.pressed && state->marqueeKey == list->key) state->marqueeKey = 0;
    if (listInteraction.pressed && listInteraction.button == MouseButton_Right) {
        Prog_QueueCommand(state, Prog_CommandKind_ContextMenuAt, UINT64_MAX, listInteraction.pressCount, listInteraction.pressOrder);
    }
    // A plain click on the empty space around the items clears the selection.
    if (state->doubleClickToParent && listInteraction.clicked && listInteraction.button == MouseButton_Left && listInteraction.pressCount == 2) {
        Prog_QueueCommand(state, Prog_CommandKind_Up, 0, 0, listInteraction.clickOrder);
    }
    if (listInteraction.clicked && listInteraction.button == MouseButton_Left && !listInteraction.mods && state->marqueeKey != list->key) {
        state->marqueeBox = (R_Rect){0};
        Prog_QueueCommand(state, Prog_CommandKind_MarqueeStart, 0, 0, listInteraction.clickOrder);
        Prog_QueueCommand(state, Prog_CommandKind_MarqueeSelect, 0, 0, listInteraction.clickOrder);
    }
    if (state->marquee && UI->activeKey != state->marqueeKey) state->marquee = false;
    // A new folder starts where it is shown, without easing there from the last one.
    b32 snapScroll = state->resetScroll;
    if (state->resetScroll) {
        // A moved tab starts scrolled where it was.
        list->scrollOffset = list->scrollShown = (UI_Vec2){ 0, state->pickScrollSet ? state->pickScroll : 0 };
        state->resetScroll = state->pickScrollSet = false;
    }

    // The grid: one item per line in Details and List, and as many cells as fit across in Columns and the icon views, stretched to fill the line.
    // Miller columns list the panel's folder by name.
    Prog_View view = state->miller ? Prog_View_List : Prog_ZoomView(state->zoom);
    f32 width = MaxF32(1, list->rect.x1 - list->rect.x0), inner = MaxF32(1, width - 2 * m->inset - (state->listScrolls ? Prog__ScrollGutter(m) : 0));
    u64 perLine = 1;
    UI_Vec2 pitch = { width, m->row };
    R_Rect item = { 0, 0, width, m->row };
    f32 iconSize = 0;
    if (view == Prog_View_Columns) {
        perLine = Max((u64)1, (u64)(inner / floorf(m->line * 25.0f)));
        pitch.x = floorf(inner / (f32)perLine);
        f32 top = floorf(4.8f * m->scale + 0.5f);
        item = (R_Rect){ m->inset, top, m->inset + pitch.x - floorf(21.6f * m->scale + 0.5f), top + m->row };
    } else if (view == Prog_View_Icons) {
        iconSize = Prog__ZoomIconSize(state->zoom, m->scale);
        perLine = Max((u64)1, (u64)(inner / (iconSize + floorf(30.0f * m->scale))));
        // This PC's drives are taller, for their fullness bars, and the other items for the lines of a long name past the first.
        b32 drives = StrIsEqual(state->directory.path, Prog_ThisPC);
        f32 extra = drives ? Prog__DriveCellExtra(iconSize, m) : (Prog_IconNameLines - 1) * m->line;
        pitch = (UI_Vec2){ floorf(inner / (f32)perLine), iconSize + floorf(77.0f * m->scale) + extra };
        // Each cell starts at its slot's left edge.
        f32 gap = floorf(7.0f * m->scale), top = floorf(4.8f * m->scale + 0.5f);
        item = (R_Rect){ m->inset, top, m->inset + pitch.x - 2 * gap, floorf(8.0f * m->scale) + iconSize + floorf(53.0f * m->scale) + extra };
    }
    // A new layout, from a zoom step or a narrower panel, lets the items glide from where they were for a moment.
    if (perLine != state->gridColumns || pitch.x != state->gridPitch.x || pitch.y != state->gridPitch.y) state->gridMoveTime = UI->nowMicroseconds;
    b32 gliding = UI->animate && UI->nowMicroseconds - Min(state->gridMoveTime, UI->nowMicroseconds) < 500000;
    state->gridColumns = perLine;
    state->gridPitch = pitch;
    state->gridItem = item;
    // A grouped list runs its groups one after another, each a heading and then the lines of its items; a list without groups is one group
    // without a heading.
    Prog_ListGroup whole = { .count = state->viewCount, .total = state->viewCount };
    Prog_ListGroup *groups = state->groupCount ? state->groups : &whole;
    u32 groupCount = Max(state->groupCount, 1u);
    f32 heading = state->groupCount ? floorf(m->row * 1.4f + 0.5f) : 0, contentHeight = 0;
    state->groupHeading = heading;
    // A group whose heading was clicked eases its items open or shut, as the sidebar's groups do: `easeShown` of their height shows, in a
    // clip, and the groups after it move up by what is hidden. A long one eases over no more than the list's height.
    u32 easeGroup = UINT32_MAX;
    f32 easeShown = 0;
    if (state->groupEaseStart && state->groupEasePanel == Prog__PanelId(state)) {
        b32 folding = state->groupEaseFolding;
        f32 since = (f32)(UI->nowMicroseconds - Min(state->groupEaseStart, UI->nowMicroseconds)) / 1e6f, eased = 1 - expf(-34.0f * since);
        if (!UI->animate || state->groupEaseIndex >= state->groupCount) eased = 1;
        if (eased > 0.999f) {
            eased = 1;
            // One folded up stays shut until the view without its items comes.
            if (folding && !state->groupEaseSettling) Prog_QueueCommand(state, Prog_CommandKind_GroupFoldSettle, 0, 0, 0);
            state->groupEaseSettling = true;
            if (!folding) state->groupEaseStart = 0;
        } else {
            UI_KeepAnimating();
        }
        if (state->groupEaseStart && state->groupEaseIndex < groupCount) {
            easeGroup = state->groupEaseIndex;
            f32 body = (f32)((groups[easeGroup].count + perLine - 1) / perLine) * pitch.y, eases = MinF32(body, list->rect.y1 - list->rect.y0 + pitch.y);
            easeShown = floorf(eases * (folding ? 1 - eased : eased) + 0.5f);
        }
    }
    for (u32 g = 0; g < groupCount; ++g) {
        groups[g].top = contentHeight;
        contentHeight += heading + (g == easeGroup ? easeShown : (f32)((groups[g].count + perLine - 1) / perLine) * pitch.y);
    }

    state->pageRows = (u64)(Max(0.0f, list->rect.y1 - list->rect.y0) / pitch.y);
    // Before its first layout the list has no height to reveal anything in. A reveal goes on while the list's height changes after it, as an
    // inspector opening below it shrinks it, which the last layout's height does not know of yet.
    f32 listHeight = Max(0.0f, list->rect.y1 - list->rect.y0);
    b32 resized = state->revealHeight > 0 && state->revealHeight != listHeight;
    if ((state->revealFocus || resized) && state->hasFocus && listHeight > 0) {
        u64 position = Prog_ViewPosition(state, state->focusedRow);
        if (position == UINT64_MAX) position = 0;
        // An item in its group's first line comes into sight with the heading.
        const Prog_ListGroup *group = &groups[state->groupCount ? Prog_GroupAt(state, position) : 0];
        u64 line = (position - Min(position, group->start)) / perLine;
        f32 bottom = group->top + heading + (f32)(line + 1) * pitch.y, top = line ? bottom - pitch.y : group->top;
        // Center Camera puts its line in the middle.
        if (state->centerFocus) list->scrollOffset.y = Max(0.0f, bottom - pitch.y * 0.5f - listHeight * 0.5f);
        else if (top < list->scrollOffset.y) list->scrollOffset.y = top;
        else if (bottom > list->scrollOffset.y + listHeight) list->scrollOffset.y = Max(0.0f, bottom - listHeight);
        state->revealFocus = state->centerFocus = false;
        state->revealHeight = listHeight;
        UI_KeepAnimating();
    } else {
        state->revealHeight = 0;
    }
    if (snapScroll || !UI->animate) list->scrollShown = list->scrollOffset;
    UI_LimitScrollEase(list);
    // Only lines near the viewport get blocks; spacers stand in for the rest. While scrolling eases, both ends of the way are covered.
    f32 scrollFrom = MinF32(list->scrollOffset.y, list->scrollShown.y), scrollTo = MaxF32(list->scrollOffset.y, list->scrollShown.y);
    f32 visibleHeight = Max(pitch.y, UI->root->rect.y1 - UI->root->rect.y0) + (scrollTo - scrollFrom);
    f32 viewFrom = MaxF32(0, scrollFrom) - 2 * pitch.y, viewTo = MaxF32(0, scrollFrom) + visibleHeight + 4 * pitch.y;
    // A tree's folder unfolding opens as the sidebar's groups do, easing: a clip under it grows over its items, pushing the rows after them
    // down; one folding up lets the rows after it rise into its place. The rows after it are drawn `treeShift` from their places, and the
    // clip is `treeOpen` tall. A long one eases over no more than the list's height, past which the rows after it are out of sight anyway.
    f32 treeShift = 0, treeOpen = 0;
    u64 treeRow = UINT64_MAX, treeNew = 0;
    b32 folding = state->treeEaseFolding && state->treeEasePanel == Prog__PanelId(state);
    if (state->treeEaseStart && state->treeEasePanel == Prog__PanelId(state) && (perLine != 1 || view == Prog_View_Columns || heading > 0 || !UI->animate ||
                                                                                   state->treeEaseRow >= state->viewCount)) {
        state->treeEaseStart = 0;
        if (folding && !state->treeEaseRelisted) Prog_QueueCommand(state, Prog_CommandKind_Refresh, 0, 0, 0);
        state->treeEaseFolding = false;
    } else if (state->treeEaseStart && state->treeEasePanel == Prog__PanelId(state)) {
        f32 since = (f32)(UI->nowMicroseconds - Min(state->treeEaseStart, UI->nowMicroseconds)) / 1e6f, eased = 1 - expf(-34.0f * since);
        // A folder folded up is listed again once its rows are shut, and stays shut until that listing comes.
        if (folding && eased > 0.999f) {
            eased = 1;
            if (!state->treeEaseRelisted) Prog_QueueCommand(state, Prog_CommandKind_Refresh, 0, 0, 0);
            state->treeEaseRelisted = true;
        }
        if (!folding && eased > 0.999f) {
            state->treeEaseStart = 0;
        } else {
            if (eased < 1) UI_KeepAnimating();
            treeRow = state->treeEaseRow;
            treeNew = state->treeEaseOpening || folding ? Min(state->treeEaseRows, state->viewCount - treeRow - 1) : 0;
            f32 total = (f32)state->treeEaseRows * pitch.y, eases = MinF32(total, list->rect.y1 - list->rect.y0 + pitch.y);
            treeOpen = floorf(eases * (folding ? 1 - eased : eased) + 0.5f);
            treeShift = state->treeEaseOpening || folding ? treeOpen - total : floorf(eases * (1 - eased) + 0.5f);
        }
    }
    UI_Parent(list) {
        UI_Key directoryKey = UI_KeyFromStr(list->key, state->directory.path), groupKey = UI_KeyFromStr(directoryKey, S("Group"));
        UI_Key marqueeFrom = 0;
        // How far down the blocks so far reach; spacers fill what they skip.
        f32 drawn = 0;
        for (u32 g = 0; g < groupCount && groups[g].top < viewTo; ++g) {
            const Prog_ListGroup *group = &groups[g];
            f32 below = group->top + heading;
            if (heading > 0 && below > viewFrom) {
                if (group->top > drawn) Prog__Spacer(UI_Flex(), UI_Pixels(group->top - drawn));
                Prog__GroupHeading(state, groupKey, g, heading, m);
                drawn = below;
            }
            u64 lines = (group->count + perLine - 1) / perLine;
            u64 first = viewFrom > below ? (u64)((viewFrom - below) / pitch.y) : 0, end = viewTo > below ? Min(lines, (u64)((viewTo - below) / pitch.y) + 1) : 0;
            // The group easing open or shut has its lines in a clip as tall as it shows.
            UI_Block *easing = NULL, *outside = NULL;
            if (g == easeGroup) {
                end = Min(end, (u64)ceilf(easeShown / pitch.y));
                if (below > drawn) Prog__Spacer(UI_Flex(), UI_Pixels(below - drawn));
                easing = Prog__Box(UI_Flex(), UI_Pixels(easeShown), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
                outside = UI_PushParent(easing);
                drawn = below;
            }
            // The rows after a tree's folder easing come into sight from further away.
            if (treeShift > 0) first = first > state->treeEaseRows ? first - state->treeEaseRows : 0;
            if (treeShift < 0) end = Min(lines, end + state->treeEaseRows);
            for (u64 line = first; line < end; ++line) {
                f32 y = below + (f32)line * pitch.y;
                if (treeRow != UINT64_MAX && line > treeRow) {
                    if (line <= treeRow + treeNew) continue;
                    y += treeShift;
                    if (y + pitch.y < viewFrom || y > viewTo) continue;
                }
                if (y > drawn) Prog__Spacer(UI_Flex(), UI_Pixels(y - drawn));
                drawn = y + pitch.y;
                u64 from = group->start + line * perLine, to = Min(group->start + group->count, from + perLine);
                // Columns keeps its cells when only one fits across.
                if (perLine == 1 && view != Prog_View_Columns) {
                    u64 row = state->view[from];
                    UI_Key key = UI_KeyFromStr(directoryKey, state->directory.entries[row].file.name);
                    // Rows after a folder folding up are striped as they will be once its items are gone.
                    u64 position = folding && treeRow != UINT64_MAX && line > treeRow ? from - treeNew : from;
                    if (Prog__Row(state, row, position, key, columns, view == Prog_View_List, m)) marqueeFrom = key;
                    // The folder's new items, in a clip from its bottom edge as tall as they have come out.
                    if (line == treeRow && treeNew) {
                        f32 top = y + pitch.y;
                        u64 shownFrom = (u64)MaxF32(0, (viewFrom - top) / pitch.y), shownTo = Min(treeNew, (u64)MaxF32(0, (MinF32(viewTo, top + treeOpen) - top) / pitch.y) + 1);
                        UI_SetNextFloatingOffset((UI_Vec2){ 0, top - list->scrollShown.y });
                        UI_SetNextPrefWidth(UI_Pixels(inner));
                        UI_SetNextPrefHeight(UI_Pixels(treeOpen));
                        UI_SetNextChildrenLayout(UI_Layout_Vertical);
                        UI_SetNextChildrenPadding((UI_Padding){ 0, (f32)shownFrom * pitch.y, 0, 0 });
                        UI_Block *opening = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
                        UI_Parent(opening) for (u64 n = shownFrom; n < shownTo; ++n) {
                            u64 at = treeRow + 1 + n, nested = state->view[at];
                            Prog__Row(state, nested, at, UI_KeyFromStr(directoryKey, state->directory.entries[nested].file.name), columns, view == Prog_View_List, m);
                        }
                    }
                    continue;
                }
                UI_Block *cells = Prog__Box(UI_Flex(), UI_Pixels(pitch.y), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(cells) {
                    for (u64 at = from; at < to; ++at) {
                        u64 row = state->view[at];
                        Prog_Entry *entry = &state->directory.entries[row];
                        UI_Key key = UI_KeyFromStr(directoryKey, entry->file.name);
                        // An item drawn in the last frame eases from where it was to its place in a new layout; others
                        // start in place.
                        f32 placeX = (f32)(at - from) * pitch.x, placeY = y;
                        if (gliding && entry->shownFrame + 1 == UI->frameIndex) {
                            entry->shownX = UI_Approach(entry->shownX, placeX, 0.5f);
                            entry->shownY = UI_Approach(entry->shownY, placeY, 0.5f);
                        } else {
                            entry->shownX = placeX;
                            entry->shownY = placeY;
                        }
                        entry->shownFrame = UI->frameIndex;
                        // Each cell is a slot of the pitch, with the item inside it where the grid puts it.
                        UI_SetNextPrefWidth(UI_Pixels(pitch.x));
                        UI_SetNextPrefHeight(UI_Pixels(pitch.y));
                        UI_SetNextChildrenPadding((UI_Padding){ item.x0 - m->inset + floorf(entry->shownX - placeX + 0.5f), item.y0 + floorf(entry->shownY - placeY + 0.5f), 0, 0 });
                        UI_Block *slot = UI_AddBlock(0);
                        UI_Parent(slot) {
                            if (view == Prog_View_Columns) {
                                if (Prog__ColumnCell(state, row, line, key, item.x1 - item.x0, m)) marqueeFrom = key;
                            } else {
                                Prog__IconCell(state, row, key, item.x1 - item.x0, item.y1 - item.y0, iconSize, m);
                            }
                        }
                    }
                }
            }
            if (easing) {
                UI_PopParent(outside);
                drawn = below + easeShown;
            }
        }
        if (!state->loading && !state->marquee && listInteraction.active && listInteraction.isDragging && listInteraction.button == MouseButton_Left) marqueeFrom = list->key;
        // The anchor is kept in content coordinates, so that the box stays on its items while the list scrolls.
        if (marqueeFrom) {
            state->marquee = true;
            state->marqueeKey = marqueeFrom;
            state->marqueeAnchor = (UI_Vec2){ UI->pressOrigin.x - list->rect.x0, UI->pressOrigin.y - list->rect.y0 + list->scrollShown.y };
            Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_MarqueeStart, 0, 0, 0);
            command->mods = UI->activeMods;
        }
        if (state->marquee) {
            // Holding the pointer past the top or bottom edge scrolls, faster the further out it is.
            f32 over = UI->mouse.y < list->rect.y0 ? UI->mouse.y - list->rect.y0 : UI->mouse.y > list->rect.y1 ? UI->mouse.y - list->rect.y1 : 0;
            list->scrollOffset.y = MaxF32(0, list->scrollOffset.y + over * 0.25f);
            UI_Vec2 anchor = state->marqueeAnchor;
            UI_Vec2 point = { UI__Clamp(UI->mouse.x - list->rect.x0, 0, width), UI->mouse.y - list->rect.y0 + list->scrollShown.y };
            point.y = UI__Clamp(point.y, 0, MaxF32(list->contentSize.y, list->rect.y1 - list->rect.y0 + list->scrollShown.y));
            R_Rect box = { MinF32(anchor.x, point.x), MinF32(anchor.y, point.y), MaxF32(anchor.x, point.x), MaxF32(anchor.y, point.y) };
            state->marqueeBox = box;
            Prog_QueueCommand(state, Prog_CommandKind_MarqueeSelect, 0, point.y >= anchor.y, 0);
            Prog__Marquee((R_Rect){ box.x0 - m->inset, box.y0 - list->scrollShown.y, box.x1 - m->inset, box.y1 - list->scrollShown.y }, m);
        }
        // A tree's folder easing open or shut takes as much room as it shows, so that the scroll bar comes and goes with it.
        if (treeRow != UINT64_MAX) contentHeight = MaxF32(drawn, contentHeight + treeShift);
        if (contentHeight > drawn) Prog__Spacer(UI_Flex(), UI_Pixels(contentHeight - drawn));
        // Cells that sit lower in their slot, as Columns' do, reach past the last one.
        if (state->viewCount && item.y1 > pitch.y) Prog__Spacer(UI_Flex(), UI_Pixels(item.y1 - pitch.y));
        // A list longer than its view scrolls on by half the view, so that its last line stops just above the middle.
        f32 viewHeight = list->rect.y1 - list->rect.y0;
        if (!state->noOverscroll && contentHeight > viewHeight && viewHeight > 2 * pitch.y) Prog__Spacer(UI_Flex(), UI_Pixels(floorf(viewHeight * 0.5f)));
        if (!state->viewCount && !state->groupCount && !state->loading) {
            // Centered a little below the top.
            String empty = state->directory.count ? Tr("All items are currently hidden by view settings.") : Tr("This folder is empty.");
            Prog__Spacer(UI_Flex(), UI_Pixels(floorf(m->row * 0.65f)));
            Prog__Label(empty, UI_Flex(), m->row, 0, theme->textDim, 0.5f);
        }
    }
    // The scroll bar sits in the window's coordinates, since the boxes around the list have no layout of their own yet.
    state->listScrolls = list->contentSize.y > list->rect.y1 - list->rect.y0 + 0.5f && list->rect.y1 > list->rect.y0;
    UI_Parent(UI->root) Prog__ScrollBar(list, pitch.y, NoTr("Files"), 1, m);
}

function String Prog__FormatCount(Arena *arena, u64 count);

// A bar filled to `fraction` with `label` over it, as the zoom shows in the status bar.
function void Prog__FillBar(String label, f32 fraction, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 barWidth = floorf(40.8f * m->scale + 0.5f), barHeight = floorf(m->line * 1.2f), radius = floorf(4.0f * m->scale + 0.5f);
    f32 filled = floorf(barWidth * fraction);
    UI_SetNextPrefWidth(UI_Pixels(barWidth));
    UI_SetNextPrefHeight(UI_Pixels(barHeight));
    UI_Block *bar = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(bar) {
        // The fill and the track are each the bar's rounded shape cut off where the fill ends, so that the fill's end is straight and
        // neither one's smoothed corners show through the other's.
        f32 from[2] = { 0, filled }, to[2] = { filled, barWidth };
        R_Color colors[2] = { theme->accent, theme->track };
        for (u32 i = 0; i < 2; ++i) {
            if (to[i] <= from[i]) continue;
            UI_SetNextFloatingOffset((UI_Vec2){ from[i], 0 });
            UI_SetNextPrefWidth(UI_Pixels(to[i] - from[i]));
            UI_SetNextPrefHeight(UI_Pixels(barHeight));
            UI_Block *cut = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
            UI_Parent(cut) {
                UI_SetNextFloatingOffset((UI_Vec2){ -from[i], 0 });
                UI_SetNextPrefWidth(UI_Pixels(barWidth));
                UI_SetNextPrefHeight(UI_Pixels(barHeight));
                UI_SetNextCornerRadius(radius);
                UI_SetNextColors((UI_Colors){ .background = colors[i] });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        }
        // A block's text draws before its children, so the label is a child after the fill.
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        Prog__Label(label, UI_Pixels(barWidth), barHeight, 0, theme->text, 0.5f);
    }
}

function void Prog__FolderSortIcon(Prog_FolderSort sort, R_Color color, R_Color behind, const Prog_Metrics *m);

// An icon glyph in a bottom bar's pill. The icon font sets its glyphs higher in their line than the text's, so it is moved down to center
// its ink on the pill, as the text's is.
function void Prog__StatusIcon(String icon, f32 width, f32 height, R_Color color, const Prog_Metrics *m)
{
    Prog__Icon(icon, width, height, color, 0.5f, m);
    UI->parent->last->props.childrenPadding.top = 2 * floorf(0.8f * m->scale + 0.5f);
}

function void Prog__StatusBar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    // The pills sit in the panel's bottom corners, running past its edges by their corner radius, so that only their inner corners round off.
    f32 radius = floorf(10.4f * m->scale + 0.5f);
    UI_SetNextChildrenPadding((UI_Padding){ -radius, 0, -radius, 0 });
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(m->pill));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *bar = UI_AddBlockFromKey(UI_KeyFromStr(0, S("StatusBar")), UI_Flag_DrawBackground);
    UI_Parent(bar) {
        // While filtering, the text sits in a darker field and the box grows to the right for the buttons that work the filter.
        // A narrow panel narrows the box, down to its icon, to keep the counts, the view and the zoom in sight.
        b32 active = state->filtering || state->filter.length;
        f32 inset = floorf(2.4f * m->scale + 0.5f);
        // The buttons' room opens and shuts easing, and snaps the last few pixels.
        f32 extra = active ? 4 * Prog__ToolbarButtonWidth(m) : 0, grown = UI_Animate(UI_KeyFromStr(0, S("FilterGrow")), extra, extra);
        if (fabsf(grown - extra) < floorf(20.0f * m->scale + 0.5f)) grown = extra;
        f32 want = floorf(m->line * 15.3f) + floorf(6.4f * m->scale + 0.5f) + radius + floorf(grown + 0.5f);
        f32 room = bar->rect.x1 - bar->rect.x0 + 2 * radius - state->statusRight;
        if (bar->rect.x1 > bar->rect.x0 && state->statusRight > 0) want = MinF32(want, MaxF32(radius + m->iconSlot + m->gap, room));
        UI_SetNextPrefWidth(UI_Pixels(want));
        UI_SetNextPrefHeight(UI_Pixels(m->pill + radius));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0 });
        UI_SetNextChildrenPadding(active ? (UI_Padding){ radius, inset, m->gap, 0 } : (UI_Padding){ m->gap + radius, 0, m->gap, 0 });
        UI_SetNextCornerRadius(radius);
        UI_SetNextBorderThickness(m->stroke);
        UI_SetNextColors((UI_Colors){ .background = theme->panel, .border = theme->tabBorder });
        UI_Block *filter = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilterBox")), UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        state->filterRect = filter->rect;
        UI_Interaction interaction = UI_InteractionFromBlock(filter);
        if (interaction.pressed) Prog_QueueCommand(state, Prog_CommandKind_StartFilter, 0, interaction.pressCount, interaction.pressOrder);
        UI_Parent(filter) {
            if (active) {
                f32 tall = m->pill - inset;
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(tall + radius));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0 });
                UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, 0, 0 });
                UI_SetNextCornerRadius(floorf(8.0f * m->scale + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = theme->inner });
                UI_Block *well = UI_AddBlock(UI_Flag_DrawBackground);
                UI_Parent(well) {
                    Prog__SearchIcon(m->iconSlot - m->gap, tall, state->filter.length ? theme->text : theme->textDim, 0.5f, floorf(0.8f * m->scale + 0.5f), m);
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(tall));
                    UI_Block *field = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilterField")), UI_Flag_Clip | UI_Flag_LeftMouseClickable);
                    Prog__EditMouse(state, field, &state->filter, floorf(m->gap * 0.5f), Prog_CommandKind_FilterCaret, m);
                    UI_Parent(field) Prog__EditContents(&state->filter, floorf(m->gap * 0.5f), floorf((tall - m->line) * 0.5f), state->filtering, m);
                }
                UI_Block *buttons = Prog__Box(UI_Fill(), UI_Pixels(tall), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(buttons) {
                    Prog__ExpandButton(state, state->expanded, m);
                    Prog__PillButton(state, S("FilterPin"), state->filterSticky ? Icon_Pin : Icon_PinOff, Prog_CommandKind_ToggleFilterPin, state->filterSticky, m);
                    Prog__PillButton(state, S("FilterApply"), Icon_Check, Prog_CommandKind_ApplyFilter, false, m);
                    Prog__PillButton(state, S("FilterOpen"), Icon_CornerDownLeft, Prog_CommandKind_OpenFocused, false, m);
                    Prog__PillButton(state, S("FilterRemove"), Icon_X, Prog_CommandKind_RemoveFilter, false, m);
                }
            } else {
                Prog__SearchIcon(m->iconSlot - m->gap, m->pill, theme->textDim, 0.5f, floorf(0.8f * m->scale + 0.5f), m);
                String placeholder = StrFormat(&state->frameArena, TrN(state->directory.count, "Filter %u file...", "Filter %u files..."), (u32)state->directory.count);
                Prog__Label(placeholder, UI_Flex(), m->pill, m->gap * 0.5f, theme->textDim, 0);
                // In the pill's middle, as the buttons of the filter field are in theirs.
                UI_Block *slot = Prog__Box(UI_Pixels(Prog__ToolbarButtonWidth(m)), UI_Pixels(m->pill), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(slot) Prog__ExpandButton(state, state->expanded, m);
            }
        }

        Prog__Spacer(UI_Flex(), UI_Pixels(m->pill));

        String status = state->error;
        // While a listing walks, what it has found so far: from the start for a new folder, and for the shown one's refresh once it has taken
        // a second, as the clock shows, so that quick refreshes, as while a copy writes into the folder, leave the counts of the rows shown still.
        u64 now = UI->nowMicroseconds;
        b32 counting = state->directoryJob && (state->loading || now - Min(state->listStarted, now) >= 1000000);
        Prog_JobInput *walking = counting ? state->directoryJob->input : NULL;
        u64 found = walking ? atomic_load(&walking->foundFolders) + atomic_load(&walking->foundFiles) : 0;
        if (!status.length && state->loading && !found) {
            status = StrFormat(&state->frameArena, TrF("Loading %S..."), walking->path);
        }
        if (!status.length) {
            // Shown out of everything the folder holds; one number when nothing is left out.
            const Prog_Directory *directory = &state->directory;
            u64 shown[2] = { state->viewFolders, state->viewCount - state->viewFolders };
            u64 total[2] = { directory->totalFolders, directory->totalFiles };
            if (found) {
                shown[0] = atomic_load(&walking->shownFolders);
                shown[1] = atomic_load(&walking->shownFiles);
                total[0] = atomic_load(&walking->foundFolders);
                total[1] = atomic_load(&walking->foundFiles);
            }
            b32 hidden[2] = { state->hideFolders, state->hideFiles };
            String icons[2] = { Icon_Folder, Icon_File };
            String keys[2] = { S("ToggleFolders"), S("ToggleFiles") };
            Prog_CommandKind kinds[2] = { Prog_CommandKind_ToggleFolders, Prog_CommandKind_ToggleFiles };
            UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, floorf(4.8f * m->scale + 0.5f) + radius, 0 });
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(m->stroke);
            UI_SetNextPrefWidth(UI_Fill());
            UI_SetNextPrefHeight(UI_Pixels(m->pill + radius));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextColors((UI_Colors){ .background = theme->panel });
            UI_Block *counts = UI_AddBlockFromKey(UI_KeyFromStr(0, S("StatusCounts")), UI_Flag_DrawBackground | UI_Flag_DrawBorder);
            state->statusRight = counts->rect.x1 - counts->rect.x0;
            counts->props.colors.border = theme->tabBorder;
            counts->props.childrenAlignment = (UI_Vec2){ 0, 0 };
            UI_Parent(counts) {
                // How long the listing has taken, once that is a second or more: counting while it walks, then kept.
                u64 took = walking ? now - Min(state->listStarted, now) : state->listTook;
                if (took >= 1000000) {
                    Prog__StatusIcon(Icon_Clock, 0, m->pill, theme->icon, m);
                    Prog__LabelPadded(StrFormat(&state->frameArena, "%.2f sec", (f64)took / 1e6), UI_TextContent(), m->pill, floorf(7.2f * m->scale + 0.5f), floorf(9.6f * m->scale + 0.5f), theme->text, 0);
                }
                if (counting) UI_KeepAnimating();
                else if (state->directoryJob) Prog_WakeAt(state, state->listStarted + 1000000);
                for (u32 i = 0; i < 2; ++i) {
                    // Both while a walk counts, so that the text does not jump between one number and two as they meet and part.
                    String text = shown[i] == total[i] && !found ? Prog__FormatCount(&state->frameArena, total[i])
                                                       : StrFormat(&state->frameArena, "%S/%S", Prog__FormatCount(&state->frameArena, shown[i]), Prog__FormatCount(&state->frameArena, total[i]));
                    // Each count toggles whether its kind is listed.
                    UI_SetNextPrefWidth(UI_Fill());
                    UI_SetNextPrefHeight(UI_Pixels(m->pill));
                    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                    UI_Block *toggle = UI_AddBlockFromKey(UI_KeyFromStr(0, keys[i]), UI_Flag_LeftMouseClickable);
                    UI_Interaction interaction = UI_InteractionFromBlock(toggle);
                    if (interaction.clicked) Prog_QueueCommand(state, kinds[i], 0, 0, interaction.clickOrder);
                    b32 hot = UI->hotKey == toggle->key;
                    UI_Parent(toggle) {
                        Prog__StatusIcon(icons[i], 0, m->pill, hidden[i] ? theme->iconDisabled : hot ? theme->text : theme->icon, m);
                        Prog__LabelPadded(text, UI_TextContent(), m->pill, floorf(7.2f * m->scale + 0.5f), floorf(9.6f * m->scale + 0.5f), hidden[i] ? theme->textDim : theme->text, 0);
                    }
                }
                // Where folders go among the files, which a click cycles, then a line before the view.
                Prog__Spacer(UI_Pixels(floorf(6.4f * m->scale + 0.5f)), UI_Pixels(m->pill));
                UI_SetNextPrefWidth(UI_Pixels(floorf(14.4f * m->scale + 0.5f)));
                UI_SetNextPrefHeight(UI_Pixels(m->pill));
                UI_Block *sortButton = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FolderSort")), UI_Flag_LeftMouseClickable);
                local_persist const String sortTips[Prog_FolderSort_Count] = { TrInit("Folders first, then files."), TrInit("Files first, then folders."), TrInit("Folders and files mixed.") };
                Prog__DescribeTooltip(state, Prog_CommandKind_CycleFolderSort, Prog_Tr(sortTips[state->folderSort]));
                UI_Interaction sortClick = UI_InteractionFromBlock(sortButton);
                if (sortClick.clicked) Prog_QueueCommand(state, Prog_CommandKind_CycleFolderSort, 0, 0, sortClick.clickOrder);
                UI_Parent(sortButton) Prog__FolderSortIcon(state->folderSort, UI->hotKey == sortButton->key ? theme->text : theme->icon, theme->panel, m);
                Prog__Spacer(UI_Pixels(floorf(12.8f * m->scale + 0.5f)), UI_Pixels(m->pill));
                // Centered on the icons' line.
                UI_SetNextPrefWidth(UI_Pixels(floorf(1.6f * m->scale + 0.5f)));
                UI_SetNextPrefHeight(UI_Pixels(m->pill));
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Parent(UI_AddBlock(0)) Prog__Box(UI_Pixels(floorf(1.6f * m->scale + 0.5f)), UI_Pixels(floorf(19.2f * m->scale + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
                Prog__Spacer(UI_Pixels(floorf(8.8f * m->scale + 0.5f)), UI_Pixels(m->pill));
                // The view and how far it is zoomed, as a bar filled to the zoom's percentage.
                Prog_View view = Prog_ZoomView(state->zoom);
                const String viewIcons[Prog_View_Count] = { Icon_List, Icon_Menu, Icon_Columns3, Icon_Image };
                String name = Prog__ZoomName(state->zoom);
                // The icon views' size after their name, dimmed; the other views are their name alone, the zoom picking the view there.
                String percent = view == Prog_View_Icons ? StrFormat(&state->frameArena, "%u%%", state->zoom / 10) : (String){0};
                // The pointer over the view and its zoom shows the zoom popup.
                f32 nameGap = floorf(8.0f * m->scale + 0.5f), percentGap = percent.length ? floorf(6.4f * m->scale + 0.5f) : 0;
                UI_SetNextPrefWidth(UI_Pixels(R_TextWidth(m->icons, viewIcons[view]) + R_TextWidth(m->font, name) + nameGap + percentGap + R_TextWidth(m->font, percent)));
                UI_SetNextPrefHeight(UI_Pixels(m->pill));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Block *viewButton = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ViewMenu")), UI_Flag_LeftMouseClickable);
                if (state->currentPanel == state->activePanel) state->zoomAnchor = viewButton->rect;
                b32 viewHot = UI->hotKey == viewButton->key || (state->zoomShown && state->currentPanel == state->activePanel);
                UI_Parent(viewButton) {
                Prog__StatusIcon(viewIcons[view], 0, m->pill, viewHot ? theme->text : theme->icon, m);
                Prog__LabelPadded(name, UI_TextContent(), m->pill, nameGap, 0, theme->text, 0);
                if (percent.length) Prog__LabelPadded(percent, UI_TextContent(), m->pill, percentGap, 0, theme->textDim, 0);
                }
            }
        } else {
            UI_SetNextPrefWidth(UI_TextContent());
            UI_SetNextPrefHeight(UI_Pixels(m->pill));
            UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
            UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextCornerRadius(m->radius * 2);
            UI_SetNextColors((UI_Colors){ .background = theme->panel, .text = state->error.length ? theme->textHeading : theme->text });
            UI_SetNextText(status);
            UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawText);
        }
    }
}

// A filled drive, which Lucide has only as an outline: a solid top over an outlined base with a light on its left.
// It sits where an icon glyph's ink would, centered on the glyph's advance.
function void Prog__DriveGlyph(f32 width, f32 height, R_Color color, const Prog_Metrics *m)
{
    f32 size = floorf(m->line * 0.75f + 0.5f);
    f32 stroke = m->separator;
    f32 radius = floorf(size * 0.2f + 0.5f);
    f32 top = floorf(size * 0.55f + 0.5f);
    f32 light = MaxF32(2, floorf(size * 0.2f + 0.5f));
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_Block *slot = UI_AddBlock(0);
    UI_Parent(slot) {
        UI_Vec2 at = { floorf((R_TextWidth(m->icons, Icon_HardDrive) - size) * 0.5f), floorf((height - size) * 0.5f) };
        // The top is a block rounded more than the base, so that it narrows upward, cut off square where the base starts.
        UI_SetNextFloatingOffset((UI_Vec2){ at.x + 1, at.y });
        UI_SetNextPrefWidth(UI_Pixels(size - 2));
        UI_SetNextPrefHeight(UI_Pixels(top));
        UI_Block *cut = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
        UI_Parent(cut) {
            UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
            UI_SetNextPrefWidth(UI_Pixels(size - 2));
            UI_SetNextPrefHeight(UI_Pixels(size));
            UI_SetNextCornerRadius(floorf(size * 0.33f + 0.5f));
            UI_SetNextColors((UI_Colors){ .background = color });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
        UI_SetNextFloatingOffset((UI_Vec2){ at.x, at.y + top - stroke });
        UI_SetNextPrefWidth(UI_Pixels(size));
        UI_SetNextPrefHeight(UI_Pixels(size - top + stroke));
        UI_SetNextCornerRadius(radius);
        UI_SetNextBorderThickness(stroke);
        UI_SetNextColors((UI_Colors){ .border = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
        UI_SetNextFloatingOffset((UI_Vec2){ at.x + stroke + 1, at.y + top + floorf((size - top - light) * 0.5f) });
        UI_SetNextPrefWidth(UI_Pixels(light));
        UI_SetNextPrefHeight(UI_Pixels(light));
        UI_SetNextCornerRadius(light * 0.3f);
        UI_SetNextColors((UI_Colors){ .background = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
}

// A sidebar section title, which opens and closes the section; without an icon it shows the filled drive glyph.
// The box a sidebar name is edited in, in place of its text in a group's title or a folder's row, as the list's rename box looks: as wide
// as its text up to the room there, with the text where it was. A press anywhere else keeps the name, as Enter does.
function void Prog__SidebarNameBox(Prog_State *state, f32 height, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 pad = floorf(m->gap * 0.5f), tall = m->line + pad, above = floorf((tall - m->line) * 0.5f);
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ -pad, 0, 0, 0 });
    UI_Block *cell = UI_AddBlockFromKey(UI_KeyFromStr(0, S("SidebarNameCell")), 0);
    f32 room = cell->rect.x1 - cell->rect.x0 + pad, wide = R_TextWidth(m->font, Prog_EditText(&state->layoutName)) + 2 * pad + Prog__CaretWidth(m);
    UI_Key key = UI_KeyFromStr(0, S("SidebarNameBox"));
    UI_Parent(cell) {
        UI_SetNextPrefWidth(UI_Pixels(room > pad ? MinF32(wide, room) : wide));
        UI_SetNextPrefHeight(UI_Pixels(tall));
        UI_SetNextCornerRadius(m->radius);
        UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->focus });
        UI_Block *box = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        UI_Parent(box) Prog__EditContents(&state->layoutName, pad, above, true, m);
    }
    state->nameShown = true;
    // The sidebar scrolls to keep it in sight.
    UI_Block *sidebar = UI__Find(UI_KeyFromStr(0, S("Sidebar")));
    if (sidebar && cell->rect.y1 > cell->rect.y0) {
        f32 top = cell->rect.y0 - sidebar->rect.y0 + sidebar->scrollShown.y, bottom = top + (cell->rect.y1 - cell->rect.y0);
        sidebar->scrollOffset.y = MinF32(MaxF32(sidebar->scrollOffset.y, bottom + m->gap - (sidebar->rect.y1 - sidebar->rect.y0)), top);
    }
    if (UI->lastPressTime != state->nameSince && UI->lastPressKey != key) {
        state->nameSince = UI->lastPressTime;
        Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 0, 0, 0);
    }
}

// A slow second click's rename of `group`'s row `index`, which starts once the double-click time passes without another press.
function void Prog__AskSidebarRename(Prog_State *state, Prog_Group group, u64 index)
{
    if (state->noClickRename) return;
    state->sidebarRenameGroup = (u32)group + 1;
    state->sidebarRenameIndex = index;
    state->sidebarRenameAt = UI->nowMicroseconds + (UI->doubleClickMicroseconds ? UI->doubleClickMicroseconds : UINT64_C(500000));
    state->sidebarRenamePress = UI->lastPressTime;
}

function void Prog__GroupTitle(Prog_State *state, Prog_Group group, String title, String icon, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Group/%S", title));
    b32 hot = UI->hotKey == key || state->groupDrag == (u32)group + 1;
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ floorf(8.0f * m->scale + 0.5f), 0, floorf(10.4f * m->scale + 0.5f), 0 });
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = theme->alternateRow });
    UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable | (hot ? UI_Flag_DrawBackground : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(row);
    if ((u32)group < Prog_SidebarGroupMax) state->groupTops[group] = row->rect.y0;
    // Dragged, it moves among the groups, before the one the line the sidebar draws is at; the click its release makes folds nothing.
    b32 moved = false;
    if (interaction.active && interaction.isDragging && interaction.button == MouseButton_Left && !state->sidebarFilter.length) state->groupDrag = (u32)group + 1;
    if (interaction.released && state->groupDrag == (u32)group + 1) {
        if (state->groupGap) Prog_QueueCommand(state, Prog_CommandKind_GroupMove, (u64)group, 0, interaction.clickOrder)->target = state->groupGap - 1;
        state->groupDrag = state->groupGap = 0;
        moved = true;
    }
    if (interaction.clicked && interaction.button == MouseButton_Left && !moved) Prog_QueueCommand(state, Prog_CommandKind_ToggleGroup, (u64)group, 0, interaction.clickOrder);
    // A right click opens the group's menu.
    if (interaction.pressed && interaction.button == MouseButton_Right) Prog_QueueCommand(state, Prog_CommandKind_GroupMenu, (u64)group, 0, interaction.pressOrder)->at = UI->mouse;
    UI_Parent(row) {
        R_Color color = hot ? theme->text : theme->textHeading;
        f32 slot = floorf(m->line * 1.5f) - floorf(2.4f * m->scale + 0.5f);
        if (icon.length) Prog__Icon(icon, slot, m->row, color, 0, m);
        else Prog__DriveGlyph(slot, m->row, color, m);
        if (state->layoutSaving && state->nameFor == 1 && (u32)group == Prog_Group_Count + state->nameGroup) Prog__SidebarNameBox(state, m->row, m);
        else Prog__Label(title, UI_Flex(), m->row, 0, color, 0);
        Prog__Icon(Prog_GroupOpen(state, group) || state->sidebarFilter.length ? Icon_ChevronDown : Icon_ChevronRight, m->line * 1.5f, m->row, color, 0.5f, m);
    }
}

// One location under a section title: its folder icon, or a drive glyph, and its name.
// Drives show how full they are under the name, and bookmarks get a remove button while hovered.
// A row of the sidebar's `group`; a `faded` one, hidden, is dimmed as the list dims hidden items.
function void Prog__MarkedText(String text, const u8 *marks, const R_Font *font, f32 height, R_Color color);

function void Prog__SidebarItem(Prog_State *state, Prog_Group group, u64 index, String name, String path, const P_Place *drive, b32 faded, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Place/%u/%" PRIu64 "/%S", group, index, path));
    UI_Key removeKey = UI_KeyFromStr(key, S("Remove"));
    // Not lit under a drag, which would hide the line where it goes; a place dragged stays lit.
    b32 hot = (UI->hotKey == key || UI->hotKey == removeKey) && !P_DropPending() && (!state->sidebarDrag || (state->sidebarDragGroup == (u32)group + 1 && state->sidebarDrag == index + 1));
    // Lit while it is shown, or while its menu is open; not under a drag, where only the folder a drop would go into is lit.
    b32 current = !P_DropPending() && (StrIsEqual(path, state->directory.path) || (state->menuOpen && StrIsEqual(path, state->menuTarget)));
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    // The bookmarks on either side of where a drag would put its folder move apart, easing, to open a gap there; the row keeps its place.
    f32 shift = 0;
    if (group == Prog_Group_Bookmarks) {
        UI_Key shiftKey = UI_KeyFromStr(key, S("Shift"));
        f32 apart = floorf(6.4f * m->scale + 0.5f);
        f32 target = !state->bookmarkGap ? 0 : index + 1 == state->bookmarkGap ? apart : index + 2 == state->bookmarkGap ? -apart : 0;
        if (target != 0 || UI_Kept(shiftKey) != 0) shift = floorf(UI_Animate(shiftKey, target, 0) + 0.5f);
    }
    UI_SetNextChildrenPadding((UI_Padding){ floorf(m->line * 1.5f + m->gap * 0.5f) + floorf(3.2f * m->scale + 0.5f), shift, floorf(m->gap * 0.5f), -shift });
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = theme->selection });
    UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_MiddleMouseClickable | UI_Flag_RightMouseClickable | (hot || current ? UI_Flag_DrawBackground : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(row);
    if (UI->hotKey == key && path.length) state->hoverMenuPath = path;
    // Under a drag the middle of a row takes it into its folder, lit; a bookmark's edges put it between the bookmarks instead.
    if (P_DropPending() && path.length && !StrIsEqual(path, Prog_ThisPC) && UI__Contains(row->rect, UI->mouse)) {
        f32 edge = group == Prog_Group_Bookmarks || (u32)group >= Prog_Group_Count ? (row->rect.y1 - row->rect.y0) * 0.25f : 0;
        if (UI->mouse.y >= row->rect.y0 + edge && UI->mouse.y < row->rect.y1 - edge) {
            state->dropPlace = StrCopy(&state->dropPlaceArena, path);
            row->flags |= UI_Flag_DrawBackground;
        }
    }
    // A bookmark dragged away is dragged as its folder, which the bookmarks' edges take to move it among them.
    if (group == Prog_Group_Bookmarks && interaction.active && interaction.isDragging && interaction.button == MouseButton_Left && !state->dragging && !state->dragRequested) {
        state->dragPlace = StrCopy(&state->dropPlaceArena, path);
        state->dragRequested = true;
    }
    if (interaction.pressed && interaction.button == MouseButton_Right) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SidebarMenu, (u64)group, 0, interaction.pressOrder);
        command->target = index;
    }
    // A place, or a folder in a group of the user's, dragged moves among its group's, before the one the line the list draws is at; the click
    // its release makes opens nothing.
    b32 moved = false, draggable = group == Prog_Group_Places || (u32)group >= Prog_Group_Count;
    if (draggable && interaction.active && interaction.isDragging && interaction.button == MouseButton_Left) {
        state->sidebarDrag = index + 1;
        state->sidebarDragGroup = (u32)group + 1;
    }
    if (draggable && interaction.released && state->sidebarDrag == index + 1 && state->sidebarDragGroup == (u32)group + 1) {
        if (state->sidebarGap && group == Prog_Group_Places) Prog_QueueCommand(state, Prog_CommandKind_PlaceMove, index, 0, interaction.clickOrder)->target = state->sidebarGap - 1;
        if (state->sidebarGap && group != Prog_Group_Places) Prog_QueueCommand(state, Prog_CommandKind_GroupItemMove, (u64)group, 0, interaction.clickOrder)->target = index | ((state->sidebarGap - 1) << 32);
        state->sidebarDrag = state->sidebarGap = 0;
        state->sidebarDragGroup = 0;
        moved = true;
    }
    // Ctrl opens it in a new tab, as the middle button does.
    b32 inTab = interaction.button == MouseButton_Middle || (interaction.button == MouseButton_Left && (UI->mods & KeyMod_Control));
    if (interaction.clicked && interaction.button == MouseButton_Left && !inTab && !moved) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SidebarOpen, (u64)group, 0, interaction.clickOrder);
        command->target = index;
        // One on the folder shown that is no double click's renames it, once the double-click time shows it is none, as in Explorer.
        if (interaction.pressCount == 1 && StrIsEqual(path, state->directory.path) && Prog__SidebarRenamable(group, path)) Prog__AskSidebarRename(state, group, index);
    }
    if (UI->hotKey == key && !UI->activeKey && path.length && !StrIsEqual(path, Prog_ThisPC)) {
        state->tooltipKey = key;
        state->tooltipRect = row->rect;
        state->tooltipPath = path;
    }
    if (interaction.clicked && inTab) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SidebarOpenInTab, (u64)group, 0, interaction.clickOrder);
        command->target = index;
    }
    UI_Parent(row) {
        f32 slot = (f32)state->iconSize + floorf(7.2f * m->scale + 0.5f);
        // A drive spinning up a disc shows a spinner until it answers.
        const Prog_Icon *spinner = drive && drive->busy ? Prog_SpinnerIcon(state, UI->nowMicroseconds) : NULL;
        if (spinner) UI_KeepAnimating();
        const Prog_Icon *icon = spinner ? spinner : drive ? Prog_PathIcon(state, path, P_FileKind_Drive) : Prog_FolderIcon(state, path);
        if (icon) {
            UI_SetNextPrefWidth(UI_Pixels(slot));
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, spinner ? theme->icon : (R_Color){ 1, 1, 1, faded ? theme->hiddenAmount.r : 1 });
            UI_AddBlock(UI_Flag_DrawImage);
        } else {
            Prog__Icon(drive ? Icon_HardDrive : StrIsEqual(path, Prog_ThisPC) ? Icon_Monitor : Icon_Folder, slot, m->row, faded ? theme->iconDisabled : theme->icon, 0, m);
        }
        // Folders and drives have the folders' color, This PC the text's; what the sidebar's filter found is in the match color.
        R_Color color = hot ? theme->contentHover : faded && !current ? theme->textDim : StrIsEqual(path, Prog_ThisPC) ? theme->text : theme->folder;
        u8 *marks = state->sidebarFilter.length ? ArenaPushArray(&state->frameArena, name.length + 1, u8) : NULL;
        if (marks && !Prog_FilterMatch(name, Prog_EditText(&state->sidebarFilter), marks)) marks = NULL;
        if (drive && drive->totalBytes) {
            // The name sits a little high to leave room for the bar under it.
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_Block *stack = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(stack) {
                f32 bar = MaxF32(2.0f, floorf(4.0f * m->scale + 0.5f));
                f32 width = floorf(76.8f * m->scale);
                f32 used = floorf(width * (f32)(drive->totalBytes - Min(drive->freeBytes, drive->totalBytes)) / (f32)drive->totalBytes);
                f32 top = floorf((m->row - m->line - bar * 2) * 0.5f);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, top });
                UI_SetNextPrefWidth(UI_Percent(1));
                UI_SetNextPrefHeight(UI_Pixels(m->line));
                UI_Block *named = UI_AddBlock(UI_Flag_Floating);
                UI_Parent(named) Prog__MarkedText(name, marks, m->font, m->line, color);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, top + m->line + bar });
                UI_SetNextPrefWidth(UI_Pixels(width));
                UI_SetNextPrefHeight(UI_Pixels(bar));
        UI_SetNextColors((UI_Colors){ .background = theme->track });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, top + m->line + bar });
                UI_SetNextPrefWidth(UI_Pixels(used));
                UI_SetNextPrefHeight(UI_Pixels(bar));
                UI_SetNextColors((UI_Colors){ .background = theme->accent });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        } else if (state->layoutSaving && state->nameFor == 2 && state->nameGroup == (u32)group && state->nameIndex == index) {
            Prog__SidebarNameBox(state, m->row, m);
        } else {
            Prog__MarkedText(name, marks, m->font, m->row, color);
        }
        // A bookmark's x removes it, as a folder's in a group of the user's does; a place's eye hides it, or shows it again while hidden ones show.
        if ((group == Prog_Group_Bookmarks || group == Prog_Group_Places || (u32)group >= Prog_Group_Count) && hot && !state->sidebarDrag) {
            b32 removeHot = UI->hotKey == removeKey;
            f32 size = floorf(m->line * 1.3f);
            UI_SetNextPrefWidth(UI_Pixels(size));
            UI_SetNextPrefHeight(UI_Pixels(size));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(m->radius);
            UI_SetNextFont(m->icons);
            UI_SetNextColors((UI_Colors){ .background = theme->hover, .text = removeHot ? theme->text : theme->icon });
            // An x, as everything that removes or deletes something from a list has.
            UI_SetNextText(group != Prog_Group_Places ? Icon_X : faded ? Icon_Eye : Icon_EyeOff);
            UI_Block *remove = UI_AddBlockFromKey(removeKey, UI_Flag_DrawText | UI_Flag_LeftMouseClickable | (removeHot ? UI_Flag_DrawBackground : 0));
            UI_Interaction removing = UI_InteractionFromBlock(remove);
            if (removing.clicked && group == Prog_Group_Bookmarks) Prog_QueueCommand(state, Prog_CommandKind_RemoveBookmark, index, 0, removing.clickOrder);
            if (removing.clicked && group == Prog_Group_Places) Prog_QueueCommand(state, Prog_CommandKind_PlaceHiddenToggle, index, 0, removing.clickOrder);
            if (removing.clicked && (u32)group >= Prog_Group_Count) Prog_QueueCommand(state, Prog_CommandKind_GroupItemRemove, (u64)group, 0, removing.clickOrder)->target = index;
        }
    }
}

// The box for a group's items, which eases open and shut over its whole height, that of its items in the last frame; null while it is shut,
// when the items need no blocks.
function UI_Block *Prog__GroupBody(Prog_State *state, Prog_Group group, const Prog_Metrics *m)
{
    // A filter shows what it finds in every group.
    b32 open = Prog_GroupOpen(state, group) || state->sidebarFilter.length;
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "GroupBody/%u", (u32)group));
    UI_Block *last = UI__Find(key);
    f32 full = last ? last->contentSize.y : 0;
    f32 shown = UI_Animate(UI_KeyFromStr(key, S("Shown")), open ? 1.0f : 0.0f, open ? 1.0f : 0.0f);
    if (!open && shown <= 0) return NULL;
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(shown >= 1 ? UI_Fill() : UI_Pixels(floorf(full * shown + 0.5f)));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    return UI_AddBlockFromKey(key, UI_Flag_Clip);
}

// Whether the Quick Access filter leaves the sidebar the item named `name`.
function b32 Prog__SidebarFinds(const Prog_State *state, String name)
{
    return !state->sidebarFilter.length || Prog_FilterMatch(name, Prog_EditText(&state->sidebarFilter), NULL);
}

// The Quick Access filter's box over the sections: a press starts filtering, and while it holds text a button clears it.
function void Prog__SidebarFilterBox(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 strip = Prog__StripHeight(m);
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(strip));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_Block *filter = UI_AddBlockFromKey(UI_KeyFromStr(0, S("SidebarFilter")), UI_Flag_LeftMouseClickable);
    UI_Interaction press = UI_InteractionFromBlock(filter);
    if (press.pressed && !state->sidebarFiltering) Prog_QueueCommand(state, Prog_CommandKind_StartQuickAccessFilter, 0, 0, press.pressOrder);
    state->sidebarFilterRect = filter->rect;
    UI_Parent(filter) {
        Prog__Spacer(UI_Pixels(floorf(9.6f * m->scale + 0.5f)), UI_Pixels(strip));
        Prog__SearchIcon(0, strip, theme->textDim, 0, 0, m);
        f32 pad = floorf(8.8f * m->scale + 0.5f);
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(strip));
        UI_Block *text = UI_AddBlock(UI_Flag_Clip);
        UI_Parent(text) {
            f32 top = floorf((strip - m->line) * 0.5f);
            if (!state->sidebarFilter.length) {
                UI_SetNextFloatingOffset((UI_Vec2){ pad, top });
                UI_SetNextPrefHeight(UI_Pixels(m->line));
                UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                UI_SetNextText(Tr("Filter..."));
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
            }
            if (state->sidebarFilter.length || state->sidebarFiltering) Prog__EditContents(&state->sidebarFilter, pad, top, state->sidebarFiltering, m);
        }
        if (state->sidebarFilter.length) {
            UI_Interaction clear = Prog__TextButton(S("SidebarFilterClear"), Icon_X, m->icons, UI_Pixels(floorf(24.0f * m->scale + 0.5f)), strip, 0, theme->textDim, theme->text,
                                                    theme->iconDisabled, true, m);
            if (clear.clicked) Prog_QueueCommand(state, Prog_CommandKind_QuickAccessFilterCancel, 0, 0, clear.clickOrder);
        }
        // A line across the whole sidebar under the box, level with the toolbar's.
        f32 side = floorf(m->gap * 0.75f);
        UI_SetNextFloatingOffset((UI_Vec2){ -side, strip - m->separator });
        UI_SetNextPrefWidth(UI_Pixels(m->sidebar));
        UI_SetNextPrefHeight(UI_Pixels(m->separator));
        UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
}

// Quick Access: recently shown folders, bookmarks, drives and the user's folders, each under a title that opens and closes it. A filter leaves
// each section the items whose names it finds, and the sections without any out.
function void Prog__Sidebar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    state->placesWanted = true;
    UI_SetNextPrefWidth(UI_Pixels(m->sidebar));
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ floorf(m->gap * 0.75f), 0, floorf(m->gap * 0.75f), m->gap });
    UI_SetNextColors((UI_Colors){ .background = theme->panel });
    UI_SetNextEdgeFade(floorf(m->row * 0.75f));
    UI_Block *sidebar =UI_AddBlockFromKey(UI_KeyFromStr(0, S("Sidebar")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable | UI_Flag_RightMouseClickable);
    // The rename a slow click asked for starts once the double-click time passed without another press.
    if (state->sidebarRenameAt && UI->lastPressTime != state->sidebarRenamePress) {
        state->sidebarRenameAt = 0;
    } else if (state->sidebarRenameAt && UI->nowMicroseconds >= state->sidebarRenameAt) {
        Prog_QueueCommand(state, Prog_CommandKind_SidebarRename, state->sidebarRenameGroup - 1, 0, 0)->target = state->sidebarRenameIndex;
        state->sidebarRenameAt = 0;
    } else if (state->sidebarRenameAt) {
        UI_KeepAnimating();
    }
    // A right click on its empty space, where no group or place takes it, opens the sidebar's own menu.
    UI_Interaction background = UI_InteractionFromBlock(sidebar);
    if (background.pressed && background.button == MouseButton_Right) Prog_QueueCommand(state, Prog_CommandKind_SidebarBackgroundMenu, 0, 0, background.pressOrder)->at = UI->mouse;
    UI_Parent(sidebar) {
        Prog__SidebarFilterBox(state, m);
        Prog__Spacer(UI_Flex(), UI_Pixels(floorf(4.8f * m->scale + 0.5f)));
        // The groups, in the order they were put in.
        // A group's title dragged opens a gap, easing, where it would go, as the bookmarks do for a folder dragged among them.
        f32 groupApart = floorf(12.8f * m->scale + 0.5f), groupGaps[Prog_SidebarGroupMax + 1] = {0};
        for (u32 n = 0; n <= Prog_SidebarGroupCount(state) && n <= Prog_SidebarGroupMax; ++n) {
            UI_Key gapKey = UI_KeyFromStr(0, StrFormat(&state->frameArena, "GroupGap/%u", n));
            f32 target = state->groupDrag && state->groupGap == n + 1 ? groupApart : 0;
            if (target != 0 || UI_Kept(gapKey) != 0) groupGaps[n] = floorf(UI_Animate(gapKey, target, 0) + 0.5f);
        }
        for (u32 n = 0; n <= Prog_SidebarGroupCount(state); ++n) {
            if (groupGaps[n] > 0) Prog__Spacer(UI_Flex(), UI_Pixels(groupGaps[n]));
            if (n == Prog_SidebarGroupCount(state)) break;
            u32 id = Prog_SidebarGroupAt(state, n);
            switch (id) {
                case Prog_Group_Recents: {
                    u64 indices[Prog_ShownRecentCount];
                    u32 shown = 0, listed = Prog_ShownRecents(state, indices);
                    for (u32 n = 0; n < listed; ++n) if (Prog__SidebarFinds(state, Prog_PathName(state, state->recents.v[indices[n]]))) indices[shown++] = indices[n];
                    if (shown || !state->sidebarFilter.length) Prog__GroupTitle(state, Prog_Group_Recents, Tr("Recents"), Icon_Clock, m);
                    UI_Block *body = shown || !state->sidebarFilter.length ? Prog__GroupBody(state, Prog_Group_Recents, m) : NULL;
                    if (body) UI_Parent(body) {
                        // Each row eases to its place by its folder, so that one opened again travels up over the others while they move down to make
                        // room for it; a folder new to the list starts in its place.
                        f32 slid[Prog_ShownRecentCount], placed[Prog_ShownRecentCount];
                        for (u32 n = 0; n < shown; ++n) {
                            placed[n] = (f32)n * m->row;
                            slid[n] = UI_Animate(UI_KeyFromStr(0, StrFormat(&state->frameArena, "RecentY/%S", state->recents.v[indices[n]])), placed[n], placed[n]);
                        }
                        Prog__Spacer(UI_Flex(), UI_Pixels((f32)shown * m->row));
                        for (u32 pass = 0; pass < 2; ++pass) {
                            for (u32 n = 0; n < shown; ++n) {
                                // Those rising are drawn last, over the rows they pass.
                                if ((slid[n] > placed[n] + 0.5f) != (pass == 1)) continue;
                                u64 i = indices[n];
                                String path = state->recents.v[i];
                                Prog__SidebarItem(state, Prog_Group_Recents, i, Prog_PathName(state, path), path, NULL, Prog_RecentFaded(state, i), m);
                                UI_Block *row = body->last;
                                row->flags |= UI_Flag_Floating;
                                // Rounded rather than floored, or a row easing sits a pixel short through the tail of the ease and then jumps.
                                row->props.floatingOffset = (UI_Vec2){ 0, floorf(slid[n] + 0.5f) };
                            }
                        }
                    }
                } break;
                case Prog_Group_Bookmarks: {
                    u64 count = state->bookmarks.header.count, found = 0;
                    for (u64 i = 0; i < count; ++i) found += Prog__SidebarFinds(state, Prog_PathName(state, state->bookmarks.v[i])) ? 1 : 0;
                    if (found || !state->sidebarFilter.length) Prog__GroupTitle(state, Prog_Group_Bookmarks, Tr("Bookmarks"), Icon_Bookmark, m);
                    // A drag over the section pins its folders between the bookmarks the pointer is between, marked by a line there; the rects are the last layout's.
                    R_Rect title = sidebar->last ? sidebar->last->rect : (R_Rect){0}, zone = title;
                    u64 before = count;
                    f32 lineY = title.y1;
                    UI_Block *body = found || !state->sidebarFilter.length ? Prog__GroupBody(state, Prog_Group_Bookmarks, m) : NULL;
                    if (body) UI_Parent(body) {
                        for (u64 i = 0; i < count; ++i) {
                            String path = state->bookmarks.v[i];
                            if (!Prog__SidebarFinds(state, Prog_PathName(state, path))) continue;
                            // One whose folder is gone, renamed or deleted elsewhere or on a drive taken away, is faded rather than dropped.
                            b32 missing = false;
                            for (u64 j = 0; j < state->missingBookmarks.header.count && !missing; ++j) missing = StrIsEqual(state->missingBookmarks.v[j], path);
                            Prog__SidebarItem(state, Prog_Group_Bookmarks, i, Prog_PathName(state, path), path, NULL, missing, m);
                            R_Rect row = body->last->rect;
                            zone.y1 = row.y1;
                            if (before == count && UI->mouse.y < (row.y0 + row.y1) * 0.5f) {
                                before = i;
                                lineY = row.y0;
                            } else if (before == count) {
                                lineY = row.y1;
                            }
                        }
                    }
                    // Not while filtering, which leaves bookmarks out from between the others.
                    b32 inZone = UI->mouse.x >= sidebar->rect.x0 && UI->mouse.x < sidebar->rect.x1 && UI->mouse.y >= zone.y0 && UI->mouse.y < zone.y1 && !state->sidebarFilter.length;
                    state->bookmarkGap = P_DropPending() && inZone && !state->dropPlace.length ? before + 1 : 0;
                    if (state->bookmarkGap) {
                        f32 thickness = MaxF32(2, floorf(2 * m->scale)), left = floorf(12.0f * m->scale + 0.5f), right = floorf(14.4f * m->scale + 0.5f);
                        // Floating offsets start inside the sidebar's padding.
                        UI_SetNextFloatingOffset((UI_Vec2){ left - floorf(m->gap * 0.75f), floorf(lineY) - sidebar->rect.y0 });
                        UI_SetNextPrefWidth(UI_Pixels(sidebar->rect.x1 - sidebar->rect.x0 - left - right));
                        UI_SetNextPrefHeight(UI_Pixels(thickness));
                        UI_SetNextColors((UI_Colors){ .background = theme->accent });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    }
                    state->bookmarkDropShown = state->bookmarkGap;
                } break;
                case Prog_Group_Storage: {
                    u64 found = 0;
                    for (u64 i = 0; i < state->drives.header.count; ++i) found += Prog__SidebarFinds(state, state->drives.v[i].name) ? 1 : 0;
                    for (u64 i = 0; i < state->shares.header.count; ++i) found += Prog__SidebarFinds(state, state->shares.v[i].name) ? 1 : 0;
                    if (found || !state->sidebarFilter.length) Prog__GroupTitle(state, Prog_Group_Storage, Tr("Storage"), (String){0}, m);
                    UI_Block *body = found || !state->sidebarFilter.length ? Prog__GroupBody(state, Prog_Group_Storage, m) : NULL;
                    if (body) UI_Parent(body) {
                        for (u64 i = 0; i < state->drives.header.count; ++i) {
                            const P_Place *drive = &state->drives.v[i];
                            if (Prog__SidebarFinds(state, drive->name)) Prog__SidebarItem(state, Prog_Group_Storage, i, drive->name, drive->path, drive, false, m);
                        }
                        // Then the shares of the servers the network drives, the bookmarks and the recents are on, as drives.
                        for (u64 i = 0; i < state->shares.header.count; ++i) {
                            const P_Place *share = &state->shares.v[i];
                            if (Prog__SidebarFinds(state, share->name)) Prog__SidebarItem(state, Prog_Group_Storage, state->drives.header.count + i, share->name, share->path, share, false, m);
                        }
                    }
                } break;
                case Prog_Group_Places: {
                    // Hidden places show only while asked for, faded, to show them again; a place dragged among the others is marked by a line where it goes.
                    u64 count = state->places.header.count, found = 0;
                    for (u64 i = 0; i < count; ++i) {
                        const P_Place *place = &state->places.v[i];
                        found += Prog__SidebarFinds(state, place->name) && (state->placesShowHidden || !Prog_PlaceHidden(state, place->path)) ? 1 : 0;
                    }
                    if (found || !state->sidebarFilter.length) Prog__GroupTitle(state, Prog_Group_Places, Tr("Places"), Icon_Folder, m);
                    UI_Block *body = found || !state->sidebarFilter.length ? Prog__GroupBody(state, Prog_Group_Places, m) : NULL;
                    u64 before = count;
                    f32 lineY = 0;
                    R_Rect zone = {0};
                    if (body) UI_Parent(body) {
                        for (u64 i = 0; i < count; ++i) {
                            const P_Place *place = &state->places.v[i];
                            b32 hidden = Prog_PlaceHidden(state, place->path);
                            if ((hidden && !state->placesShowHidden) || !Prog__SidebarFinds(state, place->name)) continue;
                            b32 named = StrIsEqual(place->path, Prog_ThisPC) || StrIsEqual(place->path, P_NetworkFolder());
                            Prog__SidebarItem(state, Prog_Group_Places, i, named ? Prog_PathName(state, place->path) : place->name, place->path, NULL, hidden, m);
                            R_Rect row = body->last->rect;
                            zone = zone.y1 > zone.y0 ? (R_Rect){ zone.x0, zone.y0, zone.x1, row.y1 } : row;
                            if (before == count && UI->mouse.y < (row.y0 + row.y1) * 0.5f) {
                                before = i;
                                lineY = row.y0;
                            } else if (before == count) {
                                lineY = row.y1;
                            }
                        }
                    }
                    // Only over the places, and not while filtering, which leaves some out from between the others.
                    b32 inZone = UI->mouse.x >= sidebar->rect.x0 && UI->mouse.x < sidebar->rect.x1 && UI->mouse.y >= zone.y0 - m->row * 0.5f && UI->mouse.y < zone.y1 + m->row * 0.5f;
                    if (state->sidebarDragGroup == Prog_Group_Places + 1) state->sidebarGap = state->sidebarDrag && body && inZone && !state->sidebarFilter.length ? before + 1 : 0;
                    if (state->sidebarDragGroup == Prog_Group_Places + 1 && state->sidebarGap && lineY > 0) {
                        f32 thickness = MaxF32(2, floorf(2 * m->scale)), left = floorf(12.0f * m->scale + 0.5f), right = floorf(14.4f * m->scale + 0.5f);
                        UI_SetNextFloatingOffset((UI_Vec2){ left - floorf(m->gap * 0.75f), floorf(lineY) - sidebar->rect.y0 });
                        UI_SetNextPrefWidth(UI_Pixels(sidebar->rect.x1 - sidebar->rect.x0 - left - right));
                        UI_SetNextPrefHeight(UI_Pixels(thickness));
                        UI_SetNextColors((UI_Colors){ .background = theme->accent });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    }
                } break;
                default: {
                    // A group of the user's: its folders, which folders dragged onto it from anywhere join and which move by dragging, with a line
                    // where one would go; empty, it says so.
                    u32 own = id - Prog_Group_Count;
                    if (own >= state->userGroupCount) break;
                    const Prog_UserGroup *userGroup = &state->userGroups[own];
                    u64 count = userGroup->folders.header.count, found = 0;
                    for (u64 i = 0; i < count; ++i) found += Prog__SidebarFinds(state, Prog_PathName(state, userGroup->folders.v[i])) ? 1 : 0;
                    if (!found && state->sidebarFilter.length) break;
                    Prog__GroupTitle(state, (Prog_Group)id, userGroup->name, Icon_Folder, m);
                    R_Rect zone = sidebar->last ? sidebar->last->rect : (R_Rect){0};
                    UI_Block *body = Prog__GroupBody(state, (Prog_Group)id, m);
                    u64 before = count;
                    f32 lineY = zone.y1;
                    if (body) UI_Parent(body) {
                        if (!count) Prog__Label(Tr("Drag folders here"), UI_Flex(), m->row, floorf(m->line * 1.5f + m->gap * 0.5f) + floorf(3.2f * m->scale + 0.5f), theme->textDim, 0);
                        for (u64 i = 0; i < count; ++i) {
                            String path = userGroup->folders.v[i];
                            if (!Prog__SidebarFinds(state, Prog_PathName(state, path))) continue;
                            Prog__SidebarItem(state, (Prog_Group)id, i, Prog_PathName(state, path), path, NULL, false, m);
                            R_Rect row = body->last->rect;
                            if (before == count && UI->mouse.y < (row.y0 + row.y1) * 0.5f) {
                                before = i;
                                lineY = row.y0;
                            } else if (before == count) {
                                lineY = row.y1;
                            }
                        }
                        zone.y1 = MaxF32(zone.y1, body->rect.y1);
                    }
                    b32 inZone = UI->mouse.x >= sidebar->rect.x0 && UI->mouse.x < sidebar->rect.x1 && UI->mouse.y >= zone.y0 && UI->mouse.y < zone.y1 && !state->sidebarFilter.length;
                    b32 dragging = state->sidebarDrag && state->sidebarDragGroup == id + 1, dropping = P_DropPending() && !state->dropPlace.length && inZone;
                    if (dragging) state->sidebarGap = inZone ? before + 1 : 0;
                    if (dropping) {
                        state->userGroupDropShown = before + 1;
                        state->userGroupDropShownIn = own + 1;
                    }
                    if ((dragging && inZone) || dropping) {
                        f32 thickness = MaxF32(2, floorf(2 * m->scale)), left = floorf(12.0f * m->scale + 0.5f), right = floorf(14.4f * m->scale + 0.5f);
                        UI_SetNextFloatingOffset((UI_Vec2){ left - floorf(m->gap * 0.75f), floorf(lineY) - sidebar->rect.y0 });
                        UI_SetNextPrefWidth(UI_Pixels(sidebar->rect.x1 - sidebar->rect.x0 - left - right));
                        UI_SetNextPrefHeight(UI_Pixels(thickness));
                        UI_SetNextColors((UI_Colors){ .background = theme->accent });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    }
                } break;
            }
        }
        // A group's title dragged goes before the group whose upper half the pointer is over, or last, marked by a line there.
        // One whose press the system took away ends without a move.
        if (state->groupDrag && !UI->activeKey) state->groupDrag = state->groupGap = 0;
        if (state->groupDrag) {
            u32 count = Prog_SidebarGroupCount(state), before = count;
            f32 end = sidebar->rect.y0 + sidebar->contentSize.y - sidebar->scrollShown.y - groupGaps[Min(count, (u32)Prog_SidebarGroupMax)];
            f32 lineY = end + groupGaps[Min(count, (u32)Prog_SidebarGroupMax)] * 0.5f;
            for (u32 n = 0; n < count && before == count; ++n) {
                f32 top = state->groupTops[Prog_SidebarGroupAt(state, n)], next = n + 1 < count ? state->groupTops[Prog_SidebarGroupAt(state, n + 1)] : end;
                if (UI->mouse.y < (top + next) * 0.5f) {
                    before = n;
                    lineY = top - groupGaps[n] * 0.5f;
                }
            }
            // Just above or below itself it stays where it is, so neither is offered.
            u32 at = count;
            for (u32 n = 0; n < count; ++n) if (Prog_SidebarGroupAt(state, n) == state->groupDrag - 1) at = n;
            state->groupGap = before == at || before == at + 1 ? 0 : before + 1;
            if (state->groupGap) {
                f32 thickness = MaxF32(2, floorf(2 * m->scale)), left = floorf(12.0f * m->scale + 0.5f), right = floorf(14.4f * m->scale + 0.5f);
                UI_SetNextFloatingOffset((UI_Vec2){ left - floorf(m->gap * 0.75f), floorf(lineY) - sidebar->rect.y0 });
                UI_SetNextPrefWidth(UI_Pixels(sidebar->rect.x1 - sidebar->rect.x0 - left - right));
                UI_SetNextPrefHeight(UI_Pixels(thickness));
                UI_SetNextColors((UI_Colors){ .background = theme->accent });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        }
    }
}

// A key chip as the menus and the palette show hotkeys, centered in a slot `slotHeight` tall; `lit` draws it for a highlighted row, and `idle`
// edges it in the danger color for a hotkey that does nothing now.
function void Prog__Chip(String text, f32 slotHeight, b32 lit, b32 idle, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 pad = floorf(4.8f * m->scale + 0.5f), height = floorf(m->line * 1.35f + 0.5f);
    UI_SetNextPrefWidth(UI_Pixels(MaxF32(R_TextWidth(m->detail, text) + 2 * pad, floorf(30.4f * m->scale + 0.5f))));
    UI_SetNextPrefHeight(UI_Pixels(slotHeight));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_Block *slot = UI_AddBlock(0);
    UI_Parent(slot) {
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_SetNextCornerRadius(floorf(4.0f * m->scale + 0.5f));
        UI_SetNextBorderThickness((idle ? 1.2f : 0.8f) * m->scale);
        UI_SetNextFont(m->detail);
        UI_SetNextColors((UI_Colors){ .background = theme->track, .border = idle ? theme->danger : lit ? theme->text : theme->outline, .text = lit ? theme->text : theme->icon });
        UI_SetNextText(text);
        UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText);
    }
}

// How wide Prog__HotkeyChipButton draws `chip`.
function f32 Prog__HotkeyChipWidth(const Prog_HotkeyChip *chip, const Prog_Metrics *m)
{
    f32 pad = floorf(4.8f * m->scale + 0.5f), height = floorf(m->line * 1.35f + 0.5f);
    return MaxF32(R_TextWidth(m->detail, chip->text) + 2 * pad, floorf(30.4f * m->scale + 0.5f)) + (chip->recording && !chip->adds ? height : 0);
}

// The commands other than `kind` that hotkey `slot` of `kind` also runs, a line each, after the words that say so.
function String Prog__ConflictText(Arena *arena, const Prog_State *state, Prog_CommandKind kind, u32 slot)
{
    const Prog_Hotkey *hotkeys[16];
    u32 count = Prog_CommandHotkeys(state, kind, hotkeys, CArrayCount(hotkeys));
    StrBuilder out = {0};
    StrBuildStr(arena, &out, Tr("Conflicts with commands:"));
    if (slot >= count) return out.buffer;
    const Prog_Hotkey *hotkey = hotkeys[slot];
    for (u32 k = Prog_CommandKind_None + 1; k < Prog_CommandKind_Count; ++k) {
        if (k == (u32)kind || Prog_Commands[k].scope != Prog_Commands[kind].scope) continue;
        const Prog_Hotkey *others[16];
        u32 found = Prog_CommandHotkeys(state, (Prog_CommandKind)k, others, CArrayCount(others));
        for (u32 i = 0; i < found; ++i) {
            if (others[i]->key != hotkey->key || others[i]->mods != hotkey->mods || others[i]->lead != hotkey->lead) continue;
            StrBuildChar(arena, &out, '\n');
            StrBuildStr(arena, &out, Prog_Commands[k].label.length ? Prog_CommandLabel((Prog_CommandKind)k) : Prog_Commands[k].name);
            break;
        }
    }
    return out.buffer;
}

// A chip of `kind`'s hotkeys, as Prog__Chip draws one, that a click rebinds: solid in the danger color while another command has its key, edged
// in it while idle; while it is recording, "Press A Key" edged in the danger color with a button that deletes the hotkey, or for a new one filled
// in the selection color. `identity` tells apart the rows that show the same command's chips.
function void Prog__HotkeyChipButton(Prog_State *state, String identity, Prog_CommandKind kind, const Prog_HotkeyChip *chip, f32 slotHeight, b32 lit, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    f32 height = floorf(m->line * 1.35f + 0.5f), radius = floorf(4.0f * m->scale + 0.5f);
    b32 mouse = chip->slot == UINT8_MAX, cross = chip->recording && !chip->adds;
    if (kind == state->hotkeyKind && chip->slot == state->hotkeySlot) state->hotkeyChipShown = true;
    UI_SetNextPrefWidth(UI_Pixels(Prog__HotkeyChipWidth(chip, m)));
    UI_SetNextPrefHeight(UI_Pixels(slotHeight));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_Block *slot = UI_AddBlock(0);
    UI_Parent(slot) {
        UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Hotkey/%S/%u/%u", identity, (u32)kind, (u32)chip->slot));
        b32 hot = !mouse && UI->hotKey == key;
        R_Color background = chip->conflict ? theme->danger : chip->adds ? theme->selection : theme->track;
        R_Color border = cross || chip->idle || chip->conflict ? theme->danger : lit || hot || chip->adds ? theme->text : theme->outline;
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextCornerRadius(radius);
        UI_SetNextBorderThickness((cross || chip->idle || chip->conflict ? 1.2f : 0.8f) * m->scale);
        UI_SetNextColors((UI_Colors){ .background = background, .border = border });
        UI_Block *face = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | (mouse ? 0 : UI_Flag_LeftMouseClickable));
        UI_Interaction interaction = UI_InteractionFromBlock(face);
        if (interaction.clicked && !chip->recording) Prog_QueueCommand(state, Prog_CommandKind_HotkeyRecord, (u64)kind, 0, interaction.clickOrder)->target = chip->slot;
        UI_Parent(face) {
            UI_SetNextFont(m->detail);
            Prog__Label(chip->text, UI_Flex(), height, 0, lit || hot || chip->conflict || chip->recording ? theme->text : theme->icon, 0.5f);
            if (cross) {
                UI_Key crossKey = UI_KeyFromStr(key, S("Delete"));
                R_Color fill = theme->danger;
                if (UI->hotKey != crossKey) fill.a *= 0.85f;
                UI_SetNextPrefWidth(UI_Pixels(height));
                UI_SetNextPrefHeight(UI_Pixels(height));
                UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                UI_SetNextFont(m->iconsSmall);
                UI_SetNextColors((UI_Colors){ .background = fill, .text = theme->text });
                UI_SetNextText(Icon_X);
                UI_Block *remove = UI_AddBlockFromKey(crossKey, UI_Flag_DrawBackground | UI_Flag_DrawText | UI_Flag_LeftMouseClickable);
                UI_Interaction removing = UI_InteractionFromBlock(remove);
                if (removing.clicked) {
                    Prog_Command *ask = Prog_QueueCommand(state, Prog_CommandKind_HotkeyDeleteAsk, (u64)kind, 0, removing.clickOrder);
                    ask->target = chip->slot;
                    ask->at = (UI_Vec2){ remove->rect.x0, remove->rect.y1 + floorf(4.0f * m->scale + 0.5f) };
                }
                Prog__PopupTooltip(state, Tr("Delete hotkey."));
            }
        }
        String tip = mouse ? Tr("Mouse hotkeys cannot be changed.") : chip->conflict ? Prog__ConflictText(frame, state, kind, chip->slot)
                   : chip->idle ? Tr("Character Key Action not set to Trigger Hotkey.") : Tr("Press to set or remove hotkey.");
        if (!chip->recording) Prog__PopupTooltip(state, tip);
    }
}

// Asks before the delete button of a chip being rebound deletes its hotkey, in a box under the button; a click anywhere else keeps it.
function void Prog__HotkeyQuestion(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->hotkeyAsking) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("HotkeyQuestionCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_HotkeyCancel, 0, 0, outside.pressOrder);
    String title = Tr("Delete assigned hotkey?");
    f32 button = floorf(96.0f * m->scale + 0.5f), gap = floorf(8.0f * m->scale + 0.5f), pad = floorf(8.0f * m->scale + 0.5f);
    f32 width = MaxF32(R_TextWidth(m->font, title) + 2 * m->inset, 2 * button + gap + 2 * pad), height = m->row + m->bar + 3 * pad;
    f32 x = MinF32(state->hotkeyAskAt.x, view.x1 - width - m->inset), y = MinF32(state->hotkeyAskAt.y, view.y1 - height - m->inset);
    UI_SetNextPrefWidth(UI_Pixels(floorf(width)));
    UI_SetNextPrefHeight(UI_Pixels(floorf(height)));
    UI_SetNextFloatingOffset((UI_Vec2){ floorf(MaxF32(x, view.x0) - view.x0), floorf(MaxF32(y, view.y0) - view.y0) });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
    UI_SetNextCornerRadius(m->radius * 2);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("HotkeyQuestion")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(box) {
        Prog__Label(title, UI_Flex(), m->row, 0, theme->text, 0.5f);
        Prog__Spacer(UI_Flex(), UI_Pixels(pad));
        UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(buttons) for (u32 b = 0; b < 2; ++b) {
            UI_Key key = UI_KeyFromStr(0, b ? S("HotkeyQuestionCancel") : S("HotkeyQuestionDelete"));
            if (b) Prog__Spacer(UI_Pixels(gap), UI_Pixels(m->bar));
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->bar));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(floorf(4.0f * m->scale + 0.5f));
            UI_SetNextBorderThickness(0.8f * m->scale);
            UI_SetNextColors((UI_Colors){ .background = UI->hotKey == key ? theme->hover : theme->track, .border = theme->outline, .text = theme->text });
            UI_SetNextText(b ? Tr("Cancel") : Tr("Delete"));
            UI_Interaction pressed = UI_InteractionFromBlock(UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText | UI_Flag_LeftMouseClickable));
            if (pressed.clicked && b) Prog_QueueCommand(state, Prog_CommandKind_HotkeyCancel, 0, 0, pressed.clickOrder);
            if (pressed.clicked && !b) Prog_QueueCommand(state, Prog_CommandKind_HotkeyDelete, state->hotkeyKind, 0, pressed.clickOrder)->target = state->hotkeySlot;
        }
    }
}

typedef struct Prog_MenuPlace {
    u32 first, count;
    f32 x, y;
    f32 openerWidth;  // width of the panel that opened this one; 0 for the top level
    R_Rect opener;    // the row that opened this one, empty when it is out of sight
    R_Rect openerPanel;
    b32 leftward;     // the panel that opened this one opened to the left of its own opener
} Prog_MenuPlace;

// A submenu against its opener's panel becomes one shape with it, joined to the opener row as the shown tab is to the listing: the two sit side by side
// with one line between them, the line curving into the row's lines where there is room. Where an end of the submenu meets the panel's edge, the edges
// meet square. Each panel's border leaves out the whole side where they touch, which is drawn here instead as pieces of box outlines, the way the
// borders are, each inside its own whole-pixel clip: so the lines are as thick as the borders, their corners meet as a box's do, and no pixel is drawn
// twice, which a translucent theme would show lighter. What neither panel covers is filled as they are.
#define Prog_MenuGapCount 8

typedef struct Prog_MenuJoin {
    f32 mirror;                  // worked out for a submenu on the right, and mirrored across this for one on the left
    R_Rect parent, child, row;   // mirrored; the row on whole pixels
    f32 line;                    // the x where the panels meet, the panel's edge to its left and the submenu's to its right
    // Corners on the side where they touch that are squared off rather than rounded, because the other panel goes on past them or ends near them.
    b32 parentSquareTop, parentSquareBottom, childSquareTop, childSquareBottom;
    b32 rowTop;                  // the submenu starts level with the row, whose top line runs on as its own
    f32 topFlare, bottomFlare;   // the curves of the panel's edge into the row's lines
    u32 childShape, parentShape; // the panels' squared corners, unmirrored
    R_Rect parentGap, childGap;  // in root coordinates
} Prog_MenuJoin;

function R_Rect Prog__Mirror(R_Rect rect, f32 mirror)
{
    return mirror != 0 ? (R_Rect){ mirror - rect.x1, rect.y0, mirror - rect.x0, rect.y1 } : rect;
}

// R_Shape corners seen in a mirror.
function u32 Prog__MirrorShape(u32 shape, f32 mirror)
{
    if (mirror == 0) return shape;
    u32 result = shape & R_Shape_Inverted;
    if (shape & R_Shape_SquareTopLeft) result |= R_Shape_SquareTopRight;
    if (shape & R_Shape_SquareTopRight) result |= R_Shape_SquareTopLeft;
    if (shape & R_Shape_SquareBottomLeft) result |= R_Shape_SquareBottomRight;
    if (shape & R_Shape_SquareBottomRight) result |= R_Shape_SquareBottomLeft;
    return result;
}

// The radius of the curves into the row's lines, and with the panels' radius, how near the panel's top a submenu snaps level with it.
function f32 Prog__MenuFlare(const Prog_Metrics *m)
{
    return floorf(m->radius * 2 + 0.5f);
}

function Prog_MenuJoin Prog__MenuJoinShape(R_Rect row, R_Rect parent, R_Rect child, b32 left, f32 radius, const Prog_Metrics *m)
{
    Prog_MenuJoin result = { .mirror = left ? child.x0 + child.x1 : 0 };
    f32 b = m->stroke, flare = Prog__MenuFlare(m), R = radius;
    R_Rect P = Prog__Mirror(parent, result.mirror), C = Prog__Mirror(child, result.mirror);
    row = Prog__Mirror(row, result.mirror);
    // On the submenu's whole pixels, so that the row's lines run on into its edges without a step.
    f32 rowHeight = floorf(row.y1 - row.y0 + 0.5f);
    row.y0 = floorf(row.y0);
    row.y1 = row.y0 + rowHeight;
    f32 L = C.x0;
    result.parent = P, result.child = C, result.row = row, result.line = L;
    result.parentSquareTop = C.y0 < P.y0 + R;
    result.parentSquareBottom = C.y1 > P.y1 - R;
    result.childSquareTop = C.y0 > P.y0 - R;
    result.childSquareBottom = C.y1 < P.y1 + R;
    result.rowTop = fabsf(C.y0 - row.y0) < 0.5f;
    // The edge is straight between the panel's corners and clear of where the submenu's top or bottom meets it.
    f32 straightTop = result.parentSquareTop ? P.y0 + b : P.y0 + R, straightBottom = result.parentSquareBottom ? P.y1 - b : P.y1 - R;
    if (C.y0 > P.y0 + 0.5f) straightTop = MaxF32(straightTop, C.y0 + b);
    f32 top = result.rowTop ? 0 : floorf(MinF32(flare, row.y0 - straightTop - 1));
    f32 bottom = floorf(MinF32(flare, straightBottom - row.y1 - 1));
    if (C.y1 < P.y1 - 0.5f) bottom = MinF32(bottom, floorf(C.y1 - row.y1 - 2));
    result.topFlare = top >= 2 ? top : 0;
    result.bottomFlare = bottom >= 2 ? bottom : 0;
    result.childShape = (result.childSquareTop ? (left ? R_Shape_SquareTopRight : R_Shape_SquareTopLeft) : 0) |
                        (result.childSquareBottom ? (left ? R_Shape_SquareBottomRight : R_Shape_SquareBottomLeft) : 0);
    result.parentShape = (result.parentSquareTop ? (left ? R_Shape_SquareTopLeft : R_Shape_SquareTopRight) : 0) |
                         (result.parentSquareBottom ? (left ? R_Shape_SquareBottomLeft : R_Shape_SquareBottomRight) : 0);
    result.parentGap = Prog__Mirror((R_Rect){ L - R, P.y0 - 1, L + 1, P.y1 + 1 }, result.mirror);
    result.childGap = Prog__Mirror((R_Rect){ L - 1, C.y0 - 1, L + R, C.y1 + 1 }, result.mirror);
    return result;
}

// A block at `rect` in root coordinates, mirrored across `mirror` when it is not 0, inside a parent whose contents start at `origin`.
function UI_Block *Prog__JoinPiece(R_Rect rect, f32 mirror, UI_Vec2 origin, R_Color background, UI_Flags flags)
{
    rect = Prog__Mirror(rect, mirror);
    UI_SetNextFloatingOffset((UI_Vec2){ rect.x0 - origin.x, rect.y0 - origin.y });
    UI_SetNextPrefWidth(UI_Pixels(rect.x1 - rect.x0));
    UI_SetNextPrefHeight(UI_Pixels(rect.y1 - rect.y0));
    UI_SetNextColors((UI_Colors){ .background = background, .border = Prog_CurrentTheme()->tabBorder });
    return UI_AddBlock(UI_Flag_Floating | flags);
}

// The part of the box `box` inside `clip`, both mirrored: its outline, or with `fill` what lies outside its rounded shape, filled as the menus are.
// `clip` is on whole pixels, as the clips that draw it are, and is skipped when empty.
function void Prog__JoinBox(R_Rect clip, R_Rect box, f32 radius, u32 shape, f32 mirror, UI_Vec2 origin, b32 fill, f32 opacity, const Prog_Metrics *m)
{
    if (clip.x1 - clip.x0 <= 0 || clip.y1 - clip.y0 <= 0) return;
    UI_Block *holder = Prog__JoinPiece(clip, mirror, origin, (R_Color){0}, UI_Flag_Clip);
    R_Rect shown = Prog__Mirror(clip, mirror);
    UI_Parent(holder) {
        UI_SetNextCornerRadius(radius);
        UI_SetNextOpacity(opacity);
        UI_SetNextShape(Prog__MirrorShape(shape, mirror) | (fill ? R_Shape_Inverted : 0));
        if (fill) {
            UI_SetNextBlur(Prog__FloatingBlur(m));
            Prog__JoinPiece(box, mirror, (UI_Vec2){ shown.x0, shown.y0 }, Prog_CurrentTheme()->menu, UI_Flag_DrawBackground);
        } else {
            UI_SetNextBorderThickness(m->stroke);
            Prog__JoinPiece(box, mirror, (UI_Vec2){ shown.x0, shown.y0 }, (R_Color){0}, UI_Flag_DrawBorder);
        }
    }
}

// The opener's panel's corners squared off where it was drawn rounded, as it is for the first build after a submenu joins it. Drawn in root coordinates
// before the submenu.
function void Prog__MenuJoinFills(const Prog_MenuJoin *join, u32 parentShape, f32 opacity, f32 radius, const Prog_Metrics *m)
{
    R_Rect P = join->parent;
    f32 L = join->line, R = radius;
    UI_Vec2 origin = { UI->root->rect.x0, UI->root->rect.y0 };
    if (join->parentSquareTop && !(parentShape & join->parentShape & (R_Shape_SquareTopLeft | R_Shape_SquareTopRight))) Prog__JoinBox((R_Rect){ L - R, P.y0, L, P.y0 + R }, (R_Rect){ L - 2 * R, P.y0, L, P.y0 + 2 * R }, R, 0, join->mirror, origin, true, opacity, m);
    if (join->parentSquareBottom && !(parentShape & join->parentShape & (R_Shape_SquareBottomLeft | R_Shape_SquareBottomRight))) Prog__JoinBox((R_Rect){ L - R, P.y1 - R, L, P.y1 }, (R_Rect){ L - 2 * R, P.y1 - 2 * R, L, P.y1 }, R, 0, join->mirror, origin, true, opacity, m);
}

// The sides where the panels touch, the line between them and the opener row's outline, as children of the submenu panel whose contents start at
// `origin`. Each piece shows the outline of a box placed so that only the lines wanted there fall inside its clip, and no two clips overlap.
function void Prog__MenuJoinLines(const Prog_MenuJoin *join, UI_Vec2 origin, f32 opacity, f32 radius, const Prog_Metrics *m)
{
    f32 b = m->stroke, band = ceilf(b), rowRadius = m->radius * 2, mirror = join->mirror, L = join->line, R = radius, outside = 4 * R;
    R_Rect P = join->parent, C = join->child, row = join->row;
    UI_SetNextOpacity(opacity);
    UI_Block *holder = Prog__JoinPiece((R_Rect){ origin.x, origin.y, origin.x, origin.y }, 0, origin, (R_Color){0}, 0);
    UI_Parent(holder) {
        b32 topLevel = fabsf(C.y0 - P.y0) < 0.5f, bottomLevel = fabsf(C.y1 - P.y1) < 0.5f;
        f32 cTop = join->topFlare, cBottom = join->bottomFlare;
        // Where the panel's edge opens for the row: at the curves, or at the row's lines, which turn into the edge as a box's corner.
        f32 openTop = cTop >= 1 ? floorf(row.y0 + b - cTop) : row.y0, openBottom = cBottom >= 1 ? ceilf(row.y1 - b + cBottom) : row.y1;
        // The panel's side: its corners, rounded or squared, and its edge, which is the line between the two.
        u32 parentShape = (join->parentSquareTop ? R_Shape_SquareTopRight : 0) | (join->parentSquareBottom ? R_Shape_SquareBottomRight : 0);
        R_Rect panelBox = { L - outside, P.y0, L, P.y1 };
        Prog__JoinBox((R_Rect){ L - R, P.y0, L, openTop }, panelBox, R, parentShape, mirror, origin, false, 1, m);
        Prog__JoinBox((R_Rect){ L - R, openBottom, L, P.y1 }, panelBox, R, parentShape, mirror, origin, false, 1, m);
        // The submenu's side: its top and bottom lines from where they meet the panel's edge, and its own edge where it goes on past the panel.
        R_Rect childBox = { L, C.y0, L + outside, C.y1 };
        u32 childShape = (join->childSquareTop ? R_Shape_SquareTopLeft : 0) | (join->childSquareBottom ? R_Shape_SquareBottomLeft : 0);
        if (!topLevel && C.y0 < P.y0) {
            Prog__JoinBox((R_Rect){ L, C.y0, L + R, P.y0 + band }, childBox, R, childShape, mirror, origin, false, 1, m);
        } else {
            Prog__JoinBox((R_Rect){ L, C.y0, L + R, C.y0 + band }, (R_Rect){ L - outside, C.y0, L + outside, C.y0 + outside }, 0, 0, mirror, origin, false, 1, m);
        }
        if (!bottomLevel && C.y1 > P.y1) {
            Prog__JoinBox((R_Rect){ L, P.y1 - band, L + R, C.y1 }, childBox, R, childShape, mirror, origin, false, 1, m);
        } else {
            Prog__JoinBox((R_Rect){ L, C.y1 - band, L + R, C.y1 }, (R_Rect){ L - outside, C.y1 - outside, L + outside, C.y1 }, 0, 0, mirror, origin, false, 1, m);
        }
        // The edge curves out of its straight part into the row's lines, as the shown tab's sides curve out into the strip's line.
        if (cTop >= 1) {
            Prog__JoinBox((R_Rect){ L - cTop, openTop, L, row.y0 + band }, (R_Rect){ L - 2 * cTop, row.y0 + b - 2 * cTop, L, row.y0 + b }, cTop, 0, mirror, origin, false, 1, m);
        }
        if (cBottom >= 1) {
            Prog__JoinBox((R_Rect){ L - cBottom, row.y1 - band, L, openBottom }, (R_Rect){ L - 2 * cBottom, row.y1 - b, L, row.y1 - b + 2 * cBottom }, cBottom, 0, mirror, origin, false, 1, m);
        }
        // Without a curve, a row's line turns into the edge as a box's corner does; level with the submenu, the top line runs straight on instead.
        b32 topCorner = cTop < 1 && !join->rowTop, bottomCorner = cBottom < 1;
        if (topCorner) Prog__JoinBox((R_Rect){ L - band, row.y0, L, row.y0 + band }, (R_Rect){ L - outside, row.y0 + b - outside, L, row.y0 + b }, 0, 0, mirror, origin, false, 1, m);
        if (bottomCorner) Prog__JoinBox((R_Rect){ L - band, row.y1 - band, L, row.y1 }, (R_Rect){ L - outside, row.y1 - b, L, row.y1 - b + outside }, 0, 0, mirror, origin, false, 1, m);
        // The row's outline, rounded at its outside end, up to the curves or corners.
        f32 topEnd = cTop >= 1 ? L - cTop : topCorner ? L - band : L, bottomEnd = cBottom >= 1 ? L - cBottom : L - band, end = MinF32(topEnd, bottomEnd);
        R_Rect rowBox = { row.x0, row.y0, L + outside, row.y1 };
        Prog__JoinBox((R_Rect){ row.x0, row.y0, end, row.y1 }, rowBox, rowRadius, 0, mirror, origin, false, 1, m);
        Prog__JoinBox((R_Rect){ end, row.y0, topEnd, row.y0 + band }, rowBox, rowRadius, 0, mirror, origin, false, 1, m);
        Prog__JoinBox((R_Rect){ end, row.y1 - band, bottomEnd, row.y1 }, rowBox, rowRadius, 0, mirror, origin, false, 1, m);
    }
}

// A menu panel's border, drawn by itself so that a joined submenu can leave gaps in it.
typedef struct Prog_MenuBorder {
    R_Rect rect;
    f32 radius, opacity;
    u32 shape;
    R_Rect gaps[Prog_MenuGapCount];
    u32 gapCount;
} Prog_MenuBorder;

function void Prog__MenuBorderGap(Prog_MenuBorder *border, R_Rect gap)
{
    if (border->gapCount < Prog_MenuGapCount) border->gaps[border->gapCount++] = gap;
}

function void Prog__MenuBorderDraw(const Prog_MenuBorder *border, const Prog_Metrics *m)
{
    R_Rect view = UI->root->rect, rect = border->rect;
    // Bands across the panel between the gaps' tops and bottoms, each drawn where no gap covers it.
    f32 cuts[2 * Prog_MenuGapCount + 2] = { rect.y0, rect.y1 };
    u32 cutCount = 2;
    for (u32 g = 0; g < border->gapCount; ++g) {
        cuts[cutCount++] = MinF32(MaxF32(border->gaps[g].y0, rect.y0), rect.y1);
        cuts[cutCount++] = MinF32(MaxF32(border->gaps[g].y1, rect.y0), rect.y1);
    }
    for (u32 i = 1; i < cutCount; ++i) for (u32 j = i; j > 0 && cuts[j - 1] > cuts[j]; --j) { f32 t = cuts[j]; cuts[j] = cuts[j - 1]; cuts[j - 1] = t; }
    for (u32 band = 0; band + 1 < cutCount; ++band) {
        f32 y0 = cuts[band], y1 = cuts[band + 1];
        if (y1 - y0 <= 0) continue;
        f32 spans[4 * Prog_MenuGapCount + 2] = { rect.x0, rect.x1 };
        u32 spanCount = 2;
        for (u32 g = 0; g < border->gapCount; ++g) {
            R_Rect gap = border->gaps[g];
            if (gap.y0 > y0 || gap.y1 < y1) continue;
            // Cut the gap's columns out of each span.
            f32 kept[4 * Prog_MenuGapCount + 2];
            u32 keptCount = 0;
            for (u32 s = 0; s < spanCount && keptCount + 4 <= CArrayCount(kept); s += 2) {
                if (gap.x0 > spans[s]) { kept[keptCount++] = spans[s]; kept[keptCount++] = MinF32(gap.x0, spans[s + 1]); }
                if (gap.x1 < spans[s + 1]) { kept[keptCount++] = MaxF32(gap.x1, spans[s]); kept[keptCount++] = spans[s + 1]; }
            }
            spanCount = keptCount;
            for (u32 k = 0; k < keptCount; ++k) spans[k] = kept[k];
        }
        for (u32 s = 0; s < spanCount; s += 2) {
            if (spans[s + 1] - spans[s] <= 0) continue;
            UI_SetNextFloatingOffset((UI_Vec2){ spans[s] - view.x0, y0 - view.y0 });
            UI_SetNextPrefWidth(UI_Pixels(spans[s + 1] - spans[s]));
            UI_SetNextPrefHeight(UI_Pixels(y1 - y0));
            UI_Block *clip = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
            UI_Parent(clip) {
                UI_SetNextFloatingOffset((UI_Vec2){ rect.x0 - spans[s], rect.y0 - y0 });
                UI_SetNextPrefWidth(UI_Pixels(rect.x1 - rect.x0));
                UI_SetNextPrefHeight(UI_Pixels(rect.y1 - rect.y0));
                UI_SetNextCornerRadius(border->radius);
                UI_SetNextShape(border->shape);
                UI_SetNextBorderThickness(m->stroke);
                UI_SetNextOpacity(border->opacity);
                UI_SetNextColors((UI_Colors){ .border = Prog_CurrentTheme()->tabBorder });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
            }
        }
    }
}

// The shadow of menus, the palette and Go To: only to the right and below, a fifth dark at the box's edge and fading out over 10.4 logical pixels.
// Offsets are from the root.
function void Prog__DropShadow(f32 x, f32 y, f32 width, f32 height, f32 radius, f32 opacity, const Prog_Metrics *m)
{
    f32 reach = floorf(10.4f * m->scale + 0.5f), shift = floorf(0.8f * m->scale + 0.5f);
    UI_SetNextPrefWidth(UI_Pixels(width + reach));
    UI_SetNextPrefHeight(UI_Pixels(height + reach));
    UI_SetNextFloatingOffset((UI_Vec2){ x + shift, y + shift });
    UI_SetNextCornerRadius(radius + reach);
    UI_SetNextSoftness(reach);
    UI_SetNextColors((UI_Colors){ .background = { 0, 0, 0, 0.2f } });
    UI_SetNextOpacity(opacity);
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
}

// One menu level as a floating panel, kept inside the window.
// Sets `child` when one of its items has its submenu open.
// The Filey command that does what a system menu's verb does, whose hotkeys its row shows; None for the rest.
function Prog_CommandKind Prog__VerbCommand(String verb)
{
    if (StrIsEqual(verb, S("cut"))) return Prog_CommandKind_CutSelection;
    if (StrIsEqual(verb, S("copy"))) return Prog_CommandKind_CopySelection;
    if (StrIsEqual(verb, S("paste"))) return Prog_CommandKind_Paste;
    if (StrIsEqual(verb, S("delete"))) return Prog_CommandKind_Trash;
    if (StrIsEqual(verb, S("rename"))) return Prog_CommandKind_Rename;
    return Prog_CommandKind_None;
}

function b32 Prog__MenuPanel(Prog_State *state, u32 level, Prog_MenuPlace place, const Prog_Metrics *m, Prog_MenuPlace *child, Prog_MenuBorder *border,
                             Prog_MenuBorder *parentBorder)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    // With inline submenus, the top level holds the open submenus' items too, each as deep as `depths` says.
    b32 flat = level == 0 && Prog_MenuInline(state);
    u32 room = flat ? Prog_MenuInlineRoom(state) : Max(place.count, 1u);
    u32 *visible = ArenaPushArray(&state->frameArena, room, u32);
    u8 *depths = ArenaPushArray(&state->frameArena, room, u8);
    String filter = Prog_MenuFilter(state, level);
    u32 shown = flat ? Prog_MenuInlineVisible(state, visible, depths) : Prog_MenuVisible(&state->menu, place.first, place.count, filter, visible);
    f32 indent = floorf(24.0f * m->scale + 0.5f);
    // The system menu's top level has its search field over the items, and stays open with nothing found.
    b32 search = level == 0 && !state->menuCommands;
    if (!shown && !search) {
        if (parentBorder) Prog__MenuBorderDraw(parentBorder, m);
        if (level > 0 && level - 1 < CArrayCount(state->menuSquare)) state->menuSquare[level - 1] = 0;
        return false;
    }
    // Rows inset from the panel edge, an icon column that every label lines up after, and hairline separators that lie on the boundary
    // between two rows rather than taking room of their own.
    f32 itemHeight = m->row, line = m->separator, separatorHeight = 0, pad = floorf(6.0f * m->scale + 0.5f);
    // Filey's own menus show each hotkey in a chip and toggles as a box at the end.
    f32 chipGap = floorf(3.2f * m->scale + 0.5f);
    f32 box = floorf(16.0f * m->scale + 0.5f);
    Prog_HotkeyChip *chips = ArenaPushArray(&state->frameArena, Max(shown, 1u) * 3, Prog_HotkeyChip);
    u32 *chipCounts = ArenaPushArray(&state->frameArena, Max(shown, 1u), u32);
    u16 *chipKinds = ArenaPushArray(&state->frameArena, Max(shown, 1u), u16);
    f32 iconLeft = floorf(7.0f * m->scale + 0.5f), iconColumn = (f32)state->iconSize + floorf(8.0f * m->scale + 0.5f);
    // The search field is a band across the top in the inner color, over a line and a little room before the items.
    f32 fieldInset = floorf(2.4f * m->scale + 0.5f), fieldHeight = floorf(30.4f * m->scale + 0.5f);
    f32 searchHeight = search ? fieldInset + fieldHeight + line + floorf(4.0f * m->scale + 0.5f) - pad : 0;
    // The widest row's label and its chips together, rather than the longest label and the most chips of any rows.
    f32 rowWidth = 0, height = 2 * pad + searchHeight;
    b32 anySubmenu = false;
    for (u32 i = 0; i < shown; ++i) {
        const P_MenuItem *item = Prog_MenuItemAt(state, visible[i]);
        height += item->separator ? separatorHeight : itemHeight;
        f32 labelWidth = R_TextWidth(m->font, item->label) + depths[i] * indent, hotkeyWidth = 0;
        anySubmenu |= item->childCount != 0;
        if (!item->separator) {
            // A system item shows the hotkeys of the Filey command that does the same.
            const Prog_MenuCommand *command = state->menuCommands ? &state->menuCommands[visible[i]] : NULL;
            u32 mouse = command && command->mouse.length ? 1 : 0;
            if (mouse) chips[i * 3] = (Prog_HotkeyChip){ .text = command->mouse, .slot = UINT8_MAX };
            Prog_CommandKind kind = command ? (Prog_CommandKind)(command->hotkeysOf ? command->hotkeysOf : command->kind) : Prog__VerbCommand(item->verb);
            chipKinds[i] = (u16)kind;
            chipCounts[i] = mouse + (kind ? Prog_HotkeyChips(&state->frameArena, state, kind, chips + i * 3 + mouse, 3 - mouse) : 0);
            f32 end = command && command->toggle ? box + m->gap : 0;
            for (u32 c = 0; c < chipCounts[i]; ++c) end += Prog__HotkeyChipWidth(&chips[i * 3 + c], m) + chipGap;
            if (end > 0) hotkeyWidth = end + m->inset - floorf(5.6f * m->scale + 0.5f);
        }
        rowWidth = MaxF32(rowWidth, labelWidth + hotkeyWidth);
    }
    f32 arrow = anySubmenu ? m->line * 1.5f : 0;
    // A system menu's top level has a star on its highlighted row, for pinning the item to the top.
    b32 pins = level == 0 && !state->menuCommands;
    f32 star = pins ? floorf(24.0f * m->scale + 0.5f) : 0;
    R_Rect view = UI->root->rect;
    // A menu taller than the window fits it and scrolls, with a bar in a gutter on the right; one with inline submenus keeps where it opened, below which
    // it has the room.
    b32 placedBefore = flat && state->menuTopSerial == state->menuSerial + 1;
    b32 scrolls = height > (placedBefore ? view.y1 - state->menuTop : view.y1 - view.y0);
    f32 gutter = scrolls ? Prog__ScrollGutter(m) : 0;
    // Wide enough for the search field's hint beside its two buttons, a little apart from them.
    f32 fieldWidth = search ? iconLeft + iconColumn + R_TextWidth(m->font, Tr("Select a system command...")) + floorf(8.0f * m->scale + 0.5f) + 2 * Prog__ToolbarButtonWidth(m) + 2 * pad : 0;
    f32 width = floorf(MinF32(MaxF32(MaxF32(rowWidth + iconLeft + iconColumn + m->inset + arrow + star + 2 * pad, fieldWidth), m->line * 12), m->line * 36)) + gutter;
    if (flat) width = floorf(UI_Animate(UI_KeyFromStr(0, StrFormat(&state->frameArena, "MenuWidth/%u", state->menuSerial)), width, width) + 0.5f);
    f32 x = level == 0 && state->menuRight > 0 ? state->menuRight - width : place.x, y = place.y;
    // Submenus open to the right of their item, or to the left when there is no room; a menu pushed in from the right edge keeps a margin.
    // A submenu sits against its opener's panel, on the side the menus have been opening to while there is room, so that it does not cover the
    // panel before.
    if (place.leftward && x - place.openerWidth - width >= view.x0) x -= place.openerWidth + width;
    else if (x + width > view.x1) x = place.openerWidth > 0 ? x - place.openerWidth - width : view.x1 - width - floorf(4.0f * m->scale + 0.5f);
    // An end of a submenu near the same end of its opener's panel goes level with it, rather than leave a step too short to curve into.
    R_Rect parent = place.openerPanel;
    b32 opened = level > 0 && place.opener.y1 > place.opener.y0;
    f32 snap = floorf(10.0f * m->scale + 0.5f) + Prog__MenuFlare(m);
    if (opened && y > parent.y0 && y - parent.y0 < snap) y = parent.y0;
    f32 bottomStep = parent.y1 - (y + height);
    if (opened && fabsf(bottomStep) > 0 && fabsf(bottomStep) < snap && y + bottomStep <= place.opener.y0 && y + bottomStep >= view.y0) y += bottomStep;
    if (y + height > view.y1) y = view.y1 - height;
    x = floorf(MaxF32(view.x0, x));
    y = floorf(MaxF32(view.y0, y));
    // A new panel of Filey's own fades in while its rows spread from a little closer together to their place; a system menu shows at once.
    UI_Key revealKey = UI_KeyFromStr(0, StrFormat(&state->frameArena, "MenuReveal/%u/%u", level, place.first));
    b32 own = state->menuCommands != NULL;
    f32 reveal = own ? UI_Animate(revealKey, 1, 0) : 1;
    itemHeight *= 0.9f + 0.1f * reveal;
    height = 2 * pad + searchHeight;
    for (u32 i = 0; i < shown; ++i) height += Prog_MenuItemAt(state, visible[i])->separator ? separatorHeight : itemHeight;
    // On whole pixels, for a joined submenu's lines to meet its edges exactly.
    height = floorf(MinF32(height, view.y1 - view.y0) + 0.5f);
    // Inline, the menu grows and shrinks with the submenus opening in it, as its rows slide.
    if (flat) {
        // It stays where it opened, and once it reaches the window's bottom its rows scroll.
        if (state->menuTopSerial != state->menuSerial + 1) {
            state->menuTop = y;
            state->menuTopSerial = state->menuSerial + 1;
        }
        y = state->menuTop;
        height = floorf(MinF32(UI_Animate(UI_KeyFromStr(0, StrFormat(&state->frameArena, "MenuHeight/%u", state->menuSerial)), height, height), view.y1 - y) + 0.5f);
    }
    f32 opacity = own ? Prog__FadeIn(UI_KeyFromStr(revealKey, S("Fade"))) : 1;

    f32 radius = floorf(10.0f * m->scale + 0.5f);
    b32 joinRight = opened && fabsf(x - parent.x1) < 0.5f;
    b32 joinLeft = opened && fabsf(x + width - parent.x0) < 0.5f;
    if (level < CArrayCount(state->menuApart)) state->menuApart[level] = !joinRight && !joinLeft;
    Prog_MenuJoin join = {0};
    R_Rect rect = { x, y, x + width, y + height };
    *border = (Prog_MenuBorder){ .rect = rect, .radius = radius, .opacity = opacity };
    // The corners a joined submenu squares off on this panel, as it found them in the last build: the panel is drawn before its submenu is placed.
    u32 squared = level < CArrayCount(state->menuSquare) ? state->menuSquare[level] : 0;
    if (joinRight || joinLeft) {
        join = Prog__MenuJoinShape(place.opener, parent, rect, joinLeft, radius, m);
        Prog__MenuBorderGap(border, join.childGap);
        Prog__MenuBorderGap(parentBorder, join.parentGap);
    }
    border->shape = join.childShape | squared;
    if (level > 0 && level - 1 < CArrayCount(state->menuSquare)) state->menuSquare[level - 1] = join.parentShape;
    if (parentBorder && !joinRight && !joinLeft) {
        // Apart from its opener's panel, a submenu covers that panel's border.
        Prog__MenuBorderDraw(parentBorder, m);
    }
    // A submenu joined on its left casts no shadow over its opener's panel, which it is one with, but does around it: the shadow is drawn in the
    // parts of the view beside, above and below that panel.
    if (joinLeft) {
        R_Rect parts[3] = { { view.x0, view.y0, parent.x0, view.y1 }, { parent.x0, view.y0, view.x1, parent.y0 }, { parent.x0, parent.y1, view.x1, view.y1 } };
        for (u32 p = 0; p < CArrayCount(parts); ++p) {
            if (parts[p].x1 <= parts[p].x0 || parts[p].y1 <= parts[p].y0) continue;
            UI_SetNextFloatingOffset((UI_Vec2){ parts[p].x0 - view.x0, parts[p].y0 - view.y0 });
            UI_SetNextPrefWidth(UI_Pixels(parts[p].x1 - parts[p].x0));
            UI_SetNextPrefHeight(UI_Pixels(parts[p].y1 - parts[p].y0));
            UI_Block *shade = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
            UI_Parent(shade) Prog__DropShadow(x - parts[p].x0, y - parts[p].y0, width, height, radius, opacity, m);
        }
    } else {
        Prog__DropShadow(x - view.x0, y - view.y0, width, height, radius, opacity, m);
    }
    if (joinRight || joinLeft) Prog__MenuJoinFills(&join, parentBorder->shape, opacity, radius, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextOpacity(opacity);
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, y - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
    UI_SetNextCornerRadius(radius);
    UI_SetNextShape(border->shape);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu });
    // Clickable so that clicks on the padding do not reach the catcher behind it, nor right clicks what is under it.
    // A submenu is not clipped, for its join to reach into its opener's panel. The border is drawn after whatever joins the panel.
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "MenuPanel/%u", level)),
                                         UI_Flag_Floating | UI_Flag_DrawBackground | (level ? 0 : UI_Flag_Clip) | UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable);
    if (level < CArrayCount(state->menuRects)) {
        state->menuRects[level] = panel->rect;
        if (level + 1 < CArrayCount(state->menuRects)) state->menuRects[level + 1] = (R_Rect){0};
    }
    b32 result = false;
    f32 itemY = y + pad;
    UI_Parent(panel) {
        if (joinRight || joinLeft) Prog__MenuJoinLines(&join, (UI_Vec2){ x + pad, y + pad }, 1, radius, m);
        if (search) {
            // Inside the border, round at the top along the box's corners and square where it meets the line.
            // One box, so that a translucent tint is not laid twice; its top corners follow the panel's, squared where a submenu squares those.
            f32 bandX = fieldInset - pad, bandY = fieldInset - pad, bandWidth = width - 2 * fieldInset;
            UI_SetNextFloatingOffset((UI_Vec2){ bandX, bandY });
            UI_SetNextPrefWidth(UI_Pixels(bandWidth));
            UI_SetNextPrefHeight(UI_Pixels(fieldHeight));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ pad - fieldInset, 0, pad - fieldInset, 0 });
            UI_SetNextCornerRadius(MaxF32(0, radius - fieldInset));
            UI_SetNextShape(R_Shape_SquareBottomLeft | R_Shape_SquareBottomRight | (border->shape & (R_Shape_SquareTopLeft | R_Shape_SquareTopRight)));
            UI_SetNextColors((UI_Colors){ .background = theme->inner });
            UI_Block *field = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            UI_Parent(field) {
                Prog__Spacer(UI_Pixels(iconLeft), UI_Pixels(fieldHeight));
                Prog__SearchIcon(iconColumn, fieldHeight, theme->textDim, 0, 0, m);
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(fieldHeight));
                UI_Block *text = UI_AddBlock(UI_Flag_Clip);
                UI_Parent(text) {
                    if (!state->menuSearch.length) {
                        UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((fieldHeight - m->line) * 0.5f) });
                        UI_SetNextPrefHeight(UI_Pixels(m->line));
                        UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                        UI_SetNextText(Tr("Select a system command..."));
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                    }
                    Prog__EditContents(&state->menuSearch, 0, floorf((fieldHeight - m->line) * 0.5f), true, m);
                }
                // A pencil opens the menu editor in its place, a little apart from the text; a little smaller than the icons, as it fills more of
                // its square than the cross beside it.
                Prog__Spacer(UI_Pixels(floorf(8.0f * m->scale + 0.5f)), UI_Pixels(fieldHeight));
                UI_Interaction pencil = Prog__TextButton(S("MenuEditOpen"), Icon_Pencil, m->iconsMedium, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0, theme->icon, theme->text,
                                                         theme->iconDisabled, true, m);
                if (pencil.clicked) Prog_QueueCommand(state, Prog_CommandKind_MenuEditOpen, 0, 0, pencil.clickOrder);
                Prog__PopupTooltip(state, Tr("Edit menu: hide items and change their order"));
                // A button at its end closes the menu, in the middle of the field's height and as far from its end as from its top.
                UI_Interaction close = Prog__CloseButton(S("MenuSearchClose"), m);
                if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_ContextMenuClose, 0, 0, close.clickOrder);
                Prog__Spacer(UI_Pixels(MaxF32(0, floorf((fieldHeight - Prog__ToolbarButtonHeight(m)) * 0.5f) - (pad - fieldInset))), UI_Pixels(fieldHeight));
            }
            UI_SetNextFloatingOffset((UI_Vec2){ bandX, bandY + fieldHeight });
            UI_SetNextPrefWidth(UI_Pixels(bandWidth));
            UI_SetNextPrefHeight(UI_Pixels(line));
            UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            Prog__Spacer(UI_Flex(), UI_Pixels(searchHeight));
            itemY += searchHeight;
        }
        // The rows, in a list of their own under the search field, which scrolls when they do not fit.
        String listName = StrFormat(&state->frameArena, "MenuList/%u/%u/%u", state->menuSerial, level, place.first);
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(MaxF32(0, height - 2 * pad - searchHeight)));
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, gutter, 0 });
        UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, listName), UI_Flag_Clip | (scrolls ? UI_Flag_Scrollable : 0));
        // Rows are keyed by their item rather than their place and each slides to where it now is, so that one pinned to the top travels up over
        // the others while they move down to make room for it.
        UI_Key *keys = ArenaPushArray(&state->frameArena, Max(shown, 1u), UI_Key);
        f32 *placed = ArenaPushArray(&state->frameArena, Max(shown, 1u), f32), *slid = ArenaPushArray(&state->frameArena, Max(shown, 1u), f32);
        // Inline, each row of an open submenu goes by the item it opened under, the row before it one less deep.
        u32 *owners = ArenaPushArray(&state->frameArena, Max(shown, 1u), u32);
        f32 rowsHeight = 0, rowWidth = width - 2 * pad - gutter;
        for (u32 i = 0; i < shown; ++i) {
            const P_MenuItem *item = Prog_MenuItemAt(state, visible[i]);
            owners[i] = UINT32_MAX;
            for (u32 j = i; depths[i] && j-- > 0;) if (depths[j] < depths[i]) { owners[i] = j; break; }
            // A line goes by the row above it; a menu may hold one command twice, so each copy has a number of its own.
            u32 copy = 0;
            for (u32 j = 0; j < i; ++j) {
                const P_MenuItem *other = Prog_MenuItemAt(state, visible[j]);
                copy += other->id == item->id && StrIsEqual(other->label, item->label);
            }
            String identity = StrFormat(&state->frameArena, "Menu/%u/%u/%S/%u", level, item->id, item->label, copy);
            keys[i] = item->separator && i ? UI_KeyFromStr(keys[i - 1], S("Line")) : UI_KeyFromStr(0, identity);
            placed[i] = rowsHeight;
            rowsHeight += item->separator ? separatorHeight : itemHeight;
        }
        for (u32 i = 0; i < shown; ++i) {
            // The rows of a submenu that opens start drawn up under its item by all their height, and slide down with the rows after them.
            f32 start = placed[i];
            if (owners[i] != UINT32_MAX) {
                u32 owner = owners[i], end = owner + 1;
                while (end < shown && depths[end] > depths[owner]) ++end;
                start -= (end < shown ? placed[end] : rowsHeight) - placed[owner] - itemHeight;
            }
            slid[i] = UI_Animate(UI_KeyFromStr(keys[i], S("Y")), placed[i], start);
        }
        UI_Parent(list) for (u32 pass = 0; pass < 2; ++pass) for (u32 i = 0; i < shown; ++i) {
            // The row just pinned or unpinned is drawn last, over those it passes on its way.
            // Each row goes in the list again, out of the clip the one before may have gone in.
            UI_PopParent(list);
            u32 index = visible[i];
            const P_MenuItem *item = Prog_MenuItemAt(state, index);
            b32 moving = level == 0 && state->menuMoved.length && StrIsEqual(item->label, state->menuMoved) && fabsf(slid[i] - placed[i]) > 0.5f;
            if (moving != (pass == 1)) continue;
            // Rounded rather than floored, or a row easing down sits a pixel short through the tail of the ease and then jumps.
            f32 rowY = floorf(slid[i] - list->scrollShown.y + 0.5f);
            // A submenu's rows show only below the item they opened under, out of which they slide; each is drawn in a clip from there down.
            if (owners[i] != UINT32_MAX) {
                // A line on the item's bottom edge shows whole, half of it above the edge.
                f32 from = floorf(slid[owners[i]] - list->scrollShown.y + 0.5f) + itemHeight - (item->separator ? ceilf(line * 0.5f) : 0);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, from });
                UI_SetNextPrefWidth(UI_Pixels(rowWidth));
                UI_SetNextPrefHeight(UI_Pixels(MaxF32(0, height - from)));
                UI_Block *below = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
                UI_PushParent(below);
                rowY -= from;
            }
            if (item->separator) {
                // A line on the boundary of the rows around it, left out next to a row its submenu joins, whose outline takes its place.
                b32 joinedNext = false;
                for (u32 side = 0; side < 2; ++side) {
                    if (side ? i + 1 >= shown : i == 0) continue;
                    u32 next = visible[side ? i + 1 : i - 1];
                    joinedNext |= level < state->menuDepth && state->menuPath[level] == next && level + 1 < CArrayCount(state->menuApart) && !state->menuApart[level + 1];
                }
                if (joinedNext) continue;
                UI_SetNextFloatingOffset((UI_Vec2){ 0, rowY - floorf(line * 0.5f) });
                UI_SetNextPrefWidth(UI_Pixels(rowWidth));
                UI_SetNextPrefHeight(UI_Pixels(line));
                UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                continue;
            }
            b32 enabled = !item->disabled || item->childCount;
            UI_Key key = keys[i];
            // Moving the mouse onto an item decides the highlight and which submenu is open, as in native menus; the keyboard takes over until the mouse moves to another item.
            // Rows sliding under a still pointer do not take the highlight from the keyboard's or the pinned row's.
            // Inline, pointing only lights an item; a click opens its submenu.
            if (enabled && UI->hotKey == key && state->menuPointerMoved) {
                state->menuHighlight = index;
                state->menuDepth = level;
                if (item->childCount && level < CArrayCount(state->menuPath) && !flat) {
                    state->menuPath[level] = index;
                    state->menuDepth = level + 1;
                }
            }
            b32 open = !flat && level < state->menuDepth && state->menuPath[level] == index;
            b32 expanded = flat && item->childCount && Prog_MenuIsExpanded(state, index);
            // The row on its way covers those it passes in the menu's own color, unlit until it is in place.
            b32 highlighted = enabled && !moving && (open || state->menuHighlight == index);
            // The row whose submenu joins it is outlined by the submenu rather than lit.
            b32 joined = open && level + 1 < CArrayCount(state->menuApart) && !state->menuApart[level + 1];
            UI_SetNextFloatingOffset((UI_Vec2){ 0, rowY });
            UI_SetNextPrefWidth(UI_Pixels(rowWidth));
            UI_SetNextPrefHeight(UI_Pixels(itemHeight));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ iconLeft, 0, 0, 0 });
            UI_SetNextCornerRadius(m->radius * 2);
            UI_SetNextColors((UI_Colors){ .background = moving ? theme->menu : theme->selection });
            UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_Floating | ((highlighted && !joined) || moving ? UI_Flag_DrawBackground : 0) | (enabled ? UI_Flag_LeftMouseClickable : 0));
            // A highlight the keyboard moved scrolls into sight; the rows' places are the last layout's, in the list's content.
            // A first frame has no layout yet to go by, so it waits for the next.
            if (scrolls && state->menuReveal && state->menuHighlight == index && state->menuDepth == level && row->rect.y1 > row->rect.y0 && list->rect.y1 > list->rect.y0) {
                f32 top = row->rect.y0 - list->rect.y0 + list->scrollShown.y, room = list->rect.y1 - list->rect.y0;
                if (top < list->scrollOffset.y) list->scrollOffset.y = top;
                if (top + itemHeight > list->scrollOffset.y + room) list->scrollOffset.y = top + itemHeight - room;
                state->menuReveal = false;
            }
            UI_Interaction interaction = UI_InteractionFromBlock(row);
            if (interaction.clicked && !item->childCount) {
                Prog_QueueCommand(state, Prog_CommandKind_ContextMenuInvoke, index, 0, interaction.clickOrder);
            } else if (interaction.clicked && flat) {
                Prog_QueueCommand(state, Prog_CommandKind_ContextMenuExpand, index, 0, interaction.clickOrder);
            }
            UI_Parent(row) {
                R_Color color = enabled ? theme->text : theme->textDim;
                // Looked up as it is drawn, as a full icon atlas can give its cell to another.
                const Prog_Icon *icon = Prog_ImageIcon(state, item->icon);
                const Prog_MenuCommand *command = state->menuCommands ? &state->menuCommands[index] : NULL;
                // An open submenu's items sit in from its item's.
                if (depths[i]) Prog__Spacer(UI_Pixels(depths[i] * indent), UI_Pixels(itemHeight));
                UI_SetNextPrefWidth(UI_Pixels(iconColumn));
                UI_SetNextPrefHeight(UI_Pixels(itemHeight));
                UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
                if (icon) UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, enabled ? (R_Color){ 1, 1, 1, 1 } : (R_Color){ 1, 1, 1, 0.4f });
                // A glyph of its own, or a check for the chosen one of a set.
                String glyph = command && command->icon.length ? command->icon : command && item->isDefault && !command->toggle ? Icon_Check : (String){0};
                if (!icon && glyph.length) {
                    UI_SetNextFont(m->icons);
                    UI_SetNextColors((UI_Colors){ .text = !enabled ? theme->iconDisabled : highlighted || !command->icon.length ? theme->text : theme->icon });
                    UI_SetNextText(glyph);
                }
                UI_AddBlock(icon ? UI_Flag_DrawImage : glyph.length ? UI_Flag_DrawText : 0);
                Prog__Label(item->label, UI_Flex(), itemHeight, 0, color, 0);
                // The star pins the item to the top of the system's menus, or unpins it; a pinned one shows it lit.
                b32 pinned = pins && Prog_MenuPinned(state, item->label);
                if (pins && !item->childCount && (highlighted || pinned)) {
                    UI_Interaction pin = Prog__TextButton(StrFormat(&state->frameArena, "MenuPin/%u/%u", item->id ? item->id : index, i), Icon_Star, m->icons, UI_Pixels(star), itemHeight, 0,
                                                          pinned && !highlighted ? theme->textMatch : theme->text, pinned && !highlighted ? theme->textMatch : theme->text, theme->iconDisabled, true, m);
                    if (pin.clicked) Prog_QueueCommand(state, Prog_CommandKind_MenuPinToggle, index, 0, pin.clickOrder);
                } else if (pins) {
                    Prog__Spacer(UI_Pixels(star), UI_Pixels(itemHeight));
                }
                for (u32 c = 0; c < chipCounts[i]; ++c) {
                    Prog__HotkeyChipButton(state, StrFormat(&state->frameArena, "Menu/%u", index), (Prog_CommandKind)chipKinds[i], &chips[i * 3 + c], itemHeight, highlighted, m);
                    Prog__Spacer(UI_Pixels(chipGap), UI_Pixels(itemHeight));
                }
                if (command && command->toggle) {
                    Prog__Spacer(UI_Pixels(m->gap - chipGap), UI_Pixels(itemHeight));
                    UI_SetNextPrefWidth(UI_Pixels(box));
                    UI_SetNextPrefHeight(UI_Pixels(itemHeight));
                    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                    UI_Block *slot = UI_AddBlock(0);
                    UI_Parent(slot) {
                        UI_SetNextPrefWidth(UI_Pixels(box));
                        UI_SetNextPrefHeight(UI_Pixels(box));
                        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                        UI_SetNextCornerRadius(floorf(3.2f * m->scale + 0.5f));
                        UI_SetNextBorderThickness(m->stroke);
                        UI_SetNextFont(m->iconsSmall);
                        // A box that cannot be changed shows dimmed.
                        R_Color fill = theme->selection, mark = theme->text;
                        if (!enabled) { fill.a *= 0.4f; mark.a *= 0.5f; }
                        UI_SetNextColors((UI_Colors){ .background = fill, .border = highlighted ? theme->text : theme->tabBorder, .text = mark });
                        UI_SetNextText(Icon_Check);
                        UI_AddBlock(item->isDefault ? UI_Flag_DrawBackground | UI_Flag_DrawText | (highlighted ? UI_Flag_DrawBorder : 0) : UI_Flag_DrawBorder);
                    }
                }
                if (command || chipCounts[i]) Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(itemHeight));
                if (item->childCount) Prog__Icon(expanded ? Icon_ChevronDown : Icon_ChevronRight, arrow, itemHeight, highlighted ? theme->text : theme->icon, 0.5f, m);
            }
            if (open) {
                // The submenu starts level with its row, the row's top line running on as its own.
                b32 laidOut = row->rect.y1 > row->rect.y0, inSight = laidOut && row->rect.y0 >= list->rect.y0 - 0.5f && row->rect.y1 <= list->rect.y1 + 0.5f;
                *child = (Prog_MenuPlace){ item->firstChild, item->childCount, x + width, floorf(laidOut ? row->rect.y0 : itemY + placed[i]), width,
                                           inSight ? row->rect : (R_Rect){0}, (R_Rect){ x, y, x + width, y + height }, opened && x + width <= parent.x0 + 0.5f };
                result = true;
            }
        }
        // The rows float, so the list scrolls by a stand-in as tall as they are.
        UI_Parent(list) Prog__Spacer(UI_Pixels(rowWidth), UI_Pixels(rowsHeight));
        // Faded out while a submenu is open, which may join the panel across it.
        f32 bar = scrolls ? UI_Animate(UI_KeyFromStr(0, StrFormat(&state->frameArena, "%S/Bar", listName)), result ? 0.0f : 1.0f, 1) : 0;
        if (bar > 0.01f) Prog__ScrollBar(list, itemHeight, listName, bar, m);
    }
    // Over this panel, where the two borders lie on one another.
    if (joinRight || joinLeft) Prog__MenuBorderDraw(parentBorder, m);
    return result;
}

// After the pointer rests on a button for a moment, a tip under it with the command the button runs and its hotkeys, fading in.
// "1.5K" for 1599, cut off rather than rounded as sizes are.
function String Prog__FormatCount(Arena *arena, u64 count)
{
    if (count < 1000) return StrFormat(arena, "%" PRIu64, count);
    u64 unit = count < 1000000 ? 1000 : 1000000, tenths = count * 10 / unit;
    String suffix = Prog_UnitText(unit == 1000 ? Prog_Unit_Thousands : Prog_Unit_Millions);
    // A whole number goes without its ".0", as "1M".
    return tenths < 1000 && tenths % 10 ? StrFormat(arena, "%" PRIu64 "%c%" PRIu64 "%S", tenths / 10, Prog_DecimalMark, tenths % 10, suffix) : StrFormat(arena, "%" PRIu64 "%S", tenths / 10, suffix);
}

// The tip of a sidebar folder, beside its row: its name over its path, when it was made, what it holds once that is counted, and the click that opens it in a tab.
function void Prog__FolderTip(Prog_State *state, f32 opacity, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String path = state->tooltipPath;
    Prog_InspectFolder(state, path);
    f32 s = m->scale, pad = floorf(12.0f * s + 0.5f), side = floorf(8.8f * s + 0.5f), row = floorf(21.6f * s + 0.5f), margin = floorf(6.4f * s + 0.5f);
    Arena *frame = &state->frameArena;
    b32 known = state->tipKnown;
    String waiting = S("...");
    String labels[] = { Prog_PathName(state, path), Tr("Created:"), Tr("Subfolders:"), Tr("Subfiles:"), Tr("Size:"), Tr("Cmd:") };
    String values[] = {
        Prog__CrumbPath(state, path),
        known && state->tip.created ? P_FormatFileTime(frame, state->tip.created) : known ? S("--") : waiting,
        known ? Prog__FormatCount(frame, state->tip.totals.folders) : waiting,
        known ? Prog__FormatCount(frame, state->tip.totals.files) : waiting,
        known ? Prog_FormatSize(frame, state->tip.totals.bytes) : waiting,
        Tr("Open In New Tab"),
    };
    String chips[] = { S("Ctrl+MouseLeft"), S("MouseMiddle") };
    f32 column = 0, valueWidth = 0, chipGap = floorf(4.8f * s + 0.5f), chipPad = floorf(4.8f * s + 0.5f);
    for (u32 i = 0; i < CArrayCount(labels); ++i) column = MaxF32(column, R_TextWidth(m->font, labels[i]));
    column += floorf(13.6f * s + 0.5f);
    for (u32 i = 0; i < CArrayCount(values); ++i) valueWidth = MaxF32(valueWidth, R_TextWidth(m->detail, values[i]));
    f32 commandWidth = R_TextWidth(m->detail, values[CArrayCount(values) - 1]);
    for (u32 i = 0; i < CArrayCount(chips); ++i) commandWidth += chipGap + R_TextWidth(m->detail, chips[i]) + 2 * chipPad;
    f32 width = 2 * side + column + MaxF32(valueWidth, commandWidth);
    f32 height = 2 * pad + (f32)CArrayCount(labels) * row;
    R_Rect view = UI->root->rect, item = state->tooltipRect;
    // Right of the row and level with it.
    f32 x = item.x1, y = floorf((item.y0 + item.y1 - height) * 0.5f);
    x = floorf(MaxF32(view.x0 + margin, MinF32(x, view.x1 - margin - width)));
    y = floorf(MaxF32(view.y0 + margin, MinF32(y, view.y1 - margin - height)));
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, y - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ side, pad, side, pad });
    UI_SetNextCornerRadius(floorf(8.0f * s + 0.5f));
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextOpacity(opacity);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *tip = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
    UI_Parent(tip) for (u32 i = 0; i < CArrayCount(labels); ++i) {
        UI_Block *line = Prog__Box(UI_Fill(), UI_Pixels(row), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(line) {
            Prog__Label(labels[i], UI_Pixels(column), row, 0, theme->text, 0);
            UI_SetNextFont(m->detail);
            Prog__Label(values[i], UI_TextContent(), row, 0, theme->textDim, 0);
            for (u32 c = 0; i + 1 == CArrayCount(labels) && c < CArrayCount(chips); ++c) {
                Prog__Spacer(UI_Pixels(chipGap), UI_Pixels(row));
                Prog__Chip(chips[c], row, false, false, m);
            }
        }
    }
}

function void Prog__Tooltip(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->tooltipKey || ((state->menuOpen || state->paletteOpen) && !state->tooltipAbove)) return;
    // None while something is dragged.
    if (state->tabDrag || state->dragging || P_DropPending()) return;
    if (UI_TimeSince(UI_KeyFromStr(state->tooltipKey, S("TooltipWait"))) < 700000) {
        UI_KeepAnimating();
        return;
    }
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale, opacity = UI_Animate(UI_KeyFromStr(state->tooltipKey, S("TooltipShown")), 1, 0);
    if (state->tooltipPath.length) {
        Prog__FolderTip(state, opacity, m);
        return;
    }
    String label = state->tooltipLabel.length ? state->tooltipLabel : Prog_CommandLabel((Prog_CommandKind)state->tooltipKind);
    // A tip with a place to go shows it over the command and leaves the hotkeys out.
    // A described button's words go over its command a line each, with its hotkeys or "Blank"; a tip of no command is only its words.
    String detail = state->tooltipDetail;
    b32 plain = state->tooltipKind == Prog_CommandKind_None, described = state->tooltipDescribes || plain;
    String hotkeys[3];
    b8 idle[3] = {0};
    u32 hotkeyCount = plain || (detail.length && !described) ? 0 : Prog_HotkeyTexts(&state->frameArena, state, (Prog_CommandKind)state->tooltipKind, hotkeys, idle, 3);
    if (described && !plain && !hotkeyCount) hotkeys[hotkeyCount++] = Tr("Blank");
    ArrayStr lines = detail.length ? StrSplit(&state->frameArena, detail, '\n') : (ArrayStr){0};
    f32 pad = floorf(9.6f * s + 0.5f), gap = floorf(4.8f * s + 0.5f), chipGap = floorf(8.0f * s + 0.5f), lineHeight = floorf(37.6f * s + 0.5f);
    f32 margin = floorf(6.4f * s + 0.5f);
    // The rows of a described tip, and of the others its words and its command, as tall as each other.
    f32 top = described ? floorf(8.0f * s + 0.5f) : 0, pitch = described ? floorf(18.4f * s + 0.5f) : floorf(m->line * 1.6f + 0.5f);
    f32 commandRow = plain ? 0 : described ? floorf(32.0f * s + 0.5f) : detail.length ? pitch : lineHeight;
    f32 bottom = described ? floorf((plain ? 8.8f : 4.8f) * s + 0.5f) : 0;
    f32 height = top + (f32)lines.header.count * pitch + commandRow + bottom;
    String lead = Tr("Cmd:");
    f32 width = plain ? 0 : R_TextWidth(m->font, lead) + gap + R_TextWidth(m->font, label);
    for (u32 i = 0; i < hotkeyCount; ++i) width += chipGap + MaxF32(R_TextWidth(m->detail, hotkeys[i]) + 2 * floorf(4.8f * s + 0.5f), floorf(30.4f * s + 0.5f));
    String chip = lines.header.count ? state->tooltipChip : (String){0};
    f32 chipWidth = chip.length ? chipGap + MaxF32(R_TextWidth(m->detail, chip) + 2 * floorf(4.8f * s + 0.5f), floorf(30.4f * s + 0.5f)) : 0;
    for (u64 i = 0; i < lines.header.count; ++i) width = MaxF32(width, R_TextWidth(m->font, lines.v[i]) + (i + 1 == lines.header.count ? chipWidth : 0));
    width += 2 * pad;
    R_Rect view = UI->root->rect, button = state->tooltipRect;
    // Centered under the button and kept a little inside the window.
    f32 x = floorf((button.x0 + button.x1 - width) * 0.5f);
    f32 y = button.y1 + margin;
    if (y + height > view.y1) y = button.y0 - height - margin;
    x = floorf(MaxF32(view.x0 + margin, MinF32(x, view.x1 - margin - width)));
    f32 radius = floorf(8.0f * s + 0.5f);
    Prog__DropShadow(x - view.x0, floorf(y) - view.y0, width, height, radius, opacity, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, floorf(y) - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0 });
    UI_SetNextChildrenPadding((UI_Padding){ pad, top, pad, bottom });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextOpacity(opacity);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *tip = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
    UI_Parent(tip) {
        for (u64 i = 0; i < lines.header.count; ++i) {
            if (i + 1 < lines.header.count || !chip.length) {
                Prog__Label(lines.v[i], UI_TextContent(), pitch, 0, theme->text, 0.5f);
                continue;
            }
            UI_SetNextPrefWidth(UI_Pixels(R_TextWidth(m->font, lines.v[i]) + chipWidth));
            UI_SetNextPrefHeight(UI_Pixels(pitch));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_Parent(UI_AddBlock(0)) {
                Prog__Label(lines.v[i], UI_TextContent(), pitch, 0, theme->text, 0);
                Prog__Spacer(UI_Pixels(chipGap), UI_Pixels(pitch));
                Prog__Chip(chip, pitch, false, false, m);
            }
        }
        if (!plain) {
            UI_SetNextPrefWidth(UI_Fill());
            UI_SetNextPrefHeight(UI_Pixels(commandRow));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_Block *command = UI_AddBlock(0);
            UI_Parent(command) {
                Prog__Label(lead, UI_TextContent(), commandRow, 0, theme->text, 0);
                Prog__Spacer(UI_Pixels(gap), UI_Pixels(commandRow));
                Prog__Label(label, UI_TextContent(), commandRow, 0, theme->textDim, 0);
                for (u32 i = 0; i < hotkeyCount; ++i) {
                    Prog__Spacer(UI_Pixels(chipGap), UI_Pixels(commandRow));
                    Prog__Chip(hotkeys[i], commandRow, false, idle[i], m);
                }
            }
        }
    }
}

// The command palette under the toolbar: the text typed in a field at the top, the commands it finds in two-line rows with what
// each does and its hotkeys, and the keys that work it at the bottom.
// The palette's tab mode, Search Tabs: a box hanging from the tab list button with the text typed at the top and under it the
// panel's tabs it finds, each with its folder's icon, name and path.
function void Prog__TabSearch(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PaletteCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, outside.pressOrder);
    u32 matches[Prog_MaxTabs];
    u32 count = Prog_TabMatches(state, matches);
    state->paletteHighlight = count ? Min(state->paletteHighlight, count - 1) : 0;
    f32 s = m->scale, line = m->separator, stroke = m->stroke;
    f32 field = floorf(32.0f * s + 0.5f), rowHeight = floorf(42.4f * s + 0.5f), pad = floorf(4.0f * s + 0.5f), inset = floorf(6.4f * s + 0.5f);
    f32 iconSlot = floorf(28.8f * s + 0.5f), lead = floorf(13.6f * s + 0.5f), textGap = floorf(14.4f * s + 0.5f), trail = floorf(8.0f * s + 0.5f);
    R_Rect button = state->tabListRect;
    f32 top = button.y1 + floorf(4.0f * s + 0.5f);
    f32 room = view.y1 - top - field - line - 2 * pad - m->gap;
    u32 shownMax = (u32)MinF32(MaxF32(floorf(room / rowHeight), 1.0f), 10.0f);
    u32 first = state->paletteHighlight >= shownMax ? state->paletteHighlight - shownMax + 1 : 0;
    u32 shown = Min(count - first, shownMax);
    // As wide as the longest name or path, within the window.
    // "tabs" even for one.
    String hint = StrFormat(&state->frameArena, TrF("Search %u tabs..."), state->tabCount);
    f32 textWidth = R_TextWidth(m->font, hint) + floorf(16.0f * s + 0.5f);
    for (u32 i = 0; i < count; ++i) {
        String path = Prog_TabPath(state, matches[i]);
        textWidth = MaxF32(textWidth, MaxF32(R_TextWidth(m->font, Prog_PathName(state, path)), R_TextWidth(m->detail, Prog__CrumbPath(state, path))));
    }
    // No narrower than this for a short name.
    f32 width = floorf(MinF32(MaxF32(inset + lead + iconSlot + textGap + textWidth + trail + inset, 187.2f * s), view.x1 - view.x0 - 2 * m->gap));
    f32 height = field + line + 2 * pad + (f32)shown * rowHeight;
    f32 x = floorf(MaxF32(view.x0 + m->gap, button.x1 - width));
    f32 opacity = Prog__FadeIn(UI_KeyFromStr(0, S("TabSearchReveal"))) * state->paletteFade;
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, top - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextOpacity(opacity);
    f32 radius = floorf(8.0f * s + 0.5f);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TabSearch")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        // The field's darker color follows the box's rounded top and stops square at the line under it.
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(field));
        UI_Block *fieldClip = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
        UI_Parent(fieldClip) {
            UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(field + radius));
            UI_SetNextCornerRadius(radius);
            UI_SetNextColors((UI_Colors){ .background = theme->inner });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
        UI_SetNextChildrenPadding((UI_Padding){ floorf(12.0f * s + 0.5f), 0, floorf(5.6f * s + 0.5f), 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(box) {
            Prog__SearchIcon(floorf(23.2f * s + 0.5f), field, theme->textDim, 0, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(hint);
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.5f), true, m);
            }
            UI_Interaction close = Prog__CloseButton(S("TabSearchClose"), m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, close.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextChildrenPadding((UI_Padding){ inset, pad, inset, pad });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels((f32)shown * rowHeight + 2 * pad), UI_Layout_Vertical, (R_Color){0}, 0);
        UI_Parent(list) for (u32 i = first; i < first + shown; ++i) {
            u32 tab = matches[i];
            UI_Key key = UI_KeyFromStr(Prog__TabKey(state, tab), S("Search"));
            b32 highlighted = i == state->paletteHighlight || UI->hotKey == key;
            String path = Prog_TabPath(state, tab), where = Prog__CrumbPath(state, path);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(rowHeight));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ lead, 0, trail, 0 });
            UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
            UI_SetNextColors((UI_Colors){ .background = theme->selection });
            UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | (highlighted ? UI_Flag_DrawBackground : 0));
            UI_Interaction interaction = UI_InteractionFromBlock(row);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, i, 0, interaction.clickOrder);
            UI_Parent(row) {
                // The folder's own picture filling the slot, as the icon views draw it, or its list icon while that loads.
                const Prog_Icon *picture = path.length ? Prog_FolderPicture(state, path, (i32)floorf(32.0f * s + 0.5f)) : NULL;
                const Prog_Icon *icon = picture ? picture : path.length ? Prog_FolderIcon(state, path) : NULL;
                f32 drawn = picture ? floorf(32.0f * s + 0.5f) : (f32)state->iconSize;
                UI_SetNextPrefWidth(UI_Pixels(iconSlot));
                UI_SetNextPrefHeight(UI_Pixels(rowHeight));
                UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                if (icon) {
                    UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ drawn, drawn }, (R_Color){ 1, 1, 1, 1 });
                    UI_AddBlock(UI_Flag_DrawImage);
                } else {
                    UI_SetNextFont(m->icons);
                    UI_SetNextColors((UI_Colors){ .text = theme->icon });
                    UI_SetNextText(Icon_Folder);
                    UI_AddBlock(UI_Flag_DrawText);
                }
                Prog__Spacer(UI_Pixels(textGap), UI_Pixels(rowHeight));
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(rowHeight));
                UI_SetNextChildrenLayout(UI_Layout_Vertical);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Block *texts = UI_AddBlock(UI_Flag_Clip);
                UI_Parent(texts) {
                    Prog__Label(path.length ? Prog_PathName(state, path) : Tr("This PC"), UI_Flex(), m->line, 0, theme->text, 0);
                    if (where.length) {
                        Prog__Spacer(UI_Flex(), UI_Pixels(floorf(2.4f * s + 0.5f)));
                        UI_SetNextFont(m->detail);
                        Prog__Label(where, UI_Flex(), m->detail->lineHeight, 0, highlighted ? theme->text : theme->textDim, 0);
                    }
                }
            }
        }
        // The border over everything inside.
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextCornerRadius(radius);
        UI_SetNextBorderThickness(stroke);
        UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
    }
}

// The width of the palette, Go To and the Options popup in a window `view` wide: half of it, from 720 to 960 units,
// inside the margins.
function f32 Prog__PaletteWidth(f32 view, f32 margin, f32 s)
{
    return MinF32(view - 2 * margin, UI__Clamp(view * 0.5f, 720.0f * s, 960.0f * s));
}

// The keys that work a palette, in its footer: each key's chip, then what it does.
function void Prog__PaletteHints(const char *const *keys, const String *words, u32 count, f32 height, R_Color background, f32 radius, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale;
    UI_Block *hints = Prog__Box(UI_Flex(), UI_Pixels(height), UI_Layout_Horizontal, background, 0);
    hints->props.childrenAlignment = (UI_Vec2){ 0.5f, 0.5f };
    // At the box's bottom, round along its corners, which its clip alone would leave square.
    hints->props.cornerRadius = radius;
    hints->props.shape = R_Shape_SquareTopLeft | R_Shape_SquareTopRight;
    // The keys sit in quiet chips, flatter than the menus' hotkey chips.
    f32 chipPad = floorf(7.2f * s + 0.5f), chipHeight = floorf(16.0f * s + 0.5f);
    UI_Parent(hints) for (u32 i = 0; i < count; ++i) {
        if (keys[i]) {
            String key = StrFromCStr(keys[i]);
            UI_SetNextPrefWidth(UI_Pixels(MaxF32(R_TextWidth(m->detail, key) + 2 * chipPad, floorf(31.2f * s + 0.5f))));
            UI_SetNextPrefHeight(UI_Pixels(chipHeight));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(floorf(6.4f * s + 0.5f));
            UI_SetNextBorderThickness(MaxF32(1, floorf(0.8f * s + 0.5f)));
            UI_SetNextFont(m->detail);
            UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder, .text = theme->textDim });
            UI_SetNextText(key);
            UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText);
            Prog__Spacer(UI_Pixels(floorf(1.6f * s + 0.5f)), UI_Pixels(height));
        } else {
            UI_SetNextFont(m->detail);
            Prog__LabelPadded(Prog_Tr(words[i]), UI_TextContent(), height, floorf(2.4f * s + 0.5f), floorf(12.8f * s + 0.5f), theme->textDim, 0);
        }
    }
}

// `text` in `color` with the bytes flagged in `marks`, when not NULL, in the match color.
function void Prog__MarkedText(String text, const u8 *marks, const R_Font *font, f32 height, R_Color color)
{
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *line = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(line) {
        for (u64 start = 0, end = 0; start < text.length; start = end) {
            b32 match = marks && marks[start];
            while (end < text.length && (marks && marks[end]) == match) ++end;
            UI_SetNextFont(font);
            Prog__Label(StrFromTo(text, start, (i64)end), UI_TextContent(), height, 0, match ? Prog_CurrentTheme()->textMatch : color, 0);
        }
    }
}

// `text` in `color` with what `search` finds in it in the match color, faded with the text when `faded` is set: the first place it appears
// whole, else its letters in order. On a highlighted row the match color goes to its full blue, which stands out from the highlight's.
function void Prog__MarkedLabel(Prog_State *state, String text, String search, const R_Font *font, f32 height, R_Color color, b32 faded, b32 lit, const Prog_Metrics *m)
{
    R_Color match = Prog_CurrentTheme()->textMatch;
    if (lit) match.b = 1.0f;
    if (faded) match.a *= 0.5f;
    u8 *marks = search.length ? ArenaPushArray(&state->frameArena, Max(text.length, (u64)1), u8) : NULL;
    b32 whole = false;
    for (u64 start = 0; marks && !whole && start + search.length <= text.length; ++start) {
        if (!Prog__ContainsFolded(StrFromTo(text, start, (i64)(start + search.length)), search)) continue;
        for (u64 i = 0; i < search.length; ++i) marks[start + i] = 1;
        whole = true;
    }
    if (marks && !whole && !Prog_FilterMatch(text, search, marks)) marks = NULL;
    for (u64 start = 0, end = 0; start < text.length; start = end) {
        b32 isMatch = marks && marks[start];
        while (end < text.length && (marks && marks[end]) == isMatch) ++end;
        R_Color matched = isMatch ? match : color;
        UI_SetNextFont(font);
        Prog__Label(StrFromTo(text, start, (i64)end), UI_TextContent(), height, 0, matched, 0);
    }
}

// The palette's Go To mode: the path typed in a field with buttons that step back, forward and up through what was typed,
// then the recent folders and the bookmarks under titles that fold them, or what is inside the folder typed, and the keys that work it.
function void Prog__GoTo(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PaletteCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, outside.pressOrder);
    u32 count;
    String folder;
    Prog_GoToItem *found = Prog_GoToMatches(state, &count, &folder);
    if (state->paletteHighlight != UINT32_MAX) state->paletteHighlight = count ? Min(state->paletteHighlight, count - 1) : 0;
    f32 s = m->scale, line = m->separator, row = m->row;
    f32 field = floorf(39.2f * s + 0.5f), footer = floorf(31.2f * s + 0.5f), pad = floorf(4.0f * s + 0.5f);
    f32 top = 2 * m->bar + floorf(11.2f * s + 0.5f), margin = floorf(32.8f * s + 0.5f), bottom = floorf(60.0f * s + 0.5f);
    // The rows in order: each section's title, then its items unless it is folded; a folder typed shows its items alone.
    typedef struct { i32 item; i32 section; b32 ghost; } Row;
    // Typing without a separator searches, and the rows found are one list too.
    b32 searching = !folder.length && state->palette.length;
    b32 sections = !folder.length && !searching;
    Row *rows = ArenaPushArray(&state->frameArena, count + Prog_SidebarGroupMax + 1, Row);
    u32 rowCount = 0, highlightRow = 0;
    // A section with nothing in it, folded or not, has no title either.
    u64 shownRecents[Prog_ShownRecentCount];
    u64 sizes[Prog_GoToSection_Folder] = { Prog_ShownRecents(state, shownRecents), state->bookmarks.header.count, state->drives.header.count, state->places.header.count };
    for (u32 n = 0; n < (sections ? Prog_SidebarGroupCount(state) : 1u); ++n) {
        u32 section = sections ? Prog_GoToSectionAt(state, n) : 0;
        u64 size = section < Prog_GoToSection_Folder ? sizes[section] : state->userGroups[section - Prog_GoToSection_Count].folders.header.count;
        if (sections && !size) continue;
        if (sections) rows[rowCount++] = (Row){ .item = -1, .section = (i32)section };
        for (u32 i = 0; i < count; ++i) {
            if (sections && found[i].section != section) continue;
            if (i == state->paletteHighlight) highlightRow = rowCount;
            rows[rowCount++] = (Row){ .item = (i32)i, .section = (i32)found[i].section };
        }
    }
    f32 room = view.y1 - view.y0 - top - bottom - field - 2 * line - footer - 2 * pad;
    // As the palette, the box reaches down to the bottom margin with the row after those that fit cut off.
    u32 visible = (u32)MaxF32(floorf((room + line) / row), 1.0f);
    // The list scrolls only as far as keeps the highlight in sight, with the title above it at the top, unless the wheel scrolled it.
    if (!state->paletteWheeled && highlightRow < state->gotoScroll + 1) state->gotoScroll = highlightRow ? highlightRow - (sections && rows[highlightRow - 1].item < 0 ? 1 : 0) : 0;
    if (!state->paletteWheeled && highlightRow >= state->gotoScroll + visible) state->gotoScroll = highlightRow - visible + 1;
    state->gotoScroll = rowCount > visible ? Min(state->gotoScroll, rowCount - visible) : 0;
    u32 first = state->gotoScroll, shown = Min(rowCount - first, visible + 1);
    // A section folding or unfolding eases while its title is in sight: its items, those it had for one folding up, show in a clip
    // `easeShown` tall, the rows after it move with it, and a box that does not fill its room grows or shrinks with it. The rows the list
    // scrolls by are the ones it has once the section is done.
    u32 easeFrom = UINT32_MAX, easeTo = 0;
    f32 easeShown = 0, easeFull = 0;
    Prog_GoToItem *ghosts = NULL;
    if (state->gotoEaseStart) {
        f32 since = (f32)(UI->nowMicroseconds - Min(state->gotoEaseStart, UI->nowMicroseconds)) / 1e6f, eased = 1 - expf(-34.0f * since);
        u32 section = state->gotoEaseSection, title = UINT32_MAX;
        for (u32 r = 0; r < rowCount && title == UINT32_MAX; ++r) if (rows[r].item < 0 && (u32)rows[r].section == section) title = r;
        if (!UI->animate || !sections || eased > 0.999f || title == UINT32_MAX) {
            state->gotoEaseStart = 0;
        } else {
            u32 end = title + 1;
            Row *easedRows = rows;
            if (state->gotoEaseFolding) {
                // Its items as they were, listed again with it unfolded.
                b32 kept = section < Prog_GoToSection_Folder && state->gotoFolded[section];
                u32 keptGroups = state->gotoGroupsFolded, ghostCount = 0;
                if (section < Prog_GoToSection_Folder) state->gotoFolded[section] = false;
                else state->gotoGroupsFolded &= ~(1u << (section - Prog_GoToSection_Count));
                String ghostFolder = {0};
                Prog_GoToItem *all = Prog_GoToMatches(state, &ghostCount, &ghostFolder);
                if (section < Prog_GoToSection_Folder) state->gotoFolded[section] = kept;
                state->gotoGroupsFolded = keptGroups;
                ghosts = all;
                u32 ghostRows = 0;
                for (u32 i = 0; i < ghostCount; ++i) ghostRows += all[i].section == section;
                easedRows = ArenaPushArray(&state->frameArena, rowCount + ghostRows + 1, Row);
                MemCopy(easedRows, rows, (title + 1) * sizeof(Row));
                for (u32 i = 0; i < ghostCount; ++i) if (all[i].section == section) easedRows[end++] = (Row){ (i32)i, (i32)section, true };
                MemCopy(easedRows + end, rows + title + 1, (rowCount - title - 1) * sizeof(Row));
                rowCount += end - title - 1;
            } else {
                while (end < rowCount && rows[end].item >= 0) ++end;
            }
            if (title < first || title >= first + visible || end == title + 1) {
                state->gotoEaseStart = 0;
                if (state->gotoEaseFolding) rowCount -= end - title - 1;
            } else {
                UI_KeepAnimating();
                rows = easedRows;
                easeFrom = title + 1;
                easeTo = end;
                easeFull = (f32)(end - title - 1) * row - (end == rowCount ? line : 0);
                easeShown = floorf(easeFull * (state->gotoEaseFolding ? 1 - eased : eased) + 0.5f);
                shown = Min(rowCount - first, visible + 1 + (end - title - 1));
            }
        }
    }
    // It opens a tenth smaller and grows into place, easing with a 31 ms time constant, while it fades in from nothing.
    f32 reveal = 1;
    if (UI->animate) {
        reveal = 1 - expf(-(f32)UI_TimeSince(UI_KeyFromStr(0, S("GoToReveal"))) / 31000.0f);
        if (reveal < 0.995f) UI_KeepAnimating();
        else reveal = 1;
    }
    f32 grow = 0.9f + 0.1f * reveal;
    row = floorf(row * grow + 0.5f);
    f32 width = floorf(Prog__PaletteWidth(view.x1 - view.x0, margin, s) * grow);
    f32 x = floorf((view.x1 - view.x0 - width) * 0.5f);
    f32 listHeight = MinF32((f32)shown * row - line - (easeFull - easeShown), floorf(room * grow)) + 2 * pad;
    f32 height = field + line + listHeight + line + footer;
    f32 opacity = Prog__FadeIn(UI_KeyFromStr(0, S("GoToFade"))) * state->paletteFade;
    f32 radius = floorf(8.0f * s + 0.5f);
    Prog__DropShadow(x, top, width, height, radius, opacity, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextOpacity(opacity);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill() });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("GoTo")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(field));
        UI_Block *fieldClip = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
        UI_Parent(fieldClip) {
            UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(field + radius));
            UI_SetNextCornerRadius(radius);
            UI_SetNextColors((UI_Colors){ .background = theme->inner });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
        UI_SetNextChildrenPadding((UI_Padding){ floorf(12.0f * s + 0.5f), 0, floorf(8.0f * s + 0.5f), 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(box) {
            // The folder typed shows its own icon where the search glass is.
            const Prog_Icon *folderIcon = folder.length ? Prog_FolderIcon(state, folder) : NULL;
            f32 lead = floorf(22.4f * s + 0.5f);
            if (folderIcon) {
                UI_SetNextPrefWidth(UI_Pixels(lead));
                UI_SetNextPrefHeight(UI_Pixels(field));
                UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextImage(folderIcon->texture, folderIcon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, (R_Color){ 1, 1, 1, 1 });
                UI_AddBlock(UI_Flag_DrawImage);
            } else {
                // The glass sits a pixel further in than the folder's picture.
                f32 inset = floorf(0.8f * s + 0.5f);
                Prog__Spacer(UI_Pixels(inset), UI_Pixels(field));
                Prog__SearchIcon(lead - inset, field, theme->textDim, 0, 0, m);
            }
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    // A little past the caret.
                    UI_SetNextFloatingOffset((UI_Vec2){ floorf(3.2f * s + 0.5f), floorf((field - m->line) * 0.5f + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(Tr("Open or expand a folder..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.5f), true, m);
            }
            struct { String identity; String icon; Prog_CommandKind kind; b32 enabled; } buttons[] = {
                { S("GoToBack"), Icon_ArrowLeft, Prog_CommandKind_GoToBack, state->gotoHistoryAt > 0 },
                { S("GoToForward"), Icon_ArrowRight, Prog_CommandKind_GoToForward, state->gotoHistoryAt + 1 < state->gotoHistory.header.count },
                { S("GoToUp"), Icon_ArrowUp, Prog_CommandKind_GoToUp, folder.length != 0 },
            };
            for (u32 i = 0; i < CArrayCount(buttons); ++i) {
                UI_Interaction button = Prog__TextButton(buttons[i].identity, buttons[i].icon, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                         theme->icon, theme->text, theme->iconDisabled, buttons[i].enabled, m);
                if (button.clicked) Prog_QueueCommand(state, buttons[i].kind, 0, 0, button.clickOrder);
            }
            Prog__Spacer(UI_Pixels(floorf(6.4f * s + 0.5f)), UI_Pixels(field));
            Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
            Prog__Spacer(UI_Pixels(floorf(5.6f * s + 0.5f)), UI_Pixels(field));
            UI_Interaction close = Prog__CloseButton(S("GoToClose"), m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, close.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        // Room on the right for the scroll bar while there is one.
        // A section easing counts for as much as it shows, so that the scroll bar comes and goes with it.
        f32 rowsShown = (f32)rowCount - (easeFull - easeShown) / row;
        b32 scrolls = rowsShown > (f32)visible + 0.01f;
        f32 bar = floorf(6.4f * s + 0.5f), gutter = scrolls ? floorf(19.2f * s + 0.5f) : floorf(6.4f * s + 0.5f);
        UI_SetNextChildrenPadding((UI_Padding){ pad, pad, gutter, pad });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(list) {
            UI_Block *outside = NULL;
            for (u32 r = first; r < first + shown; ++r) {
                Row at = rows[r];
                f32 face = row - line;
                // The section easing open or shut has its items in a clip as tall as it shows.
                if (r == easeFrom) outside = UI_PushParent(Prog__Box(UI_Flex(), UI_Pixels(easeShown), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip));
                if (r == easeTo && outside) {
                    UI_PopParent(outside);
                    outside = NULL;
                }
                if (at.item < 0) {
                    // A section's title, which folds it.
                    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "GoToSection/%d", at.section));
                    b32 folded = Prog_GoToFolded(state, (u32)at.section);
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(face));
                    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                    UI_SetNextChildrenPadding((UI_Padding){ floorf(8.8f * s + 0.5f), 0, floorf(12.0f * s + 0.5f), 0 });
                    UI_Block *title = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable);
                    UI_Interaction interaction = UI_InteractionFromBlock(title);
                    if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_GoToFold, (u64)at.section, 0, interaction.clickOrder);
                    UI_Parent(title) {
                        local_persist const String titles[] = { TrInit("Recents"), TrInit("Bookmarks"), TrInit("Storage"), TrInit("Places") };
                        const String icons[] = { Icon_Clock, Icon_Bookmark, Icon_HardDrive, Icon_Folder };
                        static_assert(CArrayCount(titles) == Prog_GoToSection_Folder, "a title for every section");
                        b32 own = at.section >= Prog_GoToSection_Count;
                        Prog__Icon(own ? Icon_Folder : icons[at.section], (f32)state->iconSize + floorf(7.2f * s + 0.5f), face, theme->icon, 0, m);
                        Prog__Label(own ? state->userGroups[at.section - Prog_GoToSection_Count].name : Prog_Tr(titles[at.section]), UI_Flex(), face, 0, theme->text, 0);
                        Prog__Icon(folded ? Icon_ChevronRight : Icon_ChevronDown, floorf(m->line * 1.2f), face, theme->icon, 0.5f, m);
                    }
                    Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
                    continue;
                }
                Prog_GoToItem item = at.ghost ? ghosts[at.item] : found[at.item];
                UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "GoTo/%d/%S", at.section, item.path));
                b32 highlighted = !at.ghost && (u32)at.item == state->paletteHighlight, hot = UI->hotKey == key;
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(face));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                // Section items sit in from their title; a folder's items do not.
                UI_SetNextChildrenPadding((UI_Padding){ floorf((sections ? 32.8f : 8.8f) * s + 0.5f), 0, pad, 0 });
                UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
                UI_SetNextBorderThickness(m->stroke);
                UI_SetNextColors((UI_Colors){ .background = theme->selection, .border = theme->selection });
                UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_Clip | (highlighted ? UI_Flag_DrawBackground : 0) | (hot && !highlighted ? UI_Flag_DrawBorder : 0));
                UI_Interaction interaction = UI_InteractionFromBlock(entry);
                if (interaction.clicked && !at.ghost) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, (u64)at.item, 0, interaction.clickOrder);
                UI_Parent(entry) {
                    const Prog_Icon *icon = item.file ? Prog_PathIcon(state, item.path, P_FileKind_File) : item.drive ? Prog_PathIcon(state, item.path, P_FileKind_Drive) : Prog_FolderIcon(state, item.path);
                    b32 faded = item.hidden && !highlighted;
                    f32 slot = (f32)state->iconSize + floorf(7.2f * s + 0.5f);
                    if (icon) {
                        UI_SetNextPrefWidth(UI_Pixels(slot));
                        UI_SetNextPrefHeight(UI_Pixels(face));
                        UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
                        UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, (R_Color){ 1, 1, 1, faded ? theme->hiddenAmount.r : 1 });
                        UI_AddBlock(UI_Flag_DrawImage);
                    } else {
                        Prog__Icon(item.file ? Icon_File : item.drive ? Icon_HardDrive : Icon_Folder, slot, face, faded ? theme->iconDisabled : theme->icon, 0, m);
                    }
                    // What was searched for shows in the match color, in the name and in the path.
                    String name = item.name.length ? item.name : Prog_PathName(state, item.path), where = Prog__GoToWhere(state, item.path), typed = Prog_EditText(&state->palette);
                    R_Color dim = highlighted ? theme->text : theme->textDim;
                    if (item.drive && item.drive->totalBytes) {
                        // A drive's fill shows under its name, as in the sidebar.
                        f32 nameWidth = R_TextWidth(m->font, name), bar = MaxF32(2.0f, floorf(4.0f * s + 0.5f));
                        f32 used = floorf(nameWidth * (f32)(item.drive->totalBytes - Min(item.drive->freeBytes, item.drive->totalBytes)) / (f32)item.drive->totalBytes);
                        f32 textTop = floorf((face - m->line - bar * 2) * 0.5f);
                        UI_SetNextPrefWidth(UI_Pixels(nameWidth));
                        UI_SetNextPrefHeight(UI_Pixels(face));
                        UI_Block *stack = UI_AddBlock(0);
                        UI_Parent(stack) {
                            UI_SetNextFloatingOffset((UI_Vec2){ 0, textTop });
                            UI_SetNextPrefHeight(UI_Pixels(m->line));
                            UI_SetNextColors((UI_Colors){ .text = theme->text });
                            UI_SetNextText(name);
                            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                            UI_SetNextFloatingOffset((UI_Vec2){ 0, textTop + m->line + bar });
                            UI_SetNextPrefWidth(UI_Pixels(nameWidth));
                            UI_SetNextPrefHeight(UI_Pixels(bar));
                            UI_SetNextColors((UI_Colors){ .background = highlighted ? theme->text : theme->track });
                            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                            UI_SetNextFloatingOffset((UI_Vec2){ 0, textTop + m->line + bar });
                            UI_SetNextPrefWidth(UI_Pixels(used));
                            UI_SetNextPrefHeight(UI_Pixels(bar));
                            UI_SetNextColors((UI_Colors){ .background = theme->accent });
                            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                        }
                    } else {
                        Prog__MarkedLabel(state, name, searching ? typed : (String){0}, m->font, face, faded ? theme->textDim : theme->text, faded, highlighted, m);
                    }
                    if (sections || searching) {
                        Prog__Spacer(UI_Pixels(floorf(5.6f * s + 0.5f)), UI_Pixels(face));
                        Prog__Icon(Icon_ChevronRight, R_TextWidth(m->icons, Icon_ChevronRight) + floorf(4.0f * s + 0.5f), face, dim, 0, m);
                        Prog__MarkedLabel(state, where, searching ? typed : (String){0}, m->detail, face, dim, faded, highlighted, m);
                    }
                }
                if (r + 1 < first + shown) Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
            }
            if (outside) UI_PopParent(outside);
            if (scrolls) {
                // The scroll bar: a thumb as long as the share of rows shown, where they are among all of them.
                f32 track = listHeight - 2 * pad, thumb = MaxF32(floorf(track * (f32)visible / rowsShown), row);
                f32 offset = floorf((track - thumb) * MinF32(1, (f32)first / (rowsShown - (f32)visible)));
                UI_SetNextFloatingOffset((UI_Vec2){ width - pad - gutter + floorf((gutter - bar) * 0.5f), offset });
                UI_SetNextPrefWidth(UI_Pixels(bar));
                UI_SetNextPrefHeight(UI_Pixels(thumb));
                UI_SetNextCornerRadius(floorf(bar * 0.5f));
                UI_SetNextOpacity(opacity);
        UI_SetNextColors((UI_Colors){ .background = theme->track });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        local_persist const char *const keys[] = { "Up", "Down", 0, "Tab", 0, "Enter", 0, "Right", 0, "Alt+E", 0, "Ctrl+Enter", 0, "Escape", 0 };
        // Shorter than the other footers' words, as Go To has more keys to name.
        local_persist const String words[] = { {0}, {0}, TrInit("navigate"), {0}, TrInit("next folder"), {0}, TrInit("open"), {0}, TrInit("expand"), {0}, TrInit("all drives"), {0},
                                               TrInit("in panel"), {0}, TrInit("dismiss") };
        local_persist const String wordsDrives[] = { {0}, {0}, TrInit("navigate"), {0}, TrInit("next folder"), {0}, TrInit("open"), {0}, TrInit("expand"), {0}, TrInit("places only"), {0},
                                                     TrInit("in panel"), {0}, TrInit("dismiss") };
        if (state->gotoSearching) UI_KeepAnimating();
        Prog__PaletteHints(keys, state->gotoDrives ? wordsDrives : words, CArrayCount(keys), footer, theme->background, radius, m);
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextOpacity(opacity);
    UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
}

// Queues `option` set to `value`, as its controls do.
// Queues `option` set to `value`, as its controls do; `preview` shows it without saving it yet.
function void Prog__QueueOption(Prog_State *state, Prog_Option option, i32 value, u64 order, b32 preview)
{
    Prog_QueueCommand(state, Prog_CommandKind_OptionSet, (u64)option, preview ? 1 : 0, order)->target = (u64)(i64)value;
}

// A choice's name as the popup shows it: longer words where Settings.conf has shorter ones, else with a capital first letter.
function String Prog__OptionName(Arena *arena, Prog_Option option, String name)
{
    // A language goes by its own name.
    if (option == Prog_Option_Language) return name;
    // The names below come of the settings' words with a capital first letter; listed here, they are translated as the rest are.
    local_persist const String words[] = { TrInit("Square"), TrInit("Round"), TrInit("Auto"), TrInit("Never"), TrInit("Always"), TrInit("Default"), TrInit("Acrylic"),
                                           TrInit("Mica"), TrInit("Tabbed"), TrInit("None"), TrInit("Persistent"), TrInit("Session"), TrInit("Root"), TrInit("Home"),
                                           TrInit("Current"), TrInit("All") };
    (void)words;
    if (option == Prog_Option_LoadThumbnails && StrIsEqual(name, S("local"))) return Tr("Local Disks");
    if (option == Prog_Option_VideoCodec) return StrIsEqual(name, S("hevc")) ? NoTr("HEVC (H.265)") : StrIsEqual(name, S("h264")) ? NoTr("H.264") : NoTr("AV1");
    if (option == Prog_Option_Borders) return StrFormat(arena, TrF("%S px"), name);
    if (option == Prog_Option_InspectorLayout && StrIsEqual(name, S("right"))) return Tr("Horizontal");
    if (option == Prog_Option_InspectorLayout && StrIsEqual(name, S("below"))) return Tr("Vertical");
    if (option == Prog_Option_LoadOnStartup && StrIsEqual(name, S("session"))) return Tr("Last Session");
    if (option == Prog_Option_Notices) {
        if (StrIsEqual(name, S("warnings"))) return Tr("Warnings And Errors");
        if (StrIsEqual(name, S("errors"))) return Tr("Errors Only");
        if (StrIsEqual(name, S("off"))) return Tr("None");
    }
    // A built-in scheme shows its theme's own name, and a custom one keeps its.
    b32 schemes = option == Prog_Option_ColorScheme || option == Prog_Option_LightScheme || option == Prog_Option_DarkScheme;
    for (u32 i = 0; schemes && i < Prog_BuiltInSchemes; ++i) if (StrIsEqual(name, Prog_OptionSchemes[i])) return StrFromCStr(Prog_OptionSchemeLabels[i]);
    if (schemes) return name;
    // A layout keeps its own name.
    if (option == Prog_Option_LoadOnStartup && !StrIsEqual(name, S("root")) && !StrIsEqual(name, S("home"))) return name;
    if (option == Prog_Option_CharacterKeyAction) {
        if (StrIsEqual(name, S("filter"))) return Tr("Type In Filter");
        if (StrIsEqual(name, S("hotkey"))) return Tr("Trigger Hotkey");
        if (StrIsEqual(name, S("select"))) return Tr("Select Typed Item");
    }
    String result = StrCopy(arena, name);
    if (result.length && result.value[0] >= 'a' && result.value[0] <= 'z') ((char *)result.value)[0] = (char)(result.value[0] - 'a' + 'A');
    return Prog_Tr(result);
}

// A switch `wide` by `tall` with a knob `inset` from its edges, on while `on`, which reports its clicks.
function UI_Interaction Prog__Switch(UI_Key key, b32 on, f32 wide, f32 tall, f32 inset, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 knob = tall - 2 * inset;
    UI_SetNextPrefWidth(UI_Pixels(wide));
    UI_SetNextPrefHeight(UI_Pixels(tall));
    UI_SetNextCornerRadius(tall * 0.5f);
    UI_SetNextBorderThickness(MaxF32(1, floorf(0.8f * m->scale + 0.5f)));
    UI_SetNextColors((UI_Colors){ .background = on ? theme->selection : theme->track, .border = theme->outline });
    UI_Block *toggle = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_LeftMouseClickable | (on ? 0 : UI_Flag_DrawBorder));
    UI_Interaction result = UI_InteractionFromBlock(toggle);
    // The knob slides across, easing; the color changes at once.
    f32 knobX = UI_Animate(UI_KeyFromStr(key, S("Knob")), on ? wide - inset - knob : inset, on ? wide - inset - knob : inset);
    UI_Parent(toggle) {
        UI_SetNextFloatingOffset((UI_Vec2){ floorf(knobX + 0.5f), inset });
        UI_SetNextPrefWidth(UI_Pixels(knob));
        UI_SetNextPrefHeight(UI_Pixels(knob));
        UI_SetNextCornerRadius(knob * 0.5f);
        // The knob's side and the track's color say whether it is on, without a check on it.
        UI_SetNextColors((UI_Colors){ .background = on ? theme->text : theme->textDim });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
    return result;
}

// The control at the right of `option`'s row: a switch, a stepper with its value between - and +,
// a slider dragged along its track, or a choice that moves on to its next value.
// An option's button that opens a list of what it can be: its value in the middle and a chevron, which points down while the list is open.
function UI_Interaction Prog__OptionButton(Arena *frame, UI_Key key, String label, b32 open, f32 width, f32 height, f32 line, f32 radius, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    b32 hot = UI->hotKey == key;
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(line);
    UI_SetNextColors((UI_Colors){ .background = open ? theme->selection : theme->track, .border = open ? theme->selection : hot ? theme->textDim : theme->outline });
    UI_Block *button = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_LeftMouseClickable | UI_Flag_Clip);
    UI_Parent(button) {
        f32 chevron = floorf(20.8f * m->scale + 0.5f);
        R_Color ink = open ? theme->contentSelection : theme->text;
        Prog__Spacer(UI_Pixels(chevron), UI_Pixels(height));
        Prog__Label(Prog__Ellipsize(frame, m->font, label, width - 2 * chevron), UI_Flex(), height, 0, ink, 0.5f);
        Prog__Icon(open ? Icon_ChevronDown : Icon_ChevronRight, chevron, height, open ? theme->contentSelection : theme->icon, 0.5f, m);
    }
    return UI_InteractionFromBlock(button);
}

function void Prog__OptionControl(Prog_State *state, Prog_Option option, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const Prog_OptionDesc *desc = &Prog_Options[option];
    Arena *frame = &state->frameArena;
    f32 s = m->scale, width = floorf(162.4f * s + 0.5f), height = floorf(21.6f * s + 0.5f), line = MaxF32(1, floorf(0.8f * s + 0.5f)), radius = floorf(4.0f * s + 0.5f);
    i32 value = Prog_OptionValue(state, option);
    // An option away from its default offers to go back to it.
    if (!Prog_OptionIsDefault(state, option)) {
        UI_Interaction restore = Prog__TextButton(StrFormat(frame, "OptionRestore/%u", (u32)option), Icon_RotateCcwSquare, m->icons, UI_Pixels(floorf(28.8f * s + 0.5f)),
                                                  height, 0, theme->icon, theme->text, theme->iconDisabled, true, m);
        if (restore.clicked) Prog_QueueCommand(state, Prog_CommandKind_OptionRestore, (u64)option, 0, restore.clickOrder);
        Prog__PopupTooltipKey(state, Tr("Restore default setting"), NoTr("Backspace"));
        Prog__Spacer(UI_Pixels(floorf(6.4f * s + 0.5f)), UI_Pixels(height));
    }
    switch (desc->control) {
        case Prog_OptionControl_Switch: {
            UI_Interaction interaction = Prog__Switch(UI_KeyFromStr(0, StrFormat(frame, "OptionSwitch/%u", (u32)option)), value, floorf(41.6f * s + 0.5f), floorf(22.4f * s + 0.5f),
                                                      floorf(1.6f * s + 0.5f), m);
            if (interaction.clicked) Prog__QueueOption(state, option, !value, interaction.clickOrder, false);
        } break;
        case Prog_OptionControl_Stepper: {
            f32 cell = floorf(28.8f * s + 0.5f);
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .border = theme->outline });
            UI_Block *box = UI_AddBlock(UI_Flag_DrawBorder | UI_Flag_Clip);
            UI_Parent(box) {
                for (u32 side = 0; side < 2; ++side) {
                    if (side) Prog__Label(StrFormat(frame, "%d", value), UI_Flex(), height, 0, theme->text, 0.5f);
                    b32 up = side == 1, enabled = up ? value < desc->most : value > desc->least;
                    UI_Interaction step = Prog__TextButton(StrFormat(frame, "OptionStep/%u/%u", (u32)option, side), up ? Icon_Plus : Icon_Minus, m->icons, UI_Pixels(cell), height,
                                                           0, theme->icon, theme->text, theme->iconDisabled, enabled, m);
                    UI->parent->last->props.colors.background = theme->track;
                    UI->parent->last->flags |= UI_Flag_DrawBackground;
                    if (step.clicked) Prog__QueueOption(state, option, value + (up ? 1 : -1), step.clickOrder, false);
                }
            }
        } break;
        case Prog_OptionControl_Slider: {
            // Its value inside a box filled from the left up to it: dragged along to set it, or clicked to type it, arithmetic too.
            f32 range = (f32)Max(desc->most - desc->least, 1), at = (f32)(value - desc->least) / range;
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "OptionSlider/%u", (u32)option));
            b32 typing = state->sliderOption == option + 1, hot = UI->hotKey == key;
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(line);
            R_Color border = typing ? (state->sliderInvalid ? theme->danger : theme->outlineSelection) : hot ? theme->outlineHover : theme->outline;
            UI_SetNextColors((UI_Colors){ .background = typing ? theme->inner : theme->track, .border = border });
            UI_Block *slider = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable | (typing ? 0 : UI_Flag_TakesWheel));
            UI_Interaction drag = UI_InteractionFromBlock(slider);
            if (!typing) {
                // The wheel over it steps it, a notch a step.
                i32 notches = Prog__WheelNotches(slider);
                if (notches) Prog__QueueOption(state, option, Max(desc->least, Min(desc->most, value + notches)), 0, false);
                // A press only sets it once it moves; let go where it was pressed, it starts typing instead.
                if (drag.pressed) state->sliderDragged = false;
                if (drag.active && drag.isDragging) {
                    state->sliderDragged = true;
                    f32 along = UI__Clamp((UI->mouse.x - slider->rect.x0) / MaxF32(1, slider->rect.x1 - slider->rect.x0), 0, 1);
                    i32 wanted = desc->least + (i32)floorf(along * range + 0.5f);
                    if (wanted != value) Prog__QueueOption(state, option, wanted, drag.pressOrder, true);
                }
                // Saved once, where it is let go.
                if (drag.released && state->sliderDragged) Prog__QueueOption(state, option, value, drag.pressOrder, false);
                else if (drag.released) Prog_QueueCommand(state, Prog_CommandKind_OptionSliderEdit, (u64)option, 0, drag.pressOrder);
            }
            if (typing && state->sliderInvalid) Prog__PopupTooltip(state, Tr("Not a number or a sum that makes one."));
            UI_Parent(slider) {
                if (typing) {
                    // In the middle, where the value was, until it no longer fits.
                    f32 typedWidth = R_TextWidth(m->font, Prog_EditText(&state->sliderEdit)) + Prog__CaretWidth(m);
                    f32 inset = MaxF32(floorf(6.4f * s + 0.5f), floorf((width - typedWidth) * 0.5f));
                    Prog__EditContents(&state->sliderEdit, inset, floorf((height - m->line) * 0.5f), true, m);
                } else {
                    // The fill keeps clear of the border, a little inside it.
                    f32 gap = line + floorf(1.6f * s + 0.5f);
                    UI_SetNextFloatingOffset((UI_Vec2){ gap, gap });
                    UI_SetNextPrefWidth(UI_Pixels(floorf(at * (width - 2 * gap) + 0.5f)));
                    UI_SetNextPrefHeight(UI_Pixels(height - 2 * gap));
                    UI_SetNextCornerRadius(MaxF32(0, radius - gap));
                    UI_SetNextColors((UI_Colors){ .background = theme->selection });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    // Opacities are percentages.
                    b32 percent = option == Prog_Option_InactiveOpacity || option == Prog_Option_WindowOpacity || option == Prog_Option_PopupOpacity;
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                    UI_SetNextPrefWidth(UI_Pixels(width));
                    UI_SetNextPrefHeight(UI_Pixels(height));
                    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                    UI_SetNextColors((UI_Colors){ .text = theme->text });
                    UI_SetNextText(StrFormat(frame, percent ? "%d%%" : "%d", value));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
            }
        } break;
        case Prog_OptionControl_Expand: {
            Prog__Icon(state->colorsExpanded ? Icon_ChevronDown : Icon_ChevronRight, floorf(20.8f * s + 0.5f), height, theme->icon, 0.5f, m);
        } break;
        case Prog_OptionControl_Color: {
            // The color's hex digits in a field, which a press types in, and a swatch of it beside them, which a click opens the picker for.
            R_Color color = Prog_OptionColor(state, option);
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "OptionColor/%u", (u32)option));
            b32 hot = UI->hotKey == key, typing = state->hexOption == option;
            f32 field = floorf(96.0f * s + 0.5f), gap = floorf(6.4f * s + 0.5f);
            UI_SetNextPrefWidth(UI_Pixels(field));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .background = theme->inner, .border = typing ? theme->outlineSelection : theme->outline, .text = theme->text });
            if (!typing) UI_SetNextText(StrFormat(frame, "%06X", state->schemeGroups[option - Prog_Option_ColorClear]));
            UI_Block *hex = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(frame, "OptionHex/%u", (u32)option)),
                                               UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_LeftMouseClickable | UI_Flag_Clip | (typing ? 0 : UI_Flag_DrawText));
            UI_Interaction press = UI_InteractionFromBlock(hex);
            if (press.pressed && !typing) Prog_QueueCommand(state, Prog_CommandKind_ColorHexEdit, (u64)option, 0, press.pressOrder);
            if (typing) UI_Parent(hex) Prog__EditContents(&state->hexEdit, floorf(4.0f * s + 0.5f), floorf((height - m->line) * 0.5f), true, m);
            Prog__Spacer(UI_Pixels(gap), UI_Pixels(height));
            UI_SetNextPrefWidth(UI_Pixels(width - field - gap));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .background = color, .border = hot ? theme->outlineHover : theme->outline });
            UI_Interaction interaction = UI_InteractionFromBlock(UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_LeftMouseClickable));
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_ColorPickerOpen, (u64)option, 0, interaction.clickOrder);
        } break;
        case Prog_OptionControl_Muted: {
            // How many kinds are muted, which a click lists.
            String label = StrFormat(frame, TrF("%u muted"), (u32)state->mutedNotices.header.count);
            UI_Interaction interaction = Prog__OptionButton(frame, UI_KeyFromStr(0, S("OptionMuted")), label, state->mutedOpen, width, height, line, radius, m);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_MutedNoticesOpen, 0, 0, interaction.clickOrder);
        } break;
        case Prog_OptionControl_Font: {
            // The family's name, which a click lets be chosen from the installed ones.
            b32 open = state->fontsOver && state->fontOption == option;
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "OptionFont/%u", (u32)option));
            UI_Interaction interaction = Prog__OptionButton(frame, key, Prog__FontChosen(state, option), open, width, height, line, radius, m);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_FontPickerOpen, (u64)option, 0, interaction.clickOrder);
        } break;
        case Prog_OptionControl_Choice: {
            u32 choiceCount = 0;
            const String *choices = Prog_OptionChoices(state, option, frame, &choiceCount);
            String label = Prog__OptionName(frame, option, choices[Min((u32)value, choiceCount - 1)]);
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "OptionChoice/%u", (u32)option));
            UI_Interaction interaction = Prog__OptionButton(frame, key, label, state->schemesOpen == option + 1, width, height, line, radius, m);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_SchemesOpen, (u64)option, 0, interaction.clickOrder);
        } break;
        default: break;
    }
}

// An option's row in the Options popup: its name over its description, and its control at the right; the highlight is an outline.
function void Prog__OptionRow(Prog_State *state, Prog_Option option, u32 index, b32 highlighted, f32 face, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const Prog_OptionDesc *desc = &Prog_Options[option];
    f32 s = m->scale;
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Option/%u", (u32)option));
    b32 hot = UI->hotKey == key && !highlighted;
    R_Color washed = theme->selection;
    washed.a *= theme->hoverAmount.r;
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(face));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ floorf(8.0f * s + 0.5f), 0, floorf(6.4f * s + 0.5f), 0 });
    UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .background = washed, .border = theme->selection });
    UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_Clip | (hot ? UI_Flag_DrawBackground : 0) | (highlighted || hot ? UI_Flag_DrawBorder : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(entry);
    if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, index, 0, interaction.clickOrder);
    // Over its own controls too, for Backspace to restore it.
    R_Rect r = entry->rect;
    if (UI->mouse.x >= r.x0 && UI->mouse.x < r.x1 && UI->mouse.y >= r.y0 && UI->mouse.y < r.y1) state->optionHovered = (u8)(option + 1);
    UI_Parent(entry) {
        // The colors and the options that narrow the one above them sit in under it.
        b32 nested = desc->control == Prog_OptionControl_Color || option == Prog_Option_SingleClickFoldersOnly ||
                     (option >= Prog_Option_CalcFolderSizesSSD && option <= Prog_Option_CalcFolderSizesNetwork) || (option >= Prog_Option_WindowOpacity && option <= Prog_Option_Backdrop);
        if (nested) Prog__Spacer(UI_Pixels(floorf(24.0f * s + 0.5f)), UI_Pixels(face));
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(face));
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ 0, state->paletteCompact ? floorf((face - m->line) * 0.5f) : floorf(4.8f * s + 0.5f), 0, 0 });
        UI_Block *texts = UI_AddBlock(UI_Flag_Clip);
        UI_Parent(texts) {
            Prog__MarkedText(Prog_Tr(desc->label), NULL, m->font, m->line, option == Prog_Option_ColorGroups ? theme->textHeading : theme->text);
            if (!state->paletteCompact) {
                Prog__Spacer(UI_Flex(), UI_Pixels(floorf(2.4f * s + 0.5f)));
                Prog__MarkedText(Prog_Tr(desc->tooltip), NULL, m->detail, m->detail->lineHeight, theme->textDim);
            }
        }
        Prog__OptionControl(state, option, m);
    }
}

// A rectangle of an icon drawn from them, filled, or outlined `border` thick.
function void Prog__IconRect(f32 x, f32 y, f32 width, f32 height, R_Color color, f32 border, f32 radius)
{
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextCornerRadius(radius);
    if (border > 0) UI_SetNextBorderThickness(border);
    UI_SetNextColors((UI_Colors){ .background = color, .border = color });
    UI_AddBlock(UI_Flag_Floating | (border > 0 ? UI_Flag_DrawBorder : UI_Flag_DrawBackground));
}

// The folder sort's icon in the block being built: the top of a folder over a bar for folders first, of a file for files
// first, and a folder in front of a file for both mixed. `behind` is what it is drawn on, which hides the file behind the folder.
function void Prog__FolderSortIcon(Prog_FolderSort sort, R_Color color, R_Color behind, const Prog_Metrics *m)
{
    f32 s = m->scale, size = floorf(14.4f * s + 0.5f), t = MaxF32(1, floorf(1.6f * s + 0.5f)), radius = floorf(1.6f * s + 0.5f);
    f32 top = floorf((m->pill - size) * 0.5f);
    if (sort == Prog_FolderSort_Mixed) {
        // The file, up and to the right, with its corner folded.
        f32 fileX = floorf(2.8f * s + 0.5f), fileWidth = size - fileX, fileHeight = floorf(13.6f * s + 0.5f), ear = floorf(4.0f * s + 0.5f);
        Prog__IconRect(fileX, top, fileWidth, fileHeight, color, t, radius);
        Prog__IconRect(size - ear, top, ear, ear, behind, 0, 0);
        Prog__IconRect(size - ear, top, t, ear, color, 0, 0);
        Prog__IconRect(size - ear, top + ear - t, ear, t, color, 0, 0);
        // The folder in front of it, down and to the left.
        f32 folderY = top + floorf(5.0f * s + 0.5f), folderWidth = floorf(11.6f * s + 0.5f), folderHeight = size - (folderY - top), tab = floorf(4.8f * s + 0.5f);
        Prog__IconRect(0, folderY, folderWidth, folderHeight, behind, 0, radius);
        Prog__IconRect(0, folderY, tab, t, color, 0, 0);
        Prog__IconRect(0, folderY + t, folderWidth, folderHeight - t, color, t, radius);
        return;
    }
    // The top of a folder or a file, open at the bottom, over a bar.
    f32 walls = floorf(7.6f * s + 0.5f), barY = floorf(8.4f * s + 0.5f);
    Prog__IconRect(0, top, t, walls, color, 0, 0);
    if (sort == Prog_FolderSort_First) {
        f32 tab = floorf(5.6f * s + 0.5f);
        Prog__IconRect(0, top, tab, t, color, 0, 0);
        Prog__IconRect(tab, top + t, size - tab, t, color, 0, 0);
        Prog__IconRect(size - t, top + t, t, walls - t, color, 0, 0);
    } else {
        f32 ear = floorf(4.0f * s + 0.5f);
        Prog__IconRect(0, top, size - ear, t, color, 0, 0);
        Prog__IconRect(size - ear, top, t, ear, color, 0, 0);
        Prog__IconRect(size - ear, top + ear - t, ear, t, color, 0, 0);
        Prog__IconRect(size - t, top + ear - t, t, walls - ear + t, color, 0, 0);
    }
    Prog__IconRect(0, top + barY, size, size - barY, color, t, radius);
}

// Ctrl+wheel's popup over the view button: the views from Details up to XL Icons beside a slider at the zoom, the shown view's row lit.
// Details, List, Columns and M Icons are a row apart; the icon sizes above them spread along the slider by their percentage.
// The Types popup under the Type column's header: a box that finds types, with buttons that check and clear them all, over a row for each of
// the listing's types with how many items have it and a box that shows it. A click on a box toggles it; elsewhere on its row, it also closes.
function void Prog__TypesPopup(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->typesOpen) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    f32 s = m->scale;
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TypesCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_TypesPopupClose, 0, 0, outside.pressOrder);
    u32 *rows = ArenaPushArray(frame, Max(state->typeCount, 1u), u32);
    u32 count = Prog_TypeMatches(state, rows), toggled = 0;
    for (u32 t = 0; t < state->typeCount; ++t) toggled += Prog_TypeShown(&state->types, state->typeNames[t]) && state->types.length;
    if (state->typesHighlight >= count) state->typesHighlight = count ? count - 1 : 0;
    f32 width = floorf(270.4f * s + 0.5f), pad = floorf(6.4f * s + 0.5f), fieldHeight = floorf(32.0f * s + 0.5f), pitch = floorf(25.6f * s + 0.5f);
    f32 line = MaxF32(1, floorf(0.8f * s + 0.5f)), radius = floorf(8.0f * s + 0.5f), button = floorf(28.0f * s + 0.5f);
    u32 visible = Min(Max(count, 1u), 12u);
    f32 above = floorf(3.2f * s + 0.5f), listHeight = (f32)visible * pitch, height = fieldHeight + line + listHeight + above + pad;
    // Under the Type column's header; with no header in sight, under the top of the window.
    R_Rect anchor = state->typesAnchor;
    f32 x = anchor.x1 > anchor.x0 ? anchor.x0 : floorf((view.x0 + view.x1 - width) * 0.5f), y = anchor.y1 > anchor.y0 ? anchor.y1 + floorf(3.2f * s + 0.5f) : view.y0 + m->bar * 2;
    x = MaxF32(view.x0, MinF32(x, view.x1 - width - pad)), y = MinF32(y, view.y1 - height - pad);
    Prog__DropShadow(x, y, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, y - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill(), .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TypesPopup")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_MouseClickable);
    UI_Parent(box) {
        UI_Block *field = Prog__Box(UI_Flex(), UI_Pixels(fieldHeight), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(field) {
            Prog__Spacer(UI_Pixels(floorf(8.0f * s + 0.5f)), UI_Pixels(fieldHeight));
            Prog__SearchIcon(floorf(24.0f * s + 0.5f), fieldHeight, theme->textDim, 0, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(fieldHeight));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                f32 top = floorf((fieldHeight - m->line) * 0.5f);
                if (!state->typesSearch.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, top });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(StrFormat(frame, TrF("Filter %u types..."), state->typeCount));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->typesSearch, 0, top, true, m);
            }
            // Checking every type shows them all as a filter would, and clearing them shows them all without one.
            UI_Interaction all = Prog__TextButton(S("TypesToggleAll"), Icon_SquareCheck, m->icons, UI_Pixels(button), fieldHeight, 0, theme->icon, theme->text, theme->iconDisabled,
                                                  toggled < state->typeCount, m);
            if (all.clicked) Prog_QueueCommand(state, Prog_CommandKind_TypesToggleAll, 0, 0, all.clickOrder);
            Prog__NoteTooltip(state, Prog_CommandKind_TypesToggleAll, Tr("Toggle All"));
            UI_Interaction none = Prog__TextButton(S("TypesUntoggleAll"), Icon_SquareX, m->icons, UI_Pixels(button), fieldHeight, 0, theme->icon, theme->text, theme->iconDisabled,
                                                   toggled > 0, m);
            if (none.clicked) Prog_QueueCommand(state, Prog_CommandKind_TypesUntoggleAll, 0, 0, none.clickOrder);
            Prog__NoteTooltip(state, Prog_CommandKind_TypesUntoggleAll, Tr("Untoggle All"));
            Prog__Spacer(UI_Pixels(floorf(4.0f * s + 0.5f)), UI_Pixels(fieldHeight));
            Prog__Box(UI_Pixels(line), UI_Pixels(floorf(m->line * 1.15f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
            Prog__Spacer(UI_Pixels(floorf(4.0f * s + 0.5f)), UI_Pixels(fieldHeight));
            UI_Interaction close = Prog__CloseButton(S("TypesClose"), m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_TypesPopupClose, 0, 0, close.clickOrder);
            Prog__Spacer(UI_Pixels(floorf(4.0f * s + 0.5f)), UI_Pixels(fieldHeight));
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextChildrenPadding((UI_Padding){ pad, above, pad, pad });
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(listHeight + above + pad));
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TypesList")), UI_Flag_Clip | (count > visible ? UI_Flag_Scrollable : 0));
        String typed = Prog_EditText(&state->typesSearch);
        f32 box16 = floorf(14.4f * s + 0.5f), iconSize = (f32)state->iconSize;
        UI_Parent(list) for (u32 i = 0; i < count; ++i) {
            u32 t = rows[i];
            UI_Key rowKey = UI_KeyFromStr(0, StrFormat(frame, "Types/%S", state->typeNames[t])), checkKey = UI_KeyFromStr(rowKey, S("Check"));
            if ((UI->hotKey == rowKey || UI->hotKey == checkKey) && UI__Contains(list->rect, UI->mouse)) state->typesHighlight = i;
            b32 lit = i == state->typesHighlight, on = state->types.length && Prog_TypeShown(&state->types, state->typeNames[t]);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(pitch));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ floorf(6.4f * s + 0.5f), 0, floorf(6.4f * s + 0.5f), 0 });
            UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
            UI_SetNextColors((UI_Colors){ .background = theme->selection });
            UI_Block *row = UI_AddBlockFromKey(rowKey, UI_Flag_LeftMouseClickable | (lit ? UI_Flag_DrawBackground : 0));
            UI_Interaction pick = UI_InteractionFromBlock(row);
            if (pick.clicked) Prog_QueueCommand(state, Prog_CommandKind_TypesToggle, t, 0, pick.clickOrder)->target = 1;
            UI_Parent(row) {
                const Prog_Icon *icon = state->typeFirsts[t] < state->directory.count ? Prog_FileIcon(state, state->directory.path, &state->directory.entries[state->typeFirsts[t]]) : NULL;
                UI_SetNextPrefWidth(UI_Pixels(iconSize + floorf(8.0f * s + 0.5f)));
                UI_SetNextPrefHeight(UI_Pixels(pitch));
                UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
                if (icon) UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ iconSize, iconSize }, (R_Color){ 1, 1, 1, 1 });
                UI_AddBlock(icon ? UI_Flag_DrawImage : 0);
                u8 *marks = typed.length ? ArenaPushArray(frame, state->typeNames[t].length + 1, u8) : NULL;
                if (marks) Prog_FuzzyScore(state->typeNames[t], (String){0}, typed, marks, NULL);
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(pitch));
                UI_SetNextChildrenLayout(UI_Layout_Vertical);
                UI_SetNextChildrenPadding((UI_Padding){ 0, floorf((pitch - m->line) * 0.5f), 0, 0 });
                UI_Block *name = UI_AddBlock(UI_Flag_Clip);
                UI_Parent(name) Prog__MarkedText(state->typeNames[t], marks, m->font, m->line, theme->text);
                Prog__Label(StrFormat(frame, "%" PRIu64, state->typeCounts[t]), UI_TextContent(), pitch, 0, lit ? theme->text : theme->textDim, 1);
                Prog__Spacer(UI_Pixels(floorf(12.0f * s + 0.5f)), UI_Pixels(pitch));
                UI_SetNextPrefWidth(UI_Pixels(box16));
                UI_SetNextPrefHeight(UI_Pixels(box16));
                UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                UI_SetNextCornerRadius(floorf(3.2f * s + 0.5f));
                UI_SetNextBorderThickness(m->stroke);
                UI_SetNextFont(m->iconsSmall);
                UI_SetNextColors((UI_Colors){ .background = theme->selection, .border = lit ? theme->text : theme->tabBorder, .text = theme->text });
                UI_SetNextText(Icon_Check);
                UI_Block *check = UI_AddBlockFromKey(checkKey, UI_Flag_LeftMouseClickable | UI_Flag_DrawBorder | (on ? UI_Flag_DrawBackground | UI_Flag_DrawText : 0));
                UI_Interaction tick = UI_InteractionFromBlock(check);
                if (tick.clicked) Prog_QueueCommand(state, Prog_CommandKind_TypesToggle, t, 0, tick.clickOrder);
            }
        }
    }
}

// The folder dropdown under its crumb: a box that finds in the folder's listing over a row for each item it finds, as wide as the widest name
// needs, the folder the path goes on into marked with an arrow. A click on a row goes to its folder or opens its file. It reaches down to the
// window's bottom at most, and only the rows in sight are built.
function void Prog__FilesPopup(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->filesOpen) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    f32 s = m->scale;
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilesCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_FilesPopupClose, 0, 0, outside.pressOrder);
    u32 *rows = ArenaPushArray(frame, Max(state->filesCount, (u64)1), u32);
    u32 count = Prog_FilesMatches(state, rows);
    if (state->filesHighlight >= count) state->filesHighlight = count ? count - 1 : 0;
    if (state->filesListed && state->filesWidest <= 0) {
        state->filesWidest = 1;
        for (u64 i = 0; i < state->filesCount; ++i) state->filesWidest = MaxF32(state->filesWidest, R_TextWidth(m->font, Prog_FilesItemName(&state->filesItems[i])));
    }
    f32 pad = floorf(6.4f * s + 0.5f), fieldHeight = floorf(32.0f * s + 0.5f), pitch = floorf(25.6f * s + 0.5f), line = MaxF32(1, floorf(1.6f * s + 0.5f));
    f32 radius = floorf(8.0f * s + 0.5f), above = floorf(2.4f * s + 0.5f), iconSize = (f32)state->iconSize;
    // Under its crumb; before the first layout has placed the crumb, under the top of the window.
    R_Rect anchor = state->filesAnchor;
    f32 x = anchor.x1 > anchor.x0 ? anchor.x0 : view.x0 + m->bar * 4, y = anchor.y1 > anchor.y0 ? anchor.y1 + floorf(2.4f * s + 0.5f) : view.y0 + m->bar * 2;
    f32 width = MinF32(MaxF32(floorf(188.0f * s + 0.5f), state->filesWidest + floorf(69.6f * s + 0.5f)), view.x1 - view.x0 - 2 * pad);
    x = MaxF32(view.x0, MinF32(x, view.x1 - width - pad));
    u32 fits = (u32)MaxF32(1, floorf((view.y1 - pad - y - fieldHeight - line - above - pad) / pitch)), visible = Min(Max(count, 1u), fits);
    f32 listHeight = (f32)visible * pitch, height = fieldHeight + line + above + listHeight + pad;
    Prog__DropShadow(x, y, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, y - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill(), .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilesPopup")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_MouseClickable);
    UI_Parent(box) {
        UI_Block *field = Prog__Box(UI_Flex(), UI_Pixels(fieldHeight), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(field) {
            Prog__Spacer(UI_Pixels(floorf(8.0f * s + 0.5f)), UI_Pixels(fieldHeight));
            Prog__SearchIcon(floorf(24.0f * s + 0.5f), fieldHeight, theme->textDim, 0, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(fieldHeight));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                f32 top = floorf((fieldHeight - m->line) * 0.5f);
                if (!state->filesSearch.length) {
                    u64 n = state->filesCount;
                    String many = n >= 1000000 ? StrFormat(frame, "%.1fM", (f64)n / 1e6) : n >= 1000 ? StrFormat(frame, "%.1fK", (f64)n / 1e3) : StrFormat(frame, "%" PRIu64, n);
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, top });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(StrFormat(frame, TrF("Filter %S files..."), many));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->filesSearch, 0, top, true, m);
            }
            UI_Interaction close = Prog__CloseButton(S("FilesClose"), m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_FilesPopupClose, 0, 0, close.clickOrder);
            Prog__Spacer(UI_Pixels(floorf(4.0f * s + 0.5f)), UI_Pixels(fieldHeight));
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextChildrenPadding((UI_Padding){ pad, above, pad, pad });
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(listHeight + above + pad));
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        b32 scrolls = count > visible;
        UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilesList")), UI_Flag_Clip | (scrolls ? UI_Flag_Scrollable : 0));
        // The highlight the keyboard moved scrolls into sight.
        if (state->filesReveal && list->rect.y1 > list->rect.y0) {
            f32 top = (f32)state->filesHighlight * pitch;
            if (top < list->scrollOffset.y) list->scrollOffset.y = top;
            if (top + pitch > list->scrollOffset.y + listHeight) list->scrollOffset.y = top + pitch - listHeight;
            state->filesReveal = false;
        }
        // The pointer lights the row it moves onto; a still pointer leaves the keyboard's highlight alone.
        b32 moved = UI->mouse.x != state->filesMouse.x || UI->mouse.y != state->filesMouse.y;
        state->filesMouse = UI->mouse;
        String typed = Prog_EditText(&state->filesSearch);
        f32 rowWidth = width - 2 * pad - (scrolls ? Prog__ScrollGutter(m) : 0), shown = list->scrollShown.y;
        u32 first = (u32)MinF32((f32)count, MaxF32(0, floorf(shown / pitch))), last = Min(count, first + visible + 1);
        UI_Parent(list) {
            for (u32 i = first; i < last; ++i) {
                const Prog_Entry *entry = &state->filesItems[rows[i]];
                f32 rowY = (f32)i * pitch - shown;
                // Rows are parted by a line, and lit between the lines.
                if (i) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, rowY });
                    UI_SetNextPrefWidth(UI_Pixels(rowWidth));
                    UI_SetNextPrefHeight(UI_Pixels(line));
                    UI_SetNextColors((UI_Colors){ .background = theme->separator });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                }
                UI_Key rowKey = UI_KeyFromStr(0, StrFormat(frame, "Files/%S", entry->file.name));
                if (UI->hotKey == rowKey && moved && UI__Contains(list->rect, UI->mouse)) {
                    state->filesHighlight = i;
                    state->filesLit = true;
                }
                b32 lit = state->filesLit && i == state->filesHighlight;
                UI_SetNextFloatingOffset((UI_Vec2){ 0, rowY + line });
                UI_SetNextPrefWidth(UI_Pixels(rowWidth));
                UI_SetNextPrefHeight(UI_Pixels(pitch - line));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextChildrenPadding((UI_Padding){ floorf(6.4f * s + 0.5f), 0, floorf(10.4f * s + 0.5f), 0 });
                UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = theme->selection });
                UI_Block *row = UI_AddBlockFromKey(rowKey, UI_Flag_Floating | UI_Flag_LeftMouseClickable | (lit ? UI_Flag_DrawBackground : 0));
                UI_Interaction pick = UI_InteractionFromBlock(row);
                if (pick.clicked) Prog_QueueCommand(state, Prog_CommandKind_FilesPopupActivate, rows[i], 0, pick.clickOrder);
                UI_Parent(row) {
                    const Prog_Icon *icon = Prog_FileIcon(state, state->filesFolder, entry);
                    UI_SetNextPrefWidth(UI_Pixels(iconSize + floorf(8.0f * s + 0.5f)));
                    UI_SetNextPrefHeight(UI_Pixels(pitch - line));
                    if (icon) UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ iconSize, iconSize }, (R_Color){ 1, 1, 1, 1 });
                    UI_AddBlock(icon ? UI_Flag_DrawImage : 0);
                    String shownName = Prog_FilesItemName(entry);
                    u8 *marks = typed.length ? ArenaPushArray(frame, shownName.length + 1, u8) : NULL;
                    if (marks) Prog_FuzzyScore(shownName, (String){0}, typed, marks, NULL);
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(pitch - line));
                    UI_SetNextChildrenLayout(UI_Layout_Vertical);
                    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf((pitch - line - m->line) * 0.5f), 0, 0 });
                    UI_Block *name = UI_AddBlock(UI_Flag_Clip);
                    UI_Parent(name) Prog__MarkedText(shownName, marks, m->font, m->line, theme->text);
                    if (state->filesChild.length && StrIsEqual(entry->file.name, state->filesChild)) Prog__Icon(Icon_ArrowLeft, 0, pitch - line, theme->text, 0.5f, m);
                }
            }
            // The rows float, so the list scrolls by a stand-in as tall as they are.
            Prog__Spacer(UI_Pixels(rowWidth), UI_Pixels((f32)count * pitch));
        }
        if (scrolls) Prog__ScrollBar(list, pitch, S("FilesList"), 1, m);
    }
}

function void Prog__ZoomPopup(Prog_State *state, const Prog_Metrics *m)
{
    R_Rect anchor = state->zoomAnchor;
    UI_Key sliderKey = UI_KeyFromStr(0, S("ZoomPopupSlider"));
    // Up while the pointer is over the view button or the popup, while the slider is dragged, and for a moment after each Ctrl+wheel step, Ctrl
    // held or not; then it fades out.
    b32 over = UI__Contains(anchor, UI->mouse) || (state->zoomShown && UI__Contains(state->zoomBox, UI->mouse));
    u64 sinceWheel = UI->nowMicroseconds - Min(state->zoomWheelTime, UI->nowMicroseconds);
    b32 wheeled = state->zoomWheelTime && sinceWheel < 450000;
    // A timer rather than an animation, so it runs with animations off too.
    if (wheeled) UI->animating = true;
    // A drag passing over it, of a tab or of files, does not open it.
    b32 dragging = state->tabDrag || state->dragging || state->foreignDrag;
    b32 wanted = anchor.x1 > anchor.x0 && !state->paletteOpen && !state->menuOpen && (wheeled || (over && !dragging) || UI->activeKey == sliderKey);
    f32 shown = UI_Animate(UI_KeyFromStr(0, S("ZoomPopupFade")), wanted ? 1.0f : 0.0f, 0);
    state->zoomShown = wanted || shown > 0.01f;
    if (!state->zoomShown) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale;
    f32 header = floorf(28.0f * s + 0.5f);
    f32 width = floorf(166.4f * s + 0.5f), height = floorf(237.6f * s + 0.5f) + header, radius = floorf(8.0f * s + 0.5f);
    f32 x = anchor.x1 - width, y = anchor.y0 - floorf(1.6f * s + 0.5f) - height;
    // The gap down to the button counts as the popup, so that the pointer can cross it.
    state->zoomBox = (R_Rect){ x, y, x + width, anchor.y0 };
    // Where a zoom's row or thumb sits from the box's top: a row's pitch apart up to M Icons at 30%, then evenly up to XL Icons at 100%.
    f32 pitch = floorf(20.0f * s + 0.5f), bottomRow = height - floorf(21.6f * s + 0.5f), topRow = header + floorf(20.0f * s + 0.5f);
    f32 iconsRow = bottomRow - 3 * pitch;
    #define Prog__ZoomY(fraction) ((fraction) <= 0.3f ? bottomRow - (fraction) * 10 * pitch : iconsRow - ((fraction) - 0.3f) / 0.7f * (iconsRow - topRow))
    f32 opacity = wanted ? MaxF32(shown, 0.3f) : shown;
    Prog__DropShadow(x, y, width, height, radius, opacity, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextOpacity(opacity);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill(), .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ZoomPopup")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    Prog_View view = Prog_ZoomView(state->zoom);
    u32 lit = view != Prog_View_Icons ? (u32)view : state->zoom < Prog_ZoomLarge ? 3u : state->zoom < Prog_ZoomMax ? 4u : 5u;
    local_persist const String names[] = { TrInit("Details"), TrInit("List"), TrInit("Columns"), TrInit("M Icons"), TrInit("L Icons"), TrInit("XL Icons") };
    const String icons[] = { Icon_List, Icon_Menu, Icon_Columns3, Icon_Image, Icon_Image, Icon_Image };
    f32 places[] = { 0, 0.1f, 0.2f, 0.3f, 0.65f, 1 };
    u32 levels[] = { 0, Prog_ZoomList, Prog_ZoomColumns, Prog_ZoomIcons, Prog_ZoomLarge, Prog_ZoomMax };
    f32 inset = floorf(11.2f * s + 0.5f), rowWidth = floorf(120.0f * s + 0.5f), rowHeight = floorf(17.6f * s + 0.5f);
    UI_Parent(box) {
        // The header: what the slider sets, and where it is.
        UI_SetNextFloatingOffset((UI_Vec2){ inset, 0 });
        UI_SetNextPrefWidth(UI_Pixels(width - 2 * inset));
        UI_SetNextPrefHeight(UI_Pixels(header));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_Block *heading = UI_AddBlock(UI_Flag_Floating);
        UI_Parent(heading) {
            Prog__Label(Tr("Zoom"), UI_Flex(), header, 0, theme->textDim, 0);
            Prog__Label(StrFormat(&state->frameArena, "%u%%", state->zoom / 10), UI_TextContent(), header, 0, theme->text, 0);
        }
        UI_SetNextFloatingOffset((UI_Vec2){ 0, header });
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(m->separator));
        UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        for (u32 i = 0; i < CArrayCount(names); ++i) {
            f32 center = floorf(Prog__ZoomY(places[i]) + 0.5f);
            UI_SetNextPrefWidth(UI_Pixels(rowWidth));
            UI_SetNextPrefHeight(UI_Pixels(rowHeight));
            UI_SetNextFloatingOffset((UI_Vec2){ inset, center - floorf(rowHeight * 0.5f) });
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextCornerRadius(floorf(3.2f * s + 0.5f));
            // A row picks its view, and is washed under the pointer.
            UI_Key rowKey = UI_KeyFromStr(0, names[i]);
            R_Color fill = theme->selection;
            if (i != lit) fill.a *= theme->hoverAmount.r;
            UI_SetNextColors((UI_Colors){ .background = fill });
            UI_Block *row = UI_AddBlockFromKey(UI_KeyFromStr(rowKey, S("ZoomPopupRow")),
                                               UI_Flag_Floating | UI_Flag_LeftMouseClickable | (i == lit || UI->hotKey == UI_KeyFromStr(rowKey, S("ZoomPopupRow")) ? UI_Flag_DrawBackground : 0));
            UI_Interaction pick = UI_InteractionFromBlock(row);
            if (pick.clicked) Prog_QueueCommand(state, Prog_CommandKind_ZoomSet, levels[i], 0, pick.clickOrder);
            UI_Parent(row) {
                Prog__Icon(icons[i], floorf(30.4f * s + 0.5f), rowHeight, i == lit ? theme->text : theme->icon, 0.6f, m);
                Prog__Label(Prog_Tr(names[i]), UI_Flex(), rowHeight, 0, theme->text, 0);
            }
            // A line under every row but the last.
            if (i > 0) {
                UI_SetNextPrefWidth(UI_Pixels(rowWidth));
                UI_SetNextPrefHeight(UI_Pixels(m->separator));
                UI_SetNextFloatingOffset((UI_Vec2){ inset, center + floorf(rowHeight * 0.5f) + floorf(0.8f * s) });
                UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        }
        // The slider: a slim track from XL Icons down to Details, filled below a small knob.
        f32 track = MaxF32(2, floorf(3.2f * s + 0.5f)), knob = floorf(11.2f * s + 0.5f), grab = floorf(20.0f * s + 0.5f);
        f32 trackX = width - floorf(14.4f * s + 0.5f) - track, trackTop = topRow + floorf(1.6f * s + 0.5f), trackBottom = bottomRow - floorf(0.8f * s + 0.5f);
        // The knob eases to a new zoom; it starts where the zoom is when the popup shows.
        f32 target = Prog__ZoomY(Prog__ZoomFraction(state->zoom));
        f32 thumb = floorf(UI_Animate(UI_KeyFromStr(0, S("ZoomPopupThumb")), target, target) + 0.5f);
        // Pressing or dragging along the slider, wider than the track, sets the icon size it points at, or the nearest view below M Icons.
        UI_SetNextPrefWidth(UI_Pixels(grab));
        UI_SetNextPrefHeight(UI_Pixels(trackBottom - trackTop + knob));
        UI_SetNextFloatingOffset((UI_Vec2){ trackX + floorf((track - grab) * 0.5f), trackTop - floorf(knob * 0.5f) });
        UI_Block *slider = UI_AddBlockFromKey(sliderKey, UI_Flag_Floating | UI_Flag_LeftMouseClickable | UI_Flag_TakesWheel);
        UI_Interaction drag = UI_InteractionFromBlock(slider);
        // The wheel over it zooms, as Ctrl+wheel does anywhere.
        i32 notches = Prog__WheelNotches(slider);
        if (notches) state->zoomWheelTime = UI->nowMicroseconds;
        for (i32 i = 0; i < (notches < 0 ? -notches : notches); ++i) Prog_QueueCommand(state, notches > 0 ? Prog_CommandKind_ZoomIn : Prog_CommandKind_ZoomOut, 0, 0, 0);
        if (drag.pressed || (drag.active && drag.isDragging)) {
            f32 at = UI->mouse.y - box->rect.y0;
            u32 wanted = Prog_ZoomIcons;
            if (at <= iconsRow) wanted = (u32)MinF32((f32)Prog_ZoomMax, (f32)Prog_ZoomIcons + floorf((iconsRow - at) / MaxF32(1, iconsRow - topRow) * 700.0f + 0.5f));
            else wanted = (u32)MaxF32(0, (f32)Prog_ZoomIcons - floorf((at - iconsRow) / pitch + 0.5f) * Prog_ZoomList);
            if (wanted != state->zoom) Prog_QueueCommand(state, Prog_CommandKind_ZoomSet, wanted, 0, drag.pressOrder);
        }
        UI_SetNextPrefWidth(UI_Pixels(track));
        UI_SetNextPrefHeight(UI_Pixels(trackBottom - trackTop));
        UI_SetNextFloatingOffset((UI_Vec2){ trackX, trackTop });
        UI_SetNextCornerRadius(track * 0.5f);
        UI_SetNextColors((UI_Colors){ .background = theme->track });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        if (trackBottom > thumb) {
            UI_SetNextPrefWidth(UI_Pixels(track));
            UI_SetNextPrefHeight(UI_Pixels(trackBottom - thumb));
            UI_SetNextFloatingOffset((UI_Vec2){ trackX, thumb });
            UI_SetNextCornerRadius(track * 0.5f);
            UI_SetNextColors((UI_Colors){ .background = theme->accent });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
        UI_SetNextPrefWidth(UI_Pixels(knob));
        UI_SetNextPrefHeight(UI_Pixels(knob));
        UI_SetNextFloatingOffset((UI_Vec2){ trackX + floorf((track - knob) * 0.5f), thumb - floorf(knob * 0.5f) });
        UI_SetNextCornerRadius(knob * 0.5f);
        UI_SetNextColors((UI_Colors){ .background = theme->text });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
    #undef Prog__ZoomY
}

function void Prog__PaletteBody(Prog_State *state, const Prog_Metrics *m);

// Load Layout: the saved layouts by name in the palette's box, which the typed text narrows, with a button on the highlighted one that
// deletes it.
function void Prog__LayoutsPicker(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PaletteCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, outside.pressOrder);
    u32 matches[Prog_MaxLayouts];
    u32 count = Prog_LayoutMatches(state, matches);
    state->paletteHighlight = count ? Min(state->paletteHighlight, count - 1) : 0;
    f32 s = m->scale, line = m->separator;
    f32 field = floorf(39.2f * s + 0.5f), footer = floorf(31.2f * s + 0.5f), pad = floorf(4.0f * s + 0.5f), row = floorf(24.0f * s + 0.5f) + line;
    f32 top = 2 * m->bar + floorf(11.2f * s + 0.5f), margin = floorf(32.8f * s + 0.5f);
    f32 width = floorf(Prog__PaletteWidth(view.x1 - view.x0, margin, s));
    f32 listHeight = (f32)Max(count, 1u) * row - line + 2 * pad;
    f32 height = field + line + listHeight + line + footer;
    f32 opacity = Prog__FadeIn(UI_KeyFromStr(0, S("PaletteReveal"))) * state->paletteFade;
    f32 radius = floorf(8.0f * s + 0.5f), x = floorf((view.x1 - view.x0 - width) * 0.5f);
    Prog__DropShadow(x, top, width, height, radius, opacity, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextOpacity(opacity);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill() });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Palette")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        UI_SetNextChildrenPadding((UI_Padding){ floorf(12.8f * s + 0.5f), 0, floorf(8.0f * s + 0.5f), 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, theme->inner, 0);
        box->props.cornerRadius = radius;
        box->props.shape = R_Shape_SquareBottomLeft | R_Shape_SquareBottomRight;
        UI_Parent(box) {
            Prog__SearchIcon(floorf(22.4f * s + 0.5f), field, theme->textDim, 0, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(Tr("Select a layout..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.5f), true, m);
            }
            UI_Interaction close = Prog__CloseButton(S("PaletteCloseButton"), m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, close.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        f32 side = floorf(6.4f * s + 0.5f);
        UI_SetNextChildrenPadding((UI_Padding){ side, pad, side, pad });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(list) {
            if (!count) {
                Prog__Label(state->layoutNames.header.count ? Tr("No layout has that name") : Tr("No layouts saved yet; Save Layout keeps one"), UI_Flex(), row - line,
                            floorf(8.0f * s + 0.5f), theme->textDim, 0);
            }
            for (u32 i = 0; i < count; ++i) {
                String name = state->layoutNames.v[matches[i]];
                UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Layout/%S", name));
                b32 highlighted = i == state->paletteHighlight, hot = UI->hotKey == key && !highlighted;
                f32 face = row - line;
                R_Color washed = theme->selection;
                washed.a *= theme->hoverAmount.r;
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(face));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextChildrenPadding((UI_Padding){ floorf(8.0f * s + 0.5f), 0, floorf(10.4f * s + 0.5f), 0 });
                UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
                UI_SetNextBorderThickness(m->stroke);
                UI_SetNextColors((UI_Colors){ .background = highlighted ? theme->selection : washed, .border = theme->selection });
                UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_Clip | (highlighted || hot ? UI_Flag_DrawBackground : 0) | (hot ? UI_Flag_DrawBorder : 0));
                UI_Interaction interaction = UI_InteractionFromBlock(entry);
                if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, i, 0, interaction.clickOrder);
                UI_Parent(entry) {
                    u8 *marks = state->palette.length ? ArenaPushArray(&state->frameArena, name.length + 1, u8) : NULL;
                    if (marks) Prog_FilterMatch(name, Prog_EditText(&state->palette), marks);
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(face));
                    UI_SetNextChildrenLayout(UI_Layout_Vertical);
                    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf((face - m->line) * 0.5f), 0, 0 });
                    UI_Block *texts = UI_AddBlock(UI_Flag_Clip);
                    UI_Parent(texts) Prog__MarkedText(name, marks, m->font, m->line, theme->text);
                    if (highlighted || hot) {
                        UI_Interaction drop = Prog__TextButton(StrFormat(&state->frameArena, "LayoutDelete/%S", name), Icon_X, m->icons, UI_Pixels(floorf(24.0f * s + 0.5f)), face, 0,
                                                               theme->text, theme->text, theme->iconDisabled, true, m);
                        if (drop.clicked) Prog_QueueCommand(state, Prog_CommandKind_LayoutDelete, i, 0, drop.clickOrder);
                        Prog__PopupTooltip(state, Tr("Delete layout."));
                    }
                }
                if (i + 1 < count) Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
            }
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        local_persist const char *const keys[] = { "Up", "Down", 0, "Enter", 0, "Escape", 0 };
        local_persist const String words[] = { {0}, {0}, TrInit("to navigate"), {0}, TrInit("to use"), {0}, TrInit("to dismiss") };
        Prog__PaletteHints(keys, words, CArrayCount(keys), footer, theme->background.a < 1 ? (R_Color){0} : theme->menu, radius, m);
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextOpacity(opacity);
    UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
}

function void Prog__ReleaseFontPreviews(Prog_State *state)
{
    for (u32 i = 0; i < CArrayCount(state->fontPreviews); ++i) {
        if (!state->fontPreviews[i].family) continue;
        R_FontRelease(&state->fontPreviews[i].font);
        ArenaRelease(&state->fontPreviews[i].arena);
        state->fontPreviews[i] = (typeof(state->fontPreviews[i])){0};
    }
}

// Family `family` at `pixels`, or NULL while it is not made yet or cannot be. Opening a font takes a moment, so a frame makes at most
// `*budget` of them and asks for another frame for the rest; a slot drawn this frame is never let go in it.
function const R_Font *Prog__FontPreview(Prog_State *state, u64 family, f32 pixels, u32 *budget)
{
    u32 free = UINT32_MAX;
    for (u32 i = 0; i < CArrayCount(state->fontPreviews); ++i) {
        typeof(state->fontPreviews[0]) *slot = &state->fontPreviews[i];
        if (slot->family == family + 1 && slot->pixels == pixels) {
            slot->frame = UI->frameIndex;
            return slot->made ? &slot->font : NULL;
        }
        if (slot->frame == UI->frameIndex && slot->family) continue;
        if (free == UINT32_MAX || !slot->family || (state->fontPreviews[free].family && slot->frame < state->fontPreviews[free].frame)) free = i;
    }
    if (free == UINT32_MAX) return NULL;
    if (!*budget) {
        UI->animating = true;
        return NULL;
    }
    --*budget;
    typeof(state->fontPreviews[0]) *slot = &state->fontPreviews[free];
    R_FontRelease(&slot->font);
    ArenaRelease(&slot->arena);
    *slot = (typeof(*slot)){ .family = family + 1, .frame = UI->frameIndex, .pixels = pixels };
    slot->made = R_FontCreateLazy(&slot->arena, state->fontNames[family], pixels, &slot->font);
    return slot->made ? &slot->font : NULL;
}

// The font dropdown under the Font or Inspector Font option it opened from, over Options: a search row, which takes what is typed, over the
// families, each named in itself, with the one the option has checked. It opens upward where the window has no room below.
function void Prog__FontsDropdown(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    Arena *frame = &state->frameArena;
    // A press anywhere else closes it, back to Options.
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FontsCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, outside.pressOrder);
    u32 *matches = ArenaPushArray(frame, state->fontCount + 1, u32);
    u32 count = Prog_FontMatches(state, matches);
    state->paletteHighlight = count ? Min(state->paletteHighlight, count - 1) : 0;
    f32 s = m->scale, line = m->separator, pad = floorf(6.0f * s + 0.5f), inset = floorf(6.4f * s + 0.5f);
    f32 field = floorf(28.0f * s + 0.5f), row = floorf(24.0f * s + 0.5f) + line, radius = floorf(6.4f * s + 0.5f), gap = floorf(0.8f * s + 0.5f);
    // Under its button, or where it was last seen while it is out of sight, as for a frame where its row is not built.
    UI_Block *button = UI__Find(UI_KeyFromStr(0, StrFormat(frame, "OptionFont/%u", (u32)state->fontOption)));
    if (button && button->rect.x1 > button->rect.x0) state->fontAnchor = button->rect;
    R_Rect anchor = state->fontAnchor.x1 > state->fontAnchor.x0 ? state->fontAnchor : (R_Rect){ view.x0 + (view.x1 - view.x0) * 0.5f, view.y0, view.x0 + (view.x1 - view.x0) * 0.5f, view.y0 };
    f32 width = MaxF32(anchor.x1 - anchor.x0, floorf(280.0f * s + 0.5f));
    f32 x = UI__Clamp(anchor.x0 - view.x0, 0, MaxF32(0, view.x1 - view.x0 - width));
    // Up to twelve families at once, below the button or above it, whichever has room for more.
    f32 below = view.y1 - anchor.y1 - gap, above = anchor.y0 - view.y0 - gap, chrome = field + line + 2 * pad;
    b32 up = below < chrome + 6 * row && above > below;
    u32 visible = (u32)UI__Clamp(floorf(((up ? above : below) - chrome + line) / row), 1, 12);
    if (!state->paletteWheeled && state->paletteHighlight < state->paletteScroll) state->paletteScroll = state->paletteHighlight;
    if (!state->paletteWheeled && state->paletteHighlight >= state->paletteScroll + visible) state->paletteScroll = state->paletteHighlight - visible + 1;
    state->paletteScroll = count > visible ? Min(state->paletteScroll, count - visible) : 0;
    u32 first = state->paletteScroll, shown = Min(count - Min(first, count), visible);
    f32 listHeight = (f32)Max(shown, 1u) * row - line + 2 * pad, height = field + line + listHeight;
    f32 y = up ? anchor.y0 - view.y0 - gap - height : anchor.y1 - view.y0 + gap;
    // The interface's own size: 9 points.
    f32 previewPixels = floorf(12.0f * s + 0.5f);
    u32 budget = 4;
    state->fontPreviewsShown = true;
    Prog__DropShadow(x, y, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(line);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Fonts")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    String current = Prog__FontChosen(state, (Prog_Option)state->fontOption);
    UI_Parent(panel) {
        UI_SetNextChildrenPadding((UI_Padding){ floorf(10.4f * s + 0.5f), 0, inset, 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(box) {
            Prog__SearchIcon(floorf(20.8f * s + 0.5f), field, theme->textDim, 0, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                f32 textTop = floorf((field - m->line) * 0.5f + 0.5f);
                if (!state->palette.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, textTop });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(Tr("Search fonts..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, textTop, true, m);
            }
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        // Room on the right for the scroll bar while there is one.
        b32 scrolls = count > visible;
        f32 bar = floorf(4.8f * s + 0.5f), gutter = scrolls ? floorf(12.8f * s + 0.5f) : 0;
        UI_SetNextChildrenPadding((UI_Padding){ inset, pad, inset + gutter, pad });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(list) {
            if (!count) Prog__Label(state->fontsJob ? Tr("Listing the installed fonts...") : Tr("No font has that name"), UI_Flex(), row - line, floorf(8.0f * s + 0.5f), theme->textDim, 0);
            for (u32 i = first; i < first + shown; ++i) {
                String name = state->fontNames[matches[i]];
                b32 chosen = Prog__EqualFolded(name, current);
                UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Font/%S", name));
                b32 highlighted = i == state->paletteHighlight, hot = UI->hotKey == key && !highlighted;
                f32 face = row - line;
                R_Color washed = theme->selection;
                washed.a *= theme->hoverAmount.r;
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(face));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextChildrenPadding((UI_Padding){ floorf(8.0f * s + 0.5f), 0, floorf(4.0f * s + 0.5f), 0 });
                UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = highlighted ? theme->selection : washed });
                UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_Clip | (highlighted || hot ? UI_Flag_DrawBackground : 0));
                UI_Interaction interaction = UI_InteractionFromBlock(entry);
                if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, i, 0, interaction.clickOrder);
                UI_Parent(entry) {
                    u8 *marks = state->palette.length ? ArenaPushArray(frame, name.length + 1, u8) : NULL;
                    if (marks) Prog_FilterMatch(name, Prog_EditText(&state->palette), marks);
                    // Each family's name in that family once it is made, and in the interface's until then.
                    const R_Font *preview = Prog__FontPreview(state, matches[i], previewPixels, &budget);
                    const R_Font *font = preview && preview->lineHeight > 0 ? preview : m->font;
                    // Centered by its letters, as some families have a far taller line than they need.
                    f32 lineHeight = font == m->font ? m->line : MinF32(ceilf(font->ascent + font->descent), face);
                    R_Color ink = highlighted ? theme->contentSelection : theme->text;
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(face));
                    UI_SetNextChildrenLayout(UI_Layout_Vertical);
                    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf((face - lineHeight) * 0.5f), 0, 0 });
                    UI_Block *texts = UI_AddBlock(UI_Flag_Clip);
                    UI_Parent(texts) Prog__MarkedText(name, marks, font, lineHeight, ink);
                    // The family the option has now.
                    if (chosen) Prog__Icon(Icon_Check, floorf(24.0f * s + 0.5f), face, ink, 0.5f, m);
                }
                if (i + 1 < first + shown) Prog__Spacer(UI_Flex(), UI_Pixels(line));
            }
        }
        // The scroll bar: a thumb as long as the share of families shown, where they are among all of them.
        if (scrolls) {
            f32 track = listHeight - 2 * pad, thumb = MaxF32(floorf(track * (f32)visible / (f32)count), row);
            f32 offset = floorf((track - thumb) * (f32)first / (f32)(count - visible));
            UI_SetNextFloatingOffset((UI_Vec2){ width - inset - floorf((gutter + bar) * 0.5f), field + line + pad + offset });
            UI_SetNextPrefWidth(UI_Pixels(bar));
            UI_SetNextPrefHeight(UI_Pixels(thumb));
            UI_SetNextCornerRadius(floorf(bar * 0.5f));
            UI_SetNextColors((UI_Colors){ .background = theme->track });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
    }
}

// The listed branches holding what is typed, as indices into them, in git's order.
function u32 Prog_BranchMatches(const Prog_State *state, u32 *found)
{
    String typed = Prog_EditText(&state->branchesSearch);
    u32 count = 0;
    for (u64 i = 0; i < state->branches.header.count; ++i) {
        if (!typed.length || Prog_FilterMatch(state->branches.v[i], typed, NULL)) found[count++] = (u32)i;
    }
    return count;
}

// The dropdown of git branches under the branch in a panel's path bar: a search row over the repository's local branches, with the one
// checked out checked.
function void Prog__BranchesDropdown(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->branchesOpen) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    Arena *frame = &state->frameArena;
    UI_Block *anchorBlock = UI__Find(state->branchesAnchor);
    if (!anchorBlock || anchorBlock->rect.x1 <= anchorBlock->rect.x0) return;
    R_Rect anchor = anchorBlock->rect;
    // A press anywhere else closes it.
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Interaction outside = UI_InteractionFromBlock(UI_AddBlockFromKey(UI_KeyFromStr(0, S("BranchesCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel));
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_GitBranchesOpen, 0, 0, outside.pressOrder)->panel = state->branchesPanel;
    u32 *matches = ArenaPushArray(frame, state->branches.header.count + 1, u32);
    u32 count = Prog_BranchMatches(state, matches);
    state->branchesHighlight = count ? Min(state->branchesHighlight, count - 1) : 0;
    f32 s = m->scale, line = m->separator, pad = floorf(6.0f * s + 0.5f), inset = floorf(6.4f * s + 0.5f);
    f32 field = floorf(28.0f * s + 0.5f), row = floorf(24.0f * s + 0.5f) + line, radius = floorf(6.4f * s + 0.5f), gap = floorf(0.8f * s + 0.5f);
    f32 width = MaxF32(anchor.x1 - anchor.x0, floorf(240.0f * s + 0.5f));
    f32 x = UI__Clamp(anchor.x1 - view.x0 - width, 0, MaxF32(0, view.x1 - view.x0 - width));
    u32 visible = (u32)UI__Clamp(floorf((view.y1 - anchor.y1 - gap - field - line - 2 * pad + line) / row), 1, 12);
    if (state->branchesHighlight < state->branchesScroll) state->branchesScroll = state->branchesHighlight;
    if (state->branchesHighlight >= state->branchesScroll + visible) state->branchesScroll = state->branchesHighlight - visible + 1;
    state->branchesScroll = count > visible ? Min(state->branchesScroll, count - visible) : 0;
    u32 first = state->branchesScroll, shown = Min(count - Min(first, count), visible);
    f32 listHeight = (f32)Max(shown, 1u) * row - line + 2 * pad, height = field + line + listHeight, y = anchor.y1 - view.y0 + gap;
    Prog__DropShadow(x, y, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(line);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Branches")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        UI_SetNextChildrenPadding((UI_Padding){ floorf(10.4f * s + 0.5f), 0, inset, 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(box) {
            Prog__SearchIcon(floorf(20.8f * s + 0.5f), field, theme->textDim, 0, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                f32 textTop = floorf((field - m->line) * 0.5f + 0.5f);
                if (!state->branchesSearch.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, textTop });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(Tr("Switch branch..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->branchesSearch, 0, textTop, true, m);
            }
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextChildrenPadding((UI_Padding){ inset, pad, inset, pad });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(list) {
            if (!count) Prog__Label(state->branchesJob ? Tr("Listing the branches...") : Tr("No branch has that name"), UI_Flex(), row - line, floorf(8.0f * s + 0.5f), theme->textDim, 0);
            for (u32 i = first; i < first + shown; ++i) {
                String name = state->branches.v[matches[i]];
                b32 current = StrIsEqual(name, state->git.branch);
                UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Branch/%S", name));
                b32 highlighted = i == state->branchesHighlight, hot = UI->hotKey == key && !highlighted;
                f32 face = row - line;
                R_Color washed = theme->selection;
                washed.a *= theme->hoverAmount.r;
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(face));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextChildrenPadding((UI_Padding){ floorf(8.0f * s + 0.5f), 0, floorf(4.0f * s + 0.5f), 0 });
                UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = highlighted ? theme->selection : washed });
                UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_Clip | (highlighted || hot ? UI_Flag_DrawBackground : 0));
                UI_Interaction interaction = UI_InteractionFromBlock(entry);
                if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_GitBranchSwitch, matches[i], 0, interaction.clickOrder)->panel = state->branchesPanel;
                UI_Parent(entry) {
                    R_Color ink = highlighted ? theme->contentSelection : theme->text;
                    u8 *marks = state->branchesSearch.length ? ArenaPushArray(frame, name.length + 1, u8) : NULL;
                    if (marks) Prog_FilterMatch(name, Prog_EditText(&state->branchesSearch), marks);
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(face));
                    UI_SetNextChildrenLayout(UI_Layout_Vertical);
                    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf((face - m->line) * 0.5f), 0, 0 });
                    UI_Block *texts = UI_AddBlock(UI_Flag_Clip);
                    UI_Parent(texts) Prog__MarkedText(name, marks, m->font, m->line, ink);
                    // The branch checked out.
                    if (current) Prog__Icon(Icon_Check, floorf(24.0f * s + 0.5f), face, ink, 0.5f, m);
                }
                if (i + 1 < first + shown) Prog__Spacer(UI_Flex(), UI_Pixels(line));
            }
        }
    }
}

// The dropdown of the muted kinds of notice under their option: each by its name with a button that shows it again, then one that shows
// them all again.
function void Prog__MutedDropdown(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->mutedOpen) return;
    Arena *frame = &state->frameArena;
    UI_Block *button = UI__Find(UI_KeyFromStr(0, S("OptionMuted")));
    u64 count = state->mutedNotices.header.count;
    if (!button || button->rect.x1 <= button->rect.x0 || !count) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    // Taller than the choices' rows, so that an x's hover keeps clear of the line under it.
    f32 s = m->scale, row = floorf(30.4f * s + 0.5f), pad = floorf(6.0f * s + 0.5f), inset = floorf(6.4f * s + 0.5f), line = MaxF32(1, floorf(0.8f * s + 0.5f));
    f32 slot = floorf(24.8f * s + 0.5f), lead = floorf(14.4f * s + 0.5f) - inset, radius = floorf(6.4f * s + 0.5f);
    f32 width = floorf(240.0f * s + 0.5f);
    for (u64 i = 0; i < count; ++i) width = MaxF32(width, 2 * inset + lead + R_TextWidth(m->font, Prog_NoticeKindName(frame, state->mutedNotices.v[i])) + slot + inset);
    f32 height = 2 * pad + (f32)(count + 1) * row + line;
    f32 x = MinF32(button->rect.x0 - view.x0, view.x1 - view.x0 - width), y = button->rect.y1 - view.y0 + floorf(0.8f * s + 0.5f);
    if (y + height > view.y1 - view.y0) y = MaxF32(0, button->rect.y0 - view.y0 - floorf(0.8f * s + 0.5f) - height);
    // A press anywhere else closes it.
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Interaction outside = UI_InteractionFromBlock(UI_AddBlockFromKey(UI_KeyFromStr(0, S("MutedCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel));
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_MutedNoticesOpen, 0, 0, outside.pressOrder);
    Prog__DropShadow(x, y, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ inset, pad, inset, pad });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(line);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Muted")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        for (u64 i = 0; i <= count; ++i) {
            // A line before the last row, which shows them all again.
            if (i == count) Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
            // A kind is shown again by its x alone; the last row, which shows them all again, as a whole.
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Muted/%" PRIu64, i));
            b32 all = i == count, lit = all && UI->hotKey == key;
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(row));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ 0, 0, floorf(2.4f * s + 0.5f), 0 });
            UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
            UI_SetNextColors((UI_Colors){ .background = theme->selection });
            UI_Block *entry = UI_AddBlockFromKey(key, (all ? UI_Flag_LeftMouseClickable : 0) | (lit ? UI_Flag_DrawBackground : 0));
            UI_Interaction interaction = UI_InteractionFromBlock(entry);
            if (all && interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_NoticesUnmuteAll, 0, 0, interaction.clickOrder);
            UI_Parent(entry) {
                R_Color ink = lit ? theme->contentSelection : theme->text;
                Prog__Spacer(UI_Pixels(lead), UI_Pixels(row));
                Prog__Label(i == count ? Tr("Restore all") : Prog_NoticeKindName(frame, state->mutedNotices.v[i]), UI_Flex(), row, 0, ink, 0);
                if (i < count) {
                    UI_Interaction unmute = Prog__CloseButton(StrFormat(frame, "Muted/%" PRIu64 "/Unmute", i), m);
                    if (unmute.clicked) Prog_QueueCommand(state, Prog_CommandKind_NoticeUnmute, i, 0, unmute.clickOrder);
                    Prog__PopupTooltip(state, Tr("Remove from the muted ones, which shows it again."));
                }
            }
        }
    }
}

// The choice shown at `place` in an option's dropdown: the choices in their order but Character Key Action's, which lists Trigger Hotkey
// before Type In Filter, its default and first in Settings.conf's order.
function u32 Prog_ChoiceAt(Prog_Option option, u32 place)
{
    if (option == Prog_Option_CharacterKeyAction && place < 2) return place ^ 1;
    return place;
}

// The dropdown under the choice option it opened from: each choice's name, and a ring that the chosen one's is filled with a dot. For the
// schemes each has its background color in a circle before its name, the highlighted row offers to fork its scheme and, for a custom one,
// to delete it, and a line parts the custom schemes from the built-in ones.
function void Prog__SchemesDropdown(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->schemesOpen) return;
    Prog_Option option = (Prog_Option)(state->schemesOpen - 1);
    Arena *frame = &state->frameArena;
    UI_Block *button = UI__Find(UI_KeyFromStr(0, StrFormat(frame, "OptionChoice/%u", (u32)option)));
    if (!button || button->rect.x1 <= button->rect.x0) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    u32 count = 0;
    const String *names = Prog_OptionChoices(state, option, frame, &count);
    u32 shown = (u32)Prog_OptionValue(state, option);
    b32 schemes = option == Prog_Option_ColorScheme || option == Prog_Option_LightScheme || option == Prog_Option_DarkScheme;
    f32 s = m->scale, row = floorf(25.6f * s + 0.5f), pad = floorf(6.0f * s + 0.5f), inset = floorf(6.4f * s + 0.5f), circle = floorf(16.0f * s + 0.5f);
    f32 slot = floorf(24.8f * s + 0.5f), ring = floorf(1.6f * s + 0.5f), line = MaxF32(1, floorf(0.8f * s + 0.5f)), parting = floorf(1.6f * s + 0.5f);
    f32 lead = floorf((schemes ? 16.0f : 14.4f) * s + 0.5f) - inset, trail = floorf(10.0f * s + 0.5f) + circle + floorf(7.2f * s + 0.5f);
    b32 parted = schemes && count > Prog_BuiltInSchemes;
    // The schemes' is as wide as their names and buttons need; another a little wider than its button, or than its longest name.
    f32 width = floorf(205.6f * s + 0.5f);
    if (!schemes) {
        width = floorf(170.4f * s + 0.5f);
        for (u32 i = 0; i < count; ++i) width = MaxF32(width, 2 * inset + lead + R_TextWidth(m->font, Prog__OptionName(frame, option, names[i])) + trail);
    }
    f32 height = 2 * pad + (f32)count * row + (parted ? parting : 0), radius = floorf(6.4f * s + 0.5f);
    // Under the button, or over it where the window has no room below.
    f32 x = MinF32(button->rect.x0 - view.x0, view.x1 - view.x0 - width), y = button->rect.y1 - view.y0 + floorf(0.8f * s + 0.5f);
    if (y + height > view.y1 - view.y0) y = MaxF32(0, button->rect.y0 - view.y0 - floorf(0.8f * s + 0.5f) - height);
    // A press anywhere else closes it, going no further.
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Interaction outside = UI_InteractionFromBlock(UI_AddBlockFromKey(UI_KeyFromStr(0, S("SchemesCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel));
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_SchemesOpen, (u64)option, 0, outside.pressOrder);
    Prog__DropShadow(x, y, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ 0, pad, 0, pad });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(line);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Schemes")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        // The pointer's row is the highlighted one, else the keyboard's.
        u32 pointed = UINT32_MAX;
        for (u32 i = 0; i < count; ++i) {
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Scheme/%u", i));
            if (UI->hotKey == key || UI->hotKey == UI_KeyFromStr(0, StrFormat(frame, "Scheme/%u/Fork", i)) || UI->hotKey == UI_KeyFromStr(0, StrFormat(frame, "Scheme/%u/Delete", i))) pointed = i;
        }
        for (u32 place = 0; place < count; ++place) {
            u32 i = Prog_ChoiceAt(option, place);
            if (parted && i == Prog_BuiltInSchemes) {
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(parting));
                UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
                UI_AddBlock(UI_Flag_DrawBackground);
            }
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Scheme/%u", i));
            b32 lit = pointed == UINT32_MAX ? i == state->schemesHighlight : i == pointed, picked = i == shown;
            UI_Block *slotBox = Prog__Box(UI_Flex(), UI_Pixels(row), UI_Layout_Horizontal, (R_Color){0}, 0);
            UI_Parent(slotBox) {
                UI_SetNextFloatingOffset((UI_Vec2){ inset, 0 });
                UI_SetNextPrefWidth(UI_Pixels(width - 2 * inset));
                UI_SetNextPrefHeight(UI_Pixels(row));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
                UI_SetNextColors((UI_Colors){ .background = theme->selection });
                UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_Floating | UI_Flag_LeftMouseClickable | (lit ? UI_Flag_DrawBackground : 0));
                UI_Interaction interaction = UI_InteractionFromBlock(entry);
                if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_SchemesPick, i, 0, interaction.clickOrder);
                UI_Parent(entry) {
                    Prog__Spacer(UI_Pixels(lead), UI_Pixels(row));
                    if (schemes) {
                        u32 groups[Prog_ColorGroupCount];
                        Prog__UseScheme(state, names[i], groups);
                        UI_SetNextPrefWidth(UI_Pixels(circle));
                        UI_SetNextPrefHeight(UI_Pixels(circle));
                        UI_SetNextCornerRadius(circle * 0.5f);
                        UI_SetNextBorderThickness(line);
                        UI_SetNextColors((UI_Colors){ .background = Prog__HexColor(groups[Prog_Option_ColorBackground - Prog_Option_ColorClear]), .border = theme->outline });
                        UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                        Prog__Spacer(UI_Pixels(floorf(8.0f * s + 0.5f)), UI_Pixels(row));
                    }
                    R_Color ink = lit ? theme->contentSelection : theme->text;
                    String label = Prog__OptionName(frame, option, names[i]);
                    Prog__Label(schemes ? Prog__Ellipsize(frame, m->font, label, width - 2 * inset - 4 * slot) : label, UI_Flex(), row, 0, ink, 0);
                    if (lit && schemes) {
                        UI_Interaction fork = Prog__TextButton(StrFormat(frame, "Scheme/%u/Fork", i), Icon_Copy, m->icons, UI_Pixels(slot), row, 0, ink, theme->text, theme->iconDisabled, true, m);
                        if (fork.clicked) Prog_QueueCommand(state, Prog_CommandKind_SchemeFork, i, 0, fork.clickOrder);
                        Prog__PopupTooltip(state, Tr("Create an editable copy of this color scheme."));
                        if (i >= Prog_BuiltInSchemes) {
                            UI_Interaction drop = Prog__TextButton(StrFormat(frame, "Scheme/%u/Delete", i), Icon_X, m->icons, UI_Pixels(slot), row, 0, ink, theme->text, theme->iconDisabled, true, m);
                            if (drop.clicked) Prog_QueueCommand(state, Prog_CommandKind_SchemeDelete, i, 0, drop.clickOrder);
                            Prog__PopupTooltip(state, Tr("Delete color scheme."));
                        }
                    }
                    Prog__Spacer(UI_Pixels(floorf(10.0f * s + 0.5f)), UI_Pixels(row));
                    // The ring, filled with the selection color around a dot for the scheme shown; on the highlight it keeps only its ring.
                    UI_SetNextPrefWidth(UI_Pixels(circle));
                    UI_SetNextPrefHeight(UI_Pixels(circle));
                    UI_SetNextCornerRadius(circle * 0.5f);
                    UI_SetNextBorderThickness(ring);
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
                    UI_SetNextColors((UI_Colors){ .background = theme->selection, .border = lit ? theme->outlineSelection : theme->tabBorder });
                    UI_Block *radio = UI_AddBlock((picked && !lit ? UI_Flag_DrawBackground : UI_Flag_DrawBorder));
                    if (picked) UI_Parent(radio) {
                        f32 dot = floorf(5.6f * s + 0.5f);
                        UI_SetNextPrefWidth(UI_Pixels(dot));
                        UI_SetNextPrefHeight(UI_Pixels(dot));
                        UI_SetNextCornerRadius(dot * 0.5f);
                        UI_SetNextColors((UI_Colors){ .background = theme->contentSelection });
                        UI_AddBlock(UI_Flag_DrawBackground);
                    }
                    Prog__Spacer(UI_Pixels(floorf(7.2f * s + 0.5f)), UI_Pixels(row));
                }
            }
        }
    }
}

// The color picker, its top left where the pointer was as it opened and kept inside the window: the group's name over a square of the
// hue's saturations and brightnesses and a strip of the hues, beside them the color, its hue, saturation, brightness, red, green and blue
// as numbers out of their greatest, its hex, and Ok and Cancel.
function void Prog__ColorPicker(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->colorPicking) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ColorPickerCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_ColorPickerClose, 0, 0, outside.pressOrder);
    f32 s = m->scale, line = m->stroke;
    // A good deal smaller than the reference's 588 by 396, which is big for picking one color.
    f32 width = floorf(440.0f * s + 0.5f), height = floorf(320.0f * s + 0.5f), radius = floorf(6.4f * s + 0.5f), bevel = floorf(4.0f * s + 0.5f);
    f32 head = floorf(26.4f * s + 0.5f), pad = floorf(8.0f * s + 0.5f), gap = floorf(8.0f * s + 0.5f), strip = floorf(18.0f * s + 0.5f);
    f32 apart = floorf(8.0f * s + 0.5f), column = floorf(140.0f * s + 0.5f), box = floorf(24.0f * s + 0.5f), rowGap = floorf(5.6f * s + 0.5f);
    f32 below = floorf(8.0f * s + 0.5f), label = floorf(18.4f * s + 0.5f), field = floorf(48.0f * s + 0.5f), slash = floorf(22.4f * s + 0.5f);
    f32 button = floorf(24.0f * s + 0.5f), margin = floorf(4.0f * s + 0.5f), ring = floorf(9.0f * s + 0.5f);
    f32 squareWidth = width - 2 * pad - gap - strip - apart - column, squareHeight = height - head - 2 * pad;
    f32 x = UI__Clamp(floorf(state->pickerAt.x - view.x0), margin, MaxF32(margin, view.x1 - view.x0 - width - margin));
    f32 y = UI__Clamp(floorf(state->pickerAt.y - view.y0), margin, MaxF32(margin, view.y1 - view.y0 - height - margin));
    R_Color color = Prog_OptionColor(state, (Prog_Option)state->colorOption);
    // The square and the strip are bevel pictures drawn smooth at any size; the square is drawn again as the hue changes.
    enum { SquareSide = 128, StripLength = 256 };
    if (!state->pickerStrip.u64[0] || state->pickerSquareHue != state->pickerHue) {
        Scratch scratch = GetScratch();
        u32 *pixels = ArenaPushArrayNoZero(scratch.arena, SquareSide * SquareSide, u32);
        for (u32 py = 0; py < SquareSide; ++py) {
            for (u32 px = 0; px < SquareSide; ++px) {
                R_Color c = Prog_HsbToRgb(state->pickerHue, (f32)px / (SquareSide - 1), 1 - (f32)py / (SquareSide - 1), 1);
                pixels[py * SquareSide + px] = 0xFF000000u | (u32)(c.b * 255 + 0.5f) << 16 | (u32)(c.g * 255 + 0.5f) << 8 | (u32)(c.r * 255 + 0.5f);
            }
        }
        if (state->pickerSquare.u64[0]) {
            R_TextureUpdateRGBA(state->pickerSquare, 0, 0, SquareSide, SquareSide, pixels);
        } else {
            state->pickerSquare = R_TextureCreateRGBA(SquareSide, SquareSide, pixels);
            R_TextureSmooth(state->pickerSquare);
        }
        state->pickerSquareHue = state->pickerHue;
        if (!state->pickerStrip.u64[0]) {
            // Red at both ends, the hues rising from the bottom.
            for (u32 py = 0; py < StripLength; ++py) {
                R_Color c = Prog_HsbToRgb(1 - (f32)py / (StripLength - 1), 1, 1, 1);
                pixels[py] = 0xFF000000u | (u32)(c.b * 255 + 0.5f) << 16 | (u32)(c.g * 255 + 0.5f) << 8 | (u32)(c.r * 255 + 0.5f);
            }
            state->pickerStrip = R_TextureCreateRGBA(1, StripLength, pixels);
            R_TextureSmooth(state->pickerStrip);
        }
        ReleaseScratch(scratch);
    }
    Prog__DropShadow(x, y, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(line);
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ColorPicker")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        // The heading follows the box's rounded top: rounded at all four corners and a radius taller, its lower corners cut off.
        UI_Block *heading = Prog__Box(UI_Flex(), UI_Pixels(head), UI_Layout_Horizontal, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(heading) {
            UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(head + radius));
            UI_SetNextCornerRadius(radius);
            UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(head));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextColors((UI_Colors){ .text = theme->text });
            UI_SetNextText(StrFormat(&state->frameArena, TrF("%S color"), Prog_Tr(Prog_Options[state->colorOption].label)));
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
        }
        UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
        UI_Block *body = Prog__Box(UI_Flex(), UI_Flex(), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(body) {
            // Across the square saturation rises, and up it brightness.
            UI_SetNextPrefWidth(UI_Pixels(squareWidth));
            UI_SetNextPrefHeight(UI_Pixels(squareHeight));
            UI_SetNextChildrenPadding((UI_Padding){ line, line, line, line });
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextImage(state->pickerSquare, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ squareWidth - 2 * line, squareHeight - 2 * line }, (R_Color){ 1, 1, 1, 1 });
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
            UI_Block *square = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ColorPickerSquare")), UI_Flag_DrawImage | UI_Flag_DrawBorder | UI_Flag_LeftMouseClickable);
            UI_Interaction pick = UI_InteractionFromBlock(square);
            if (pick.pressed || (pick.active && pick.isDragging)) {
                f32 across = UI__Clamp((UI->mouse.x - square->rect.x0 - line) / MaxF32(1, squareWidth - 2 * line), 0, 1);
                f32 up = 1 - UI__Clamp((UI->mouse.y - square->rect.y0 - line) / MaxF32(1, squareHeight - 2 * line), 0, 1);
                Prog_QueueCommand(state, Prog_CommandKind_ColorPickerSet, 1, 0, pick.pressOrder)->target = (u64)(across * 1e6f);
                Prog_QueueCommand(state, Prog_CommandKind_ColorPickerSet, 2, 0, pick.pressOrder)->target = (u64)(up * 1e6f);
            }
            UI_Parent(square) {
                // A light ring around a dark one, which shows on either.
                f32 cx = floorf(line + state->pickerSaturation * (squareWidth - 2 * line)), cy = floorf(line + (1 - state->pickerBrightness) * (squareHeight - 2 * line));
                for (u32 r = 0; r < 2; ++r) {
                    f32 size = 2 * (ring + (f32)r * line);
                    UI_SetNextFloatingOffset((UI_Vec2){ cx - size * 0.5f, cy - size * 0.5f });
                    UI_SetNextPrefWidth(UI_Pixels(size));
                    UI_SetNextPrefHeight(UI_Pixels(size));
                    UI_SetNextCornerRadius(size * 0.5f);
                    UI_SetNextBorderThickness(line);
                    UI_SetNextColors((UI_Colors){ .border = r ? (R_Color){ 0, 0, 0, 0.5f } : (R_Color){ 1, 1, 1, 0.9f } });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
                }
            }
            Prog__Spacer(UI_Pixels(gap), UI_Pixels(squareHeight));
            UI_SetNextPrefWidth(UI_Pixels(strip));
            UI_SetNextPrefHeight(UI_Pixels(squareHeight));
            UI_SetNextChildrenPadding((UI_Padding){ line, line, line, line });
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextImage(state->pickerStrip, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ strip - 2 * line, squareHeight - 2 * line }, (R_Color){ 1, 1, 1, 1 });
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
            UI_Block *hues = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ColorPickerStrip")), UI_Flag_DrawImage | UI_Flag_DrawBorder | UI_Flag_LeftMouseClickable);
            UI_Interaction slide = UI_InteractionFromBlock(hues);
            if (slide.pressed || (slide.active && slide.isDragging)) {
                f32 up = 1 - UI__Clamp((UI->mouse.y - hues->rect.y0 - line) / MaxF32(1, squareHeight - 2 * line), 0, 1);
                Prog_QueueCommand(state, Prog_CommandKind_ColorPickerSet, 0, 0, slide.pressOrder)->target = (u64)(up * 1e6f);
            }
            UI_Parent(hues) {
                f32 at = floorf(line + (1 - state->pickerHue) * (squareHeight - 2 * line) - line * 0.5f);
                UI_SetNextFloatingOffset((UI_Vec2){ -bevel * 0.5f, at });
                UI_SetNextPrefWidth(UI_Pixels(strip + bevel));
                UI_SetNextPrefHeight(UI_Pixels(line));
                UI_SetNextColors((UI_Colors){ .background = theme->text });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(squareHeight));
            UI_Block *side = Prog__Box(UI_Pixels(column), UI_Pixels(squareHeight), UI_Layout_Vertical, (R_Color){0}, 0);
            UI_Parent(side) {
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(box));
                UI_SetNextCornerRadius(bevel);
                UI_SetNextBorderThickness(line);
                UI_SetNextColors((UI_Colors){ .background = color, .border = theme->tabBorder });
                UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                Prog__Spacer(UI_Flex(), UI_Pixels(below));
                local_persist const String labels[7] = { SInit("H:"), SInit("S:"), SInit("B:"), SInit("R:"), SInit("G:"), SInit("B:"), SInit("#:") };
                local_persist const String greatest[6] = { SInit("360"), SInit("100%"), SInit("100%"), SInit("255"), SInit("255"), SInit("255") };
                for (u32 f = 1; f <= 7; ++f) {
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                    UI_Block *row = Prog__Box(UI_Flex(), UI_Pixels(box), UI_Layout_Horizontal, (R_Color){0}, 0);
                    UI_Parent(row) {
                        Prog__Label(labels[f - 1], UI_Pixels(label), box, 0, theme->text, 0);
                        b32 typing = state->pickerField == f;
                        UI_SetNextPrefWidth(f == 7 ? UI_Flex() : UI_Pixels(field));
                        UI_SetNextPrefHeight(UI_Pixels(box));
                        UI_SetNextCornerRadius(bevel);
                        UI_SetNextBorderThickness(line);
                        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                        UI_SetNextColors((UI_Colors){ .background = theme->inner, .border = typing ? theme->selection : theme->tabBorder, .text = theme->text });
                        if (!typing) UI_SetNextText(Prog_PickerFieldText(state, &state->frameArena, f));
                        UI_Block *number = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "ColorPickerField/%u", f)),
                                                              UI_Flag_DrawBorder | UI_Flag_LeftMouseClickable | UI_Flag_Clip | (typing ? UI_Flag_DrawBackground : UI_Flag_DrawText));
                        UI_Interaction press = UI_InteractionFromBlock(number);
                        if (press.pressed && !typing) Prog_QueueCommand(state, Prog_CommandKind_ColorPickerEdit, f, 0, press.pressOrder);
                        if (typing) {
                            f32 wide = R_TextWidth(m->font, Prog_EditText(&state->pickerEdit));
                            f32 boxWidth = MaxF32(1, number->rect.x1 - number->rect.x0);
                            UI_Parent(number) Prog__EditContents(&state->pickerEdit, floorf((boxWidth - wide) * 0.5f), floorf((box - m->line) * 0.5f), true, m);
                        }
                        if (f < 7) {
                            Prog__Label(S("/"), UI_Pixels(slash), box, 0, theme->text, 0.5f);
                            UI_SetNextPrefWidth(UI_Flex());
                            UI_SetNextPrefHeight(UI_Pixels(box));
                            UI_SetNextCornerRadius(bevel);
                            UI_SetNextBorderThickness(line);
                            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                            UI_SetNextColors((UI_Colors){ .border = theme->separator, .text = theme->textDim });
                            UI_SetNextText(greatest[f - 1]);
                            UI_AddBlock(UI_Flag_DrawBorder | UI_Flag_DrawText);
                        }
                    }
                    Prog__Spacer(UI_Flex(), UI_Pixels(rowGap));
                }
                Prog__Spacer(UI_Flex(), UI_Flex());
                UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(button), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(buttons) {
                    for (u32 b = 0; b < 2; ++b) {
                        UI_Key key = UI_KeyFromStr(0, b ? S("ColorPickerCancel") : S("ColorPickerOk"));
                        b32 hot = UI->hotKey == key;
                        UI_SetNextPrefWidth(UI_Flex());
                        UI_SetNextPrefHeight(UI_Pixels(button));
                        UI_SetNextCornerRadius(bevel);
                        UI_SetNextBorderThickness(line);
                        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                        UI_SetNextColors((UI_Colors){ .background = theme->track, .border = hot ? theme->outlineHover : theme->tabBorder, .text = theme->text });
                        UI_SetNextText(b ? Tr("Cancel") : Tr("Ok"));
                        UI_Interaction click = UI_InteractionFromBlock(UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText | UI_Flag_LeftMouseClickable));
                        if (click.clicked) Prog_QueueCommand(state, b ? Prog_CommandKind_ColorPickerCancel : Prog_CommandKind_ColorPickerClose, 0, 0, click.clickOrder);
                        if (!b) Prog__Spacer(UI_Pixels(floorf(6.4f * s + 0.5f)), UI_Pixels(button));
                    }
                }
            }
        }
    }
}
// A button of the layouts' boxes: a filled face with its label, dimmed and taking no clicks while it would do nothing.
function b32 Prog__LayoutButton(Prog_State *state, String key, String label, b32 able, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale;
    UI_Key id = UI_KeyFromStr(0, key);
    b32 hot = able && UI->hotKey == id;
    R_Color face = theme->track;
    if (!able) face.a *= 0.5f;
    UI_SetNextPrefWidth(UI_Pixels(floorf(96.0f * s + 0.5f)));
    UI_SetNextPrefHeight(UI_Pixels(floorf(24.0f * s + 0.5f)));
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextCornerRadius(floorf(3.2f * s + 0.5f));
    UI_SetNextBorderThickness(hot ? m->stroke : 0);
    UI_SetNextColors((UI_Colors){ .background = face, .border = theme->textDim, .text = able ? theme->text : theme->textDim });
    UI_SetNextText(label);
    UI_Block *button = UI_AddBlockFromKey(id, UI_Flag_DrawBackground | UI_Flag_DrawText | (hot ? UI_Flag_DrawBorder : 0) | (able ? UI_Flag_LeftMouseClickable : 0));
    return able && UI_InteractionFromBlock(button).clicked;
}

// The Save Layout box: its title, the name being typed, and Save, Update, which only a taken name allows, and Cancel.
function void Prog__LayoutSaveBox(Prog_State *state, const Prog_Metrics *m)
{
    // A name in the sidebar is edited in its row.
    if (!state->layoutSaving || state->nameFor) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("LayoutSaveCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 2, 0, outside.pressOrder);
    f32 s = m->scale, line = m->separator;
    f32 head = floorf(32.0f * s + 0.5f), field = floorf(36.8f * s + 0.5f), band = floorf(43.2f * s + 0.5f), radius = floorf(8.0f * s + 0.5f);
    f32 width = floorf(Prog__PaletteWidth(view.x1 - view.x0, floorf(32.8f * s + 0.5f), s)), height = head + line + field + line + band;
    f32 x = floorf((view.x1 - view.x0 - width) * 0.5f), top = 2 * m->bar + floorf(11.2f * s + 0.5f);
    f32 opacity = Prog__FadeIn(UI_KeyFromStr(0, S("LayoutSaveReveal")));
    Prog__DropShadow(x, top, width, height, radius, opacity, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextOpacity(opacity);
    UI_SetNextCornerRadius(radius);
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("LayoutSave")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    String name = Prog__TrimSpaces(Prog_EditText(&state->layoutName));
    b32 taken = false;
    for (u64 i = 0; i < state->layoutNames.header.count && !taken; ++i) taken = Prog__EqualFolded(state->layoutNames.v[i], name);
    UI_Parent(panel) {
        UI_Block *title = Prog__Box(UI_Flex(), UI_Pixels(head), UI_Layout_Horizontal, theme->menu, 0);
        UI_Parent(title) Prog__Label(Tr("Save layout"), UI_Flex(), head, 0, theme->text, 0.5f);
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextChildrenPadding((UI_Padding){ floorf(8.8f * s + 0.5f), 0, floorf(8.8f * s + 0.5f), 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, theme->inner, UI_Flag_Clip);
        UI_Parent(box) {
            if (!state->layoutName.length) {
                UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.5f) });
                UI_SetNextPrefHeight(UI_Pixels(m->line));
                UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                UI_SetNextText(Tr("Type a new or existing layout name to update..."));
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
            }
            Prog__EditContents(&state->layoutName, 0, floorf((field - m->line) * 0.5f + 0.5f), true, m);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        // The buttons end as far from the outline as from the inside of the box.
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, floorf(11.2f * s + 0.5f), 0 });
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(band), UI_Layout_Horizontal, theme->background, 0);
        UI_Parent(buttons) {
            Prog__Spacer(UI_Flex(), UI_Pixels(band));
            f32 apart = floorf(6.4f * s + 0.5f);
            if (Prog__LayoutButton(state, S("LayoutSaveSave"), Tr("Save"), name.length != 0, m)) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 0, 0, 0);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(band));
            if (Prog__LayoutButton(state, S("LayoutSaveUpdate"), Tr("Update"), taken, m)) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 1, 0, 0);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(band));
            if (Prog__LayoutButton(state, S("LayoutSaveCancel"), Tr("Cancel"), true, m)) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 2, 0, 0);
        }
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextOpacity(opacity);
    UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
}

// The box asking for an encrypted archive's password: its title, the password typed shown as dots, a line when the last one tried was
// wrong, and Open and Cancel.
function void Prog__PasswordBox(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->passwordArchive.length) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PasswordCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_ArchivePasswordButton, 1, 0, outside.pressOrder);
    f32 s = m->scale, line = m->separator;
    f32 head = floorf(32.0f * s + 0.5f), field = floorf(36.8f * s + 0.5f), band = floorf(43.2f * s + 0.5f), radius = floorf(8.0f * s + 0.5f);
    f32 note = state->passwordWrong ? m->row : 0;
    f32 width = floorf(MinF32(view.x1 - view.x0 - 2 * m->inset, 400.0f * s)), height = head + line + field + note + line + band;
    f32 x = floorf((view.x1 - view.x0 - width) * 0.5f), top = 2 * m->bar + floorf(11.2f * s + 0.5f);
    f32 opacity = Prog__FadeIn(UI_KeyFromStr(0, S("PasswordReveal")));
    Prog__DropShadow(x, top, width, height, radius, opacity, m);
    // The password is shown as a dot a character, with its caret and selection where they are in it.
    const Prog_TextEdit *typed = &state->password;
    Prog_TextEdit *shown = &state->passwordShown;
    u32 dots = 0, cursor = 0, anchor = 0;
    for (u32 i = 0; i < typed->length && (dots + 1) * 3 <= sizeof(shown->text); ++i) {
        if ((typed->text[i] & 0xC0) == 0x80) continue;
        cursor += i < typed->cursor ? 1 : 0;
        anchor += i < typed->anchor ? 1 : 0;
        MemCopy(shown->text + dots * 3, "\xE2\x80\xA2", 3);
        ++dots;
    }
    shown->length = dots * 3;
    shown->cursor = Min(cursor, dots) * 3;
    shown->anchor = Min(anchor, dots) * 3;
    shown->version = typed->version;
    shown->typing = typed->typing;
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextOpacity(opacity);
    UI_SetNextCornerRadius(radius);
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Password")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        UI_Block *title = Prog__Box(UI_Flex(), UI_Pixels(head), UI_Layout_Horizontal, theme->menu, 0);
        UI_Parent(title) Prog__Label(StrFormat(&state->frameArena, TrF("Password for %S"), state->passwordServer ? state->passwordArchive : Prog__FolderName(state->passwordArchive)), UI_Flex(), head, 0, theme->text, 0.5f);
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextChildrenPadding((UI_Padding){ floorf(8.8f * s + 0.5f), 0, floorf(8.8f * s + 0.5f), 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, theme->inner, UI_Flag_Clip);
        f32 textTop = floorf((field - m->line) * 0.5f + 0.5f);
        UI_Parent(box) {
            if (!shown->length) {
                UI_SetNextFloatingOffset((UI_Vec2){ 0, textTop });
                UI_SetNextPrefHeight(UI_Pixels(m->line));
                UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                UI_SetNextText(state->passwordServer ? Tr("Type the server's password...") : Tr("Type the archive's password..."));
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
            }
            Prog__EditContents(shown, 0, textTop, !state->passwordTrying, m);
        }
        if (note > 0) {
            UI_SetNextChildrenPadding((UI_Padding){ floorf(8.8f * s + 0.5f), 0, 0, 0 });
            UI_Block *wrong = Prog__Box(UI_Flex(), UI_Pixels(note), UI_Layout_Horizontal, theme->inner, 0);
            UI_Parent(wrong) Prog__Label(state->passwordServer ? Tr("The server did not accept that password.") : Tr("That password does not open it."), UI_Flex(), note, 0, theme->danger, 0);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, floorf(11.2f * s + 0.5f), 0 });
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(band), UI_Layout_Horizontal, theme->background, 0);
        UI_Parent(buttons) {
            Prog__Spacer(UI_Flex(), UI_Pixels(band));
            if (Prog__LayoutButton(state, S("PasswordOpen"), Tr("Open"), typed->length && !state->passwordTrying, m)) Prog_QueueCommand(state, Prog_CommandKind_ArchivePasswordButton, 0, 0, 0);
            Prog__Spacer(UI_Pixels(floorf(6.4f * s + 0.5f)), UI_Pixels(band));
            if (Prog__LayoutButton(state, S("PasswordCancel"), Tr("Cancel"), true, m)) Prog_QueueCommand(state, Prog_CommandKind_ArchivePasswordButton, 1, 0, 0);
        }
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextOpacity(opacity);
    UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
}

// What the layouts ask before overwriting or deleting one, over everything, with a button to go ahead and one not to.
function void Prog__LayoutQuestion(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->layoutAsk) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_SetNextColors((UI_Colors){ .background = { 0, 0, 0, 0.35f } });
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("LayoutAskCatcher")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_LayoutAnswer, 0, 0, outside.pressOrder);
    String name = StrMake((const char *)state->layoutAskName, state->layoutAskLength);
    b32 deleting = state->layoutAsk == Prog_LayoutAsk_Delete || state->layoutAsk == Prog_LayoutAsk_DeleteScheme, switching = state->layoutAsk == Prog_LayoutAsk_SwitchBranch;
    String title = switching ? StrFormat(&state->frameArena, TrN(state->git.changes, "%u change not committed. Switch to \"%S\" and take it along?", "%u changes not committed. Switch to \"%S\" and take them along?"), state->git.changes, name)
                 : state->layoutAsk == Prog_LayoutAsk_DeleteScheme ? StrFormat(&state->frameArena, TrF("Delete the custom color scheme \"%S\"?"), name)
                 : deleting ? StrFormat(&state->frameArena, TrF("Delete the layout \"%S\"?"), name) : StrFormat(&state->frameArena, TrF("Overwrite the existing layout \"%S\"?"), name);
    f32 width = floorf(MinF32(MaxF32(R_TextWidth(m->font, title) + 2 * m->inset, m->line * 20), view.x1 - view.x0 - 2 * m->inset));
    f32 height = floorf(m->row + m->bar + m->inset * 3);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ floorf((view.x1 - view.x0 - width) * 0.5f), floorf((view.y1 - view.y0 - height) * 0.4f) });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, m->inset, m->inset, m->inset });
    UI_SetNextCornerRadius(m->radius * 2);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->hover });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("LayoutAsk")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        Prog__Label(title, UI_Flex(), m->row, 0, theme->text, 0);
        Prog__Spacer(UI_Flex(), UI_Pixels(m->inset));
        UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->menu, 0);
        UI_Parent(buttons) {
            Prog__Spacer(UI_Flex(), UI_Pixels(m->bar));
            UI_Interaction cancel = Prog__TextButton(S("LayoutAskCancel"), Tr("Cancel"), NULL, UI_TextContent(), m->bar * 0.8f, m->inset, theme->textHeading, theme->text, theme->textDim, true, m);
            if (cancel.clicked) Prog_QueueCommand(state, Prog_CommandKind_LayoutAnswer, 0, 0, cancel.clickOrder);
            Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
            UI_Interaction confirm = Prog__TextButton(S("LayoutAskConfirm"), switching ? Tr("Switch") : deleting ? Tr("Delete") : Tr("Overwrite"), NULL, UI_TextContent(), m->bar * 0.8f,
                                                      m->inset, switching ? theme->textHeading : theme->danger, switching ? theme->text : theme->danger, theme->textDim, true, m);
            if (confirm.clicked) Prog_QueueCommand(state, Prog_CommandKind_LayoutAnswer, 1, 0, confirm.clickOrder);
        }
    }
}

// The palette, Go To, Search Tabs or Options while open, and for a moment after they close as they fade out.
function void Prog__Palette(Prog_State *state, const Prog_Metrics *m)
{
    if (state->paletteOpen) {
        state->paletteShown = true;
        state->paletteFade = 1;
        Prog__PaletteBody(state, m);
        return;
    }
    if (state->paletteShown) {
        state->paletteShown = false;
        state->paletteClosedAt = UI->animate ? UI->nowMicroseconds : 0;
    }
    if (!state->paletteClosedAt) return;
    state->paletteFade = expf(-(f32)(UI->nowMicroseconds - Min(state->paletteClosedAt, UI->nowMicroseconds)) / 32000.0f);
    if (state->paletteFade < 0.02f) {
        state->paletteClosedAt = 0;
        return;
    }
    UI_KeepAnimating();
    UI_NoInput() Prog__PaletteBody(state, m);
}

function void Prog__PaletteBody(Prog_State *state, const Prog_Metrics *m)
{
    if (state->paletteMode == Prog_PaletteMode_GoTo) {
        Prog__GoTo(state, m);
        return;
    }
    if (state->paletteMode == Prog_PaletteMode_Tabs) {
        Prog__TabSearch(state, m);
        return;
    }
    if (state->paletteMode == Prog_PaletteMode_Layouts) {
        Prog__LayoutsPicker(state, m);
        return;
    }
    // The font dropdown opens over Options, which show under it as they were.
    if (state->paletteMode == Prog_PaletteMode_Fonts) {
        Prog_TextEdit search = state->palette;
        u32 highlight = state->paletteHighlight, scroll = state->paletteScroll;
        b32 wheeled = state->paletteWheeled;
        state->paletteMode = Prog_PaletteMode_Options;
        state->palette = state->optionsPalette;
        state->paletteHighlight = state->optionsHighlight;
        state->paletteScroll = state->optionsScroll;
        state->paletteWheeled = state->optionsWheeled;
        state->fontsOver = true;
        Prog__PaletteBody(state, m);
        state->fontsOver = false;
        state->optionsHighlight = state->paletteHighlight;
        state->optionsScroll = state->paletteScroll;
        state->paletteMode = Prog_PaletteMode_Fonts;
        state->palette = search;
        state->paletteHighlight = highlight;
        state->paletteScroll = scroll;
        state->paletteWheeled = wheeled;
        Prog__FontsDropdown(state, m);
        return;
    }
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PaletteCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, outside.pressOrder);
    // The Options popup is the palette with options in its rows.
    b32 options = state->paletteMode == Prog_PaletteMode_Options;
    Prog_PaletteRow rows[Prog_MaxPaletteRows];
    u8 found[Prog_Option_Count];
    u32 count = options ? Prog_OptionMatches(state, found) : Prog_PaletteMatches(state, rows);
    state->paletteHighlight = count ? Min(state->paletteHighlight, count - 1) : 0;
    String typed = Prog_EditText(&state->palette);
    b32 searching = typed.length > 0;
    f32 s = m->scale, line = m->separator;
    f32 field = floorf(39.2f * s + 0.5f), footer = floorf(31.2f * s + 0.5f), pad = floorf(4.0f * s + 0.5f);
    f32 row = floorf((state->paletteCompact ? 24.0f : 40.8f) * s + 0.5f) + line;
    // A font taller than the reference's grows the rows rather than cutting off the description under the label.
    if (!state->paletteCompact) row = MaxF32(row, floorf(4.8f * s + 0.5f) + m->line + floorf(2.4f * s + 0.5f) + m->detail->lineHeight + line);
    f32 top = 2 * m->bar + floorf(11.2f * s + 0.5f), margin = floorf(32.8f * s + 0.5f), bottom = floorf(60.0f * s + 0.5f);
    // The box reaches down to the bottom margin, with as many rows as fit and the next one cut off under them, with lines between them.
    // The list scrolls only as far as keeps the highlight wholly in sight, unless the wheel scrolled it.
    f32 room = view.y1 - view.y0 - top - bottom - field - 2 * line - footer - 2 * pad;
    u32 visible = (u32)MaxF32(floorf((room + line) / row), 1.0f);
    if (!state->paletteWheeled && state->paletteHighlight < state->paletteScroll) state->paletteScroll = state->paletteHighlight;
    if (!state->paletteWheeled && state->paletteHighlight >= state->paletteScroll + visible) state->paletteScroll = state->paletteHighlight - visible + 1;
    state->paletteScroll = count > visible ? Min(state->paletteScroll, count - visible) : 0;
    // The rows slide to where the scroll stops rather than jump.
    f32 scrolled = count > visible ? UI_Animate(UI_KeyFromStr(0, S("PaletteScroll")), (f32)state->paletteScroll, (f32)state->paletteScroll) : 0;
    u32 first = Min((u32)scrolled, count ? count - 1 : 0), shown = Min(count - first, visible + 2);
    f32 lag = (scrolled - (f32)first) * row;
    f32 width = floorf(Prog__PaletteWidth(view.x1 - view.x0, margin, s));
    f32 listHeight = MinF32((f32)Min(count - state->paletteScroll, visible + 1) * row - line, room) + 2 * pad;
    f32 height = field + line + listHeight + line + footer;
    // It fades in as the menus do.
    f32 opacity = Prog__FadeIn(UI_KeyFromStr(0, S("PaletteReveal"))) * state->paletteFade;
    f32 radius = floorf(8.0f * s + 0.5f), x = floorf((view.x1 - view.x0 - width) * 0.5f);
    Prog__DropShadow(x, top, width, height, radius, opacity, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextOpacity(opacity);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill() });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Palette")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        UI_SetNextChildrenPadding((UI_Padding){ floorf(12.8f * s + 0.5f), 0, floorf(8.0f * s + 0.5f), 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, theme->inner, 0);
        // Round along the box's top corners, which its clip alone would leave square.
        box->props.cornerRadius = radius;
        box->props.shape = R_Shape_SquareBottomLeft | R_Shape_SquareBottomRight;
        UI_Parent(box) {
            Prog__SearchIcon(floorf(22.4f * s + 0.5f), field, theme->textDim, 0, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    // Placed as the edit's own text is.
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(options ? Tr("Select an option...") : Tr("Select a command..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.5f), true, m);
            }
            // The descriptions' switch, lit while they are hidden, then a line before the close button.
            UI_Key detailsKey = UI_KeyFromStr(0, S("PaletteDetailsButton"));
            b32 lit = state->paletteCompact || UI->hotKey == detailsKey;
            R_Color wash = theme->selection;
            wash.a *= theme->hoverAmount.r;
            UI_SetNextPrefWidth(UI_Pixels(Prog__ToolbarButtonWidth(m)));
            UI_SetNextPrefHeight(UI_Pixels(Prog__ToolbarButtonHeight(m)));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
            UI_SetNextColors((UI_Colors){ .background = wash, .text = lit ? theme->text : theme->icon });
            UI_SetNextFont(m->icons);
            UI_SetNextText(state->paletteCompact ? Icon_FileX : Icon_FileText);
            UI_Block *details = UI_AddBlockFromKey(detailsKey, UI_Flag_DrawText | UI_Flag_LeftMouseClickable | (lit ? UI_Flag_DrawBackground : 0));
            Prog__NoteTooltip(state, Prog_CommandKind_PaletteToggleDetails, Tr("Toggle Extra Details"));
            // Over the palette, which keeps the window's other tips out.
            if (state->tooltipKey == details->key) state->tooltipAbove = true;
            UI_Interaction toggle = UI_InteractionFromBlock(details);
            if (toggle.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteToggleDetails, 0, 0, toggle.clickOrder);
            f32 apart = floorf(6.4f * s + 0.5f);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(field));
            Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(field));
            UI_Interaction close = Prog__CloseButton(S("PaletteCloseButton"), m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, close.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        local_persist const String groupNames[] = { TrInit("Quick"), TrInit("Folder"), TrInit("View"), TrInit("Sort"), TrInit("Inspector"), TrInit("Panels"), TrInit("Tabs"), TrInit("Go To"),
                                                    TrInit("Misc"), TrInit("Options"), TrInit("Rename"), TrInit("Text"), TrInit("Scroll") };
        const String groupIcons[] = { Icon_StarFilled, Icon_Folder, Icon_Image, Icon_ArrowDownAZ, Icon_ScanEye, Icon_LayoutPanelLeft, Icon_AppWindow,
                                                    Icon_FolderSearch, Icon_Share2, Icon_SlidersHorizontal, Icon_TextCursorInput, Icon_TextCursorInput, Icon_ChevronsUpDown };
        static_assert(CArrayCount(groupNames) == Prog_CommandGroup_Count && CArrayCount(groupIcons) == Prog_CommandGroup_Count, "a name and an icon for every group");
        // The groups in front of found commands are as wide as the widest name, as some languages' are long.
        f32 groupWidth = floorf(63.2f * s + 0.5f);
        for (u32 g = 0; g < Prog_CommandGroup_Count; ++g) groupWidth = MaxF32(groupWidth, ceilf(R_TextWidth(m->font, Prog_Tr(groupNames[g])) + m->gap));
        // Room on the right for the scroll bar while there is one.
        f32 bar = floorf(6.4f * s + 0.5f), side = floorf(6.4f * s + 0.5f), gutter = count > visible ? floorf(19.2f * s + 0.5f) : side;
        UI_SetNextChildrenPadding((UI_Padding){ side, pad - floorf(lag), gutter, pad + floorf(lag) });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        state->optionHovered = 0;
        UI_Parent(list) {
            for (u32 i = first; i < first + shown; ++i) {
                if (options) {
                    // The keyboard's row is outlined once a key is pressed, not as Options opens.
                    Prog__OptionRow(state, (Prog_Option)found[i], i, i == state->paletteHighlight && state->paletteKeyed, row - line, m);
                    if (i + 1 < first + shown) Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
                    continue;
                }
                Prog_PaletteRow at = rows[i];
                UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Palette/%u/%u", (u32)at.group, (u32)at.kind));
                b32 highlighted = i == state->paletteHighlight, hot = UI->hotKey == key && !highlighted;
                f32 face = row - line, slot = floorf(21.6f * s + 0.5f);
                // The pointer's row is the highlight's color washed out, inside its outline; a command that would do nothing now is dimmed,
                // and highlighted in that washed-out color.
                R_Color washed = theme->selection;
                washed.a *= theme->hoverAmount.r;
                b32 able = at.kind == Prog_CommandKind_None || Prog_CommandAvailable(state, (Prog_CommandKind)at.kind);
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(face));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextChildrenPadding((UI_Padding){ floorf(8.0f * s + 0.5f), 0, floorf(12.0f * s + 0.5f), 0 });
                UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
                UI_SetNextBorderThickness(m->stroke);
                UI_SetNextColors((UI_Colors){ .background = highlighted && able ? theme->selection : washed, .border = theme->selection });
                UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_Clip | (highlighted || hot ? UI_Flag_DrawBackground : 0) | (hot ? UI_Flag_DrawBorder : 0));
                UI_Interaction interaction = UI_InteractionFromBlock(entry);
                if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, i, 0, interaction.clickOrder);
                UI_Parent(entry) {
                    // The highlight shows a row that would do nothing in full, on its washed-out color.
                    R_Color icon = highlighted ? theme->text : able ? theme->icon : theme->iconDisabled, named = able || highlighted ? theme->text : theme->textDim;
                    if (at.kind == Prog_CommandKind_None) {
                        // A group's title, which opens and closes it.
                        b32 open = state->paletteExpanded & (1u << at.group);
                        Prog__Icon(groupIcons[at.group], slot, face, icon, 0, m);
                        Prog__Label(Prog_Tr(groupNames[at.group]), UI_Flex(), face, 0, theme->text, 0);
                        Prog__Icon(open ? Icon_ChevronDown : Icon_ChevronRight, floorf(m->line * 1.2f), face, icon, 0.5f, m);
                    } else {
                        String label = Prog_CommandLabel((Prog_CommandKind)at.kind), tooltip = Prog_CommandTooltip((Prog_CommandKind)at.kind);
                        if (searching) {
                            // A command found by searching shows its group in front of it, and a check in place of its icon while it is on.
                            Prog__Icon(Prog_CommandChecked(state, (Prog_CommandKind)at.kind) ? Icon_Check : groupIcons[at.group], slot, face, icon, 0, m);
                            Prog__Label(Prog_Tr(groupNames[at.group]), UI_Pixels(groupWidth), face, 0, named, 0);
                            Prog__Icon(Icon_ChevronRight, floorf(20.8f * s + 0.5f), face, icon, 0, m);
                        } else if (Prog_CommandChecked(state, (Prog_CommandKind)at.kind)) {
                            // A check, as the menus have, for the view, the sort and the grouping shown now.
                            Prog__Icon(Icon_Check, slot + floorf(1.6f * s + 0.5f), face, icon, 0, m);
                        } else {
                            Prog__Spacer(UI_Pixels(slot + floorf(1.6f * s + 0.5f)), UI_Pixels(face));
                        }
                        // What was typed shows in the match color, in the label and in the description.
                        u8 *labelMarks = searching ? ArenaPushArray(&state->frameArena, label.length + 1, u8) : NULL;
                        u8 *tooltipMarks = searching ? ArenaPushArray(&state->frameArena, tooltip.length + 1, u8) : NULL;
                        if (searching) Prog_FuzzyScore(label, tooltip, typed, labelMarks, tooltipMarks);
                        UI_SetNextPrefWidth(UI_Flex());
                        UI_SetNextPrefHeight(UI_Pixels(face));
                        UI_SetNextChildrenLayout(UI_Layout_Vertical);
                        UI_SetNextChildrenPadding((UI_Padding){ 0, state->paletteCompact ? floorf((face - m->line) * 0.5f) : floorf(4.8f * s + 0.5f), 0, 0 });
                        UI_Block *texts = UI_AddBlock(UI_Flag_Clip);
                        UI_Parent(texts) {
                            Prog__MarkedText(label, labelMarks, m->font, m->line, named);
                            if (!state->paletteCompact) {
                                Prog__Spacer(UI_Flex(), UI_Pixels(floorf(2.4f * s + 0.5f)));
                                R_Color described = theme->textDim;
                                if (!able && !highlighted) described.a *= 0.6f;
                                Prog__MarkedText(tooltip, tooltipMarks, m->detail, m->detail->lineHeight, highlighted ? theme->text : described);
                            }
                        }
                        // The highlighted row's buttons star it into Quick or out of it, bring back its default hotkeys once they changed and add a
                        // new one; then its chips, which a click rebinds.
                        if (highlighted) {
                            b32 quick = Prog_IsQuick(state, (Prog_CommandKind)at.kind);
                            UI_Interaction star = Prog__TextButton(StrFormat(&state->frameArena, "PaletteQuick/%u", (u32)at.kind), quick ? Icon_StarFilled : Icon_Star, m->icons,
                                                                   UI_Pixels(slot), face, 0, theme->text, theme->text, theme->iconDisabled, true, m);
                            if (star.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteQuickToggle, at.kind, 0, star.clickOrder);
                            Prog__PopupTooltip(state, quick ? Tr("Remove from Quick.") : Tr("Add to Quick."));
                            if (Prog_HotkeysChanged(state, (Prog_CommandKind)at.kind)) {
                                UI_Interaction restore = Prog__TextButton(StrFormat(&state->frameArena, "PaletteRestore/%u", (u32)at.kind), Icon_RotateCcw, m->icons, UI_Pixels(slot), face, 0,
                                                                          theme->text, theme->text, theme->iconDisabled, true, m);
                                if (restore.clicked) Prog_QueueCommand(state, Prog_CommandKind_HotkeyRestore, at.kind, 0, restore.clickOrder);
                                Prog__PopupTooltip(state, Tr("Restore default hotkeys."));
                            }
                            UI_Interaction add = Prog__TextButton(StrFormat(&state->frameArena, "PaletteAdd/%u", (u32)at.kind), Icon_Plus, m->icons, UI_Pixels(slot), face, 0,
                                                                  theme->text, theme->text, theme->iconDisabled, true, m);
                            if (add.clicked) Prog_QueueCommand(state, Prog_CommandKind_HotkeyRecord, at.kind, 0, add.clickOrder)->target = Prog_NewHotkeySlot;
                            Prog__PopupTooltip(state, Tr("Assign new hotkey."));
                        }
                        Prog_HotkeyChip chips[4];
                        u32 chipCount = Prog_HotkeyChips(&state->frameArena, state, (Prog_CommandKind)at.kind, chips, CArrayCount(chips));
                        for (u32 c = 0; c < chipCount; ++c) {
                            Prog__Spacer(UI_Pixels(floorf(3.2f * s + 0.5f)), UI_Pixels(face));
                            Prog__HotkeyChipButton(state, NoTr("Palette"), (Prog_CommandKind)at.kind, &chips[c], face, highlighted, m);
                        }
                    }
                }
                if (i + 1 < first + shown) Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
            }
            if (count > visible) {
                // The scroll bar: a thumb as long as the share of rows shown, where they are among all of them. It sits in the list's padding,
                // which moves up with the rows while they slide part of a row, so it is moved back down by as much.
                f32 track = listHeight - 2 * pad, thumb = MaxF32(floorf(track * (f32)visible / (f32)count), row);
                f32 offset = floorf((track - thumb) * scrolled / (f32)(count - visible)) + floorf(lag);
                UI_SetNextFloatingOffset((UI_Vec2){ width - side - gutter + floorf((gutter - bar) * 0.5f), offset });
                UI_SetNextPrefWidth(UI_Pixels(bar));
                UI_SetNextPrefHeight(UI_Pixels(thumb));
                UI_SetNextCornerRadius(floorf(bar * 0.5f));
                UI_SetNextOpacity(opacity);
        UI_SetNextColors((UI_Colors){ .background = theme->track });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        }
        // The keys that work the palette.
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        local_persist const char *const keys[] = { "Up", "Down", 0, "Enter", 0, "Escape", 0 };
        local_persist const String words[] = { {0}, {0}, TrInit("to navigate"), {0}, TrInit("to use"), {0}, TrInit("to dismiss") };
        // In its own band, unless the box is already in the menus' color, which a second coat would darken.
        Prog__PaletteHints(keys, words, CArrayCount(keys), footer, theme->background.a < 1 ? (R_Color){0} : theme->menu, radius, m);
    }
    // The outline over everything inside, so that rows do not cover it.
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextOpacity(opacity);
    UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
}

// The name of the file a transfer is copying, read again while the job rewrites it; empty before the first.
function String Prog__TransferName(Arena *arena, P_Progress *progress)
{
    char *name = ArenaPushArrayNoZero(arena, CArrayCount(progress->name), char);
    for (u32 tries = 0; tries < 100; ++tries) {
        u32 version = atomic_load(&progress->nameVersion);
        if (version & 1) continue;
        u64 length = 0;
        while (length + 1 < CArrayCount(progress->name) && (name[length] = atomic_load_explicit(&progress->name[length], memory_order_relaxed))) ++length;
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&progress->nameVersion, memory_order_relaxed) == version) return StrMake(name, length);
    }
    return (String){0};
}

// Copies and moves that take a while, in cards at the bottom right: what goes where, the file being copied, how far it is, and a question when a name is taken.
// How long a warning and an info show, from when they came or were last under the pointer, and how long they take to fade.
enum { Prog_NoticeInfoMicroseconds = 3000000, Prog_NoticeWarningMicroseconds = 6000000, Prog_NoticeFadeMicroseconds = 300000 };

// How long `notice` has left before it is gone, or UINT64_MAX for an error, which stays until closed.
function u64 Prog_NoticeLeft(const Prog_Notice *notice, u64 now)
{
    if (notice->kind == Prog_NoticeKind_Error || notice->action) return UINT64_MAX;
    u64 lasts = notice->kind == Prog_NoticeKind_Warning ? Prog_NoticeWarningMicroseconds : Prog_NoticeInfoMicroseconds, gone = now - Min(notice->since, now);
    return gone < lasts ? lasts - gone : 0;
}

// `text` broken at its spaces into lines no wider than `width`, at most `most`, the last one cut with an ellipsis when more is left.
function u32 Prog__WrapLines(Arena *arena, const R_Font *font, String text, f32 width, String *lines, u32 most)
{
    u32 count = 0;
    u64 at = 0;
    while (at < text.length && count < most) {
        while (at < text.length && text.value[at] == ' ') ++at;
        if (at >= text.length) break;
        if (count + 1 == most) {
            lines[count++] = Prog__Ellipsize(arena, font, StrFrom(text, at), width);
            break;
        }
        // As many words as fit, or the first one whole however long.
        u64 end = at, fit = at;
        while (end < text.length) {
            u64 next = end;
            while (next < text.length && text.value[next] == ' ') ++next;
            while (next < text.length && text.value[next] != ' ') ++next;
            if (fit > at && R_TextWidth(font, StrFromTo(text, at, (i64)next)) > width) break;
            fit = end = next;
        }
        lines[count++] = Prog__Ellipsize(arena, font, StrFromTo(text, at, (i64)fit), width);
        at = fit;
    }
    return count;
}

// Where a stack of cards goes: the box from whose bottom right corner it rises, `margin` in from it, and the top of the stack so far.
typedef struct Prog__CardStack {
    R_Rect corner;
    f32 margin;
    f32 bottom;
} Prog__CardStack;

// A card narrower than this puts on lines of their own what a wide one has side by side.
function b32 Prog__NarrowCard(f32 width, const Prog_Metrics *m)
{
    return width < m->line * 22;
}

// The buttons that mute a notice's kind and copy its message.
function void Prog__NoticeButtons(Prog_State *state, const Prog_Notice *notice, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    // A warning or an info can be muted; an error only by hand, in MutedNotices.conf.
    if (notice->kind != Prog_NoticeKind_Error && notice->keyLength) {
        UI_Interaction mute = Prog__TextButton(StrFormat(frame, "NoticeMute/%" PRIu64, notice->id), Icon_BellOff, m->iconsSmall, UI_Pixels(m->row), m->row, 0,
                                               theme->icon, theme->text, theme->iconDisabled, true, m);
        if (mute.clicked) Prog_QueueCommand(state, Prog_CommandKind_NoticeMute, notice->id, 0, mute.clickOrder);
        Prog__PopupTooltip(state, Tr("Don't show this again."));
    }
    UI_Interaction copy = Prog__TextButton(StrFormat(frame, "NoticeCopy/%" PRIu64, notice->id), Icon_Copy, m->iconsSmall, UI_Pixels(m->row), m->row, 0,
                                           theme->icon, theme->text, theme->iconDisabled, true, m);
    if (copy.clicked) Prog_QueueCommand(state, Prog_CommandKind_NoticeCopy, notice->id, 0, copy.clickOrder);
    Prog__PopupTooltip(state, Tr("Copy the message."));
}

// The notices over the copy and move cards of `stack`, the newest lowest: the kind's icon in its color, the title, how many times it came,
// and buttons to mute it, copy it and close it, over its message, a few lines at most; a narrow card has the first two under its message.
// A notice under the pointer does not fade.
function void Prog__Notices(Prog_State *state, Prog__CardStack stack, const Prog_Metrics *m)
{
    if (!state->noticeCount) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    Arena *frame = &state->frameArena;
    u64 now = PlatformNowMicroseconds();
    f32 width = floorf(MinF32(m->line * 26, stack.corner.x1 - stack.corner.x0 - 2 * stack.margin)), pad = m->inset * 0.75f, lineHeight = m->detail->lineHeight + floorf(1.6f * m->scale + 0.5f);
    f32 slot = floorf(m->line * 1.6f + 0.5f), bottom = stack.bottom;
    b32 narrow = Prog__NarrowCard(width, m);
    for (u32 n = state->noticeCount; n-- > 0;) {
        Prog_Notice *notice = &state->notices[n];
        u64 left = Prog_NoticeLeft(notice, now);
        if (!left) continue;
        R_Color tint = notice->kind == Prog_NoticeKind_Error ? theme->danger : notice->kind == Prog_NoticeKind_Warning ? Prog__GitInk(Prog_GitMark_Modified) : theme->accent;
        String icon = notice->kind == Prog_NoticeKind_Error ? Icon_CircleX : notice->kind == Prog_NoticeKind_Warning ? Icon_TriangleAlert : Icon_Info;
        String title = StrMake((const char *)notice->title, notice->titleLength), message = StrMake((const char *)notice->message, notice->messageLength);
        String lines[4];
        u32 lineCount = Prog__WrapLines(frame, m->detail, message, width - 2 * pad - slot, lines, CArrayCount(lines));
        f32 actionHeight = notice->action ? m->bar : narrow ? m->row : 0;
        f32 height = 2 * pad + m->row + (f32)lineCount * lineHeight + actionHeight;
        bottom -= height + m->gap;
        UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Notice/%" PRIu64, notice->id));
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextFloatingOffset((UI_Vec2){ stack.corner.x1 - view.x0 - width - stack.margin, floorf(bottom) });
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextBlur(Prog__FloatingBlur(m));
        // It fades in its last moment, and comes in as the menus do.
        f32 opacity = MinF32(Prog__FadeIn(key), left < Prog_NoticeFadeMicroseconds ? (f32)left / (f32)Prog_NoticeFadeMicroseconds : 1);
        UI_SetNextOpacity(opacity);
        R_Color edge = tint;
        edge.a *= 0.7f;
        UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = edge });
        UI_Block *card = UI_AddBlockFromKey(key, UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        b32 hovered = UI->hotKey && (UI->hotKey == key || UI->hotKey == UI_KeyFromStr(0, StrFormat(frame, "NoticeCopy/%" PRIu64, notice->id)) ||
                                     UI->hotKey == UI_KeyFromStr(0, StrFormat(frame, "NoticeMute/%" PRIu64, notice->id)) ||
                                     UI->hotKey == UI_KeyFromStr(0, StrFormat(frame, "NoticeClose/%" PRIu64, notice->id)));
        if (hovered && notice->kind != Prog_NoticeKind_Error) notice->since = now;
        UI_Parent(card) {
            UI_Block *head = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, (R_Color){0}, 0);
            UI_Parent(head) {
                Prog__Icon(icon, slot, m->row, tint, 0, m);
                f32 room = width - 2 * pad - slot - (narrow ? 1 : 3) * m->row - (notice->count > 1 ? m->line * 2 : 0);
                Prog__Label(Prog__Ellipsize(frame, m->font, title, MaxF32(0, room)), UI_Flex(), m->row, 0, theme->text, 0);
                if (notice->count > 1) {
                    UI_SetNextFont(m->detail);
                    Prog__Label(StrFormat(frame, "\u00D7%u", notice->count), UI_TextContent(), m->row, floorf(4.0f * m->scale + 0.5f), theme->textDim, 0);
                }
                if (!narrow) Prog__NoticeButtons(state, notice, m);
                UI_Interaction close = Prog__CloseButton(StrFormat(frame, "NoticeClose/%" PRIu64, notice->id), m);
                if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_NoticeClose, notice->id, 0, close.clickOrder);
            }
            for (u32 l = 0; l < lineCount; ++l) {
                UI_Block *line = Prog__Box(UI_Flex(), UI_Pixels(lineHeight), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(line) {
                    Prog__Spacer(UI_Pixels(slot), UI_Pixels(lineHeight));
                    UI_SetNextFont(m->detail);
                    Prog__Label(lines[l], UI_Flex(), lineHeight, 0, theme->textDim, 0);
                }
            }
            // Its own button, at the right under the message, and a narrow card's others after it.
            if (notice->action || narrow) {
                UI_SetNextChildrenAlignment((UI_Vec2){ 1, 1 });
                UI_Block *actions = Prog__Box(UI_Flex(), UI_Pixels(actionHeight), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(actions) {
                    Prog__Spacer(UI_Flex(), UI_Pixels(actionHeight));
                    if (notice->action) {
                        String label = StrMake((const char *)notice->actionLabel, notice->actionLength);
                        UI_Interaction act = Prog__TextButton(StrFormat(frame, "NoticeAction/%" PRIu64, notice->id), label, NULL, UI_TextContent(), Prog__ToolbarButtonHeight(m), m->inset,
                                                              theme->contentSelection, theme->contentSelection, theme->iconDisabled, true, m);
                        UI->parent->last->props.colors.background = theme->selection;
                        UI->parent->last->flags |= UI_Flag_DrawBackground;
                        if (act.clicked) Prog_QueueCommand(state, (Prog_CommandKind)notice->action, notice->id, 0, act.clickOrder);
                    }
                    if (narrow) Prog__NoticeButtons(state, notice, m);
                }
            }
        }
        if (left != UINT64_MAX) UI_KeepAnimating();
    }
}

// `milliseconds` as "1:23" or "1:02:03".
function String Prog__Clock(Arena *arena, u64 milliseconds)
{
    u64 seconds = milliseconds / 1000, hours = seconds / 3600;
    return hours ? StrFormat(arena, "%u:%02u:%02u", (u32)hours, (u32)(seconds / 60 % 60), (u32)(seconds % 60)) : StrFormat(arena, "%u:%02u", (u32)(seconds / 60), (u32)(seconds % 60));
}

// The panel in whose bottom right corner the card of a transfer into the folder of `targetHash` goes: the active one or another showing that
// folder, or else the one in the window's corner; while a panel is zoomed over the others, that one. UINT32_MAX for the window's corner.
function u32 Prog__CardPanel(const Prog_State *state, u64 targetHash, R_Rect view)
{
    if (state->zoomShownPanel) return Prog_PanelIndex(state, state->zoomShownPanel);
    u32 result = UINT32_MAX;
    for (u32 n = 0; n < state->panelCount; ++n) {
        u32 i = (state->currentPanel + n) % state->panelCount;
        const Prog_Panel *panel = &state->panels[i];
        String path = i == state->currentPanel ? state->directory.path : panel->directory.path;
        R_Rect rect = i == state->currentPanel ? state->panelRect : panel->panelRect;
        if (panel->inspectorOf || rect.x1 <= rect.x0) continue;
        if (targetHash && MapHashBytes(path.value, path.length) == targetHash) return i;
        if (rect.x1 >= view.x1 - 1 && rect.y1 >= view.y1 - 1) result = i;
    }
    return result;
}

// A transfer card's buttons that pause it and stop it.
function void Prog__TransferButtons(Prog_State *state, u32 slot, b32 paused, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    // Lucide draws these larger than its cross, which the smaller icons bring to its size.
    UI_Interaction pause = Prog__TextButton(StrFormat(&state->frameArena, "TransferPause/%u", slot), paused ? Icon_Play : Icon_Pause, m->iconsSmall, UI_Pixels(m->row), m->row, 0,
                                            theme->icon, theme->text, theme->iconDisabled, true, m);
    if (pause.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferPause, slot, 0, pause.clickOrder);
    UI_Interaction cancel = Prog__TextButton(StrFormat(&state->frameArena, "TransferCancel/%u", slot), Icon_X, m->icons, UI_Pixels(m->row), m->row, 0,
                                             theme->icon, theme->text, theme->iconDisabled, true, m);
    if (cancel.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferCancel, slot, 0, cancel.clickOrder);
}

// The copy and move cards in the corners of the panels they go into, or at the bottom of the sidebar `sidebar` wide as it slides, the newest
// lowest; it gives the stack the notices go over.
function Prog__CardStack Prog__Transfers(Prog_State *state, f32 sidebar, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    u64 now = PlatformNowMicroseconds();
    f32 pad = m->inset * 0.75f;
    // Each panel's stack, then the window's and the sidebar's.
    u32 windowStack = Prog_MaxPanels, sidebarStack = Prog_MaxPanels + 1;
    Prog__CardStack stacks[Prog_MaxPanels + 2];
    for (u32 k = 0; k <= windowStack; ++k) {
        R_Rect corner = k == state->currentPanel ? state->panelRect : k < state->panelCount ? state->panels[k].panelRect : view;
        stacks[k] = (Prog__CardStack){ corner, m->inset, corner.y1 - view.y0 - m->pill - m->inset };
    }
    f32 edge = view.x0 + sidebar - m->separator;
    f32 snug = floorf(m->gap * 0.75f + 0.5f);
    stacks[sidebarStack] = (Prog__CardStack){ { edge - m->sidebar, view.y0, edge, view.y1 }, snug, view.y1 - view.y0 - snug };
    b32 inSidebar = state->noticesInSidebar && sidebar > m->separator;
    u64 torrentTarget = 0;
    for (u32 i = 0; i < Prog_TransferSlots; ++i) {
        Prog_Transfer *transfer = &state->transfers[i];
        P_Progress *progress = &transfer->progress;
        b32 asking = transfer->job && atomic_load(&progress->asking), paused = atomic_load(&progress->paused);
        u64 items = atomic_load(&progress->totalItems), doneItems = atomic_load(&progress->doneItems);
        u64 bytes = atomic_load(&progress->totalBytes), doneBytes = atomic_load(&progress->doneBytes);
        // Windows shows its own progress for copies outside archives, which leave the counts at zero and never show here.
        if (!transfer->job || !items || (!asking && now - transfer->started < UINT64_C(400000))) continue;
        String target = StrMake((const char *)transfer->target, transfer->targetLength);
        if (transfer->op == Prog_TransferOp_Torrent) torrentTarget = transfer->targetHash;
        // A question about a taken name needs a panel's room for its buttons.
        Prog__CardStack *at = &stacks[inSidebar && !asking ? sidebarStack : Min(Prog__CardPanel(state, transfer->targetHash, view), windowStack)];
        f32 width = floorf(MinF32(m->line * 26, at->corner.x1 - at->corner.x0 - 2 * at->margin));
        b32 narrow = Prog__NarrowCard(width, m);
        // An encoding's or a download's speed and time left stay whole after its name, which is cut short first; a narrow card has them under it.
        String name = Prog__TransferName(&state->frameArena, progress);
        Index dot = transfer->op >= Prog_TransferOp_CompressVideo ? StrIndexStr(name, S(" \u00B7 "), SSF_None) : (Index){0};
        String details = dot.exists ? StrFrom(name, dot.value) : (String){0};
        if (dot.exists) name = StrFromTo(name, 0, (i64)dot.value);
        f32 height = 2 * pad + m->row * 3 + (asking ? m->bar : 0) + (narrow ? m->gap + (details.length && !asking ? m->row : 0) : 0);
        at->bottom -= height + m->gap;
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextFloatingOffset((UI_Vec2){ at->corner.x1 - view.x0 - width - at->margin, floorf(at->bottom) });
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextBlur(Prog__FloatingBlur(m));
        // Outlined as the panels are in the sidebar, whose color it shares.
        UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = asking ? theme->accent : at == &stacks[sidebarStack] ? theme->tabBorder : theme->separator });
        UI_Block *card = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "Transfer/%u", i)),
                                            UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        // A click shows where it goes, whichever panel has the keyboard.
        UI_Interaction picked = UI_InteractionFromBlock(card);
        if (picked.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferReveal, i, 0, picked.clickOrder)->panel = 0;
        UI_Parent(card) {
            UI_Block *head = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->menu, 0);
            UI_Parent(head) {
                b32 downloading = transfer->op == Prog_TransferOp_Download || transfer->op == Prog_TransferOp_Torrent || transfer->op == Prog_TransferOp_TorrentFetch;
                String title = downloading ? StrFormat(&state->frameArena, TrF("Downloading into %S"), target)
                             : transfer->op == Prog_TransferOp_CompressVideo ? StrFormat(&state->frameArena, TrN(items, "Compressing %u video", "Compressing %u videos"), (u32)items)
                             : transfer->opening ? StrFormat(&state->frameArena, TrF("Extracting %S to open it"), target)
                             : transfer->move ? StrFormat(&state->frameArena, TrN(items, "Moving %u item to %S", "Moving %u items to %S"), (u32)items, target)
                                              : StrFormat(&state->frameArena, TrN(items, "Copying %u item to %S", "Copying %u items to %S"), (u32)items, target);
                // A narrow card's title has the line to itself, its buttons going beside the name, and its percent beside the amount.
                b32 buttons = !narrow || asking;
                Prog__Label(Prog__Ellipsize(&state->frameArena, m->font, title, MaxF32(0, width - 2 * pad - (buttons ? 2 * m->row : 0))), UI_Flex(), m->row, 0, theme->text, 0);
                if (buttons) Prog__TransferButtons(state, i, paused, m);
            }
            if (asking) {
                String name = StrFromCStr(progress->conflictName);
                Prog__Label(StrFormat(&state->frameArena, TrF("\"%S\" is already in %S"), name, target), UI_Flex(), m->row, 0, theme->textHeading, 0);
                UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->menu, 0);
                UI_Parent(buttons) {
                    struct { String label; P_Conflict answer; b32 shown; } choices[] = {
                        { Tr("Replace"), P_Conflict_Replace, !progress->conflictFolder },
                        { Tr("Keep Both"), P_Conflict_KeepBoth, true },
                        { Tr("Skip"), P_Conflict_Skip, true },
                    };
                    for (u32 c = 0; c < CArrayCount(choices); ++c) {
                        if (!choices[c].shown) continue;
                        UI_Interaction choice = Prog__TextButton(StrFormat(&state->frameArena, "TransferAnswer/%u/%u", i, c), choices[c].label, NULL, UI_TextContent(), m->bar * 0.8f, m->inset * 0.75f,
                                                                 c == 0 ? theme->danger : theme->text, theme->textHeading, theme->textDim, true, m);
                        if (choice.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferAnswer, i, 0, choice.clickOrder)->target = (u64)choices[c].answer;
                        Prog__Spacer(UI_Pixels(m->gap * 0.5f), UI_Pixels(m->bar));
                    }
                    Prog__Spacer(UI_Flex(), UI_Pixels(m->bar));
                    // Only where more names may be taken.
                    if (items > 1) {
                        UI_Interaction all = Prog__TextButton(StrFormat(&state->frameArena, "TransferAll/%u", i), transfer->answerAll ? Tr("For all: on") : Tr("For all: off"), NULL, UI_TextContent(),
                                                              m->bar * 0.8f, m->inset * 0.75f, transfer->answerAll ? theme->textMatch : theme->textDim, theme->text, theme->textDim, true, m);
                        if (all.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferAnswerAll, i, 0, all.clickOrder);
                    }
                }
            }
            // Bytes where there are any to copy; a move within one file system only counts its items.
            f32 done = bytes ? (f32)((f64)doneBytes / (f64)bytes) : (f32)((f64)doneItems / (f64)items);
            String percent = StrFormat(&state->frameArena, paused ? TrF("Paused at %u%%") : "%u%%", (u32)(UI__Clamp(done, 0, 1) * 100));
            if (!asking) {
                UI_Block *now = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->menu, 0);
                UI_Parent(now) {
                    f32 room = width - 2 * pad - (narrow ? 2 * m->row : 2 * m->gap + R_TextWidth(m->font, details) + R_TextWidth(m->font, percent));
                    Prog__Label(Prog__Ellipsize(&state->frameArena, m->font, name, MaxF32(0, room)), UI_TextContent(), m->row, 0, theme->textDim, 0);
                    if (dot.exists && !narrow) Prog__Label(details, UI_TextContent(), m->row, 0, theme->textDim, 0);
                    Prog__Spacer(UI_Flex(), UI_Pixels(m->row));
                    if (narrow) Prog__TransferButtons(state, i, paused, m);
                    else Prog__Label(percent, UI_TextContent(), m->row, m->gap, theme->text, 1);
                }
                if (dot.exists && narrow) {
                    String rest = StrFrom(details, S(" \u00B7 ").length);
                    Prog__Label(Prog__Ellipsize(&state->frameArena, m->font, rest, MaxF32(0, width - 2 * pad)), UI_Flex(), m->row, 0, theme->textDim, 0);
                }
            }
            // A video's progress is in milliseconds of it, shown as times.
            String amount = transfer->op == Prog_TransferOp_CompressVideo && bytes ? StrFormat(&state->frameArena, TrF("%S of %S"), Prog__Clock(&state->frameArena, Min(doneBytes, bytes)),
                                                                                               Prog__Clock(&state->frameArena, bytes))
                          : bytes ? StrFormat(&state->frameArena, TrF("%S of %S"), Prog_FormatSize(&state->frameArena, Min(doneBytes, bytes)), Prog_FormatSize(&state->frameArena, bytes))
                                  : StrFormat(&state->frameArena, TrF("%u of %u"), (u32)Min(doneItems, items), (u32)items);
            // A narrow card's bar is as wide as it is, over the amount.
            f32 lineHeight = narrow ? m->gap : m->row;
            UI_Block *line = Prog__Box(UI_Flex(), UI_Pixels(lineHeight), UI_Layout_Horizontal, theme->menu, 0);
            UI_Parent(line) {
                f32 barWidth = narrow ? width - 2 * pad : floorf(width * 0.62f), barHeight = MaxF32(3, floorf(4 * m->scale));
                UI_SetNextPrefWidth(UI_Pixels(barWidth));
                UI_SetNextPrefHeight(UI_Pixels(lineHeight));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Block *slot = UI_AddBlock(0);
                UI_Parent(slot) {
                    UI_SetNextPrefWidth(UI_Pixels(barWidth));
                    UI_SetNextPrefHeight(UI_Pixels(barHeight));
                    UI_SetNextCornerRadius(barHeight * 0.5f);
        UI_SetNextColors((UI_Colors){ .background = theme->track });
                    UI_Block *track = UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_Clip);
                    UI_Parent(track) {
                        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                        UI_SetNextPrefWidth(UI_Pixels(floorf(barWidth * UI__Clamp(done, 0, 1))));
                        UI_SetNextPrefHeight(UI_Pixels(barHeight));
                        UI_SetNextCornerRadius(barHeight * 0.5f);
                        UI_SetNextColors((UI_Colors){ .background = paused ? theme->textDim : theme->accent });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    }
                }
                if (!narrow) Prog__Label(amount, UI_Flex(), m->row, m->gap, theme->textDim, 1);
            }
            if (narrow) {
                UI_Block *under = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->menu, 0);
                UI_Parent(under) {
                    Prog__Label(amount, UI_Flex(), m->row, 0, theme->textDim, 0);
                    Prog__Label(percent, UI_TextContent(), m->row, 0, theme->text, 1);
                }
            }
        }
    }
    // The torrents waiting for a slot, over the running ones' cards: the first few by name, each with a button that drops it.
    u32 waiting = 0, listed = 0;
    u64 rows[4];
    for (u64 i = 0; i < state->torrents.header.count; ++i) {
        String line = state->torrents.v[i];
        Index tab = StrIndexChar(line, '\t', SSF_CountOnFail);
        if (Prog_TorrentSlot(state, StrFromTo(line, 0, (i64)tab.value)) != UINT32_MAX) continue;
        if (listed < CArrayCount(rows)) rows[listed++] = i;
        ++waiting;
    }
    if (waiting) {
        Prog__CardStack *at = &stacks[inSidebar ? sidebarStack : Min(Prog__CardPanel(state, torrentTarget, view), windowStack)];
        f32 width = floorf(MinF32(m->line * 26, at->corner.x1 - at->corner.x0 - 2 * at->margin));
        f32 height = 2 * pad + m->row * (f32)(1 + listed + (waiting > listed));
        at->bottom -= height + m->gap;
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextFloatingOffset((UI_Vec2){ at->corner.x1 - view.x0 - width - at->margin, floorf(at->bottom) });
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextBlur(Prog__FloatingBlur(m));
        UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = at == &stacks[sidebarStack] ? theme->tabBorder : theme->separator });
        UI_Block *card = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TorrentsWaiting")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        UI_Parent(card) {
            Prog__Label(StrFormat(&state->frameArena, TrN(waiting, "%u torrent waiting to start", "%u torrents waiting to start"), waiting), UI_Flex(), m->row, 0, theme->text, 0);
            for (u32 k = 0; k < listed; ++k) {
                String line = state->torrents.v[rows[k]];
                Index tab = StrIndexChar(line, '\t', SSF_CountOnFail);
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(m->row));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Block *entry = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "TorrentWaiting/%u", (u32)rows[k])), UI_Flag_LeftMouseClickable);
                UI_Interaction picked = UI_InteractionFromBlock(entry);
                if (picked.clicked) Prog_QueueCommand(state, Prog_CommandKind_TorrentReveal, rows[k], 0, picked.clickOrder)->panel = 0;
                UI_Parent(entry) {
                    Prog__Label(Prog__NameStem(Prog_EntryLeaf(StrFromTo(line, 0, (i64)tab.value))), UI_Flex(), m->row, 0, theme->textDim, 0);
                    UI_Interaction drop = Prog__TextButton(StrFormat(&state->frameArena, "TorrentDrop/%u", (u32)rows[k]), Icon_X, m->icons, UI_Pixels(m->row), m->row, 0,
                                                           theme->icon, theme->text, theme->iconDisabled, true, m);
                    if (drop.clicked) Prog_QueueCommand(state, Prog_CommandKind_TorrentDrop, rows[k], 0, drop.clickOrder);
                    Prog__PopupTooltip(state, Tr("Don't download it after all."));
                }
            }
            if (waiting > listed) Prog__Label(StrFormat(&state->frameArena, TrF("and %u more"), waiting - listed), UI_Flex(), m->row, 0, theme->textDim, 0);
        }
    }
    // The sidebar's list stops over its cards, which last, while the notices float over it.
    f32 cards = inSidebar && stacks[sidebarStack].bottom < view.y1 - view.y0 - snug ? view.y1 - view.y0 - stacks[sidebarStack].bottom + snug : 0;
    if (cards != state->sidebarCards) {
        state->sidebarCards = cards;
        UI_KeepAnimating();
    }
    return stacks[inSidebar ? sidebarStack : Min(Prog__CardPanel(state, 0, view), windowStack)];
}

// Asks before a permanent delete, over a dimmed window that takes every click.
function void Prog__DeleteConfirmation(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->confirmingDelete) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_SetNextColors((UI_Colors){ .background = { 0, 0, 0, 0.35f } });
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("DeleteCatcher")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_DeleteCancel, 0, 0, outside.pressOrder);
    String title = state->confirmEmptyTrash ? StrFormat(&state->frameArena, TrF("Permanently delete everything in the %S?"), Prog_PathName(state, state->confirmPaths[0]))
                 : state->confirmCount == 1 ? StrFormat(&state->frameArena, TrF("Permanently delete \"%S\"?"), Prog__FolderName(state->confirmPaths[0]))
                                            : StrFormat(&state->frameArena, TrN(state->confirmCount, "Permanently delete %u item?", "Permanently delete %u items?"), (u32)state->confirmCount);
    f32 width = floorf(MinF32(MaxF32(R_TextWidth(m->font, title) + 2 * m->inset, m->line * 20), view.x1 - view.x0 - 2 * m->inset));
    f32 height = floorf(m->row * 2 + m->bar + m->inset * 2);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ floorf((view.x1 - view.x0 - width) * 0.5f), floorf((view.y1 - view.y0 - height) * 0.4f) });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, m->inset, m->inset, m->inset });
    UI_SetNextCornerRadius(m->radius * 2);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->hover });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("DeletePanel")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        Prog__Label(title, UI_Flex(), m->row, 0, theme->text, 0);
        Prog__Label(Tr("This cannot be undone."), UI_Flex(), m->row, 0, theme->textDim, 0);
        UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->menu, 0);
        UI_Parent(buttons) {
            Prog__Spacer(UI_Flex(), UI_Pixels(m->bar));
            UI_Interaction cancel = Prog__TextButton(S("DeleteCancel"), Tr("Cancel"), NULL, UI_TextContent(), m->bar * 0.8f, m->inset, theme->textHeading, theme->text, theme->textDim, true, m);
            if (cancel.clicked) Prog_QueueCommand(state, Prog_CommandKind_DeleteCancel, 0, 0, cancel.clickOrder);
            Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
            UI_Interaction confirm = Prog__TextButton(S("DeleteConfirm"), Tr("Delete"), NULL, UI_TextContent(), m->bar * 0.8f, m->inset, theme->danger, theme->danger, theme->textDim, true, m);
            if (confirm.clicked) Prog_QueueCommand(state, Prog_CommandKind_DeleteConfirm, 0, 0, confirm.clickOrder);
        }
    }
}

// The menu editor, in the place of the system menu whose pencil opened it: the top level's items in their order, each with dots to drag it by
// and a minus that hides it, then the hidden ones, each with a plus that brings it back. Reset brings back the system's order and every item.
function void Prog__MenuEditor(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->menuEditing) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    f32 s = m->scale;
    R_Rect view = UI->root->rect;
    // A press anywhere else closes it.
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("MenuEditCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_MenuEditClose, 0, 0, outside.pressOrder);
    u64 total = state->menuEditLabels.header.count;
    u32 *shown = ArenaPushArray(frame, total + 1, u32), *hidden = ArenaPushArray(frame, total + 1, u32), shownCount = 0, hiddenCount = 0;
    f32 widest = 0;
    for (u64 i = 0; i < total; ++i) {
        String label = state->menuEditLabels.v[i];
        if (Prog__ListPlace(&state->menuHidden, label) >= 0) hidden[hiddenCount++] = (u32)i;
        else shown[shownCount++] = (u32)i;
        widest = MaxF32(widest, R_TextWidth(m->font, label));
    }
    f32 pad = floorf(6.4f * s + 0.5f), head = floorf(35.2f * s + 0.5f), row = floorf(24.0f * s + 0.5f), grip = floorf(20.8f * s + 0.5f), radius = floorf(10.0f * s + 0.5f);
    f32 button = Prog__ToolbarButtonHeight(m), title = floorf(24.0f * s + 0.5f), chevron = floorf(16.0f * s + 0.5f);
    f32 width = MaxF32(state->menuEditAt.x1 - state->menuEditAt.x0, widest + grip + chevron + button + 4 * pad + m->gap);
    width = floorf(MinF32(MaxF32(width, 260.0f * s), view.x1 - view.x0 - 2 * pad));
    f32 listHeight = (f32)shownCount * row, hiddenHeight = hiddenCount ? m->separator + title + (f32)hiddenCount * row + pad : 0;
    // Below the header, what does not fit in the window scrolls.
    f32 content = pad + listHeight + pad + hiddenHeight, room = MaxF32(row, view.y1 - view.y0 - head - m->separator - 2 * pad);
    f32 bodyHeight = MinF32(content, room);
    b32 scrolls = content > bodyHeight + 0.5f;
    f32 gutter = scrolls ? Prog__ScrollGutter(m) : 0;
    f32 height = head + m->separator + bodyHeight;
    f32 x = floorf(MaxF32(view.x0, MinF32(state->menuEditAt.x0, view.x1 - width))), y = floorf(MaxF32(view.y0, MinF32(state->menuEditAt.y0, view.y1 - height)));
    Prog__DropShadow(x - view.x0, y - view.y0, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, y - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("MenuEditor")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_MouseClickable);
    UI_Parent(box) {
        UI_SetNextChildrenPadding((UI_Padding){ 2 * pad, 0, pad, 0 });
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_Block *header = Prog__Box(UI_Flex(), UI_Pixels(head), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(header) {
            Prog__Icon(Icon_Pencil, grip, head, theme->icon, 0, m);
            Prog__Label(Tr("Edit Menu"), UI_Flex(), head, 0, theme->text, 0);
            b32 changed = state->menuHidden.header.count || state->menuOrder.header.count;
            UI_Interaction reset = Prog__TextButton(S("MenuEditReset"), Tr("Reset"), NULL, UI_TextContent(), button, m->inset, theme->text, theme->text, theme->iconDisabled, changed, m);
            if (reset.clicked) Prog_QueueCommand(state, Prog_CommandKind_MenuEditReset, 0, 0, reset.clickOrder);
            Prog__PopupTooltip(state, Tr("Bring back the system's own order and every item"));
            Prog__Spacer(UI_Pixels(floorf(pad * 0.5f)), UI_Pixels(head));
            UI_Interaction done = Prog__TextButton(S("MenuEditDone"), Tr("Done"), NULL, UI_TextContent(), button, m->inset, theme->contentSelection, theme->contentSelection, theme->iconDisabled, true, m);
            UI->parent->last->props.colors.background = theme->selection;
            UI->parent->last->flags |= UI_Flag_DrawBackground;
            if (done.clicked) Prog_QueueCommand(state, Prog_CommandKind_MenuEditClose, 0, 0, done.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(m->separator), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(bodyHeight));
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, gutter, 0 });
        UI_Block *body = UI_AddBlockFromKey(UI_KeyFromStr(0, S("MenuEditBody")), UI_Flag_Clip | (scrolls ? UI_Flag_Scrollable : 0));
        UI_Block *outer = UI_PushParent(body);
        Prog__Spacer(UI_Flex(), UI_Pixels(pad));
        UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("MenuEditList")), 0);
        list->props.width = UI_Flex(), list->props.height = UI_Pixels(listHeight), list->props.childrenLayout = UI_Layout_Vertical;
        // Where a row dragged by its dots would go: before the row whose middle the pointer is above.
        u32 dragged = state->menuEditDrag ? state->menuEditDrag - 1 : UINT32_MAX;
        u32 landing = (u32)Min(Max((i64)floorf((UI->mouse.y - list->rect.y0) / MaxF32(1, row) + 0.5f), (i64)0), (i64)shownCount);
        b32 anyActive = false;
        UI_Parent(list) {
            for (u32 k = 0; k < shownCount; ++k) {
                u32 index = shown[k];
                String label = state->menuEditLabels.v[index];
                UI_SetNextChildrenPadding((UI_Padding){ pad, 0, pad, 0 });
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                if (k == dragged) UI_SetNextOpacity(0.35f);
                UI_Block *line = Prog__Box(UI_Flex(), UI_Pixels(row), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(line) {
                    UI_Key gripKey = UI_KeyFromStr(0, StrFormat(frame, "MenuEditGrip/%S", label));
                    UI_SetNextPrefWidth(UI_Pixels(grip));
                    UI_SetNextPrefHeight(UI_Pixels(row));
                    UI_Block *handle = UI_AddBlockFromKey(gripKey, UI_Flag_LeftMouseClickable);
                    UI_Interaction drag = UI_InteractionFromBlock(handle);
                    anyActive |= drag.active;
                    if (drag.active && drag.isDragging) state->menuEditDrag = k + 1;
                    if (drag.released && state->menuEditDrag == k + 1) {
                        u64 before = landing < shownCount ? shown[landing] : (shownCount ? (u64)shown[shownCount - 1] + 1 : 0);
                        Prog_QueueCommand(state, Prog_CommandKind_MenuEditMove, index, 0, drag.pressOrder)->target = before;
                        state->menuEditDrag = 0;
                    }
                    UI_Parent(handle) Prog__Icon(Icon_GripVertical, grip, row, UI->hotKey == gripKey || drag.active ? theme->text : theme->textDim, 0.5f, m);
                    Prog__Label(label, UI_Flex(), row, 0, theme->text, 0);
                    if (state->menuEditSubs[index]) Prog__Icon(Icon_ChevronRight, chevron, row, theme->textDim, 0.5f, m);
                    UI_Interaction hide = Prog__TextButton(StrFormat(frame, "MenuEditHide/%S", label), Icon_CircleMinus, m->icons, UI_Pixels(button), button, 0, theme->icon, theme->text,
                                                           theme->iconDisabled, true, m);
                    if (hide.clicked) Prog_QueueCommand(state, Prog_CommandKind_MenuEditToggle, index, 0, hide.clickOrder);
                }
            }
            if (state->menuEditDrag && dragged < shownCount) {
                // The line where it lands, and the row itself carried under the pointer.
                if (landing != dragged && landing != dragged + 1) {
                    UI_SetNextFloatingOffset((UI_Vec2){ pad + grip, (f32)landing * row - floorf(m->stroke) });
                    UI_SetNextPrefWidth(UI_Pixels(width - 2 * pad - grip - gutter));
                    UI_SetNextPrefHeight(UI_Pixels(MaxF32(2, floorf(1.6f * s + 0.5f))));
                    UI_SetNextColors((UI_Colors){ .background = theme->selection });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                }
                f32 carried = MinF32(MaxF32(UI->mouse.y - list->rect.y0 - row * 0.5f, -row * 0.5f), listHeight - row * 0.5f);
                UI_SetNextFloatingOffset((UI_Vec2){ floorf(pad * 0.5f), floorf(carried) });
                UI_SetNextPrefWidth(UI_Pixels(width - pad - gutter));
                UI_SetNextPrefHeight(UI_Pixels(row));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenPadding((UI_Padding){ floorf(pad * 0.5f), 0, pad, 0 });
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextCornerRadius(floorf(5.0f * s + 0.5f));
                UI_SetNextBorderThickness(m->stroke);
                UI_SetNextColors((UI_Colors){ .background = theme->track, .border = theme->selection });
                UI_Block *lifted = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                UI_Parent(lifted) {
                    Prog__Icon(Icon_GripVertical, grip, row, theme->text, 0.5f, m);
                    Prog__Label(state->menuEditLabels.v[shown[dragged]], UI_Flex(), row, 0, theme->text, 0);
                }
            }
        }
        if (!anyActive) state->menuEditDrag = 0;
        Prog__Spacer(UI_Flex(), UI_Pixels(pad));
        if (hiddenCount) {
            Prog__Box(UI_Flex(), UI_Pixels(m->separator), UI_Layout_Horizontal, theme->tabBorder, 0);
            UI_SetNextChildrenPadding((UI_Padding){ 2 * pad, 0, pad, 0 });
            UI_Block *heading = Prog__Box(UI_Flex(), UI_Pixels(title), UI_Layout_Horizontal, (R_Color){0}, 0);
            UI_Parent(heading) {
                UI_SetNextFont(m->detail);
                Prog__Label(StrFormat(frame, TrF("Hidden \xC2\xB7 %u"), hiddenCount), UI_Flex(), title, 0, theme->textDim, 0);
            }
            for (u32 k = 0; k < hiddenCount; ++k) {
                u32 index = hidden[k];
                String label = state->menuEditLabels.v[index];
                UI_SetNextChildrenPadding((UI_Padding){ pad + grip, 0, pad, 0 });
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Block *line = Prog__Box(UI_Flex(), UI_Pixels(row), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(line) {
                    Prog__Label(label, UI_Flex(), row, 0, theme->textDim, 0);
                    UI_Interaction back = Prog__TextButton(StrFormat(frame, "MenuEditShow/%S", label), Icon_CirclePlus, m->icons, UI_Pixels(button), button, 0, theme->accent, theme->text,
                                                           theme->iconDisabled, true, m);
                    if (back.clicked) Prog_QueueCommand(state, Prog_CommandKind_MenuEditToggle, index, 0, back.clickOrder);
                }
            }
            Prog__Spacer(UI_Flex(), UI_Pixels(pad));
        }
        UI_PopParent(outer);
        // The bar goes by the list's last layout, which a list just opened has yet to have, so another frame follows for it.
        if (scrolls && body->rect.y1 <= body->rect.y0) UI_KeepAnimating();
        if (scrolls) Prog__ScrollBar(body, row, S("MenuEdit"), 1, m);
    }
}

function void Prog__ContextMenu(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->menuOpen) {
        MemZero(state->menuSquare, sizeof(state->menuSquare));
        return;
    }
    // Clicks anywhere else close the menu and go no further; a right click goes on to open the menu of what it is on.
    UI_SetNextPrefWidth(UI_Pixels(UI->root->rect.x1 - UI->root->rect.x0));
    UI_SetNextPrefHeight(UI_Pixels(UI->root->rect.y1 - UI->root->rect.y0));
    // The panels blur what is behind the menu as it was before any of them, so that joined ones read as one sheet of glass, with no panel's
    // shadow or tint in another's blur.
    UI_SetNextBlurSource(1);
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("MenuCatcher")), UI_Flag_Floating | UI_Flag_LeftMouseClickable | UI_Flag_MiddleMouseClickable | UI_Flag_TakesWheel);
    UI_Interaction interaction = UI_InteractionFromBlock(catcher);
    if (interaction.pressed) Prog_QueueCommand(state, Prog_CommandKind_ContextMenuClose, 0, 0, interaction.pressOrder);
    state->menuPointerMoved = UI->mouse.x != state->menuPointer.x || UI->mouse.y != state->menuPointer.y;
    state->menuPointer = UI->mouse;
    // Each open level is its own top-level panel, so no panel clips the next.
    Prog_MenuPlace place = { .count = state->menu.rootCount, .x = state->menuAt.x, .y = state->menuAt.y };
    // Each panel's border is drawn once the next level has had its say on it, or once nothing more opens.
    Prog_MenuBorder borders[CArrayCount(state->menuPath) + 1] = {0};
    for (u32 level = 0; level <= CArrayCount(state->menuPath); ++level) {
        Prog_MenuPlace child = {0};
        if (!Prog__MenuPanel(state, level, place, m, &child, &borders[level], level ? &borders[level - 1] : NULL)) {
            if (borders[level].rect.y1 > borders[level].rect.y0) Prog__MenuBorderDraw(&borders[level], m);
            // Nothing joins the last panel.
            for (u32 rest = level; rest < CArrayCount(state->menuSquare); ++rest) state->menuSquare[rest] = 0;
            break;
        }
        place = child;
    }
    UI_SetNextBlurSource(2);
    UI_AddBlock(UI_Flag_Floating);
    state->menuHot = UI->hotKey;
}

// One drop zone at `box` (in the panel's coordinates): a faint frame around a sketch of the panel it would make, one of `parts` across or down,
// the `target`th washed in the selection color and outlined with it, filled with it while the pointer is over the zone.
// Returns whether the pointer is over it.
function b32 Prog__DropZone(R_Rect box, R_Rect panel, u32 parts, b32 across, u32 target, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 width = box.x1 - box.x0, height = box.y1 - box.y0, pad = floorf(7.2f * m->scale + 0.5f), radius = floorf(6.0f * m->scale + 0.5f);
    b32 hot = UI__Contains((R_Rect){ panel.x0 + box.x0, panel.y0 + box.y0, panel.x0 + box.x1, panel.y0 + box.y1 }, UI->mouse);
    UI_SetNextFloatingOffset((UI_Vec2){ box.x0, box.y0 });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextCornerRadius(radius * 1.5f);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->tabBorder });
    UI_Block *frame = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
    UI_Parent(frame) {
        f32 innerWidth = width - 2 * pad, innerHeight = height - 2 * pad;
        f32 part = floorf(((across ? innerWidth : innerHeight) - (f32)(parts - 1) * pad) / (f32)parts);
        for (u32 i = 0; i < parts; ++i) {
            R_Color fill = theme->selection;
            fill.a *= i == target ? (hot ? 1 : theme->hoverAmount.r) : 0;
            UI_SetNextFloatingOffset(across ? (UI_Vec2){ pad + (f32)i * (part + pad), pad } : (UI_Vec2){ pad, pad + (f32)i * (part + pad) });
            UI_SetNextPrefWidth(UI_Pixels(across ? part : innerWidth));
            UI_SetNextPrefHeight(UI_Pixels(across ? innerHeight : part));
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(m->stroke);
            UI_SetNextColors((UI_Colors){ .background = fill, .border = i == target ? theme->selection : theme->tabBorder });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
        }
    }
    return hot;
}

// While a tab is dragged over a panel, its drop zones: beside it on the left and right with into it between them in the middle, above it at the top and below it at the bottom.
// The tab's own panel has no "into", which would leave it where it is, and none at all while the tab is its only one, which a split would only move.
function void Prog__DropZones(Prog_State *state, R_Rect panel, f32 top, const Prog_Metrics *m)
{
    b32 own = Prog__PanelId(state) == state->tabDrop.fromPanel;
    if (own && state->tabCount <= 1) return;
    f32 s = m->scale, width = panel.x1 - panel.x0, height = panel.y1 - panel.y0;
    f32 side = floorf(70 * s), middle = floorf(96 * s), tall = floorf(94 * s), gap = floorf(12 * s), wide = floorf(118 * s), low = floorf(66 * s);
    f32 cx = floorf(width * 0.5f), cy = floorf(top + (height - top) * 0.5f);
    struct { R_Rect box; u32 parts; b32 across; u32 target; Prog_TabDrop drop; } zones[] = {
        { { cx - middle * 0.5f - gap - side, cy - tall * 0.5f, cx - middle * 0.5f - gap, cy + tall * 0.5f }, 2, true, 0, Prog_TabDrop_Left },
        { { cx - middle * 0.5f, cy - tall * 0.5f, cx + middle * 0.5f, cy + tall * 0.5f }, 1, true, 0, Prog_TabDrop_Into },
        { { cx + middle * 0.5f + gap, cy - tall * 0.5f, cx + middle * 0.5f + gap + side, cy + tall * 0.5f }, 2, true, 1, Prog_TabDrop_Right },
        { { cx - wide * 0.5f, top, cx + wide * 0.5f, top + low }, 2, false, 0, Prog_TabDrop_Top },
        { { cx - wide * 0.5f, height - low - gap, cx + wide * 0.5f, height - gap }, 2, false, 1, Prog_TabDrop_Bottom },
    };
    // The part of the panel the tab would take, washed under the zones while the pointer is over one: the body's half or all of it, and but for the bottom half a tab's height of the strip above it, shaped as a tab.
    for (u32 i = 0; i < CArrayCount(zones); ++i) {
        R_Rect box = zones[i].box;
        if ((own && zones[i].drop == Prog_TabDrop_Into) || !UI__Contains((R_Rect){ panel.x0 + box.x0, panel.y0 + box.y0, panel.x0 + box.x1, panel.y0 + box.y1 }, UI->mouse)) continue;
        Prog_TabDrop drop = zones[i].drop;
        f32 halfWidth = floorf(width * 0.5f), halfHeight = floorf(height * 0.5f);
        f32 x0 = drop == Prog_TabDrop_Right ? halfWidth : 0, x1 = drop == Prog_TabDrop_Left ? halfWidth : width;
        UI_Block *dragged = UI__Find(state->tabDragKey);
        f32 tab = dragged ? dragged->rect.y1 - dragged->rect.y0 : m->bar;
        const Prog_Theme *theme = Prog_CurrentTheme();
        R_Color fill = theme->selection;
        fill.a *= theme->hoverAmount.r * (2.0f / 3.0f);
        struct { R_Rect rect; u32 shape; } parts[2] = {
            { { x0, -tab, x1, 0 }, R_Shape_SquareBottomLeft | R_Shape_SquareBottomRight },
            { { x0, drop == Prog_TabDrop_Bottom ? halfHeight : 0, x1, drop == Prog_TabDrop_Top ? halfHeight : height }, R_Shape_SquareTopLeft | R_Shape_SquareTopRight },
        };
        for (u32 p = drop == Prog_TabDrop_Bottom ? 1 : 0; p < 2; ++p) {
            UI_SetNextFloatingOffset((UI_Vec2){ parts[p].rect.x0, parts[p].rect.y0 });
            UI_SetNextPrefWidth(UI_Pixels(parts[p].rect.x1 - parts[p].rect.x0));
            UI_SetNextPrefHeight(UI_Pixels(parts[p].rect.y1 - parts[p].rect.y0));
            UI_SetNextCornerRadius(m->radius);
            UI_SetNextShape(drop == Prog_TabDrop_Bottom ? 0 : parts[p].shape);
            UI_SetNextBorderThickness(m->stroke);
            UI_SetNextColors((UI_Colors){ .background = fill, .border = theme->tabBorder });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
        }
        break;
    }
    for (u32 i = 0; i < CArrayCount(zones); ++i) {
        if (own && zones[i].drop == Prog_TabDrop_Into) continue;
        if (Prog__DropZone(zones[i].box, panel, zones[i].parts, zones[i].across, zones[i].target, m)) {
            state->tabDrop.kind = (u8)zones[i].drop;
            state->tabDrop.panel = Prog__PanelId(state);
        }
    }
}

// The dragged tab under the pointer, over what letting go does there.
function void Prog__TabGhost(Prog_State *state, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    u32 from = Prog_PanelIndex(state, state->tabDrop.fromPanel);
    if (from == UINT32_MAX) return;
    Prog__UsePanel(state, from);
    String path = state->tabDrop.fromSlot < state->tabCount ? Prog_TabPath(state, state->tabDrop.fromSlot) : (String){0};
    Prog__UsePanel(state, state->activePanel);
    b32 copy = (UI->mods & KeyMod_Control) != 0;
    String action = state->tabDrop.kind == Prog_TabDrop_Window && state->tabDropOther ? (copy ? Tr("Copy to that window") : Tr("Move to that window"))
                  : state->tabDrop.kind == Prog_TabDrop_Window ? (copy ? Tr("Copy to a new window") : Tr("Move to a new window"))
                  : copy ? Tr("Copy") : Tr("Move");
    // While it moves, the key that copies follows in a key chip.
    String key = NoTr("Ctrl"), hint = Tr("to copy");
    f32 chip = floorf(4.8f * m->scale + 0.5f) * 2 + R_TextWidth(m->detail, key);
    f32 words = R_TextWidth(m->font, action) + (copy ? 0 : 2 * m->gap + MaxF32(chip, floorf(30.4f * m->scale + 0.5f)) + R_TextWidth(m->font, hint));
    // Wide enough for what it says, and kept inside the window, which it cannot draw outside of.
    f32 width = MaxF32(floorf(m->line * 12.0f), words + 2 * m->gap), height = floorf(m->bar * 0.85f), radius = floorf(8.0f * scale + 0.5f);
    // A translucent theme's panels would leave it unreadable over the list, so it takes the menus' color there.
    R_Color face = theme->panel.a < 1 ? theme->menu : theme->panel, foot = theme->background.a < 1 ? theme->menu : theme->background;
    R_Rect view = UI->root->rect;
    // Outside the window it is a window of its own over the others, at the window's top left to be drawn on its own, and only the tab.
    b32 outside = !UI__Contains(view, UI->mouse);
    f32 x = UI__Clamp(floorf(UI->mouse.x - view.x0 - width * 0.3f), 0, MaxF32(0, view.x1 - view.x0 - width));
    f32 y = UI__Clamp(floorf(UI->mouse.y - view.y0 + m->gap), 0, MaxF32(0, view.y1 - view.y0 - height - m->row));
    UI_SetNextFloatingOffset(outside ? (UI_Vec2){ 0, 0 } : (UI_Vec2){ x, y });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(outside ? height : height + m->row));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_Block *ghost = UI_AddBlock(UI_Flag_Floating | (outside ? UI_Flag_Detached : 0));
    if (outside) {
        state->tabGhost = ghost;
        state->tabGhostHot = (UI_Vec2){ floorf(width * 0.3f), -m->gap };
    }
    UI_Parent(ghost) {
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextChildrenPadding((UI_Padding){ floorf(14.0f * scale), 0, m->gap, 0 });
        // With what letting go does under it, the two are one card.
        UI_SetNextCornerRadius(radius);
        UI_SetNextShape(outside ? 0 : R_Shape_SquareBottomLeft | R_Shape_SquareBottomRight);
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextColors((UI_Colors){ .background = face, .border = theme->tabBorder });
        UI_Block *tab = UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder);
        UI_Parent(tab) {
            const Prog_Icon *icon = path.length ? Prog_FolderIcon(state, path) : NULL;
            if (icon) {
                UI_SetNextPrefWidth(UI_Pixels((f32)state->iconSize + m->gap));
                UI_SetNextPrefHeight(UI_Pixels(height));
                UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, (R_Color){ 1, 1, 1, 1 });
                UI_AddBlock(UI_Flag_DrawImage);
            } else {
                Prog__Icon(Icon_Folder, (f32)state->iconSize + m->gap, height, theme->icon, 0, m);
            }
            Prog__Label(Prog_PathName(state, path), UI_Flex(), height, 0, theme->text, 0);
        }
        // Outside the window only the tab shows, with nothing to say where it goes.
        if (!outside) {
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextCornerRadius(radius);
            UI_SetNextShape(R_Shape_SquareTopLeft | R_Shape_SquareTopRight);
            UI_SetNextBorderThickness(m->separator);
            UI_SetNextColors((UI_Colors){ .background = foot, .border = theme->tabBorder });
            UI_Block *footer = UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder);
            UI_Parent(footer) {
                Prog__Spacer(UI_Flex(), UI_Pixels(m->row));
                Prog__Label(action, UI_TextContent(), m->row, 0, theme->text, 0);
                if (!copy) {
                    Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->row));
                    Prog__Chip(key, m->row, false, false, m);
                    Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->row));
                    Prog__Label(hint, UI_TextContent(), m->row, 0, theme->text, 0);
                }
                Prog__Spacer(UI_Flex(), UI_Pixels(m->row));
            }
        }
    }
}

// The character cells `text` takes, with tabs to the next stop of four.
function u32 Prog__TextCells(String text)
{
    u32 cells = 0;
    for (u64 i = 0; i < text.length; ++i) {
        u8 c = (u8)text.value[i];
        if (c == '\t') cells += 4 - cells % 4;
        else if ((c & 0xC0) != 0x80) ++cells;
    }
    return cells;
}

// The offset in `text`, one row of the inspector, nearest to `cell` cells from its start, between its characters.
function u64 Prog__OffsetAtCell(String text, f32 cell)
{
    u32 cells = 0;
    for (u64 i = 0; i < text.length;) {
        u32 width = text.value[i] == '\t' ? 4 - cells % 4 : 1;
        if (cell < (f32)cells + (f32)width * 0.5f) return i;
        cells += width;
        do ++i; while (i < text.length && ((u8)text.value[i] & 0xC0) == 0x80);
    }
    return text.length;
}

// Letters, digits and underscores, and any byte of a character past ASCII, which a double click selects runs of.
function b32 Prog__WordByte(u8 c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
}

// The rows `line` takes wrapped at `columns` cells: a space that does not fit starts the next row, and a word that does not is moved to it whole,
// unless it is the row's only one. `starts`, when not NULL, gets where each of the first `room` rows starts.
function u32 Prog__WrapLine(String line, u32 columns, u32 *starts, u32 room)
{
    u32 rows = 1, cells = 0;
    u64 rowStart = 0, breakAt = 0;
    if (starts && room) starts[0] = 0;
    for (u64 i = 0; i < line.length;) {
        u8 c = (u8)line.value[i];
        u64 next = i + 1;
        while (next < line.length && ((u8)line.value[next] & 0xC0) == 0x80) ++next;
        u32 width = c == '\t' ? 4 - cells % 4 : 1;
        if (cells && cells + width > columns) {
            u64 at = c != ' ' && c != '	' && breakAt > rowStart ? breakAt : i;
            if (starts && rows < room) starts[rows] = (u32)at;
            ++rows;
            rowStart = breakAt = at;
            cells = Prog__TextCells(StrFromTo(line, at, (i64)i));
            width = c == '\t' ? 4 - cells % 4 : 1;
        }
        cells += width;
        if (c == ' ' || c == '\t') breakAt = next;
        i = next;
    }
    return rows;
}

// One row of the inspector's text in `font`, `cell` wide per character: tabs to their stops, with `marks` spaces as dots and tabs as dashes,
// and the bytes flagged in `matched`, when not NULL, in the match color.
// A code token's color: lighter hues on a dark theme and deeper ones on a light one, told apart by how bright its text is.
function R_Color Prog__TokenColor(const Prog_Theme *theme, u8 kind)
{
    switch (kind) {
        case Prog_Token_Keyword: return theme->codeKeyword;
        case Prog_Token_String: return theme->codeString;
        case Prog_Token_Comment: return theme->textDim;
        case Prog_Token_Number: return theme->codeNumber;
        case Prog_Token_Heading: return theme->codeHeading;
        default: return theme->text;
    }
}

function void Prog__InspectorRow(Prog_State *state, String text, const u8 *matched, const u8 *tokens, const R_Font *font, f32 cell, f32 height, b32 marks, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    u32 cells = 0;
    for (u64 start = 0; start < text.length;) {
        u8 c = (u8)text.value[start];
        if (c == '\t') {
            u32 width = 4 - cells % 4;
            cells += width;
            ++start;
            UI_SetNextPrefWidth(UI_Pixels((f32)width * cell));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_Block *tab = UI_AddBlock(0);
            if (marks) UI_Parent(tab) {
                // A dash from the tab's first cell, short of the next stop.
                f32 thick = MaxF32(1, floorf(1.6f * m->scale + 0.5f));
                UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((height - thick + 1) * 0.5f) });
                UI_SetNextPrefWidth(UI_Pixels((f32)width * cell - floorf(cell * 0.67f)));
                UI_SetNextPrefHeight(UI_Pixels(thick));
                UI_SetNextColors((UI_Colors){ .background = theme->textDim });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
            continue;
        }
        // A run of spaces, as dots when marked, or of everything else up to the next tab or space, or to where the filter's letters start or stop.
        b32 spaces = c == ' ', match = matched && matched[start];
        u8 token = tokens ? tokens[start] : 0;
        u64 end = start;
        while (end < text.length && text.value[end] != '\t' && (text.value[end] == ' ') == spaces && (matched && matched[end]) == match && (tokens ? tokens[end] : 0) == token) ++end;
        String run = StrFromTo(text, start, (i64)end);
        u32 runCells = Prog__TextCells(run);
        cells += runCells;
        start = end;
        if (spaces && !marks) {
            Prog__Spacer(UI_Pixels((f32)runCells * cell), UI_Pixels(height));
            continue;
        }
        StrBuilder dots = {0};
        if (spaces) for (u64 i = 0; i < run.length; ++i) StrBuildStr(&state->frameArena, &dots, S("·"));
        UI_SetNextFont(font);
        Prog__Label(spaces ? dots.buffer : run, UI_Pixels((f32)runCells * cell), height, 0, spaces ? theme->textDim : match ? theme->textMatch : Prog__TokenColor(theme, token), 0);
    }
}


// How a byte of the hex view looks: its kind, zero, a printable character or neither, and how bright its value makes it, in four steps.
function u32 Prog__HexLook(u8 byte)
{
    return byte == 0 ? 0 : (byte >= 0x20 && byte < 0x7F ? 8u : 16u) + 1u + (byte >> 6);
}

function R_Color Prog__HexInk(const Prog_Theme *theme, u32 look)
{
    R_Color color = look == 0 ? theme->textDim : look < 16 ? theme->text : theme->textMatch;
    color.a *= look == 0 ? 0.5f : 0.55f + 0.15f * (f32)((look & 7) - 1);
    return color;
}

// A scroll bar for more than the view's block holds, as a big file's: the thumb `share` of the way down its track and `visible` of it long;
// true once it is let go after a drag, with where it was left in `target`, as a share of the way.
function b32 Prog__WholeScrollBar(UI_Block *view, f32 share, f32 visible, String name, f32 *target, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale, viewHeight = view->rect.y1 - view->rect.y0;
    if (viewHeight <= 0 || visible >= 1) return false;
    f32 head = floorf(31.2f * s + 0.5f), foot = floorf(13.6f * s + 0.5f), track = MaxF32(0, viewHeight - head - foot);
    f32 thumb = MaxF32(floorf(track * visible), floorf(16.0f * s + 0.5f)), thick = floorf(11.2f * s + 0.5f);
    // Where the thumb was taken, and where it is dragged to while it is, of the bar dragged.
    local_persist f32 grab, held = -1;
    local_persist UI_Key heldKey;
    UI_Key key = UI_KeyFromStr(0, StrFormat(UI->frameArena, "%S/ScrollThumb", name));
    b32 holding = held >= 0 && heldKey == key;
    f32 place = floorf((track - thumb) * MinF32(MaxF32(holding ? held : share, 0), 1));
    f32 x = view->rect.x1 - UI->parent->rect.x0 - thick - floorf(2.4f * s + 0.5f), y = view->rect.y0 - UI->parent->rect.y0;
    UI_SetNextFloatingOffset((UI_Vec2){ x, y + head + place });
    UI_SetNextPrefWidth(UI_Pixels(thick));
    UI_SetNextPrefHeight(UI_Pixels(thumb));
    UI_SetNextCornerRadius(floorf(thick * 0.5f));
    UI_SetNextColors((UI_Colors){ .background = theme->track });
    UI_Block *bar = UI_AddBlockFromKey(key, UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_LeftMouseClickable);
    UI_Interaction drag = UI_InteractionFromBlock(bar);
    if (drag.pressed) grab = UI->mouse.y - (view->rect.y0 + head + place);
    if (drag.active && track > thumb) {
        held = MinF32(MaxF32((UI->mouse.y - grab - view->rect.y0 - head) / (track - thumb), 0), 1);
        heldKey = key;
    }
    if (drag.released && holding) {
        *target = held;
        held = -1;
        return true;
    }
    return false;
}

// The inspector's hex view: the file's bytes in rows, as many to a row as fit, each colored by its kind and brighter the higher its value,
// zeros at half strength, from pages read as they come into sight. It holds up to Prog_HexRows rows from hexBase at a time, moved along as
// the view nears either end, with a scroll bar for the whole file. Dragging over either column selects bytes, which Copy takes as hex, and
// the peek bar under the rows reads those from the selection's start as numbers.
function void Prog__InspectorHex(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const R_Font *font = state->monoFont ? state->monoFont : m->font;
    f32 s = m->scale, size = (f32)(state->inspectorFontSize ? state->inspectorFontSize : Prog_InspectorFontSize);
    f32 pitch = floorf((size + 6.8f) * s + 0.5f), cell = R_TextWidth(font, S("M"));
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    f32 margin = floorf(32.8f * s + 0.5f), left = floorf(9.6f * s + 0.5f), above = floorf(13.6f * s + 0.5f);
    UI_SetNextChildrenPadding((UI_Padding){ 0, above, margin, floorf(4.0f * s + 0.5f) });
    UI_Block *view = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorText")), UI_Flag_Clip | UI_Flag_Scrollable | UI_Flag_LeftMouseClickable);
    UI_Interaction press = UI_InteractionFromBlock(view);
    b32 dragging = UI->activeKey == view->key;
    if (UI->hotKey == view->key || dragging) state->cursor = PlatformCursor_Text;
    if (state->inspectorResetScroll) {
        view->scrollOffset = view->scrollShown = (UI_Vec2){0};
        state->hexBase = 0;
        state->inspectorResetScroll = false;
    }
    if (!UI->animate) view->scrollShown = view->scrollOffset;
    UI_LimitScrollEase(view);
    // A view new this frame has no size yet, so its rows in sight come in the next.
    if (view->rect.y1 <= view->rect.y0) UI_KeepAnimating();
    u64 total = state->hexSize;
    u32 places = Prog_HexPlaces(total);
    u32 perLine = Prog_HexPerLine(places, (view->rect.x1 - view->rect.x0 - left - margin) / cell);
    u64 totalRows = (total + perLine - 1) / perLine, span = Prog_HexRows, lastBase = totalRows > span ? totalRows - span : 0;
    u64 baseRow = Min(state->hexBase / perLine, lastBase);
    f32 height = MaxF32(0, view->rect.y1 - view->rect.y0);
    u64 seen = (u64)(height / pitch) + 1;
    // A find's match, or a place the scroll bar was let go at, comes to the view's middle.
    if (state->hexJumpTo != UINT64_MAX) {
        u64 row = Min(state->hexJumpTo / perLine, totalRows), top = row > seen / 2 ? row - seen / 2 : 0;
        baseRow = Min(top > span / 2 ? top - span / 2 : 0, lastBase);
        view->scrollOffset.y = view->scrollShown.y = (f32)(top - baseRow) * pitch;
        state->hexJumpTo = UINT64_MAX;
    }
    // Near either end of the rows it holds, the window moves to have the top row in its middle, and the scroll back by as many rows.
    u64 topRow = baseRow + (u64)MaxF32(0, floorf(view->scrollShown.y / pitch)), reach = span / 8, windowRows = Min(totalRows - baseRow, span);
    if ((baseRow > 0 && topRow < baseRow + reach) || (baseRow < lastBase && topRow + seen + reach > baseRow + windowRows)) {
        u64 rebased = Min(topRow > span / 2 ? topRow - span / 2 : 0, lastBase);
        f32 shift = (f32)((i64)baseRow - (i64)rebased) * pitch;
        view->scrollOffset.y = MaxF32(0, view->scrollOffset.y + shift);
        view->scrollShown.y = MaxF32(0, view->scrollShown.y + shift);
        baseRow = rebased;
        windowRows = Min(totalRows - baseRow, span);
    }
    state->hexBase = baseRow * perLine;
    f32 from = MinF32(view->scrollOffset.y, view->scrollShown.y), to = MaxF32(view->scrollOffset.y, view->scrollShown.y) + height;
    u64 first = Min((u64)MaxF32(0, floorf(from / pitch)), windowRows), last = Min((u64)MaxF32(0, ceilf(to / pitch)) + 1, windowRows);
    // The byte under the pointer, of the rows in sight, and the one it was pressed on: a press selects that, and a drag from there the bytes
    // up to the one it is over.
    u64 hit = UINT64_MAX, pressedOn = UINT64_MAX;
    for (u32 which = 0; which < 2 && last > first && total; ++which) {
        UI_Vec2 at = which ? UI->pressOrigin : UI->mouse;
        f32 pointerRow = floorf((at.y - view->rect.y0 - above + view->scrollShown.y) / pitch);
        u64 pointed = baseRow + (u64)MinF32(MaxF32(pointerRow, (f32)first), (f32)(last - 1));
        u64 byte = Min(pointed * perLine + Prog_HexByteAt(places, perLine, (at.x - view->rect.x0 - left) / cell), total - 1);
        if (which) pressedOn = byte;
        else hit = byte;
    }
    if (pressedOn != UINT64_MAX && press.pressed) {
        state->hexPress = hit = pressedOn;
        state->previewAnchor = hit;
        state->previewCaret = hit + 1;
        state->previewTextFocus = true;
    } else if (hit != UINT64_MAX && dragging) {
        state->previewAnchor = Min(state->hexPress, hit);
        state->previewCaret = Max(state->hexPress, hit) + 1;
        f32 over = UI->mouse.y < view->rect.y0 ? UI->mouse.y - view->rect.y0 : UI->mouse.y > view->rect.y1 ? UI->mouse.y - view->rect.y1 : 0;
        if (over != 0) {
            view->scrollOffset.y = MaxF32(0, view->scrollOffset.y + over * 0.25f);
            UI_KeepAnimating();
        }
    }
    u64 selectFrom = Min(state->previewAnchor, state->previewCaret), selectTo = Min(Max(state->previewAnchor, state->previewCaret), total);
    Arena *frame = &state->frameArena;
    UI_Parent(view) {
        Prog__Spacer(UI_Flex(), UI_Pixels((f32)first * pitch));
        for (u64 row = first; row < last; ++row) {
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(pitch));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_Block *rowBlock = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(rowBlock) {
                Prog__Spacer(UI_Pixels(left), UI_Pixels(pitch));
                u64 offset = (baseRow + row) * perLine, count = Min((u64)perLine, total - offset);
                const u8 *page = state->hexPages[offset / Prog_HexPageBytes];
                // A page not read yet is asked for, one at a time, and its rows stay empty until it comes.
                if (!page) {
                    if (!state->hexPageJob) {
                        P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
                        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
                        input->kind = Prog_JobKind_HexPage;
                        input->panel = Prog__PanelId(state);
                        input->path = StrCopy(&job->arena, state->previewPath);
                        input->offset = offset / Prog_HexPageBytes * Prog_HexPageBytes;
                        job->input = input;
                        state->hexPageJob = job;
                        P_JobSubmit(job);
                    }
                    continue;
                }
                const u8 *bytes = page + offset % Prog_HexPageBytes;
                // The selected bytes of the row, behind their hex and their characters.
                u64 a = Max(selectFrom, offset), b = Min(selectTo, offset + count);
                if (a < b) {
                    u64 spans[2][2] = { { Prog_HexByteColumn(places, a - offset), Prog_HexByteColumn(places, b - 1 - offset) + 2 },
                                        { Prog_HexCharColumn(places, perLine, a - offset), Prog_HexCharColumn(places, perLine, b - offset) } };
                    for (u32 part = 0; part < 2; ++part) {
                        UI_SetNextFloatingOffset((UI_Vec2){ left + (f32)spans[part][0] * cell, 0 });
                        UI_SetNextPrefWidth(UI_Pixels((f32)(spans[part][1] - spans[part][0]) * cell));
                        UI_SetNextPrefHeight(UI_Pixels(pitch));
                        UI_SetNextColors((UI_Colors){ .background = theme->selection });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    }
                }
                String text = Prog_HexRowText(frame, bytes, count, offset, perLine, places);
                UI_SetNextFont(font);
                Prog__Label(StrFromTo(text, 0, places), UI_Pixels((f32)(places + 2) * cell), pitch, 0, theme->textDim, 0);
                // Runs of bytes that look alike share a label, in the hex columns and then in the characters.
                for (u32 part = 0; part < 2; ++part) {
                    for (u64 at = 0; at < count;) {
                        u32 look = Prog__HexLook(bytes[at]);
                        u64 end = at + 1;
                        while (end < count && Prog__HexLook(bytes[end]) == look) ++end;
                        u64 start = part ? Prog_HexCharColumn(places, perLine, at) : Prog_HexByteColumn(places, at);
                        // The last hex run takes the spaces up to the characters, which keeps a short last row's characters under those above.
                        u64 stop = part ? Prog_HexCharColumn(places, perLine, end) : end == count ? Prog_HexCharColumn(places, perLine, 0) : Prog_HexByteColumn(places, end);
                        UI_SetNextFont(font);
                        Prog__Label(StrFromTo(text, start, (i64)stop), UI_Pixels((f32)(stop - start) * cell), pitch, 0, Prog__HexInk(theme, look), 0);
                        at = end;
                    }
                }
            }
        }
        Prog__Spacer(UI_Flex(), UI_Pixels((f32)(windowRows - last) * pitch));
    }
    // A file of more rows than the view holds has a scroll bar for all of them.
    if (totalRows > span) {
        f32 target = 0, share = (f32)((f64)(baseRow + (u64)(view->scrollShown.y / pitch)) / (f64)Max(totalRows > seen ? totalRows - seen : 0, (u64)1));
        if (Prog__WholeScrollBar(view, share, (f32)((f64)seen / (f64)totalRows), S("InspectorText"), &target, m)) {
            state->hexJumpTo = ((u64)((f64)target * (f64)(totalRows > seen ? totalRows - seen : 0)) + seen / 2) * perLine;
        }
    } else {
        Prog__ScrollBar(view, pitch, S("InspectorText"), 1, m);
    }
    // The peek bar: the bytes from the selection's start as numbers, little-endian, as many of them as there are, and else how to get them.
    StrBuilder peek = {0};
    u8 held[8] = {0};
    u32 have = 0;
    for (; selectFrom < selectTo && have < 8 && selectFrom + have < total; ++have) {
        const u8 *page = state->hexPages[(selectFrom + have) / Prog_HexPageBytes];
        if (!page) break;
        held[have] = page[(selectFrom + have) % Prog_HexPageBytes];
    }
    if (have) {
        u64 value = 0;
        for (u32 i = 0; i < have; ++i) value |= (u64)held[i] << (8 * i);
        StrBuildStr(frame, &peek, StrFormat(frame, "%llX", (unsigned long long)selectFrom));
        for (u32 width = 1; width <= 8 && width <= have; width *= 2) {
            u64 part = width == 8 ? value : value & ((UINT64_C(1) << (8 * width)) - 1);
            StrBuildStr(frame, &peek, StrFormat(frame, "    u%u %llu", width * 8, (unsigned long long)part));
        }
        f32 single = 0;
        f64 twice = 0;
        if (have >= 4) MemCopy(&single, held, 4), StrBuildStr(frame, &peek, StrFormat(frame, "    f32 %g", (f64)single));
        if (have >= 8) MemCopy(&twice, held, 8), StrBuildStr(frame, &peek, StrFormat(frame, "    f64 %g", twice));
    }
    UI_SetNextChildrenPadding((UI_Padding){ left, 0, margin, 0 });
    UI_Block *bar = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->inner, UI_Flag_Clip);
    UI_Parent(bar) {
        UI_SetNextFont(have ? font : m->detail);
        Prog__Label(have ? peek.buffer : Tr("Select bytes to read them as numbers"), UI_Flex(), m->row, 0, theme->textDim, 0);
    }
}

// Which of the inspector text's shown lines holds `row`: the row itself without wrapping, else found among the lines' first rows.
function u32 Prog__TextRowLine(const Prog_State *state, u32 row, u32 columns, u32 shownCount)
{
    if (!columns) return row;
    u32 low = 0, high = shownCount;
    while (low + 1 < high) {
        u32 middle = (low + high) / 2;
        if (state->previewRows[middle] <= row) low = middle;
        else high = middle;
    }
    return low;
}

// A big text file's scroll bar, for the whole file rather than the window it holds: the thumb sits where the top line is in the file, by its
// bytes, and let go elsewhere, the view goes there, in the window when it holds that place, else at the count's step there once counted.
function void Prog__TextScrollBar(Prog_State *state, UI_Block *view, f32 pitch, u32 top, u32 columns, const Prog_Metrics *m)
{
    u32 count = state->previewLineCount;
    String text = state->previewText;
    f32 viewHeight = view->rect.y1 - view->rect.y0;
    if (viewHeight <= 0 || !state->previewSize || !count) return;
    u64 at = state->previewOffset + state->previewLines[Min(top, count - 1)];
    f64 lines = state->textIndex.done ? (f64)state->textIndex.lines : (f64)state->previewSize * (f64)count / (f64)Max(text.length, (u64)1);
    f32 target = 0;
    if (!Prog__WholeScrollBar(view, (f32)((f64)at / (f64)state->previewSize), (f32)MinF64(1, (f64)viewHeight / (lines * (f64)pitch)), S("InspectorText"), &target, m)) return;
    u64 place = (u64)((f64)target * (f64)state->previewSize);
    if (place >= state->previewOffset && place < state->previewOffset + text.length) {
        u32 low = 0, high = count;
        while (low + 1 < high) {
            u32 middle = (low + high) / 2;
            if (state->previewOffset + state->previewLines[middle] <= place) low = middle;
            else high = middle;
        }
        view->scrollOffset.y = view->scrollShown.y = (f32)(columns && state->previewRows ? state->previewRows[low] : low) * pitch;
    } else if (state->textIndex.done && state->textIndex.count) {
        u64 step = Min(place / Prog_TextStep, state->textIndex.count - 1);
        Prog_RequestTextWindow(state, state->textIndex.offsets[step], state->textIndex.numbers[step], state->textIndex.states[step], true);
    }
}

// The inspector's text: its lines in the monospace font, numbered in a gutter unless hidden, wrapped to the width when asked.
// Only the rows in sight get blocks, with spacers for the rest.
function void Prog__InspectorText(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const R_Font *font = state->monoFont ? state->monoFont : m->font;
    f32 s = m->scale, size = (f32)(state->inspectorFontSize ? state->inspectorFontSize : Prog_InspectorFontSize);
    f32 pitch = floorf((size + 6.8f) * s + 0.5f), cell = R_TextWidth(font, S("M"));
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    // The text stops short of the right edge, leaving the scroll bar room.
    f32 margin = floorf(32.8f * s + 0.5f);
    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf(13.6f * s + 0.5f), margin, floorf(4.0f * s + 0.5f) });
    // Its text is selected by dragging over it, a word by a double click and a line by a third.
    UI_Block *view = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorText")), UI_Flag_Clip | UI_Flag_Scrollable | UI_Flag_LeftMouseClickable);
    UI_Interaction press = UI_InteractionFromBlock(view);
    b32 dragging = UI->activeKey == view->key;
    state->inspectorTextRect = view->rect;
    if (UI->hotKey == view->key || dragging) state->cursor = PlatformCursor_Text;
    if (state->inspectorResetScroll) {
        view->scrollOffset = view->scrollShown = (UI_Vec2){0};
        state->inspectorResetScroll = false;
    }
    if (!UI->animate) view->scrollShown = view->scrollOffset;
    UI_LimitScrollEase(view);
    if (view->rect.y1 <= view->rect.y0) UI_KeepAnimating();
    // A big file's filter looks through the whole file in a job, whose lines show in place of the window once found; until then, the
    // window's that it keeps. A filter changed or cleared puts the window back where it was.
    String filter = Prog_EditText(&state->inspectorFilter);
    if (state->previewWindowed) {
        u32 version = state->inspectorFilter.version;
        if (state->textFound && (!filter.length || state->textFind.version != version)) {
            Prog_ShowTextWindow(state);
            view->scrollOffset.y = view->scrollShown.y = state->textSavedScroll;
        }
        if (filter.length && state->textFindAsked != version) Prog_RequestTextFind(state);
        if (filter.length && !state->textFound && state->textFind.done && state->textFind.version == version) {
            state->textSaved = (Prog_FoundLines){ .text = state->previewText, .lines = state->previewLines, .states = state->previewLineStates, .count = state->previewLineCount };
            state->textSavedScroll = view->scrollOffset.y;
            state->previewText = state->textFind.text;
            state->previewLines = state->textFind.lines;
            state->previewLineStates = state->textFind.states;
            state->previewLineCount = state->textFind.count;
            state->previewNumbers = state->textFind.numbers;
            state->previewKept = state->previewRows = NULL;
            state->previewAnchor = state->previewCaret = 0;
            state->textFound = true;
            view->scrollOffset.y = view->scrollShown.y = 0;
        }
    }
    String text = state->previewText;
    // What is worked out of a big file's window goes with it.
    Arena *textArena = state->previewWindowed ? &state->textWindowArena : &state->previewArena;
    u64 selectFrom = Min(state->previewAnchor, state->previewCaret), selectTo = Max(state->previewAnchor, state->previewCaret);
    u32 lineCount = state->previewLineCount;
    // The lines the filter keeps, picked again when it changes; the rows are then worked out again too.
    if (!state->previewKept || state->previewKeptVersion != state->inspectorFilter.version) {
        state->previewKept = ArenaPushArray(textArena, lineCount + 1, u32);
        u32 kept = 0;
        for (u32 i = 0; i < lineCount; ++i) {
            if (!filter.length || Prog_FilterMatch(StrFromTo(text, state->previewLines[i], (i64)state->previewLines[i + 1]), filter, NULL)) state->previewKept[kept++] = i;
        }
        state->previewKeptCount = kept;
        state->previewKeptVersion = state->inspectorFilter.version;
        state->previewRowColumns = 0;
    }
    u32 shownCount = state->previewKeptCount;
    // The numbers sit in a gutter that grows for long files; the text starts closer to the edge without them.
    u32 digits = 1;
    u64 total = !state->previewWindowed ? lineCount : state->textIndex.done ? state->textIndex.lines : state->previewFirstLine + lineCount;
    for (u64 n = total; n >= 10; n /= 10) ++digits;
    f32 digitWidth = R_TextWidth(m->detail, S("0")), numbers = (f32)digits * digitWidth;
    // A hex dump has its offsets for numbers and an archive's listing its sizes, and their lines are not wrapped.
    b32 numbered = !state->inspectorHideNumbers && !state->previewHex && !state->previewArchive;
    f32 gap = floorf(10.4f * s + 0.5f), left = !numbered ? floorf(9.6f * s + 0.5f) : MaxF32(floorf(52.0f * s + 0.5f), floorf(26.4f * s + 0.5f) + numbers + gap);
    f32 room = MaxF32(cell, view->rect.x1 - view->rect.x0 - left - margin);
    b32 wrap = state->previewDocument ? !state->documentUnwrapped : state->inspectorWrap;
    u32 columns = wrap && !state->previewHex && !state->previewArchive ? Max((u32)(room / cell), 1u) : 0;
    // Each line's first row, worked out again when the width in characters changes.
    if (columns && state->previewRowColumns != columns) {
        state->previewRows = ArenaPushArray(textArena, shownCount + 1, u32);
        u32 rows = 0;
        for (u32 i = 0; i < shownCount; ++i) {
            u32 line = state->previewKept[i];
            state->previewRows[i] = rows;
            rows += Prog__WrapLine(StrFromTo(text, state->previewLines[line], (i64)state->previewLines[line + 1]), columns, NULL, 0);
        }
        state->previewRows[shownCount] = rows;
        state->previewRowColumns = columns;
    }
    u32 rowCount = columns ? state->previewRows[shownCount] : shownCount;
    // A big file's window: a new one keeps the line that was at the top there, and the line at the top now is noted for the next.
    if (state->previewWindowed && !state->textFound && shownCount) {
        if (state->textKeepTop) {
            u64 at = state->textTopLine >= state->previewFirstLine ? state->textTopLine - state->previewFirstLine : 0;
            u32 shown = (u32)Min(at, (u64)shownCount - 1);
            f32 y = (f32)(columns ? state->previewRows[shown] : shown) * pitch + state->textTopPart, moved = y - view->scrollOffset.y;
            view->scrollOffset.y = MaxF32(0, y);
            view->scrollShown.y = MaxF32(0, view->scrollShown.y + moved);
            state->textKeepTop = false;
        }
        u32 top = Min(Prog__TextRowLine(state, (u32)MaxF32(0, floorf(view->scrollOffset.y / pitch)), columns, shownCount), shownCount - 1);
        state->textTopLine = state->previewFirstLine + state->previewKept[top];
        state->textTopPart = view->scrollOffset.y - (f32)(columns ? state->previewRows[top] : top) * pitch;
    }
    f32 from = MinF32(view->scrollOffset.y, view->scrollShown.y), to = MaxF32(view->scrollOffset.y, view->scrollShown.y) + (view->rect.y1 - view->rect.y0);
    u32 first = Min((u32)MaxF32(0, floorf(from / pitch)), rowCount), last = Min((u32)MaxF32(0, ceilf(to / pitch)) + 1, rowCount);
    // The row and cell under the pointer, among the rows in sight; above them it is the first one's start, and below the last one's end.
    f32 pointerRow = floorf((UI->mouse.y - view->rect.y0 - floorf(13.6f * s + 0.5f) + view->scrollShown.y) / pitch);
    f32 pointerCell = (UI->mouse.x - view->rect.x0 - left) / cell;
    if (pointerRow < (f32)first) pointerCell = -1;
    if (pointerRow >= (f32)last) pointerCell = 1e9f;
    u32 hitRow = last > first ? (u32)MinF32(MaxF32(pointerRow, (f32)first), (f32)(last - 1)) : UINT32_MAX, hitLine = 0;
    u64 hit = UINT64_MAX;
    // A big file's window moves on as the view nears either of its ends: ahead from a line it holds, back to a step of the count, once the
    // whole file has been counted.
    if (state->previewWindowed && !filter.length && lineCount) {
        u32 reach = 2000, top = Min(Prog__TextRowLine(state, first, columns, shownCount), lineCount - 1), bottom = Prog__TextRowLine(state, last, columns, shownCount);
        if (bottom + reach >= lineCount && state->previewOffset + text.length < state->previewSize && top > 0) {
            u32 from = top > reach ? top - reach : top;
            Prog_RequestTextWindow(state, state->previewOffset + state->previewLines[from], state->previewFirstLine + from, state->previewLineStates ? state->previewLineStates[from] : 0, false);
        } else if (top < reach && state->previewOffset > 0 && state->textIndex.done && state->textIndex.count) {
            u64 at = state->previewOffset + state->previewLines[top], back = at > Prog_TextWindow / 2 ? at - Prog_TextWindow / 2 : 0;
            u64 step = Min(back / Prog_TextStep, state->textIndex.count - 1);
            while (step > 0 && state->textIndex.offsets[step] >= state->previewOffset) --step;
            if (state->textIndex.offsets[step] < state->previewOffset) {
                Prog_RequestTextWindow(state, state->textIndex.offsets[step], state->textIndex.numbers[step], state->textIndex.states[step], false);
            }
        }
    }
    UI_Parent(view) {
        Prog__Spacer(UI_Flex(), UI_Pixels((f32)first * pitch));
        // The shown line holding the first row in sight.
        u32 shown = Prog__TextRowLine(state, first, columns, shownCount);
        for (u32 row = first; row < last && shown < shownCount; ++shown) {
            u32 line = state->previewKept[shown];
            String whole = StrFromTo(text, state->previewLines[line], (i64)state->previewLines[line + 1]);
            if (whole.length && whole.value[whole.length - 1] == '\n') --whole.length;
            if (whole.length && whole.value[whole.length - 1] == '\r') --whole.length;
            u8 *matched = filter.length ? ArenaPushArray(&state->frameArena, whole.length + 1, u8) : NULL;
            if (matched) Prog_FilterMatch(whole, filter, matched);
            u8 *tokens = state->previewLineStates && !state->noHighlight ? ArenaPushArrayNoZero(&state->frameArena, whole.length + 1, u8) : NULL;
            if (tokens) Prog_SyntaxLine(state->previewSyntax, whole, state->previewLineStates[line], tokens);
            u32 starts[256];
            u32 rows = columns ? Min(Prog__WrapLine(whole, columns, starts, CArrayCount(starts)), (u32)CArrayCount(starts)) : 1;
            if (!columns) starts[0] = 0;
            u32 lineRow = columns ? state->previewRows[shown] : shown;
            for (u32 r = row - lineRow; r < rows && row < last; ++r, ++row) {
                u64 end = r + 1 < rows ? starts[r + 1] : whole.length;
                u64 lineStart = state->previewLines[line], rowFrom = lineStart + starts[r], rowTo = lineStart + end;
                if (row == hitRow) {
                    hit = rowFrom + Prog__OffsetAtCell(StrFromTo(whole, starts[r], (i64)end), pointerCell);
                    hitLine = line;
                }
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(pitch));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_Block *rowBlock = UI_AddBlock(UI_Flag_Clip);
                UI_Parent(rowBlock) {
                    // What of the row is selected, behind its text, and a cell more for the line break when the selection goes on past it.
                    if (selectFrom < selectTo && selectTo > rowFrom && selectFrom <= rowTo) {
                        u64 a = Max(selectFrom, rowFrom), b = Min(selectTo, rowTo);
                        f32 x0 = (f32)Prog__TextCells(StrFromTo(whole, starts[r], (i64)(a - lineStart))) * cell;
                        f32 x1 = (f32)Prog__TextCells(StrFromTo(whole, starts[r], (i64)(b - lineStart))) * cell + (selectTo > rowTo && r + 1 == rows ? cell : 0);
                        if (x1 > x0) {
                            UI_SetNextFloatingOffset((UI_Vec2){ left + x0, 0 });
                            UI_SetNextPrefWidth(UI_Pixels(x1 - x0));
                            UI_SetNextPrefHeight(UI_Pixels(pitch));
                            UI_SetNextColors((UI_Colors){ .background = theme->selection });
                            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                        }
                    }
                    // The number shows on a line's first row, right-aligned against the gap before the text.
                    if (numbered && r == 0) {
                        UI_SetNextFont(m->detail);
                        u64 number = state->previewNumbers ? state->previewNumbers[line] : state->previewFirstLine + line;
                        Prog__Label(StrFormat(&state->frameArena, "%llu", (unsigned long long)(number + 1)), UI_Pixels(left - gap), pitch, 0, theme->textDim, 1);
                        Prog__Spacer(UI_Pixels(gap), UI_Pixels(pitch));
                    } else {
                        Prog__Spacer(UI_Pixels(left), UI_Pixels(pitch));
                    }
                    Prog__InspectorRow(state, StrFromTo(whole, starts[r], (i64)end), matched ? matched + starts[r] : NULL, tokens ? tokens + starts[r] : NULL, font, cell, pitch,
                                       state->inspectorWhitespace, m);
                }
            }
        }
        Prog__Spacer(UI_Flex(), UI_Pixels((f32)(rowCount - last) * pitch));
    }
    if (hit != UINT64_MAX && press.pressed) {
        state->previewAnchor = state->previewCaret = hit;
        state->previewClicks = press.pressCount;
        state->previewTextFocus = true;
        if (press.pressCount == 2) {
            while (state->previewAnchor > 0 && Prog__WordByte((u8)text.value[state->previewAnchor - 1])) --state->previewAnchor;
            while (state->previewCaret < text.length && Prog__WordByte((u8)text.value[state->previewCaret])) ++state->previewCaret;
        } else if (press.pressCount >= 3) {
            state->previewAnchor = state->previewLines[hitLine];
            state->previewCaret = state->previewLines[hitLine + 1];
        }
    } else if (hit != UINT64_MAX && dragging && state->previewClicks < 2) {
        state->previewCaret = hit;
        // Holding the pointer past the top or bottom edge scrolls, faster the further out it is, as the lists' boxes do.
        f32 over = UI->mouse.y < view->rect.y0 ? UI->mouse.y - view->rect.y0 : UI->mouse.y > view->rect.y1 ? UI->mouse.y - view->rect.y1 : 0;
        if (over != 0) {
            view->scrollOffset.y = MaxF32(0, view->scrollOffset.y + over * 0.25f);
            UI_KeepAnimating();
        }
    }
    if (state->previewWindowed && !state->textFound) Prog__TextScrollBar(state, view, pitch, shownCount ? state->previewKept[Min(Prog__TextRowLine(state, first, columns, shownCount), shownCount - 1)] : 0, columns, m);
    else Prog__ScrollBar(view, pitch, S("InspectorText"), 1, m);
}

// A dashed outline with round corners, `width` by `height` at `x`, `y` in the parent: the corners are cut from a rounded outline,
// and the edges between them dashes of `dash` with gaps of `gap`.
function void Prog__RoundDashedOutline(f32 x, f32 y, f32 width, f32 height, f32 radius, f32 thick, f32 dash, f32 gap, R_Color color)
{
    for (u32 corner = 0; corner < 4; ++corner) {
        b32 right = (b32)(corner & 1), lower = (b32)(corner >> 1);
        UI_SetNextFloatingOffset((UI_Vec2){ x + (right ? width - radius : 0), y + (lower ? height - radius : 0) });
        UI_SetNextPrefWidth(UI_Pixels(radius));
        UI_SetNextPrefHeight(UI_Pixels(radius));
        UI_Block *clip = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
        UI_Parent(clip) {
            UI_SetNextFloatingOffset((UI_Vec2){ right ? -radius : 0, lower ? -radius : 0 });
            UI_SetNextPrefWidth(UI_Pixels(2 * radius));
            UI_SetNextPrefHeight(UI_Pixels(2 * radius));
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(thick);
            UI_SetNextColors((UI_Colors){ .border = color });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
        }
    }
    for (u32 side = 0; side < 4; ++side) {
        b32 across = side < 2;
        f32 length = (across ? width : height) - 2 * radius;
        for (f32 at = 0; at < length; at += dash + gap) {
            f32 piece = MinF32(dash, length - at);
            f32 px = across ? x + radius + at : (side == 2 ? x : x + width - thick);
            f32 py = across ? (side == 0 ? y : y + height - thick) : y + radius + at;
            UI_SetNextFloatingOffset((UI_Vec2){ px, py });
            UI_SetNextPrefWidth(UI_Pixels(across ? piece : thick));
            UI_SetNextPrefHeight(UI_Pixels(across ? thick : piece));
            UI_SetNextColors((UI_Colors){ .background = color });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
    }
}

// The item's permissions where the system has them, as the inspector's toolbar shows them: their letters, owner's, group's and others',
// each turned on and off by a click.
function void Prog__PermissionChips(Prog_State *state, const Prog_Entry *entry, f32 height, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    f32 s = m->scale;
    u16 mode = entry->file.mode;
    state->permissionsPath = Prog_JoinPath(frame, state->directory.path, entry->file.name);
    local_persist const char letters[3] = { 'r', 'w', 'x' };
    f32 cell = floorf(14.4f * s + 0.5f), apart = floorf(6.4f * s + 0.5f), gap = MaxF32(1, floorf(1.6f * s + 0.5f));
    UI_SetNextPrefWidth(UI_Pixels(9 * cell + 2 * apart + 6 * gap));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_Parent(UI_AddBlock(0)) {
        for (u32 bit = 0; bit < 9; ++bit) {
            if (bit && bit % 3 == 0) Prog__Spacer(UI_Pixels(apart), UI_Pixels(height));
            else if (bit) Prog__Spacer(UI_Pixels(gap), UI_Pixels(height));
            u16 flag = (u16)(0400 >> bit);
            b32 on = (mode & flag) != 0;
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Permission/%u", bit));
            b32 hot = UI->hotKey == key;
            UI_SetNextPrefWidth(UI_Pixels(cell));
            UI_SetNextPrefHeight(UI_Pixels(cell));
            UI_SetNextCornerRadius(floorf(3.2f * s + 0.5f));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextFont(m->detail);
            // As ls and eza color them: read in yellow, write in red and run in green, the scheme's own.
            R_Color ink = bit % 3 == 0 ? theme->codeNumber : bit % 3 == 1 ? theme->danger : theme->codeString;
            UI_SetNextColors((UI_Colors){ .background = hot ? theme->hover : theme->track, .text = on ? ink : theme->textDim });
            UI_SetNextText(on ? StrMake(&letters[bit % 3], 1) : S("-"));
            UI_Block *letter = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawText | UI_Flag_LeftMouseClickable);
            UI_Interaction toggle = UI_InteractionFromBlock(letter);
            if (toggle.clicked) Prog_QueueCommand(state, Prog_CommandKind_PermissionsSet, 0, 0, toggle.clickOrder)->target = (u64)(mode ^ flag);
            local_persist const String tips[9] = {
                TrInit("The owner may read"), TrInit("The owner may write"), TrInit("The owner may run"),
                TrInit("The group may read"), TrInit("The group may write"), TrInit("The group may run"),
                TrInit("Others may read"), TrInit("Others may write"), TrInit("Others may run"),
            };
            if (hot) Prog__PopupTooltip(state, Prog_Tr(tips[bit]));
        }
    }
}

// The card of the item's details, at the inspector's top right: its name with buttons to fold and close the card, then what it is.
// The card of the inspected item's details at the pane's top right, reaching left as far as the toolbar's controls let it, at `room` from
// the pane's left, up to a width that holds most paths.
function void Prog__InspectorDetails(Prog_State *state, const Prog_Entry *entry, f32 paneWidth, f32 room, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale, head = floorf(32.0f * s + 0.5f), row = floorf(20.8f * s + 0.5f), margin = floorf(9.6f * s + 0.5f);
    f32 width = floorf(MinF32(MaxF32(paneWidth - margin - room - floorf(8.0f * s + 0.5f), 244.0f * s), 358.4f * s) + 0.5f);
    f32 top = floorf(3.2f * s + 0.5f), bottom = floorf(6.4f * s + 0.5f), left = floorf(13.6f * s + 0.5f), right = floorf(12.8f * s + 0.5f), radius = floorf(8.0f * s + 0.5f);
    Arena *frame = &state->frameArena;
    b32 folder = entry->file.kind == P_FileKind_Directory;
    String path = P_NativePath(frame, Prog_JoinPath(frame, state->directory.path, entry->file.name));
    String none = S("--");
    String size = folder ? (entry->measured ? Prog_FormatSize(frame, entry->bytes) : none) : Prog_FormatSize(frame, entry->file.size);
    String created = entry->file.createdTimeUnix ? P_FormatFileTime(frame, entry->file.createdTimeUnix) : none;
    String labels[16], values[16];
    u32 count = 0;
    labels[count] = Tr("Path"), values[count++] = path;
    labels[count] = Tr("Type"), values[count++] = Prog_Tr(entry->type);
    labels[count] = Tr("Size"), values[count++] = size;
    if (folder) {
        labels[count] = Tr("Subfolders"), values[count++] = entry->measured && entry->folders ? StrFormat(frame, "%" PRIu64, entry->folders) : none;
        labels[count] = Tr("Subfiles"), values[count++] = entry->measured && entry->files ? StrFormat(frame, "%" PRIu64, entry->files) : none;
    }
    labels[count] = Tr("Created"), values[count++] = created;
    labels[count] = Tr("Modified"), values[count++] = Prog_EntryModified(frame, entry);
    // An audio or video file's details, once its preview has read them.
    for (u64 at = 0; state->previewMedia.length && at < state->previewMedia.length && count < CArrayCount(labels);) {
        Index end = StrIndexChar(StrFrom(state->previewMedia, at), '\n', SSF_CountOnFail);
        String line = StrFromTo(state->previewMedia, at, (i64)(at + end.value));
        at += end.value + 1;
        Index tab = StrIndexChar(line, '\t', SSF_None);
        if (tab.exists) labels[count] = Prog_Tr(StrFromTo(line, 0, (i64)tab.value)), values[count++] = StrFrom(line, tab.value + 1);
    }
    // Folding eases it shut to its title and open again, as the sidebar's groups do; the card clips what does not fit.
    f32 body = m->separator + (f32)count * row + top + bottom, folded = state->inspectorDetailsFolded ? 0.0f : 1.0f;
    f32 open = UI_Animate(UI_KeyFromStr(0, S("InspectorDetailsOpen")), folded, folded);
    f32 height = head + floorf(body * open + 0.5f);
    UI_SetNextFloatingOffset((UI_Vec2){ paneWidth - width - margin, floorf(4.8f * s + 0.5f) });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextCornerRadius(radius);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill() });
    UI_Block *card = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorDetails")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(card) {
        UI_SetNextChildrenPadding((UI_Padding){ left, 0, right, 0 });
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_Block *title = Prog__Box(UI_Flex(), UI_Pixels(head), UI_Layout_Horizontal, theme->menu, 0);
        UI_Parent(title) {
            // Folding it, then closing it at the far right.
            Prog__Label(entry->file.name, UI_Flex(), head, 0, theme->text, 0);
            UI_Interaction fold = Prog__TextButton(S("InspectorDetailsFold"), state->inspectorDetailsFolded ? Icon_ChevronRight : Icon_ChevronDown, m->icons,
                                                   UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0, theme->icon, theme->text, theme->iconDisabled, true, m);
            if (fold.clicked) Prog_QueueCommand(state, Prog_CommandKind_InspectorFoldDetails, 0, 0, fold.clickOrder);
            Prog__ToolbarButton(state, S("InspectorDetailsClose"), Icon_X, Prog_CommandKind_InspectorToggleDetails, true, m);
        }
        if (open > 0) {
            Prog__Box(UI_Flex(), UI_Pixels(m->separator), UI_Layout_Horizontal, theme->tabBorder, 0);
            Prog__Spacer(UI_Flex(), UI_Pixels(top));
            for (u32 i = 0; i < count; ++i) {
                UI_SetNextChildrenPadding((UI_Padding){ left, 0, right, 0 });
                UI_Block *line = Prog__Box(UI_Flex(), UI_Pixels(row), UI_Layout_Horizontal, (R_Color){0}, 0);
                UI_Parent(line) {
                    UI_SetNextFont(m->detail);
                    Prog__Label(labels[i], UI_TextContent(), row, 0, theme->text, 0);
                    Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(row));
                    // A value too long for the card, such as the path, ends in an ellipsis.
                    f32 room = width - left - right - m->gap - R_TextWidth(m->detail, labels[i]);
                    UI_SetNextFont(m->detail);
                    Prog__Label(Prog__Ellipsize(frame, m->detail, values[i], room), UI_Flex(), row, 0, theme->textDim, 1);
                }
            }
        }
    }
    UI_SetNextFloatingOffset((UI_Vec2){ paneWidth - width - margin, floorf(4.8f * s + 0.5f) });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
}

function UI_Key Prog_PanelKeySeed(u64 id);

// Makes the inspector's panel of panel `owner` (an id) current, with its own block keys, and returns its index; UINT32_MAX while there is none.
// Prog__LeaveInspectorPanel puts the owner and its keys back.
function u32 Prog__EnterInspectorPanel(Prog_State *state, u64 owner)
{
    u32 inner = Prog_InspectorPanel(state, owner, false);
    if (inner == UINT32_MAX) return inner;
    Prog__UsePanel(state, inner);
    UI->keySeed = Prog_PanelKeySeed(state->panels[inner].id);
    return inner;
}

function void Prog__LeaveInspectorPanel(Prog_State *state, u64 owner)
{
    Prog__UsePanel(state, Prog_PanelIndex(state, owner));
    UI->keySeed = Prog_PanelKeySeed(owner);
}

// What the inspector says over an archive's list: what it holds in all, then its own size, `packed`, and how much of its contents' size the
// packing saves.
function String Prog_ArchiveSummary(Arena *arena, P_DirectoryTotals totals, u64 packed)
{
    String size = Prog_FormatSize(arena, packed);
    u64 items = totals.folders + totals.files;
    if (!totals.complete) return StrFormat(arena, TrN(items, "Over %S item, packed to %S", "Over %S items, packed to %S"), Prog__FormatCount(arena, items), size);
    StrBuilder text = {0};
    if (totals.folders) StrBuildFormat(arena, &text, TrN(totals.folders, "%S folder", "%S folders"), Prog__FormatCount(arena, totals.folders));
    if (totals.folders) StrBuildStr(arena, &text, S(", "));
    StrBuildFormat(arena, &text, TrN(totals.files, "%S file, %S, packed to %S", "%S files, %S, packed to %S"), Prog__FormatCount(arena, totals.files),
                   Prog_FormatSize(arena, totals.bytes), size);
    u32 saved = totals.bytes > packed ? (u32)((f64)(totals.bytes - packed) * 100.0 / (f64)totals.bytes + 0.5) : 0;
    StrBuildChar(arena, &text, ' ');
    if (saved) StrBuildFormat(arena, &text, TrF("(%u%% smaller)"), saved);
    else StrBuildStr(arena, &text, Tr("(no smaller)"));
    return text.buffer;
}

// A folder in the inspector: the list of the inspector's panel, worked as a panel's, with its column titles in Details and its status bar under it.
function void Prog__InspectorList(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    u64 owner = state->panels[state->currentPanel].id;
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_Block *body = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorFolder")), UI_Flag_Clip);
    R_Rect rect = body->rect;
    u32 inner = Prog__EnterInspectorPanel(state, owner);
    if (inner == UINT32_MAX) {
        UI_Parent(body) Prog__Label(Tr("Loading..."), UI_Flex(), m->row, 0, theme->textDim, 0.5f);
        return;
    }
    {
        state->panelRect = rect;
        if (UI__Contains(rect, UI->mouse)) state->pointerPanel = state->panels[inner].id;
        Prog_Columns columns = Prog__Columns(state, m->scale, rect.x1 - rect.x0 - 2 * m->inset - (state->listScrolls ? Prog__ScrollGutter(m) : 0));
        UI_Parent(body) {
            UI_Block *column = Prog__Box(UI_Flex(), UI_Flex(), UI_Layout_Vertical, theme->background, 0);
            UI_Parent(column) {
                if (Prog_ZoomView(state->zoom) == Prog_View_Details) {
                    Prog__Header(state, &columns, m);
                    Prog__Box(UI_Flex(), UI_Pixels(floorf(4.8f * m->scale + 0.5f)), UI_Layout_Horizontal, theme->background, 0);
                }
                Prog__List(state, &columns, m);
            }
            Prog__StatusBar(state, m);
        }
    }
    Prog__LeaveInspectorPanel(state, owner);
}

// The inspector's filter box at its footer's left: what it holds with buttons to keep it and to clear it, or while empty and idle `prompt`.
function void Prog__InspectorFilterBox(Prog_State *state, String prompt, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 radius = floorf(10.4f * m->scale + 0.5f), inset = floorf(2.4f * m->scale + 0.5f);
    b32 active = state->inspectorFiltering || state->inspectorFilter.length;
    UI_SetNextPrefWidth(UI_Pixels(floorf(m->line * 15.3f) + floorf(6.4f * m->scale + 0.5f) + radius));
    UI_SetNextPrefHeight(UI_Pixels(m->pill + radius));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0 });
    UI_SetNextChildrenPadding(active ? (UI_Padding){ radius, inset, m->gap, 0 } : (UI_Padding){ m->gap + radius, 0, m->gap, 0 });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(m->stroke);
    UI_SetNextColors((UI_Colors){ .background = theme->panel, .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorFilterBox")), UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Interaction interaction = UI_InteractionFromBlock(box);
    if (interaction.pressed) Prog_QueueCommand(state, Prog_CommandKind_InspectorStartFilter, 0, 0, interaction.pressOrder);
    UI_Parent(box) {
        if (active) {
            f32 tall = m->pill - inset;
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(tall + radius));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0 });
            UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, 0, 0 });
            UI_SetNextCornerRadius(floorf(8.0f * m->scale + 0.5f));
            UI_SetNextColors((UI_Colors){ .background = theme->inner });
            UI_Block *well = UI_AddBlock(UI_Flag_DrawBackground);
            UI_Parent(well) {
                Prog__SearchIcon(m->iconSlot - m->gap, tall, state->inspectorFilter.length ? theme->text : theme->textDim, 0.5f, floorf(0.8f * m->scale + 0.5f), m);
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(tall));
                UI_Block *field = UI_AddBlock(UI_Flag_Clip);
                UI_Parent(field) Prog__EditContents(&state->inspectorFilter, floorf(m->gap * 0.5f), floorf((tall - m->line) * 0.5f), state->inspectorFiltering, m);
            }
            UI_Block *buttons = Prog__Box(UI_Fill(), UI_Pixels(tall), UI_Layout_Horizontal, (R_Color){0}, 0);
            UI_Parent(buttons) {
                // Kept while it is being typed; once kept, only clearing it is left.
                if (state->inspectorFiltering) Prog__PillButton(state, S("InspectorFilterApply"), Icon_Check, Prog_CommandKind_InspectorApplyFilter, false, m);
                Prog__PillButton(state, S("InspectorFilterRemove"), Icon_X, Prog_CommandKind_InspectorRemoveFilter, false, m);
            }
        } else {
            Prog__SearchIcon(m->iconSlot - m->gap, m->pill, theme->textDim, 0.5f, floorf(0.8f * m->scale + 0.5f), m);
            Prog__Label(prompt, UI_Flex(), m->pill, m->gap * 0.5f, theme->textDim, 0);
        }
    }
}

// A row of buttons in a dashed outline, the chosen one lit, each running `kind` with its place in the row;
// their tips say what each does over the command `tipKind`. A NULL font is the icon font's.
function void Prog__ChoiceGroup(Prog_State *state, const String *keys, const String *labels, const R_Font *const *fonts, const String *tips, u32 count, u32 chosen,
                                Prog_CommandKind kind, Prog_CommandKind tipKind, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale, bar = Prog__StripHeight(m), pad = floorf(4.0f * s + 0.5f), gap = floorf(6.4f * s + 0.5f);
    f32 width = (f32)count * Prog__ToolbarButtonWidth(m) + (f32)(count - 1) * gap + 2 * pad, height = floorf(29.6f * s + 0.5f);
    UI_SetNextChildrenPadding((UI_Padding){ pad, 0, pad, 0 });
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_Block *group = Prog__Box(UI_Pixels(width), UI_Pixels(bar), UI_Layout_Horizontal, (R_Color){0}, 0);
    UI_Parent(group) {
        R_Color wash = theme->selection;
        wash.a *= theme->hoverAmount.r;
        for (u32 i = 0; i < count; ++i) {
            if (i) Prog__Spacer(UI_Pixels(gap), UI_Pixels(bar));
            UI_Key key = UI_KeyFromStr(0, keys[i]);
            b32 lit = chosen == i || UI->hotKey == key;
            UI_SetNextPrefWidth(UI_Pixels(Prog__ToolbarButtonWidth(m)));
            UI_SetNextPrefHeight(UI_Pixels(Prog__ToolbarButtonHeight(m)));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
            UI_SetNextColors((UI_Colors){ .background = wash, .text = lit ? theme->text : theme->icon });
            UI_SetNextFont(fonts && fonts[i] ? fonts[i] : m->icons);
            UI_SetNextText(labels[i]);
            UI_Block *button = UI_AddBlockFromKey(key, UI_Flag_DrawText | UI_Flag_LeftMouseClickable | (lit ? UI_Flag_DrawBackground : 0));
            Prog__DescribeTooltip(state, tipKind, Prog_Tr(tips[i]));
            UI_Interaction interaction = UI_InteractionFromBlock(button);
            if (interaction.clicked) Prog_QueueCommand(state, kind, i, 0, interaction.clickOrder);
        }
        // From the group's edge, which floating blocks count from inside its padding.
        Prog__RoundDashedOutline(-pad, floorf((bar - height) * 0.5f), width, height, floorf(8.0f * s + 0.5f), MaxF32(1, floorf(1.2f * s + 0.5f)),
                                 floorf(4.8f * s + 0.5f), floorf(2.4f * s + 0.5f), theme->outline);
    }
}

// The model's axes in the view's lower left corner, as Blender and Herta show them: a short line to a dot for each, its letter beyond it, the
// farthest drawn first. They are the file's own, so that a Z-up file's Z points up. `rows` turns the view's Y-up space to the view.
function void Prog__ModelAxes(Prog_State *state, f32 width, f32 height, const f32 rows[3][3], const Prog_Metrics *m)
{
    local_persist const String names[3] = { SInit("X"), SInit("Y"), SInit("Z") };
    local_persist const u32 colors[3] = { 0xD57B7F, 0x83B993, 0x7C9ED5 };
    f32 s = m->scale, length = floorf(16.0f * s + 0.5f), dot = MaxF32(2, floorf(4.0f * s + 0.5f)), stroke = MaxF32(1, 1.5f * s), gap = floorf(3.0f * s + 0.5f);
    // Each axis in the view's Y-up space: a Z-up file's Y is the view's -Z and its Z the view's Y.
    f32 axes[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    if (state->model.zUp) axes[1][1] = 0, axes[1][2] = -1, axes[2][1] = 1, axes[2][2] = 0;
    f32 tips[3][3];
    for (u32 a = 0; a < 3; ++a) for (u32 r = 0; r < 3; ++r) tips[a][r] = rows[r][0] * axes[a][0] + rows[r][1] * axes[a][1] + rows[r][2] * axes[a][2];
    // Room for the letters whichever way the axes point, as far as one pointing straight left or down reaches, so that the axes line up with
    // the left edge of the details below and keep their place as they turn.
    f32 letters = MaxF32(R_TextWidth(m->detail, names[0]), MaxF32(R_TextWidth(m->detail, names[1]), R_TextWidth(m->detail, names[2])));
    f32 ox = length + gap + letters, oy = height - (length + gap + m->line);
    if (ox > width || oy < 0) return;
    u32 order[3] = { 0, 1, 2 };
    for (u32 i = 0; i < 3; ++i) for (u32 j = i + 1; j < 3; ++j) if (tips[order[j]][2] < tips[order[i]][2]) { u32 swap = order[i]; order[i] = order[j]; order[j] = swap; }
    for (u32 i = 0; i < 3; ++i) {
        u32 a = order[i];
        R_Color color = Prog__HexColor(colors[a]);
        f32 dx = tips[a][0] * length, dy = -tips[a][1] * length, reach = sqrtf(dx * dx + dy * dy);
        // The line in a block around it, from its corner nearest the view's top left.
        f32 left = ox + MinF32(dx, 0) - stroke, top = oy + MinF32(dy, 0) - stroke;
        UI_SetNextFloatingOffset((UI_Vec2){ left, top });
        UI_SetNextPrefWidth(UI_Pixels(fabsf(dx) + 2 * stroke));
        UI_SetNextPrefHeight(UI_Pixels(fabsf(dy) + 2 * stroke));
        UI_SetNextLine((UI_Vec2){ ox - left, oy - top }, (UI_Vec2){ ox + dx - left, oy + dy - top }, stroke);
        UI_SetNextColors((UI_Colors){ .background = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawLine);
        UI_SetNextFloatingOffset((UI_Vec2){ ox + dx - dot * 0.5f, oy + dy - dot * 0.5f });
        UI_SetNextPrefWidth(UI_Pixels(dot));
        UI_SetNextPrefHeight(UI_Pixels(dot));
        UI_SetNextCornerRadius(dot * 0.5f);
        UI_SetNextColors((UI_Colors){ .background = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        // The letter past the tip, along the axis, or above the middle when the axis points at the view.
        f32 ux = reach > 0.5f ? dx / reach : 0, uy = reach > 0.5f ? dy / reach : -1, letter = R_TextWidth(m->detail, names[a]);
        f32 away = reach + gap + (fabsf(ux) * letter + fabsf(uy) * m->line) * 0.5f;
        UI_SetNextFloatingOffset((UI_Vec2){ floorf(ox + ux * away - letter * 0.5f), floorf(oy + uy * away - m->line * 0.5f) });
        UI_SetNextPrefWidth(UI_Pixels(letter));
        UI_SetNextPrefHeight(UI_Pixels(m->line));
        UI_SetNextFont(m->detail);
        UI_SetNextColors((UI_Colors){ .text = color });
        UI_SetNextText(names[a]);
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
    }
}

// The model's parts to draw, with the see-through ones from the farthest to the nearest as `toward`, the view's depth row, sees them:
// each one's triangles written in that order over its indices, and the parts themselves in the order of their triangles' average depth.
function const R_MeshPart *Prog__ModelOrder(Prog_State *state, const f32 toward[4])
{
    Prog_ModelView *model = &state->model;
    if (!model->seeTriangles) return model->parts;
    Arena *frame = &state->frameArena;
    R_MeshPart *parts = ArenaPushArrayNoZero(frame, model->partCount, R_MeshPart);
    MemCopy(parts, model->parts, model->partCount * sizeof(R_MeshPart));
    f32 *depth = ArenaPushArrayNoZero(frame, model->partCount, f32);
    u32 *sorted = ArenaPushArrayNoZero(frame, model->seeTriangles * 3, u32);
    for (u32 i = 0, at = 0; i < model->partCount; ++i) {
        u64 count = model->parts[i].count / 3;
        depth[i] = 0;
        if (!model->parts[i].blend || !count) continue;
        Scratch scratch = GetScratchEx(frame);
        f32 *keys = ArenaPushArrayNoZero(scratch.arena, count, f32);
        u32 *order = ArenaPushArrayNoZero(scratch.arena, count, u32);
        for (u64 t = 0; t < count; ++t) {
            const f32 *middle = model->seeMiddles + (u64)(at + t) * 3;
            keys[t] = toward[0] * middle[0] + toward[1] * middle[1] + toward[2] * middle[2];
            depth[i] += keys[t] / (f32)count;
            order[t] = (u32)t;
        }
        Prog_SortByKey(scratch.arena, keys, order, count);
        u32 *out = sorted + (u64)at * 3;
        for (u64 t = 0; t < count; ++t) MemCopy(out + t * 3, model->seeIndices + (u64)(at + order[t]) * 3, 12);
        R_MeshIndices(model->mesh, model->parts[i].first, out, count * 3);
        ReleaseScratch(scratch);
        at += (u32)count;
    }
    // The see-through parts among themselves, farthest first, in the places they had in the list; the solid ones stay.
    u32 *see = ArenaPushArrayNoZero(frame, model->partCount, u32);
    f32 *keys = ArenaPushArrayNoZero(frame, model->partCount, f32);
    u32 seeCount = 0;
    for (u32 i = 0; i < model->partCount; ++i) if (model->parts[i].blend) keys[i] = depth[i], see[seeCount++] = i;
    u32 *places = ArenaPushArrayNoZero(frame, Max(seeCount, 1u), u32);
    MemCopy(places, see, seeCount * 4);
    Prog_SortByKey(frame, keys, see, seeCount);
    for (u32 k = 0; k < seeCount; ++k) parts[places[k]] = model->parts[see[k]];
    return parts;
}

// A 3D model on the GPU, `height` tall above its details: dragging turns it about its up axis and tips it toward the view, the wheel zooms,
// and a double click puts it back as it first showed. It is drawn into a texture of the view's size whenever the view or the size changes.
function void Prog__InspectorModel(Prog_State *state, f32 height, const Prog_Metrics *m)
{
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_Block *view = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorModel")), UI_Flag_Clip | UI_Flag_LeftMouseClickable | UI_Flag_TakesWheel);
    UI_Interaction drag = UI_InteractionFromBlock(view);
    if (drag.pressed) state->model.dragFrom = UI->mouse;
    if (drag.pressed && drag.pressCount == 2) state->model.yaw = 0.5235988f, state->model.pitch = 0.3490659f, state->model.zoom = 1;
    if (drag.active && drag.isDragging) {
        // A view's width of dragging turns it about once around.
        f32 turn = 6.2831853f / MaxF32(view->rect.x1 - view->rect.x0, 1);
        state->model.yaw -= (UI->mouse.x - state->model.dragFrom.x) * turn;
        state->model.pitch = MinF32(MaxF32(state->model.pitch + (UI->mouse.y - state->model.dragFrom.y) * turn, -1.5533f), 1.5533f);
        state->model.dragFrom = UI->mouse;
    }
    i32 notches = Prog__WheelNotches(view);
    if (notches) state->model.zoom = MinF32(MaxF32(state->model.zoom * powf(1.15f, (f32)notches), 0.2f), 20.0f);
    // What is shown eases to the turn and zoom asked for, as the RAD Debugger's views do, the zoom by its ratio; a new model starts there.
    if (state->model.shownZoom <= 0) {
        state->model.shownYaw = state->model.yaw, state->model.shownPitch = state->model.pitch, state->model.shownZoom = state->model.zoom;
    } else {
        state->model.shownYaw = UI_Approach(state->model.shownYaw, state->model.yaw, 0.0005f);
        state->model.shownPitch = UI_Approach(state->model.shownPitch, state->model.pitch, 0.0005f);
        state->model.shownZoom = expf(UI_Approach(logf(state->model.shownZoom), logf(state->model.zoom), 0.0005f));
    }
    i32 width = (i32)(view->rect.x1 - view->rect.x0), tall = (i32)(view->rect.y1 - view->rect.y0);
    // A view new this frame has no size yet.
    if (width <= 0 || tall <= 0) {
        UI_KeepAnimating();
        return;
    }
    if (width != state->model.textureWidth || tall != state->model.textureHeight) {
        R_TextureRelease(state->model.texture);
        state->model.texture = R_TexturePremultiplied(R_TextureCreateRGBA(width, tall, NULL));
        state->model.textureWidth = width, state->model.textureHeight = tall;
        state->model.stale = true;
    }
    f32 now[3] = { state->model.shownYaw, state->model.shownPitch, state->model.shownZoom };
    if (state->model.stale || MemCompare(now, state->model.drawn, sizeof(now))) {
        // Centered, turned about Y, tipped about X, and scaled so that its bounds fill 88% of the shorter side; depth is not zoomed, so that
        // it stays within the view space's -1 to 1.
        f32 cy = cosf(now[0]), sy = sinf(now[0]), cp = cosf(now[1]), sp = sinf(now[1]);
        f32 fit = 0.88f / state->model.radius, s = fit * now[2];
        f32 r[3][3] = { { cy, 0, -sy }, { -sp * sy, cp, -sp * cy }, { cp * sy, sp, cp * cy } };
        f32 rows[3][4];
        for (u32 i = 0; i < 3; ++i) {
            f32 scale = i == 2 ? fit : s;
            for (u32 k = 0; k < 3; ++k) rows[i][k] = r[i][k] * scale;
            rows[i][3] = -(rows[i][0] * state->model.center[0] + rows[i][1] * state->model.center[1] + rows[i][2] * state->model.center[2]);
        }
        f32 matrix[16] = { rows[0][0], rows[1][0], rows[2][0], 0, rows[0][1], rows[1][1], rows[2][1], 0, rows[0][2], rows[1][2], rows[2][2], 0, rows[0][3], rows[1][3], rows[2][3], 1 };
        MemCopy(state->model.turn, r, sizeof(r));
        R_MeshDraw(state->model.mesh, state->model.texture, width, tall, matrix, Prog__ModelOrder(state, rows[2]), state->model.partCount);
        MemCopy(state->model.drawn, now, sizeof(now));
        state->model.stale = false;
    }
    UI_Parent(view) {
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(UI_Pixels((f32)width));
        UI_SetNextPrefHeight(UI_Pixels((f32)tall));
        UI_SetNextImage(state->model.texture, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ (f32)width, (f32)tall }, (R_Color){ 1, 1, 1, 1 });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawImage);
        Prog__ModelAxes(state, (f32)width, (f32)tall, state->model.turn, m);
    }
    (void)m;
}

// A PDF's pages one below another, each as wide as the view, scrolled by the wheel and the scroll bar. The pages in sight are drawn at that
// width, one at a time, the first from the preview's picture until then, and one not drawn yet shows blank until it comes.
function void Prog__InspectorPdf(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale, left = floorf(10.4f * s + 0.5f), right = floorf(18.4f * s + 0.5f), gap = floorf(9.6f * s + 0.5f);
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ left, gap, right, 0 });
    UI_Block *view = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorPdf")), UI_Flag_Clip | UI_Flag_Scrollable);
    if (state->inspectorResetScroll) {
        view->scrollOffset = view->scrollShown = (UI_Vec2){0};
        state->inspectorResetScroll = false;
    }
    if (!UI->animate) view->scrollShown = view->scrollOffset;
    UI_LimitScrollEase(view);
    // A view new this frame has no size yet, so its pages come in the next.
    if (view->rect.y1 <= view->rect.y0) UI_KeepAnimating();
    f32 width = floorf(MaxF32(1, view->rect.x1 - view->rect.x0 - left - right));
    f32 from = MinF32(view->scrollOffset.y, view->scrollShown.y) - gap, to = MaxF32(view->scrollOffset.y, view->scrollShown.y) + (view->rect.y1 - view->rect.y0);
    u32 wanted = UINT32_MAX;
    f32 at = 0, skipped = 0;
    UI_Parent(view) {
        for (u32 i = 0; i < state->pdf.pageCount; ++i) {
            f32 height = floorf(width * state->pdf.sizes[2 * i + 1] / MaxF32(1, state->pdf.sizes[2 * i]) + 0.5f);
            if (at + height < from || at > to) {
                skipped += height + gap;
                at += height + gap;
                continue;
            }
            if (skipped > 0) Prog__Spacer(UI_Pixels(width), UI_Pixels(skipped));
            skipped = 0;
            Prog_PdfPage *drawn = NULL;
            for (u32 d = 0; d < Prog_PdfDrawn && !drawn; ++d) if (state->pdf.drawn[d].texture.u64[0] && state->pdf.drawn[d].page == i) drawn = &state->pdf.drawn[d];
            if (drawn) drawn->used = UI->frameIndex;
            // Drawn again once the view is a tenth wider or narrower than the page was drawn for.
            if ((!drawn || fabsf((f32)drawn->width - width) > width * 0.1f) && wanted == UINT32_MAX) wanted = i;
            R_TextureHandle texture = drawn ? drawn->texture : i == 0 && state->previewPicture ? state->previewTexture : (R_TextureHandle){0};
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(height));
            if (texture.u64[0]) {
                UI_SetNextImage(texture, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ width, height }, (R_Color){ 1, 1, 1, 1 });
                UI_AddBlock(UI_Flag_DrawImage);
            } else {
                UI_SetNextColors((UI_Colors){ .background = (R_Color){ 1, 1, 1, 1 } });
                UI_AddBlock(UI_Flag_DrawBackground);
            }
            Prog__Spacer(UI_Pixels(width), UI_Pixels(gap));
            at += height + gap;
        }
        if (skipped > 0) Prog__Spacer(UI_Pixels(width), UI_Pixels(skipped));
    }
    if (wanted != UINT32_MAX && !state->pdf.job && view->rect.y1 > view->rect.y0) {
        P_Job *job = P_JobCreate(&state->jobs, Prog__DirectoryJob);
        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
        input->kind = Prog_JobKind_PdfPage;
        input->panel = Prog__PanelId(state);
        input->path = StrCopy(&job->arena, state->previewPath);
        input->offset = wanted;
        input->size = (u64)width;
        job->input = input;
        state->pdf.job = job;
        P_JobSubmit(job);
    }
    (void)theme;
    Prog__ScrollBar(view, floorf(m->row * 3), S("InspectorPdf"), 1, m);
}

// The inspector's image: fitted, at its own size or zoomed, centered in the view and dragged to move it while it does not fit,
// over a checkered grid, with a minimap of the part in sight. An audio file's waveform fills the view instead, in the accent.
function void Prog__InspectorImage(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    // The picture keeps inside a margin, wider above it and at the right, where the scroll bar of the text view would be.
    f32 s = m->scale, left = floorf(10.4f * s + 0.5f), right = floorf(15.2f * s + 0.5f), top = floorf(18.4f * s + 0.5f), bottom = floorf(8.0f * s + 0.5f);
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_Block *view = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorImage")), UI_Flag_Clip | UI_Flag_LeftMouseClickable | UI_Flag_TakesWheel);
    UI_Interaction drag = UI_InteractionFromBlock(view);
    f32 roomWidth = MaxF32(1, view->rect.x1 - view->rect.x0 - left - right), roomHeight = MaxF32(1, view->rect.y1 - view->rect.y0 - top - bottom);
    f32 width = (f32)Max(state->previewImageWidth, 1), height = (f32)Max(state->previewImageHeight, 1);
    f32 fit = MinF32(roomWidth / width, roomHeight / height), least = MinF32(fit, 1);
    state->imageFit = fit;
    f32 scale = state->imageScaleMode == Prog_ImageScale_Fit ? least : state->imageScaleMode == Prog_ImageScale_Original ? 1 : state->imageScale;
    // The wheel zooms by a steady ratio, a fine-grained one smoothly, and a double click goes between fitting the view and 1:1, both about
    // the point under the pointer, which stays where it is.
    f32 wanted = scale;
    b32 audio = state->previewSeconds > 0 && Prog_IsAudio(state->previewPath);
    if (!audio && UI->hotKey == view->key && UI->wheel.y != 0 && !(UI->mods & KeyMod_Control)) wanted = MinF32(MaxF32(scale * powf(1.2f, UI->wheel.y), least), 32.0f);
    b32 toFit = !audio && drag.pressed && drag.pressCount == 2 && state->imageScaleMode != Prog_ImageScale_Fit;
    if (!audio && drag.pressed && drag.pressCount == 2) wanted = toFit ? least : 1;
    if (wanted != scale || toFit) {
        f32 px = UI->mouse.x - view->rect.x0 - left, py = UI->mouse.y - view->rect.y0 - top;
        f32 u = (px - ((roomWidth - width * scale) * 0.5f + state->imagePan.x)) / scale, v = (py - ((roomHeight - height * scale) * 0.5f + state->imagePan.y)) / scale;
        state->imagePan.x = px - u * wanted - (roomWidth - width * wanted) * 0.5f;
        state->imagePan.y = py - v * wanted - (roomHeight - height * wanted) * 0.5f;
        state->imageScaleMode = toFit ? Prog_ImageScale_Fit : wanted == 1 ? Prog_ImageScale_Original : Prog_ImageScale_Custom;
        state->imageScale = wanted;
        scale = wanted;
    }
    f32 shownWidth = audio ? roomWidth : floorf(width * scale + 0.5f), shownHeight = audio ? roomHeight : floorf(height * scale + 0.5f);
    if (drag.pressed) state->imageDragFrom = UI->mouse;
    if (drag.active && drag.isDragging && !audio) {
        state->imagePan.x += UI->mouse.x - state->imageDragFrom.x;
        state->imagePan.y += UI->mouse.y - state->imageDragFrom.y;
        state->imageDragFrom = UI->mouse;
    }
    f32 spareX = MaxF32(0, (shownWidth - roomWidth) * 0.5f), spareY = MaxF32(0, (shownHeight - roomHeight) * 0.5f);
    state->imagePan.x = MinF32(MaxF32(state->imagePan.x, -spareX), spareX);
    state->imagePan.y = MinF32(MaxF32(state->imagePan.y, -spareY), spareY);
    // In the room's coordinates.
    f32 x = floorf((roomWidth - shownWidth) * 0.5f + state->imagePan.x), y = floorf((roomHeight - shownHeight) * 0.5f + state->imagePan.y);
    UI_Parent(view) {
        UI_SetNextFloatingOffset((UI_Vec2){ left, top });
        UI_SetNextPrefWidth(UI_Pixels(roomWidth));
        UI_SetNextPrefHeight(UI_Pixels(roomHeight));
        UI_Block *room = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
        UI_Parent(room) {
            // The grid: squares a little lighter than the background between plain ones.
            if (!state->imageHideGrid && !audio) {
                f32 cell = floorf(36.0f * s + 0.5f);
                R_Color light = theme->background;
                light.r += 4.0f / 255.0f, light.g += 4.0f / 255.0f, light.b += 5.0f / 255.0f;
                for (u32 row = 0; (f32)row * cell < roomHeight; ++row) {
                    for (u32 column = (row + 1) & 1; (f32)column * cell < roomWidth; column += 2) {
                        UI_SetNextFloatingOffset((UI_Vec2){ (f32)column * cell, (f32)row * cell });
                        UI_SetNextPrefWidth(UI_Pixels(cell));
                        UI_SetNextPrefHeight(UI_Pixels(cell));
                        UI_SetNextColors((UI_Colors){ .background = light });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    }
                }
            }
            // An audio file's waveform shows how far it has played: what is heard in the accent, the rest dimmed, with the playhead between,
            // which goes on smoothly from what the player last said. A press on it, and a drag after, moves the playhead under the pointer,
            // and letting go plays from there, so that ffmpeg starts again once rather than at every step; a faint line under the pointer
            // shows where a press would go.
            f32 start = view->rect.x0 + left + x;
            f32 pointed = UI__Clamp((UI->mouse.x - start) / MaxF32(shownWidth, 1), 0, 1);
            if (audio && drag.pressed && UI->mouse.x >= start && UI->mouse.x < start + shownWidth) state->scrubbing = true;
            if (!audio || (!drag.active && !drag.released)) state->scrubbing = false;
            // The frame it is let go in still shows it where it was dropped, as the seek starts playing from there only on the next.
            b32 dropped = state->scrubbing && drag.released;
            if (dropped) {
                Prog_QueueCommand(state, Prog_CommandKind_InspectorSeek, 0, 0, UINT64_MAX)->target = (u64)((f64)pointed * state->previewSeconds * 1000);
                state->scrubbing = false;
            }
            b32 played = audio && state->playerJob && state->playerHash == MapHashBytes(state->previewPath.value, state->previewPath.length);
            P_Progress *progress = &state->playerProgress[state->playerSlot];
            u64 heard = atomic_load(&progress->doneBytes), now = UI->nowMicroseconds;
            if (heard != state->playheadHeard) state->playheadHeard = heard, state->playheadAt = now;
            u64 since = played && !atomic_load(&progress->paused) ? Min((now - Min(state->playheadAt, now)) / 1000, (u64)250) : 0;
            f64 length = MaxF64(state->previewSeconds * 1000, 1);
            f64 along = state->scrubbing || dropped ? (f64)pointed : played ? MinF64((f64)(heard + since) / length, 1) : 0;
            b32 split = state->scrubbing || dropped || played;
            f32 cut = split ? floorf((f32)along * shownWidth) : shownWidth;
            R_Color dim = theme->accent;
            dim.a *= 0.35f;
            for (u32 part = 0; part < 2; ++part) {
                f32 from = part ? cut : 0, to = part ? shownWidth : cut;
                if (to <= from) continue;
                UI_SetNextFloatingOffset((UI_Vec2){ x + from, y });
                UI_SetNextPrefWidth(UI_Pixels(to - from));
                UI_SetNextPrefHeight(UI_Pixels(shownHeight));
                R_Rect uv = { from / shownWidth, 0, to / shownWidth, 1 };
                R_Color tint = !audio ? (R_Color){ 1, 1, 1, 1 } : part ? dim : theme->accent;
                UI_SetNextImage(state->previewTexture, audio ? uv : (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ to - from, shownHeight }, tint);
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawImage);
            }
            f32 head = MaxF32(1, floorf(1.6f * s + 0.5f));
            if (split) {
                UI_SetNextFloatingOffset((UI_Vec2){ x + cut - floorf(head * 0.5f), y });
                UI_SetNextPrefWidth(UI_Pixels(head));
                UI_SetNextPrefHeight(UI_Pixels(shownHeight));
                UI_SetNextColors((UI_Colors){ .background = theme->text });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                if (played && !atomic_load(&progress->paused)) UI_KeepAnimating();
            }
            if (audio && !state->scrubbing && UI->hotKey == view->key && UI->mouse.x >= start && UI->mouse.x < start + shownWidth) {
                R_Color faint = theme->text;
                faint.a *= 0.3f;
                UI_SetNextFloatingOffset((UI_Vec2){ x + floorf(pointed * shownWidth) - floorf(head * 0.5f), y });
                UI_SetNextPrefWidth(UI_Pixels(head));
                UI_SetNextPrefHeight(UI_Pixels(shownHeight));
                UI_SetNextColors((UI_Colors){ .background = faint });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
            // Its edge, for pictures with clear parts.
            f32 line = MaxF32(1, floorf(0.8f * s + 0.5f));
            UI_SetNextFloatingOffset((UI_Vec2){ x - line, y - line });
            UI_SetNextPrefWidth(UI_Pixels(shownWidth + 2 * line));
            UI_SetNextPrefHeight(UI_Pixels(shownHeight + 2 * line));
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
            UI_AddBlock(UI_Flag_Floating | (audio ? 0 : UI_Flag_DrawBorder));
        }
        // Zoomed in far enough that its pixels show, the one under the pointer is read out beside it: its color, its value and where it is.
        f32 overX = UI->mouse.x - (view->rect.x0 + left + x), overY = UI->mouse.y - (view->rect.y0 + top + y);
        b32 over = UI->hotKey == view->key && !drag.active && overX >= 0 && overY >= 0 && overX < shownWidth && overY < shownHeight;
        if (!audio && over && scale >= 4 && state->previewPixels && state->previewTextureWidth > 0 && state->previewTextureHeight > 0) {
            i32 px = Min((i32)(overX / scale), (i32)width - 1), py = Min((i32)(overY / scale), (i32)height - 1);
            i32 tx = Min((i32)((f32)px * (f32)state->previewTextureWidth / width), state->previewTextureWidth - 1);
            i32 ty = Min((i32)((f32)py * (f32)state->previewTextureHeight / height), state->previewTextureHeight - 1);
            u32 pixel = state->previewPixels[(u64)ty * (u64)state->previewTextureWidth + (u64)tx];
            u32 r = pixel & 0xFF, g = (pixel >> 8) & 0xFF, b = (pixel >> 16) & 0xFF, a = pixel >> 24;
            Arena *frame = &state->frameArena;
            String value = a == 255 ? StrFormat(frame, "#%02X%02X%02X", r, g, b) : StrFormat(frame, "#%02X%02X%02X%02X", r, g, b, a);
            String where = StrFormat(frame, "%d, %d", px, py);
            f32 pad = floorf(6.4f * s + 0.5f), swatch = floorf(m->line * 0.9f), tall = m->row;
            f32 cardWidth = pad + swatch + pad + R_TextWidth(m->detail, value) + m->gap + R_TextWidth(m->detail, where) + pad;
            f32 cardX = UI->mouse.x - view->rect.x0 + floorf(16.0f * s), cardY = UI->mouse.y - view->rect.y0 + floorf(16.0f * s);
            if (cardX + cardWidth > view->rect.x1 - view->rect.x0) cardX = UI->mouse.x - view->rect.x0 - floorf(8.0f * s) - cardWidth;
            if (cardY + tall > view->rect.y1 - view->rect.y0) cardY = UI->mouse.y - view->rect.y0 - floorf(8.0f * s) - tall;
            UI_SetNextFloatingOffset((UI_Vec2){ floorf(cardX), floorf(cardY) });
            UI_SetNextPrefWidth(UI_Pixels(cardWidth));
            UI_SetNextPrefHeight(UI_Pixels(tall));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ pad, 0, pad, 0 });
            UI_SetNextCornerRadius(m->radius);
            UI_SetNextBorderThickness(m->stroke);
            UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
            UI_Block *card = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
            UI_Parent(card) {
                UI_SetNextPrefWidth(UI_Pixels(swatch));
                UI_SetNextPrefHeight(UI_Pixels(swatch));
                UI_SetNextCornerRadius(floorf(2.4f * s + 0.5f));
                UI_SetNextBorderThickness(m->stroke);
                UI_SetNextColors((UI_Colors){ .background = { (f32)r / 255.0f, (f32)g / 255.0f, (f32)b / 255.0f, 1 }, .border = theme->tabBorder });
                UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                Prog__Spacer(UI_Pixels(pad), UI_Pixels(tall));
                UI_SetNextFont(m->detail);
                Prog__Label(value, UI_TextContent(), tall, 0, theme->text, 0);
                Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(tall));
                UI_SetNextFont(m->detail);
                Prog__Label(where, UI_TextContent(), tall, 0, theme->textDim, 0);
            }
        }
        // The minimap: the whole picture dimmed in the bottom right corner, with the part in sight bright and outlined.
        if (!state->imageHideMinimap && (shownWidth > roomWidth + 0.5f || shownHeight > roomHeight + 0.5f)) {
            f32 most = floorf(240.0f * s + 0.5f), tall = floorf(160.0f * s + 0.5f), shrink = MinF32(most / width, tall / height);
            f32 mapWidth = floorf(width * shrink), mapHeight = floorf(height * shrink), edge = floorf(1.6f * s + 0.5f);
            f32 mapX = left + roomWidth - floorf(10.4f * s + 0.5f) - mapWidth, mapY = top + roomHeight - floorf(10.4f * s + 0.5f) - mapHeight;
            UI_SetNextFloatingOffset((UI_Vec2){ mapX - edge, mapY - edge });
            UI_SetNextPrefWidth(UI_Pixels(mapWidth + 2 * edge));
            UI_SetNextPrefHeight(UI_Pixels(mapHeight + 2 * edge));
            UI_SetNextBorderThickness(edge);
            UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->outline });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
            UI_SetNextFloatingOffset((UI_Vec2){ mapX, mapY });
            UI_SetNextPrefWidth(UI_Pixels(mapWidth));
            UI_SetNextPrefHeight(UI_Pixels(mapHeight));
            UI_SetNextImage(state->previewTexture, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ mapWidth, mapHeight }, (R_Color){ 0.35f, 0.35f, 0.35f, 1 });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawImage);
            R_Rect seen = {
                MaxF32(0, -x / shownWidth), MaxF32(0, -y / shownHeight),
                MinF32(1, (roomWidth - x) / shownWidth), MinF32(1, (roomHeight - y) / shownHeight),
            };
            f32 seenWidth = floorf((seen.x1 - seen.x0) * mapWidth), seenHeight = floorf((seen.y1 - seen.y0) * mapHeight);
            UI_SetNextFloatingOffset((UI_Vec2){ mapX + floorf(seen.x0 * mapWidth), mapY + floorf(seen.y0 * mapHeight) });
            UI_SetNextPrefWidth(UI_Pixels(seenWidth));
            UI_SetNextPrefHeight(UI_Pixels(seenHeight));
            UI_SetNextImage(state->previewTexture, seen, (UI_Vec2){ seenWidth, seenHeight }, (R_Color){ 1, 1, 1, 1 });
            UI_SetNextBorderThickness(MaxF32(1, floorf(1.2f * s + 0.5f)));
            UI_SetNextColors((UI_Colors){ .border = theme->text });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawImage | UI_Flag_DrawBorder);
        }
    }
}

// A toolbar button that runs `kind` on `row`, with `kind`'s tooltip or `tip`.
function void Prog__ToolbarRowButton(Prog_State *state, String identity, String icon, Prog_CommandKind kind, u64 row, String tip, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Interaction interaction = Prog__TextButton(identity, icon, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                  theme->icon, theme->text, theme->iconDisabled, true, m);
    if (tip.length) Prog__DescribeTooltip(state, kind, tip);
    else Prog__NoteTooltip(state, kind, (String){0});
    if (interaction.clicked) Prog_QueueCommand(state, kind, row, 0, interaction.clickOrder);
}

// A box that ticks a torrent's file `row` to be downloaded, or with UINT64_MAX all of them: filled with a tick when it is, with a dash when some
// of them are. It stays as it is, dimmed, while the download runs.
function void Prog__TorrentCheck(Prog_State *state, UI_Key key, u32 mark, u64 row, b32 fixed, f32 height, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 box = floorf(16.0f * m->scale + 0.5f);
    UI_SetNextPrefWidth(UI_Pixels(box));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_Parent(UI_AddBlock(0)) {
        UI_SetNextPrefWidth(UI_Pixels(box));
        UI_SetNextPrefHeight(UI_Pixels(box));
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_SetNextCornerRadius(floorf(3.2f * m->scale + 0.5f));
        UI_SetNextBorderThickness(m->stroke);
        UI_SetNextFont(m->iconsSmall);
        UI_SetNextOpacity(fixed ? 0.5f : 1);
        UI_SetNextColors((UI_Colors){ .background = theme->selection, .border = UI->hotKey == key && !fixed ? theme->text : theme->tabBorder, .text = theme->text });
        UI_SetNextText(mark == 2 ? Icon_Check : Icon_Minus);
        UI_Block *check = UI_AddBlockFromKey(key, (fixed ? 0 : UI_Flag_LeftMouseClickable) | UI_Flag_DrawBorder | (mark ? UI_Flag_DrawBackground | UI_Flag_DrawText : 0));
        UI_Interaction tick = UI_InteractionFromBlock(check);
        if (tick.clicked) Prog_QueueCommand(state, Prog_CommandKind_InspectorTorrentToggle, row, 0, tick.clickOrder);
    }
}

// A torrent's files in the inspector, ticked to be downloaded: a box over them that ticks all of them, with the torrent's name and how many of
// them and of their bytes are ticked, then each file with its folders dimmed and its size. Only the rows in sight are built, and while the
// download runs, what it chose stays as it is.
function void Prog__InspectorTorrent(Prog_State *state, const Prog_Entry *entry, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    const Prog_TorrentInfo *torrent = &state->torrent;
    f32 s = m->scale, row = m->row, gutter = Prog__ScrollGutter(m), box = floorf(16.0f * s + 0.5f);
    String path = Prog_JoinPath(frame, state->directory.path, entry->file.name);
    b32 fixed = Prog_TorrentSlot(state, path) != UINT32_MAX || Prog_TorrentWaiting(state, path) != UINT64_MAX;
    // The files shown, which leave out the padding.
    u32 *shown = ArenaPushArray(frame, torrent->fileCount, u32), count = 0, ticked = 0;
    u64 bytes = 0;
    for (u32 i = 0; i < torrent->fileCount; ++i) {
        if (torrent->files[i].pad) continue;
        shown[count++] = i;
        if (!state->torrentSkip[i]) ++ticked, bytes += torrent->files[i].size;
    }
    // Keyed, for its width from the last frame, which the name is cut to; level with the rows' sizes, left of their scroll bar's room.
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset + gutter, 0 });
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_Block *header = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TorrentHeader")), 0);
    UI_Parent(header) {
        Prog__TorrentCheck(state, UI_KeyFromStr(0, S("TorrentAll")), ticked == count ? 2 : ticked ? 1 : 0, UINT64_MAX, fixed, row, m);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(row));
        String summary = StrFormat(frame, "%S \u00B7 %S", StrFormat(frame, TrF("%u of %u"), ticked, count), Prog_FormatSize(frame, bytes));
        f32 room = MaxF32(0, header->rect.x1 - header->rect.x0 - 2 * m->inset - gutter - box - 2 * m->gap - R_TextWidth(m->detail, summary));
        Prog__Label(Prog__Ellipsize(frame, m->font, torrent->name, room), UI_Flex(), row, 0, theme->textHeading, 0);
        UI_SetNextFont(m->detail);
        Prog__LabelPadded(summary, UI_TextContent(), row, m->gap, 0, theme->textDim, 1);
    }
    Prog__Box(UI_Flex(), UI_Pixels(m->separator), UI_Layout_Horizontal, theme->tabBorder, 0);
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ 0, 0, gutter, 0 });
    UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TorrentFiles")), UI_Flag_Clip | UI_Flag_Scrollable);
    // A new torrent starts at its top.
    if (state->inspectorResetScroll) list->scrollOffset = list->scrollShown = (UI_Vec2){0}, state->inspectorResetScroll = false;
    UI_Parent(list) {
        // The rows in sight, and enough for a first frame that has no height yet.
        f32 view = MaxF32(list->rect.y1 - list->rect.y0, 40 * row);
        u32 first = (u32)Min((u64)MaxF32(0, floorf(list->scrollShown.y / row)), (u64)count);
        u32 last = (u32)Min((u64)first + (u64)ceilf(view / row) + 2, (u64)count);
        Prog__Spacer(UI_Flex(), UI_Pixels((f32)first * row));
        f32 width = list->rect.x1 - list->rect.x0 - gutter - 2 * m->inset - box - m->gap;
        // Columns level from row to row: the widest size in sight, and once how far each is known, a usage bar and its percent in what the
        // widest name in sight leaves, within bounds.
        f32 sizeWidth = 0, nameWidth = 0;
        for (u32 k = first; k < last; ++k) {
            sizeWidth = MaxF32(sizeWidth, R_TextWidth(m->detail, Prog_FormatSize(frame, torrent->files[shown[k]].size)));
            nameWidth = MaxF32(nameWidth, R_TextWidth(m->font, torrent->files[shown[k]].path));
        }
        sizeWidth += m->gap;
        f32 usageWidth = state->torrentDone ? m->gap + floorf(UI__Clamp(width - sizeWidth - nameWidth - 2 * m->gap, 96.0f * s, 240.0f * s)) : 0;
        R_Color washed = theme->selection;
        washed.a *= theme->hoverAmount.r;
        for (u32 k = first; k < last; ++k) {
            u32 index = shown[k];
            const Prog_TorrentFile *file = &torrent->files[index];
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "TorrentFile/%u", index));
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(row));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
            UI_SetNextCornerRadius(m->radius);
            UI_SetNextColors((UI_Colors){ .background = washed });
            UI_Block *line = UI_AddBlockFromKey(key, (fixed ? 0 : UI_Flag_LeftMouseClickable) | (UI->hotKey == key && !fixed ? UI_Flag_DrawBackground : 0));
            UI_Interaction pick = UI_InteractionFromBlock(line);
            if (pick.clicked) Prog_QueueCommand(state, Prog_CommandKind_InspectorTorrentToggle, index, 0, pick.clickOrder);
            UI_Parent(line) {
                Prog__TorrentCheck(state, UI_KeyFromStr(key, S("Check")), state->torrentSkip[index] ? 0 : 2, index, fixed, row, m);
                Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(row));
                // Its name whole where it fits, its folders cut first.
                String size = Prog_FormatSize(frame, file->size);
                f32 room = MaxF32(0, width - sizeWidth - usageWidth);
                Index slash = StrIndexChar(file->path, '/', SSF_SearchBackwards);
                String folders = slash.exists ? StrFromTo(file->path, 0, (i64)slash.value + 1) : (String){0}, leaf = slash.exists ? StrFrom(file->path, slash.value + 1) : file->path;
                f32 leafWidth = R_TextWidth(m->font, leaf);
                if (folders.length) Prog__Label(Prog__Ellipsize(frame, m->font, folders, MaxF32(0, room - leafWidth)), UI_TextContent(), row, 0, theme->textDim, 0);
                Prog__Label(Prog__Ellipsize(frame, m->font, leaf, room), UI_TextContent(), row, 0, state->torrentSkip[index] ? theme->textDim : theme->text, 0);
                Prog__Spacer(UI_Flex(), UI_Pixels(row));
                // How far it is once that is known: a bar filled that far, then its percent.
                if (state->torrentDone) {
                    f32 share = file->size ? (f32)Min(state->torrentDone[index], file->size) / (f32)file->size : 1;
                    Prog__UsageCell(StrFormat(frame, "%u%%", (u32)floorf(share * 100)), usageWidth, row, m->gap, 0, share >= 1 ? theme->accent : theme->textDim, m);
                }
                UI_SetNextFont(m->detail);
                Prog__LabelPadded(size, UI_Pixels(sizeWidth), row, m->gap, 0, theme->textDim, 1);
            }
        }
        Prog__Spacer(UI_Flex(), UI_Pixels((f32)(count - last) * row));
    }
    Prog__ScrollBar(list, row, S("TorrentFiles"), 1, m);
}

// Audio plays through ffmpeg, looked for once a run.
function b32 Prog__HasFfmpeg(void)
{
    local_persist i32 found = -1;
    if (found < 0) found = P_ProgramFound(S("ffmpeg"));
    return found > 0;
}

// The inspector toolbar's tools for an audio file, a video or a torrent: playing it with how far it is; the codec and quality of Compress
// Video, which Options sets too, and the button that starts it; downloading the torrent, or pausing and stopping its download.
function void Prog__InspectorTools(Prog_State *state, const Prog_Entry *entry, f32 tight, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale, bar = Prog__StripHeight(m), apart = floorf(6.4f * s + 0.5f);
    Arena *frame = &state->frameArena;
    String path = Prog_JoinPath(frame, state->directory.path, entry->file.name);
    u64 hash = MapHashBytes(path.value, path.length);
    Prog__Spacer(UI_Pixels(tight), UI_Pixels(bar));
    Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
    Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
    if (Prog_IsAudio(entry->file.name)) {
        P_Progress *progress = &state->playerProgress[state->playerSlot];
        b32 mine = state->playerJob && state->playerHash == hash, playing = mine && !atomic_load(&progress->paused);
        Prog__ToolbarRowButton(state, S("InspectorPlay"), playing ? Icon_Pause : Icon_Play, Prog_CommandKind_InspectorPlay, 0, (String){0}, m);
        if (mine) {
            u64 at = atomic_load(&progress->doneBytes), length = (u64)(state->previewSeconds * 1000);
            String clock = length ? StrFormat(frame, "%S / %S", Prog__Clock(frame, Min(at, length)), Prog__Clock(frame, length)) : Prog__Clock(frame, at);
            UI_SetNextFont(m->detail);
            Prog__LabelPadded(clock, UI_TextContent(), bar, apart, apart, theme->textDim, 0);
            if (playing) UI_KeepAnimating();
        }
        // How loud: the speaker turns the sound off and on again, and the bar beside it sets it by a press, a drag or the wheel, which what
        // plays follows as it moves; the setting is kept once it is let go.
        Prog__Spacer(UI_Pixels(apart), UI_Pixels(bar));
        u32 volume = state->volumeDragging ? state->volumeDragged : state->audioMuted ? 0 : state->audioVolume;
        String speaker = !volume ? Icon_VolumeX : volume < 34 ? Icon_Volume : volume < 67 ? Icon_Volume1 : Icon_Volume2;
        Prog__ToolbarRowButton(state, S("InspectorMute"), speaker, Prog_CommandKind_InspectorMute, 0, (String){0}, m);
        f32 track = floorf(64.0f * s + 0.5f), thick = MaxF32(2, floorf(3.2f * s + 0.5f)), knob = floorf(11.2f * s + 0.5f);
        UI_SetNextPrefWidth(UI_Pixels(track + knob));
        UI_SetNextPrefHeight(UI_Pixels(bar));
        UI_Block *slider = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorVolume")), UI_Flag_LeftMouseClickable | UI_Flag_TakesWheel);
        UI_Interaction drag = UI_InteractionFromBlock(slider);
        if (drag.pressed) state->volumeDragging = true;
        if (state->volumeDragging) state->volumeDragged = (u8)(UI__Clamp((UI->mouse.x - slider->rect.x0 - knob * 0.5f) / track, 0, 1) * 100 + 0.5f);
        atomic_store(&state->playerVolume, state->volumeDragging ? state->volumeDragged : state->audioMuted ? 0u : state->audioVolume);
        if (state->volumeDragging && !drag.active) {
            state->volumeDragging = false;
            Prog_QueueCommand(state, Prog_CommandKind_InspectorVolume, 0, 0, UINT64_MAX)->target = state->volumeDragged;
        }
        i32 notches = Prog__WheelNotches(slider);
        if (notches) Prog_QueueCommand(state, Prog_CommandKind_InspectorVolume, 0, 0, 0)->target = (u64)Max(0, Min((i32)volume + notches * 5, 100));
        UI_Parent(slider) {
            f32 y = floorf((bar - thick) * 0.5f), filled = floorf(track * (f32)volume / 100.0f + 0.5f);
            for (u32 layer = 0; layer < 2; ++layer) {
                UI_SetNextFloatingOffset((UI_Vec2){ floorf(knob * 0.5f), y });
                UI_SetNextPrefWidth(UI_Pixels(layer ? filled : track));
                UI_SetNextPrefHeight(UI_Pixels(thick));
                UI_SetNextCornerRadius(thick * 0.5f);
                UI_SetNextColors((UI_Colors){ .background = layer ? theme->accent : theme->track });
                if (!layer || filled > 0) UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
            UI_SetNextFloatingOffset((UI_Vec2){ filled, floorf((bar - knob) * 0.5f) });
            UI_SetNextPrefWidth(UI_Pixels(knob));
            UI_SetNextPrefHeight(UI_Pixels(knob));
            UI_SetNextCornerRadius(knob * 0.5f);
            UI_SetNextColors((UI_Colors){ .background = theme->text });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
        // Its percent, in room for 100% so that nothing after it moves.
        UI_SetNextFont(m->detail);
        Prog__LabelPadded(StrFormat(frame, "%u%%", volume), UI_Pixels(R_TextWidth(m->detail, S("100%")) + 2 * apart), bar, apart, apart, theme->textDim, 0);
    } else if (Prog_IsVideo(entry->file.name)) {
        local_persist const String codecKeys[3] = { SInit("InspectorCodecHevc"), SInit("InspectorCodecH264"), SInit("InspectorCodecAv1") };
        const String codecLabels[3] = { NoTr("HEVC"), NoTr("H.264"), NoTr("AV1") };
        const R_Font *codecFonts[3] = { m->detail, m->detail, m->detail };
        local_persist const String codecTips[3] = { TrInit("Compress Video encodes to HEVC (H.265): small files that most players play."),
                                                    TrInit("Compress Video encodes to H.264: larger, but plays everywhere."),
                                                    TrInit("Compress Video encodes to AV1: the smallest, for newer players and graphics cards.") };
        Prog__ChoiceGroup(state, codecKeys, codecLabels, codecFonts, codecTips, 3, state->videoCodec, Prog_CommandKind_InspectorVideoCodec, Prog_CommandKind_CompressVideo, m);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
        String tip = Tr("Compress Video's quality: lower is better and larger; 18 to 23 looks as the original does.");
        Prog__ToolbarRowButton(state, S("InspectorQualityDown"), Icon_Minus, Prog_CommandKind_InspectorVideoQuality, 0, tip, m);
        UI_SetNextFont(m->detail);
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        Prog__LabelPadded(StrFormat(frame, TrF("Q %u"), (u32)state->videoQuality), UI_Pixels(floorf(36.0f * s + 0.5f)), bar, 0, 0, theme->text, 0.5f);
        Prog__ToolbarRowButton(state, S("InspectorQualityUp"), Icon_Plus, Prog_CommandKind_InspectorVideoQuality, 1, tip, m);
        Prog__Spacer(UI_Pixels(apart), UI_Pixels(bar));
        Prog__ToolbarButton(state, S("InspectorCompress"), Icon_Shrink, Prog_CommandKind_CompressVideo, true, m);
    } else {
        u32 slot = Prog_TorrentSlot(state, path);
        u64 waiting = Prog_TorrentWaiting(state, path);
        if (waiting != UINT64_MAX) {
            // Waiting for a slot, which its button gives up.
            Prog__ToolbarRowButton(state, S("InspectorTorrentDrop"), Icon_X, Prog_CommandKind_TorrentDrop, waiting, Tr("Don't download it after all."), m);
            UI_SetNextFont(m->detail);
            Prog__LabelPadded(Tr("Waiting for a free slot"), UI_TextContent(), bar, apart, apart, theme->textDim, 0);
        } else if (slot == UINT32_MAX) {
            // Not without a file ticked that is still to come, once its files are known.
            b32 any = !state->torrent.fileCount;
            for (u32 i = 0; i < state->torrent.fileCount && state->torrentSkip; ++i) {
                b32 whole = state->torrentDone && state->torrentDone[i] >= state->torrent.files[i].size;
                any |= !state->torrentSkip[i] && !state->torrent.files[i].pad && !whole;
            }
            Prog__ToolbarButton(state, S("InspectorDownload"), Icon_Download, Prog_CommandKind_DownloadTorrent, any, m);
        } else {
            P_Progress *progress = &state->transfers[slot].progress;
            b32 paused = atomic_load(&progress->paused);
            Prog__ToolbarRowButton(state, S("InspectorTorrentPause"), paused ? Icon_Play : Icon_Pause, Prog_CommandKind_TransferPause, slot,
                                   paused ? Tr("Go on with the download.") : Tr("Pause the download."), m);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(bar));
            Prog__ToolbarRowButton(state, S("InspectorTorrentCancel"), Icon_X, Prog_CommandKind_TransferCancel, slot, Tr("Stop the download; downloading it again goes on from here."), m);
            // How far it is, with its speed, peers and time left.
            u64 bytes = atomic_load(&progress->totalBytes), done = atomic_load(&progress->doneBytes);
            String status = Prog__TransferName(frame, progress);
            Index dot = StrIndexStr(status, S(" \u00B7 "), SSF_None);
            String text = bytes ? StrFormat(frame, "%u%%", (u32)(Min(done, bytes) * 100 / bytes)) : Tr("Starting...");
            if (dot.exists) text = StrFormat(frame, "%S%S", text, StrFrom(status, dot.value));
            UI_SetNextFont(m->detail);
            Prog__LabelPadded(text, UI_TextContent(), bar, apart, apart, theme->textDim, 0);
        }
    }
}

// An item's name over its type and size, or a folder's counts, and when it was changed, a line each.
function void Prog__InspectorFacts(Prog_State *state, const Prog_Entry *entry, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    Prog__Label(entry->file.name, UI_Flex(), m->row, 0, theme->textHeading, 0);
    String type = Prog_Tr(entry->type);
    String details = entry->file.kind == P_FileKind_Directory
        ? (entry->measured ? StrFormat(frame, "%S, %S, %S, %S", type, StrFormat(frame, TrN(entry->folders, "%u folder", "%u folders"), (u32)entry->folders),
                                       StrFormat(frame, TrN(entry->files, "%u file", "%u files"), (u32)entry->files), Prog_FormatSize(frame, entry->bytes)) : type)
        : StrFormat(frame, "%S, %S", type, Prog_FormatSize(frame, entry->file.size));
    Prog__Label(details, UI_Flex(), m->row, 0, theme->textDim, 0);
    Prog__Label(Prog_EntryModified(frame, entry), UI_Flex(), m->row, 0, theme->textDim, 0);
}

// The inspector beside the list or below it, `size` wide or tall with its divider:
// a toolbar with its layouts, the text view's switches and the item's name, the focused item's text, picture or folder,
// and a footer with the filter and what the item holds.
function void Prog__Inspector(Prog_State *state, f32 size, b32 below, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale, bar = Prog__StripHeight(m);
    Prog__Box(below ? UI_Flex() : UI_Pixels(m->separator), below ? UI_Pixels(m->separator) : UI_Flex(), UI_Layout_Horizontal, theme->tabBorder, 0);
    f32 inner = size - m->separator;
    const Prog_Entry *entry = !state->loading && state->hasFocus && state->focusedRow < state->directory.count ? &state->directory.entries[state->focusedRow] : NULL;
    b32 text = entry && !state->previewPicture && ((state->previewText.length && state->previewLines) || state->previewHex);
    b32 pdf = entry && state->pdf.pageCount && state->previewImage;
    b32 model = entry && state->model.mesh.u64[0];
    b32 image = entry && state->previewImage && !pdf;
    b32 archive = entry && Prog_PreviewsArchive(state, entry);
    b32 folder = entry && (entry->file.kind == P_FileKind_Directory || archive);
    UI_SetNextPrefWidth(below ? UI_Flex() : UI_Pixels(inner));
    UI_SetNextPrefHeight(below ? UI_Pixels(inner) : UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *pane = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Inspector")), UI_Flag_DrawBackground | UI_Flag_Clip);
    state->panels[state->currentPanel].inspectorRect = pane->rect;
    f32 room = 0;
    UI_Parent(pane) {
        UI_Block *toolbar = Prog__Box(UI_Flex(), UI_Pixels(bar), UI_Layout_Horizontal, theme->background, 0);
        UI_Parent(toolbar) {
            // The groups sit a little closer to the lines between them than the list toolbar's buttons do.
            f32 tight = m->gap - floorf(1.6f * s + 0.5f);
            Prog__Spacer(UI_Pixels(floorf(3.2f * s + 0.5f)), UI_Pixels(bar));
            Prog__ToolbarButton(state, S("InspectorClose"), Icon_X, Prog_CommandKind_TogglePreview, true, m);
            Prog__Spacer(UI_Pixels(tight), UI_Pixels(bar));
            Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
            // The layouts, in a dashed outline, the one chosen lit.
            local_persist const String layoutKeys[Prog_InspectorLayout_Count] = { SInit("InspectorLayoutAuto"), SInit("InspectorLayoutRight"), SInit("InspectorLayoutBelow") };
            const String layoutIcons[Prog_InspectorLayout_Count] = { Icon_PanelsTopLeft, Icon_PanelLeft, Icon_PanelTop };
            Prog__Spacer(UI_Pixels(tight), UI_Pixels(bar));
            local_persist const String layoutTips[Prog_InspectorLayout_Count] = {
                TrInit("Auto positions the inspector to the right or below based on available space."), TrInit("Forces the inspector to the right."), TrInit("Forces the inspector below."),
            };
            Prog__ChoiceGroup(state, layoutKeys, layoutIcons, NULL, layoutTips, Prog_InspectorLayout_Count, state->inspectorLayout, Prog_CommandKind_InspectorSetLayout,
                              Prog_CommandKind_InspectorChangeLayout, m);
            // An audio file's waveform and a video's frame leave the zoom to the pictures, for their own tools.
            b32 media = entry && (Prog_IsAudio(entry->file.name) || Prog_IsVideo(entry->file.name));
            if (image && !media) {
                // An image's scales the same way, then its grid and minimap.
                local_persist const String scaleKeys[Prog_ImageScale_Count] = { SInit("InspectorScaleFit"), SInit("InspectorScaleOriginal"), SInit("InspectorScaleCustom") };
                const String scaleLabels[Prog_ImageScale_Count] = { Icon_Minimize2, S("1:1"), Icon_Maximize2 };
                const R_Font *scaleFonts[Prog_ImageScale_Count] = { NULL, m->detail, NULL };
                local_persist const String scaleTips[Prog_ImageScale_Count] = {
                    TrInit("Display image scaled to fit within the viewport."), TrInit("Display image at its original source size, no scaling."),
                    TrInit("Display image with a custom scale adjustment."),
                };
                Prog__Spacer(UI_Pixels(tight), UI_Pixels(bar));
                Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
                Prog__Spacer(UI_Pixels(tight), UI_Pixels(bar));
                Prog__ChoiceGroup(state, scaleKeys, scaleLabels, scaleFonts, scaleTips, Prog_ImageScale_Count, state->imageScaleMode, Prog_CommandKind_InspectorSetImageScale,
                                  Prog_CommandKind_InspectorToggleImageScale, m);
                Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
                UI_Interaction grid = Prog__TextButton(S("InspectorGrid"), Icon_Hash, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                       state->imageHideGrid ? theme->iconDisabled : theme->icon, theme->text, theme->iconDisabled, true, m);
                Prog__NoteTooltip(state, Prog_CommandKind_InspectorToggleImageGrid, (String){0});
                if (grid.clicked) Prog_QueueCommand(state, Prog_CommandKind_InspectorToggleImageGrid, 0, 0, grid.clickOrder);
                Prog__ToolbarButton(state, S("InspectorMinimap"), state->imageHideMinimap ? Icon_LocateOff : Icon_Locate, Prog_CommandKind_InspectorToggleImageMinimap, true, m);
            }
            if (entry && ((Prog_IsAudio(entry->file.name) && Prog__HasFfmpeg()) || Prog_IsVideo(entry->file.name) || Prog_IsTorrent(entry->file.name))) {
                Prog__InspectorTools(state, entry, tight, m);
            }
            if (text) {
                f32 apart = floorf(6.4f * s + 0.5f);
                Prog__Spacer(UI_Pixels(tight), UI_Pixels(bar));
                Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
                Prog__Spacer(UI_Pixels(m->gap - floorf(0.8f * s + 0.5f)), UI_Pixels(bar));
                b32 wraps = state->previewDocument ? !state->documentUnwrapped : state->inspectorWrap;
                Prog__ToolbarButton(state, S("InspectorWrap"), wraps ? Icon_TextWrap : Icon_TextAlignStart, Prog_CommandKind_InspectorToggleWordWrap, true, m);
                Prog__Spacer(UI_Pixels(apart), UI_Pixels(bar));
                Prog__ToolbarButton(state, S("InspectorNumbers"), state->inspectorHideNumbers ? Icon_ListX : Icon_ListOrdered, Prog_CommandKind_InspectorToggleLineNumbers, true, m);
                Prog__Spacer(UI_Pixels(apart), UI_Pixels(bar));
                Prog__ToolbarButton(state, S("InspectorWhitespace"), state->inspectorWhitespace ? Icon_Minus : Icon_Space, Prog_CommandKind_InspectorToggleWhitespace, true, m);
            }
            if (folder) {
                u64 owner = state->panels[state->currentPanel].id;
                if (Prog__EnterInspectorPanel(state, owner) != UINT32_MAX) {
                    Prog__Spacer(UI_Pixels(floorf(14.4f * s + 0.5f)), UI_Pixels(bar));
                    Prog__FolderButtons(state, floorf(10.4f * s + 0.5f), m);
                    Prog__LeaveInspectorPanel(state, owner);
                }
            }
            // The item's permissions, where the system has them, after a line.
            if (entry && entry->file.mode) {
                Prog__Spacer(UI_Pixels(tight), UI_Pixels(bar));
                Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
                Prog__Spacer(UI_Pixels(tight), UI_Pixels(bar));
                Prog__PermissionChips(state, entry, bar, m);
            }
            // Where the controls end, which the card of the details reaches to.
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(bar));
            UI_Block *rest = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorToolbarRest")), 0);
            room = rest->rect.x0 - pane->rect.x0;
            // The item's name, cut to the room the controls leave beside the button for the card of its details.
            String name = entry ? entry->file.name : Prog_PathName(state, state->directory.path);
            f32 after = floorf(m->inset * 0.4f), button = entry ? m->gap + Prog__ToolbarButtonWidth(m) : 0;
            f32 fits = MaxF32(0, pane->rect.x1 - rest->rect.x0 - button - after);
            Prog__Label(Prog__Ellipsize(&state->frameArena, m->font, name, fits), UI_TextContent(), bar, 0, theme->text, 0);
            if (entry) {
                Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
                Prog__ToolbarButton(state, S("InspectorDetailsButton"), Icon_ChevronRight, Prog_CommandKind_InspectorToggleDetails, true, m);
            }
            Prog__Spacer(UI_Pixels(after), UI_Pixels(bar));
        }
        if (entry && state->torrent.fileCount && state->torrentSkip) {
            Prog__InspectorTorrent(state, entry, m);
        } else if (text && state->previewHex) {
            Prog__InspectorHex(state, m);
        } else if (text) {
            Prog__InspectorText(state, m);
        } else if (pdf) {
            Prog__InspectorPdf(state, m);
        } else if (image && Prog_IsAudio(entry->file.name)) {
            // An audio file's facts over its waveform, which takes the rest.
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(3 * m->row + m->inset));
            UI_SetNextChildrenLayout(UI_Layout_Vertical);
            UI_SetNextChildrenPadding((UI_Padding){ m->inset, m->inset, m->inset, 0 });
            UI_Parent(UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorFacts")), UI_Flag_Clip)) Prog__InspectorFacts(state, entry, m);
            Prog__InspectorImage(state, m);
        } else if (image) {
            Prog__InspectorImage(state, m);
        } else if (folder) {
            if (archive) {
                String summary = state->previewArchiveCounted ? Prog_ArchiveSummary(&state->frameArena, state->previewArchiveTotals, entry->file.size) : Tr("Counting...");
                Prog__Label(summary, UI_Flex(), m->row, m->inset, theme->textDim, 0);
            }
            Prog__InspectorList(state, m);
        } else {
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Flex());
            UI_SetNextChildrenLayout(UI_Layout_Vertical);
            UI_SetNextChildrenPadding((UI_Padding){ m->inset, m->inset, m->inset, m->inset });
            UI_Block *body = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorBody")), UI_Flag_Clip);
            UI_Parent(body) {
                f32 room = (pane->rect.x1 - pane->rect.x0) - 2 * m->inset;
                if (!entry) {
                    Prog__Label(Tr("Nothing to preview"), UI_Flex(), m->row, 0, theme->textDim, 0.5f);
                } else {
                    // A picture as wide as the pane, but no taller than half of it.
                    f32 side = floorf(MinF32(room, MaxF32(0, (body->rect.y1 - body->rect.y0) * 0.5f)));
                    // A model takes all the height its details leave, to be turned in.
                    f32 belowModel = 3 * m->row + 2 * m->inset;
                    if (model && body->rect.y1 - body->rect.y0 > belowModel) {
                        Prog__InspectorModel(state, floorf(body->rect.y1 - body->rect.y0 - belowModel), m);
                        Prog__Spacer(UI_Flex(), UI_Pixels(m->inset));
                    } else if (state->previewPicture && side > 0) {
                        UI_SetNextPrefWidth(UI_Flex());
                        UI_SetNextPrefHeight(UI_Pixels(side));
                        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                        UI_SetNextImage(state->previewTexture, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ side, side }, (R_Color){ 1, 1, 1, 1 });
                        UI_AddBlock(UI_Flag_DrawImage);
                        Prog__Spacer(UI_Flex(), UI_Pixels(m->inset));
                    }
                    Prog__InspectorFacts(state, entry, m);
                    // A file inside an archive is taken out before it shows, which a large one takes a while for.
                    P_Progress *taking = state->previewJob ? ((Prog_JobInput *)state->previewJob->input)->progress : NULL;
                    u64 total = taking ? atomic_load(&taking->totalBytes) : 0, done = total ? Min(atomic_load(&taking->doneBytes), total) : 0;
                    if (total && UI->nowMicroseconds - Min(state->previewSubmitted, UI->nowMicroseconds) >= 400000) {
                        f32 part = (f32)((f64)done / (f64)total), barWidth = floorf(MinF32(room, m->line * 16)), barHeight = MaxF32(3, floorf(4 * m->scale));
                        Prog__Spacer(UI_Flex(), UI_Pixels(m->inset));
                        Prog__Label(StrFormat(&state->frameArena, TrF("Extracting it from the archive to preview, %u%%"), (u32)(part * 100)), UI_Flex(), m->row, 0, theme->text, 0);
                        UI_SetNextPrefWidth(UI_Pixels(barWidth));
                        UI_SetNextPrefHeight(UI_Pixels(barHeight));
                        UI_SetNextCornerRadius(barHeight * 0.5f);
                        UI_SetNextColors((UI_Colors){ .background = theme->track });
                        UI_Block *track = UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_Clip);
                        UI_Parent(track) {
                            UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                            UI_SetNextPrefWidth(UI_Pixels(floorf(barWidth * part)));
                            UI_SetNextPrefHeight(UI_Pixels(barHeight));
                            UI_SetNextCornerRadius(barHeight * 0.5f);
                            UI_SetNextColors((UI_Colors){ .background = theme->accent });
                            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                        }
                        Prog__Label(StrFormat(&state->frameArena, TrF("%S of %S"), Prog_FormatSize(&state->frameArena, done), Prog_FormatSize(&state->frameArena, total)), UI_Flex(), m->row, 0,
                                    theme->textDim, 0);
                    }
                    if (taking) UI_KeepAnimating();
                }
            }
        }
        // The footer: the text's filter, then its encoding, its line count and its size in points; a folder's list has its own status bar.
        f32 radius = floorf(10.4f * s + 0.5f);
        UI_Block *footer = NULL;
        if (!folder) {
            UI_SetNextChildrenPadding((UI_Padding){ -radius, 0, -radius, 0 });
            footer = Prog__Box(UI_Flex(), UI_Pixels(m->pill), UI_Layout_Horizontal, theme->background, 0);
        }
        if (footer) UI_Parent(footer) {
            // In the hex view the box finds bytes, written as hex, or text.
            if (text) Prog__InspectorFilterBox(state, state->previewHex ? Tr("Find bytes or text...") : StrFormat(&state->frameArena, TrF("Filter %u lines..."), state->previewWindowed && state->textIndex.done ? (u32)Min(state->textIndex.lines, (u64)UINT32_MAX) : state->previewLineCount), m);
            Prog__Spacer(UI_Flex(), UI_Pixels(m->pill));
            if (pdf) {
                String pages = StrFormat(&state->frameArena, TrN(state->pdf.pageCount, "%u page", "%u pages"), state->pdf.pageCount);
                Prog__LabelPadded(pages, UI_TextContent(), m->pill, 0, radius, theme->textDim, 0);
            }
            if (image && !Prog_IsAudio(entry->file.name)) {
                // An image's scale, filled up to its own size; a waveform, which fills the view, has none.
                f32 scale = state->imageScaleMode == Prog_ImageScale_Fit ? MinF32(state->imageFit, 1) : state->imageScaleMode == Prog_ImageScale_Original ? 1 : state->imageScale;
                UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, radius + floorf(4.8f * s + 0.5f), 0 });
                UI_SetNextCornerRadius(radius);
                UI_SetNextBorderThickness(m->stroke);
                UI_Block *zoom = Prog__Box(UI_Fill(), UI_Pixels(m->pill + radius), UI_Layout_Horizontal, theme->panel, UI_Flag_DrawBorder);
                zoom->props.colors.border = theme->tabBorder;
                zoom->props.childrenAlignment = (UI_Vec2){ 0, 0 };
                UI_Parent(zoom) {
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                    UI_Block *slot = Prog__Box(UI_Pixels(floorf(m->line * 2.4f)), UI_Pixels(m->pill), UI_Layout_Horizontal, (R_Color){0}, 0);
                    UI_Parent(slot) Prog__FillBar(StrFormat(&state->frameArena, "%u%%", (u32)(scale * 100.0f + 0.5f)), MinF32(scale, 1), m);
                }
            }
            if (text) {
                UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, radius + floorf(4.8f * s + 0.5f), 0 });
                UI_SetNextCornerRadius(radius);
                UI_SetNextBorderThickness(m->stroke);
                UI_Block *facts = Prog__Box(UI_Fill(), UI_Pixels(m->pill + radius), UI_Layout_Horizontal, theme->panel, UI_Flag_DrawBorder);
                facts->props.colors.border = theme->tabBorder;
                facts->props.childrenAlignment = (UI_Vec2){ 0, 0 };
                UI_Parent(facts) {
                    f32 before = floorf(7.2f * s + 0.5f);
                    Prog__LabelPadded(state->previewHex ? NoTr("Hex") : state->previewArchive ? Tr("Archive") : S("UTF-8"), UI_TextContent(), m->pill, before, floorf(13.6f * s + 0.5f), theme->text, 0);
                    Prog__Icon(Icon_TextAlignStart, 0, m->pill, theme->icon, 0.5f, m);
                    // The lines the filter keeps out of all of them, of a big file's window those it holds until those of the whole file are
                    // found, with a plus where there were more than it holds; the whole file's once counted; for hex, the file's size.
                    u64 every = state->previewWindowed && state->textIndex.done ? state->textIndex.lines : state->textFound ? state->previewFirstLine + state->textSaved.count : state->previewLineCount;
                    u64 among = state->textFound ? every : state->previewLineCount;
                    String lines = state->previewHex ? Prog_FormatSize(&state->frameArena, state->hexSize)
                                 : state->inspectorFilter.length ? StrFormat(&state->frameArena, "%u%s/%llu", state->previewKeptCount, state->textFound && state->textFind.cut ? "+" : "", (unsigned long long)among)
                                 : StrFormat(&state->frameArena, "%llu", (unsigned long long)every);
                    Prog__LabelPadded(lines, UI_TextContent(), m->pill, floorf(10.4f * s + 0.5f), floorf(12.0f * s + 0.5f), theme->text, 0);
                    // The text's size, filled from the least to the most it can be.
                    u32 points = state->inspectorFontSize ? state->inspectorFontSize : Prog_InspectorFontSize;
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                    UI_Block *slot = Prog__Box(UI_Pixels(floorf(m->line * 2.4f)), UI_Pixels(m->pill), UI_Layout_Horizontal, (R_Color){0}, 0);
                    UI_Parent(slot) Prog__FillBar(StrFormat(&state->frameArena, "%u", points), (f32)(points - Prog_InspectorFontMin) / (f32)(Prog_InspectorFontMax - Prog_InspectorFontMin), m);
                }
            }
        }
        if (entry && state->inspectorDetails) Prog__InspectorDetails(state, entry, pane->rect.x1 - pane->rect.x0, room, m);
    }
}

// A Miller column: the items of `column`'s folder by name, with `lit`, the one on the way to the panel's folder, marked as selected.
function void Prog__MillerColumnUI(Prog_State *state, Prog_MillerColumn *column, String lit, f32 width, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    u32 slot = (u32)(column - state->millerColumns->v);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, floorf(4.8f * m->scale + 0.5f), m->inset, 0 });
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(frame, "Miller/%S", column->path)), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable);
    if (!column->listed) {
        UI_Parent(list) Prog__Label(Tr("Loading..."), UI_Flex(), m->row, m->gap, theme->textDim, 0);
        return;
    }
    u64 litRow = UINT64_MAX;
    for (u64 i = 0; lit.length && i < column->count && litRow == UINT64_MAX; ++i) if (StrIsEqual(column->items[i].file.name, lit)) litRow = i;
    f32 pitch = m->row, height = MaxF32(0, list->rect.y1 - list->rect.y0);
    // The lit row is brought into sight the first time the column has room to show it.
    if (!column->revealed && height > 0) {
        if (litRow != UINT64_MAX && (f32)(litRow + 1) * pitch > height) list->scrollOffset.y = list->scrollShown.y = MaxF32(0, (f32)litRow * pitch + pitch * 0.5f - height * 0.5f);
        column->revealed = true;
    }
    // Only the rows near the view get blocks.
    UI_LimitScrollEase(list);
    u64 first = (u64)(MaxF32(0, MinF32(list->scrollOffset.y, list->scrollShown.y)) / pitch), end = column->count;
    first = first > 2 ? first - 2 : 0;
    end = Min(end, first + (u64)((height + fabsf(list->scrollOffset.y - list->scrollShown.y)) / pitch) + 6);
    UI_Parent(list) {
        if (first) Prog__Spacer(UI_Flex(), UI_Pixels((f32)first * pitch));
        for (u64 row = Min(first, column->count); row < end; ++row) {
            const Prog_Entry *entry = &column->items[row];
            UI_Key key = UI_KeyFromStr(list->key, entry->file.name);
            b32 hot = UI->hotKey == key, folder = entry->file.kind == P_FileKind_Directory;
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(pitch));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_Block *line = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable);
            UI_Interaction press = UI_InteractionFromBlock(line);
            String path = Prog_JoinPath(frame, column->path, entry->file.name);
            // A click goes to its folder with it selected, on the release, as the columns move along then; a drag takes it out as the
            // list's items are, and a right press opens its own menu.
            if (press.clicked && press.button == MouseButton_Left) Prog_QueueCommand(state, Prog_CommandKind_MillerSelect, row, press.pressCount, press.clickOrder)->target = slot;
            if (press.active && press.isDragging && press.button == MouseButton_Left && !state->dragging && !state->dragRequested) {
                state->dragPlace = StrCopy(&state->dropPlaceArena, path);
                state->dragRequested = true;
            }
            if (press.pressed && press.button == MouseButton_Right) Prog_QueueCommand(state, Prog_CommandKind_MillerMenu, row, 0, press.pressOrder)->target = slot;
            if (hot) state->hoverMenuPath = path;
            // Under a drag, a folder takes it in, lit, as the list's folders do.
            b32 dropping = folder && P_DropPending() && UI__Contains(line->rect, UI->mouse);
            if (dropping) state->dropPlace = StrCopy(&state->dropPlaceArena, path);
            UI_Parent(line) {
                Prog__ItemBox(Prog__ItemLook(row == litRow || dropping, hot, false), UI_Percent(1), m);
                const Prog_Icon *icon = Prog_PathIcon(state, path, entry->file.kind);
                f32 size = (f32)state->iconSize;
                if (icon) {
                    UI_SetNextPrefWidth(UI_Pixels(m->iconSlot - m->gap));
                    UI_SetNextPrefHeight(UI_Pixels(pitch));
                    UI_SetNextTextAlignment((UI_Vec2){ 1, 0.5f });
                    UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ size, size }, (R_Color){ 1, 1, 1, entry->file.hidden ? theme->hiddenAmount.r : 1 });
                    UI_AddBlock(UI_Flag_DrawImage);
                } else {
                    Prog__Icon(folder ? Icon_Folder : Icon_File, m->iconSlot - m->gap, pitch, theme->icon, 1, m);
                }
                Prog__Label(entry->file.name, UI_Flex(), pitch, m->gap, entry->file.hidden ? theme->textDim : theme->text, 0);
                // A folder goes on into another column.
                if (folder) Prog__Icon(Icon_ChevronRight, m->iconSlot - m->gap, pitch, theme->iconDisabled, 0.5f, m);
            }
        }
        if (end < column->count) Prog__Spacer(UI_Flex(), UI_Pixels((f32)(column->count - end) * pitch));
    }
}

// Miller columns: the shown folder's parents left of its list and the focused folder's items right of it, as many as fit at a column's
// width, which they share. The focus on a file leaves the right column empty, as its preview is the inspector's.
function void Prog__MillerView(Prog_State *state, f32 width, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    u32 fit = (u32)MaxF32(1, floorf(width / floorf(240.0f * m->scale + 0.5f)));
    String parents[Prog_MillerSlots] = {0};
    u32 parentCount = 0;
    for (String at = state->directory.path; fit >= 3 && parentCount < Min(fit - 2, (u32)Prog_MillerSlots - 2);) {
        String up = Prog_UpPath(at);
        if (!up.length || StrIsEqual(up, at) || StrIsEqual(up, Prog_ThisPC)) break;
        parents[parentCount++] = up;
        at = up;
    }
    String next = {0};
    if (fit >= 2 && !state->loading && state->hasFocus && state->focusedRow < state->directory.count) {
        const Prog_Entry *entry = &state->directory.entries[state->focusedRow];
        if (entry->file.kind == P_FileKind_Directory || Prog_PreviewsArchive(state, entry)) next = Prog_JoinPath(frame, state->directory.path, entry->file.name);
    }
    u32 count = parentCount + 1 + (fit >= 2 ? 1 : 0);
    f32 share = floorf((width - (f32)(count - 1) * m->separator) / (f32)count), own = width - (f32)(count - 1) * (share + m->separator);
    for (u32 i = parentCount; i-- > 0;) {
        String below = i ? parents[i - 1] : state->directory.path;
        Prog__MillerColumnUI(state, Prog_MillerList(state, parents[i]), Prog__FolderName(below), share, m);
        Prog__Box(UI_Pixels(m->separator), UI_Flex(), UI_Layout_Horizontal, theme->tabBorder, 0);
    }
    UI_Block *middle = Prog__Box(UI_Pixels(own), UI_Flex(), UI_Layout_Vertical, theme->background, 0);
    UI_Parent(middle) {
        Prog_Columns columns = Prog__Columns(state, m->scale, own - 2 * m->inset - (state->listScrolls ? Prog__ScrollGutter(m) : 0));
        Prog__List(state, &columns, m);
    }
    if (fit < 2) return;
    Prog__Box(UI_Pixels(m->separator), UI_Flex(), UI_Layout_Horizontal, theme->tabBorder, 0);
    if (next.length) Prog__MillerColumnUI(state, Prog_MillerList(state, next), (String){0}, share, m);
    else Prog__Box(UI_Pixels(share), UI_Flex(), UI_Layout_Vertical, theme->background, 0);
}

// The key seed of panel `id`'s blocks; a nonzero parent keeps it from depending on the seed in use.
function UI_Key Prog_PanelKeySeed(u64 id)
{
    return UI_KeyFromStr(1, StrMake((const char *)&id, sizeof(id)));
}

// One panel at `rect`: its tab strip over its toolbar, column header and list; only the active panel has the filter and status bar, and the others are dimmed.
function void Prog__PanelUI(Prog_State *state, u32 index, R_Rect rect, R_Rect area, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog__UsePanel(state, index);
    // Keys of blocks without a parent start from the panel's, so that panels showing the same folder keep apart.
    UI->keySeed = Prog_PanelKeySeed(state->panels[index].id);
    state->panelRect = rect;
    if (UI__Contains(rect, UI->mouse)) state->pointerPanel = state->panels[index].id;
    f32 width = rect.x1 - rect.x0, height = rect.y1 - rect.y0;
    // A panel whose inspector's list has the keyboard counts as active.
    b32 top = rect.y0 <= area.y0, active = index == state->activePanel || state->panels[state->activePanel].inspectorOf == state->panels[index].id;
    // A panel whose shown tab has its inspector open gives half of itself under the tab strip to it:
    // the right half, or the lower half when the panel is taller than wide or when asked.
    b32 inspecting = state->previewShown;
    f32 mainHeight = height - (top ? m->separator : 0) - (top ? Prog__TitleStripHeight(m) : Prog__StripHeight(m));
    b32 below = state->inspectorLayout == Prog_InspectorLayout_Below || (state->inspectorLayout == Prog_InspectorLayout_Auto && mainHeight > width);
    if (inspecting) state->inspectorBelow = below;
    f32 share = state->inspectorShare > 0 ? state->inspectorShare : 0.5f;
    f32 previewWidth = inspecting && !below ? floorf((width - m->separator) * share) : 0;
    f32 previewHeight = inspecting && below ? floorf((mainHeight - m->separator) * share) : 0;
    f32 listWidth = width - previewWidth;
    Prog_Columns columns = Prog__Columns(state, scale, listWidth - 2 * m->inset - (state->listScrolls ? Prog__ScrollGutter(m) : 0));
    // A panel a split just made zooms in from 60% of its size about its middle, easing with a 38 ms time constant.
    u64 split = state->panels[index].splitMicroseconds;
    if (split && UI->animate) {
        f32 grown = 1 - expf(-(f32)(UI->nowMicroseconds - Min(split, UI->nowMicroseconds)) / 38000.0f);
        if (grown < 0.995f) {
            UI_SetNextScale(0.6f + 0.4f * grown);
            UI_KeepAnimating();
        } else {
            state->panels[index].splitMicroseconds = 0;
        }
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_Block *panel = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(panel) {
        // The window's top border, over the title bar.
        if (top) Prog__Box(UI_Flex(), UI_Pixels(m->separator), UI_Layout_Horizontal, theme->tabBorder, 0);
        Prog__TabStrip(state, width, top, top && (state->sidebarHidden || state->uiHidden) && rect.x0 <= area.x0, top && rect.x1 >= area.x1, scale, m);
        UI_Block *main = Prog__Box(UI_Flex(), UI_Flex(), below ? UI_Layout_Vertical : UI_Layout_Horizontal, theme->background, 0);
        UI_Parent(main) {
            // The status bar slides up into the panel that becomes active and down out of the one that stops being it, the first more
            // slowly; the one that stops being it fades to dim, while the new one brightens at once.
            Prog_Panel *own = &state->panels[index];
            if (!own->drawn || own->wasActive != active) {
                // A panel's first frame shows it settled, so the window does not open with a slide.
                if (own->drawn) own->focusMicroseconds = UI->nowMicroseconds;
                own->wasActive = (b8)active;
                own->drawn = true;
            }
            // Hide UI leaves a panel its status bar only while it filters, as the filter's box is in it.
            f32 shown = active && (!state->uiHidden || state->filtering || state->filter.length) ? 1 : 0, dimmed = active ? 0 : 1;
            if (own->focusMicroseconds && UI->animate) {
                f32 since = (f32)(UI->nowMicroseconds - Min(own->focusMicroseconds, UI->nowMicroseconds));
                if (shown > 0 || !active) shown = active ? 1 - expf(-since / 60000.0f) : expf(-since / 28000.0f);
                if (!active) dimmed = 1 - expf(-since / 25000.0f);
                if (since < 400000) UI_KeepAnimating();
                else own->focusMicroseconds = 0;
            }
            UI_Block *listSide = Prog__Box(below ? UI_Flex() : UI_Pixels(listWidth), below ? UI_Pixels(mainHeight - previewHeight) : UI_Flex(), UI_Layout_Vertical, theme->background, 0);
            UI_Parent(listSide) {
                if (!state->uiHidden) Prog__Toolbar(state, m);
                UI_Block *listColumn = Prog__Box(UI_Flex(), UI_Flex(), state->miller ? UI_Layout_Horizontal : UI_Layout_Vertical, theme->background, 0);
                UI_Parent(listColumn) {
                    if (state->miller) {
                        Prog__MillerView(state, below ? width : listWidth, m);
                    } else {
                        if (Prog_ZoomView(state->zoom) == Prog_View_Details) {
                            Prog__Header(state, &columns, m);
                            Prog__Box(UI_Flex(), UI_Pixels(floorf(4.8f * scale + 0.5f)), UI_Layout_Horizontal, theme->background, 0);
                        }
                        Prog__List(state, &columns, m);
                    }
                }
                if (shown > 0.01f) {
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(floorf(m->pill * shown + 0.5f)));
                    UI_SetNextChildrenLayout(UI_Layout_Vertical);
                    UI_Block *slide = UI_AddBlock(UI_Flag_Clip);
                    UI_Parent(slide) Prog__StatusBar(state, m);
                }
            }
            if (inspecting) Prog__Inspector(state, below ? previewHeight : previewWidth, below, m);
            if (dimmed > 0.01f) {
                UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                UI_SetNextPrefWidth(UI_Pixels(width));
                UI_SetNextPrefHeight(UI_Pixels(MaxF32(0, height - m->bar)));
                R_Color dim = theme->background;
                dim.a = (1.0f - (f32)(60 + state->inactiveShift) / 100.0f) * dimmed;
                UI_SetNextColors((UI_Colors){ .background = dim });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
            // Every panel shows its zones from the start of a drag, over the dimming so that an inactive panel's show as clearly.
            if (state->tabDrag) Prog__DropZones(state, (R_Rect){ rect.x0, rect.y0 + m->bar, rect.x1, rect.y1 }, 0, m);
        }
    }
}

// The layout tree below `node` filling `rect`, its splits parted by dividers that drag to resize; `area` is the whole tree's rect.
function void Prog__Layout(Prog_State *state, u32 node, R_Rect rect, R_Rect area, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const Prog_Split *split = &state->splits[node];
    if (split->kind == Prog_SplitKind_Panel) {
        // The zoomed panel is drawn over the others; its place here stays empty.
        if (split->panel == state->zoomShownPanel) {
            state->zoomFrom = rect;
            Prog__Spacer(UI_Pixels(rect.x1 - rect.x0), UI_Pixels(rect.y1 - rect.y0));
            return;
        }
        u32 index = Prog_PanelIndex(state, split->panel);
        if (index != UINT32_MAX) Prog__PanelUI(state, index, rect, area, scale, m);
        return;
    }
    if (split->kind != Prog_SplitKind_Horizontal && split->kind != Prog_SplitKind_Vertical) return;
    b32 across = split->kind == Prog_SplitKind_Horizontal;
    f32 length = across ? rect.x1 - rect.x0 : rect.y1 - rect.y0, line = m->separator;
    f32 first = floorf(MaxF32(0, length - line) * (split->ratio > 0 ? split->ratio : 0.5f) + 0.5f);
    R_Rect a = rect, b = rect;
    if (across) { a.x1 = rect.x0 + first; b.x0 = a.x1 + line; }
    else        { a.y1 = rect.y0 + first; b.y0 = a.y1 + line; }
    UI_SetNextPrefWidth(UI_Pixels(rect.x1 - rect.x0));
    UI_SetNextPrefHeight(UI_Pixels(rect.y1 - rect.y0));
    UI_SetNextChildrenLayout(across ? UI_Layout_Horizontal : UI_Layout_Vertical);
    UI_Block *box = UI_AddBlock(0);
    UI_Parent(box) {
        Prog__Layout(state, split->first, a, area, scale, m);
        // Keys here are the window's, not the last built panel's.
        UI->keySeed = 0;
        UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Divider/%u", node));
        UI_SetNextPrefWidth(UI_Pixels(across ? line : rect.x1 - rect.x0));
        UI_SetNextPrefHeight(UI_Pixels(across ? rect.y1 - rect.y0 : line));
        UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
        UI_Block *divider = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_LeftMouseClickable);
        if (UI->hotKey == key || UI->activeKey == key) state->cursor = across ? PlatformCursor_ResizeHorizontal : PlatformCursor_ResizeVertical;
        // Through the title bar it gives way to the bar, under the window's top border, so that the strips on either side meet.
        if (across && rect.y0 <= area.y0) UI_Parent(divider) {
            UI_SetNextFloatingOffset((UI_Vec2){ 0, line });
            UI_SetNextPrefWidth(UI_Pixels(line));
            UI_SetNextPrefHeight(UI_Pixels(Prog__TitleStripHeight(m) - line));
            UI_SetNextColors((UI_Colors){ .background = Prog__CaptionColor(state) });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        }
        UI_Interaction drag = UI_InteractionFromBlock(divider);
        if (drag.active && drag.isDragging && length > line) {
            f32 at = (across ? UI->mouse.x - rect.x0 : UI->mouse.y - rect.y0) / (length - line);
            Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_ResizeSplit, node, 0, 0);
            command->target = (u64)(UI__Clamp(at, 0, 1) * 1e6f);
            command->panel = 0;
        }
        Prog__Layout(state, split->second, b, area, scale, m);
    }
}

// The sidebar with, above it in the title bar, its button at its right edge over the strip line.
function void Prog__SidebarColumn(Prog_State *state, f32 width, f32 height, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_Block *column = UI_AddBlock(0);
    UI_Parent(column) {
        // Where the button sat when the tabs began right after the sidebar.
        f32 button = floorf(m->bar * 0.75f);
        // The window's top border over the title bar, then the bar as tall as the strips beside it.
        Prog__Box(UI_Pixels(width), UI_Pixels(m->separator), UI_Layout_Horizontal, theme->tabBorder, 0);
        f32 strip = Prog__TitleStripHeight(m);
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, MaxF32(0, m->inset - floorf(6.0f * scale + 0.5f)), 0 });
        UI_Block *bar = Prog__Box(UI_Pixels(width), UI_Pixels(strip), UI_Layout_Horizontal, Prog__CaptionColor(state), 0);
        bar->props.childrenAlignment = (UI_Vec2){ 0, 0.5f };
        UI_Parent(bar) {
            Prog__Caption(state, S("SidebarCaption"), width, strip, 0);
            Prog__StripLine(0, width, strip, m);
            // In a maximized window the button reaches the screen's corner; it sits a pixel below the middle, or then a pixel above it.
            b32 maximized = PlatformWindowIsMaximized();
            f32 nudge = floorf(1.6f * scale + 0.5f);
            Prog__Spacer(UI_Pixels(maximized ? 0 : floorf(2.4f * scale + 0.5f)), UI_Pixels(strip));
            UI_SetNextPrefWidth(UI_Pixels(floorf(36.0f * scale + 0.5f)));
            UI_SetNextPrefHeight(UI_Pixels(strip));
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ 0, maximized ? 0 : nudge, 0, maximized ? nudge : 0 });
            UI_Parent(UI_AddBlock(0)) Prog__MenuButton(state, S("MainMenu"), Icon_Menu, Prog_CommandKind_MainMenu, true, maximized ? strip - m->separator : 0, m);
            Prog__Spacer(UI_Flex(), UI_Pixels(strip));
            Prog__SidebarToggle(state, button, m);
        }
        UI_Block *body = Prog__Box(UI_Pixels(width), UI_Flex(), UI_Layout_Horizontal, theme->background, 0);
        UI_Parent(body) {
            // The list, over the room of the progress cards at its bottom.
            UI_SetNextPrefWidth(UI_Pixels(m->sidebar));
            UI_SetNextPrefHeight(UI_Flex());
            UI_SetNextChildrenLayout(UI_Layout_Vertical);
            UI_Block *list = UI_AddBlock(0);
            UI_Parent(list) {
                Prog__Sidebar(state, m);
                if (state->sidebarCards > 0) Prog__Box(UI_Pixels(m->sidebar), UI_Pixels(state->sidebarCards), UI_Layout_Horizontal, theme->panel, 0);
            }
            Prog__Box(UI_Pixels(m->separator), UI_Flex(), UI_Layout_Horizontal, theme->tabBorder, 0);
        }
    }
}

// The sidebar's edge line, from `y0` down, ends at `x`. It drags by a grip 2 px wider than the line on each side, built over the panels so
// that its right side takes the pointer from them, and lights a round-ended band a pixel past each side of the line: dimly under the pointer, fully when pressed.
// The line keeps where it was taken under the pointer.
function void Prog__SidebarEdge(Prog_State *state, f32 left, f32 x, f32 y0, f32 y1, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, S("SidebarEdge"));
    b32 hot = UI->hotKey == key, active = UI->activeKey == key;
    if (hot || active) state->cursor = PlatformCursor_ResizeHorizontal;
    f32 reach = floorf(1.6f * scale + 0.5f), glow = floorf(0.8f * scale + 0.5f), line = x - m->separator;
    if (hot || active) {
        // The dim light is opaque, the selection at 35% over the list's background, so that the line does not show through it.
        R_Color color = theme->selection, under = theme->background;
        if (!active) color = (R_Color){ under.r + (color.r - under.r) * 0.35f, under.g + (color.g - under.g) * 0.35f, under.b + (color.b - under.b) * 0.35f, 1 };
        UI_SetNextFloatingOffset((UI_Vec2){ line - glow - UI->root->rect.x0, y0 - UI->root->rect.y0 });
        UI_SetNextPrefWidth(UI_Pixels(m->separator + 2 * glow));
        UI_SetNextPrefHeight(UI_Pixels(y1 - y0));
        UI_SetNextCornerRadius(m->separator * 0.5f + glow);
        UI_SetNextColors((UI_Colors){ .background = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
    UI_SetNextFloatingOffset((UI_Vec2){ line - reach - UI->root->rect.x0, y0 - UI->root->rect.y0 });
    UI_SetNextPrefWidth(UI_Pixels(m->separator + 2 * reach));
    UI_SetNextPrefHeight(UI_Pixels(y1 - y0));
    UI_Interaction drag = UI_InteractionFromBlock(UI_AddBlockFromKey(key, UI_Flag_Floating | UI_Flag_LeftMouseClickable));
    if (drag.pressed) state->sidebarGrab = UI->mouse.x - line;
    if (drag.active && drag.isDragging) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_ResizeSidebar, 0, 0, 0);
        command->target = (u64)(MaxF32(UI->mouse.x - state->sidebarGrab - left, 0) / scale * 1000.0f);
    }
}

// The panel that has the keyboard when `id` does: its owner for an inspector's.
function u64 Prog__FocusOwner(const Prog_State *state, u64 id)
{
    u32 at = Prog_PanelIndex(state, id);
    return at != UINT32_MAX && state->panels[at].inspectorOf ? state->panels[at].inspectorOf : id;
}

// The window's outline, where no system border draws it, over everything as Windows' border is,
// in the color of the top border the title bar draws, and round where the compositor rounds the window.
function void Prog__WindowBorder(const Prog_Metrics *m)
{
    if (!PlatformWindowNeedsBorder()) return;
    R_Rect view = UI->root->rect;
    UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_SetNextCornerRadius(PlatformWindowGetInfo().cornerRadius);
    UI_SetNextBorderThickness(m->separator);
    UI_SetNextColors((UI_Colors){ .border = Prog_CurrentTheme()->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder | UI_Flag_KeepCorners);
}

// The window's buttons at the top right of the Setup page, as the title strip has them.
function void Prog__SetupWindowButtons(Prog_State *state, f32 scale, const Prog_Metrics *m)
{
    if (!PlatformWindowHasButtons()) return;
    R_Rect view = UI->root->rect;
    f32 controlWidth = floorf(40.8f * scale + 0.5f), height = Prog__TitleStripHeight(m) - m->separator, wash = floorf(23.2f * scale + 0.5f);
    f32 washTop = floorf((height - wash) * 0.5f + 0.5f);
    wash = height - washTop * 2;
    UI_SetNextFloatingOffset((UI_Vec2){ view.x1 - view.x0 - controlWidth * 3, 0 });
    UI_SetNextPrefWidth(UI_Pixels(controlWidth * 3));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *buttons = UI_AddBlock(UI_Flag_Floating);
    UI_Parent(buttons) {
        Prog_WindowButton maximize = PlatformWindowIsMaximized() ? Prog_WindowButton_Restore : Prog_WindowButton_Maximize;
        Prog__WindowButton(state, S("WindowMinimize"), Prog_WindowButton_Minimize, Prog_CommandKind_WindowMinimize, controlWidth, height, washTop, wash, scale, m);
        Prog__WindowButton(state, S("WindowMaximize"), maximize, Prog_CommandKind_WindowToggleMaximize, controlWidth, height, washTop, wash, scale, m);
        Prog__WindowButton(state, S("WindowClose"), Prog_WindowButton_Close, Prog_CommandKind_WindowClose, controlWidth, height, washTop, wash, scale, m);
    }
}

// A line of `text` in `font` floating at `x`, with its middle at `y`, in the Setup page.
function UI_Block *Prog__SetupText(String text, const R_Font *font, R_Color color, f32 x, f32 y)
{
    f32 height = font->lineHeight;
    UI_SetNextFloatingOffset((UI_Vec2){ floorf(x + 0.5f), floorf(y - height * 0.5f + 0.5f) });
    UI_SetNextPrefWidth(UI_Pixels(ceilf(R_TextWidth(font, text))));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFont(font);
    UI_SetNextColors((UI_Colors){ .text = color });
    UI_SetNextText(text);
    return UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
}

// One of the Setup page's buttons: its word in the large text, centered at `y`, lighter under the pointer and dimmer while it would do nothing.
function void Prog__SetupButton(Prog_State *state, String identity, String word, String tip, Prog_CommandKind command, b32 enabled, f32 middle, f32 y,
                                const R_Font *font, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, identity);
    f32 width = ceilf(R_TextWidth(font, word)) + 2 * m->gap, height = font->lineHeight + m->gap;
    R_Color dim = theme->textDim, under = theme->background;
    R_Color color = !enabled ? (R_Color){ under.r + (dim.r - under.r) * 0.8f, under.g + (dim.g - under.g) * 0.8f, under.b + (dim.b - under.b) * 0.8f, 1 }
                  : UI->hotKey == key ? theme->text : dim;
    UI_SetNextFloatingOffset((UI_Vec2){ floorf(middle - width * 0.5f + 0.5f), floorf(y - height * 0.5f + 0.5f) });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextFont(font);
    UI_SetNextColors((UI_Colors){ .text = color });
    UI_SetNextText(word);
    UI_Interaction interaction = UI_InteractionFromBlock(UI_AddBlockFromKey(key, UI_Flag_Floating | UI_Flag_DrawText | (enabled ? UI_Flag_LeftMouseClickable : 0)));
    if (enabled && interaction.clicked) Prog_QueueCommand(state, command, 0, 0, interaction.clickOrder);
    if (enabled && tip.length) Prog__PopupTooltip(state, tip);
}

// The Setup page's lines across it, fading out at their ends.
function void Prog__SetupLine(f32 middle, f32 y, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 half = floorf(356.0f * m->scale + 0.5f), thick = MaxF32(1, floorf(1.6f * m->scale + 0.5f));
    // ponytail: the fade is a few steps of the line's color, as the renderer has no gradients.
    for (u32 step = 0; step < 4; ++step) {
        f32 inset = half * (f32)step * 0.06f;
        R_Color color = theme->tabBorder;
        color.a *= (f32)(step + 1) / 4.0f;
        UI_SetNextFloatingOffset((UI_Vec2){ floorf(middle - half + inset + 0.5f), floorf(y + 0.5f) });
        UI_SetNextPrefWidth(UI_Pixels(floorf(2 * (half - inset) + 0.5f)));
        UI_SetNextPrefHeight(UI_Pixels(thick));
        UI_SetNextColors((UI_Colors){ .background = color });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
}

// Filey Setup, in place of the panels: the logo over the version, a line, where the install goes and its switches, then the buttons that
// install it or change it, uninstall it and go back. A press on the page's empty space moves the window.
function void Prog__SetupPage(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Arena *frame = &state->frameArena;
    R_Rect view = UI->root->rect;
    f32 s = m->scale, middle = floorf((view.x1 - view.x0) * 0.5f);
    const R_Font *large = state->setupFonts[0] ? state->setupFonts[0] : m->font, *medium = state->setupFonts[1] ? state->setupFonts[1] : m->font;
    const R_Font *fine = state->setupFonts[2] ? state->setupFonts[2] : m->font;
    const P_Install *setup = &state->setup;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *page = UI_AddBlockFromKey(UI_KeyFromStr(0, S("SetupPage")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_LeftMouseClickable);
    UI_Interaction drag = UI_InteractionFromBlock(page);
    if (drag.pressed) Prog_QueueCommand(state, Prog_CommandKind_WindowMove, 0, 0, drag.pressOrder);
    UI_Parent(page) {
        f32 logo = floorf(128.0f * s + 0.5f);
        if (state->setupLogo.u64[0]) {
            UI_SetNextFloatingOffset((UI_Vec2){ middle - floorf(logo * 0.5f), floorf(51.6f * s + 0.5f) });
            UI_SetNextPrefWidth(UI_Pixels(logo));
            UI_SetNextPrefHeight(UI_Pixels(logo));
            UI_SetNextImage(state->setupLogo, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ logo, logo }, (R_Color){ 1, 1, 1, 1 });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawImage);
        }
        String name = NoTr("Filey");
        Prog__SetupText(name, large, theme->icon, middle - floorf(R_TextWidth(large, name) * 0.5f), floorf(217.6f * s + 0.5f));
        // What runs and what is installed: the version in a brighter color.
        String before, version, after;
        if (setup->running) {
            before = StrFormat(frame, "%S ", Tr("Installed version")), version = S(FILEY_VERSION), after = S(".");
        } else if (!setup->installed) {
            before = StrFormat(frame, "%S ", Tr("Standalone version")), version = S(FILEY_VERSION), after = S(".");
        } else if (setup->same) {
            before = Tr("The same version is already installed at:"), version = after = (String){0};
        } else {
            before = StrFormat(frame, "%S (", Tr("A different version")), version = setup->version.length ? setup->version : Tr("unknown"), after = StrFormat(frame, ") %S", Tr("is installed."));
        }
        f32 lineWidth = R_TextWidth(medium, before) + R_TextWidth(medium, version) + R_TextWidth(medium, after), x = middle - floorf(lineWidth * 0.5f), y = floorf(282.8f * s + 0.5f);
        Prog__SetupText(before, medium, theme->textDim, x, y);
        if (version.length) Prog__SetupText(version, medium, theme->icon, x + R_TextWidth(medium, before), y);
        if (after.length) Prog__SetupText(after, medium, theme->textDim, x + R_TextWidth(medium, before) + R_TextWidth(medium, version), y);
        Prog__SetupLine(middle, floorf(318.4f * s + 0.5f), m);
        // Where it goes, which is always the same folder.
        f32 boxWidth = floorf(504.0f * s + 0.5f), boxLeft = middle - floorf(boxWidth * 0.5f), boxTop = floorf(393.6f * s + 0.5f), boxHeight = floorf(44.8f * s + 0.5f);
        Prog__SetupText(setup->installed ? Tr("Installed Path:") : Tr("Install Path:"), large, theme->textDim, boxLeft, floorf(360.8f * s + 0.5f));
        UI_SetNextFloatingOffset((UI_Vec2){ boxLeft, boxTop });
        UI_SetNextPrefWidth(UI_Pixels(boxWidth));
        UI_SetNextPrefHeight(UI_Pixels(boxHeight));
        UI_SetNextCornerRadius(floorf(2.4f * s + 0.5f));
        UI_SetNextBorderThickness(MaxF32(1, floorf(0.8f * s + 0.5f)));
        UI_SetNextColors((UI_Colors){ .background = theme->inner, .border = theme->tabBorder });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
        String folder = Prog__Ellipsize(frame, fine, P_NativePath(frame, setup->folder), boxWidth - floorf(17.6f * s + 0.5f));
        Prog__SetupText(folder, fine, theme->textDim, boxLeft + floorf(8.8f * s + 0.5f), boxTop + floorf(boxHeight * 0.5f));
        // The switches, each after its name, in two columns of two rows.
        local_persist const String names[4] = { TrInit("Desktop shortcut:"), TrInit("Add to PATH:"), TrInit("Set as default:"), TrInit("Context menu:") };
        local_persist const String tips[4] = { TrInit("Creates a shortcut on the desktop for quick access to Filey."),
                                                TrInit("Allows Filey to be launched from the command line using Filey.exe."),
                                                TrInit("Opens folders with Filey and assigns it to the Win+E shortcut."),
                                                TrInit("Adds Filey to Explorer's right-click menus: Show in Filey on files, Open in Filey on folders and Open Filey here inside one.") };
        b32 values[4] = { state->setupDesktop, state->setupOnPath, state->setupDefault, state->setupMenu };
        u32 columns[4] = { 0, 1, 1, 0 }, rows[4] = { 0, 1, 0, 1 };
        f32 wide = floorf(45.6f * s + 0.5f), tall = floorf(28.8f * s + 0.5f);
        f32 labels[2] = { middle - floorf(225.6f * s + 0.5f), middle + floorf(18.4f * s + 0.5f) }, switches[2] = { middle - floorf(64.0f * s + 0.5f), middle + floorf(142.4f * s + 0.5f) };
        for (u32 i = 0; i < CArrayCount(values); ++i) {
            f32 row = floorf((484.4f + 43.2f * (f32)rows[i]) * s + 0.5f);
            Prog__SetupText(Prog_Tr(names[i]), medium, theme->textDim, labels[columns[i]], row + floorf(1.6f * s + 0.5f));
            UI_SetNextFloatingOffset((UI_Vec2){ switches[columns[i]], row - floorf(tall * 0.5f) });
            UI_SetNextPrefWidth(UI_Pixels(wide));
            UI_SetNextPrefHeight(UI_Pixels(tall));
            UI_Block *holder = UI_AddBlock(UI_Flag_Floating);
            UI_Parent(holder) {
                UI_Interaction flip = Prog__Switch(UI_KeyFromStr(0, StrFormat(frame, "SetupSwitch/%u", i)), values[i], wide, tall, floorf(2.4f * s + 0.5f), m);
                if (flip.clicked) Prog_QueueCommand(state, Prog_CommandKind_SetupToggle, i, 0, flip.clickOrder);
                Prog__PopupTooltip(state, tips[i]);
            }
        }
        if (!setup->installed) {
            String note = Tr("All options can be enabled or disabled after install.");
            Prog__SetupText(note, fine, theme->textDim, middle - floorf(R_TextWidth(fine, note) * 0.5f), floorf(552.0f * s + 0.5f));
        }
        // Install, replace or change; changing does something only once a switch differs from the install.
        b32 changed = state->setupDesktop != setup->desktopShortcut || state->setupOnPath != setup->onPath || state->setupDefault != setup->setDefault ||
                      state->setupMenu != setup->contextMenu;
        String word = !setup->installed ? Tr("Install") : setup->same ? Tr("Modify") : Tr("Install & Replace");
        String tip = !setup->installed ? StrFormat(frame, TrF("This will install version %s on your PC."), FILEY_VERSION)
                   : setup->same ? Tr("This will apply modifications to the existing installation.")
                                 : StrFormat(frame, TrF("This will overwrite the existing version %S with version %s."), setup->version, FILEY_VERSION);
        Prog__SetupButton(state, S("SetupInstall"), word, tip, Prog_CommandKind_SetupInstall, !setup->installed || !setup->same || changed, middle, floorf(608.0f * s + 0.5f), large, m);
        Prog__SetupLine(middle, floorf(646.4f * s + 0.5f), m);
        if (setup->installed) {
            Prog__SetupButton(state, S("SetupUninstall"), Tr("Uninstall"), Tr("This will remove the current version from your PC, including its shortcuts, right-click entries, default program registration and PATH changes."),
                              Prog_CommandKind_SetupUninstall, true, middle, floorf(688.8f * s + 0.5f), large, m);
        }
        Prog__SetupButton(state, S("SetupBack"), Tr("Back"), (String){0}, Prog_CommandKind_SetupBack, true, middle, floorf((setup->installed ? 744.0f : 688.8f) * s + 0.5f), large, m);
    }
}

function void Prog_BuildUI(Prog_State *state, const R_Font *font, const R_Font *icons, f32 scale)
{
    state->iconsPending = 0;
    state->hotToolbarCommand = Prog_CommandKind_None;
    state->permissionsPath = (String){0};
    state->tabGhost = NULL;
    state->foreignOver = false;
    state->hasHoverRow = false;
    state->hoverSpacePanel = 0;
    state->hoverMenuPath = (String){0};
    state->tooltipKey = 0;
    state->tooltipAbove = false;
    state->tooltipPath = (String){0};
    state->cursor = PlatformCursor_Arrow;
    for (u32 i = 0; i < state->panelCount; ++i) state->panels[i].inspectorRect = (R_Rect){0};
    state->inspectorTextRect = (R_Rect){0};
    state->bookmarkDropShown = 0;
    state->userGroupDropShown = state->userGroupDropShownIn = 0;
    ArenaClear(&state->dropPlaceArena);
    state->dropPlace = (String){0};
    state->pointerPanel = 0;
    Prog__EnsureTab(state);
    // A dragged tab is let go where the last build found it would go; Ctrl, held now, copies it.
    if (state->tabDrag && UI->activeKey != state->tabDragKey) {
        state->tabDrag = false;
        state->tabDropped = state->tabDrop;
        state->tabDropped.copy = (UI->mods & KeyMod_Control) != 0;
        if (state->tabDropped.kind != Prog_TabDrop_None) Prog_QueueCommand(state, Prog_CommandKind_TabDrop, 0, 0, 0)->panel = 0;
    }
    if (state->tabDragCancelled && UI->activeKey != state->tabDragKey) state->tabDragCancelled = false;
    state->tabDrop.kind = Prog_TabDrop_None;
    Prog_Metrics metrics = Prog__Metrics(font, icons, scale);
    if (state->sidebarWidth > 0) metrics.sidebar = floorf(state->sidebarWidth * scale + 0.5f);
    // Spacing moves the rows apart or together from the default 4, two pixels a step at 125%.
    metrics.row = MaxF32(metrics.line + 2, metrics.row + (f32)state->spacingShift * floorf(1.6f * scale + 0.5f));
    // The border size is in pixels at any scale: the outlines' as it is, half pixels drawn soft, and the lines between areas, which the
    // layout sizes boxes by, in the nearest whole pixels, so that they stay crisp.
    f32 border = 1 + 0.5f * (f32)state->borderSize;
    metrics.stroke = border;
    metrics.separator = floorf(border + 0.5f);
    UI->squareCorners = state->squareCorners;
    UI->reverseScroll = state->reverseScroll;
    metrics.detail = state->smallFont ? state->smallFont : font;
    metrics.iconsSmall = state->smallIcons ? state->smallIcons : icons;
    metrics.iconsMedium = state->mediumIcons ? state->mediumIcons : icons;
    i32 iconSize = (i32)floorf(16.0f * scale + 0.5f);
    // Icons from another theme or the system's updated ones are loaded again in place; a new size's start over.
    b32 iconsChanged = P_IconThemeChanged();
    if (iconSize != state->iconSize) Prog__ResetIcons(state, iconSize);
    else if (iconsChanged) Prog_ReloadIcons(state);
    state->iconScale = scale;
    const Prog_Metrics *m = &metrics;

    UI->root->props.childrenPadding = (UI_Padding){0};
    UI->root->props.childrenLayout = UI_Layout_Horizontal;
    R_Rect view = UI->root->rect;
    if (state->setupOpen) {
        Prog__SetupPage(state, m);
        Prog__SetupWindowButtons(state, scale, m);
        Prog__Tooltip(state, m);
        Prog__WindowBorder(m);
        state->hotkeyChipShown = false;
        return;
    }
    // The sidebar slides in and out from the left edge, its contents moving with it rather than squeezing.
    f32 full = m->sidebar + m->separator;
    f32 shown = UI_Slide(UI_KeyFromStr(0, S("SidebarSlide")), state->sidebarHidden || state->uiHidden ? 0.0f : 1.0f, Prog__SlideCap(m) / MaxF32(full, 1));
    f32 sidebar = floorf(full * shown + 0.5f);
    if (sidebar > 0) {
        UI_SetNextPrefWidth(UI_Pixels(sidebar));
        UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenPadding((UI_Padding){ sidebar - full, 0, 0, 0 });
        UI_Block *slide = UI_AddBlock(UI_Flag_Clip);
        UI_Parent(slide) Prog__SidebarColumn(state, full, view.y1 - view.y0, scale, m);
    }
    R_Rect area = { view.x0 + sidebar, view.y0, view.x1, view.y1 };
    // A zoomed panel grows from its place to the whole area, easing, and shrinks back when unzoomed; the others are not built while it
    // covers them. Moving the keyboard to another panel unzooms it.
    u64 owner = state->panels[state->activePanel].inspectorOf ? state->panels[state->activePanel].inspectorOf : state->panels[state->activePanel].id;
    if (state->zoomedPanel && (owner != state->zoomedPanel || Prog_PanelIndex(state, state->zoomedPanel) == UINT32_MAX)) state->zoomedPanel = 0;
    if (state->zoomedPanel) state->zoomShownPanel = state->zoomedPanel;
    f32 zoomed = UI_Animate(UI_KeyFromStr(0, S("PanelZoom")), state->zoomedPanel ? 1.0f : 0.0f, 0);
    u32 zoomIndex = state->zoomShownPanel ? Prog_PanelIndex(state, state->zoomShownPanel) : UINT32_MAX;
    if (zoomIndex == UINT32_MAX || (!state->zoomedPanel && zoomed < 0.001f)) {
        state->zoomShownPanel = 0;
        zoomIndex = UINT32_MAX;
    }
    if (zoomIndex == UINT32_MAX || zoomed < 0.999f) Prog__Layout(state, 0, area, area, scale, m);
    if (zoomIndex != UINT32_MAX) {
        R_Rect from = state->zoomFrom, rect = {
            floorf(from.x0 + (area.x0 - from.x0) * zoomed + 0.5f), floorf(from.y0 + (area.y0 - from.y0) * zoomed + 0.5f),
            floorf(from.x1 + (area.x1 - from.x1) * zoomed + 0.5f), floorf(from.y1 + (area.y1 - from.y1) * zoomed + 0.5f),
        };
        UI->keySeed = 0;
        UI_SetNextFloatingOffset((UI_Vec2){ rect.x0 - view.x0, rect.y0 - view.y0 });
        UI_SetNextPrefWidth(UI_Pixels(rect.x1 - rect.x0));
        UI_SetNextPrefHeight(UI_Pixels(rect.y1 - rect.y0));
        UI_Block *holder = UI_AddBlock(UI_Flag_Floating);
        UI_Parent(holder) Prog__PanelUI(state, zoomIndex, rect, area, scale, m);
    }
    UI->keySeed = 0;
    if (sidebar > 0) Prog__SidebarEdge(state, view.x0 + sidebar - full, view.x0 + sidebar, view.y0 + m->separator + Prog__TitleStripHeight(m), view.y1, scale, m);
    // A name being edited in the sidebar whose row is out of sight, folded away or with the sidebar gone, is dropped; one still sliding in is not.
    if (state->layoutSaving && state->nameFor && !state->nameShown && (sidebar >= full || state->sidebarHidden || state->uiHidden)) {
        Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 2, 0, 0);
    }
    state->nameShown = false;
    Prog__UsePanel(state, state->activePanel);
    // Under a moving pointer, the panel it is over takes the keyboard, as a click there would, and one the keyboard switched to keeps it
    // until the pointer moves. A panel's inspector counts as the panel; nothing switches while a press, a text box or a popup has the input.
    b32 moved = UI->mouse.x != state->focusMouse.x || UI->mouse.y != state->focusMouse.y;
    state->focusMouse = UI->mouse;
    if (!state->clickToFocus && moved && UI->mouseKnown && state->pointerPanel && !UI->activeKey && !state->tabDrag && !state->dragging && !state->renaming && !state->filtering &&
        !state->inspectorFiltering && !state->menuOpen && !state->paletteOpen && !state->typesOpen && !state->zoomShown && !state->confirmingDelete && !state->hotkeyKind) {
        u64 under = Prog__FocusOwner(state, state->pointerPanel);
        if (under != Prog__FocusOwner(state, state->panels[state->activePanel].id)) Prog_QueueCommand(state, Prog_CommandKind_PanelHover, 0, 0, 0)->panel = under;
    }
    // The dragged tab's path and traits, for the window it may be dropped on to open.
    b32 outside = state->tabDrag && !UI__Contains(view, UI->mouse);
    String dragged = {0};
    u32 draggedFrom = outside ? Prog_PanelIndex(state, state->tabDrop.fromPanel) : UINT32_MAX;
    if (draggedFrom != UINT32_MAX) {
        Prog__UsePanel(state, draggedFrom);
        if (state->tabDrop.fromSlot < state->tabCount) {
            dragged = StrFormat(&state->frameArena, "%S\t%S", Prog_TabPath(state, state->tabDrop.fromSlot), Prog__TabTraits(state, state->tabDrop.fromSlot));
        }
        Prog__UsePanel(state, state->activePanel);
    }
    PlatformDragHover(outside, dragged);
    if (state->tabDrag) {
        if (!UI__Contains(view, UI->mouse)) state->tabDrop.kind = Prog_TabDrop_Window;
        state->tabDropOther = state->tabDrop.kind == Prog_TabDrop_Window && PlatformOtherWindowAtPointer(S(""));
        Prog__TabGhost(state, scale, m);
    }
    Prog__Notices(state, Prog__Transfers(state, sidebar, m), m);
    Prog__ContextMenu(state, m);
    Prog__MenuEditor(state, m);
    Prog__TypesPopup(state, m);
    Prog__FilesPopup(state, m);
    Prog__ZoomPopup(state, m);
    Prog__Palette(state, m);
    Prog__SchemesDropdown(state, m);
    Prog__MutedDropdown(state, m);
    Prog__BranchesDropdown(state, m);
    Prog__HotkeyQuestion(state, m);
    Prog__Tooltip(state, m);
    Prog__DeleteConfirmation(state, m);
    Prog__ColorPicker(state, m);
    Prog__LayoutSaveBox(state, m);
    Prog__PasswordBox(state, m);
    Prog__LayoutQuestion(state, m);
    Prog__WindowBorder(m);
    // A chip being rebound that went out of sight, with its menu or its palette row, stops.
    if (state->hotkeyKind && !state->hotkeyChipShown) Prog_QueueCommand(state, Prog_CommandKind_HotkeyCancel, 0, 0, 0);
    state->hotkeyChipShown = false;
    if (!state->fontPreviewsShown) Prog__ReleaseFontPreviews(state);
    state->fontPreviewsShown = false;
}
