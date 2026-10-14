// Browser UI.
// Sizes derive from the font's line height so the layout keeps its proportions at any font size and display scale; the ratios come from the reference screenshots.

#include <math.h>

#include "Meta/Icons.meta.h"

#define Prog_Gray(v)     { (v) / 255.0f, (v) / 255.0f, (v) / 255.0f, 1 }
#define Prog_Rgb(r, g, b) { (r) / 255.0f, (g) / 255.0f, (b) / 255.0f, 1 }

typedef struct Prog_Theme {
    R_Color window;        // cleared behind everything
    R_Color background;    // toolbar and list base
    R_Color caption;       // the tab strip and the bar above the sidebar
    R_Color menu;          // context menus, which cover other content
    R_Color panel;         // sidebar and bottom pills
    R_Color alternateRow;
    R_Color separator;
    R_Color hover;
    R_Color selection;
    R_Color focus;         // outline of the focused row
    R_Color tabBorder;     // outline of the shown tab, which the strip's bottom line and the sidebar's edge continue
    R_Color closeHover;    // the window's close button under the pointer
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
} Prog_Theme;

// The neutral "custom" scheme from the reference screenshots.
global const Prog_Theme Prog_ThemeCustom = {
    .window       = Prog_Gray(25),
    .background   = Prog_Gray(25),
    .caption      = Prog_Gray(31),
    .menu         = Prog_Gray(31),
    .panel        = Prog_Gray(31),
    .alternateRow = Prog_Gray(28),
    .separator    = Prog_Gray(28),
    .hover        = Prog_Gray(80),
    .selection    = Prog_Gray(80),
    .focus        = Prog_Gray(80),
    .tabBorder    = Prog_Gray(56),
    .closeHover   = Prog_Rgb(196, 43, 28),
    .text         = Prog_Rgb(221, 222, 224),
    .textDim      = Prog_Rgb(107, 112, 120),
    .textHeading  = Prog_Gray(162),
    .textMatch    = Prog_Rgb(0, 170, 255),
    .marquee      = Prog_Rgb(0, 173, 238),
    .accent       = Prog_Rgb(0, 121, 166),
    .track        = Prog_Rgb(56, 60, 61),
    .outline      = Prog_Gray(31),
    .danger       = Prog_Rgb(255, 99, 88),
    .icon         = Prog_Gray(154),
    .iconDisabled = Prog_Gray(70),
};

#define Prog_White(a) { 1, 1, 1, (a) }

// The custom scheme over a see-through window: the window is one translucent layer and the surfaces on it are faint white tints, so they do not stack into an opaque color.
global const Prog_Theme Prog_ThemeGlass = {
    .window       = { 25 / 255.0f, 25 / 255.0f, 25 / 255.0f, 0.55f },
    .background   = { 0, 0, 0, 0 },
    .caption      = Prog_White(0.025f),
    .menu         = { 31 / 255.0f, 31 / 255.0f, 31 / 255.0f, 0.97f },
    .panel        = Prog_White(0.025f),
    .alternateRow = Prog_White(0.012f),
    .separator    = Prog_White(0.03f),
    .hover        = Prog_White(0.2f),
    .selection    = Prog_White(0.2f),
    .focus        = Prog_White(0.22f),
    .tabBorder    = Prog_White(0.04f),
    .closeHover   = Prog_Rgb(196, 43, 28),
    .text         = Prog_Rgb(221, 222, 224),
    .textDim      = Prog_Rgb(150, 155, 162),
    .textHeading  = Prog_Gray(170),
    .textMatch    = Prog_Rgb(0, 170, 255),
    .marquee      = Prog_Rgb(0, 173, 238),
    .accent       = Prog_Rgb(0, 121, 166),
    .track        = Prog_Rgb(56, 60, 61),
    .outline      = Prog_White(0.08f),
    .danger       = Prog_Rgb(255, 99, 88),
    .icon         = Prog_Gray(160),
    .iconDisabled = Prog_Gray(90),
};

// A distinct loud color for every role, so that screenshots show which surface draws where.
global const Prog_Theme Prog_ThemeDebug = {
    .window       = Prog_Rgb(60, 0, 60),
    .background   = Prog_Rgb(10, 25, 80),
    .caption      = Prog_Rgb(80, 40, 0),
    .menu         = Prog_Rgb(0, 70, 70),
    .panel        = Prog_Rgb(10, 70, 20),
    .alternateRow = Prog_Rgb(90, 80, 0),
    .separator    = Prog_Rgb(255, 128, 0),
    .hover        = Prog_Rgb(200, 0, 200),
    .selection    = Prog_Rgb(150, 20, 20),
    .focus        = Prog_Rgb(255, 255, 0),
    .tabBorder    = Prog_Rgb(0, 255, 255),
    .closeHover   = Prog_Rgb(255, 0, 0),
    .text         = Prog_Rgb(255, 255, 255),
    .textDim      = Prog_Rgb(255, 170, 200),
    .textHeading  = Prog_Rgb(150, 255, 150),
    .textMatch    = Prog_Rgb(0, 170, 255),
    .marquee      = Prog_Rgb(255, 0, 128),
    .accent       = Prog_Rgb(0, 200, 0),
    .track        = Prog_Rgb(90, 0, 90),
    .outline      = Prog_Rgb(200, 120, 255),
    .danger       = Prog_Rgb(255, 99, 88),
    .icon         = Prog_Rgb(255, 230, 120),
    .iconDisabled = Prog_Rgb(120, 100, 60),
};

#define Prog_Hex(v) { (((v) >> 16) & 255) / 255.0f, (((v) >> 8) & 255) / 255.0f, ((v) & 255) / 255.0f, 1 }

// The reference's built-in "Fullmoon" scheme, for comparing the two side by side.
global const Prog_Theme Prog_ThemeFullmoon = {
    .window       = Prog_Hex(0x151515),
    .background   = Prog_Hex(0x191B1C),
    .caption      = Prog_Hex(0x272A2B),
    .menu         = Prog_Hex(0x1F2223),
    .panel        = Prog_Hex(0x1F2223),
    .alternateRow = Prog_Hex(0x1D1F21),
    .separator    = Prog_Hex(0x202324),
    .hover        = Prog_Hex(0x595959),
    .selection    = Prog_Hex(0x0079A6),
    .focus        = Prog_Hex(0x0079A6),
    .tabBorder    = Prog_Hex(0x343A3B),
    .closeHover   = Prog_Rgb(196, 43, 28),
    .text         = Prog_Hex(0xDDDEE0),
    .textDim      = Prog_Hex(0x6B7078),
    .textHeading  = Prog_Hex(0x9A9C9D),
    .textMatch    = Prog_Hex(0x00ADEE),
    .marquee      = Prog_Hex(0x00ADEE),
    .accent       = Prog_Hex(0x0079A6),
    .track        = Prog_Hex(0x383C3D),
    .outline      = Prog_Hex(0x464A4D),
    .danger       = Prog_Hex(0xFF6666),
    .icon         = Prog_Hex(0x9A9C9D),
    .iconDisabled = Prog_Hex(0x595959),
};

global const Prog_Theme *Prog_Theme_ = &Prog_ThemeCustom;

function const Prog_Theme *Prog_CurrentTheme(void) { return Prog_Theme_; }

// The custom theme with the settings' colors.
global Prog_Theme Prog_ThemeUser = Prog_ThemeCustom;

// The theme's colors by the names the settings give them.
global const struct { const char *name; u64 offset; } Prog_ThemeColors[] = {
#define Prog__ThemeColor(field) { #field, offsetof(Prog_Theme, field) }
    Prog__ThemeColor(window), Prog__ThemeColor(background), Prog__ThemeColor(caption), Prog__ThemeColor(menu), Prog__ThemeColor(panel), Prog__ThemeColor(alternateRow),
    Prog__ThemeColor(separator), Prog__ThemeColor(hover), Prog__ThemeColor(selection), Prog__ThemeColor(focus), Prog__ThemeColor(tabBorder),
    Prog__ThemeColor(closeHover), Prog__ThemeColor(text), Prog__ThemeColor(textDim), Prog__ThemeColor(textHeading), Prog__ThemeColor(textMatch),
    Prog__ThemeColor(marquee), Prog__ThemeColor(accent), Prog__ThemeColor(track), Prog__ThemeColor(outline), Prog__ThemeColor(danger), Prog__ThemeColor(icon), Prog__ThemeColor(iconDisabled),
#undef Prog__ThemeColor
};
static_assert(sizeof(Prog_ThemeColors) / sizeof(Prog_ThemeColors[0]) * sizeof(R_Color) == sizeof(Prog_Theme));

function void Prog__ApplyTheme(Prog_State *state)
{
    Prog_Theme_ = state->debugTheme ? &Prog_ThemeDebug : state->transparent ? &Prog_ThemeGlass : &Prog_ThemeUser;
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

// Applies settings `text`, lines of "name = value", over the defaults; unknown names and bad values are skipped, and a line starting with '#' is a comment.
//   scale = 1.25                  the UI's size beyond the display's own scale
//   theme = fullmoon              the reference's scheme under the colors below, for side-by-side checks
//   animations = off              menus, scrolling and the caret change at once
//   color.<theme field> = #rrggbb[aa]
//   hotkey.<Command> = Ctrl+K, F6  replaces the command's default hotkeys; "none" leaves it without
function void Prog_ApplySettings(Prog_State *state, String text)
{
    Prog_ThemeUser = Prog_ThemeCustom;
    state->uiScale = 1;
    state->animationsOff = false;
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
        if (StrIsEqual(name, S("scale"))) {
            f32 scale = 0, fraction = 0;
            for (u64 i = 0; i < value.length; ++i) {
                char c = value.value[i];
                if (c == '.' && fraction == 0) fraction = 0.1f;
                else if (c >= '0' && c <= '9' && fraction == 0) scale = scale * 10 + (f32)(c - '0');
                else if (c >= '0' && c <= '9') { scale += (f32)(c - '0') * fraction; fraction *= 0.1f; }
                else { scale = 0; break; }
            }
            if (scale >= 0.5f && scale <= 3) state->uiScale = scale;
        } else if (StrIsEqual(name, S("theme"))) {
            // Colors given before the theme line are replaced with the rest.
            if (Prog__EqualFolded(value, S("fullmoon"))) Prog_ThemeUser = Prog_ThemeFullmoon;
        } else if (StrIsEqual(name, S("animations"))) {
            state->animationsOff = Prog__EqualFolded(value, S("off")) || Prog__EqualFolded(value, S("false")) || StrIsEqual(value, S("0"));
        } else if (name.length > 6 && StrIsEqual(StrFromTo(name, 0, 6), S("color."))) {
            String field = StrFrom(name, 6);
            R_Color color;
            for (u64 i = 0; i < CArrayCount(Prog_ThemeColors); ++i) {
                if (StrIsEqual(field, StrFromCStr(Prog_ThemeColors[i].name)) && Prog__ParseColor(value, &color)) *(R_Color *)((u8 *)&Prog_ThemeUser + Prog_ThemeColors[i].offset) = color;
            }
        } else if (name.length > 7 && StrIsEqual(StrFromTo(name, 0, 7), S("hotkey."))) {
            String command = StrFrom(name, 7);
            for (u32 k = Prog_CommandKind_None + 1; k < Prog_CommandKind_Count; ++k) {
                if (!StrIsEqual(command, Prog_Commands[k].name)) continue;
                state->hotkeyOverridden[k] = true;
                if (Prog__EqualFolded(value, S("none"))) break;
                for (u64 at = 0; at < value.length;) {
                    Index comma = StrIndexChar(StrFrom(value, at), ',', SSF_CountOnFail);
                    Prog_Hotkey hotkey = { .command = (u8)k };
                    if (Prog_ParseHotkey(StrFromTo(value, at, (i64)(at + comma.value)), &hotkey.key, &hotkey.mods) && state->userHotkeyCount < CArrayCount(state->userHotkeys)) {
                        state->userHotkeys[state->userHotkeyCount++] = hotkey;
                    }
                    at += comma.value + 1;
                }
                break;
            }
        }
    }
    ReleaseScratch(scratch);
    Prog__ApplyTheme(state);
}

// Every setting, commented out at its default, for a first edit.
function String Prog_SettingsTemplate(Arena *arena, const Prog_State *state)
{
    StrBuilder text = {0};
    StrBuildStr(arena, &text, S("# Filey settings, applied as soon as this file is saved.\n"
                                "# Lines are \"name = value\"; remove the # in front of a setting to change it.\n\n"
                                "# The size of everything, beyond the display's own scale.\n# scale = 1\n\n"
                                "# \"fullmoon\" starts from the reference's scheme; colors set before this line are replaced.\n# theme = default\n\n"
                                "# Easing for menus and scrolling, and the fading caret.\n# animations = on\n\n"
                                "# Colors of the default theme, as #rrggbb or #rrggbbaa.\n"));
    for (u64 i = 0; i < CArrayCount(Prog_ThemeColors); ++i) {
        R_Color c = *(const R_Color *)((const u8 *)&Prog_ThemeCustom + Prog_ThemeColors[i].offset);
        u32 r = (u32)(c.r * 255 + 0.5f), g = (u32)(c.g * 255 + 0.5f), b = (u32)(c.b * 255 + 0.5f), a = (u32)(c.a * 255 + 0.5f);
        StrBuildStr(arena, &text, a == 255 ? StrFormat(arena, "# color.%s = #%02x%02x%02x\n", Prog_ThemeColors[i].name, r, g, b)
                                           : StrFormat(arena, "# color.%s = #%02x%02x%02x%02x\n", Prog_ThemeColors[i].name, r, g, b, a));
    }
    StrBuildStr(arena, &text, S("\n# Hotkeys, several separated by commas; \"none\" removes a command's.\n"));
    Prog_State defaults = {0};
    (void)state;
    for (u32 k = Prog_CommandKind_None + 1; k < Prog_CommandKind_Count; ++k) {
        if (!Prog_Commands[k].label.length) continue;
        String hotkey = Prog_HotkeyText(arena, &defaults, (Prog_CommandKind)k);
        StrBuildStr(arena, &text, StrFormat(arena, "# hotkey.%S = %S\n", Prog_Commands[k].name, hotkey.length ? hotkey : S("none")));
    }
    return text.buffer;
}

function void Prog_SetTransparent(Prog_State *state, b32 transparent)
{
    state->transparent = transparent;
    Prog__ApplyTheme(state);
}

function void Prog_SetDebugTheme(Prog_State *state, b32 debug)
{
    state->debugTheme = debug;
    Prog__ApplyTheme(state);
}

typedef struct Prog_Metrics {
    f32 line;        // font line height
    f32 row;         // list rows and the column header
    f32 bar;         // tab strip and toolbar
    f32 pill;        // bottom filter and status pills
    f32 separator;
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
} Prog_Metrics;

function Prog_Metrics Prog__Metrics(const R_Font *font, const R_Font *icons, f32 scale)
{
    f32 line = font->lineHeight;
    return (Prog_Metrics){
        .font = font,
        .detail = font,
        .icons = icons,
        .iconsSmall = icons,
        .line = line,
        .row = floorf(line * 1.6f + 0.5f),
        .bar = floorf(line * 2.1f + 0.5f),
        .pill = floorf(line * 2.0f + 0.5f),
        .separator = MaxF32(1.0f, floorf(1.6f * scale + 0.5f)),
        .sidebar = floorf(180.0f * scale + 0.5f),
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
    return MaxF32(1.0f, floorf(m->separator * 0.75f));
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

function void Prog__Icon(String icon, f32 width, f32 height, R_Color color, f32 alignX, const Prog_Metrics *m)
{
    UI_SetNextFont(m->icons);
    Prog__Label(icon, width > 0 ? UI_Pixels(width) : UI_TextContent(), height, 0, color, alignX);
}

// Text button that tints its text when hot and greys out when disabled.
// A NULL font uses the default one.
function UI_Interaction Prog__TextButton(String identity, String text, const R_Font *font, UI_Size width, f32 height, f32 padding,
                                         R_Color color, R_Color hotColor, R_Color disabledColor, b32 enabled, const Prog_Metrics *m)
{
    UI_Key key = UI_KeyFromStr(0, identity);
    b32 hot = enabled && UI->hotKey == key;
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){ padding, 0, padding, 0 });
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = Prog_CurrentTheme()->panel, .text = !enabled ? disabledColor : hot ? hotColor : color });
    UI_SetNextText(text);
    if (font) UI_SetNextFont(font);
    UI_Block *block = UI_AddBlockFromKey(key, UI_Flag_DrawText | (hot ? UI_Flag_DrawBackground : 0) | (enabled ? UI_Flag_LeftMouseClickable : 0));
    UI_Interaction result = UI_InteractionFromBlock(block);
    if (!enabled) result.clicked = false;
    return result;
}

function void Prog__ToolbarButton(Prog_State *state, String identity, String icon, Prog_CommandKind kind, b32 enabled, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Interaction interaction = Prog__TextButton(identity, icon, m->icons, UI_Pixels(m->bar * 0.75f), m->bar * 0.75f, 0,
                                                  theme->icon, theme->text, theme->iconDisabled, enabled, m);
    if (interaction.clicked) Prog_QueueCommand(state, kind, 0, 0, interaction.clickOrder);
}

// Toolbar button that opens one of Filey's menus below it, lit in the selection color while that menu is open.
// A button in the window's right half lines its menu up with its right edge.
function void Prog__MenuButton(Prog_State *state, String identity, String icon, Prog_CommandKind kind, b32 enabled, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 size = m->bar * 0.75f;
    b32 lit = state->menuOpen && state->menuOpener == kind;
    UI_Key key = UI_KeyFromStr(0, identity);
    b32 hot = enabled && UI->hotKey == key;
    UI_SetNextPrefWidth(UI_Pixels(size));
    UI_SetNextPrefHeight(UI_Pixels(size));
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextCornerRadius(m->radius * 2);
    UI_SetNextColors((UI_Colors){ .background = lit ? theme->selection : theme->panel, .text = !enabled ? theme->iconDisabled : hot || lit ? theme->text : theme->icon });
    UI_SetNextText(icon);
    UI_SetNextFont(m->icons);
    UI_Block *block = UI_AddBlockFromKey(key, UI_Flag_DrawText | (hot || lit ? UI_Flag_DrawBackground : 0) | (enabled ? UI_Flag_LeftMouseClickable : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(block);
    if (enabled && interaction.clicked) {
        R_Rect rect = block->rect;
        f32 gap = floorf(4.0f * m->scale + 0.5f);
        Prog_Command *command = Prog_QueueCommand(state, kind, 0, 0, interaction.clickOrder);
        command->at = (UI_Vec2){ rect.x0, rect.y1 + gap };
        if (rect.x0 > (UI->root->rect.x0 + UI->root->rect.x1) * 0.5f) command->atRight = rect.x1;
    }
}

// Icon button inside a bottom pill; `on` shows a toggle that is set.
function void Prog__PillButton(Prog_State *state, String identity, String icon, Prog_CommandKind kind, b32 on, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Interaction interaction = Prog__TextButton(identity, icon, m->icons, UI_Pixels(floorf(m->line * 1.5f)), floorf(m->line * 1.5f), 0,
                                                  on ? theme->text : theme->icon, theme->text, theme->iconDisabled, true, m);
    if (interaction.clicked) Prog_QueueCommand(state, kind, 0, 0, interaction.clickOrder);
}

// The last component of a folder path, or the whole path for a root such as "C:/" or "/".
function String Prog__FolderName(String path)
{
    String trimmed = path;
    while (trimmed.length > 1 && trimmed.value[trimmed.length - 1] == '/' && trimmed.value[trimmed.length - 2] != ':') --trimmed.length;
    Index slash = StrIndexChar(trimmed, '/', SSF_SearchBackwards);
    return slash.exists && slash.value + 1 < trimmed.length ? StrFrom(trimmed, slash.value + 1) : trimmed;
}

typedef enum Prog_WindowButton {
    Prog_WindowButton_Minimize,
    Prog_WindowButton_Maximize,
    Prog_WindowButton_Restore,
    Prog_WindowButton_Close,
} Prog_WindowButton;

// A window button at the strip's right end; the close button turns red under the pointer, as the system's does.
// The glyphs are thin shapes the size of the system's caption glyphs, which the icon font's strokes are too heavy for; only the close cross comes from the font.
function void Prog__WindowButton(Prog_State *state, String identity, Prog_WindowButton kind, Prog_CommandKind command, f32 width, f32 height, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, identity);
    b32 hot = UI->hotKey == key;
    b32 close = kind == Prog_WindowButton_Close;
    R_Color background = close ? theme->closeHover : theme->selection;
    R_Color ink = !hot ? theme->icon : close ? (R_Color){ 1, 1, 1, 1 } : theme->text;
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextColors((UI_Colors){ .background = background, .text = ink });
    if (close) {
        UI_SetNextFont(m->icons);
        UI_SetNextText(Icon_X);
    }
    UI_Block *button = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | (close ? UI_Flag_DrawText : 0) | (hot ? UI_Flag_DrawBackground : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(button);
    if (interaction.clicked) Prog_QueueCommand(state, command, 0, 0, interaction.clickOrder);
    f32 glyph = floorf(10.0f * scale);
    f32 line = MaxF32(1.0f, floorf(1.25f * scale + 0.5f));
    UI_Parent(button) {
        if (kind == Prog_WindowButton_Minimize) {
            Prog__Box(UI_Pixels(glyph), UI_Pixels(line), UI_Layout_Horizontal, ink, 0);
        } else if (kind == Prog_WindowButton_Maximize) {
            UI_SetNextPrefWidth(UI_Pixels(glyph));
            UI_SetNextPrefHeight(UI_Pixels(glyph));
            UI_SetNextColors((UI_Colors){ .border = ink });
            UI_AddBlock(UI_Flag_DrawBorder);
        } else if (kind == Prog_WindowButton_Restore) {
            // A square in front of another one, which shows only above and to the right of it.
            f32 offset = floorf(2.0f * scale + 0.5f);
            f32 size = glyph - offset;
            UI_SetNextPrefWidth(UI_Pixels(glyph));
            UI_SetNextPrefHeight(UI_Pixels(glyph));
            UI_Block *squares = UI_AddBlock(0);
            UI_Parent(squares) {
                UI_SetNextFloatingOffset((UI_Vec2){ offset, 0 });
                UI_SetNextPrefWidth(UI_Pixels(size));
                UI_SetNextPrefHeight(UI_Pixels(size));
                UI_SetNextColors((UI_Colors){ .border = ink });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, offset });
                UI_SetNextPrefWidth(UI_Pixels(size));
                UI_SetNextPrefHeight(UI_Pixels(size));
                UI_SetNextColors((UI_Colors){ .background = hot ? background : theme->panel, .border = ink });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
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

// The strip's line under it, which the shown tab opens into the toolbar; it is as thick as the sidebar's edge it meets.
function void Prog__StripLine(f32 width, f32 y, const Prog_Metrics *m)
{
    UI_SetNextFloatingOffset((UI_Vec2){ 0, y - m->separator });
    UI_SetNextPrefWidth(UI_Pixels(width));
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
    UI_Interaction toggle = Prog__TextButton(S("ToggleQuickAccess"), Icon_PanelLeft, m->icons, UI_Pixels(button), button, 0, theme->icon, theme->text, theme->iconDisabled, true, m);
    if (toggle.clicked) Prog_QueueCommand(state, Prog_CommandKind_ToggleQuickAccess, 0, 0, toggle.clickOrder)->panel = 0;
}

// Where a dragged tab would go in a strip: a tab-shaped slot in the accent color.
function void Prog__TabPlaceholder(f32 width, f32 height, f32 radius, f32 stroke, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_SetNextPrefWidth(UI_Pixels(width + m->separator));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){0});
    UI_Block *slot = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(slot) {
        R_Color fill = theme->accent;
        fill.a = 0.35f;
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height + radius));
        UI_SetNextCornerRadius(radius);
        UI_SetNextBorderThickness(stroke);
        UI_SetNextColors((UI_Colors){ .background = fill, .border = theme->accent });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
    }
}

// A panel's tab strip `width` wide: one tab per slot, the shown one filled like the toolbar it joins, the others parted by faint lines.
// Tabs share the strip's width up to a fixed size, and dragging one moves it between the others.
// In the title bar (`caption`) the strip's empty space moves the window; `lead` puts the sidebar button first while the sidebar is hidden, and `controls` the window buttons last.
function void Prog__TabStrip(Prog_State *state, f32 width, b32 caption, b32 lead, b32 controls, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog__EnsureTab(state);
    f32 top = floorf(m->bar * 0.15f);
    UI_SetNextChildrenPadding((UI_Padding){ 0, top, 0, 0 });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(m->bar));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    // Children start at the top, so that tabs taller than the strip hang below it.
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0 });
    UI_SetNextColors((UI_Colors){ .background = theme->caption });
    // Keyed, so that its last layout tells whether a dragged tab is over it.
    UI_Block *strip = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TabStrip")), UI_Flag_DrawBackground);
    UI_Parent(strip) {
        if (caption) Prog__Caption(state, S("Caption"), width, m->bar, -top);
        f32 stroke = m->separator;
        Prog__StripLine(width, m->bar - top, m);
        f32 button = floorf(m->bar * 0.75f);
        f32 leadWidth = lead ? button + m->inset : floorf(6.0f * scale + 0.5f);
        UI_SetNextPrefWidth(UI_Pixels(leadWidth));
        UI_SetNextPrefHeight(UI_Pixels(m->bar - top));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
        // Centered on the whole strip rather than on the tabs.
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, 0, top });
        UI_Block *leading = UI_AddBlock(0);
        if (lead) UI_Parent(leading) Prog__SidebarToggle(state, button, m);
        controls = controls && PlatformWindowHasButtons();
        f32 controlWidth = floorf(40.0f * scale);
        f32 plus = floorf(m->bar * 0.9f);
        f32 room = MaxF32(0, width - leadWidth - plus - button - m->inset - (controls ? controlWidth * 3 : 0));
        // A tab dragged over the strip goes into the slot nearest the pointer, which a placeholder marks unless it would stay where it is.
        u64 panelId = Prog__PanelId(state);
        b32 dropHere = state->tabDrag && UI__Contains(strip->rect, UI->mouse);
        f32 slotGuess = floorf(MinF32(m->line * 12.0f, room / (f32)(state->tabCount + 1)));
        u32 dropSlot = dropHere && slotGuess > 0 ? (u32)UI__Clamp(floorf((UI->mouse.x - strip->rect.x0 - leadWidth) / slotGuess + 0.5f), 0, (f32)state->tabCount) : 0;
        b32 stays = state->tabDrop.fromPanel == panelId && (dropSlot == state->tabDrop.fromSlot || dropSlot == state->tabDrop.fromSlot + 1);
        b32 placeholder = dropHere && !stays;
        if (dropHere) {
            state->tabDrop.kind = Prog_TabDrop_Strip;
            state->tabDrop.panel = panelId;
            state->tabDrop.slot = dropSlot;
        }
        f32 slot = floorf(MinF32(m->line * 12.0f, room / (f32)(state->tabCount + (placeholder ? 1 : 0))));
        f32 tabWidth = MaxF32(0, slot - m->separator);
        f32 height = m->bar - top;
        // The shown tab hangs past the strip by its corner radius, so the toolbar drawn after it covers its lower corners and border.
        f32 tabRadius = floorf(8.0f * scale + 0.5f);
        f32 hang = tabRadius + stroke + 1;
        for (u32 i = 0; i <= state->tabCount; ++i) {
            if (placeholder && i == dropSlot) Prog__TabPlaceholder(tabWidth, height, tabRadius, stroke, m);
            if (i == state->tabCount) break;
            b32 shown = i == state->activeTab;
            String path = Prog_TabPath(state, i);
            String name = Prog__FolderName(path);
            UI_Key key = Prog__TabKey(state, i);
            UI_Key closeKey = UI_KeyFromStr(key, S("Close"));
            b32 hot = Prog__TabHot(state, i);
            UI_SetNextPrefWidth(UI_Pixels(tabWidth));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            f32 tabInset = floorf(14.0f * scale);
            UI_SetNextChildrenPadding((UI_Padding){ tabInset, 0, floorf(m->gap * 0.75f), 0 });
            UI_Block *tab = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_MiddleMouseClickable);
            UI_Interaction interaction = UI_InteractionFromBlock(tab);
            if (interaction.pressed && interaction.button == MouseButton_Left) Prog_QueueCommand(state, Prog_CommandKind_SelectTab, i, 0, interaction.pressOrder);
            if (interaction.clicked && interaction.button == MouseButton_Middle) Prog_QueueCommand(state, Prog_CommandKind_CloseTabAt, i, 0, interaction.clickOrder);
            if (interaction.active && interaction.isDragging && interaction.button == MouseButton_Left && !state->tabDrag && !state->tabDragCancelled) {
                state->tabDrag = true;
                state->tabDragKey = key;
                state->tabDrop = (Prog_TabDropTarget){ .fromPanel = panelId, .fromSlot = i };
            }
            UI_Parent(tab) {
                if (shown || hot) {
                    // Floating offsets start inside the padding.
                    // A hovered tab has the shown tab's shape cut off square above the strip's bottom line.
                    UI_SetNextFloatingOffset((UI_Vec2){ -tabInset, 0 });
                    UI_SetNextPrefWidth(UI_Pixels(tabWidth));
                    UI_SetNextPrefHeight(UI_Pixels(shown ? height + hang : height - stroke));
                    UI_Block *outline = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
                    UI_Parent(outline) {
                        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                        UI_SetNextPrefWidth(UI_Pixels(tabWidth));
                        UI_SetNextPrefHeight(UI_Pixels(height + hang));
                        UI_SetNextCornerRadius(tabRadius);
                        UI_SetNextBorderThickness(stroke);
                        UI_SetNextColors((UI_Colors){ .background = shown ? theme->background : theme->alternateRow, .border = theme->tabBorder });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                    }
                }
                // The shown tab's sides curve out into the strip's bottom line: a square of the tab's color with a quarter of a strip-colored circle cut from it.
                // Each square also covers the tab's own border column, which the circle's border takes over.
                // Next to a hovered tab the side stays straight, so that the curve does not cut into the hovered tab's corner.
                if (shown) {
                    f32 flare = floorf(m->radius + 0.5f) + stroke;
                    for (u32 side = 0; side < 2; ++side) {
                        if (Prog__TabHot(state, side ? (i64)i + 1 : (i64)i - 1)) continue;
                        UI_SetNextFloatingOffset((UI_Vec2){ side ? tabWidth - tabInset - stroke : -tabInset - flare, height - flare });
                        UI_SetNextPrefWidth(UI_Pixels(flare + stroke));
                        UI_SetNextPrefHeight(UI_Pixels(flare));
                        UI_SetNextColors((UI_Colors){ .background = theme->background });
                        UI_Block *corner = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_Clip);
                        UI_Parent(corner) {
                            UI_SetNextFloatingOffset((UI_Vec2){ side ? 0 : stroke - flare, -flare });
                            UI_SetNextPrefWidth(UI_Pixels(flare * 2));
                            UI_SetNextPrefHeight(UI_Pixels(flare * 2));
                            UI_SetNextCornerRadius(flare);
                            UI_SetNextBorderThickness(stroke);
                            UI_SetNextColors((UI_Colors){ .background = theme->caption, .border = theme->tabBorder });
                            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                        }
                    }
                }
                Prog_Entry folder = { .file = { .name = name, .kind = P_FileKind_Directory } };
                const Prog_Icon *icon = path.length ? Prog_FileIcon(state, Prog_ParentPath(path), &folder) : NULL;
                if (icon) {
                    UI_SetNextPrefWidth(UI_Pixels((f32)state->iconSize + m->gap));
                    UI_SetNextPrefHeight(UI_Pixels(height));
                    UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
                    UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, (R_Color){ 1, 1, 1, 1 });
                    UI_AddBlock(UI_Flag_DrawImage);
                } else {
                    Prog__Icon(Icon_Folder, (f32)state->iconSize + m->gap, height, theme->icon, 0, m);
                }
                Prog__Label(name, UI_Flex(), height, 0, theme->text, 0);
                // The close button shows on the shown and the hovered tab.
                if (shown || hot) {
                    b32 closeHot = UI->hotKey == closeKey;
                    f32 size = floorf(m->line * 1.3f);
                    UI_SetNextPrefWidth(UI_Pixels(size));
                    UI_SetNextPrefHeight(UI_Pixels(size));
                    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                    UI_SetNextCornerRadius(m->radius);
                    UI_SetNextFont(m->icons);
                    UI_SetNextColors((UI_Colors){ .background = theme->selection, .text = closeHot ? theme->text : theme->icon });
                    UI_SetNextText(Icon_X);
                    UI_Block *close = UI_AddBlockFromKey(closeKey, UI_Flag_DrawText | UI_Flag_LeftMouseClickable | (closeHot ? UI_Flag_DrawBackground : 0));
                    UI_Interaction closing = UI_InteractionFromBlock(close);
                    if (closing.clicked) Prog_QueueCommand(state, Prog_CommandKind_CloseTabAt, i, 0, closing.clickOrder);
                }
            }
            // Lines part the tabs that are neither shown nor hovered, centered on the tabs.
            b32 nextLit = i + 1 == state->activeTab || Prog__TabHot(state, (i64)i + 1);
            UI_Block *gap = Prog__Box(UI_Pixels(m->separator), UI_Pixels(height), UI_Layout_Horizontal, (R_Color){0}, 0);
            if (!shown && !hot && !nextLit) {
                UI_Parent(gap) Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(height * 0.62f)), UI_Layout_Horizontal, theme->alternateRow, 0);
            }
        }
        UI_SetNextPrefWidth(UI_Pixels(plus));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_Block *plusBox = UI_AddBlock(0);
        UI_Parent(plusBox) {
            UI_Interaction add = Prog__TextButton(S("TabNew"), Icon_Plus, m->icons, UI_Pixels(floorf(plus * 0.8f)), floorf(plus * 0.75f), 0, theme->icon, theme->text, theme->iconDisabled,
                                                  state->tabCount < Prog_MaxTabs, m);
            if (add.clicked) Prog_QueueCommand(state, Prog_CommandKind_TabNew, 0, 0, add.clickOrder);
        }
        // The tab list, left of the window buttons.
        UI_SetNextFloatingOffset((UI_Vec2){ width - (controls ? controlWidth * 3 : 0) - button - m->gap, 0 });
        UI_SetNextPrefWidth(UI_Pixels(button));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_Block *listBox = UI_AddBlock(UI_Flag_Floating);
        UI_Parent(listBox) Prog__MenuButton(state, S("TabList"), Icon_ChevronDown, Prog_CommandKind_TabList, true, m);
        // The window buttons fill the strip's full height at its right end.
        if (controls) {
            UI_SetNextFloatingOffset((UI_Vec2){ width - controlWidth * 3, -top });
            UI_SetNextPrefWidth(UI_Pixels(controlWidth * 3));
            UI_SetNextPrefHeight(UI_Pixels(m->bar));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_Block *buttons = UI_AddBlock(UI_Flag_Floating);
            UI_Parent(buttons) {
                Prog_WindowButton maximize = PlatformWindowIsMaximized() ? Prog_WindowButton_Restore : Prog_WindowButton_Maximize;
                Prog__WindowButton(state, S("WindowMinimize"), Prog_WindowButton_Minimize, Prog_CommandKind_WindowMinimize, controlWidth, m->bar, scale, m);
                Prog__WindowButton(state, S("WindowMaximize"), maximize, Prog_CommandKind_WindowToggleMaximize, controlWidth, m->bar, scale, m);
                Prog__WindowButton(state, S("WindowClose"), Prog_WindowButton_Close, Prog_CommandKind_WindowClose, controlWidth, m->bar, scale, m);
            }
        }
    }
}

// "C:/Git/Filey" -> "This PC" › "Local Disk (C:)" › "Git" › "Filey", and a folder inside one of the path roots from that root on, as "Pictures" › "Screenshots".
// Every segment but the current one opens its folder.
function void Prog__Breadcrumbs(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String path = state->directory.path;
    u64 start = path.length && path.value[0] == '/' ? 1 : 0;
    // The deepest root the folder is inside starts the crumbs, under its own folder name.
    u64 rootLength = 0;
    for (u64 i = 0; i < state->pathRoots.header.count; ++i) {
        String root = state->pathRoots.v[i];
        while (root.length > 1 && root.value[root.length - 1] == '/') --root.length;
        if (root.length > rootLength && path.length > root.length + 1 && path.value[root.length] == '/' && StrIsEqual(StrFromTo(path, 0, (i64)root.length), root)) {
            rootLength = root.length;
        }
    }
    b32 drive = !rootLength && path.length >= 2 && path.value[1] == ':';
    if (rootLength) {
        Index slash = StrIndexChar(StrFromTo(path, 0, (i64)rootLength), '/', SSF_SearchBackwards);
        start = slash.exists ? slash.value + 1 : 0;
    } else if (drive) {
        Prog__TextButton(S("Crumb/ThisPC"), S("This PC"), NULL, UI_TextContent(), m->bar * 0.75f, m->gap * 0.5f, theme->textDim, theme->text, theme->textDim, false, m);
    } else if (start) {
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
    }
    for (u64 at = start; at < path.length;) {
        Index slash = StrIndexChar(StrFrom(path, at), '/', SSF_CountOnFail);
        u64 end = at + slash.value;
        String segment = StrFromTo(path, at, (i64)end);
        b32 current = end >= path.length;
        if (segment.length) {
            if (at > start || drive) Prog__SmallIcon(Icon_ChevronRight, floorf(m->line * 1.6f), m->bar, theme->textDim, 0.5f, m);
            // A drive shows under the name the sidebar gives it.
            for (u64 i = 0; drive && at == 0 && i < state->drives.header.count; ++i) {
                const P_Place *place = &state->drives.v[i];
                if (place->path.length >= 2 && Prog__FoldAscii((u8)place->path.value[0]) == Prog__FoldAscii((u8)path.value[0]) && place->path.value[1] == ':') segment = place->name;
            }
            String identity = StrFormat(&state->frameArena, "Crumb/%S", StrFromTo(path, 0, (i64)end));
            UI_Interaction interaction = Prog__TextButton(identity, segment, NULL, UI_TextContent(), m->bar * 0.75f, m->gap * 0.5f,
                                                          theme->textDim, theme->text, current ? theme->text : theme->textDim, !current, m);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_NavigatePrefix, end, 0, interaction.clickOrder);
        }
        at = end + 1;
    }
}

function void Prog__Toolbar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Block *toolbar = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(toolbar) {
        Prog__Spacer(UI_Pixels(m->inset * 0.6f), UI_Pixels(m->bar));
        String parent = Prog_ParentPath(state->directory.path);
        Prog__ToolbarButton(state, S("Back"), Icon_ArrowLeft, Prog_CommandKind_Back, state->historyAt > 0, m);
        Prog__ToolbarButton(state, S("Forward"), Icon_ArrowRight, Prog_CommandKind_Forward, state->historyAt + 1 < state->history.header.count, m);
        Prog__ToolbarButton(state, S("Up"), Icon_ArrowUp, Prog_CommandKind_Up, parent.length && !StrIsEqual(parent, state->directory.path), m);
        Prog__MenuButton(state, S("HistoryMenu"), Icon_ChevronDown, Prog_CommandKind_HistoryMenu, state->history.header.count > 1, m);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
        Prog__Box(UI_Pixels(m->separator), UI_Pixels(m->line * 1.2f), UI_Layout_Horizontal, theme->separator, 0);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
        // Lit while the folder is bookmarked.
        b32 bookmarked = Prog_IsBookmarked(state, state->directory.path);
        UI_Interaction bookmark = Prog__TextButton(S("Bookmark"), Icon_Bookmark, m->icons, UI_Pixels(m->bar * 0.75f), m->bar * 0.75f, 0,
                                                   bookmarked ? theme->textMatch : theme->icon, bookmarked ? theme->textMatch : theme->text, theme->iconDisabled,
                                                   state->directory.path.length != 0, m);
        if (bookmark.clicked) Prog_QueueCommand(state, Prog_CommandKind_BookmarksToggle, 0, 0, bookmark.clickOrder);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
        Prog__Box(UI_Pixels(m->separator), UI_Pixels(m->line * 1.2f), UI_Layout_Horizontal, theme->separator, 0);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
        // A path wider than the room shows its end, cut off at the left.
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(m->bar));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextScrollOffset((UI_Vec2){ 1e9f, 0 });
        UI_Block *crumbs = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Breadcrumbs")), UI_Flag_Clip);
        UI_Parent(crumbs) Prog__Breadcrumbs(state, m);
        Prog__MenuButton(state, S("PanelMenu"), Icon_EllipsisVertical, Prog_CommandKind_PanelMenu, true, m);
        Prog__Spacer(UI_Pixels(m->inset * 0.4f), UI_Pixels(m->bar));
    }
}

typedef struct Prog_Column {
    String title;
    f32 width;   // 0 takes the remaining width
    f32 alignX;
} Prog_Column;

// The sort a column header selects.
global const Prog_Sort Prog_ColumnSort[Prog_ColumnId_Count] = {
    Prog_Sort_Name, Prog_Sort_Type, Prog_Sort_Modified, Prog_Sort_Folders, Prog_Sort_Files, Prog_Sort_Size,
};

// The columns of a list whose rows are `room` wide: shares of it as the reference measures them, Name taking what is left, down to the least that still shows a size or a count whole.
function void Prog__Columns(Prog_Column *columns, f32 scale, f32 room)
{
    local_persist const f32 shares[Prog_ColumnId_Count] = { 0, 0.15f, 0.15f, 0.075f, 0.075f, 0.1f };
    // Of the slot they share, Folders takes a little more than half.
    local_persist const f32 shift[Prog_ColumnId_Count] = { 0, 0, 0, 6, -6, 0 };
    local_persist const f32 least[Prog_ColumnId_Count] = { 0, 50, 70, 45, 45, 75 };
    if (room <= 0) room = 918.0f * scale;
    local_persist const String titles[Prog_ColumnId_Count] = { S("Name"), S("Type"), S("Date Modified"), S("Folders"), S("Files"), S("Size") };
    for (u32 i = 0; i < Prog_ColumnId_Count; ++i) {
        f32 width = i == Prog_ColumnId_Name ? 0 : MaxF32(least[i] * scale, floorf(shares[i] * room + shift[i] * scale + 0.5f));
        columns[i] = (Prog_Column){ titles[i], width, i == Prog_ColumnId_Size ? 1.0f : 0 };
    }
}

// The space inside a details column before and after its title and values; Files sits close after Folders, and sizes keep clear of the row's end.
function f32 Prog__ColumnLeft(u32 column, const Prog_Metrics *m)
{
    return column == Prog_ColumnId_Name ? m->gap : floorf((column == Prog_ColumnId_Files ? 8.0f : 20.0f) * m->scale + 0.5f);
}

function f32 Prog__ColumnRight(u32 column, const Prog_Metrics *m)
{
    return column == Prog_ColumnId_Size ? floorf(20.0f * m->scale + 0.5f) : column == Prog_ColumnId_Folders ? 0 : m->gap;
}

// Folders and Files share one slot, so no line parts them.
function b32 Prog__ColumnLineAfter(u32 column) { return column + 1 < Prog_ColumnId_Count && column != Prog_ColumnId_Folders; }

function String Prog__Ellipsize(Arena *arena, const R_Font *font, String text, f32 width);

function void Prog__Header(Prog_State *state, const Prog_Column *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
    UI_Block *header = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(header) {
        for (u32 i = 0; i < Prog_ColumnId_Count; ++i) {
            Prog_Sort sort = Prog_ColumnSort[i];
            b32 sorted = sort < Prog_Sort_Count && state->sort == sort;
            UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Header/%S", columns[i].title));
            b32 sortable = sort < Prog_Sort_Count && !state->loading;
            b32 hot = UI->hotKey == key && sortable;
            UI_SetNextPrefWidth(columns[i].width > 0 ? UI_Pixels(columns[i].width) : UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_Block *cell = UI_AddBlockFromKey(key, sortable ? UI_Flag_LeftMouseClickable : 0);
            UI_Interaction interaction = UI_InteractionFromBlock(cell);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_Sort, (u64)sort, 0, interaction.clickOrder);
            UI_Parent(cell) {
                b32 name = i == Prog_ColumnId_Name;
                f32 left = Prog__ColumnLeft(i, m), right = Prog__ColumnRight(i, m), line = Prog__ColumnLineAfter(i) ? m->separator : 0;
                if (name) Prog__Spacer(UI_Pixels(m->iconSlot - m->gap), UI_Pixels(m->row));
                R_Color color = sorted || hot ? theme->text : theme->textDim;
                // Titles start at the left even over right-aligned values, and end in an ellipsis where the column is too narrow; the sort arrow sits at the column's end.
                f32 arrow = sorted ? floorf(m->line * 0.6f) : 0;
                f32 room = columns[i].width > 0 ? columns[i].width - left - right - line - arrow : 0;
                Prog__LabelPadded(room > 0 ? Prog__Ellipsize(&state->frameArena, m->font, columns[i].title, room) : columns[i].title, UI_Flex(), m->row, left, sorted ? 0 : right, color, 0);
                if (sorted) Prog__Icon(state->descending ? Icon_MoveDown : Icon_MoveUp, arrow, m->row, theme->textDim, 0.5f, m);
                if (sorted) Prog__Spacer(UI_Pixels(MaxF32(right, Prog__ColumnLeft(Prog_ColumnId_Type, m))), UI_Pixels(m->row));
                if (line > 0) {
                    UI_SetNextCornerRadius(line * 0.5f);
                    Prog__Box(UI_Pixels(line), UI_Pixels(floorf(m->line * 1.15f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0)->flags |= UI_Flag_DrawBackground;
                }
                // While folder totals are being counted, the columns they fill show a spinner at their right end.
                const Prog_Icon *spinner = state->measureJob && (i == Prog_ColumnId_Files || i == Prog_ColumnId_Size) ? Prog_SpinnerIcon(state, UI->nowMicroseconds) : NULL;
                if (spinner) {
                    f32 slot = m->line + m->gap;
                    UI_SetNextFloatingOffset((UI_Vec2){ cell->rect.x1 - cell->rect.x0 - slot - (Prog__ColumnLineAfter(i) ? m->separator + m->gap : 0), 0 });
                    UI_SetNextPrefWidth(UI_Pixels(slot));
                    UI_SetNextPrefHeight(UI_Pixels(m->row));
                    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                    UI_SetNextImage(spinner->texture, spinner->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, theme->icon);
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawImage);
                }
            }
        }
    }
}

// The caret stays lit for a moment after every change, then fades out and back in, about once a second.
function f32 Prog__CaretOpacity(const Prog_TextEdit *edit)
{
    UI_Key key = UI_KeyFromStr(0, StrFormat(UI->frameArena, "Caret/%p/%u/%u/%llu", (const void *)edit, edit->cursor, edit->anchor, (unsigned long long)Prog_EditText(edit).length));
    u64 since = UI_TimeSince(key) % 1050000;
    UI_KeepAnimating();
    if (!UI->animate || since < 300000) return 1;
    f32 t = (f32)(since - 300000) / 375000.0f;
    return 0.85f * (t < 1 ? 1 - t : t - 1);
}

// The text of `edit` with its selection and, when `caret` is set, its caret, floating at `x`, `y` in the current parent.
function void Prog__EditContents(const Prog_TextEdit *edit, f32 x, f32 y, b32 caret, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String text = Prog_EditText(edit);
    u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
    f32 x0 = x + R_TextWidth(m->font, StrMake(text.value, from));
    f32 x1 = x + R_TextWidth(m->font, StrMake(text.value, to));
    if (caret && to > from) {
        UI_SetNextFloatingOffset((UI_Vec2){ x0, y });
        UI_SetNextPrefWidth(UI_Pixels(x1 - x0));
        UI_SetNextPrefHeight(UI_Pixels(m->line));
        UI_SetNextColors((UI_Colors){ .background = theme->hover });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextPrefHeight(UI_Pixels(m->line));
    UI_SetNextColors((UI_Colors){ .text = theme->text });
    UI_SetNextText(text);
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
    if (caret) {
        UI_SetNextFloatingOffset((UI_Vec2){ edit->cursor == from ? x0 : x1, y });
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
    b32 drag = interaction.active && !interaction.pressed;
    if (interaction.button != MouseButton_Left || (!interaction.pressed && !drag)) return;
    u32 at = Prog__EditOffsetAt(edit, m->font, UI->mouse.x - block->rect.x0 - x);
    Prog_Command *command = Prog_QueueCommand(state, kind, at, interaction.pressCount, interaction.pressed ? interaction.pressOrder : 0);
    command->mods = interaction.mods | (drag ? KeyMod_Shift : 0);
}

// The name being edited, with its selection and caret, in place of the name label.
function void Prog__RenameBox(Prog_State *state, UI_Size width, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String text = Prog_EditText(&state->rename);
    f32 pad = floorf(m->gap * 0.5f);
    f32 height = m->line + pad;
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenPadding((UI_Padding){ m->gap - pad, 0, m->gap, 0 });
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *cell = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(cell) {
        UI_SetNextPrefWidth(UI_Pixels(R_TextWidth(m->font, text) + 2 * pad + Prog__CaretWidth(m)));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextCornerRadius(m->radius);
        UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->focus });
        UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("RenameBox")), UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        state->renameRect = box->rect;
        Prog__EditMouse(state, box, &state->rename, pad, Prog_CommandKind_RenameCaret, m);
        UI_Parent(box) Prog__EditContents(&state->rename, pad, floorf((height - m->line) * 0.5f), true, m);
    }
}

// A name whose first `prefix` bytes, the folder in an expanded listing, are dimmed, with the bytes a filter matched after it, flagged in `marks` when not NULL, in the match color.
function void Prog__NameLabel(String name, u64 prefix, const u8 *marks, UI_Size width, R_Color color, R_Color dim, const Prog_Metrics *m)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, m->gap, 0 });
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *cell = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(cell) {
        if (prefix) Prog__Label(StrFromTo(name, 0, (i64)prefix), UI_TextContent(), m->row, 0, dim, 0);
        for (u64 start = prefix, end = prefix; start < name.length; start = end) {
            b32 match = marks && marks[start - prefix];
            while (end < name.length && (marks && marks[end - prefix]) == match) ++end;
            Prog__Label(StrFromTo(name, start, (i64)end), UI_TextContent(), m->row, 0, match ? Prog_CurrentTheme()->textMatch : color, 0);
        }
    }
}

// What a row or cell shows in each column: folders show their totals once measured; zero counts and files' counts read "--".
function void Prog__EntryValues(Prog_State *state, const Prog_Entry *entry, String *values)
{
    Arena *frame = &state->frameArena;
    String none = S("--");
    String folders = none, files = none, size = {0};
    if (entry->file.kind == P_FileKind_Directory) {
        if (entry->measured) {
            if (entry->folders) folders = StrFormat(frame, "%" PRIu64, entry->folders);
            if (entry->files) files = StrFormat(frame, "%" PRIu64, entry->files);
            size = entry->files ? Prog_FormatSize(frame, entry->bytes) : none;
        } else {
            folders = files = (String){0};
        }
    } else if (entry->file.metadataValid) {
        size = Prog_FormatSize(frame, entry->file.size);
    }
    String result[Prog_ColumnId_Count] = { entry->file.name, entry->type, entry->modified, folders, files, size };
    for (u32 i = 0; i < Prog_ColumnId_Count; ++i) values[i] = result[i];
}

// `text` cut at a codepoint boundary to fit `width`, with an ellipsis when anything was cut.
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

// How an item's box shows: filled with the selection color when selected, washed with it and outlined under the pointer, and outlined as the keyboard's item.
typedef struct Prog_ItemLook { R_Color fill, border; UI_Flags flags; } Prog_ItemLook;

function Prog_ItemLook Prog__ItemLook(b32 selected, b32 hot, b32 focused)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog_ItemLook result = { .fill = theme->selection, .border = hot ? theme->selection : theme->focus };
    if (!selected) result.fill.a *= 0.35f;
    if (selected || hot) result.flags |= UI_Flag_DrawBackground;
    if (!selected && (hot || focused)) result.flags |= UI_Flag_DrawBorder;
    return result;
}

// The line under each row of the list.
function f32 Prog__RowLine(const Prog_Metrics *m) { return MaxF32(1, floorf(2.0f * m->scale + 0.5f)); }

// A line-height item's box behind what comes after it in the current block, `width` wide, with the line under it.
function void Prog__ItemBox(Prog_ItemLook look, UI_Size width, const Prog_Metrics *m)
{
    f32 line = Prog__RowLine(m);
    if (look.flags) {
        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
        UI_SetNextPrefWidth(width);
        UI_SetNextPrefHeight(UI_Pixels(m->row - line));
        UI_SetNextCornerRadius(m->radius);
        UI_SetNextBorderThickness(1.5f * m->scale);
        UI_SetNextColors((UI_Colors){ .background = look.fill, .border = look.border });
        UI_AddBlock(UI_Flag_Floating | look.flags);
    }
    UI_SetNextFloatingOffset((UI_Vec2){ 0, m->row - line });
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(line));
    UI_SetNextColors((UI_Colors){ .background = Prog_CurrentTheme()->separator });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
}

// Hidden and dot-named items are faded whether or not the OS marks them hidden.
function b32 Prog__EntryFaded(const Prog_Entry *entry)
{
    return entry->file.hidden || (entry->file.name.length && entry->file.name.value[0] == '.');
}

// The item's system icon drawn at `size` in a `width` x `height` slot, or a glyph while it loads or when it has none.
// Above the list icon size it is the item's thumbnail, and nothing shows while that loads, since a stretched list icon would only blur.
function void Prog__ItemIcon(Prog_State *state, const Prog_Entry *entry, f32 size, f32 width, f32 height, f32 alignX, b32 highlighted, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    b32 faded = Prog__EntryFaded(entry);
    b32 thumbnail = size > (f32)state->iconSize;
    const Prog_Icon *icon = thumbnail ? Prog_ThumbnailIcon(state, state->directory.path, entry, (i32)size) : Prog_FileIcon(state, state->directory.path, entry);
    if (thumbnail && !icon) {
        Prog__Spacer(UI_Pixels(width), UI_Pixels(height));
    } else if (icon) {
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextTextAlignment((UI_Vec2){ alignX, 0.5f });
        UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ size, size }, (R_Color){ 1, 1, 1, faded ? 0.35f : 1 });
        UI_AddBlock(UI_Flag_DrawImage);
    } else {
        Prog__Icon(entry->file.kind == P_FileKind_Directory ? Icon_Folder : Icon_File, width, height,
                   highlighted ? theme->text : faded ? theme->iconDisabled : theme->icon, alignX, m);
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
    // Clicks inside the name being edited stay in the box instead of reopening or dragging the item.
    if (renaming && UI__Contains(state->renameRect, UI->mouse)) {
    } else if (!state->loading && interaction.pressed && interaction.button == MouseButton_Right) {
        Prog_QueueCommand(state, Prog_CommandKind_ContextMenuAt, index, interaction.pressCount, interaction.pressOrder);
    } else if (!state->loading && ((interaction.pressed && !keepSelection) ||
                                   (interaction.clicked && keepSelection && interaction.button == MouseButton_Left && state->marqueeKey != key))) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SelectRow, index, interaction.pressCount,
                                                  interaction.pressed ? interaction.pressOrder : interaction.clickOrder);
        command->mods = interaction.mods;
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

// A Details or List row; List shows only the icon and the name.
function b32 Prog__Row(Prog_State *state, u64 index, u64 position, UI_Key key, const Prog_Column *columns, b32 nameOnly, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog_Entry *entry = &state->directory.entries[index];
    b32 focused = state->hasFocus && state->focusedRow == index;
    b32 hot = UI->hotKey == key && !state->loading;
    (void)position;
    b32 highlighted = entry->selected || hot;
    b32 faded = Prog__EntryFaded(entry);
    f32 height = m->row - Prog__RowLine(m);
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *row = UI_AddBlockFromKey(key, !state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0);
    b32 renaming = Prog__ItemRenaming(state, entry);
    // Presses on the icon and the name drag the files; elsewhere on the row they draw a selection box, as in Explorer.
    b32 onName = UI->pressOrigin.x < row->rect.x0 + m->iconSlot + R_TextWidth(m->font, entry->file.name) + m->gap;
    b32 result = Prog__ItemInput(state, index, row, key, onName, renaming);
    String values[Prog_ColumnId_Count];
    Prog__EntryValues(state, entry, values);
    UI_Parent(row) {
        Prog__ItemBox(Prog__ItemLook(entry->selected, hot, focused), UI_Percent(1), m);
        Prog__ItemIcon(state, entry, (f32)state->iconSize, m->iconSlot - m->gap, height, 1, highlighted, m);
        for (u32 i = 0; i < (nameOnly ? 1u : (u32)Prog_ColumnId_Count); ++i) {
            UI_Size width = columns[i].width > 0 && !nameOnly ? UI_Pixels(columns[i].width) : UI_Flex();
            if (i == Prog_ColumnId_Name && renaming) {
                Prog__RenameBox(state, width, m);
                continue;
            }
            R_Color color = (i == Prog_ColumnId_Name && !faded) || highlighted ? theme->text : theme->textDim;
            if (i == Prog_ColumnId_Name && (state->filter.length || state->directory.expanded)) {
                String leaf = Prog_EntryLeaf(entry->file.name);
                u64 prefix = entry->file.name.length - leaf.length;
                u8 *marks = state->filter.length ? ArenaPushArrayNoZero(&state->frameArena, Max(leaf.length, (u64)1), u8) : NULL;
                if (marks && !Prog_FilterMatch(leaf, Prog_EditText(&state->filter), marks)) marks = NULL;
                Prog__NameLabel(entry->file.name, prefix, marks, width, color, theme->textDim, m);
                continue;
            }
            // Values too wide for a fixed column end in an ellipsis.
            f32 left = Prog__ColumnLeft(i, m), right = Prog__ColumnRight(i, m);
            // Right-aligned values may run into the space before them.
            if (columns[i].alignX > 0) left = m->gap;
            String value = columns[i].width > 0 && !nameOnly ? Prog__Ellipsize(&state->frameArena, m->detail, values[i], columns[i].width - left - right) : values[i];
            if (i != Prog_ColumnId_Name) UI_SetNextFont(m->detail);
            Prog__LabelPadded(value, width, height, left, right, color, columns[i].alignX);
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
    (void)line;
    b32 highlighted = entry->selected || hot;
    f32 height = m->row - Prog__RowLine(m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *cell = UI_AddBlockFromKey(key, !state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0);
    b32 renaming = Prog__ItemRenaming(state, entry);
    b32 onName = UI->pressOrigin.x < cell->rect.x0 + m->iconSlot + R_TextWidth(m->font, entry->file.name) + m->gap;
    b32 result = Prog__ItemInput(state, index, cell, key, onName, renaming);
    UI_Parent(cell) {
        Prog__ItemBox(Prog__ItemLook(entry->selected, hot, focused), UI_Pixels(width), m);
        Prog__ItemIcon(state, entry, (f32)state->iconSize, m->iconSlot - m->gap, height, 1, highlighted, m);
        if (renaming) Prog__RenameBox(state, UI_Flex(), m);
        else Prog__Label(entry->file.name, UI_Flex(), height, m->gap, !Prog__EntryFaded(entry) || highlighted ? theme->text : theme->textDim, 0);
    }
    return result;
}

// An icon view cell: the icon over the name and the size, centered, on a rounded box that shows only when selected, hovered or focused.
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
    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf(9.0f * m->scale), 0, 0 });
    UI_SetNextCornerRadius(m->radius * 2);
    Prog_ItemLook look = Prog__ItemLook(entry->selected, hot, focused);
    UI_SetNextBorderThickness(1.5f * m->scale);
    UI_SetNextColors((UI_Colors){ .background = look.fill, .border = look.border });
    UI_Block *cell = UI_AddBlockFromKey(key, look.flags | (!state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0));
    b32 renaming = Prog__ItemRenaming(state, entry);
    // The whole cell is the item, so presses on it drag the files; the box selection starts from the space between cells.
    Prog__ItemInput(state, index, cell, key, true, renaming);
    String values[Prog_ColumnId_Count];
    Prog__EntryValues(state, entry, values);
    f32 text = width - m->gap;
    UI_Parent(cell) {
        Prog__ItemIcon(state, entry, iconSize, width, iconSize, 0.5f, highlighted, m);
        if (renaming) {
            Prog__RenameBox(state, UI_Pixels(width), m);
        } else {
            String name = Prog__Ellipsize(&state->frameArena, m->font, entry->file.name, text);
            Prog__Label(name, UI_Pixels(width), m->line, 0, !Prog__EntryFaded(entry) || highlighted ? theme->text : theme->textDim, 0.5f);
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
    return zoom < Prog_ZoomFirstIcon ? (Prog_View)zoom : Prog_View_Icons;
}

// Zoom as a fraction: 0 for Details, a tenth a step to Columns, then icons from 0.3 in steps of 0.055 up to 1.
function f32 Prog__ZoomFraction(u32 zoom)
{
    return zoom < Prog_ZoomFirstIcon ? (f32)zoom * 0.1f : MinF32(1, 0.3f + (f32)(zoom - Prog_ZoomFirstIcon) * 0.055f);
}

// Icon views draw their icons from 48 logical pixels at 30% to 205 at 100%, in physical pixels.
function f32 Prog__ZoomIconSize(u32 zoom, f32 scale)
{
    f32 t = (Prog__ZoomFraction(zoom) - 0.3f) / 0.7f;
    return floorf((48.0f + t * (204.8f - 48.0f)) * scale + 0.5f);
}

function void Prog__List(Prog_State *state, const Prog_Column *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Files")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable |
                                        (!state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0));
    UI_Interaction listInteraction = UI_InteractionFromBlock(list);
    if (listInteraction.pressed && listInteraction.button == MouseButton_Right) {
        Prog_QueueCommand(state, Prog_CommandKind_ContextMenuAt, UINT64_MAX, listInteraction.pressCount, listInteraction.pressOrder);
    }
    // A plain click on the empty space around the items clears the selection.
    if (listInteraction.clicked && listInteraction.button == MouseButton_Left && !listInteraction.mods && state->marqueeKey != list->key) {
        state->marqueeBox = (R_Rect){0};
        Prog_QueueCommand(state, Prog_CommandKind_MarqueeStart, 0, 0, listInteraction.clickOrder);
        Prog_QueueCommand(state, Prog_CommandKind_MarqueeSelect, 0, 0, listInteraction.clickOrder);
    }
    if (state->marquee && UI->activeKey != state->marqueeKey) state->marquee = false;
    // A new folder starts where it is shown, without easing there from the last one.
    b32 snapScroll = state->resetScroll;
    if (state->resetScroll) { list->scrollOffset = list->scrollShown = (UI_Vec2){0}; state->resetScroll = false; }

    // The grid: one item per line in Details and List, and as many cells as fit across in Columns and the icon views, stretched to fill the line.
    Prog_View view = Prog_ZoomView(state->zoom);
    f32 width = MaxF32(1, list->rect.x1 - list->rect.x0), inner = MaxF32(1, width - 2 * m->inset);
    u64 perLine = 1;
    UI_Vec2 pitch = { width, m->row };
    R_Rect item = { 0, 0, width, m->row };
    f32 iconSize = 0;
    if (view == Prog_View_Columns) {
        perLine = Max((u64)1, (u64)(inner / floorf(m->line * 25.0f)));
        pitch.x = floorf(inner / (f32)perLine);
        item = (R_Rect){ m->inset, 0, m->inset + pitch.x - floorf(27.0f * m->scale), m->row };
    } else if (view == Prog_View_Icons) {
        iconSize = Prog__ZoomIconSize(state->zoom, m->scale);
        perLine = Max((u64)1, (u64)(inner / (iconSize + floorf(30.0f * m->scale))));
        pitch = (UI_Vec2){ floorf(inner / (f32)perLine), iconSize + floorf(77.0f * m->scale) };
        f32 gap = floorf(7.0f * m->scale), top = floorf(8.0f * m->scale);
        item = (R_Rect){ m->inset + gap, top, m->inset + pitch.x - gap, top + iconSize + floorf(53.0f * m->scale) };
    }
    state->gridColumns = perLine;
    state->gridPitch = pitch;
    state->gridItem = item;
    u64 lines = (state->viewCount + perLine - 1) / perLine;

    state->pageRows = (u64)(Max(0.0f, list->rect.y1 - list->rect.y0) / pitch.y);
    // Before its first layout the list has no height to reveal anything in.
    if (state->revealFocus && state->hasFocus && list->rect.y1 > list->rect.y0) {
        u64 position = Prog_ViewPosition(state, state->focusedRow);
        f32 top = (f32)((position == UINT64_MAX ? 0 : position) / perLine) * pitch.y;
        f32 height = Max(0.0f, list->rect.y1 - list->rect.y0);
        if (top < list->scrollOffset.y) list->scrollOffset.y = top;
        else if (top + pitch.y > list->scrollOffset.y + height) list->scrollOffset.y = Max(0.0f, top + pitch.y - height);
        state->revealFocus = false;
    }
    if (snapScroll || !UI->animate) list->scrollShown = list->scrollOffset;
    // Only lines near the viewport get blocks; spacers stand in for the rest. While scrolling eases, both ends of the way are covered.
    f32 scrollFrom = MinF32(list->scrollOffset.y, list->scrollShown.y), scrollTo = MaxF32(list->scrollOffset.y, list->scrollShown.y);
    u64 first = (u64)(Max(0.0f, scrollFrom) / pitch.y);
    if (first > 2) first -= 2; else first = 0;
    first = Min(first, lines);
    f32 visibleHeight = Max(pitch.y, UI->root->rect.y1 - UI->root->rect.y0) + (scrollTo - scrollFrom);
    u64 end = Min(lines, first + (u64)(visibleHeight / pitch.y) + 6);
    UI_Parent(list) {
        if (first) Prog__Spacer(UI_Flex(), UI_Pixels((f32)first * pitch.y));
        UI_Key directoryKey = UI_KeyFromStr(list->key, state->directory.path);
        UI_Key marqueeFrom = 0;
        for (u64 line = first; line < end; ++line) {
            if (perLine == 1) {
                u64 row = state->view[line];
                UI_Key key = UI_KeyFromStr(directoryKey, state->directory.entries[row].file.name);
                if (Prog__Row(state, row, line, key, columns, view == Prog_View_List, m)) marqueeFrom = key;
                continue;
            }
            UI_Block *cells = Prog__Box(UI_Flex(), UI_Pixels(pitch.y), UI_Layout_Horizontal, (R_Color){0}, 0);
            UI_Parent(cells) {
                for (u64 at = line * perLine; at < Min(state->viewCount, (line + 1) * perLine); ++at) {
                    u64 row = state->view[at];
                    UI_Key key = UI_KeyFromStr(directoryKey, state->directory.entries[row].file.name);
                    // Each cell is a slot of the pitch, with the item inside it where the grid puts it.
                    UI_SetNextPrefWidth(UI_Pixels(pitch.x));
                    UI_SetNextPrefHeight(UI_Pixels(pitch.y));
                    UI_SetNextChildrenPadding((UI_Padding){ item.x0 - m->inset, item.y0, 0, 0 });
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
        if (end < lines) Prog__Spacer(UI_Flex(), UI_Pixels((f32)(lines - end) * pitch.y));
        if (!state->viewCount && !state->loading) {
            String empty = state->directory.count ? S("No items match the filter") : S("This folder is empty");
            Prog__Label(empty, UI_Flex(), m->row, m->iconSlot, theme->textDim, 0);
        }
    }
}

function void Prog__StatusBar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Block *bar = Prog__Box(UI_Flex(), UI_Pixels(m->pill), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(bar) {
        UI_SetNextPrefWidth(UI_Pixels(floorf(m->line * 15.3f)));
        UI_SetNextPrefHeight(UI_Pixels(m->pill));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, m->gap, 0 });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_SetNextColors((UI_Colors){ .background = theme->panel, .border = theme->focus });
        UI_Block *filter = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilterBox")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_LeftMouseClickable |
                                              (state->filtering ? UI_Flag_DrawBorder : 0));
        state->filterRect = filter->rect;
        UI_Interaction interaction = UI_InteractionFromBlock(filter);
        if (interaction.pressed) Prog_QueueCommand(state, Prog_CommandKind_StartFilter, 0, interaction.pressCount, interaction.pressOrder);
        UI_Parent(filter) {
            Prog__Icon(Icon_Search, m->iconSlot - m->gap, m->pill, state->filter.length ? theme->text : theme->textDim, 0.5f, m);
            if (state->filtering || state->filter.length) {
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(m->pill));
                UI_Block *field = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilterField")), UI_Flag_Clip | UI_Flag_LeftMouseClickable);
                Prog__EditMouse(state, field, &state->filter, floorf(m->gap * 0.5f), Prog_CommandKind_FilterCaret, m);
                UI_Parent(field) Prog__EditContents(&state->filter, floorf(m->gap * 0.5f), floorf((m->pill - m->line) * 0.5f), state->filtering, m);
                Prog__PillButton(state, S("FilterExpand"), Icon_FolderTree, Prog_CommandKind_ToggleExpandFolder, state->expanded, m);
                Prog__PillButton(state, S("FilterPin"), state->filterSticky ? Icon_Pin : Icon_PinOff, Prog_CommandKind_ToggleFilterPin, state->filterSticky, m);
                Prog__PillButton(state, S("FilterApply"), Icon_Check, Prog_CommandKind_ApplyFilter, false, m);
                Prog__PillButton(state, S("FilterOpen"), Icon_CornerDownLeft, Prog_CommandKind_OpenFocused, false, m);
                Prog__PillButton(state, S("FilterRemove"), Icon_X, Prog_CommandKind_RemoveFilter, false, m);
            } else {
                String placeholder = StrFormat(&state->frameArena, "Filter %" PRIu64 " files...", state->directory.count);
                Prog__Label(placeholder, UI_Flex(), m->pill, m->gap * 0.5f, theme->textDim, 0);
                Prog__PillButton(state, S("FilterExpand"), Icon_FolderTree, Prog_CommandKind_ToggleExpandFolder, state->expanded, m);
            }
        }

        Prog__Spacer(UI_Flex(), UI_Pixels(m->pill));

        String status = state->error;
        if (!status.length && state->loading) {
            status = StrFormat(&state->frameArena, "Loading %S...", ((Prog_JobInput *)state->directoryJob->input)->path);
        }
        if (!status.length) {
            // Shown out of everything the folder holds; one number when nothing is left out.
            const Prog_Directory *directory = &state->directory;
            u64 shown[2] = { state->viewFolders, state->viewCount - state->viewFolders };
            u64 total[2] = { directory->totalFolders, directory->totalFiles };
            b32 hidden[2] = { state->hideFolders, state->hideFiles };
            String icons[2] = { Icon_Folder, Icon_File };
            String keys[2] = { S("ToggleFolders"), S("ToggleFiles") };
            Prog_CommandKind kinds[2] = { Prog_CommandKind_ToggleFolders, Prog_CommandKind_ToggleFiles };
            UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, m->gap, 0 });
            UI_SetNextCornerRadius(m->radius * 2);
            UI_Block *counts = Prog__Box(UI_Fill(), UI_Pixels(m->pill), UI_Layout_Horizontal, theme->panel, 0);
            UI_Parent(counts) {
                for (u32 i = 0; i < 2; ++i) {
                    String text = shown[i] == total[i] ? StrFormat(&state->frameArena, "%" PRIu64, total[i])
                                                       : StrFormat(&state->frameArena, "%" PRIu64 "/%" PRIu64, shown[i], total[i]);
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
                        Prog__Icon(icons[i], 0, m->pill, hidden[i] ? theme->iconDisabled : hot ? theme->text : theme->icon, 0.5f, m);
                        Prog__Label(text, UI_TextContent(), m->pill, m->gap * 0.5f, hidden[i] ? theme->textDim : theme->text, 0);
                    }
                }
                Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->pill));
                // The view and how far it is zoomed, as a bar filled to the zoom's percentage.
                Prog_View view = Prog_ZoomView(state->zoom);
                local_persist const String viewIcons[Prog_View_Count] = { Icon_List, Icon_Menu, Icon_Columns3, Icon_Image };
                local_persist const String viewNames[Prog_View_Count] = { S("Details"), S("List"), S("Columns"), {0} };
                u32 step = state->zoom - Prog_ZoomFirstIcon;
                String name = view != Prog_View_Icons ? viewNames[view] : step < 7 ? S("M Icons") : state->zoom + 1 < Prog_ZoomLevels ? S("L Icons") : S("XL Icons");
                f32 zoom = Prog__ZoomFraction(state->zoom);
                f32 barWidth = floorf(m->line * 2.4f), barHeight = floorf(m->line * 1.2f);
                // The view and its zoom open the view menu above them.
                UI_SetNextPrefWidth(UI_Pixels(R_TextWidth(m->icons, viewIcons[view]) + R_TextWidth(m->font, name) + m->gap * 1.5f + barWidth));
                UI_SetNextPrefHeight(UI_Pixels(m->pill));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Block *viewButton = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ViewMenu")), UI_Flag_LeftMouseClickable);
                UI_Interaction viewClick = UI_InteractionFromBlock(viewButton);
                if (viewClick.clicked) {
                    f32 menuHeight = m->row * 9 + floorf(13.0f * m->scale);
                    Prog_QueueCommand(state, Prog_CommandKind_ViewMenu, 0, 0, viewClick.clickOrder)->at = (UI_Vec2){ viewButton->rect.x0, viewButton->rect.y0 - menuHeight };
                }
                b32 viewHot = UI->hotKey == viewButton->key;
                UI_Parent(viewButton) {
                Prog__Icon(viewIcons[view], 0, m->pill, viewHot ? theme->text : theme->icon, 0.5f, m);
                Prog__Label(name, UI_TextContent(), m->pill, m->gap * 0.5f, theme->text, 0);
                Prog__Spacer(UI_Pixels(m->gap * 0.5f), UI_Pixels(m->pill));
                UI_SetNextPrefWidth(UI_Pixels(barWidth));
                UI_SetNextPrefHeight(UI_Pixels(barHeight));
                UI_SetNextCornerRadius(m->radius * 0.5f);
                UI_SetNextColors((UI_Colors){ .background = theme->track });
                UI_Block *bar = UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_Clip);
                UI_Parent(bar) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                    UI_SetNextPrefWidth(UI_Pixels(floorf(barWidth * zoom)));
                    UI_SetNextPrefHeight(UI_Pixels(barHeight));
                    UI_SetNextCornerRadius(m->radius * 0.5f);
                    UI_SetNextColors((UI_Colors){ .background = theme->accent });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    // A block's text draws before its children, so the percentage is a child after the fill.
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                    Prog__Label(StrFormat(&state->frameArena, "%u%%", (u32)(zoom * 100.0f + 0.001f)), UI_Pixels(barWidth), barHeight, 0, theme->text, 0.5f);
                }
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
function void Prog__GroupTitle(Prog_State *state, Prog_Group group, String title, String icon, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Group/%S", title));
    b32 hot = UI->hotKey == key;
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ floorf(m->gap * 0.5f), 0, floorf(m->gap * 0.5f), 0 });
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = theme->alternateRow });
    UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | (hot ? UI_Flag_DrawBackground : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(row);
    if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_ToggleGroup, (u64)group, 0, interaction.clickOrder);
    UI_Parent(row) {
        R_Color color = hot ? theme->text : theme->textHeading;
        if (icon.length) Prog__Icon(icon, m->line * 1.5f, m->row, color, 0, m);
        else Prog__DriveGlyph(m->line * 1.5f, m->row, color, m);
        Prog__Label(title, UI_Flex(), m->row, 0, color, 0);
        Prog__Icon(Prog_GroupOpen(state, group) ? Icon_ChevronDown : Icon_ChevronRight, m->line * 1.5f, m->row, color, 0.5f, m);
    }
}

// One location under a section title: its folder icon, or a drive glyph, and its name.
// Drives show how full they are under the name, and bookmarks get a remove button while hovered.
function void Prog__SidebarItem(Prog_State *state, Prog_Group group, u64 index, String name, String path, const P_Place *drive, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Place/%u/%" PRIu64 "/%S", group, index, path));
    UI_Key removeKey = UI_KeyFromStr(key, S("Remove"));
    b32 hot = UI->hotKey == key || UI->hotKey == removeKey;
    b32 current = StrIsEqual(path, state->directory.path);
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenPadding((UI_Padding){ floorf(m->line * 1.5f + m->gap * 0.5f), 0, floorf(m->gap * 0.5f), 0 });
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = current ? theme->selection : theme->alternateRow });
    UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_MiddleMouseClickable | (hot || current ? UI_Flag_DrawBackground : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(row);
    if (interaction.clicked && interaction.button == MouseButton_Left) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SidebarOpen, (u64)group, 0, interaction.clickOrder);
        command->target = index;
    }
    if (interaction.clicked && interaction.button == MouseButton_Middle) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SidebarOpenInTab, (u64)group, 0, interaction.clickOrder);
        command->target = index;
    }
    UI_Parent(row) {
        f32 slot = (f32)state->iconSize + m->gap;
        Prog_Entry folder = { .file = { .name = Prog__FolderName(path), .kind = P_FileKind_Directory } };
        const Prog_Icon *icon = drive ? Prog_PathIcon(state, path, P_FileKind_Drive) : Prog_FileIcon(state, Prog_ParentPath(path), &folder);
        if (icon) {
            UI_SetNextPrefWidth(UI_Pixels(slot));
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, (R_Color){ 1, 1, 1, 1 });
            UI_AddBlock(UI_Flag_DrawImage);
        } else {
            Prog__Icon(drive ? Icon_HardDrive : Icon_Folder, slot, m->row, theme->icon, 0, m);
        }
        R_Color color = current || hot ? theme->text : theme->textHeading;
        if (drive && drive->totalBytes) {
            // The name sits a little high to leave room for the bar under it.
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_Block *stack = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(stack) {
                f32 bar = MaxF32(2.0f, floorf(m->separator * 1.5f));
                f32 width = floorf(MinF32(R_TextWidth(m->font, name), m->sidebar * 0.55f));
                f32 used = floorf(width * (f32)(drive->totalBytes - Min(drive->freeBytes, drive->totalBytes)) / (f32)drive->totalBytes);
                f32 top = floorf((m->row - m->line - bar * 2) * 0.5f);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, top });
                UI_SetNextPrefHeight(UI_Pixels(m->line));
                UI_SetNextColors((UI_Colors){ .text = color });
                UI_SetNextText(name);
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, top + m->line + bar });
                UI_SetNextPrefWidth(UI_Pixels(width));
                UI_SetNextPrefHeight(UI_Pixels(bar));
                UI_SetNextColors((UI_Colors){ .background = theme->hover });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, top + m->line + bar });
                UI_SetNextPrefWidth(UI_Pixels(used));
                UI_SetNextPrefHeight(UI_Pixels(bar));
                UI_SetNextColors((UI_Colors){ .background = theme->textMatch });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        } else {
            Prog__Label(name, UI_Flex(), m->row, 0, color, 0);
        }
        if (group == Prog_Group_Bookmarks && hot) {
            b32 removeHot = UI->hotKey == removeKey;
            f32 size = floorf(m->line * 1.3f);
            UI_SetNextPrefWidth(UI_Pixels(size));
            UI_SetNextPrefHeight(UI_Pixels(size));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(m->radius);
            UI_SetNextFont(m->icons);
            UI_SetNextColors((UI_Colors){ .background = theme->hover, .text = removeHot ? theme->text : theme->icon });
            UI_SetNextText(Icon_X);
            UI_Block *remove = UI_AddBlockFromKey(removeKey, UI_Flag_DrawText | UI_Flag_LeftMouseClickable | (removeHot ? UI_Flag_DrawBackground : 0));
            UI_Interaction removing = UI_InteractionFromBlock(remove);
            if (removing.clicked) Prog_QueueCommand(state, Prog_CommandKind_RemoveBookmark, index, 0, removing.clickOrder);
        }
    }
}

// The box for a group's items, which eases open and shut to the height its items took last frame (none on the first frame it opens); null while it is shut, when the items need no blocks.
function UI_Block *Prog__GroupBody(Prog_State *state, Prog_Group group)
{
    b32 open = Prog_GroupOpen(state, group);
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "GroupBody/%u", (u32)group));
    f32 shown = UI_Animate(UI_KeyFromStr(key, S("Shown")), open ? 1.0f : 0.0f, open ? 1.0f : 0.0f);
    if (!open && shown <= 0) return NULL;
    UI_Block *last = UI__Find(key);
    f32 full = last ? last->contentSize.y : 0;
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(shown >= 1 ? UI_Fill() : UI_Pixels(floorf(full * shown + 0.5f)));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    return UI_AddBlockFromKey(key, UI_Flag_Clip);
}

// Quick Access: recently shown folders, bookmarks, drives and the user's folders, each under a title that opens and closes it.
function void Prog__Sidebar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    state->placesWanted = true;
    UI_SetNextPrefWidth(UI_Pixels(m->sidebar));
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ floorf(m->gap * 0.75f), 0, floorf(m->gap * 0.75f), m->gap });
    UI_SetNextColors((UI_Colors){ .background = theme->panel });
    UI_Block *sidebar = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Sidebar")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable);
    UI_Parent(sidebar) {
        // ponytail: the filter box is a placeholder until Filter Quick Access is implemented.
        UI_Block *filter = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->panel, 0);
        UI_Parent(filter) {
            Prog__Spacer(UI_Pixels(floorf(m->gap * 0.5f)), UI_Pixels(m->bar));
            Prog__Icon(Icon_Search, 0, m->bar, theme->textDim, 0, m);
            Prog__Label(S("Filter..."), UI_Flex(), m->bar, m->gap * 0.5f, theme->textDim, 0);
        }
        Prog__GroupTitle(state, Prog_Group_Recents, S("Recents"), Icon_Clock, m);
        UI_Block *body = Prog__GroupBody(state, Prog_Group_Recents);
        if (body) UI_Parent(body) {
            for (u64 i = 0; i < state->recents.header.count; ++i) {
                String path = state->recents.v[i];
                Prog__SidebarItem(state, Prog_Group_Recents, i, Prog__FolderName(path), path, NULL, m);
            }
        }
        Prog__GroupTitle(state, Prog_Group_Bookmarks, S("Bookmarks"), Icon_Bookmark, m);
        // A drag over the section pins its folders between the bookmarks the pointer is between, marked by a line there; the rects are the last layout's.
        R_Rect title = sidebar->last->rect, zone = title;
        u64 count = state->bookmarks.header.count, before = count;
        f32 lineY = title.y1;
        body = Prog__GroupBody(state, Prog_Group_Bookmarks);
        if (body) UI_Parent(body) {
            for (u64 i = 0; i < count; ++i) {
                String path = state->bookmarks.v[i];
                Prog__SidebarItem(state, Prog_Group_Bookmarks, i, Prog__FolderName(path), path, NULL, m);
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
        if (P_DropPending() && UI->mouse.x >= sidebar->rect.x0 && UI->mouse.x < sidebar->rect.x1 && UI->mouse.y >= zone.y0 && UI->mouse.y < zone.y1) {
            state->bookmarkDropShown = before + 1;
            f32 thickness = MaxF32(2, floorf(2 * m->scale));
            UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf(lineY - thickness * 0.5f) - sidebar->rect.y0 });
            UI_SetNextPrefWidth(UI_Pixels(sidebar->rect.x1 - sidebar->rect.x0 - 2 * floorf(m->gap * 0.75f)));
            UI_SetNextPrefHeight(UI_Pixels(thickness));
            UI_SetNextColors((UI_Colors){ .background = theme->accent });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            // What letting go does, beside the pointer; the drag image covers the pointer itself.
            String hint = S("Pin to Bookmarks");
            f32 hintWidth = R_TextWidth(m->font, hint) + 2 * m->inset, hintHeight = m->row;
            UI_SetNextFloatingOffset((UI_Vec2){ floorf(MinF32(UI->mouse.x - sidebar->rect.x0 + m->inset * 2, sidebar->rect.x1 - sidebar->rect.x0 - hintWidth - m->gap)),
                                                floorf(lineY - sidebar->rect.y0 + m->gap) });
            UI_SetNextPrefWidth(UI_Pixels(hintWidth));
            UI_SetNextPrefHeight(UI_Pixels(hintHeight));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(m->radius * 2);
            UI_SetNextBorderThickness(m->separator);
            UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->accent, .text = theme->text });
            UI_SetNextText(hint);
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText);
        }
        Prog__GroupTitle(state, Prog_Group_Storage, S("Storage"), (String){0}, m);
        body = Prog__GroupBody(state, Prog_Group_Storage);
        if (body) UI_Parent(body) {
            for (u64 i = 0; i < state->drives.header.count; ++i) {
                const P_Place *drive = &state->drives.v[i];
                Prog__SidebarItem(state, Prog_Group_Storage, i, drive->name, drive->path, drive, m);
            }
        }
        Prog__GroupTitle(state, Prog_Group_Places, S("Places"), Icon_Folder, m);
        body = Prog__GroupBody(state, Prog_Group_Places);
        if (body) UI_Parent(body) {
            for (u64 i = 0; i < state->places.header.count; ++i) {
                const P_Place *place = &state->places.v[i];
                Prog__SidebarItem(state, Prog_Group_Places, i, place->name, place->path, NULL, m);
            }
        }
    }
}

typedef struct Prog_MenuPlace {
    u32 first, count;
    f32 x, y;
    f32 openerWidth;  // width of the panel that opened this one; 0 for the top level
} Prog_MenuPlace;

// One menu level as a floating panel, kept inside the window.
// Sets `child` when one of its items has its submenu open.
function b32 Prog__MenuPanel(Prog_State *state, u32 level, Prog_MenuPlace place, const Prog_Metrics *m, Prog_MenuPlace *child)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    u32 *visible = ArenaPushArray(&state->frameArena, Max(place.count, 1u), u32);
    u32 shown = Prog_MenuVisible(&state->menu, place.first, place.count, visible);
    if (!shown) return false;
    // Rows inset from the panel edge, an icon column that every label lines up after, and hairline separators.
    f32 itemHeight = m->row, line = m->separator, separatorHeight = line, pad = floorf(6.0f * m->scale + 0.5f);
    // Filey's own menus show each hotkey in a chip and toggles as a box at the end.
    f32 chipPad = floorf(6.0f * m->scale + 0.5f), chipGap = floorf(4.0f * m->scale + 0.5f), chipHeight = floorf(m->line * 1.35f + 0.5f);
    f32 box = floorf(18.0f * m->scale + 0.5f);
    String *chips = state->menuCommands ? ArenaPushArray(&state->frameArena, Max(shown, 1u) * 3, String) : NULL;
    u32 *chipCounts = state->menuCommands ? ArenaPushArray(&state->frameArena, Max(shown, 1u), u32) : NULL;
    f32 iconLeft = floorf(7.0f * m->scale + 0.5f), iconColumn = (f32)state->iconSize + floorf(8.0f * m->scale + 0.5f);
    f32 labelWidth = 0, hotkeyWidth = 0, height = 2 * pad;
    b32 anySubmenu = false;
    for (u32 i = 0; i < shown; ++i) {
        const P_MenuItem *item = &state->menu.items[visible[i]];
        height += item->separator ? separatorHeight : itemHeight;
        labelWidth = MaxF32(labelWidth, R_TextWidth(m->font, item->label));
        anySubmenu |= item->childCount != 0;
        if (state->menuCommands && !item->separator) {
            const Prog_MenuCommand *command = &state->menuCommands[visible[i]];
            chipCounts[i] = Prog_HotkeyTexts(&state->frameArena, state, (Prog_CommandKind)command->kind, chips + i * 3, 3);
            f32 end = command->toggle ? box + m->gap : 0;
            for (u32 c = 0; c < chipCounts[i]; ++c) end += R_TextWidth(m->detail, chips[i * 3 + c]) + 2 * chipPad + chipGap;
            hotkeyWidth = MaxF32(hotkeyWidth, end + m->inset);
        }
    }
    f32 arrow = anySubmenu ? m->line * 1.5f : 0;
    f32 width = floorf(MinF32(MaxF32(labelWidth + hotkeyWidth + iconLeft + iconColumn + m->inset + arrow + 2 * pad, m->line * 12), m->line * 36));
    R_Rect view = UI->root->rect;
    f32 x = level == 0 && state->menuRight > 0 ? state->menuRight - width : place.x, y = place.y;
    // Submenus open to the right of their item, or to the left when there is no room.
    if (x + width > view.x1) x = place.openerWidth > 0 ? x - place.openerWidth - width + 2 * pad : view.x1 - width;
    if (y + height > view.y1) y = view.y1 - height;
    x = floorf(MaxF32(view.x0, x));
    y = floorf(MaxF32(view.y0, y));
    // A new panel fades in while its rows spread from a little closer together to their place.
    f32 reveal = UI_Animate(UI_KeyFromStr(0, StrFormat(&state->frameArena, "MenuReveal/%u/%u", level, place.first)), 1, 0);
    itemHeight *= 0.9f + 0.1f * reveal;
    height = 2 * pad;
    for (u32 i = 0; i < shown; ++i) height += state->menu.items[visible[i]].separator ? separatorHeight : itemHeight;
    f32 opacity = 0.3f + 0.7f * reveal;

    f32 radius = floorf(10.0f * m->scale + 0.5f);
    // A shadow of a few faint layers, each a little larger and lower, since the renderer has no blur.
    for (u32 i = 1; i <= 3; ++i) {
        f32 grow = (f32)i * floorf(m->scale + 0.5f);
        UI_SetNextPrefWidth(UI_Pixels(width + 2 * grow));
        UI_SetNextPrefHeight(UI_Pixels(height + 2 * grow));
        UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0 - grow, y - view.y0 });
        UI_SetNextCornerRadius(radius + grow);
        UI_SetNextColors((UI_Colors){ .background = { 0, 0, 0, 0.09f } });
        UI_SetNextOpacity(opacity);
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextOpacity(opacity);
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, y - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(1.5f * m->scale);
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    // Clickable so that clicks on the padding do not reach the catcher behind it.
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "MenuPanel/%u", level)),
                                         UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    b32 result = false;
    f32 itemY = y + pad;
    UI_Parent(panel) {
        for (u32 i = 0; i < shown; ++i) {
            u32 index = visible[i];
            const P_MenuItem *item = &state->menu.items[index];
            if (item->separator) {
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(separatorHeight));
                Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
                itemY += separatorHeight;
                continue;
            }
            b32 enabled = !item->disabled || item->childCount;
            UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Menu/%u/%u", level, index));
            // Moving the mouse onto an item decides the highlight and which submenu is open, as in native menus; the keyboard takes over until the mouse moves to another item.
            if (enabled && UI->hotKey == key && state->menuHot != key) {
                state->menuHighlight = index;
                state->menuDepth = level;
                if (item->childCount && level < CArrayCount(state->menuPath)) {
                    state->menuPath[level] = index;
                    state->menuDepth = level + 1;
                }
            }
            b32 open = level < state->menuDepth && state->menuPath[level] == index;
            b32 highlighted = enabled && (open || state->menuHighlight == index);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(itemHeight));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ iconLeft, 0, 0, 0 });
            UI_SetNextCornerRadius(m->radius * 2);
            UI_SetNextColors((UI_Colors){ .background = theme->selection });
            UI_Block *row = UI_AddBlockFromKey(key, (highlighted ? UI_Flag_DrawBackground : 0) | (enabled ? UI_Flag_LeftMouseClickable : 0));
            UI_Interaction interaction = UI_InteractionFromBlock(row);
            if (interaction.clicked && !item->childCount) {
                Prog_QueueCommand(state, Prog_CommandKind_ContextMenuInvoke, index, 0, interaction.clickOrder);
            }
            UI_Parent(row) {
                R_Color color = enabled ? theme->text : theme->textDim;
                const Prog_Icon *icon = state->menuIcons ? state->menuIcons[index] : NULL;
                const Prog_MenuCommand *command = state->menuCommands ? &state->menuCommands[index] : NULL;
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
                for (u32 c = 0; command && c < chipCounts[i]; ++c) {
                    UI_SetNextPrefWidth(UI_Pixels(R_TextWidth(m->detail, chips[i * 3 + c]) + 2 * chipPad));
                    UI_SetNextPrefHeight(UI_Pixels(itemHeight));
                    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                    UI_Block *slot = UI_AddBlock(0);
                    UI_Parent(slot) {
                        UI_SetNextPrefWidth(UI_Flex());
                        UI_SetNextPrefHeight(UI_Pixels(chipHeight));
                        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                        UI_SetNextCornerRadius(floorf(5.0f * m->scale + 0.5f));
                        UI_SetNextBorderThickness(m->scale);
                        UI_SetNextFont(m->detail);
                        UI_SetNextColors((UI_Colors){ .background = theme->track, .border = highlighted ? theme->text : theme->outline, .text = highlighted ? theme->text : theme->icon });
                        UI_SetNextText(chips[i * 3 + c]);
                        UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText);
                    }
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
                        UI_SetNextCornerRadius(floorf(4.0f * m->scale + 0.5f));
                        UI_SetNextBorderThickness(1.5f * m->scale);
                        UI_SetNextFont(m->iconsSmall);
                        UI_SetNextColors((UI_Colors){ .background = theme->selection, .border = highlighted ? theme->text : theme->tabBorder, .text = theme->text });
                        UI_SetNextText(Icon_Check);
                        UI_AddBlock(item->isDefault ? UI_Flag_DrawBackground | UI_Flag_DrawText | (highlighted ? UI_Flag_DrawBorder : 0) : UI_Flag_DrawBorder);
                    }
                }
                if (command) Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(itemHeight));
                if (item->childCount) Prog__Icon(Icon_ChevronRight, arrow, itemHeight, highlighted ? theme->text : theme->icon, 0.5f, m);
            }
            if (open) {
                *child = (Prog_MenuPlace){ item->firstChild, item->childCount, x + width - pad, itemY - pad, width };
                result = true;
            }
            itemY += itemHeight;
        }
    }
    return result;
}

// The command palette near the top of the window: the text typed over the commands it finds, with their hotkeys.
function void Prog__Palette(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->paletteOpen) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PaletteCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, outside.pressOrder);
    Prog_CommandKind matches[Prog_CommandKind_Count];
    u32 count = Prog_PaletteMatches(state, matches);
    state->paletteHighlight = count ? Min(state->paletteHighlight, count - 1) : 0;
    const u32 shownMax = 12;
    u32 first = state->paletteHighlight >= shownMax ? state->paletteHighlight - shownMax + 1 : 0;
    u32 shown = Min(count - first, shownMax);
    f32 pad = floorf(6.0f * m->scale + 0.5f), field = m->bar;
    f32 width = floorf(MinF32(m->line * 40, view.x1 - view.x0 - 2 * m->inset));
    f32 height = 2 * pad + field + (count ? pad + (f32)shown * m->row : 0);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ floorf((view.x1 - view.x0 - width) * 0.5f), floorf(m->bar * 1.6f) });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
    UI_SetNextCornerRadius(floorf(10.0f * m->scale + 0.5f));
    UI_SetNextBorderThickness(m->separator);
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->separator });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Palette")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, theme->panel, 0);
        UI_Parent(box) {
            Prog__Icon(Icon_Search, m->iconSlot, field, theme->textDim, 0.5f, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    // Placed as the edit's own text is.
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(S("Type a command"));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f), true, m);
            }
        }
        if (count) Prog__Spacer(UI_Flex(), UI_Pixels(pad));
        for (u32 i = first; i < first + shown; ++i) {
            UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Palette/%u", (u32)matches[i]));
            b32 highlighted = i == state->paletteHighlight || UI->hotKey == key;
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
            UI_SetNextCornerRadius(m->radius * 2);
            UI_SetNextColors((UI_Colors){ .background = theme->hover });
            UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | (highlighted ? UI_Flag_DrawBackground : 0));
            UI_Interaction interaction = UI_InteractionFromBlock(row);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, i, 0, interaction.clickOrder);
            UI_Parent(row) {
                Prog__Label(Prog_Commands[matches[i]].label, UI_Flex(), m->row, 0, theme->text, 0);
                String hotkey = Prog_HotkeyText(&state->frameArena, state, matches[i]);
                if (hotkey.length) Prog__Label(hotkey, UI_TextContent(), m->row, 0, theme->textDim, 1);
            }
        }
    }
}

// Copies and moves that take a while, in cards at the bottom right: what goes where, how far it is, and a question when a name is taken.
function void Prog__Transfers(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    u64 now = PlatformNowMicroseconds();
    f32 width = floorf(MinF32(m->line * 26, view.x1 - view.x0 - 2 * m->inset)), pad = m->inset * 0.75f;
    f32 bottom = view.y1 - view.y0 - m->pill - m->inset;
    for (u32 i = 0; i < Prog_MaxTransfers; ++i) {
        Prog_Transfer *transfer = &state->transfers[i];
        P_Progress *progress = &transfer->progress;
        b32 asking = transfer->job && atomic_load(&progress->asking);
        u64 items = atomic_load(&progress->totalItems), doneItems = atomic_load(&progress->doneItems);
        u64 bytes = atomic_load(&progress->totalBytes), doneBytes = atomic_load(&progress->doneBytes);
        // Windows shows its own progress, so its transfers leave the counts at zero and never show here.
        if (!transfer->job || !items || (!asking && now - transfer->started < UINT64_C(400000))) continue;
        String target = StrMake((const char *)transfer->target, transfer->targetLength);
        f32 height = 2 * pad + m->row * (asking ? 3 : 2) + (asking ? m->bar : 0);
        bottom -= height + m->gap;
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextFloatingOffset((UI_Vec2){ view.x1 - view.x0 - width - m->inset, floorf(bottom) });
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = asking ? theme->accent : theme->separator });
        UI_Block *card = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "Transfer/%u", i)),
                                            UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        UI_Parent(card) {
            UI_Block *head = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->menu, 0);
            UI_Parent(head) {
                String title = StrFormat(&state->frameArena, "%s %" PRIu64 " item%s to %S", transfer->move ? "Moving" : "Copying", items, items == 1 ? "" : "s", target);
                Prog__Label(title, UI_Flex(), m->row, 0, theme->text, 0);
                UI_Interaction cancel = Prog__TextButton(StrFormat(&state->frameArena, "TransferCancel/%u", i), Icon_X, m->icons, UI_Pixels(m->row), m->row, 0,
                                                         theme->icon, theme->text, theme->iconDisabled, true, m);
                if (cancel.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferCancel, i, 0, cancel.clickOrder);
            }
            if (asking) {
                String name = StrFromCStr(progress->conflictName);
                Prog__Label(StrFormat(&state->frameArena, "\"%S\" is already in %S", name, target), UI_Flex(), m->row, 0, theme->textHeading, 0);
                UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->menu, 0);
                UI_Parent(buttons) {
                    struct { String label; P_Conflict answer; b32 shown; } choices[] = {
                        { S("Replace"), P_Conflict_Replace, !progress->conflictFolder },
                        { S("Keep Both"), P_Conflict_KeepBoth, true },
                        { S("Skip"), P_Conflict_Skip, true },
                    };
                    for (u32 c = 0; c < CArrayCount(choices); ++c) {
                        if (!choices[c].shown) continue;
                        UI_Interaction choice = Prog__TextButton(StrFormat(&state->frameArena, "TransferAnswer/%u/%u", i, c), choices[c].label, NULL, UI_TextContent(), m->bar * 0.8f, m->inset * 0.75f,
                                                                 c == 0 ? theme->danger : theme->text, theme->textHeading, theme->textDim, true, m);
                        if (choice.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferAnswer, i, 0, choice.clickOrder)->target = (u64)choices[c].answer;
                        Prog__Spacer(UI_Pixels(m->gap * 0.5f), UI_Pixels(m->bar));
                    }
                    Prog__Spacer(UI_Flex(), UI_Pixels(m->bar));
                    UI_Interaction all = Prog__TextButton(StrFormat(&state->frameArena, "TransferAll/%u", i), transfer->answerAll ? S("For all: on") : S("For all: off"), NULL, UI_TextContent(),
                                                          m->bar * 0.8f, m->inset * 0.75f, transfer->answerAll ? theme->textMatch : theme->textDim, theme->text, theme->textDim, true, m);
                    if (all.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferAnswerAll, i, 0, all.clickOrder);
                }
            }
            // Bytes where there are any to copy; a move within one file system only counts its items.
            f32 done = bytes ? (f32)((f64)doneBytes / (f64)bytes) : (f32)((f64)doneItems / (f64)items);
            String amount = bytes ? StrFormat(&state->frameArena, "%S of %S", Prog_FormatSize(&state->frameArena, Min(doneBytes, bytes)), Prog_FormatSize(&state->frameArena, bytes))
                                  : StrFormat(&state->frameArena, "%" PRIu64 " of %" PRIu64, Min(doneItems, items), items);
            UI_Block *line = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->menu, 0);
            UI_Parent(line) {
                f32 barWidth = floorf(width * 0.55f), barHeight = MaxF32(3, floorf(4 * m->scale));
                UI_SetNextPrefWidth(UI_Pixels(barWidth));
                UI_SetNextPrefHeight(UI_Pixels(m->row));
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
                        UI_SetNextColors((UI_Colors){ .background = theme->accent });
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    }
                }
                Prog__Label(amount, UI_Flex(), m->row, m->gap, theme->textDim, 1);
            }
        }
    }
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
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("DeleteCatcher")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_MouseClickable);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_DeleteCancel, 0, 0, outside.pressOrder);
    String title = state->confirmCount == 1 ? StrFormat(&state->frameArena, "Permanently delete \"%S\"?", Prog__FolderName(state->confirmPaths[0]))
                                            : StrFormat(&state->frameArena, "Permanently delete %" PRIu64 " items?", state->confirmCount);
    f32 width = floorf(MinF32(MaxF32(R_TextWidth(m->font, title) + 2 * m->inset, m->line * 20), view.x1 - view.x0 - 2 * m->inset));
    f32 height = floorf(m->row * 2 + m->bar + m->inset * 2);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ floorf((view.x1 - view.x0 - width) * 0.5f), floorf((view.y1 - view.y0 - height) * 0.4f) });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, m->inset, m->inset, m->inset });
    UI_SetNextCornerRadius(m->radius * 2);
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->hover });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("DeletePanel")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        Prog__Label(title, UI_Flex(), m->row, 0, theme->text, 0);
        Prog__Label(S("This cannot be undone."), UI_Flex(), m->row, 0, theme->textDim, 0);
        UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->menu, 0);
        UI_Parent(buttons) {
            Prog__Spacer(UI_Flex(), UI_Pixels(m->bar));
            UI_Interaction cancel = Prog__TextButton(S("DeleteCancel"), S("Cancel"), NULL, UI_TextContent(), m->bar * 0.8f, m->inset, theme->textHeading, theme->text, theme->textDim, true, m);
            if (cancel.clicked) Prog_QueueCommand(state, Prog_CommandKind_DeleteCancel, 0, 0, cancel.clickOrder);
            Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
            UI_Interaction confirm = Prog__TextButton(S("DeleteConfirm"), S("Delete"), NULL, UI_TextContent(), m->bar * 0.8f, m->inset, theme->danger, theme->danger, theme->textDim, true, m);
            if (confirm.clicked) Prog_QueueCommand(state, Prog_CommandKind_DeleteConfirm, 0, 0, confirm.clickOrder);
        }
    }
}

function void Prog__ContextMenu(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->menuOpen) return;
    // Clicks anywhere else close the menu and go no further.
    UI_SetNextPrefWidth(UI_Pixels(UI->root->rect.x1 - UI->root->rect.x0));
    UI_SetNextPrefHeight(UI_Pixels(UI->root->rect.y1 - UI->root->rect.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("MenuCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable);
    UI_Interaction interaction = UI_InteractionFromBlock(catcher);
    if (interaction.pressed) Prog_QueueCommand(state, Prog_CommandKind_ContextMenuClose, 0, 0, interaction.pressOrder);
    // Each open level is its own top-level panel, so no panel clips the next.
    Prog_MenuPlace place = { 0, state->menu.rootCount, state->menuAt.x, state->menuAt.y, 0 };
    for (u32 level = 0; level <= CArrayCount(state->menuPath); ++level) {
        Prog_MenuPlace child = {0};
        if (!Prog__MenuPanel(state, level, place, m, &child)) break;
        place = child;
    }
    state->menuHot = UI->hotKey;
}

// One drop zone at `box` (in the panel's coordinates): a dashed frame around the panel it would make, one of `parts` across or down, the `target`th filled.
// Returns whether the pointer is over it.
function b32 Prog__DropZone(R_Rect box, R_Rect panel, u32 parts, b32 across, u32 target, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 width = box.x1 - box.x0, height = box.y1 - box.y0, pad = floorf(8.0f * m->scale + 0.5f), radius = floorf(6.0f * m->scale + 0.5f);
    b32 hot = UI__Contains((R_Rect){ panel.x0 + box.x0, panel.y0 + box.y0, panel.x0 + box.x1, panel.y0 + box.y1 }, UI->mouse);
    UI_SetNextFloatingOffset((UI_Vec2){ box.x0, box.y0 });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextCornerRadius(radius * 1.5f);
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *frame = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
    UI_Parent(frame) {
        Prog__DashedOutline(width, height, radius, theme->tabBorder, m);
        f32 innerWidth = width - 2 * pad, innerHeight = height - 2 * pad;
        f32 part = floorf(((across ? innerWidth : innerHeight) - (f32)(parts - 1) * pad) / (f32)parts);
        for (u32 i = 0; i < parts; ++i) {
            R_Color fill = theme->accent;
            fill.a = i == target ? (hot ? 0.7f : 0.35f) : 0;
            UI_SetNextFloatingOffset(across ? (UI_Vec2){ pad + (f32)i * (part + pad), pad } : (UI_Vec2){ pad, pad + (f32)i * (part + pad) });
            UI_SetNextPrefWidth(UI_Pixels(across ? part : innerWidth));
            UI_SetNextPrefHeight(UI_Pixels(across ? innerHeight : part));
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(MaxF32(1, floorf(1.5f * m->scale)));
            UI_SetNextColors((UI_Colors){ .background = fill, .border = i == target ? theme->accent : theme->tabBorder });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
        }
    }
    return hot;
}

// While a tab is dragged over a panel, its drop zones: beside it on the left and right with into it between them in the middle, above it at the top and below it at the bottom.
function void Prog__DropZones(Prog_State *state, R_Rect panel, f32 top, const Prog_Metrics *m)
{
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
    for (u32 i = 0; i < CArrayCount(zones); ++i) {
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
    String action = state->tabDrop.kind == Prog_TabDrop_Window ? (copy ? S("Copy to a new window") : S("Move to a new window (Ctrl to copy)"))
                  : copy ? S("Copy") : S("Move (Ctrl to copy)");
    f32 width = floorf(m->line * 12.0f), height = floorf(m->bar * 0.85f), radius = floorf(8.0f * scale + 0.5f);
    R_Rect view = UI->root->rect;
    UI_SetNextFloatingOffset((UI_Vec2){ floorf(UI->mouse.x - view.x0 - width * 0.3f), floorf(UI->mouse.y - view.y0 + m->gap) });
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height + m->row));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_Block *ghost = UI_AddBlock(UI_Flag_Floating);
    UI_Parent(ghost) {
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextChildrenPadding((UI_Padding){ floorf(14.0f * scale), 0, m->gap, 0 });
        UI_SetNextCornerRadius(radius);
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextColors((UI_Colors){ .background = theme->panel, .border = theme->tabBorder });
        UI_Block *tab = UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder);
        UI_Parent(tab) {
            Prog_Entry folder = { .file = { .name = Prog__FolderName(path), .kind = P_FileKind_Directory } };
            const Prog_Icon *icon = path.length ? Prog_FileIcon(state, Prog_ParentPath(path), &folder) : NULL;
            if (icon) {
                UI_SetNextPrefWidth(UI_Pixels((f32)state->iconSize + m->gap));
                UI_SetNextPrefHeight(UI_Pixels(height));
                UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
                UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, (R_Color){ 1, 1, 1, 1 });
                UI_AddBlock(UI_Flag_DrawImage);
            } else {
                Prog__Icon(Icon_Folder, (f32)state->iconSize + m->gap, height, theme->icon, 0, m);
            }
            Prog__Label(Prog__FolderName(path), UI_Flex(), height, 0, theme->text, 0);
        }
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(m->row));
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->tabBorder, .text = theme->text });
        UI_SetNextText(action);
        UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText);
    }
}

// The preview pane, `width` wide with its divider: the focused item's picture or text over its name and details.
function void Prog__PreviewPane(Prog_State *state, f32 width, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog__Box(UI_Pixels(m->separator), UI_Flex(), UI_Layout_Horizontal, theme->tabBorder, 0);
    f32 inner = width - m->separator, room = inner - 2 * m->inset;
    // Keyed, so that their last layouts tell how much fits.
    UI_SetNextPrefWidth(UI_Pixels(inner));
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, m->inset, m->inset, m->inset });
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *pane = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PreviewPane")), UI_Flag_DrawBackground | UI_Flag_Clip);
    UI_Parent(pane) {
        const Prog_Entry *entry = !state->loading && state->hasFocus && state->focusedRow < state->directory.count ? &state->directory.entries[state->focusedRow] : NULL;
        if (!entry) {
            Prog__Label(S("Nothing to preview"), UI_Flex(), m->row, 0, theme->textDim, 0.5f);
        } else {
            // A picture as wide as the pane, but no taller than half of it.
            f32 side = floorf(MinF32(room, MaxF32(0, (pane->rect.y1 - pane->rect.y0) * 0.5f)));
            if (state->previewPicture && side > 0) {
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(side));
                UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
                UI_SetNextImage(state->previewTexture, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ side, side }, (R_Color){ 1, 1, 1, 1 });
                UI_AddBlock(UI_Flag_DrawImage);
                Prog__Spacer(UI_Flex(), UI_Pixels(m->inset));
            }
            Prog__Label(entry->file.name, UI_Flex(), m->row, 0, theme->textHeading, 0);
            String details = entry->file.kind == P_FileKind_Directory
                ? (entry->measured ? StrFormat(&state->frameArena, "%S, %" PRIu64 " folders, %" PRIu64 " files, %S", entry->type, entry->folders, entry->files, Prog_FormatSize(&state->frameArena, entry->bytes)) : entry->type)
                : StrFormat(&state->frameArena, "%S, %S", entry->type, Prog_FormatSize(&state->frameArena, entry->file.size));
            Prog__Label(details, UI_Flex(), m->row, 0, theme->textDim, 0);
            Prog__Label(entry->modified, UI_Flex(), m->row, 0, theme->textDim, 0);
            // The start of a text file, one line per row, as many as fit; tabs show as spaces.
            if (state->previewText.length && !state->previewPicture) {
                Prog__Spacer(UI_Flex(), UI_Pixels(m->inset));
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Flex());
                UI_SetNextChildrenLayout(UI_Layout_Vertical);
                UI_SetNextChildrenPadding((UI_Padding){ m->gap, m->gap, m->gap, m->gap });
                UI_SetNextCornerRadius(m->radius * 2);
                UI_SetNextColors((UI_Colors){ .background = theme->panel });
                UI_Block *page = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PreviewText")), UI_Flag_DrawBackground | UI_Flag_Clip);
                UI_Parent(page) {
                    f32 lineHeight = floorf(m->line * 1.25f);
                    u32 lines = (u32)MaxF32(1, (page->rect.y1 - page->rect.y0) / lineHeight + 1);
                    String text = state->previewText;
                    for (u64 at = 0; at < text.length && lines; --lines) {
                        Index newline = StrIndexChar(StrFrom(text, at), '\n', SSF_CountOnFail);
                        String line = StrFromTo(text, at, (i64)(at + newline.value));
                        at += newline.value + 1;
                        if (line.length && line.value[line.length - 1] == '\r') --line.length;
                        StrBuilder shown = {0};
                        for (u64 i = 0; i < line.length; ++i) {
                            if (line.value[i] == '\t') StrBuildStr(&state->frameArena, &shown, S("    "));
                            else StrBuildChar(&state->frameArena, &shown, line.value[i]);
                        }
                        Prog__Label(shown.buffer, UI_Flex(), lineHeight, 0, theme->text, 0);
                    }
                }
            }
        }
    }
}

// The key seed of panel `id`'s blocks; a nonzero parent keeps it from depending on the seed in use.
function UI_Key Prog_PanelKeySeed(u64 id)
{
    return UI_KeyFromStr(1, StrMake((const char *)&id, sizeof(id)));
}

// One panel at `rect`: its tab strip over its toolbar, column header and list; only the active panel has the filter and status bar, and the others are dimmed.
function void Prog__PanelUI(Prog_State *state, u32 index, R_Rect rect, R_Rect area, const Prog_Column *columns, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog__UsePanel(state, index);
    // Keys of blocks without a parent start from the panel's, so that panels showing the same folder keep apart.
    UI->keySeed = Prog_PanelKeySeed(state->panels[index].id);
    state->panelRect = rect;
    if (UI__Contains(rect, UI->mouse)) state->pointerPanel = state->panels[index].id;
    f32 width = rect.x1 - rect.x0, height = rect.y1 - rect.y0;
    b32 top = rect.y0 <= area.y0, active = index == state->activePanel;
    // The active panel's list makes room for the preview pane.
    f32 previewWidth = active && state->previewShown ? floorf(MinF32(width * 0.4f, m->line * 30)) : 0;
    f32 listWidth = width - previewWidth;
    Prog_Column panelColumns[Prog_ColumnId_Count];
    Prog__Columns(panelColumns, scale, listWidth - 2 * m->inset);
    columns = panelColumns;
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_Block *panel = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(panel) {
        Prog__TabStrip(state, width, top, top && state->sidebarHidden && rect.x0 <= area.x0, top && rect.x1 >= area.x1, scale, m);
        UI_Block *main = Prog__Box(UI_Flex(), UI_Flex(), UI_Layout_Vertical, theme->background, 0);
        UI_Parent(main) {
            Prog__Toolbar(state, m);
            UI_Block *body = Prog__Box(UI_Flex(), UI_Flex(), UI_Layout_Horizontal, theme->background, 0);
            UI_Parent(body) {
                UI_Block *listColumn = Prog__Box(UI_Pixels(listWidth), UI_Flex(), UI_Layout_Vertical, theme->background, 0);
                UI_Parent(listColumn) {
                    if (Prog_ZoomView(state->zoom) == Prog_View_Details) {
                        Prog__Header(state, columns, m);
                        Prog__Box(UI_Flex(), UI_Pixels(floorf(4.0f * scale + 0.5f)), UI_Layout_Horizontal, theme->background, 0);
                    }
                    Prog__List(state, columns, m);
                }
                if (previewWidth > 0) Prog__PreviewPane(state, previewWidth, m);
            }
            if (active) Prog__StatusBar(state, m);
            if (!active) {
                UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                UI_SetNextPrefWidth(UI_Pixels(width));
                UI_SetNextPrefHeight(UI_Pixels(MaxF32(0, height - m->bar)));
                R_Color dim = theme->background;
                dim.a = 0.4f;
                UI_SetNextColors((UI_Colors){ .background = dim });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
            // Over the dimming, so that an inactive panel's zones show as clearly.
            if (state->tabDrag && UI__Contains((R_Rect){ rect.x0, rect.y0 + m->bar, rect.x1, rect.y1 }, UI->mouse)) {
                Prog__DropZones(state, (R_Rect){ rect.x0, rect.y0 + m->bar, rect.x1, rect.y1 }, 0, m);
            }
        }
    }
}

// The layout tree below `node` filling `rect`, its splits parted by dividers that drag to resize; `area` is the whole tree's rect.
function void Prog__Layout(Prog_State *state, u32 node, R_Rect rect, R_Rect area, const Prog_Column *columns, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const Prog_Split *split = &state->splits[node];
    if (split->kind == Prog_SplitKind_Panel) {
        u32 index = Prog_PanelIndex(state, split->panel);
        if (index != UINT32_MAX) Prog__PanelUI(state, index, rect, area, columns, scale, m);
        return;
    }
    if (split->kind != Prog_SplitKind_Horizontal && split->kind != Prog_SplitKind_Vertical) return;
    b32 across = split->kind == Prog_SplitKind_Horizontal;
    f32 length = across ? rect.x1 - rect.x0 : rect.y1 - rect.y0, line = m->separator;
    f32 first = floorf(MaxF32(0, length - line) * (split->ratio > 0 ? split->ratio : 0.5f));
    R_Rect a = rect, b = rect;
    if (across) { a.x1 = rect.x0 + first; b.x0 = a.x1 + line; }
    else        { a.y1 = rect.y0 + first; b.y0 = a.y1 + line; }
    UI_SetNextPrefWidth(UI_Pixels(rect.x1 - rect.x0));
    UI_SetNextPrefHeight(UI_Pixels(rect.y1 - rect.y0));
    UI_SetNextChildrenLayout(across ? UI_Layout_Horizontal : UI_Layout_Vertical);
    UI_Block *box = UI_AddBlock(0);
    UI_Parent(box) {
        Prog__Layout(state, split->first, a, area, columns, scale, m);
        // Keys here are the window's, not the last built panel's.
        UI->keySeed = 0;
        UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Divider/%u", node));
        UI_SetNextPrefWidth(UI_Pixels(across ? line : rect.x1 - rect.x0));
        UI_SetNextPrefHeight(UI_Pixels(across ? rect.y1 - rect.y0 : line));
        UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
        UI_Block *divider = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_LeftMouseClickable);
        UI_Interaction drag = UI_InteractionFromBlock(divider);
        if (drag.active && drag.isDragging && length > line) {
            f32 at = (across ? UI->mouse.x - rect.x0 : UI->mouse.y - rect.y0) / (length - line);
            Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_ResizeSplit, node, 0, 0);
            command->target = (u64)(UI__Clamp(at, 0, 1) * 1e6f);
            command->panel = 0;
        }
        Prog__Layout(state, split->second, b, area, columns, scale, m);
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
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, MaxF32(0, m->inset - floorf(6.0f * scale + 0.5f)), 0 });
        UI_Block *bar = Prog__Box(UI_Pixels(width), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->caption, 0);
        bar->props.childrenAlignment = (UI_Vec2){ 0, 0.5f };
        UI_Parent(bar) {
            Prog__Caption(state, S("SidebarCaption"), width, m->bar, 0);
            Prog__StripLine(width, m->bar, m);
            Prog__Spacer(UI_Pixels(floorf(10.0f * scale + 0.5f)), UI_Pixels(m->bar));
            Prog__MenuButton(state, S("MainMenu"), Icon_Menu, Prog_CommandKind_MainMenu, true, m);
            Prog__Spacer(UI_Flex(), UI_Pixels(m->bar));
            Prog__SidebarToggle(state, button, m);
        }
        UI_Block *body = Prog__Box(UI_Pixels(width), UI_Flex(), UI_Layout_Horizontal, theme->background, 0);
        UI_Parent(body) {
            Prog__Sidebar(state, m);
            Prog__Box(UI_Pixels(m->separator), UI_Flex(), UI_Layout_Horizontal, theme->tabBorder, 0);
        }
    }
}

function void Prog_BuildUI(Prog_State *state, const R_Font *font, const R_Font *icons, f32 scale)
{
    state->hasHoverRow = false;
    state->bookmarkDropShown = 0;
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
    metrics.detail = state->smallFont ? state->smallFont : font;
    metrics.iconsSmall = state->smallIcons ? state->smallIcons : icons;
    i32 iconSize = (i32)floorf(16.0f * scale + 0.5f);
    if (iconSize != state->iconSize) Prog__ResetIcons(state, iconSize);
    const Prog_Metrics *m = &metrics;
    Prog_Column columns[Prog_ColumnId_Count];
    Prog__Columns(columns, scale, 0);

    UI->root->props.childrenPadding = (UI_Padding){0};
    UI->root->props.childrenLayout = UI_Layout_Horizontal;
    R_Rect view = UI->root->rect;
    f32 sidebar = state->sidebarHidden ? 0 : m->sidebar + m->separator;
    if (sidebar > 0) Prog__SidebarColumn(state, sidebar, view.y1 - view.y0, scale, m);
    R_Rect area = { view.x0 + sidebar, view.y0, view.x1, view.y1 };
    Prog__Layout(state, 0, area, area, columns, scale, m);
    UI->keySeed = 0;
    Prog__UsePanel(state, state->activePanel);
    if (state->tabDrag) {
        if (!UI__Contains(view, UI->mouse)) state->tabDrop.kind = Prog_TabDrop_Window;
        Prog__TabGhost(state, scale, m);
    }
    Prog__Transfers(state, m);
    Prog__ContextMenu(state, m);
    Prog__Palette(state, m);
    Prog__DeleteConfirmation(state, m);
}
