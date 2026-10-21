// Browser UI.
// Sizes derive from the font's line height so the layout keeps its proportions at any font size and display scale.

#include <math.h>

#include "Meta/Icons.meta.h"

#define Prog_Gray(v)     { (v) / 255.0f, (v) / 255.0f, (v) / 255.0f, 1 }
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
    // Grays whose brightness is an amount: how much of the selection color washes an item under the pointer, and how much shows of a
    // hidden item and of one that would do nothing now.
    R_Color hoverAmount;
    R_Color hiddenAmount;
    R_Color disabledAmount;
} Prog_Theme;

#define Prog_Hex(v) { (((v) >> 16) & 255) / 255.0f, (((v) >> 8) & 255) / 255.0f, ((v) & 255) / 255.0f, 1 }

// The neutral default scheme, as Prog__ThemeFromScheme makes it from its colors.
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
    .textHeading              = Prog_Hex(0xA2A2A2),
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
    .hoverAmount              = Prog_Hex(0x595959),
    .hiddenAmount             = Prog_Hex(0x595959),
    .disabledAmount           = Prog_Hex(0x595959),
};

#define Prog_White(a) { 1, 1, 1, (a) }

// The custom scheme over a see-through window: the window is one translucent layer and the surfaces on it are faint white tints, so they do not stack into an opaque color.
global const Prog_Theme Prog_ThemeGlass = {
    .window       = { 25 / 255.0f, 25 / 255.0f, 25 / 255.0f, 0.55f },
    .background   = { 0, 0, 0, 0 },
    .inner        = Prog_White(0.02f),
    // Clear like the panels, as the shown tab cannot hide a tint under it the way an opaque tab does, and would show lighter than its panel.
    .caption      = { 0, 0, 0, 0 },
    // Menus, the palette and the other floating boxes blur what is under them, so their tint lets it show through softened.
    .menu         = { 31 / 255.0f, 31 / 255.0f, 31 / 255.0f, 0.62f },
    .panel        = Prog_White(0.025f),
    .alternateRow = Prog_White(0.012f),
    .separator    = Prog_White(0.03f),
    .hover        = Prog_White(0.2f),
    .selection    = Prog_White(0.2f),
    .focus        = Prog_White(0.22f),
    .tabBorder    = Prog_White(0.04f),
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
    .file         = Prog_Rgb(221, 222, 224),
    .folder       = Prog_Rgb(221, 222, 224),
    .contentHover = Prog_Rgb(221, 222, 224),
    .contentSelection = Prog_Rgb(221, 222, 224),
    .contentDisabledSelection = Prog_Gray(164),
    .outlineHover = Prog_White(0.3f),
    .outlineSelection = Prog_White(0.5f),
    .outlineDisabledSelection = Prog_White(0.15f),
    .matchHover   = Prog_Rgb(0, 185, 255),
    .matchSelection = Prog_Rgb(0, 170, 255),
    .matchDisabledSelection = Prog_Rgb(68, 184, 227),
    .hoverAmount  = Prog_Gray(89),
    .hiddenAmount = Prog_Gray(89),
    .disabledAmount = Prog_Gray(89),
};

// A distinct loud color for every role, so that screenshots show which surface draws where.
global const Prog_Theme Prog_ThemeDebug = {
    .window       = Prog_Rgb(60, 0, 60),
    .background   = Prog_Rgb(10, 25, 80),
    .inner        = Prog_Rgb(40, 0, 80),
    .caption      = Prog_Rgb(80, 40, 0),
    .menu         = Prog_Rgb(0, 70, 70),
    .panel        = Prog_Rgb(10, 70, 20),
    .alternateRow = Prog_Rgb(90, 80, 0),
    .separator    = Prog_Rgb(255, 128, 0),
    .hover        = Prog_Rgb(200, 0, 200),
    .selection    = Prog_Rgb(150, 20, 20),
    .focus        = Prog_Rgb(255, 255, 0),
    .tabBorder    = Prog_Rgb(0, 255, 255),
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
    .file         = Prog_Rgb(120, 255, 120),
    .folder       = Prog_Rgb(120, 160, 255),
    .contentHover = Prog_Rgb(255, 120, 255),
    .contentSelection = Prog_Rgb(255, 255, 120),
    .contentDisabledSelection = Prog_Rgb(180, 180, 90),
    .outlineHover = Prog_Rgb(0, 255, 160),
    .outlineSelection = Prog_Rgb(255, 160, 0),
    .outlineDisabledSelection = Prog_Rgb(120, 80, 0),
    .matchHover   = Prog_Rgb(255, 0, 128),
    .matchSelection = Prog_Rgb(128, 255, 0),
    .matchDisabledSelection = Prog_Rgb(90, 160, 40),
    .hoverAmount  = Prog_Gray(89),
    .hiddenAmount = Prog_Gray(89),
    .disabledAmount = Prog_Gray(89),
};

// The built-in schemes' colors, in Prog_OptionSchemes' order, each by Prog_Option_ColorClear's order of the groups.
global const u32 Prog_SchemeColors[Prog_BuiltInSchemes][Prog_ColorGroupCount] = {
    { 0x191919, 0x1F1F1F, 0x191919, 0x1F1F1F, 0x383C3D, 0x191919, 0x383838, 0x1F1F1F, 0x1C1C1C, 0x1C1C1C, 0x9A9A9A, 0xDDDEE0, 0x6B7078, 0x999999, 0xDDDEE0, 0xDDDEE0, 0xFF6358, 0x0079A6, 0x505050, 0x00ADEE, 0x00AAFF, 0x595959, 0x595959, 0x595959, 0xDDDEE0, 0xDDDEE0, 0xA4A5A6, 0x757C80, 0xA5AEB3, 0x464A4D, 0x00B9FF, 0x00AAFF, 0x44B8E3 },
    { 0xFFFFFF, 0xEBEBEB, 0xFFFFFF, 0xF8F8F8, 0xDADBDE, 0xFFFFFF, 0xD8D8D9, 0xC6C6C7, 0xF7F7F7, 0xFBFBFB, 0x909293, 0x3E3F40, 0x8A8A8C, 0x828485, 0x3E3F40, 0x3E3F40, 0xFF6666, 0x00ADEE, 0x00ADEE, 0x0093CC, 0x0078CC, 0x808080, 0x666666, 0x666666, 0x1A6480, 0xFFFFFF, 0xFFFFFF, 0xB3B3B3, 0xE6E6E6, 0xE0E0E3, 0x0050FF, 0x0050FF, 0x7397E6 },
    { 0xFDF8E9, 0xE6DEC9, 0xFDF6E3, 0xEEE8D5, 0xD6D1BF, 0xF7F1DD, 0xCCC8BC, 0xC4C0B5, 0xF2ECD9, 0xF7F1DE, 0x8F8B84, 0x595854, 0x8F897E, 0x8F8B84, 0x595854, 0x595854, 0xFF6666, 0x00ADEE, 0x00ADEE, 0x0093CC, 0x0065EE, 0x999999, 0x737373, 0x8C8C8C, 0x155066, 0xF7F7F7, 0xFFFFFF, 0xB3AFA5, 0xE0E0E0, 0xD9D9D9, 0x0050FF, 0x0050FF, 0x7397E6 },
    { 0x313135, 0x44454A, 0x37383C, 0x3D3E42, 0x585A60, 0x36363B, 0x515257, 0x6B6C73, 0x3D3E42, 0x3A3C40, 0xABACAF, 0xE7E8EB, 0x959599, 0xABACAF, 0xE7E8EB, 0xE7E8EB, 0xFF6666, 0x0079A6, 0x0079A6, 0x00ADEE, 0x00ADEE, 0x333333, 0x757575, 0x4D4D4D, 0xE7E8EB, 0xF8F9FB, 0xC9CACC, 0x82848C, 0xB8B8B8, 0x6B6C73, 0x00B9FF, 0x00B9FF, 0x009DD9 },
    { 0x151515, 0x272A2B, 0x191B1C, 0x1F2223, 0x383C3D, 0x1B1B1C, 0x343A3B, 0x464A4D, 0x202324, 0x1D1F21, 0x9A9C9D, 0xDDDEE0, 0x6B7078, 0x9A9C9D, 0xDDDEE0, 0xDDDEE0, 0xFF6666, 0x0079A6, 0x0079A6, 0x00ADEE, 0x00ADEE, 0x4D4D4D, 0x595959, 0x595959, 0xDDDEE0, 0xDDDEE0, 0xA4A5A6, 0x757C80, 0xA5AEB3, 0x464A4D, 0x00B9FF, 0x00AAFF, 0x44B8E3 },
    { 0x002730, 0x003C4D, 0x002B36, 0x003442, 0x004E63, 0x002F3B, 0x265561, 0x2D6270, 0x003340, 0x002F3B, 0x9DAFB4, 0xF2F2F2, 0x6B8F99, 0x9DAFB4, 0xF2F2F2, 0xF2F2F2, 0xBF616A, 0x0079A6, 0x0079A6, 0x00ADEE, 0x00ADEE, 0x404040, 0x666666, 0x4D4D4D, 0xF2F2F2, 0xF2F2F2, 0xCCCCCC, 0x387B8C, 0xB3B3B3, 0x2D6270, 0x00B9FF, 0x00B9FF, 0x009DD9 },
    { 0x002047, 0x001D42, 0x002451, 0x00204A, 0x1F4370, 0x001F47, 0x2D4462, 0x365275, 0x00295C, 0x002757, 0xA1ABB7, 0xE7E7E7, 0x6B7F99, 0xA1ABB7, 0xE7E7E7, 0xE7E7E7, 0xFF4D4D, 0x0079A6, 0x0079A6, 0x00ADEE, 0x00ADEE, 0x4D4D4D, 0x666666, 0x666666, 0xE7E7E7, 0xF2F2F2, 0xBDBDBD, 0x4D73A6, 0xA6A6A6, 0x41628C, 0x00B9FF, 0x00B9FF, 0x009ED9 },
};

function R_Color Prog__HexColor(u32 hex) { return (R_Color){ (f32)((hex >> 16) & 255) / 255.0f, (f32)((hex >> 8) & 255) / 255.0f, (f32)(hex & 255) / 255.0f, 1 }; }

// `from` moved toward `to` by `amount`, from 0 to 1, or each channel scaled by `scale` when `to` is negative, to the nearest byte.
function u32 Prog__ShiftHex(u32 from, i64 to, f32 amount)
{
    u32 result = 0;
    for (u32 shift = 0; shift < 24; shift += 8) {
        f32 a = (f32)((from >> shift) & 255), b = to < 0 ? a * amount : (f32)(((u64)to >> shift) & 255), mixed = to < 0 ? b : a + (b - a) * amount;
        result |= (u32)floorf(UI__Clamp(mixed, 0, 255) + 0.5f) << shift;
    }
    return result;
}

// The theme a scheme's colors make: the popups have the surface's, the focus outline the selection's, and the disabled icons the
// icons' faded into the background by the disabled amount; the sections' titles show a little brighter than their color.
function Prog_Theme Prog__ThemeFromScheme(const u32 *groups)
{
#define Prog__Group(name) groups[Prog_Option_Color##name - Prog_Option_ColorClear]
#define Prog__GroupColor(name) Prog__HexColor(Prog__Group(name))
    Prog_Theme theme = {
        .window = Prog__GroupColor(Clear), .background = Prog__GroupColor(Background), .inner = Prog__GroupColor(Inner), .caption = Prog__GroupColor(Caption),
        .menu = Prog__GroupColor(Surface), .panel = Prog__GroupColor(Surface), .alternateRow = Prog__GroupColor(AlternatingRow), .separator = Prog__GroupColor(Separator),
        .hover = Prog__GroupColor(Hover), .selection = Prog__GroupColor(Selection), .focus = Prog__GroupColor(Selection), .tabBorder = Prog__GroupColor(Border),
        .text = Prog__GroupColor(Text), .textDim = Prog__GroupColor(Secondary), .textHeading = Prog__HexColor(Prog__ShiftHex(Prog__Group(Group), -1, 1.06f)),
        .textMatch = Prog__GroupColor(Match), .marquee = Prog__GroupColor(RectSelection), .accent = Prog__GroupColor(Progress), .track = Prog__GroupColor(Foreground),
        .outline = Prog__GroupColor(Outline), .danger = Prog__GroupColor(Warning), .icon = Prog__GroupColor(IconTint),
        .iconDisabled = Prog__HexColor(Prog__ShiftHex(Prog__Group(Background), Prog__Group(IconTint), (f32)(Prog__Group(Disabled) & 255) / 255.0f)),
        .file = Prog__GroupColor(File), .folder = Prog__GroupColor(Folder), .contentHover = Prog__GroupColor(ContentHover), .contentSelection = Prog__GroupColor(ContentSelection),
        .contentDisabledSelection = Prog__GroupColor(ContentDisabledSelection), .outlineHover = Prog__GroupColor(OutlineHover), .outlineSelection = Prog__GroupColor(OutlineSelection),
        .outlineDisabledSelection = Prog__GroupColor(OutlineDisabledSelection), .matchHover = Prog__GroupColor(MatchHover), .matchSelection = Prog__GroupColor(MatchSelection),
        .matchDisabledSelection = Prog__GroupColor(MatchDisabledSelection), .hoverAmount = Prog__GroupColor(Hover), .hiddenAmount = Prog__GroupColor(Hidden),
        .disabledAmount = Prog__GroupColor(Disabled),
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
    Prog__ThemeColor(matchDisabledSelection), Prog__ThemeColor(hoverAmount), Prog__ThemeColor(hiddenAmount), Prog__ThemeColor(disabledAmount),
#undef Prog__ThemeColor
};
static_assert(sizeof(Prog_ThemeColors) / sizeof(Prog_ThemeColors[0]) * sizeof(R_Color) == sizeof(Prog_Theme));

// The transparent theme with the settings' glass colors, and how far its floating boxes blur what is under them, in logical pixels.
global Prog_Theme Prog_ThemeGlassUser = Prog_ThemeGlass;
global f32 Prog_GlassBlur = 24;

function void Prog__ApplyTheme(Prog_State *state)
{
    Prog_Theme_ = state->debugTheme ? &Prog_ThemeDebug : state->transparent ? &Prog_ThemeGlassUser : &Prog_ThemeUser;
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

// The built-in schemes, before the custom ones.
global const String Prog_OptionSchemes[] = { SInit("default"), SInit("sirius"), SInit("pollux"), SInit("eridanus"), SInit("fullmoon"), SInit("regulus"), SInit("altair") };
static_assert(CArrayCount(Prog_OptionSchemes) == Prog_BuiltInSchemes, "the built-in schemes counted");
static_assert(Prog_Option_ColorMatchDisabledSelection - Prog_Option_ColorClear + 1 == Prog_ColorGroupCount, "the color groups counted");

// The groups' names in a custom scheme's lines.
global const char *const Prog_ColorGroupNames[Prog_ColorGroupCount] = {
    "Clear", "Caption", "Background", "Surface", "Foreground", "Inner", "Border", "Outline", "Separator", "AlternatingRow", "IconTint", "Text", "Secondary", "Group",
    "File", "Folder", "Warning", "Progress", "Selection", "RectSelection", "Match", "Hidden", "Hover", "Disabled", "ContentHover", "ContentSelection",
    "ContentDisabledSelection", "OutlineHover", "OutlineSelection", "OutlineDisabledSelection", "MatchHover", "MatchSelection", "MatchDisabledSelection",
};

// Gives `groups` the colors of the scheme named `name`: a built-in one, or a custom one's over the default scheme's; "light" is the first light
// one. Returns its place among the built-in schemes and then the custom ones, 0 for an unknown name.
function u8 Prog__UseScheme(const Prog_State *state, String name, u32 *groups)
{
    MemCopy(groups, Prog_SchemeColors[0], sizeof(Prog_SchemeColors[0]));
    if (Prog__EqualFolded(name, S("light"))) name = S("sirius");
    for (u32 i = 0; i < CArrayCount(Prog_OptionSchemes); ++i) {
        if (!Prog__EqualFolded(name, Prog_OptionSchemes[i])) continue;
        MemCopy(groups, Prog_SchemeColors[i], sizeof(Prog_SchemeColors[i]));
        return (u8)i;
    }
    for (u64 i = 0; i < state->schemeNames.header.count; ++i) {
        if (!Prog__EqualFolded(state->schemeNames.v[i], name)) continue;
        String text = state->schemeTexts.v[i];
        for (u64 at = 0; at < text.length;) {
            u64 end = at + StrIndexChar(StrFrom(text, at), '\n', SSF_CountOnFail).value;
            String line = StrFromTo(text, at, (i64)end);
            at = end + 1;
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

// A switch setting's words: on, true or 1, and off, false or 0.
function b32 Prog__SettingOn(String value) { return Prog__EqualFolded(value, S("on")) || Prog__EqualFolded(value, S("true")) || StrIsEqual(value, S("1")); }
function b32 Prog__SettingOff(String value) { return Prog__EqualFolded(value, S("off")) || Prog__EqualFolded(value, S("false")) || StrIsEqual(value, S("0")); }

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
global const String Prog_OptionStartups[Prog_Startup_Count] = { SInit("session"), SInit("root"), SInit("home") };
global const String Prog_OptionNewTabs[Prog_NewTab_Count] = { SInit("root"), SInit("home"), SInit("current") };
global const String Prog_OptionCharacterKeys[Prog_CharacterKeys_Count] = { SInit("filter"), SInit("hotkey"), SInit("select") };

// Applies settings `text`, lines of "name = value", over the defaults; unknown names and bad values are skipped, and a line starting with '#' is a comment.
//   scale = 1.25                  the UI's size beyond the display's own scale
//   theme = fullmoon              the reference's scheme under the colors below, for side-by-side checks
//   animations = false            menus, scrolling and the caret change at once
//   color.<theme field> = #rrggbb[aa]
//   glass.window = #rrggbbaa      the transparent theme's tint over what is behind the window
//   glass.popup = #rrggbbaa       its tint over the blur under menus, the palette and the other floating boxes
//   glass.opacity = 55            how much of glass.window's tint covers what is behind, in percent, where there is no backdrop to choose
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
    MemCopy(state->schemeGroups, Prog_SchemeColors[0], sizeof(state->schemeGroups));
    state->scheme = 0;
    state->lightScheme = 1;
    state->schemeName.length = state->lightSchemeName.length = 0;
    Prog_ThemeGlassUser = Prog_ThemeGlass;
    Prog_GlassBlur = 24;
    state->glassBackdrop = PlatformBackdrop_Acrylic;
    state->uiScale = 1;
    state->animationsOff = false;
    state->transparent = false;
    state->scheme = 0;
    state->systemScheme = false;
    state->spacingShift = 0;
    state->squareCorners = false;
    state->alternatingRows = false;
    state->inactiveShift = 0;
    state->actOnRelease = state->singleClick = state->singleClickFoldersOnly = state->hoverToSelect = false;
    state->clickToFocus = true;
    state->underlineAlways = state->doubleClickToParent = state->noOverscroll = state->reverseScroll = false;
    state->characterKeys = Prog_CharacterKeys_TypeInFilter;
    state->hideExtensions = state->showSystem = false;
    state->fullPaths = state->noTrail = false;
    state->thumbnailMode = Prog_Thumbnails_LocalDisk;
    state->showCheckboxes = state->noTrashConfirm = state->menuInline = state->separateInstances = false;
    state->startup = Prog_Startup_LastSession;
    state->newTab = Prog_NewTab_Root;
    state->storeRecents = Prog_StoreRecents_Persistent;
    state->showRecents = state->sortRecents = state->noVSync = false;
    state->font.length = state->inspectorFont.length = 0;
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
            Prog_ThemeGlassUser.window = color;
        } else if (StrIsEqual(name, S("glass.popup")) && Prog__ParseColor(value, &color)) {
            Prog_ThemeGlassUser.menu = color;
        } else if (StrIsEqual(name, S("glass.opacity"))) {
            if (number >= 0 && number <= 100) Prog_ThemeGlassUser.window.a = number / 100.0f;
        } else if (StrIsEqual(name, S("glass.blur"))) {
            if (number >= 0 && number <= 100) Prog_GlassBlur = number;
        } else if (StrIsEqual(name, S("glass.backdrop"))) {
            local_persist const String backdrops[PlatformBackdrop_Count] = { SInit("acrylic"), SInit("mica"), SInit("tabbed"), SInit("none") };
            for (u32 i = 0; i < PlatformBackdrop_Count; ++i) if (Prog__EqualFolded(value, backdrops[i])) state->glassBackdrop = (u8)i;
        } else if (StrIsEqual(name, S("theme"))) {
            // Colors given before the theme line are replaced with the rest.
            state->scheme = Prog__UseScheme(state, value, state->schemeGroups);
            Prog_ThemeUser = Prog__ThemeFromScheme(state->schemeGroups);
            state->schemeName.length = (u32)Min(value.length, (u64)sizeof(state->schemeName.text));
            MemCopy(state->schemeName.text, value.value, state->schemeName.length);
        } else if (StrIsEqual(name, S("theme.light"))) {
            u32 unused[Prog_ColorGroupCount];
            state->lightScheme = Prog__UseScheme(state, value, unused);
            state->lightSchemeName.length = (u32)Min(value.length, (u64)sizeof(state->lightSchemeName.text));
            MemCopy(state->lightSchemeName.text, value.value, state->lightSchemeName.length);
        } else if (StrIsEqual(name, S("theme.system"))) {
            state->systemScheme = Prog__EqualFolded(value, S("on")) || Prog__EqualFolded(value, S("true")) || StrIsEqual(value, S("1"));
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
    // The system's light mode brings the light scheme, its dark mode the one chosen.
    if (state->systemScheme && !P_PrefersDarkColors()) {
        Prog__UseScheme(state, state->lightSchemeName.length ? StrMake(state->lightSchemeName.text, state->lightSchemeName.length) : S("light"), state->schemeGroups);
        Prog_ThemeUser = Prog__ThemeFromScheme(state->schemeGroups);
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
}

// Every setting, commented out at its default, for a first edit.
function String Prog_SettingsTemplate(Arena *arena, const Prog_State *state)
{
    StrBuilder text = {0};
    StrBuildStr(arena, &text, S("# Filey settings, applied as soon as this file is saved.\n"
                                "# Lines are \"name = value\", switches true or false; remove the # in front of a setting to change it.\n\n"
                                "# The size of everything, beyond the display's own scale.\n# scale = 1\n\n"
                                "# A scheme by its name: default, sirius, pollux, eridanus, fullmoon, regulus, altair or a custom one; colors set before this line are replaced.\n# theme = default\n"
                                "# With theme.system on, the system's light mode brings theme.light instead.\n# theme.system = false\n# theme.light = sirius\n\n"
                                "# How the lists look: the space between items from 0 to 13, the corners, every other row tinted,\n"
                                "# and how much of inactive panels shows, from 0 to 100.\n# spacing = 4\n# corners = round\n# rows.alternate = false\n# inactive.opacity = 60\n\n"
                                "# The pointer: select on press or release, open with one click (folders only), select by hovering,\n"
                                "# give the keyboard to the panel it moves over, underline only the pointed name in single-click mode,\n"
                                "# go up with a double click on the background, scroll past the last item, and turn the wheel around.\n"
                                "# click.onPress = true\n# click.single = false\n# click.singleFoldersOnly = false\n# hover.select = false\n# hover.focus = false\n"
                                "# hover.underline = true\n"
                                "# click.doubleToParent = false\n# scroll.over = true\n# scroll.reverse = false\n\n"
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
                                "# What a start without folders to open shows: the last session, root (This PC) or home; what a new tab shows: root, home or current.\n"
                                "# startup = session\n# newtab = root\n\n"
                                "# Whether starting Filey, or opening a folder in it from elsewhere, brings the open window forward, the folder in a new tab, rather\n"
                                "# than opening another window.\n# instances.single = true\n\n"
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
    // The glass settings, at their defaults.
    R_Color colors[2] = { Prog_ThemeGlass.window, Prog_ThemeGlass.menu };
    String hex[2];
    for (u32 i = 0; i < 2; ++i) {
        R_Color c = colors[i];
        hex[i] = StrFormat(arena, "#%02x%02x%02x%02x", (u32)(c.r * 255 + 0.5f), (u32)(c.g * 255 + 0.5f), (u32)(c.b * 255 + 0.5f), (u32)(c.a * 255 + 0.5f));
    }
    StrBuildStr(arena, &text, StrFormat(arena, "\n# The transparent theme (the \"Transparency\" command), with colors as #rrggbbaa whose last pair is the opacity:\n"
                                               "# glass.window tints what is behind the window, glass.popup the blur under menus, the palette and the other floating boxes,\n"
                                               "# glass.opacity is the tint's opacity alone in percent, where there is no backdrop to choose,\n"
                                               "# and glass.blur is how far that blur reaches in pixels, 0 for none.\n"
                                               "# glass.backdrop is what Windows draws behind the window: acrylic (its blur), mica, tabbed or none.\n"
                                               "# transparent = false\n# glass.window = %S\n# glass.popup = %S\n# glass.blur = 24\n# glass.backdrop = acrylic\n", hex[0], hex[1]));
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
global const String Prog_OptionCorners[] = { SInit("square"), SInit("round") };
global const String Prog_OptionLayouts[] = { SInit("auto"), SInit("right"), SInit("below") };
global const String Prog_OptionThumbnails[] = { SInit("never"), SInit("local"), SInit("always") };
global const String Prog_OptionBackdrops[] = { SInit("acrylic"), SInit("mica"), SInit("tabbed"), SInit("none") };
global const Prog_OptionDesc Prog_Options[Prog_Option_Count] = {
    [Prog_Option_Font] = { SInit("Font"), SInit("The system-installed font name that is applied to the entire UI."), Prog_OptionControl_Font, 0, 0, NULL, 0, SInit("font") },
    [Prog_Option_InspectorFont] = { SInit("Inspector Font"), SInit("The system-installed font used when previewing text contents in the inspector."), Prog_OptionControl_Font, 0, 0,
                                    NULL, 0, SInit("inspector.font") },
    [Prog_Option_FontSize] = { SInit("Font Size"), SInit("Determines the scale of the whole UI."), Prog_OptionControl_Stepper, 6, 24, NULL, 0, SInit("scale") },
    [Prog_Option_InspectorFontSize] = { SInit("Inspector Font Size"), SInit("Specifies the font size used to display text contents in the inspector."), Prog_OptionControl_Stepper,
                                        Prog_InspectorFontMin, Prog_InspectorFontMax, NULL, 0, SInit("inspector.fontSize") },
    [Prog_Option_Spacing] = { SInit("Spacing"), SInit("Defines the space between items in a list."), Prog_OptionControl_Slider, 0, 13, NULL, 0, SInit("spacing") },
    [Prog_Option_Animations] = { SInit("Animations"), SInit("Applies animations to new popups, view scrolling, text cursor, etc."), Prog_OptionControl_Switch, 0, 1,
                                 Prog_OptionSwitch, 2, SInit("animations") },
    [Prog_Option_RoundCorners] = { SInit("Round Corners"), SInit("Applies rounding to the corners of UI elements such as popups, buttons, and selections."), Prog_OptionControl_Switch,
                                   0, 1, Prog_OptionCorners, 2, SInit("corners") },
    [Prog_Option_InlineSubmenus] = { SInit("Inline Submenus"), SInit("Opens a menu's submenus inside it, under their item, rather than beside it."), Prog_OptionControl_Switch, 0, 1,
                                     Prog_OptionSwitch, 2, SInit("menus.inline") },
    [Prog_Option_SystemScheme] = { SInit("System Light / Dark Scheme"), SInit("Automatically adapt between light and dark color schemes based on system settings."),
                                   Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("theme.system") },
    [Prog_Option_ColorScheme] = { SInit("Color Scheme"), SInit("Applies a color palette to all UI elements."),
                                  Prog_OptionControl_Choice, 0, 0, Prog_OptionSchemes, CArrayCount(Prog_OptionSchemes), SInit("theme") },
    [Prog_Option_LightScheme] = { SInit("Light Color Scheme"), SInit("Color theme applied when the system uses bright colors."), Prog_OptionControl_Choice, 0, 0,
                                  Prog_OptionSchemes, CArrayCount(Prog_OptionSchemes), SInit("theme.light") },
    [Prog_Option_DarkScheme] = { SInit("Dark Color Scheme"), SInit("Color theme applied when the system uses dark colors."), Prog_OptionControl_Choice, 0, 0,
                                 Prog_OptionSchemes, CArrayCount(Prog_OptionSchemes), SInit("theme") },
    [Prog_Option_ColorGroups] = { SInit("Color Scheme"), SInit("Exposes the individual components of the selected color scheme."), Prog_OptionControl_Expand, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorClear] = { SInit("Clear"), SInit("Background color visible only between panel borders."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorCaption] = { SInit("Caption"), SInit("Background color for caption bar."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorBackground] = { SInit("Background"), SInit("Background color for panels and windows."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorSurface] = { SInit("Surface"), SInit("Background color for popups, sidebar, and floating regions like the footer."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorForeground] = { SInit("Foreground"), SInit("Background color for scrollbar, dropdowns and other buttons."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorInner] = { SInit("Inner"), SInit("Background color for text field."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorBorder] = { SInit("Border"), SInit("Border color for panels, windows, and popups."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorOutline] = { SInit("Outline"), SInit("Outline color for UI elements like buttons and toggles."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorSeparator] = { SInit("Separator"), SInit("Color for line separators between item rows."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorAlternatingRow] = { SInit("Alternating Row"), SInit("Background color for alternating rows in lists and columns."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorIconTint] = { SInit("Icon"), SInit("Color of all non-system icons in the UI."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorText] = { SInit("Text"), SInit("Color of primary UI text."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorSecondary] = { SInit("Secondary"), SInit("Color for secondary text."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorGroup] = { SInit("Group"), SInit("Color for sections that can be opened or closed."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorFile] = { SInit("File"), SInit("Text color for file items."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorFolder] = { SInit("Folder"), SInit("Text color for folder items."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorWarning] = { SInit("Warning"), SInit("Background color for buttons requiring extra attention."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorProgress] = { SInit("Progress"), SInit("Fill color for slider and toggle."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorSelection] = { SInit("Selection"), SInit("Background color of selected items."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorRectSelection] = { SInit("Rect selection"), SInit("Color of the rectangle when selecting multiple items in a panel."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorMatch] = { SInit("Match"), SInit("Color for matching letters in filtered text."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorHidden] = { SInit("Hidden Opacity"), SInit("Controls the opacity of hidden/system files using the brightness value."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorHover] = { SInit("Hover Opacity"), SInit("Blends the selection with the background when hovering using the brightness value."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorDisabled] = { SInit("Disabled Opacity"), SInit("Controls the opacity of disabled items using the brightness value."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorContentHover] = { SInit("Content Hover"), SInit("Color of the content when items are hovered."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorContentSelection] = { SInit("Content Selection"), SInit("Color of the content when items are selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorContentDisabledSelection] = { SInit("Content Disabled Selection"), SInit("Color of the content when items are both disabled and selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorOutlineHover] = { SInit("Outline Hover"), SInit("Color of the outlines when items are hovered."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorOutlineSelection] = { SInit("Outline Selection"), SInit("Color of the outlines when items are selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorOutlineDisabledSelection] = { SInit("Outline Disabled Selection"), SInit("Color of the outlines when items are both disabled and selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorMatchHover] = { SInit("Match Hover"), SInit("Color for matching letters in filtered text when items are hovered."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorMatchSelection] = { SInit("Match Selection"), SInit("Color for matching letters in filtered text when items are selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_ColorMatchDisabledSelection] = { SInit("Match Disabled Selection"), SInit("Color for matching letters in filtered text when items are both disabled and selected."), Prog_OptionControl_Color, 0, 0, NULL, 0, {0} },
    [Prog_Option_AlternatingRows] = { SInit("Alternating Row Colors"), SInit("Applies alternating row colors to improve readability and distinguish adjacent rows."),
                                      Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("rows.alternate") },
    [Prog_Option_InactiveOpacity] = { SInit("Inactive Panel Contents Opacity"), SInit("Applies dimming to inactive panel contents to make the active panel stand out."), Prog_OptionControl_Slider,
                                      0, 100, NULL, 0, SInit("inactive.opacity") },
    [Prog_Option_ShowFileExtension] = { SInit("Show File Extension"), SInit("Toggle the visibility of file extensions."), Prog_OptionControl_Switch, 0, 1,
                                        Prog_OptionSwitch, 2, SInit("files.extensions") },
    [Prog_Option_ShowHiddenFiles] = { SInit("Show Hidden Files"), SInit("Toggle the visibility of hidden and dot files (e.g., .gitignore, .config)."), Prog_OptionControl_Switch, 0, 1,
                                      Prog_OptionSwitch, 2, SInit("files.hidden") },
    [Prog_Option_ShowSystemFiles] = { SInit("Show System Files"), SInit("Toggle the visibility of system files."), Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("files.system") },
    [Prog_Option_ShortenedPaths] = { SInit("Display Compact Paths"), SInit("Trim paths from the first recognized folder."), Prog_OptionControl_Switch, 0, 1,
                                     Prog_OptionSwitch, 2, SInit("paths.short") },
    [Prog_Option_Breadcrumbs] = { SInit("Display Path Breadcrumbs"), SInit("Show previously visited subfolders in the address bar."), Prog_OptionControl_Switch, 0, 1,
                                  Prog_OptionSwitch, 2, SInit("paths.trail") },
    [Prog_Option_ShowCheckboxes] = { SInit("Show Selection Boxes"), SInit("Toggle visibility of checkboxes when selecting multiple items in a panel."), Prog_OptionControl_Switch, 0, 1,
                                     Prog_OptionSwitch, 2, SInit("checkboxes") },
    [Prog_Option_ConfirmEmptyTrash] = { SInit("Confirm Emptying Recycle Bin"), SInit("Ask before permanently deleting everything in the Recycle Bin."),
                                        Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("trash.confirm") },
    [Prog_Option_StoreRecents] = { SInit("Store Recents"), SInit("Determines how recently accessed files are stored."), Prog_OptionControl_Choice, 0, 0,
                                   Prog_OptionStoreRecents, CArrayCount(Prog_OptionStoreRecents), SInit("recents.store") },
    [Prog_Option_ShowRecents] = { SInit("Highlight Recents"), SInit("Mark recently accessed files and directories."), Prog_OptionControl_Switch, 0, 1,
                                  Prog_OptionSwitch, 2, SInit("recents.highlight") },
    [Prog_Option_SortRecents] = { SInit("Sort Recents On Top"), SInit("Sort recently accessed files and directories to the top."), Prog_OptionControl_Switch, 0, 1,
                                  Prog_OptionSwitch, 2, SInit("recents.sort") },
    [Prog_Option_LoadThumbnails] = { SInit("Load Thumbnails"), SInit("Criteria for loading thumbnails of listed directories."), Prog_OptionControl_Choice, 0, 0,
                                     Prog_OptionThumbnails, CArrayCount(Prog_OptionThumbnails), SInit("thumbnails") },
    [Prog_Option_CalcFolderSizes] = { SInit("Calculate Folder Sizes"), SInit("Calculates folder sizes by scanning their contents."), Prog_OptionControl_Switch, 0, 1,
                                      Prog_OptionSwitch, 2, SInit("folders.size") },
    [Prog_Option_CalcFolderSizesSSD] = { SInit("SSDs"), SInit("Calculate folder sizes on local SSD drives."), Prog_OptionControl_Switch, 0, 1,
                                         Prog_OptionSwitch, 2, SInit("folders.size.ssd") },
    [Prog_Option_CalcFolderSizesHDD] = { SInit("HDDs"), SInit("Calculate folder sizes on local HDD drives."), Prog_OptionControl_Switch, 0, 1,
                                         Prog_OptionSwitch, 2, SInit("folders.size.hdd") },
    [Prog_Option_CalcFolderSizesNetwork] = { SInit("Network Locations"), SInit("Calculate folder sizes on network drives and shared locations."), Prog_OptionControl_Switch,
                                             0, 1, Prog_OptionSwitch, 2, SInit("folders.size.network") },
    [Prog_Option_ActOnPress] = { SInit("Act On Press"), SInit("Whether interactions register on mouse press or release."), Prog_OptionControl_Switch, 0, 1,
                                 Prog_OptionSwitch, 2, SInit("click.onPress") },
    [Prog_Option_SingleClick] = { SInit("Single-click To Open"), SInit("Open items with a single click."), Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("click.single") },
    [Prog_Option_SingleClickFoldersOnly] = { SInit("Folders Only"), SInit("Single click applies to folders only."), Prog_OptionControl_Switch, 0, 1,
                                             Prog_OptionSwitch, 2, SInit("click.singleFoldersOnly") },
    [Prog_Option_HoverToSelect] = { SInit("Point To Select"), SInit("Select items by hovering over them."), Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("hover.select") },
    [Prog_Option_HoverToFocus] = { SInit("Point To Focus Panel"), SInit("Give the keyboard to the panel the pointer moves over."), Prog_OptionControl_Switch, 0, 1,
                                   Prog_OptionSwitch, 2, SInit("hover.focus") },
    [Prog_Option_UnderlineOnHover] = { SInit("Underline On Point"), SInit("Underlines items only when pointing at them."), Prog_OptionControl_Switch, 0, 1,
                                       Prog_OptionSwitch, 2, SInit("hover.underline") },
    [Prog_Option_DoubleClickToParent] = { SInit("Double-click To Parent"), SInit("Go to parent folder by double clicking background."), Prog_OptionControl_Switch, 0, 1,
                                          Prog_OptionSwitch, 2, SInit("click.doubleToParent") },
    [Prog_Option_Overscroll] = { SInit("Scroll Beyond Last Item"), SInit("Allows scrolling past the last item for extra space."), Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("scroll.over") },
    [Prog_Option_ReverseScroll] = { SInit("Reverse Scroll Direction"), SInit("Reverses the scroll direction in scrollable views."), Prog_OptionControl_Switch, 0, 1,
                                    Prog_OptionSwitch, 2, SInit("scroll.reverse") },
    [Prog_Option_CharacterKeyAction] = { SInit("Character Key Action"), SInit("Determines the action when you press raw character keys without modifiers."),
                                         Prog_OptionControl_Choice, 0, 0, Prog_OptionCharacterKeys, Prog_CharacterKeys_Count, SInit("keys.character") },
    [Prog_Option_InspectorLayout] = { SInit("Inspector Layout"), SInit("Default layout for opening the new inspector."), Prog_OptionControl_Choice, 0, 0,
                                      Prog_OptionLayouts, CArrayCount(Prog_OptionLayouts), SInit("inspector.layout") },
    [Prog_Option_HighlightCode] = { SInit("Highlight Code"), SInit("Color keywords, strings, comments and numbers of code in the inspector."), Prog_OptionControl_Switch, 0, 1,
                                    Prog_OptionSwitch, 2, SInit("inspector.highlight") },
    [Prog_Option_LoadOnStartup] = { SInit("Load On Startup"), SInit("Previous session, location, or custom layout to load on startup."), Prog_OptionControl_Choice, 0, 0,
                                    Prog_OptionStartups, CArrayCount(Prog_OptionStartups), SInit("startup") },
    [Prog_Option_NewTabShow] = { SInit("New Tab Show"), SInit("Location to open in a new tab."), Prog_OptionControl_Choice, 0, 0,
                                 Prog_OptionNewTabs, CArrayCount(Prog_OptionNewTabs), SInit("newtab") },
    [Prog_Option_SingleWindow] = { SInit("Open In One Window"), SInit("Starting Filey or opening a folder from elsewhere uses the open window, in a new tab."),
                                   Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("instances.single") },
    [Prog_Option_VSync] = { SInit("VSync"), SInit("Reduces hardware demands during high activity, such as rapid mouse movements. This comes at the cost of slight input lag."),
                            Prog_OptionControl_Switch, 0, 1, Prog_OptionSwitch, 2, SInit("vsync") },
    [Prog_Option_Transparency] = { SInit("Transparency"), SInit("Shows what is behind the window through it, blurred."), Prog_OptionControl_Switch, 0, 1,
                                   Prog_OptionSwitch, 2, SInit("transparent") },
    [Prog_Option_WindowOpacity] = { SInit("Window Opacity"), SInit("How much the window's color covers what is behind it while the window is transparent."),
                                    Prog_OptionControl_Slider, 0, 100, NULL, 0, SInit("glass.opacity") },
    [Prog_Option_PopupBlur] = { SInit("Popup Blur"), SInit("How far menus and popups blur what is under them while the window is transparent."), Prog_OptionControl_Slider, 0, 100,
                                NULL, 0, SInit("glass.blur") },
    [Prog_Option_Backdrop] = { SInit("Backdrop"), SInit("What Windows draws behind the transparent window."), Prog_OptionControl_Choice, 0, 0,
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
    return true;
}

// The scheme shown, by its place as `scheme` counts them, and its name: with the system's choice in its light mode, the light one.
function u8 Prog_ShownScheme(const Prog_State *state, String *name)
{
    b32 light = state->systemScheme && !P_PrefersDarkColors();
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
        // With the system's choice, a scheme for each of its modes.
        if (i == Prog_Option_ColorScheme && state->systemScheme) continue;
        if ((i == Prog_Option_LightScheme || i == Prog_Option_DarkScheme) && !state->systemScheme) continue;
        b32 colors = i == Prog_Option_ColorGroups || Prog_Options[i].control == Prog_OptionControl_Color;
        if (colors && (Prog_ShownScheme(state, NULL) < Prog_BuiltInSchemes || typed.length || (i != Prog_Option_ColorGroups && !state->colorsExpanded))) continue;
        if (!typed.length || Prog_FuzzyScore(Prog_Options[i].label, Prog_Options[i].tooltip, typed, NULL, NULL) > 0) found[count++] = (u8)i;
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
        case Prog_Option_ColorScheme: case Prog_Option_DarkScheme: return state->scheme;
        case Prog_Option_LightScheme: return state->lightScheme;
        case Prog_Option_Spacing: return 4 + state->spacingShift;
        case Prog_Option_RoundCorners: return !state->squareCorners;
        case Prog_Option_InlineSubmenus: return state->menuInline;
        case Prog_Option_SystemScheme: return state->systemScheme;
        case Prog_Option_AlternatingRows: return state->alternatingRows;
        case Prog_Option_InactiveOpacity: return 60 + state->inactiveShift;
        case Prog_Option_ShowFileExtension: return !state->hideExtensions;
        case Prog_Option_ShowHiddenFiles: return state->showHidden;
        case Prog_Option_ShowSystemFiles: return state->showSystem;
        case Prog_Option_ShortenedPaths: return !state->fullPaths;
        case Prog_Option_Breadcrumbs: return !state->noTrail;
        case Prog_Option_ShowCheckboxes: return state->showCheckboxes;
        case Prog_Option_ConfirmEmptyTrash: return !state->noTrashConfirm;
        case Prog_Option_StoreRecents: return state->storeRecents;
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
        case Prog_Option_Overscroll: return !state->noOverscroll;
        case Prog_Option_ReverseScroll: return state->reverseScroll;
        case Prog_Option_CharacterKeyAction: return state->characterKeys;
        case Prog_Option_Transparency: return state->transparent != 0;
        case Prog_Option_PopupBlur: return (i32)floorf(Prog_GlassBlur + 0.5f);
        case Prog_Option_WindowOpacity: return (i32)floorf(Prog_ThemeGlassUser.window.a * 100 + 0.5f);
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
    if (option == Prog_Option_WindowOpacity) Prog_ThemeGlassUser.window.a = (f32)value / 100.0f;
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
    const ArrayStr *more = option == Prog_Option_LoadOnStartup ? &state->layoutNames : schemes ? &state->schemeNames : NULL;
    if (!more || !more->header.count) return desc->choices;
    String *all = ArenaPushArray(arena, desc->choiceCount + more->header.count, String);
    for (u32 i = 0; i < desc->choiceCount; ++i) all[i] = desc->choices[i];
    for (u64 i = 0; i < more->header.count; ++i) all[desc->choiceCount + i] = more->v[i];
    *count = desc->choiceCount + (u32)more->header.count;
    return all;
}

#define Prog_SchemesFile S("Schemes.conf")

// The custom schemes saved in Schemes.conf, each a "scheme" line with its name, its colors, and an "end" line.
function void Prog_LoadSchemes(Prog_State *state)
{
    ArenaClear(&state->schemesArena);
    state->schemeNames = state->schemeTexts = (ArrayStr){0};
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    String text = {0}, name = {0};
    StrBuilder body = {0};
    b32 inside = false;
    ArrayStr lines = P_ReadFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, Prog_SchemesFile), &text) ? StrSplit(scratch.arena, text, '\n') : (ArrayStr){0};
    for (u64 l = 0; l < lines.header.count; ++l) {
        String line = lines.v[l];
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        if (!inside && line.length > 7 && StrIsEqual(StrFromTo(line, 0, 7), S("scheme "))) {
            name = StrFrom(line, 7);
            body = (StrBuilder){0};
            inside = true;
        } else if (inside && StrIsEqual(line, S("end"))) {
            if (name.length && name.length <= sizeof(state->schemeName.text)) {
                ArrayPush(&state->schemesArena, state->schemeNames, StrCopy(&state->schemesArena, name));
                ArrayPush(&state->schemesArena, state->schemeTexts, StrCopy(&state->schemesArena, body.buffer));
            }
            inside = false;
        } else if (inside) {
            StrBuildStr(scratch.arena, &body, line);
            StrBuildChar(scratch.arena, &body, '\n');
        }
    }
    ReleaseScratch(scratch);
}

// Gives the custom scheme named `name` the colors `text`, after the others when it is new, or with `remove` takes it out; Schemes.conf is
// written again at once, as this happens only from a command.
function void Prog_SetScheme(Prog_State *state, String name, String text, b32 remove)
{
    Arena kept = {0};
    ArrayStr names = {0}, texts = {0};
    b32 placed = false;
    for (u64 i = 0; i < state->schemeNames.header.count; ++i) {
        b32 same = Prog__EqualFolded(state->schemeNames.v[i], name);
        placed |= same;
        if (same && remove) continue;
        ArrayPush(&kept, names, StrCopy(&kept, same ? name : state->schemeNames.v[i]));
        ArrayPush(&kept, texts, StrCopy(&kept, same ? text : state->schemeTexts.v[i]));
    }
    if (!placed && !remove) {
        ArrayPush(&kept, names, StrCopy(&kept, name));
        ArrayPush(&kept, texts, StrCopy(&kept, text));
    }
    ArenaRelease(&state->schemesArena);
    state->schemesArena = kept;
    state->schemeNames = names;
    state->schemeTexts = texts;
    if (!state->configDirectory.length) return;
    Scratch scratch = GetScratch();
    StrBuilder file = {0};
    for (u64 i = 0; i < names.header.count; ++i) StrBuildStr(scratch.arena, &file, StrFormat(scratch.arena, "scheme %S\n%Send\n", names.v[i], texts.v[i]));
    P_FileError error = P_WriteFile(scratch.arena, Prog_JoinPath(scratch.arena, state->configDirectory, Prog_SchemesFile), file.buffer);
    if (error.code) {
        ArenaClear(&state->errorArena);
        state->error = StrFormat(&state->errorArena, "Save schemes: %S", error.message);
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
        String candidate = StrFormat(scratch.arena, "Custom %u", n);
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
        .sidebar = floorf(179.2f * scale + 0.5f),
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
    if (kind == Prog_CommandKind_Back && state->historyAt > 0) to = state->history.v[state->historyAt - 1];
    if (kind == Prog_CommandKind_Forward && state->historyAt + 1 < state->history.header.count) to = state->history.v[state->historyAt + 1];
    if (kind == Prog_CommandKind_Up) to = Prog_UpPath(state->directory.path);
    state->tooltipDetail = to.length ? P_NativePath(&state->frameArena, to) : (String){0};
    state->tooltipDescribes = false;
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
    if (interaction.clicked) Prog_QueueCommand(state, kind, 0, 0, interaction.clickOrder);
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
        }
    }
    // These open menus rather than run commands, so they name themselves; the tab list runs Search Tabs.
    String tip = kind == Prog_CommandKind_PanelMenu ? S("Panel Menu") : kind == Prog_CommandKind_MainMenu ? S("Main Menu")
               : kind == Prog_CommandKind_HistoryMenu ? S("History") : tabs ? (String){0} : S("View");
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
    // Its pixels at 125%: "#" is ink, and "+" and "-" the softer pixels of its smoothed edges.
    local_persist const char *const pixels[] = {
        ".......+++++........",
        "......+######-......",
        "......+#+..+#######.",
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
        "......+#+..-#######.",
        "......+#+...-++++##-",
        "-++++-+#+........+#-",
        "######+#+........+#-",
        "......+#+........+#-",
        "......+#+........+#-",
        "......+############-",
        ".......++++++++++++.",
    };
    f32 unit = m->scale / 1.25f;
    R_Color ink = on || UI->hotKey == button->key ? theme->text : theme->icon;
    // On whole pixels from where the button and the filter's pill were laid out last, since they can sit between them: a pixel right of the
    // button's middle and a pixel above the pill's, which keeps it in place while the button moves as the filter opens, as there.
    f32 x = floorf(button->rect.x0 + (width - 20 * unit) * 0.5f + unit) - button->rect.x0;
    f32 y = floorf(state->filterRect.y0 + (m->pill - 21 * unit) * 0.5f - unit + 0.5f) - button->rect.y0;
    UI_Parent(button) {
        for (u32 row = 0; row < CArrayCount(pixels); ++row) {
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

// A window button at the strip's right end, washed under the pointer like the other buttons and as round as the tab list's beside it,
// the close button in the danger color.
// The glyphs are thin shapes the size of the system's caption glyphs, which the icon font's strokes are too heavy for; only the close cross comes from the font.
function void Prog__WindowButton(Prog_State *state, String identity, Prog_WindowButton kind, Prog_CommandKind command, f32 width, f32 height, f32 scale, const Prog_Metrics *m)
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
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
    UI_SetNextCornerRadius(floorf(5.6f * scale + 0.5f));
    UI_SetNextColors((UI_Colors){ .background = background, .text = ink });
    if (close) {
        UI_SetNextFont(m->icons);
        UI_SetNextText(Icon_X);
    }
    UI_Block *button = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | (close ? UI_Flag_DrawText : 0) | (hot ? UI_Flag_DrawBackground : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(button);
    if (interaction.clicked) Prog_QueueCommand(state, command, 0, 0, interaction.clickOrder);
    // The glyphs sit in the middle of the button's wash, their boxes on whole pixels. Their strokes are a little over a pixel at 100%,
    // and smoothed where they end between pixels.
    f32 glyph = floorf(9.6f * scale + 0.5f);
    f32 stroke = 1.2f * scale;
    f32 gx = ceilf((width - glyph) * 0.5f), gy = floorf((height - glyph) * 0.5f);
    UI_Parent(button) {
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
        if (caption) Prog__Caption(state, S("Caption"), width, bar, -top);
        // The tab's outline, a little thinner than the strip's line, runs a pixel in from its sides.
        f32 stroke = 1.2f * scale, edge = floorf(0.8f * scale + 0.5f);
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
        b32 dropHere = state->tabDrag && UI__Contains(strip->rect, UI->mouse);
        f32 slotGuess = floorf(MinF32(m->line * 12.25f, room / (f32)(state->tabCount + 1)));
        u32 dropSlot = dropHere && slotGuess > 0 ? (u32)UI__Clamp(floorf((UI->mouse.x - strip->rect.x0 - leadWidth) / slotGuess + 0.5f), 0, (f32)state->tabCount) : 0;
        b32 stays = state->tabDrop.fromPanel == panelId && (dropSlot == state->tabDrop.fromSlot || dropSlot == state->tabDrop.fromSlot + 1);
        b32 placeholder = dropHere && !stays;
        if (dropHere) {
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
        f32 tabRadius = floorf(8.0f * scale + 0.5f);
        f32 hang = tabRadius + stroke + 1;
        f32 flare = floorf(m->radius + 0.5f) + stroke;
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
                // A spinner stands in for the folder while the shown tab's listing is on its way, or while its drive spins up a disc.
                const P_Place *drive = Prog_PathDrive(state, path);
                const Prog_Icon *spinner = (shown && state->directoryJob) || (drive && drive->busy) ? Prog_SpinnerIcon(state, UI->nowMicroseconds) : NULL;
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
        UI_SetNextPrefWidth(UI_Pixels(plus));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_Block *plusBox = UI_AddBlock(0);
        UI_Parent(plusBox) {
            UI_Interaction add = Prog__TextButton(S("TabNew"), Icon_Plus, m->icons, UI_Pixels(floorf(plus * 0.8f)), floorf(plus * 0.75f), 0, Prog__CaptionInk(theme), theme->text, theme->iconDisabled,
                                                  state->tabCount < Prog_MaxTabs, m);
            if (add.clicked) Prog_QueueCommand(state, Prog_CommandKind_TabNew, 0, 0, add.clickOrder);
        }
        // The tab list, left of the window buttons; in the title bar it fills the strip under its border as they do.
        b32 maximized = PlatformWindowIsMaximized();
        UI_SetNextFloatingOffset((UI_Vec2){ width - (controls ? controlWidth * 3 : 0) - button - m->gap - floorf((maximized ? 2.4f : 4.8f) * scale + 0.5f),
                                            caption ? -top : -floorf(0.8f * scale + 0.5f) });
        UI_SetNextPrefWidth(UI_Pixels(button));
        UI_SetNextPrefHeight(UI_Pixels(caption ? bar - m->separator : height));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_Block *listBox = UI_AddBlock(UI_Flag_Floating);
        UI_Parent(listBox) {
            Prog__MenuButton(state, S("TabList"), Icon_ChevronDown, Prog_CommandKind_TabList, true, caption ? bar - m->separator : floorf(26.4f * scale + 0.5f), m);
            if (state->currentPanel == state->activePanel) state->tabListRect = listBox->last->rect;
        }
        // The window buttons fill the strip's full height at its right end.
        if (controls) {
            UI_SetNextFloatingOffset((UI_Vec2){ width - controlWidth * 3, -top });
            UI_SetNextPrefWidth(UI_Pixels(controlWidth * 3));
            UI_SetNextPrefHeight(UI_Pixels(bar - m->separator));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_Block *buttons = UI_AddBlock(UI_Flag_Floating);
            UI_Parent(buttons) {
                Prog_WindowButton maximize = PlatformWindowIsMaximized() ? Prog_WindowButton_Restore : Prog_WindowButton_Maximize;
                Prog__WindowButton(state, S("WindowMinimize"), Prog_WindowButton_Minimize, Prog_CommandKind_WindowMinimize, controlWidth, bar - m->separator, scale, m);
                Prog__WindowButton(state, S("WindowMaximize"), maximize, Prog_CommandKind_WindowToggleMaximize, controlWidth, bar - m->separator, scale, m);
                Prog__WindowButton(state, S("WindowClose"), Prog_WindowButton_Close, Prog_CommandKind_WindowClose, controlWidth, bar - m->separator, scale, m);
            }
        }
    }
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
// Every segment but the current one opens its folder.
function void Prog__Breadcrumbs(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    b32 thisPC = StrIsEqual(state->directory.path, Prog_ThisPC);
    u64 shownEnd;
    String path = Prog__CrumbsPath(state, &shownEnd);
    b32 drive;
    u64 start = Prog__CrumbStart(state, path, &drive);
    if (drive || thisPC) {
        UI_Interaction button = Prog__TextButton(S("Crumb/ThisPC"), S("This PC"), NULL, UI_TextContent(), m->bar * 0.75f, m->gap * 0.5f, theme->textDim, theme->text,
                                                 thisPC ? theme->text : theme->textDim, !thisPC, m);
        if (button.clicked) Prog_QueueCommand(state, Prog_CommandKind_NavigatePrefix, 0, 0, button.clickOrder);
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
                                                            m->iconsSmall, UI_Pixels(floorf(m->line * 1.6f)), m->bar * 0.75f, 0, theme->textDim, theme->text,
                                                            theme->textDim, true, m);
                if (separator.clicked) Prog_QueueCommand(state, Prog_CommandKind_FilesPopupOpen, 0, 0, separator.clickOrder)->target = at - 1;
            } else if (drive && at == start) {
                Prog__SmallIcon(Icon_ChevronRight, floorf(m->line * 1.6f), Prog__StripHeight(m), theme->textDim, 0.5f, m);
            }
            if (drive && at == 0) segment = Prog__DriveName(state, path);
            else if (P_IsTrashFolder(StrFromTo(path, 0, (i64)end))) segment = Prog_PathName(state, StrFromTo(path, 0, (i64)end));
            // A walk through the whole tree is marked after the folder walked.
            if (current && state->expanded) segment = StrFormat(&state->frameArena, "%S [...]", segment);
            String identity = StrFormat(&state->frameArena, "Crumb/%S", StrFromTo(path, 0, (i64)end));
            if (state->filesOpen && StrIsEqual(Prog__CrumbFolder(path, end), state->filesFolder)) {
                b32 more = end < path.length;
                f32 chevron = floorf(m->line * 1.6f);
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
                if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_NavigatePrefix, end, 0, interaction.clickOrder);
            }
        }
        at = end + 1;
    }
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
    if (path.length) Prog__DescribeTooltip(state, Prog_CommandKind_FolderViewToggle, kept ? S("Folder view settings saved and applied.\nIcons, sort, folders first, list columns.")
                                                                                        : S("No folder view settings saved.\nUsing panel's view settings."));
    if (lock.clicked) Prog_QueueCommand(state, Prog_CommandKind_FolderViewToggle, 0, 0, lock.clickOrder);
    Prog__Spacer(UI_Pixels(apart), UI_Pixels(Prog__StripHeight(m)));
    b32 bookmarked = Prog_IsBookmarked(state, path);
    UI_Interaction bookmark = Prog__TextButton(S("Bookmark"), Icon_Bookmark, m->icons, UI_Pixels(width), height, 0,
                                               bookmarked ? theme->textMatch : theme->icon, bookmarked ? theme->textMatch : theme->text, theme->iconDisabled,
                                               path.length != 0, m);
    if (path.length) Prog__DescribeTooltip(state, Prog_CommandKind_BookmarksToggle, bookmarked ? S("This path is bookmarked.") : S("This path is not bookmarked."));
    if (bookmark.clicked) Prog_QueueCommand(state, Prog_CommandKind_BookmarksToggle, 0, 0, bookmark.clickOrder);
}

// The branch of the git repository the shown folder is in, with the commits to push and to pull beside it when there are any.
function void Prog__GitStatus(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->git.branch.length) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 bar = Prog__StripHeight(m), gap = floorf(4.8f * m->scale + 0.5f);
    Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
    UI_SetNextFont(m->detail);
    Prog__Label(state->git.branch, UI_TextContent(), bar, 0, theme->textDim, 0);
    for (u32 side = 0; side < 2; ++side) {
        i32 count = side ? state->git.behind : state->git.ahead;
        if (count <= 0) continue;
        Prog__Spacer(UI_Pixels(gap), UI_Pixels(bar));
        Prog__SmallIcon(side ? Icon_ArrowDown : Icon_ArrowUp, 0, bar, theme->textMatch, 0.5f, m);
        UI_SetNextFont(m->detail);
        Prog__Label(StrFormat(&state->frameArena, "%d", count), UI_TextContent(), bar, 0, theme->textMatch, 0);
    }
    Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
}

function void Prog__Toolbar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 bar = Prog__StripHeight(m);
    UI_Block *toolbar = Prog__Box(UI_Flex(), UI_Pixels(bar), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(toolbar) {
        Prog__Spacer(UI_Pixels(floorf(4.8f * m->scale + 0.5f)), UI_Pixels(bar));
        String up = Prog_UpPath(state->directory.path);
        Prog__ToolbarButton(state, S("Back"), Icon_ArrowLeft, Prog_CommandKind_Back, state->historyAt > 0, m);
        Prog__ToolbarButton(state, S("Forward"), Icon_ArrowRight, Prog_CommandKind_Forward, state->historyAt + 1 < state->history.header.count, m);
        Prog__ToolbarButton(state, S("Up"), Icon_ArrowUp, Prog_CommandKind_Up, up.length != 0, m);
        Prog__MenuButton(state, S("HistoryMenu"), Icon_ChevronDown, Prog_CommandKind_HistoryMenu, state->history.header.count > 1, 0, m);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
        Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
        Prog__FolderButtons(state, floorf(5.6f * m->scale + 0.5f), m);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
        Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(bar));
        // A path wider than the room shows its end, cut off at the left.
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(bar));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextScrollOffset((UI_Vec2){ 1e9f, 0 });
        UI_Block *crumbs = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Breadcrumbs")), UI_Flag_Clip);
        UI_Parent(crumbs) Prog__Breadcrumbs(state, m);
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

// The sort a column's title selects.
// This PC's drives are not sorted, so its own columns sort nothing (Prog_Sort_Count).
global const Prog_Sort Prog_ColumnSort[Prog_ColumnId_Count] = {
    Prog_Sort_Name, Prog_Sort_Type, Prog_Sort_Modified, Prog_Sort_Folders, Prog_Sort_Files, Prog_Sort_Size, Prog_Sort_Created, Prog_Sort_Accessed, Prog_Sort_Size, Prog_Sort_Size,
    Prog_Sort_Count, Prog_Sort_Count, Prog_Sort_Count, Prog_Sort_Count, Prog_Sort_Count,
};

global const String Prog_ColumnTitles[Prog_ColumnId_Count] = {
    SInit("Name"), SInit("Type"), SInit("Date Modified"), SInit("Folders"), SInit("Files"), SInit("Size"), SInit("Date Created"), SInit("Date Accessed"), SInit("Folder Usage"), SInit("Volume Usage"),
    SInit("File System"), SInit("Free Space"), SInit("Percent Full"), SInit("Original Location"), SInit("Date Deleted"),
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
        Prog_ColumnId_VolumeUsage, Prog_ColumnId_FolderUsage, Prog_ColumnId_FileSystem, Prog_ColumnId_FreeSpace, Prog_ColumnId_PercentFull,
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
            result.v[result.count++] = (Prog_Column){ Prog_ColumnTitles[id], width, right ? 1.0f : 0, (u8)id, (u8)i, (b8)(part + 1 == parts && i + 1 < set.count) };
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
    UI_Parent(header) {
        if (Prog__MarksRecents(state)) Prog__Spacer(UI_Pixels(Prog__RecentGutter(m->scale)), UI_Pixels(m->row));
        for (u32 c = 0; c < columns->count; ++c) {
            const Prog_Column *column = &columns->v[c];
            u32 id = column->id;
            Prog_Sort sort = Prog_ColumnSort[id];
            b32 sorted = sort < Prog_Sort_Count && state->sort == sort && (id < Prog_ColumnId_FolderUsage);
            UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Header/%S", column->title));
            b32 sortable = sort < Prog_Sort_Count && !state->loading;
            b32 hot = UI->hotKey == key && sortable;
            UI_SetNextPrefWidth(column->width > 0 ? UI_Pixels(column->width) : UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_Block *cell = UI_AddBlockFromKey(key, UI_Flag_RightMouseClickable | UI_Flag_MiddleMouseClickable | (sortable ? UI_Flag_LeftMouseClickable : 0));
            ends[c] = cell->rect.x1;
            if (c == 0) x0 = cell->rect.x0;
            UI_Interaction interaction = UI_InteractionFromBlock(cell);
            if (interaction.clicked && interaction.button == MouseButton_Left) Prog_QueueCommand(state, Prog_CommandKind_Sort, (u64)sort, 0, interaction.clickOrder);
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
                    UI_SetNextBorderThickness(1.2f * m->scale);
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
        for (u32 c = 0; c + 1 < columns->count; ++c) {
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
// `caretGrow` lengthens the caret past the text line at both ends, as the palettes' tall fields draw it.
function void Prog__EditContents(const Prog_TextEdit *edit, f32 x, f32 y, b32 caret, f32 caretGrow, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String text = Prog_EditText(edit);
    u32 from = Min(edit->cursor, edit->anchor), to = Max(edit->cursor, edit->anchor);
    f32 x0 = floorf(x + R_TextWidth(m->font, StrMake(text.value, from)) + 0.5f);
    f32 target = floorf(R_TextWidth(m->font, StrMake(text.value, to)) - R_TextWidth(m->font, StrMake(text.value, from)) + 0.5f);
    // The caret jumps where it goes, but the selection's width eases, so that clearing the text draws the selection back to its start;
    // the caret waits for that.
    f32 width = floorf(UI_Animate(UI_KeyFromStr(0, StrFormat(UI->frameArena, "EditSelection/%p", (const void *)edit)), target, target) + 0.5f);
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
        UI_SetNextFloatingOffset((UI_Vec2){ caretX, y - caretGrow });
        UI_SetNextPrefWidth(UI_Pixels(Prog__CaretWidth(m)));
        UI_SetNextPrefHeight(UI_Pixels(m->line + 2 * caretGrow));
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
    UI_Block *cell = UI_AddBlock(icons ? 0 : UI_Flag_Clip);
    UI_Parent(cell) {
        UI_SetNextPrefWidth(UI_Pixels(boxWidth));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextCornerRadius(m->radius);
        UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->focus });
        UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("RenameBox")), UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        state->renameRect = box->rect;
        Prog__EditMouse(state, box, &state->rename, pad, Prog_CommandKind_RenameCaret, m);
        UI_Parent(box) Prog__EditContents(&state->rename, pad, above, true, 0, m);
    }
}

// The bytes from `start` to `end` of `name`, those a filter matched, flagged in `marks` when not NULL, in the match color.
function void Prog__NameRuns(String name, u64 start, u64 end, const u8 *marks, R_Color color, R_Color matched, const R_Font *font, const Prog_Metrics *m)
{
    for (u64 at = start; at < end; start = at) {
        b32 match = marks && marks[at];
        while (at < end && (marks && marks[at]) == match) ++at;
        if (font) UI_SetNextFont(font);
        Prog__Label(StrFromTo(name, start, (i64)at), UI_TextContent(), m->row, 0, match ? matched : color, 0);
    }
}

// A name whose first `prefix` bytes are the folder it is in, in an expanded listing: the item's own name, then a chevron and that folder, smaller and dimmed.
function void Prog__NameLabel(String name, u64 prefix, const u8 *marks, UI_Size width, R_Color color, R_Color dim, R_Color matched, const Prog_Metrics *m)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, m->gap, 0 });
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *cell = UI_AddBlock(UI_Flag_Clip);
    UI_Parent(cell) {
        Prog__NameRuns(name, prefix, name.length, marks, color, matched, NULL, m);
        if (prefix > 1) {
            Prog__Icon(Icon_ChevronRight, floorf(22.8f * m->scale), m->row, dim, 0.5f, m);
            Prog__NameRuns(name, 0, prefix - 1, marks, dim, matched, m->detail, m);
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
        values[Prog_ColumnId_Type] = entry->type;
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
    String result[Prog_ColumnId_Count] = { entry->file.name, entry->type, Prog_EntryModified(frame, entry), folders, files, size, created, accessed, folderUsage, volumeUsage };
    for (u32 i = 0; i < Prog_ColumnId_Count; ++i) values[i] = result[i];
    values[Prog_ColumnId_Name] = Prog_EntryName(state, entry);
    // A binned item shows where it came from and when it went.
    if (entry->original.length) values[Prog_ColumnId_OriginalLocation] = P_NativePath(frame, entry->original);
    if (entry->deletedTimeUnix) values[Prog_ColumnId_DateDeleted] = P_FormatFileTime(frame, entry->deletedTimeUnix);
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

function b32 Prog__EntryFaded(const Prog_Entry *entry);

// The color of an item's text: the content colors while it is selected or under the pointer, else its name's by its kind, and the
// secondary one for its other values and a hidden item's name; and the color of the letters a filter matched in it.
function R_Color Prog__ItemInk(const Prog_Entry *entry, b32 hot, b32 name)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    if (entry->selected) return theme->contentSelection;
    if (hot) return theme->contentHover;
    if (!name || Prog__EntryFaded(entry)) return theme->textDim;
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
        UI_SetNextBorderThickness(1.2f * m->scale);
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
// Above the list icon size it is the item's thumbnail, with its type's icon at that size while the thumbnail loads, since a stretched list icon would blur.
function void Prog__ItemIcon(Prog_State *state, const Prog_Entry *entry, f32 size, f32 width, f32 height, f32 alignX, b32 highlighted, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    b32 faded = Prog__EntryFaded(entry);
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
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextTextAlignment((UI_Vec2){ alignX, 0.5f });
        UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ size, size }, spinner ? theme->icon : (R_Color){ 1, 1, 1, faded ? theme->hiddenAmount.r : 1 });
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
        UI_SetNextBorderThickness(1.2f * m->scale);
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
    // Presses on the icon and the name drag the files; elsewhere on the row they draw a selection box, as in Explorer.
    b32 onName = UI->pressOrigin.x < row->rect.x0 + (Prog__MarksRecents(state) ? Prog__RecentGutter(m->scale) : 0) + m->iconSlot + R_TextWidth(m->font, entry->file.name) + m->gap;
    b32 result = Prog__ItemInput(state, index, row, key, onName, renaming);
    String values[Prog_ColumnId_Count];
    Prog__EntryValues(state, entry, values);
    P_DirectoryTotals live;
    b32 counting = entry->file.kind == P_FileKind_Directory && !entry->measured && Prog__LiveTotals(state, entry, &live) && !live.complete;
    UI_Parent(row) {
        Prog__ItemBox(Prog__ItemLook(entry->selected, hot, focused), UI_Percent(1), m);
        if (Prog__MarksRecents(state) && entry->recent) Prog__Icon(Icon_Clock, Prog__RecentGutter(m->scale), height, theme->text, 0.5f, m);
        else if (Prog__MarksRecents(state)) Prog__Spacer(UI_Pixels(Prog__RecentGutter(m->scale)), UI_Pixels(height));
        if (!Prog__ItemCheckbox(state, index, m->iconSlot - m->gap, height, m)) Prog__ItemIcon(state, entry, (f32)state->iconSize, m->iconSlot - m->gap, height, 1, highlighted, m);
        for (u32 c = 0; c < (nameOnly ? 1u : columns->count); ++c) {
            const Prog_Column *column = &columns->v[c];
            u32 i = nameOnly ? (u32)Prog_ColumnId_Name : column->id;
            // The icon before the name is inside Name's width, as the header counts it.
            f32 columnWidth = i == Prog_ColumnId_Name ? MaxF32(0, column->width - (m->iconSlot - m->gap)) : column->width;
            UI_Size width = column->width > 0 && !nameOnly ? UI_Pixels(columnWidth) : UI_Flex();
            if (i == Prog_ColumnId_Name && renaming) {
                Prog__RenameBox(state, width, false, m);
                continue;
            }
            R_Color color = Prog__ItemInk(entry, hot, i == Prog_ColumnId_Name);
            if (i == Prog_ColumnId_Name && (state->filter.length || state->directory.expanded)) {
                String leaf = Prog_EntryLeaf(entry->file.name);
                u64 prefix = entry->file.name.length - leaf.length;
                u8 *marks = state->filter.length ? ArenaPushArrayNoZero(&state->frameArena, Max(entry->file.name.length, (u64)1), u8) : NULL;
                if (marks) Prog_EntryScore(entry->file.name, Prog_EditText(&state->filter), marks);
                Prog__NameLabel(entry->file.name, prefix, marks, width, color, theme->textDim, Prog__ItemMatch(entry, hot), m);
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
        if (!Prog__ItemCheckbox(state, index, m->iconSlot - m->gap, height, m)) Prog__ItemIcon(state, entry, (f32)state->iconSize, m->iconSlot - m->gap, height, 1, highlighted, m);
        if (renaming) Prog__RenameBox(state, UI_Flex(), false, m);
        else Prog__Label(Prog_EntryName(state, entry), UI_Flex(), height, m->gap, Prog__ItemInk(entry, hot, true), 0);
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
    UI_SetNextBorderThickness(1.2f * m->scale);
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
                UI_SetNextBorderThickness(1.2f * m->scale);
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
                            ? StrFormat(&state->frameArena, "%S free of %S (%S)", values[Prog_ColumnId_FreeSpace], values[Prog_ColumnId_Size], values[Prog_ColumnId_FileSystem])
                            : StrFormat(&state->frameArena, "%S free of %S", values[Prog_ColumnId_FreeSpace], values[Prog_ColumnId_Size]);
                UI_SetNextFont(m->detail);
                Prog__Label(Prog__Ellipsize(&state->frameArena, m->detail, free, text), UI_Pixels(width), m->line, 0, theme->textDim, 0.5f);
            }
        } else {
            String name = Prog__Ellipsize(&state->frameArena, m->font, Prog_EntryName(state, entry), text);
            Prog__Label(name, UI_Pixels(width), m->line, 0, Prog__ItemInk(entry, hot, true), 0.5f);
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
    local_persist const String names[Prog_View_Count] = { SInit("Details"), SInit("List"), SInit("Columns"), {0} };
    Prog_View view = Prog_ZoomView(zoom);
    return view != Prog_View_Icons ? names[view] : zoom < Prog_ZoomLarge ? S("M Icons") : zoom < Prog_ZoomMax ? S("L Icons") : S("XL Icons");
}

// Icon views draw their icons from 48 logical pixels at 30% to 205 at 100%, in physical pixels.
function f32 Prog__ZoomIconSize(u32 zoom, f32 scale)
{
    f32 t = (Prog__ZoomFraction(zoom) - 0.3f) / 0.7f;
    return floorf((48.0f + t * (204.8f - 48.0f)) * scale + 0.5f);
}

function void Prog__List(Prog_State *state, const Prog_Columns *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
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
    UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Files")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable |
                                        (!state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0));
    UI_Interaction listInteraction = UI_InteractionFromBlock(list);
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
    if (state->resetScroll) { list->scrollOffset = list->scrollShown = (UI_Vec2){0}; state->resetScroll = false; }

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
        // This PC's drives are taller, for their fullness bars.
        f32 extra = StrIsEqual(state->directory.path, Prog_ThisPC) ? Prog__DriveCellExtra(iconSize, m) : 0;
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
    u64 lines = (state->viewCount + perLine - 1) / perLine;

    state->pageRows = (u64)(Max(0.0f, list->rect.y1 - list->rect.y0) / pitch.y);
    // Before its first layout the list has no height to reveal anything in.
    if (state->revealFocus && state->hasFocus && list->rect.y1 > list->rect.y0) {
        u64 position = Prog_ViewPosition(state, state->focusedRow);
        f32 top = (f32)((position == UINT64_MAX ? 0 : position) / perLine) * pitch.y;
        f32 height = Max(0.0f, list->rect.y1 - list->rect.y0);
        // Center Camera puts its line in the middle.
        if (state->centerFocus) list->scrollOffset.y = Max(0.0f, top + pitch.y * 0.5f - height * 0.5f);
        else if (top < list->scrollOffset.y) list->scrollOffset.y = top;
        else if (top + pitch.y > list->scrollOffset.y + height) list->scrollOffset.y = Max(0.0f, top + pitch.y - height);
        state->revealFocus = state->centerFocus = false;
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
            // Columns keeps its cells when only one fits across.
            if (perLine == 1 && view != Prog_View_Columns) {
                u64 row = state->view[line];
                UI_Key key = UI_KeyFromStr(directoryKey, state->directory.entries[row].file.name);
                if (Prog__Row(state, row, line, key, columns, view == Prog_View_List, m)) marqueeFrom = key;
                continue;
            }
            UI_Block *cells = Prog__Box(UI_Flex(), UI_Pixels(pitch.y), UI_Layout_Horizontal, (R_Color){0}, 0);
            UI_Parent(cells) {
                for (u64 at = line * perLine; at < Min(state->viewCount, (line + 1) * perLine); ++at) {
                    u64 row = state->view[at];
                    Prog_Entry *entry = &state->directory.entries[row];
                    UI_Key key = UI_KeyFromStr(directoryKey, entry->file.name);
                    // An item drawn in the last frame eases from where it was to its place in a new layout; others
                    // start in place.
                    f32 placeX = (f32)(at - line * perLine) * pitch.x, placeY = (f32)line * pitch.y;
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
        // Cells that sit lower in their slot, as Columns' do, reach past the last one.
        if (lines && item.y1 > pitch.y) Prog__Spacer(UI_Flex(), UI_Pixels(item.y1 - pitch.y));
        // A list longer than its view scrolls on by half the view, so that its last line stops just above the middle.
        f32 viewHeight = list->rect.y1 - list->rect.y0;
        if (!state->noOverscroll && (f32)lines * pitch.y > viewHeight && viewHeight > 2 * pitch.y) Prog__Spacer(UI_Flex(), UI_Pixels(floorf(viewHeight * 0.5f)));
        if (!state->viewCount && !state->loading) {
            // Centered a little below the top.
            String empty = state->directory.count ? S("All items are currently hidden by view settings.") : S("This folder is empty.");
            Prog__Spacer(UI_Flex(), UI_Pixels(floorf(m->row * 0.65f)));
            Prog__Label(empty, UI_Flex(), m->row, 0, theme->textDim, 0.5f);
        }
    }
    // The scroll bar sits in the window's coordinates, since the boxes around the list have no layout of their own yet.
    state->listScrolls = list->contentSize.y > list->rect.y1 - list->rect.y0 + 0.5f && list->rect.y1 > list->rect.y0;
    UI_Parent(UI->root) Prog__ScrollBar(list, pitch.y, S("Files"), 1, m);
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
        UI_SetNextBorderThickness(1.2f * m->scale);
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
                    Prog__StatusIcon(Icon_Search, m->iconSlot - m->gap, tall, state->filter.length ? theme->text : theme->textDim, m);
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(tall));
                    UI_Block *field = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilterField")), UI_Flag_Clip | UI_Flag_LeftMouseClickable);
                    Prog__EditMouse(state, field, &state->filter, floorf(m->gap * 0.5f), Prog_CommandKind_FilterCaret, m);
                    UI_Parent(field) Prog__EditContents(&state->filter, floorf(m->gap * 0.5f), floorf((tall - m->line) * 0.5f), state->filtering, 0, m);
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
                Prog__StatusIcon(Icon_Search, m->iconSlot - m->gap, m->pill, theme->textDim, m);
                String placeholder = StrFormat(&state->frameArena, "Filter %" PRIu64 " files...", state->directory.count);
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
            status = StrFormat(&state->frameArena, "Loading %S...", walking->path);
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
            UI_SetNextBorderThickness(1.2f * m->scale);
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
                if (state->directoryJob) UI_KeepAnimating();
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
                local_persist const String sortTips[Prog_FolderSort_Count] = { SInit("Folders first, then files."), SInit("Files first, then folders."), SInit("Folders and files mixed.") };
                Prog__DescribeTooltip(state, Prog_CommandKind_CycleFolderSort, sortTips[state->folderSort]);
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
                f32 zoom = Prog__ZoomFraction(state->zoom);
                f32 barWidth = floorf(40.8f * m->scale + 0.5f);
                // The pointer over the view and its zoom shows the zoom popup.
                f32 nameGap = floorf(8.0f * m->scale + 0.5f), barGap = floorf(13.6f * m->scale + 0.5f);
                UI_SetNextPrefWidth(UI_Pixels(R_TextWidth(m->icons, viewIcons[view]) + R_TextWidth(m->font, name) + nameGap + barGap + barWidth));
                UI_SetNextPrefHeight(UI_Pixels(m->pill));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Block *viewButton = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ViewMenu")), UI_Flag_LeftMouseClickable);
                if (state->currentPanel == state->activePanel) state->zoomAnchor = viewButton->rect;
                b32 viewHot = UI->hotKey == viewButton->key || (state->zoomShown && state->currentPanel == state->activePanel);
                UI_Parent(viewButton) {
                Prog__StatusIcon(viewIcons[view], 0, m->pill, viewHot ? theme->text : theme->icon, m);
                Prog__LabelPadded(name, UI_TextContent(), m->pill, nameGap, 0, theme->text, 0);
                Prog__Spacer(UI_Pixels(barGap), UI_Pixels(m->pill));
                Prog__FillBar(StrFormat(&state->frameArena, "%u%%", state->zoom / 10), zoom, m);
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
    UI_SetNextChildrenPadding((UI_Padding){ floorf(8.0f * m->scale + 0.5f), 0, floorf(10.4f * m->scale + 0.5f), 0 });
    UI_SetNextCornerRadius(m->radius);
    UI_SetNextColors((UI_Colors){ .background = theme->alternateRow });
    UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | (hot ? UI_Flag_DrawBackground : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(row);
    if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_ToggleGroup, (u64)group, 0, interaction.clickOrder);
    UI_Parent(row) {
        R_Color color = hot ? theme->text : theme->textHeading;
        f32 slot = floorf(m->line * 1.5f) - floorf(2.4f * m->scale + 0.5f);
        if (icon.length) Prog__Icon(icon, slot, m->row, color, 0, m);
        else Prog__DriveGlyph(slot, m->row, color, m);
        Prog__Label(title, UI_Flex(), m->row, 0, color, 0);
        Prog__Icon(Prog_GroupOpen(state, group) || state->sidebarFilter.length ? Icon_ChevronDown : Icon_ChevronRight, m->line * 1.5f, m->row, color, 0.5f, m);
    }
}

// One location under a section title: its folder icon, or a drive glyph, and its name.
// Drives show how full they are under the name, and bookmarks get a remove button while hovered.
// A row of the sidebar's `group`; a `faded` one, hidden, is dimmed as the list dims hidden items.
function void Prog__SidebarItem(Prog_State *state, Prog_Group group, u64 index, String name, String path, const P_Place *drive, b32 faded, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Place/%u/%" PRIu64 "/%S", group, index, path));
    UI_Key removeKey = UI_KeyFromStr(key, S("Remove"));
    // Not lit under a drag, which would hide the line where it goes.
    b32 hot = (UI->hotKey == key || UI->hotKey == removeKey) && !P_DropPending();
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
    // Under a drag the middle of a row takes it into its folder, lit; a bookmark's edges put it between the bookmarks instead.
    if (P_DropPending() && path.length && !StrIsEqual(path, Prog_ThisPC) && UI__Contains(row->rect, UI->mouse)) {
        f32 edge = group == Prog_Group_Bookmarks ? (row->rect.y1 - row->rect.y0) * 0.25f : 0;
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
    // Ctrl opens it in a new tab, as the middle button does.
    b32 inTab = interaction.button == MouseButton_Middle || (interaction.button == MouseButton_Left && (UI->mods & KeyMod_Control));
    if (interaction.clicked && interaction.button == MouseButton_Left && !inTab) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SidebarOpen, (u64)group, 0, interaction.clickOrder);
        command->target = index;
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
        // Folders and drives have the folders' color, This PC the text's.
        R_Color color = hot ? theme->contentHover : faded && !current ? theme->textDim : StrIsEqual(path, Prog_ThisPC) ? theme->text : theme->folder;
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
                UI_SetNextPrefHeight(UI_Pixels(m->line));
                UI_SetNextColors((UI_Colors){ .text = color });
                UI_SetNextText(name);
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
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
            UI_SetNextText(Icon_Trash2);
            UI_Block *remove = UI_AddBlockFromKey(removeKey, UI_Flag_DrawText | UI_Flag_LeftMouseClickable | (removeHot ? UI_Flag_DrawBackground : 0));
            UI_Interaction removing = UI_InteractionFromBlock(remove);
            if (removing.clicked) Prog_QueueCommand(state, Prog_CommandKind_RemoveBookmark, index, 0, removing.clickOrder);
        }
    }
}

// The box for a group's items, which slides open and shut to the height its items took last frame (none on the first frame it opens, which
// shows them at once); null while it is shut, when the items need no blocks.
function UI_Block *Prog__GroupBody(Prog_State *state, Prog_Group group, const Prog_Metrics *m)
{
    // A filter shows what it finds in every group.
    b32 open = Prog_GroupOpen(state, group) || state->sidebarFilter.length;
    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "GroupBody/%u", (u32)group));
    UI_Block *last = UI__Find(key);
    f32 full = last ? last->contentSize.y : 0;
    f32 shown = UI_Slide(UI_KeyFromStr(key, S("Shown")), open ? 1.0f : 0.0f, Prog__SlideCap(m) / MaxF32(full, 1));
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
        Prog__Icon(Icon_Search, 0, strip, theme->textDim, 0, m);
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
                UI_SetNextText(S("Filter..."));
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
            }
            if (state->sidebarFilter.length || state->sidebarFiltering) Prog__EditContents(&state->sidebarFilter, pad, top, state->sidebarFiltering, 0, m);
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
    UI_Block *sidebar = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Sidebar")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable);
    UI_Parent(sidebar) {
        Prog__SidebarFilterBox(state, m);
        Prog__Spacer(UI_Flex(), UI_Pixels(floorf(4.8f * m->scale + 0.5f)));
        u64 indices[Prog_ShownRecentCount];
        u32 shown = 0, listed = Prog_ShownRecents(state, indices);
        for (u32 n = 0; n < listed; ++n) if (Prog__SidebarFinds(state, Prog_PathName(state, state->recents.v[indices[n]]))) indices[shown++] = indices[n];
        if (shown || !state->sidebarFilter.length) Prog__GroupTitle(state, Prog_Group_Recents, S("Recents"), Icon_Clock, m);
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
        u64 count = state->bookmarks.header.count, found = 0;
        for (u64 i = 0; i < count; ++i) found += Prog__SidebarFinds(state, Prog_PathName(state, state->bookmarks.v[i])) ? 1 : 0;
        if (found || !state->sidebarFilter.length) Prog__GroupTitle(state, Prog_Group_Bookmarks, S("Bookmarks"), Icon_Bookmark, m);
        // A drag over the section pins its folders between the bookmarks the pointer is between, marked by a line there; the rects are the last layout's.
        R_Rect title = sidebar->last ? sidebar->last->rect : (R_Rect){0}, zone = title;
        u64 before = count;
        f32 lineY = title.y1;
        body = found || !state->sidebarFilter.length ? Prog__GroupBody(state, Prog_Group_Bookmarks, m) : NULL;
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
        found = 0;
        for (u64 i = 0; i < state->drives.header.count; ++i) found += Prog__SidebarFinds(state, state->drives.v[i].name) ? 1 : 0;
        if (found || !state->sidebarFilter.length) Prog__GroupTitle(state, Prog_Group_Storage, S("Storage"), (String){0}, m);
        body = found || !state->sidebarFilter.length ? Prog__GroupBody(state, Prog_Group_Storage, m) : NULL;
        if (body) UI_Parent(body) {
            for (u64 i = 0; i < state->drives.header.count; ++i) {
                const P_Place *drive = &state->drives.v[i];
                if (Prog__SidebarFinds(state, drive->name)) Prog__SidebarItem(state, Prog_Group_Storage, i, drive->name, drive->path, drive, false, m);
            }
        }
        found = 0;
        for (u64 i = 0; i < state->places.header.count; ++i) found += Prog__SidebarFinds(state, state->places.v[i].name) ? 1 : 0;
        if (found || !state->sidebarFilter.length) Prog__GroupTitle(state, Prog_Group_Places, S("Places"), Icon_Folder, m);
        body = found || !state->sidebarFilter.length ? Prog__GroupBody(state, Prog_Group_Places, m) : NULL;
        if (body) UI_Parent(body) {
            for (u64 i = 0; i < state->places.header.count; ++i) {
                const P_Place *place = &state->places.v[i];
                if (Prog__SidebarFinds(state, place->name)) Prog__SidebarItem(state, Prog_Group_Places, i, place->name, place->path, NULL, false, m);
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
    StrBuildStr(arena, &out, S("Conflicts with commands:"));
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
                Prog__PopupTooltip(state, S("Delete hotkey."));
            }
        }
        String tip = mouse ? S("Mouse hotkeys cannot be changed.") : chip->conflict ? Prog__ConflictText(frame, state, kind, chip->slot)
                   : chip->idle ? S("Character Key Action not set to Trigger Hotkey.") : S("Press to set or remove hotkey.");
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
    String title = S("Delete assigned hotkey?");
    f32 button = floorf(96.0f * m->scale + 0.5f), gap = floorf(8.0f * m->scale + 0.5f), pad = floorf(8.0f * m->scale + 0.5f);
    f32 width = MaxF32(R_TextWidth(m->font, title) + 2 * m->inset, 2 * button + gap + 2 * pad), height = m->row + m->bar + 3 * pad;
    f32 x = MinF32(state->hotkeyAskAt.x, view.x1 - width - m->inset), y = MinF32(state->hotkeyAskAt.y, view.y1 - height - m->inset);
    UI_SetNextPrefWidth(UI_Pixels(floorf(width)));
    UI_SetNextPrefHeight(UI_Pixels(floorf(height)));
    UI_SetNextFloatingOffset((UI_Vec2){ floorf(MaxF32(x, view.x0) - view.x0), floorf(MaxF32(y, view.y0) - view.y0) });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
    UI_SetNextCornerRadius(m->radius * 2);
    UI_SetNextBorderThickness(1.2f * m->scale);
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
            UI_SetNextText(b ? S("Cancel") : S("Delete"));
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
    f32 b = 1.2f * m->scale, flare = Prog__MenuFlare(m), R = radius;
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
            UI_SetNextBorderThickness(1.2f * m->scale);
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
    f32 b = 1.2f * m->scale, band = ceilf(b), rowRadius = m->radius * 2, mirror = join->mirror, L = join->line, R = radius, outside = 4 * R;
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
                UI_SetNextBorderThickness(1.2f * m->scale);
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
    f32 width = floorf(MinF32(MaxF32(rowWidth + iconLeft + iconColumn + m->inset + arrow + star + 2 * pad, m->line * 12), m->line * 36)) + gutter;
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
            UI_SetNextChildrenPadding((UI_Padding){ pad - fieldInset, 0, pad - fieldInset, 0 });
            UI_SetNextCornerRadius(MaxF32(0, radius - fieldInset));
            UI_SetNextShape(R_Shape_SquareBottomLeft | R_Shape_SquareBottomRight | (border->shape & (R_Shape_SquareTopLeft | R_Shape_SquareTopRight)));
            UI_SetNextColors((UI_Colors){ .background = theme->inner });
            UI_Block *field = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            UI_Parent(field) {
                Prog__Spacer(UI_Pixels(iconLeft), UI_Pixels(fieldHeight));
                Prog__Icon(Icon_Search, iconColumn, fieldHeight, theme->textDim, 0, m);
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(fieldHeight));
                UI_Block *text = UI_AddBlock(UI_Flag_Clip);
                UI_Parent(text) {
                    if (!state->menuSearch.length) {
                        UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((fieldHeight - m->line) * 0.5f) });
                        UI_SetNextPrefHeight(UI_Pixels(m->line));
                        UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                        UI_SetNextText(S("Select a system command..."));
                        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                    }
                    Prog__EditContents(&state->menuSearch, 0, floorf((fieldHeight - m->line) * 0.5f), true, 0, m);
                }
                // A button at its end closes the menu.
                UI_Interaction close = Prog__TextButton(S("MenuSearchClose"), Icon_X, m->icons, UI_Pixels(floorf(28.8f * m->scale + 0.5f)), fieldHeight, 0,
                                                        theme->textDim, theme->text, theme->iconDisabled, true, m);
                if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_ContextMenuClose, 0, 0, close.clickOrder);
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
                const Prog_Icon *icon = state->menuIcons ? state->menuIcons[index] : NULL;
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
                        UI_SetNextBorderThickness(1.2f * m->scale);
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
    String suffix = unit == 1000 ? S("K") : S("M");
    // A whole number goes without its ".0", as "1M".
    return tenths < 1000 && tenths % 10 ? StrFormat(arena, "%" PRIu64 ".%" PRIu64 "%S", tenths / 10, tenths % 10, suffix) : StrFormat(arena, "%" PRIu64 "%S", tenths / 10, suffix);
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
    String labels[] = { Prog_PathName(state, path), S("Created:"), S("Subfolders:"), S("Subfiles:"), S("Size:"), S("Cmd:") };
    String values[] = {
        Prog__CrumbPath(state, path),
        known && state->tip.created ? P_FormatFileTime(frame, state->tip.created) : known ? S("--") : waiting,
        known ? Prog__FormatCount(frame, state->tip.totals.folders) : waiting,
        known ? Prog__FormatCount(frame, state->tip.totals.files) : waiting,
        known ? Prog_FormatSize(frame, state->tip.totals.bytes) : waiting,
        S("Open In New Tab"),
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
    UI_SetNextBorderThickness(1.2f * s);
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
    if (described && !plain && !hotkeyCount) hotkeys[hotkeyCount++] = S("Blank");
    ArrayStr lines = detail.length ? StrSplit(&state->frameArena, detail, '\n') : (ArrayStr){0};
    f32 pad = floorf(9.6f * s + 0.5f), gap = floorf(4.8f * s + 0.5f), chipGap = floorf(8.0f * s + 0.5f), lineHeight = floorf(37.6f * s + 0.5f);
    f32 margin = floorf(6.4f * s + 0.5f);
    // The rows of a described tip, and of the others its words and its command, as tall as each other.
    f32 top = described ? floorf(8.0f * s + 0.5f) : 0, pitch = described ? floorf(18.4f * s + 0.5f) : floorf(m->line * 1.6f + 0.5f);
    f32 commandRow = plain ? 0 : described ? floorf(32.0f * s + 0.5f) : detail.length ? pitch : lineHeight;
    f32 bottom = described ? floorf((plain ? 8.8f : 4.8f) * s + 0.5f) : 0;
    f32 height = top + (f32)lines.header.count * pitch + commandRow + bottom;
    String lead = S("Cmd:");
    f32 width = plain ? 0 : R_TextWidth(m->font, lead) + gap + R_TextWidth(m->font, label);
    for (u32 i = 0; i < hotkeyCount; ++i) width += chipGap + MaxF32(R_TextWidth(m->detail, hotkeys[i]) + 2 * floorf(4.8f * s + 0.5f), floorf(30.4f * s + 0.5f));
    for (u64 i = 0; i < lines.header.count; ++i) width = MaxF32(width, R_TextWidth(m->font, lines.v[i]));
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
    UI_SetNextBorderThickness(1.2f * s);
    UI_SetNextOpacity(opacity);
    UI_SetNextBlur(Prog__FloatingBlur(m));
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *tip = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
    UI_Parent(tip) {
        for (u64 i = 0; i < lines.header.count; ++i) Prog__Label(lines.v[i], UI_TextContent(), pitch, 0, theme->text, 0.5f);
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
    f32 s = m->scale, line = m->separator, stroke = 1.2f * s;
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
    String hint = StrFormat(&state->frameArena, "Search %u tabs...", state->tabCount);
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
            Prog__Icon(Icon_Search, floorf(23.2f * s + 0.5f), field, theme->textDim, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(hint);
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f), true, floorf(4.0f * s + 0.5f), m);
            }
            UI_Interaction close = Prog__TextButton(S("TabSearchClose"), Icon_X, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                    theme->icon, theme->text, theme->iconDisabled, true, m);
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
                    Prog__Label(path.length ? Prog_PathName(state, path) : S("This PC"), UI_Flex(), m->line, 0, theme->text, 0);
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
function void Prog__PaletteHints(const char *const *keys, const char *const *words, u32 count, f32 height, R_Color background, f32 radius, const Prog_Metrics *m)
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
            Prog__LabelPadded(StrFromCStr(words[i]), UI_TextContent(), height, floorf(2.4f * s + 0.5f), floorf(12.8f * s + 0.5f), theme->textDim, 0);
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
    if (state->gotoSearching) UI_KeepAnimating();
    if (state->paletteHighlight != UINT32_MAX) state->paletteHighlight = count ? Min(state->paletteHighlight, count - 1) : 0;
    f32 s = m->scale, line = m->separator, row = m->row;
    f32 field = floorf(39.2f * s + 0.5f), footer = floorf(31.2f * s + 0.5f), pad = floorf(4.0f * s + 0.5f);
    f32 top = 2 * m->bar + floorf(11.2f * s + 0.5f), margin = floorf(32.8f * s + 0.5f), bottom = floorf(60.0f * s + 0.5f);
    // The rows in order: each section's title, then its items unless it is folded; a folder typed shows its items alone.
    typedef struct { i32 item; i32 section; } Row;
    // Typing without a separator searches, and the rows found are one list too.
    b32 searching = !folder.length && state->palette.length;
    b32 sections = !folder.length && !searching;
    Row *rows = ArenaPushArray(&state->frameArena, count + Prog_GoToSection_Folder + 1, Row);
    u32 rowCount = 0, highlightRow = 0;
    for (u32 section = 0; section < (sections ? (u32)Prog_GoToSection_Folder : 1u); ++section) {
        if (sections) rows[rowCount++] = (Row){ -1, (i32)section };
        for (u32 i = 0; i < count; ++i) {
            if (sections && found[i].section != section) continue;
            if (i == state->paletteHighlight) highlightRow = rowCount;
            rows[rowCount++] = (Row){ (i32)i, (i32)found[i].section };
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
    f32 listHeight = MinF32((f32)shown * row - line, floorf(room * grow)) + 2 * pad;
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
                Prog__Icon(Icon_Search, lead - inset, field, theme->textDim, 0, m);
            }
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    // A little past the caret.
                    UI_SetNextFloatingOffset((UI_Vec2){ floorf(3.2f * s + 0.5f), floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(S("Open or expand a folder..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f), true, floorf(4.0f * s + 0.5f), m);
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
            UI_Interaction close = Prog__TextButton(S("GoToClose"), Icon_X, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                    theme->icon, theme->text, theme->iconDisabled, true, m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, close.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        // Room on the right for the scroll bar while there is one.
        f32 bar = floorf(6.4f * s + 0.5f), gutter = rowCount > visible ? floorf(19.2f * s + 0.5f) : floorf(6.4f * s + 0.5f);
        UI_SetNextChildrenPadding((UI_Padding){ pad, pad, gutter, pad });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(list) {
            for (u32 r = first; r < first + shown; ++r) {
                Row at = rows[r];
                f32 face = row - line;
                if (at.item < 0) {
                    // A section's title, which folds it.
                    UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "GoToSection/%d", at.section));
                    b32 folded = state->gotoFolded[at.section];
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(face));
                    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                    UI_SetNextChildrenPadding((UI_Padding){ floorf(8.8f * s + 0.5f), 0, floorf(12.0f * s + 0.5f), 0 });
                    UI_Block *title = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable);
                    UI_Interaction interaction = UI_InteractionFromBlock(title);
                    if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_GoToFold, (u64)at.section, 0, interaction.clickOrder);
                    UI_Parent(title) {
                        local_persist const String titles[] = { SInit("Recents"), SInit("Bookmarks"), SInit("Storage"), SInit("Places") };
                        const String icons[] = { Icon_Clock, Icon_Bookmark, Icon_HardDrive, Icon_Folder };
                        static_assert(CArrayCount(titles) == Prog_GoToSection_Folder, "a title for every section");
                        Prog__Icon(icons[at.section], (f32)state->iconSize + floorf(7.2f * s + 0.5f), face, theme->icon, 0, m);
                        Prog__Label(titles[at.section], UI_Flex(), face, 0, theme->text, 0);
                        Prog__Icon(folded ? Icon_ChevronRight : Icon_ChevronDown, floorf(m->line * 1.2f), face, theme->icon, 0.5f, m);
                    }
                    Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
                    continue;
                }
                Prog_GoToItem item = found[at.item];
                UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "GoTo/%d/%S", at.section, item.path));
                b32 highlighted = (u32)at.item == state->paletteHighlight, hot = UI->hotKey == key;
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(face));
                UI_SetNextChildrenLayout(UI_Layout_Horizontal);
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                // Section items sit in from their title; a folder's items do not.
                UI_SetNextChildrenPadding((UI_Padding){ floorf((sections ? 32.8f : 8.8f) * s + 0.5f), 0, pad, 0 });
                UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
                UI_SetNextBorderThickness(1.2f * s);
                UI_SetNextColors((UI_Colors){ .background = theme->selection, .border = theme->selection });
                UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_Clip | (highlighted ? UI_Flag_DrawBackground : 0) | (hot && !highlighted ? UI_Flag_DrawBorder : 0));
                UI_Interaction interaction = UI_InteractionFromBlock(entry);
                if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, (u64)at.item, 0, interaction.clickOrder);
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
            if (rowCount > visible) {
                // The scroll bar: a thumb as long as the share of rows shown, where they are among all of them.
                f32 track = listHeight - 2 * pad, thumb = MaxF32(floorf(track * (f32)visible / (f32)rowCount), row);
                f32 offset = floorf((track - thumb) * (f32)first / (f32)(rowCount - visible));
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
        local_persist const char *const keys[] = { "Up", "Down", 0, "Enter", 0, "Tab", 0, "Ctrl+Enter", 0, "Escape", 0 };
        local_persist const char *const words[] = { 0, 0, "to navigate", 0, "to use", 0, "to expand", 0, "to open in panel", 0, "to dismiss" };
        Prog__PaletteHints(keys, words, CArrayCount(keys), footer, theme->background, radius, m);
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(1.2f * s);
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
    if (option == Prog_Option_LoadThumbnails && StrIsEqual(name, S("local"))) return S("Local Disks");
    if (option == Prog_Option_InspectorLayout && StrIsEqual(name, S("right"))) return S("Horizontal");
    if (option == Prog_Option_InspectorLayout && StrIsEqual(name, S("below"))) return S("Vertical");
    if (option == Prog_Option_LoadOnStartup && StrIsEqual(name, S("session"))) return S("Last Session");
    // A custom scheme keeps its own name, and the reference's has its own.
    b32 schemes = option == Prog_Option_ColorScheme || option == Prog_Option_LightScheme || option == Prog_Option_DarkScheme;
    if (schemes && StrIsEqual(name, S("fullmoon"))) return S("Full Moon");
    b32 builtIn = false;
    for (u32 i = 0; i < Prog_BuiltInSchemes; ++i) builtIn |= StrIsEqual(name, Prog_OptionSchemes[i]);
    if (schemes && !builtIn) return name;
    // A layout keeps its own name.
    if (option == Prog_Option_LoadOnStartup && !StrIsEqual(name, S("root")) && !StrIsEqual(name, S("home"))) return name;
    if (option == Prog_Option_CharacterKeyAction) {
        if (StrIsEqual(name, S("filter"))) return S("Type In Filter");
        if (StrIsEqual(name, S("hotkey"))) return S("Trigger Hotkey");
        if (StrIsEqual(name, S("select"))) return S("Select Typed Item");
    }
    String result = StrCopy(arena, name);
    if (result.length && result.value[0] >= 'a' && result.value[0] <= 'z') ((char *)result.value)[0] = (char)(result.value[0] - 'a' + 'A');
    return result;
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
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        // A check on the knob while on; off, the track's color says it alone.
        UI_SetNextFont(m->iconsSmall);
        UI_SetNextText(Icon_Check);
        UI_SetNextColors((UI_Colors){ .background = on ? theme->text : theme->textDim, .text = theme->textDim });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | (on ? UI_Flag_DrawText : 0));
    }
    return result;
}

// The control at the right of `option`'s row: a switch, a stepper with its value between - and +,
// a slider dragged along its track, or a choice that moves on to its next value.
function void Prog__OptionControl(Prog_State *state, Prog_Option option, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const Prog_OptionDesc *desc = &Prog_Options[option];
    Arena *frame = &state->frameArena;
    f32 s = m->scale, width = floorf(162.4f * s + 0.5f), height = floorf(21.6f * s + 0.5f), line = MaxF32(1, floorf(0.8f * s + 0.5f)), radius = floorf(4.0f * s + 0.5f);
    i32 value = Prog_OptionValue(state, option);
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
            f32 wide = floorf(155.2f * s + 0.5f), track = floorf(12.8f * s + 0.5f), knob = floorf(20.8f * s + 0.5f);
            f32 range = (f32)Max(desc->most - desc->least, 1), at = (f32)(value - desc->least) / range;
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(knob));
            UI_Block *slider = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(frame, "OptionSlider/%u", (u32)option)), UI_Flag_LeftMouseClickable | UI_Flag_TakesWheel);
            UI_Interaction drag = UI_InteractionFromBlock(slider);
            // The wheel over it steps it, a notch a step.
            i32 notches = Prog__WheelNotches(slider);
            if (notches) Prog__QueueOption(state, option, Max(desc->least, Min(desc->most, value + notches)), 0, false);
            if (drag.pressed || (drag.active && drag.isDragging)) {
                f32 along = UI__Clamp((UI->mouse.x - slider->rect.x0 - knob * 0.5f) / MaxF32(1, wide - knob), 0, 1);
                i32 wanted = desc->least + (i32)floorf(along * range + 0.5f);
                if (wanted != value) Prog__QueueOption(state, option, wanted, drag.pressOrder, true);
            }
            // Saved once, where it is let go.
            if (drag.released) Prog__QueueOption(state, option, value, drag.pressOrder, false);
            UI_Parent(slider) {
                f32 middle = floorf((knob - track) * 0.5f), knobX = floorf(at * (wide - knob));
                UI_SetNextFloatingOffset((UI_Vec2){ 0, middle });
                UI_SetNextPrefWidth(UI_Pixels(wide));
                UI_SetNextPrefHeight(UI_Pixels(track));
                UI_SetNextCornerRadius(track * 0.5f);
                UI_SetNextBorderThickness(line);
                UI_SetNextColors((UI_Colors){ .background = theme->track, .border = theme->outline });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                UI_SetNextFloatingOffset((UI_Vec2){ 0, middle });
                UI_SetNextPrefWidth(UI_Pixels(knobX + knob * 0.5f));
                UI_SetNextPrefHeight(UI_Pixels(track));
                UI_SetNextCornerRadius(track * 0.5f);
                UI_SetNextColors((UI_Colors){ .background = theme->selection });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                // The knob is solid in the color the fill shows over the track, which a translucent selection color would let show through it.
                R_Color fill = theme->selection, under = theme->track;
                R_Color solid = { fill.r * fill.a + under.r * (1 - fill.a), fill.g * fill.a + under.g * (1 - fill.a), fill.b * fill.a + under.b * (1 - fill.a), 1 };
                UI_SetNextFloatingOffset((UI_Vec2){ knobX, 0 });
                UI_SetNextPrefWidth(UI_Pixels(knob));
                UI_SetNextPrefHeight(UI_Pixels(knob));
                UI_SetNextCornerRadius(knob * 0.5f);
                UI_SetNextColors((UI_Colors){ .background = fill.a < 1 && under.a >= 1 ? solid : fill });
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
            }
        } break;
        case Prog_OptionControl_Expand: {
            Prog__Icon(state->colorsExpanded ? Icon_ChevronDown : Icon_ChevronRight, floorf(20.8f * s + 0.5f), height, theme->icon, 0.5f, m);
        } break;
        case Prog_OptionControl_Color: {
            // The color's hex digits in a field, and a swatch of it beside them, which a click opens the picker for.
            R_Color color = Prog_OptionColor(state, option);
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "OptionColor/%u", (u32)option));
            b32 hot = UI->hotKey == key;
            f32 field = floorf(96.0f * s + 0.5f), gap = floorf(6.4f * s + 0.5f);
            UI_SetNextPrefWidth(UI_Pixels(field));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .background = theme->inner, .border = theme->outline, .text = theme->text });
            UI_SetNextText(StrFormat(frame, "%06X", state->schemeGroups[option - Prog_Option_ColorClear]));
            UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText);
            Prog__Spacer(UI_Pixels(gap), UI_Pixels(height));
            UI_SetNextPrefWidth(UI_Pixels(width - field - gap));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .background = color, .border = hot ? theme->outlineHover : theme->outline });
            UI_Interaction interaction = UI_InteractionFromBlock(UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_LeftMouseClickable));
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_ColorPickerOpen, (u64)option, 0, interaction.clickOrder);
        } break;
        case Prog_OptionControl_Font: {
            // The family's name, which a click lets be chosen from the installed ones.
            const Prog_FontName *font = option == Prog_Option_InspectorFont ? &state->inspectorFont : &state->font;
            String name = font->length ? StrMake(font->text, font->length) : S("Default");
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "OptionFont/%u", (u32)option));
            b32 hot = UI->hotKey == key;
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .background = theme->inner, .border = hot ? theme->textDim : theme->inner, .text = font->length ? theme->text : theme->textDim });
            UI_SetNextText(Prog__Ellipsize(frame, m->font, name, width - 2 * radius));
            UI_Block *box = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText | UI_Flag_LeftMouseClickable | UI_Flag_Clip);
            UI_Interaction interaction = UI_InteractionFromBlock(box);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_FontPickerOpen, (u64)option, 0, interaction.clickOrder);
        } break;
        case Prog_OptionControl_Choice: {
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "OptionChoice/%u", (u32)option));
            b32 hot = UI->hotKey == key;
            b32 schemes = option == Prog_Option_ColorScheme || option == Prog_Option_LightScheme || option == Prog_Option_DarkScheme;
            b32 open = schemes && state->schemesOpen == option + 1;
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .background = open ? theme->selection : theme->track, .border = open ? theme->selection : hot ? theme->textDim : theme->outline });
            UI_Block *choice = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_LeftMouseClickable | UI_Flag_Clip);
            UI_Interaction interaction = UI_InteractionFromBlock(choice);
            u32 choiceCount = 0;
            const String *choices = Prog_OptionChoices(state, option, frame, &choiceCount);
            if (interaction.clicked && schemes) Prog_QueueCommand(state, Prog_CommandKind_SchemesOpen, (u64)option, 0, interaction.clickOrder);
            else if (interaction.clicked) Prog__QueueOption(state, option, (value + 1) % (i32)choiceCount, interaction.clickOrder, false);
            UI_Parent(choice) {
                f32 chevron = floorf(20.8f * s + 0.5f);
                Prog__Spacer(UI_Pixels(chevron), UI_Pixels(height));
                Prog__Label(Prog__OptionName(frame, option, choices[Min((u32)value, choiceCount - 1)]), UI_Flex(), height, 0, open ? theme->contentSelection : theme->text, 0.5f);
                Prog__Icon(open ? Icon_ChevronDown : Icon_ChevronRight, chevron, height, open ? theme->contentSelection : theme->icon, 0.5f, m);
            }
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
    UI_SetNextBorderThickness(1.2f * s);
    UI_SetNextColors((UI_Colors){ .background = washed, .border = theme->selection });
    UI_Block *entry = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_Clip | (hot ? UI_Flag_DrawBackground : 0) | (highlighted || hot ? UI_Flag_DrawBorder : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(entry);
    if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteRun, index, 0, interaction.clickOrder);
    UI_Parent(entry) {
        if (desc->control == Prog_OptionControl_Color) Prog__Spacer(UI_Pixels(floorf(24.0f * s + 0.5f)), UI_Pixels(face));
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(face));
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ 0, state->paletteCompact ? floorf((face - m->line) * 0.5f) : floorf(4.8f * s + 0.5f), 0, 0 });
        UI_Block *texts = UI_AddBlock(UI_Flag_Clip);
        UI_Parent(texts) {
            Prog__MarkedText(desc->label, NULL, m->font, m->line, option == Prog_Option_ColorGroups ? theme->textHeading : theme->text);
            if (!state->paletteCompact) {
                Prog__Spacer(UI_Flex(), UI_Pixels(floorf(2.4f * s + 0.5f)));
                Prog__MarkedText(desc->tooltip, NULL, m->detail, m->detail->lineHeight, theme->textDim);
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
    UI_SetNextBorderThickness(1.2f * s);
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill(), .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("TypesPopup")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_MouseClickable);
    UI_Parent(box) {
        UI_Block *field = Prog__Box(UI_Flex(), UI_Pixels(fieldHeight), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(field) {
            Prog__Spacer(UI_Pixels(floorf(8.0f * s + 0.5f)), UI_Pixels(fieldHeight));
            Prog__Icon(Icon_Search, floorf(24.0f * s + 0.5f), fieldHeight, theme->textDim, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(fieldHeight));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                f32 top = floorf((fieldHeight - m->line) * 0.5f);
                if (!state->typesSearch.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, top });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(StrFormat(frame, "Filter %u types...", state->typeCount));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->typesSearch, 0, top, true, 0, m);
            }
            // Checking every type shows them all as a filter would, and clearing them shows them all without one.
            UI_Interaction all = Prog__TextButton(S("TypesToggleAll"), Icon_SquareCheck, m->icons, UI_Pixels(button), fieldHeight, 0, theme->icon, theme->text, theme->iconDisabled,
                                                  toggled < state->typeCount, m);
            if (all.clicked) Prog_QueueCommand(state, Prog_CommandKind_TypesToggleAll, 0, 0, all.clickOrder);
            Prog__NoteTooltip(state, Prog_CommandKind_TypesToggleAll, S("Toggle All"));
            UI_Interaction none = Prog__TextButton(S("TypesUntoggleAll"), Icon_SquareX, m->icons, UI_Pixels(button), fieldHeight, 0, theme->icon, theme->text, theme->iconDisabled,
                                                   toggled > 0, m);
            if (none.clicked) Prog_QueueCommand(state, Prog_CommandKind_TypesUntoggleAll, 0, 0, none.clickOrder);
            Prog__NoteTooltip(state, Prog_CommandKind_TypesUntoggleAll, S("Untoggle All"));
            Prog__Spacer(UI_Pixels(floorf(4.0f * s + 0.5f)), UI_Pixels(fieldHeight));
            Prog__Box(UI_Pixels(line), UI_Pixels(floorf(m->line * 1.15f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
            Prog__Spacer(UI_Pixels(floorf(4.0f * s + 0.5f)), UI_Pixels(fieldHeight));
            UI_Interaction close = Prog__TextButton(S("TypesClose"), Icon_X, m->icons, UI_Pixels(button), fieldHeight, 0, theme->textDim, theme->text, theme->iconDisabled, true, m);
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
                UI_SetNextBorderThickness(1.2f * s);
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
        for (u64 i = 0; i < state->filesCount; ++i) state->filesWidest = MaxF32(state->filesWidest, R_TextWidth(m->font, state->filesItems[i].file.name));
    }
    f32 pad = floorf(6.4f * s + 0.5f), fieldHeight = floorf(32.0f * s + 0.5f), pitch = floorf(25.6f * s + 0.5f), line = MaxF32(1, floorf(1.6f * s + 0.5f));
    f32 radius = floorf(8.0f * s + 0.5f), button = floorf(28.0f * s + 0.5f), above = floorf(2.4f * s + 0.5f), iconSize = (f32)state->iconSize;
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
    UI_SetNextBorderThickness(1.2f * s);
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill(), .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("FilesPopup")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_MouseClickable);
    UI_Parent(box) {
        UI_Block *field = Prog__Box(UI_Flex(), UI_Pixels(fieldHeight), UI_Layout_Horizontal, (R_Color){0}, 0);
        UI_Parent(field) {
            Prog__Spacer(UI_Pixels(floorf(8.0f * s + 0.5f)), UI_Pixels(fieldHeight));
            Prog__Icon(Icon_Search, floorf(24.0f * s + 0.5f), fieldHeight, theme->textDim, 0, m);
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
                    UI_SetNextText(StrFormat(frame, "Filter %S files...", many));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->filesSearch, 0, top, true, 0, m);
            }
            UI_Interaction close = Prog__TextButton(S("FilesClose"), Icon_X, m->icons, UI_Pixels(button), fieldHeight, 0, theme->textDim, theme->text, theme->iconDisabled, true, m);
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
                    u8 *marks = typed.length ? ArenaPushArray(frame, entry->file.name.length + 1, u8) : NULL;
                    if (marks) Prog_FuzzyScore(entry->file.name, (String){0}, typed, marks, NULL);
                    UI_SetNextPrefWidth(UI_Flex());
                    UI_SetNextPrefHeight(UI_Pixels(pitch - line));
                    UI_SetNextChildrenLayout(UI_Layout_Vertical);
                    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf((pitch - line - m->line) * 0.5f), 0, 0 });
                    UI_Block *name = UI_AddBlock(UI_Flag_Clip);
                    UI_Parent(name) Prog__MarkedText(entry->file.name, marks, m->font, m->line, theme->text);
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
    b32 wanted = anchor.x1 > anchor.x0 && !state->paletteOpen && !state->menuOpen && (wheeled || over || UI->activeKey == sliderKey);
    f32 shown = UI_Animate(UI_KeyFromStr(0, S("ZoomPopupFade")), wanted ? 1.0f : 0.0f, 0);
    state->zoomShown = wanted || shown > 0.01f;
    if (!state->zoomShown) return;
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 s = m->scale;
    f32 width = floorf(166.4f * s + 0.5f), height = floorf(237.6f * s + 0.5f), radius = floorf(8.0f * s + 0.5f);
    f32 x = anchor.x1 - width, y = anchor.y0 - floorf(1.6f * s + 0.5f) - height;
    // The gap down to the button counts as the popup, so that the pointer can cross it.
    state->zoomBox = (R_Rect){ x, y, x + width, anchor.y0 };
    // Where a zoom's row or thumb sits from the box's top: a row's pitch apart up to M Icons at 30%, then evenly up to XL Icons at 100%.
    f32 pitch = floorf(20.0f * s + 0.5f), bottomRow = height - floorf(21.6f * s + 0.5f), topRow = floorf(20.0f * s + 0.5f);
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
    UI_SetNextBorderThickness(1.2f * s);
    UI_SetNextColors((UI_Colors){ .background = Prog__FloatingFill(), .border = theme->tabBorder });
    UI_Block *box = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ZoomPopup")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    Prog_View view = Prog_ZoomView(state->zoom);
    u32 lit = view != Prog_View_Icons ? (u32)view : state->zoom < Prog_ZoomLarge ? 3u : state->zoom < Prog_ZoomMax ? 4u : 5u;
    local_persist const String names[] = { SInit("Details"), SInit("List"), SInit("Columns"), SInit("M Icons"), SInit("L Icons"), SInit("XL Icons") };
    const String icons[] = { Icon_List, Icon_Menu, Icon_Columns3, Icon_Image, Icon_Image, Icon_Image };
    f32 places[] = { 0, 0.1f, 0.2f, 0.3f, 0.65f, 1 };
    u32 levels[] = { 0, Prog_ZoomList, Prog_ZoomColumns, Prog_ZoomIcons, Prog_ZoomLarge, Prog_ZoomMax };
    f32 inset = floorf(11.2f * s + 0.5f), rowWidth = floorf(120.0f * s + 0.5f), rowHeight = floorf(17.6f * s + 0.5f);
    UI_Parent(box) {
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
                Prog__Label(names[i], UI_Flex(), rowHeight, 0, theme->text, 0);
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
        // The slider: a track from XL Icons down to Details, filled below the thumb.
        f32 track = floorf(12.8f * s + 0.5f), knob = floorf(20.0f * s + 0.5f);
        f32 trackX = width - floorf(10.4f * s + 0.5f) - track, trackTop = topRow + floorf(1.6f * s + 0.5f), trackBottom = bottomRow - floorf(0.8f * s + 0.5f);
        // The thumb eases to a new zoom; it starts where the zoom is when the popup shows.
        f32 target = Prog__ZoomY(Prog__ZoomFraction(state->zoom));
        f32 thumb = floorf(UI_Animate(UI_KeyFromStr(0, S("ZoomPopupThumb")), target, target) + 0.5f);
        // Pressing or dragging along the slider sets the icon size it points at, or the nearest view below M Icons.
        UI_SetNextPrefWidth(UI_Pixels(knob + 2 * floorf(4.0f * s + 0.5f)));
        UI_SetNextPrefHeight(UI_Pixels(trackBottom - trackTop + knob));
        UI_SetNextFloatingOffset((UI_Vec2){ trackX + floorf((track - knob) * 0.5f) - floorf(4.0f * s + 0.5f), trackTop - floorf(knob * 0.5f) });
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
        UI_SetNextOpacity(opacity);
        UI_SetNextColors((UI_Colors){ .background = theme->track });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
        // Shorter than it is wide, the knob covers it, and drawn rounded at its width it would stand out above the knob.
        if (trackBottom - thumb >= track) {
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
        UI_SetNextColors((UI_Colors){ .background = theme->accent });
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
            Prog__Icon(Icon_Search, floorf(22.4f * s + 0.5f), field, theme->textDim, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(S("Select a layout..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f), true, floorf(4.0f * s + 0.5f), m);
            }
            UI_Interaction close = Prog__TextButton(S("PaletteCloseButton"), Icon_X, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                    theme->icon, theme->text, theme->iconDisabled, true, m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, close.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        f32 side = floorf(6.4f * s + 0.5f);
        UI_SetNextChildrenPadding((UI_Padding){ side, pad, side, pad });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(list) {
            if (!count) {
                Prog__Label(state->layoutNames.header.count ? S("No layout has that name") : S("No layouts saved yet; Save Layout keeps one"), UI_Flex(), row - line,
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
                UI_SetNextBorderThickness(1.2f * s);
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
                        UI_Interaction drop = Prog__TextButton(StrFormat(&state->frameArena, "LayoutDelete/%S", name), Icon_Trash2, m->icons, UI_Pixels(floorf(24.0f * s + 0.5f)), face, 0,
                                                               theme->text, theme->text, theme->iconDisabled, true, m);
                        if (drop.clicked) Prog_QueueCommand(state, Prog_CommandKind_LayoutDelete, i, 0, drop.clickOrder);
                        Prog__PopupTooltip(state, S("Delete layout."));
                    }
                }
                if (i + 1 < count) Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
            }
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        local_persist const char *const keys[] = { "Up", "Down", 0, "Enter", 0, "Escape", 0 };
        local_persist const char *const words[] = { 0, 0, "to navigate", 0, "to use", 0, "to dismiss" };
        Prog__PaletteHints(keys, words, CArrayCount(keys), footer, theme->background.a < 1 ? (R_Color){0} : theme->menu, radius, m);
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(1.2f * s);
    UI_SetNextOpacity(opacity);
    UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
}

// The font picker: the installed families by name in the palette's box, which the typed text narrows, the default first, and the one the
// option has highlighted when it opens; the rows scroll to keep the highlight in sight.
function void Prog__FontsPicker(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    R_Rect view = UI->root->rect;
    UI_SetNextPrefWidth(UI_Pixels(view.x1 - view.x0));
    UI_SetNextPrefHeight(UI_Pixels(view.y1 - view.y0));
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("PaletteCatcher")), UI_Flag_Floating | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, outside.pressOrder);
    u32 *matches = ArenaPushArray(&state->frameArena, state->fontCount + 1, u32);
    u32 count = Prog_FontMatches(state, matches);
    state->paletteHighlight = count ? Min(state->paletteHighlight, count - 1) : 0;
    f32 s = m->scale, line = m->separator;
    f32 field = floorf(39.2f * s + 0.5f), footer = floorf(31.2f * s + 0.5f), pad = floorf(4.0f * s + 0.5f), row = floorf(24.0f * s + 0.5f) + line;
    f32 top = 2 * m->bar + floorf(11.2f * s + 0.5f), margin = floorf(32.8f * s + 0.5f), bottom = floorf(60.0f * s + 0.5f);
    f32 room = view.y1 - view.y0 - top - bottom - field - 2 * line - footer - 2 * pad;
    u32 visible = (u32)MaxF32(floorf((room + line) / row), 1.0f);
    if (!state->paletteWheeled && state->paletteHighlight < state->paletteScroll) state->paletteScroll = state->paletteHighlight;
    if (!state->paletteWheeled && state->paletteHighlight >= state->paletteScroll + visible) state->paletteScroll = state->paletteHighlight - visible + 1;
    state->paletteScroll = count > visible ? Min(state->paletteScroll, count - visible) : 0;
    u32 first = state->paletteScroll, shown = Min(count - Min(first, count), visible);
    f32 width = floorf(Prog__PaletteWidth(view.x1 - view.x0, margin, s));
    f32 listHeight = (f32)Max(shown, 1u) * row - line + 2 * pad;
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
    const Prog_FontName *current = state->fontOption == Prog_Option_InspectorFont ? &state->inspectorFont : &state->font;
    UI_Parent(panel) {
        UI_SetNextChildrenPadding((UI_Padding){ floorf(12.8f * s + 0.5f), 0, floorf(8.0f * s + 0.5f), 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, theme->inner, 0);
        box->props.cornerRadius = radius;
        box->props.shape = R_Shape_SquareBottomLeft | R_Shape_SquareBottomRight;
        UI_Parent(box) {
            Prog__Icon(Icon_Search, floorf(22.4f * s + 0.5f), field, theme->textDim, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(state->fontOption == Prog_Option_InspectorFont ? S("Select the inspector's font...") : S("Select a font..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f), true, floorf(4.0f * s + 0.5f), m);
            }
            UI_Interaction close = Prog__TextButton(S("PaletteCloseButton"), Icon_X, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                    theme->icon, theme->text, theme->iconDisabled, true, m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, close.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        f32 side = floorf(6.4f * s + 0.5f);
        UI_SetNextChildrenPadding((UI_Padding){ side, pad, side, pad });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(list) {
            if (!count) Prog__Label(state->fontsJob ? S("Listing the installed fonts...") : S("No font has that name"), UI_Flex(), row - line, floorf(8.0f * s + 0.5f), theme->textDim, 0);
            for (u32 i = first; i < first + shown; ++i) {
                b32 standard = matches[i] == UINT32_MAX;
                String name = standard ? S("Default") : state->fontNames[matches[i]];
                b32 chosen = standard ? !current->length : Prog__EqualFolded(name, StrMake(current->text, current->length));
                UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Font/%S", name));
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
                UI_SetNextBorderThickness(1.2f * s);
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
                    UI_Parent(texts) Prog__MarkedText(name, marks, m->font, m->line, standard && !highlighted ? theme->textDim : theme->text);
                    // The family the option has now.
                    if (chosen) Prog__Icon(Icon_Check, floorf(24.0f * s + 0.5f), face, theme->text, 0.5f, m);
                }
                if (i + 1 < first + shown) Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
            }
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        local_persist const char *const keys[] = { "Up", "Down", 0, "Enter", 0, "Escape", 0 };
        local_persist const char *const words[] = { 0, 0, "to navigate", 0, "to use", 0, "to dismiss" };
        Prog__PaletteHints(keys, words, CArrayCount(keys), footer, theme->background.a < 1 ? (R_Color){0} : theme->menu, radius, m);
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(1.2f * s);
    UI_SetNextOpacity(opacity);
    UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
}

// The schemes' dropdown under the scheme option it opened from: each scheme's background color in a circle, its name, and a ring that the
// shown one's is filled with a dot; the highlighted row offers to fork its scheme and, for a custom one, to delete it. A line parts the
// custom schemes from the built-in ones.
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
    f32 s = m->scale, row = floorf(25.6f * s + 0.5f), pad = floorf(6.0f * s + 0.5f), inset = floorf(6.4f * s + 0.5f), circle = floorf(16.0f * s + 0.5f);
    f32 slot = floorf(24.8f * s + 0.5f), ring = floorf(1.6f * s + 0.5f), line = MaxF32(1, floorf(0.8f * s + 0.5f)), parting = floorf(1.6f * s + 0.5f);
    b32 parted = count > Prog_BuiltInSchemes;
    f32 width = floorf(205.6f * s + 0.5f), height = 2 * pad + (f32)count * row + (parted ? parting : 0), radius = floorf(6.4f * s + 0.5f);
    // Under the button, or over it where the window has no room below.
    f32 x = MinF32(button->rect.x0 - view.x0, view.x1 - view.x0 - width), y = button->rect.y1 - view.y0 + floorf(1.6f * s + 0.5f);
    if (y + height > view.y1 - view.y0) y = MaxF32(0, button->rect.y0 - view.y0 - floorf(1.6f * s + 0.5f) - height);
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
        for (u32 i = 0; i < count; ++i) {
            if (parted && i == Prog_BuiltInSchemes) {
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(parting));
                UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
                UI_AddBlock(UI_Flag_DrawBackground);
            }
            UI_Key key = UI_KeyFromStr(0, StrFormat(frame, "Scheme/%u", i));
            b32 lit = pointed == UINT32_MAX ? i == state->schemesHighlight : i == pointed, picked = i == shown;
            UI_Block *place = Prog__Box(UI_Flex(), UI_Pixels(row), UI_Layout_Horizontal, (R_Color){0}, 0);
            UI_Parent(place) {
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
                    u32 groups[Prog_ColorGroupCount];
                    Prog__UseScheme(state, names[i], groups);
                    Prog__Spacer(UI_Pixels(floorf(16.0f * s + 0.5f) - inset), UI_Pixels(row));
                    UI_SetNextPrefWidth(UI_Pixels(circle));
                    UI_SetNextPrefHeight(UI_Pixels(circle));
                    UI_SetNextCornerRadius(circle * 0.5f);
                    UI_SetNextBorderThickness(line);
                    UI_SetNextColors((UI_Colors){ .background = Prog__HexColor(groups[Prog_Option_ColorBackground - Prog_Option_ColorClear]), .border = theme->outline });
                    UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder);
                    Prog__Spacer(UI_Pixels(floorf(8.0f * s + 0.5f)), UI_Pixels(row));
                    R_Color ink = lit ? theme->contentSelection : theme->text;
                    Prog__Label(Prog__Ellipsize(frame, m->font, Prog__OptionName(frame, option, names[i]), width - 2 * inset - 4 * slot), UI_Flex(), row, 0, ink, 0);
                    if (lit) {
                        UI_Interaction fork = Prog__TextButton(StrFormat(frame, "Scheme/%u/Fork", i), Icon_Copy, m->icons, UI_Pixels(slot), row, 0, ink, theme->text, theme->iconDisabled, true, m);
                        if (fork.clicked) Prog_QueueCommand(state, Prog_CommandKind_SchemeFork, i, 0, fork.clickOrder);
                        Prog__PopupTooltip(state, S("Create an editable copy of this color scheme."));
                        if (i >= Prog_BuiltInSchemes) {
                            UI_Interaction drop = Prog__TextButton(StrFormat(frame, "Scheme/%u/Delete", i), Icon_Trash2, m->icons, UI_Pixels(slot), row, 0, ink, theme->text, theme->iconDisabled, true, m);
                            if (drop.clicked) Prog_QueueCommand(state, Prog_CommandKind_SchemeDelete, i, 0, drop.clickOrder);
                            Prog__PopupTooltip(state, S("Delete color scheme."));
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

// The color picker: the group's name over a swatch of the color, and a slider for each of its hue, saturation and brightness, then its red,
// green and blue, which change the color as they move.
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
    f32 s = m->scale, pad = floorf(12.0f * s + 0.5f), row = floorf(28.0f * s + 0.5f), head = floorf(32.0f * s + 0.5f), swatch = floorf(40.0f * s + 0.5f);
    f32 width = floorf(340.0f * s + 0.5f), height = head + swatch + pad + 6 * row + pad, radius = floorf(8.0f * s + 0.5f);
    f32 x = floorf((view.x1 - view.x0 - width) * 0.5f), y = floorf((view.y1 - view.y0 - height) * 0.4f);
    R_Color color = Prog_OptionColor(state, (Prog_Option)state->colorOption);
    Prog__DropShadow(x, y, width, height, radius, 1, m);
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, y });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ pad, 0, pad, pad });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(1.2f * s);
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->tabBorder });
    UI_Block *panel = UI_AddBlockFromKey(UI_KeyFromStr(0, S("ColorPicker")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
    UI_Parent(panel) {
        UI_Block *title = Prog__Box(UI_Flex(), UI_Pixels(head), UI_Layout_Horizontal, theme->menu, 0);
        UI_Parent(title) {
            Prog__Label(Prog_Options[state->colorOption].label, UI_Flex(), head, 0, theme->text, 0);
            UI_Interaction close = Prog__TextButton(S("ColorPickerClose"), Icon_X, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                    theme->icon, theme->text, theme->iconDisabled, true, m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_ColorPickerClose, 0, 0, close.clickOrder);
        }
        UI_SetNextPrefWidth(UI_Flex());
        UI_SetNextPrefHeight(UI_Pixels(swatch));
        UI_SetNextCornerRadius(floorf(4.0f * s + 0.5f));
        UI_SetNextBorderThickness(1.2f * s);
        UI_SetNextColors((UI_Colors){ .background = color, .border = theme->outline });
        UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder);
        Prog__Spacer(UI_Flex(), UI_Pixels(pad));
        local_persist const String labels[6] = { SInit("H:"), SInit("S:"), SInit("B:"), SInit("R:"), SInit("G:"), SInit("B:") };
        f32 values[6] = { state->pickerHue, state->pickerSaturation, state->pickerBrightness, color.r, color.g, color.b };
        i32 shown[6] = { (i32)(state->pickerHue * 360 + 0.5f), (i32)(state->pickerSaturation * 100 + 0.5f), (i32)(state->pickerBrightness * 100 + 0.5f),
                         (i32)(color.r * 255 + 0.5f), (i32)(color.g * 255 + 0.5f), (i32)(color.b * 255 + 0.5f) };
        f32 track = floorf(10.4f * s + 0.5f), knob = floorf(16.0f * s + 0.5f), label = floorf(24.0f * s + 0.5f), number = floorf(36.0f * s + 0.5f);
        for (u32 c = 0; c < 6; ++c) {
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_Block *line = Prog__Box(UI_Flex(), UI_Pixels(row), UI_Layout_Horizontal, theme->menu, 0);
            UI_Parent(line) {
                Prog__Label(labels[c], UI_Pixels(label), row, 0, theme->textDim, 0);
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(knob));
                UI_Block *slider = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "ColorPickerSlider/%u", c)), UI_Flag_LeftMouseClickable);
                UI_Interaction drag = UI_InteractionFromBlock(slider);
                f32 wide = MaxF32(1, slider->rect.x1 - slider->rect.x0);
                if (drag.pressed || (drag.active && drag.isDragging)) {
                    f32 along = UI__Clamp((UI->mouse.x - slider->rect.x0 - knob * 0.5f) / MaxF32(1, wide - knob), 0, 1);
                    Prog_QueueCommand(state, Prog_CommandKind_ColorPickerSet, c, 0, drag.pressOrder)->target = (u64)(along * 1e6f);
                }
                UI_Parent(slider) {
                    f32 middle = floorf((knob - track) * 0.5f), knobX = floorf(UI__Clamp(values[c], 0, 1) * (wide - knob));
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, middle });
                    UI_SetNextPrefWidth(UI_Pixels(wide));
                    UI_SetNextPrefHeight(UI_Pixels(track));
                    UI_SetNextCornerRadius(track * 0.5f);
                    UI_SetNextColors((UI_Colors){ .background = theme->track });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, middle });
                    UI_SetNextPrefWidth(UI_Pixels(knobX + knob * 0.5f));
                    UI_SetNextPrefHeight(UI_Pixels(track));
                    UI_SetNextCornerRadius(track * 0.5f);
                    UI_SetNextColors((UI_Colors){ .background = theme->accent });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                    UI_SetNextFloatingOffset((UI_Vec2){ knobX, 0 });
                    UI_SetNextPrefWidth(UI_Pixels(knob));
                    UI_SetNextPrefHeight(UI_Pixels(knob));
                    UI_SetNextCornerRadius(knob * 0.5f);
                    UI_SetNextColors((UI_Colors){ .background = theme->text });
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);
                }
                Prog__Label(StrFormat(&state->frameArena, "%d", shown[c]), UI_Pixels(number), row, 0, theme->text, 1);
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
    UI_SetNextBorderThickness(hot ? 1.2f * s : 0);
    UI_SetNextColors((UI_Colors){ .background = face, .border = theme->textDim, .text = able ? theme->text : theme->textDim });
    UI_SetNextText(label);
    UI_Block *button = UI_AddBlockFromKey(id, UI_Flag_DrawBackground | UI_Flag_DrawText | (hot ? UI_Flag_DrawBorder : 0) | (able ? UI_Flag_LeftMouseClickable : 0));
    return able && UI_InteractionFromBlock(button).clicked;
}

// The Save Layout box: its title, the name being typed, and Save, Update, which only a taken name allows, and Cancel.
function void Prog__LayoutSaveBox(Prog_State *state, const Prog_Metrics *m)
{
    if (!state->layoutSaving) return;
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
        UI_Parent(title) Prog__Label(S("Save layout"), UI_Flex(), head, 0, theme->text, 0.5f);
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        UI_SetNextChildrenPadding((UI_Padding){ floorf(8.8f * s + 0.5f), 0, floorf(8.8f * s + 0.5f), 0 });
        UI_Block *box = Prog__Box(UI_Flex(), UI_Pixels(field), UI_Layout_Horizontal, theme->inner, UI_Flag_Clip);
        UI_Parent(box) {
            if (!state->layoutName.length) {
                UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f) });
                UI_SetNextPrefHeight(UI_Pixels(m->line));
                UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                UI_SetNextText(S("Type a new or existing layout name to update..."));
                UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
            }
            Prog__EditContents(&state->layoutName, 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f), true, floorf(4.0f * s + 0.5f), m);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        // The buttons end as far from the outline as from the inside of the box.
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, floorf(11.2f * s + 0.5f), 0 });
        UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
        UI_Block *buttons = Prog__Box(UI_Flex(), UI_Pixels(band), UI_Layout_Horizontal, theme->background, 0);
        UI_Parent(buttons) {
            Prog__Spacer(UI_Flex(), UI_Pixels(band));
            f32 apart = floorf(6.4f * s + 0.5f);
            if (Prog__LayoutButton(state, S("LayoutSaveSave"), S("Save"), name.length != 0, m)) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 0, 0, 0);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(band));
            if (Prog__LayoutButton(state, S("LayoutSaveUpdate"), S("Update"), taken, m)) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 1, 0, 0);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(band));
            if (Prog__LayoutButton(state, S("LayoutSaveCancel"), S("Cancel"), true, m)) Prog_QueueCommand(state, Prog_CommandKind_LayoutSaveButton, 2, 0, 0);
        }
    }
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(1.2f * s);
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
    b32 deleting = state->layoutAsk == Prog_LayoutAsk_Delete || state->layoutAsk == Prog_LayoutAsk_DeleteScheme;
    String title = state->layoutAsk == Prog_LayoutAsk_DeleteScheme ? StrFormat(&state->frameArena, "Delete the custom color scheme \"%S\"?", name)
                 : deleting ? StrFormat(&state->frameArena, "Delete the layout \"%S\"?", name) : StrFormat(&state->frameArena, "Overwrite the existing layout \"%S\"?", name);
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
            UI_Interaction cancel = Prog__TextButton(S("LayoutAskCancel"), S("Cancel"), NULL, UI_TextContent(), m->bar * 0.8f, m->inset, theme->textHeading, theme->text, theme->textDim, true, m);
            if (cancel.clicked) Prog_QueueCommand(state, Prog_CommandKind_LayoutAnswer, 0, 0, cancel.clickOrder);
            Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
            UI_Interaction confirm = Prog__TextButton(S("LayoutAskConfirm"), deleting ? S("Delete") : S("Overwrite"), NULL, UI_TextContent(), m->bar * 0.8f, m->inset,
                                                      theme->danger, theme->danger, theme->textDim, true, m);
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
    if (state->paletteMode == Prog_PaletteMode_Fonts) {
        Prog__FontsPicker(state, m);
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
            Prog__Icon(Icon_Search, floorf(22.4f * s + 0.5f), field, theme->textDim, 0, m);
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(field));
            UI_Block *text = UI_AddBlock(UI_Flag_Clip);
            UI_Parent(text) {
                if (!state->palette.length) {
                    // Placed as the edit's own text is.
                    UI_SetNextFloatingOffset((UI_Vec2){ 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f) });
                    UI_SetNextPrefHeight(UI_Pixels(m->line));
                    UI_SetNextColors((UI_Colors){ .text = theme->textDim });
                    UI_SetNextText(options ? S("Select an option...") : S("Select a command..."));
                    UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawText);
                }
                Prog__EditContents(&state->palette, 0, floorf((field - m->line) * 0.5f + 0.8f * s + 0.5f), true, floorf(4.0f * s + 0.5f), m);
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
            Prog__NoteTooltip(state, Prog_CommandKind_PaletteToggleDetails, S("Toggle Extra Details"));
            UI_Interaction toggle = UI_InteractionFromBlock(details);
            if (toggle.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteToggleDetails, 0, 0, toggle.clickOrder);
            f32 apart = floorf(6.4f * s + 0.5f);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(field));
            Prog__Box(UI_Pixels(m->separator), UI_Pixels(floorf(m->line * 1.25f + 0.5f)), UI_Layout_Horizontal, theme->tabBorder, 0);
            Prog__Spacer(UI_Pixels(apart), UI_Pixels(field));
            UI_Interaction close = Prog__TextButton(S("PaletteCloseButton"), Icon_X, m->icons, UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0,
                                                    theme->icon, theme->text, theme->iconDisabled, true, m);
            if (close.clicked) Prog_QueueCommand(state, Prog_CommandKind_PaletteClose, 0, 0, close.clickOrder);
        }
        Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->tabBorder, 0);
        local_persist const String groupNames[] = { SInit("Quick"), SInit("Folder"), SInit("View"), SInit("Sort"), SInit("Inspector"), SInit("Panels"), SInit("Tabs"), SInit("Go To"), SInit("Misc"),
                                                    SInit("Options"), SInit("Rename"), SInit("Text"), SInit("Scroll") };
        const String groupIcons[] = { Icon_StarFilled, Icon_Folder, Icon_Image, Icon_ArrowDownAZ, Icon_ScanEye, Icon_LayoutPanelLeft, Icon_AppWindow,
                                                    Icon_FolderSearch, Icon_Share2, Icon_SlidersHorizontal, Icon_TextCursorInput, Icon_TextCursorInput, Icon_ChevronsUpDown };
        static_assert(CArrayCount(groupNames) == Prog_CommandGroup_Count && CArrayCount(groupIcons) == Prog_CommandGroup_Count, "a name and an icon for every group");
        // Room on the right for the scroll bar while there is one.
        f32 bar = floorf(6.4f * s + 0.5f), side = floorf(6.4f * s + 0.5f), gutter = count > visible ? floorf(19.2f * s + 0.5f) : side;
        UI_SetNextChildrenPadding((UI_Padding){ side, pad - floorf(lag), gutter, pad + floorf(lag) });
        UI_Block *list = Prog__Box(UI_Flex(), UI_Pixels(listHeight), UI_Layout_Vertical, (R_Color){0}, UI_Flag_Clip);
        UI_Parent(list) {
            for (u32 i = first; i < first + shown; ++i) {
                if (options) {
                    Prog__OptionRow(state, (Prog_Option)found[i], i, i == state->paletteHighlight, row - line, m);
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
                UI_SetNextBorderThickness(1.2f * s);
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
                        Prog__Label(groupNames[at.group], UI_Flex(), face, 0, theme->text, 0);
                        Prog__Icon(open ? Icon_ChevronDown : Icon_ChevronRight, floorf(m->line * 1.2f), face, icon, 0.5f, m);
                    } else {
                        String label = Prog_CommandLabel((Prog_CommandKind)at.kind), tooltip = Prog_CommandTooltip((Prog_CommandKind)at.kind);
                        if (searching) {
                            // A command found by searching shows its group in front of it.
                            Prog__Icon(groupIcons[at.group], slot, face, icon, 0, m);
                            Prog__Label(groupNames[at.group], UI_Pixels(floorf(63.2f * s + 0.5f)), face, 0, named, 0);
                            Prog__Icon(Icon_ChevronRight, floorf(20.8f * s + 0.5f), face, icon, 0, m);
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
                            Prog__PopupTooltip(state, quick ? S("Remove from Quick.") : S("Add to Quick."));
                            if (Prog_HotkeysChanged(state, (Prog_CommandKind)at.kind)) {
                                UI_Interaction restore = Prog__TextButton(StrFormat(&state->frameArena, "PaletteRestore/%u", (u32)at.kind), Icon_RotateCcw, m->icons, UI_Pixels(slot), face, 0,
                                                                          theme->text, theme->text, theme->iconDisabled, true, m);
                                if (restore.clicked) Prog_QueueCommand(state, Prog_CommandKind_HotkeyRestore, at.kind, 0, restore.clickOrder);
                                Prog__PopupTooltip(state, S("Restore default hotkeys."));
                            }
                            UI_Interaction add = Prog__TextButton(StrFormat(&state->frameArena, "PaletteAdd/%u", (u32)at.kind), Icon_Plus, m->icons, UI_Pixels(slot), face, 0,
                                                                  theme->text, theme->text, theme->iconDisabled, true, m);
                            if (add.clicked) Prog_QueueCommand(state, Prog_CommandKind_HotkeyRecord, at.kind, 0, add.clickOrder)->target = Prog_NewHotkeySlot;
                            Prog__PopupTooltip(state, S("Assign new hotkey."));
                        }
                        Prog_HotkeyChip chips[4];
                        u32 chipCount = Prog_HotkeyChips(&state->frameArena, state, (Prog_CommandKind)at.kind, chips, CArrayCount(chips));
                        for (u32 c = 0; c < chipCount; ++c) {
                            Prog__Spacer(UI_Pixels(floorf(3.2f * s + 0.5f)), UI_Pixels(face));
                            Prog__HotkeyChipButton(state, S("Palette"), (Prog_CommandKind)at.kind, &chips[c], face, highlighted, m);
                        }
                    }
                }
                if (i + 1 < first + shown) Prog__Box(UI_Flex(), UI_Pixels(line), UI_Layout_Horizontal, theme->separator, 0);
            }
            if (count > visible) {
                // The scroll bar: a thumb as long as the share of rows shown, where they are among all of them.
                f32 track = listHeight - 2 * pad, thumb = MaxF32(floorf(track * (f32)visible / (f32)count), row);
                f32 offset = floorf((track - thumb) * scrolled / (f32)(count - visible));
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
        local_persist const char *const words[] = { 0, 0, "to navigate", 0, "to use", 0, "to dismiss" };
        // In its own band, unless the box is already in the menus' color, which a second coat would darken.
        Prog__PaletteHints(keys, words, CArrayCount(keys), footer, theme->background.a < 1 ? (R_Color){0} : theme->menu, radius, m);
    }
    // The outline over everything inside, so that rows do not cover it.
    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x, top });
    UI_SetNextCornerRadius(radius);
    UI_SetNextBorderThickness(1.2f * s);
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
        b32 asking = transfer->job && atomic_load(&progress->asking), paused = atomic_load(&progress->paused);
        u64 items = atomic_load(&progress->totalItems), doneItems = atomic_load(&progress->doneItems);
        u64 bytes = atomic_load(&progress->totalBytes), doneBytes = atomic_load(&progress->doneBytes);
        // Windows shows its own progress for copies outside archives, which leave the counts at zero and never show here.
        if (!transfer->job || !items || (!asking && now - transfer->started < UINT64_C(400000))) continue;
        String target = StrMake((const char *)transfer->target, transfer->targetLength);
        f32 height = 2 * pad + m->row * 3 + (asking ? m->bar : 0);
        bottom -= height + m->gap;
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(height));
        UI_SetNextFloatingOffset((UI_Vec2){ view.x1 - view.x0 - width - m->inset, floorf(bottom) });
        UI_SetNextChildrenLayout(UI_Layout_Vertical);
        UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextBlur(Prog__FloatingBlur(m));
        UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = asking ? theme->accent : theme->separator });
        UI_Block *card = UI_AddBlockFromKey(UI_KeyFromStr(0, StrFormat(&state->frameArena, "Transfer/%u", i)),
                                            UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_LeftMouseClickable);
        UI_Parent(card) {
            UI_Block *head = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->menu, 0);
            UI_Parent(head) {
                String title = transfer->opening ? StrFormat(&state->frameArena, "Extracting %S to open it", target)
                             : StrFormat(&state->frameArena, "%s %" PRIu64 " item%s to %S", transfer->move ? "Moving" : "Copying", items, items == 1 ? "" : "s", target);
                Prog__Label(title, UI_Flex(), m->row, 0, theme->text, 0);
                // Lucide draws these larger than its cross, which the smaller icons bring to its size.
                UI_Interaction pause = Prog__TextButton(StrFormat(&state->frameArena, "TransferPause/%u", i), paused ? Icon_Play : Icon_Pause, m->iconsSmall, UI_Pixels(m->row), m->row, 0,
                                                        theme->icon, theme->text, theme->iconDisabled, true, m);
                if (pause.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferPause, i, 0, pause.clickOrder);
                // Washed red, as the window's close button.
                UI_Interaction cancel = Prog__WashedButton(StrFormat(&state->frameArena, "TransferCancel/%u", i), Icon_X, m->icons, UI_Pixels(m->row), m->row, 0,
                                                           theme->icon, theme->text, theme->iconDisabled, theme->danger, true, m);
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
                    // Only where more names may be taken.
                    if (items > 1) {
                        UI_Interaction all = Prog__TextButton(StrFormat(&state->frameArena, "TransferAll/%u", i), transfer->answerAll ? S("For all: on") : S("For all: off"), NULL, UI_TextContent(),
                                                              m->bar * 0.8f, m->inset * 0.75f, transfer->answerAll ? theme->textMatch : theme->textDim, theme->text, theme->textDim, true, m);
                        if (all.clicked) Prog_QueueCommand(state, Prog_CommandKind_TransferAnswerAll, i, 0, all.clickOrder);
                    }
                }
            }
            // Bytes where there are any to copy; a move within one file system only counts its items.
            f32 done = bytes ? (f32)((f64)doneBytes / (f64)bytes) : (f32)((f64)doneItems / (f64)items);
            if (!asking) {
                UI_Block *now = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->menu, 0);
                UI_Parent(now) {
                    Prog__Label(Prog__TransferName(&state->frameArena, progress), UI_Flex(), m->row, 0, theme->textDim, 0);
                    Prog__Label(StrFormat(&state->frameArena, paused ? "Paused at %u%%" : "%u%%", (u32)(UI__Clamp(done, 0, 1) * 100)), UI_TextContent(), m->row, m->gap, theme->text, 1);
                }
            }
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
                        UI_SetNextColors((UI_Colors){ .background = paused ? theme->textDim : theme->accent });
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
    UI_Block *catcher = UI_AddBlockFromKey(UI_KeyFromStr(0, S("DeleteCatcher")), UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_MouseClickable | UI_Flag_TakesWheel);
    UI_Interaction outside = UI_InteractionFromBlock(catcher);
    if (outside.pressed) Prog_QueueCommand(state, Prog_CommandKind_DeleteCancel, 0, 0, outside.pressOrder);
    String title = state->confirmEmptyTrash ? StrFormat(&state->frameArena, "Permanently delete everything in the %S?", Prog_PathName(state, state->confirmPaths[0]))
                 : state->confirmCount == 1 ? StrFormat(&state->frameArena, "Permanently delete \"%S\"?", Prog__FolderName(state->confirmPaths[0]))
                                            : StrFormat(&state->frameArena, "Permanently delete %" PRIu64 " items?", state->confirmCount);
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
// the `target`th washed in the selection color and outlined with it, more strongly while the pointer is over the zone.
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
    UI_SetNextBorderThickness(1.2f * m->scale);
    UI_SetNextColors((UI_Colors){ .background = theme->background, .border = theme->tabBorder });
    UI_Block *frame = UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
    UI_Parent(frame) {
        f32 innerWidth = width - 2 * pad, innerHeight = height - 2 * pad;
        f32 part = floorf(((across ? innerWidth : innerHeight) - (f32)(parts - 1) * pad) / (f32)parts);
        for (u32 i = 0; i < parts; ++i) {
            R_Color fill = theme->selection;
            fill.a *= i == target ? (hot ? 2 : 1) * theme->hoverAmount.r : 0;
            UI_SetNextFloatingOffset(across ? (UI_Vec2){ pad + (f32)i * (part + pad), pad } : (UI_Vec2){ pad, pad + (f32)i * (part + pad) });
            UI_SetNextPrefWidth(UI_Pixels(across ? part : innerWidth));
            UI_SetNextPrefHeight(UI_Pixels(across ? innerHeight : part));
            UI_SetNextCornerRadius(radius);
            UI_SetNextBorderThickness(1.2f * m->scale);
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
    String action = state->tabDrop.kind == Prog_TabDrop_Window ? (copy ? S("Copy to a new window") : S("Move to a new window (Ctrl to copy)"))
                  : copy ? S("Copy") : S("Move (Ctrl to copy)");
    f32 width = floorf(m->line * 12.0f), height = floorf(m->bar * 0.85f), radius = floorf(8.0f * scale + 0.5f);
    // A translucent theme's panels would leave it unreadable over the list, so it takes the menus' color there.
    R_Color face = theme->panel.a < 1 ? theme->menu : theme->panel, foot = theme->background.a < 1 ? theme->menu : theme->background;
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
        UI_SetNextPrefWidth(UI_Pixels(width));
        UI_SetNextPrefHeight(UI_Pixels(m->row));
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_SetNextBorderThickness(m->separator);
        UI_SetNextColors((UI_Colors){ .background = foot, .border = theme->tabBorder, .text = theme->text });
        UI_SetNextText(action);
        UI_AddBlock(UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText);
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
    b32 dark = theme->text.r + theme->text.g + theme->text.b > 1.5f;
    switch (kind) {
        case Prog_Token_Keyword: return dark ? (R_Color)Prog_Rgb(198, 120, 221) : (R_Color)Prog_Rgb(166, 38, 164);
        case Prog_Token_String: return dark ? (R_Color)Prog_Rgb(152, 195, 121) : (R_Color)Prog_Rgb(64, 140, 60);
        case Prog_Token_Comment: return theme->textDim;
        case Prog_Token_Number: return dark ? (R_Color)Prog_Rgb(209, 154, 102) : (R_Color)Prog_Rgb(152, 104, 1);
        case Prog_Token_Heading: return dark ? (R_Color)Prog_Rgb(97, 175, 239) : (R_Color)Prog_Rgb(1, 100, 200);
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


// A file's bytes as hex rows, as many to a row as fit, zeros dimmed and bytes that are not printable characters in the match color.
// Only the rows in sight get blocks, and the pages they are in are read when first in sight.
function void Prog__InspectorHex(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    const R_Font *font = state->monoFont ? state->monoFont : m->font;
    f32 s = m->scale, size = (f32)(state->inspectorFontSize ? state->inspectorFontSize : Prog_InspectorFontSize);
    f32 pitch = floorf((size + 6.8f) * s + 0.5f), cell = R_TextWidth(font, S("M"));
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    f32 margin = floorf(32.8f * s + 0.5f), left = floorf(9.6f * s + 0.5f);
    UI_SetNextChildrenPadding((UI_Padding){ 0, floorf(13.6f * s + 0.5f), margin, floorf(4.0f * s + 0.5f) });
    UI_Block *view = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorText")), UI_Flag_Clip | UI_Flag_Scrollable);
    if (state->inspectorResetScroll) {
        view->scrollOffset = view->scrollShown = (UI_Vec2){0};
        state->inspectorResetScroll = false;
    }
    if (!UI->animate) view->scrollShown = view->scrollOffset;
    // A view new this frame has no size yet, so its rows in sight come in the next.
    if (view->rect.y1 <= view->rect.y0) UI_KeepAnimating();
    u64 shownBytes = Min(state->hexSize, state->hexPageCount * Prog_HexPageBytes);
    u32 places = Prog_HexPlaces(state->hexSize);
    u32 perLine = Prog_HexPerLine(places, (view->rect.x1 - view->rect.x0 - left - margin) / cell);
    u64 rows = (shownBytes + perLine - 1) / perLine, cutRows = Min(rows, (u64)Prog_HexRows);
    b32 cut = cutRows * perLine < state->hexSize;
    u64 rowCount = cutRows + (cut ? 1 : 0);
    f32 from = MinF32(view->scrollOffset.y, view->scrollShown.y), to = MaxF32(view->scrollOffset.y, view->scrollShown.y) + (view->rect.y1 - view->rect.y0);
    u64 first = Min((u64)MaxF32(0, floorf(from / pitch)), rowCount), last = Min((u64)MaxF32(0, ceilf(to / pitch)) + 1, rowCount);
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
                if (row >= cutRows) {
                    UI_SetNextFont(font);
                    Prog__Label(S("..."), UI_TextContent(), pitch, 0, theme->textDim, 0);
                    continue;
                }
                u64 offset = row * perLine, count = Min((u64)perLine, state->hexSize - offset);
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
                String text = Prog_HexRowText(frame, bytes, count, offset, perLine, places);
                UI_SetNextFont(font);
                Prog__Label(StrFromTo(text, 0, places), UI_Pixels((f32)(places + 2) * cell), pitch, 0, theme->textDim, 0);
                // Runs of bytes of one kind share a label, in the hex columns and then in the characters.
                for (u32 part = 0; part < 2; ++part) {
                    for (u64 at = 0; at < count;) {
                        u32 kind = bytes[at] == 0 ? 0 : bytes[at] >= 0x20 && bytes[at] < 0x7F ? 1 : 2;
                        u64 end = at + 1;
                        while (end < count && (bytes[end] == 0 ? 0u : bytes[end] >= 0x20 && bytes[end] < 0x7F ? 1u : 2u) == kind) ++end;
                        u64 start = part ? Prog_HexCharColumn(places, perLine, at) : Prog_HexByteColumn(places, at);
                        // The last hex run takes the spaces up to the characters, which keeps a short last row's characters under those above.
                        u64 stop = part ? Prog_HexCharColumn(places, perLine, end) : end == count ? Prog_HexCharColumn(places, perLine, 0) : Prog_HexByteColumn(places, end);
                        R_Color color = kind == 0 ? theme->textDim : kind == 1 ? theme->text : theme->textMatch;
                        UI_SetNextFont(font);
                        Prog__Label(StrFromTo(text, start, (i64)stop), UI_Pixels((f32)(stop - start) * cell), pitch, 0, color, 0);
                        at = end;
                    }
                }
            }
        }
        Prog__Spacer(UI_Flex(), UI_Pixels((f32)(rowCount - last) * pitch));
    }
    Prog__ScrollBar(view, pitch, S("InspectorText"), 1, m);
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
    if (view->rect.y1 <= view->rect.y0) UI_KeepAnimating();
    String text = state->previewText;
    u64 selectFrom = Min(state->previewAnchor, state->previewCaret), selectTo = Max(state->previewAnchor, state->previewCaret);
    u32 lineCount = state->previewLineCount;
    // The lines the filter keeps, picked again when it changes; the rows are then worked out again too.
    String filter = Prog_EditText(&state->inspectorFilter);
    if (!state->previewKept || state->previewKeptVersion != state->inspectorFilter.version) {
        state->previewKept = ArenaPushArray(&state->previewArena, lineCount + 1, u32);
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
    for (u32 n = lineCount; n >= 10; n /= 10) ++digits;
    f32 digitWidth = R_TextWidth(m->detail, S("0")), numbers = (f32)digits * digitWidth;
    // A hex dump has its offsets for numbers and an archive's listing its sizes, and their lines are not wrapped.
    b32 numbered = !state->inspectorHideNumbers && !state->previewHex && !state->previewArchive;
    f32 gap = floorf(10.4f * s + 0.5f), left = !numbered ? floorf(9.6f * s + 0.5f) : MaxF32(floorf(52.0f * s + 0.5f), floorf(26.4f * s + 0.5f) + numbers + gap);
    f32 room = MaxF32(cell, view->rect.x1 - view->rect.x0 - left - margin);
    b32 wrap = state->previewDocument ? !state->documentUnwrapped : state->inspectorWrap;
    u32 columns = wrap && !state->previewHex && !state->previewArchive ? Max((u32)(room / cell), 1u) : 0;
    // Each line's first row, worked out again when the width in characters changes.
    if (columns && state->previewRowColumns != columns) {
        state->previewRows = ArenaPushArray(&state->previewArena, shownCount + 1, u32);
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
    f32 from = MinF32(view->scrollOffset.y, view->scrollShown.y), to = MaxF32(view->scrollOffset.y, view->scrollShown.y) + (view->rect.y1 - view->rect.y0);
    u32 first = Min((u32)MaxF32(0, floorf(from / pitch)), rowCount), last = Min((u32)MaxF32(0, ceilf(to / pitch)) + 1, rowCount);
    // The row and cell under the pointer, among the rows in sight; above them it is the first one's start, and below the last one's end.
    f32 pointerRow = floorf((UI->mouse.y - view->rect.y0 - floorf(13.6f * s + 0.5f) + view->scrollShown.y) / pitch);
    f32 pointerCell = (UI->mouse.x - view->rect.x0 - left) / cell;
    if (pointerRow < (f32)first) pointerCell = -1;
    if (pointerRow >= (f32)last) pointerCell = 1e9f;
    u32 hitRow = last > first ? (u32)MinF32(MaxF32(pointerRow, (f32)first), (f32)(last - 1)) : UINT32_MAX, hitLine = 0;
    u64 hit = UINT64_MAX;
    UI_Parent(view) {
        Prog__Spacer(UI_Flex(), UI_Pixels((f32)first * pitch));
        // The shown line holding the first row in sight.
        u32 shown = first;
        if (columns) {
            u32 low = 0, high = shownCount;
            while (low + 1 < high) {
                u32 middle = (low + high) / 2;
                if (state->previewRows[middle] <= first) low = middle;
                else high = middle;
            }
            shown = low;
        }
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
                        Prog__Label(StrFormat(&state->frameArena, "%u", line + 1), UI_Pixels(left - gap), pitch, 0, theme->textDim, 1);
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
    Prog__ScrollBar(view, pitch, S("InspectorText"), 1, m);
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
    String labels[7], values[7];
    u32 count = 0;
    labels[count] = S("Path"), values[count++] = path;
    labels[count] = S("Type"), values[count++] = entry->type;
    labels[count] = S("Size"), values[count++] = size;
    if (folder) {
        labels[count] = S("Subfolders"), values[count++] = entry->measured && entry->folders ? StrFormat(frame, "%" PRIu64, entry->folders) : none;
        labels[count] = S("Subfiles"), values[count++] = entry->measured && entry->files ? StrFormat(frame, "%" PRIu64, entry->files) : none;
    }
    labels[count] = S("Created"), values[count++] = created;
    labels[count] = S("Modified"), values[count++] = Prog_EntryModified(frame, entry);
    f32 height = head + (state->inspectorDetailsFolded ? 0 : m->separator + (f32)count * row + top + bottom);
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
            Prog__Label(entry->file.name, UI_Flex(), head, 0, theme->text, 0);
            Prog__ToolbarButton(state, S("InspectorDetailsClose"), Icon_SquareX, Prog_CommandKind_InspectorToggleDetails, true, m);
            Prog__Spacer(UI_Pixels(floorf(8.0f * s + 0.5f)), UI_Pixels(head));
            UI_Interaction fold = Prog__TextButton(S("InspectorDetailsFold"), state->inspectorDetailsFolded ? Icon_ChevronRight : Icon_ChevronDown, m->icons,
                                                   UI_Pixels(Prog__ToolbarButtonWidth(m)), Prog__ToolbarButtonHeight(m), 0, theme->icon, theme->text, theme->iconDisabled, true, m);
            if (fold.clicked) Prog_QueueCommand(state, Prog_CommandKind_InspectorFoldDetails, 0, 0, fold.clickOrder);
        }
        if (!state->inspectorDetailsFolded) {
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
    UI_SetNextBorderThickness(1.2f * s);
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
    if (!totals.complete) return StrFormat(arena, "Over %S items, packed to %S", Prog__FormatCount(arena, totals.folders + totals.files), size);
    StrBuilder text = {0};
    if (totals.folders) StrBuildFormat(arena, &text, "%S %s, ", Prog__FormatCount(arena, totals.folders), totals.folders == 1 ? "folder" : "folders");
    StrBuildFormat(arena, &text, "%S %s, %S, packed to %S", Prog__FormatCount(arena, totals.files), totals.files == 1 ? "file" : "files",
                   Prog_FormatSize(arena, totals.bytes), size);
    u32 saved = totals.bytes > packed ? (u32)((f64)(totals.bytes - packed) * 100.0 / (f64)totals.bytes + 0.5) : 0;
    if (saved) StrBuildFormat(arena, &text, " (%u%% smaller)", saved);
    else StrBuildStr(arena, &text, S(" (no smaller)"));
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
        UI_Parent(body) Prog__Label(S("Loading..."), UI_Flex(), m->row, 0, theme->textDim, 0.5f);
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
    UI_SetNextBorderThickness(1.2f * m->scale);
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
                Prog__StatusIcon(Icon_Search, m->iconSlot - m->gap, tall, state->inspectorFilter.length ? theme->text : theme->textDim, m);
                UI_SetNextPrefWidth(UI_Flex());
                UI_SetNextPrefHeight(UI_Pixels(tall));
                UI_Block *field = UI_AddBlock(UI_Flag_Clip);
                UI_Parent(field) Prog__EditContents(&state->inspectorFilter, floorf(m->gap * 0.5f), floorf((tall - m->line) * 0.5f), state->inspectorFiltering, 0, m);
            }
            UI_Block *buttons = Prog__Box(UI_Fill(), UI_Pixels(tall), UI_Layout_Horizontal, (R_Color){0}, 0);
            UI_Parent(buttons) {
                // Kept while it is being typed; once kept, only clearing it is left.
                if (state->inspectorFiltering) Prog__PillButton(state, S("InspectorFilterApply"), Icon_Check, Prog_CommandKind_InspectorApplyFilter, false, m);
                Prog__PillButton(state, S("InspectorFilterRemove"), Icon_X, Prog_CommandKind_InspectorRemoveFilter, false, m);
            }
        } else {
            Prog__StatusIcon(Icon_Search, m->iconSlot - m->gap, m->pill, theme->textDim, m);
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
            Prog__DescribeTooltip(state, tipKind, tips[i]);
            UI_Interaction interaction = UI_InteractionFromBlock(button);
            if (interaction.clicked) Prog_QueueCommand(state, kind, i, 0, interaction.clickOrder);
        }
        Prog__RoundDashedOutline(0, floorf((bar - height) * 0.5f), width, height, floorf(8.0f * s + 0.5f), MaxF32(1, floorf(1.2f * s + 0.5f)),
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
    i32 width = (i32)(view->rect.x1 - view->rect.x0), tall = (i32)(view->rect.y1 - view->rect.y0);
    // A view new this frame has no size yet.
    if (width <= 0 || tall <= 0) {
        UI_KeepAnimating();
        return;
    }
    if (width != state->model.textureWidth || tall != state->model.textureHeight) {
        R_TextureRelease(state->model.texture);
        state->model.texture = R_TextureCreateRGBA(width, tall, NULL);
        state->model.textureWidth = width, state->model.textureHeight = tall;
        state->model.stale = true;
    }
    f32 now[3] = { state->model.yaw, state->model.pitch, state->model.zoom };
    if (state->model.stale || MemCompare(now, state->model.drawn, sizeof(now))) {
        // Centered, turned about Y, tipped about X, and scaled so that its bounds fill 88% of the shorter side; depth is not zoomed, so that
        // it stays within the view space's -1 to 1.
        f32 cy = cosf(state->model.yaw), sy = sinf(state->model.yaw), cp = cosf(state->model.pitch), sp = sinf(state->model.pitch);
        f32 fit = 0.88f / state->model.radius, s = fit * state->model.zoom;
        f32 r[3][3] = { { cy, 0, -sy }, { -sp * sy, cp, -sp * cy }, { cp * sy, sp, cp * cy } };
        f32 rows[3][4];
        for (u32 i = 0; i < 3; ++i) {
            f32 scale = i == 2 ? fit : s;
            for (u32 k = 0; k < 3; ++k) rows[i][k] = r[i][k] * scale;
            rows[i][3] = -(rows[i][0] * state->model.center[0] + rows[i][1] * state->model.center[1] + rows[i][2] * state->model.center[2]);
        }
        f32 matrix[16] = { rows[0][0], rows[1][0], rows[2][0], 0, rows[0][1], rows[1][1], rows[2][1], 0, rows[0][2], rows[1][2], rows[2][2], 0, rows[0][3], rows[1][3], rows[2][3], 1 };
        MemCopy(state->model.turn, r, sizeof(r));
        R_MeshDraw(state->model.mesh, state->model.texture, width, tall, matrix, state->model.parts, state->model.partCount);
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
// over a checkered grid, with a minimap of the part in sight.
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
    if (UI->hotKey == view->key && UI->wheel.y != 0 && !(UI->mods & KeyMod_Control)) wanted = MinF32(MaxF32(scale * powf(1.2f, UI->wheel.y), least), 32.0f);
    b32 toFit = drag.pressed && drag.pressCount == 2 && state->imageScaleMode != Prog_ImageScale_Fit;
    if (drag.pressed && drag.pressCount == 2) wanted = toFit ? least : 1;
    if (wanted != scale || toFit) {
        f32 px = UI->mouse.x - view->rect.x0 - left, py = UI->mouse.y - view->rect.y0 - top;
        f32 u = (px - ((roomWidth - width * scale) * 0.5f + state->imagePan.x)) / scale, v = (py - ((roomHeight - height * scale) * 0.5f + state->imagePan.y)) / scale;
        state->imagePan.x = px - u * wanted - (roomWidth - width * wanted) * 0.5f;
        state->imagePan.y = py - v * wanted - (roomHeight - height * wanted) * 0.5f;
        state->imageScaleMode = toFit ? Prog_ImageScale_Fit : wanted == 1 ? Prog_ImageScale_Original : Prog_ImageScale_Custom;
        state->imageScale = wanted;
        scale = wanted;
    }
    f32 shownWidth = floorf(width * scale + 0.5f), shownHeight = floorf(height * scale + 0.5f);
    if (drag.pressed) state->imageDragFrom = UI->mouse;
    if (drag.active && drag.isDragging) {
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
            if (!state->imageHideGrid) {
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
            UI_SetNextFloatingOffset((UI_Vec2){ x, y });
            UI_SetNextPrefWidth(UI_Pixels(shownWidth));
            UI_SetNextPrefHeight(UI_Pixels(shownHeight));
            UI_SetNextImage(state->previewTexture, (R_Rect){ 0, 0, 1, 1 }, (UI_Vec2){ shownWidth, shownHeight }, (R_Color){ 1, 1, 1, 1 });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawImage);
            // Its edge, for pictures with clear parts.
            f32 line = MaxF32(1, floorf(0.8f * s + 0.5f));
            UI_SetNextFloatingOffset((UI_Vec2){ x - line, y - line });
            UI_SetNextPrefWidth(UI_Pixels(shownWidth + 2 * line));
            UI_SetNextPrefHeight(UI_Pixels(shownHeight + 2 * line));
            UI_SetNextBorderThickness(line);
            UI_SetNextColors((UI_Colors){ .border = theme->tabBorder });
            UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBorder);
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
                SInit("Auto positions the inspector to the right or below based on available space."), SInit("Forces the inspector to the right."), SInit("Forces the inspector below."),
            };
            Prog__ChoiceGroup(state, layoutKeys, layoutIcons, NULL, layoutTips, Prog_InspectorLayout_Count, state->inspectorLayout, Prog_CommandKind_InspectorSetLayout,
                              Prog_CommandKind_InspectorChangeLayout, m);
            if (image) {
                // An image's scales the same way, then its grid and minimap.
                local_persist const String scaleKeys[Prog_ImageScale_Count] = { SInit("InspectorScaleFit"), SInit("InspectorScaleOriginal"), SInit("InspectorScaleCustom") };
                const String scaleLabels[Prog_ImageScale_Count] = { Icon_Minimize2, S("1:1"), Icon_Maximize2 };
                const R_Font *scaleFonts[Prog_ImageScale_Count] = { NULL, m->detail, NULL };
                local_persist const String scaleTips[Prog_ImageScale_Count] = {
                    SInit("Display image scaled to fit within the viewport."), SInit("Display image at its original source size, no scaling."),
                    SInit("Display image with a custom scale adjustment."),
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
            // Where the controls end, which the card of the details reaches to.
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(bar));
            UI_Block *rest = UI_AddBlockFromKey(UI_KeyFromStr(0, S("InspectorToolbarRest")), 0);
            room = rest->rect.x0 - pane->rect.x0;
            // The item's name, and beside it the button for the card of its details.
            String name = entry ? entry->file.name : Prog_PathName(state, state->directory.path);
            Prog__Label(name, UI_TextContent(), bar, 0, theme->text, 0);
            if (entry) Prog__ToolbarButton(state, S("InspectorDetailsButton"), Icon_ChevronRight, Prog_CommandKind_InspectorToggleDetails, true, m);
            Prog__Spacer(UI_Pixels(floorf(m->inset * 0.4f)), UI_Pixels(bar));
        }
        if (text && state->previewHex) {
            Prog__InspectorHex(state, m);
        } else if (text) {
            Prog__InspectorText(state, m);
        } else if (pdf) {
            Prog__InspectorPdf(state, m);
        } else if (image) {
            Prog__InspectorImage(state, m);
        } else if (folder) {
            if (archive) {
                String summary = state->previewArchiveCounted ? Prog_ArchiveSummary(&state->frameArena, state->previewArchiveTotals, entry->file.size) : S("Counting...");
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
                    Prog__Label(S("Nothing to preview"), UI_Flex(), m->row, 0, theme->textDim, 0.5f);
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
                    Prog__Label(entry->file.name, UI_Flex(), m->row, 0, theme->textHeading, 0);
                    String details = entry->file.kind == P_FileKind_Directory
                        ? (entry->measured ? StrFormat(&state->frameArena, "%S, %" PRIu64 " folders, %" PRIu64 " files, %S", entry->type, entry->folders, entry->files, Prog_FormatSize(&state->frameArena, entry->bytes)) : entry->type)
                        : StrFormat(&state->frameArena, "%S, %S", entry->type, Prog_FormatSize(&state->frameArena, entry->file.size));
                    Prog__Label(details, UI_Flex(), m->row, 0, theme->textDim, 0);
                    Prog__Label(Prog_EntryModified(&state->frameArena, entry), UI_Flex(), m->row, 0, theme->textDim, 0);
                    // A file inside an archive is taken out before it shows, which a large one takes a while for.
                    P_Progress *taking = state->previewJob ? ((Prog_JobInput *)state->previewJob->input)->progress : NULL;
                    u64 total = taking ? atomic_load(&taking->totalBytes) : 0, done = total ? Min(atomic_load(&taking->doneBytes), total) : 0;
                    if (total && UI->nowMicroseconds - Min(state->previewSubmitted, UI->nowMicroseconds) >= 400000) {
                        f32 part = (f32)((f64)done / (f64)total), barWidth = floorf(MinF32(room, m->line * 16)), barHeight = MaxF32(3, floorf(4 * m->scale));
                        Prog__Spacer(UI_Flex(), UI_Pixels(m->inset));
                        Prog__Label(StrFormat(&state->frameArena, "Extracting it from the archive to preview, %u%%", (u32)(part * 100)), UI_Flex(), m->row, 0, theme->text, 0);
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
                        Prog__Label(StrFormat(&state->frameArena, "%S of %S", Prog_FormatSize(&state->frameArena, done), Prog_FormatSize(&state->frameArena, total)), UI_Flex(), m->row, 0,
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
            if (text && !state->previewHex) Prog__InspectorFilterBox(state, StrFormat(&state->frameArena, "Filter %u lines...", state->previewLineCount), m);
            Prog__Spacer(UI_Flex(), UI_Pixels(m->pill));
            if (pdf) {
                String pages = state->pdf.pageCount == 1 ? S("1 page") : StrFormat(&state->frameArena, "%u pages", state->pdf.pageCount);
                Prog__LabelPadded(pages, UI_TextContent(), m->pill, 0, radius, theme->textDim, 0);
            }
            if (image) {
                // An image's scale, filled up to its own size.
                f32 scale = state->imageScaleMode == Prog_ImageScale_Fit ? MinF32(state->imageFit, 1) : state->imageScaleMode == Prog_ImageScale_Original ? 1 : state->imageScale;
                UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, radius + floorf(4.8f * s + 0.5f), 0 });
                UI_SetNextCornerRadius(radius);
                UI_SetNextBorderThickness(1.2f * s);
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
                UI_SetNextBorderThickness(1.2f * s);
                UI_Block *facts = Prog__Box(UI_Fill(), UI_Pixels(m->pill + radius), UI_Layout_Horizontal, theme->panel, UI_Flag_DrawBorder);
                facts->props.colors.border = theme->tabBorder;
                facts->props.childrenAlignment = (UI_Vec2){ 0, 0 };
                UI_Parent(facts) {
                    f32 before = floorf(7.2f * s + 0.5f);
                    Prog__LabelPadded(state->previewHex ? S("Hex") : state->previewArchive ? S("Archive") : S("UTF-8"), UI_TextContent(), m->pill, before, floorf(13.6f * s + 0.5f), theme->text, 0);
                    Prog__Icon(Icon_TextAlignStart, 0, m->pill, theme->icon, 0.5f, m);
                    // The lines the filter keeps out of all of them; for hex, the file's size.
                    String lines = state->previewHex ? Prog_FormatSize(&state->frameArena, state->hexSize)
                                 : state->inspectorFilter.length ? StrFormat(&state->frameArena, "%u/%u", state->previewKeptCount, state->previewLineCount)
                                 : StrFormat(&state->frameArena, "%u", state->previewLineCount);
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
        UI_Parent(list) Prog__Label(S("Loading..."), UI_Flex(), m->row, m->gap, theme->textDim, 0);
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
            UI_Block *line = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable);
            UI_Interaction press = UI_InteractionFromBlock(line);
            if (press.pressed && press.button == MouseButton_Left) Prog_QueueCommand(state, Prog_CommandKind_MillerSelect, row, press.pressCount, press.pressOrder)->target = slot;
            UI_Parent(line) {
                Prog__ItemBox(Prog__ItemLook(row == litRow, hot, false), UI_Percent(1), m);
                const Prog_Icon *icon = Prog_PathIcon(state, Prog_JoinPath(frame, column->path, entry->file.name), entry->file.kind);
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
    f32 previewWidth = inspecting && !below ? floorf((width - m->separator) * 0.5f) : 0;
    f32 previewHeight = inspecting && below ? floorf((mainHeight - m->separator) * 0.5f) : 0;
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
        Prog__TabStrip(state, width, top, top && state->sidebarHidden && rect.x0 <= area.x0, top && rect.x1 >= area.x1, scale, m);
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
            f32 shown = active ? 1 : 0, dimmed = active ? 0 : 1;
            if (own->focusMicroseconds && UI->animate) {
                f32 since = (f32)(UI->nowMicroseconds - Min(own->focusMicroseconds, UI->nowMicroseconds));
                shown = active ? 1 - expf(-since / 60000.0f) : expf(-since / 28000.0f);
                if (!active) dimmed = 1 - expf(-since / 25000.0f);
                if (since < 400000) UI_KeepAnimating();
                else own->focusMicroseconds = 0;
            }
            UI_Block *listSide = Prog__Box(below ? UI_Flex() : UI_Pixels(listWidth), below ? UI_Pixels(mainHeight - previewHeight) : UI_Flex(), UI_Layout_Vertical, theme->background, 0);
            UI_Parent(listSide) {
                Prog__Toolbar(state, m);
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
            Prog__Sidebar(state, m);
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
    f32 controlWidth = floorf(40.8f * scale + 0.5f), height = Prog__TitleStripHeight(m) - m->separator;
    UI_SetNextFloatingOffset((UI_Vec2){ view.x1 - view.x0 - controlWidth * 3, 0 });
    UI_SetNextPrefWidth(UI_Pixels(controlWidth * 3));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *buttons = UI_AddBlock(UI_Flag_Floating);
    UI_Parent(buttons) {
        Prog_WindowButton maximize = PlatformWindowIsMaximized() ? Prog_WindowButton_Restore : Prog_WindowButton_Maximize;
        Prog__WindowButton(state, S("WindowMinimize"), Prog_WindowButton_Minimize, Prog_CommandKind_WindowMinimize, controlWidth, height, scale, m);
        Prog__WindowButton(state, S("WindowMaximize"), maximize, Prog_CommandKind_WindowToggleMaximize, controlWidth, height, scale, m);
        Prog__WindowButton(state, S("WindowClose"), Prog_WindowButton_Close, Prog_CommandKind_WindowClose, controlWidth, height, scale, m);
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
        String name = S("Filey");
        Prog__SetupText(name, large, theme->icon, middle - floorf(R_TextWidth(large, name) * 0.5f), floorf(217.6f * s + 0.5f));
        // What runs and what is installed: the version in a brighter color.
        String before, version, after;
        if (setup->running) {
            before = S("Installed version "), version = S(FILEY_VERSION), after = S(".");
        } else if (!setup->installed) {
            before = S("Standalone version "), version = S(FILEY_VERSION), after = S(".");
        } else if (setup->same) {
            before = S("The same version is already installed at:"), version = after = (String){0};
        } else {
            before = S("A different version ("), version = setup->version.length ? setup->version : S("unknown"), after = S(") is installed.");
        }
        f32 lineWidth = R_TextWidth(medium, before) + R_TextWidth(medium, version) + R_TextWidth(medium, after), x = middle - floorf(lineWidth * 0.5f), y = floorf(282.8f * s + 0.5f);
        Prog__SetupText(before, medium, theme->textDim, x, y);
        if (version.length) Prog__SetupText(version, medium, theme->icon, x + R_TextWidth(medium, before), y);
        if (after.length) Prog__SetupText(after, medium, theme->textDim, x + R_TextWidth(medium, before) + R_TextWidth(medium, version), y);
        Prog__SetupLine(middle, floorf(318.4f * s + 0.5f), m);
        // Where it goes, which is always the same folder.
        f32 boxWidth = floorf(504.0f * s + 0.5f), boxLeft = middle - floorf(boxWidth * 0.5f), boxTop = floorf(393.6f * s + 0.5f), boxHeight = floorf(44.8f * s + 0.5f);
        Prog__SetupText(setup->installed ? S("Installed Path:") : S("Install Path:"), large, theme->textDim, boxLeft, floorf(360.8f * s + 0.5f));
        UI_SetNextFloatingOffset((UI_Vec2){ boxLeft, boxTop });
        UI_SetNextPrefWidth(UI_Pixels(boxWidth));
        UI_SetNextPrefHeight(UI_Pixels(boxHeight));
        UI_SetNextCornerRadius(floorf(2.4f * s + 0.5f));
        UI_SetNextBorderThickness(MaxF32(1, floorf(0.8f * s + 0.5f)));
        UI_SetNextColors((UI_Colors){ .background = theme->inner, .border = theme->tabBorder });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder);
        String folder = Prog__Ellipsize(frame, fine, P_NativePath(frame, setup->folder), boxWidth - floorf(17.6f * s + 0.5f));
        Prog__SetupText(folder, fine, theme->textDim, boxLeft + floorf(8.8f * s + 0.5f), boxTop + floorf(boxHeight * 0.5f));
        // The switches, each after its name, in two columns of two rows; the first column's second row is for the right-click entries.
        local_persist const String names[3] = { SInit("Desktop shortcut:"), SInit("Add to PATH:"), SInit("Set as default:") };
        local_persist const String tips[3] = { SInit("Creates a shortcut on the desktop for quick access to Filey."),
                                                SInit("Allows Filey to be launched from the command line using Filey.exe."),
                                                SInit("Opens folders with Filey and assigns it to the Win+E shortcut.") };
        b32 values[3] = { state->setupDesktop, state->setupOnPath, state->setupDefault };
        u32 columns[3] = { 0, 1, 1 }, rows[3] = { 0, 1, 0 };
        f32 wide = floorf(45.6f * s + 0.5f), tall = floorf(28.8f * s + 0.5f);
        f32 labels[2] = { middle - floorf(225.6f * s + 0.5f), middle + floorf(18.4f * s + 0.5f) }, switches[2] = { middle - floorf(64.0f * s + 0.5f), middle + floorf(142.4f * s + 0.5f) };
        for (u32 i = 0; i < 3; ++i) {
            f32 row = floorf((484.4f + 43.2f * (f32)rows[i]) * s + 0.5f);
            Prog__SetupText(names[i], medium, theme->textDim, labels[columns[i]], row + floorf(1.6f * s + 0.5f));
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
            String note = S("All options can be enabled or disabled after install.");
            Prog__SetupText(note, fine, theme->textDim, middle - floorf(R_TextWidth(fine, note) * 0.5f), floorf(552.0f * s + 0.5f));
        }
        // Install, replace or change; changing does something only once a switch differs from the install.
        b32 changed = state->setupDesktop != setup->desktopShortcut || state->setupOnPath != setup->onPath || state->setupDefault != setup->setDefault;
        String word = !setup->installed ? S("Install") : setup->same ? S("Modify") : S("Install & Replace");
        String tip = !setup->installed ? StrFormat(frame, "This will install version %s on your PC.", FILEY_VERSION)
                   : setup->same ? S("This will apply modifications to the existing installation.")
                                 : StrFormat(frame, "This will overwrite the existing version %S with version %s.", setup->version, FILEY_VERSION);
        Prog__SetupButton(state, S("SetupInstall"), word, tip, Prog_CommandKind_SetupInstall, !setup->installed || !setup->same || changed, middle, floorf(608.0f * s + 0.5f), large, m);
        Prog__SetupLine(middle, floorf(646.4f * s + 0.5f), m);
        if (setup->installed) {
            Prog__SetupButton(state, S("SetupUninstall"), S("Uninstall"), S("This will remove the current version from your PC, including its shortcuts, default program registration and PATH changes."),
                              Prog_CommandKind_SetupUninstall, true, middle, floorf(688.8f * s + 0.5f), large, m);
        }
        Prog__SetupButton(state, S("SetupBack"), S("Back"), (String){0}, Prog_CommandKind_SetupBack, true, middle, floorf((setup->installed ? 744.0f : 688.8f) * s + 0.5f), large, m);
    }
}

function void Prog_BuildUI(Prog_State *state, const R_Font *font, const R_Font *icons, f32 scale)
{
    state->iconsPending = 0;
    state->hasHoverRow = false;
    state->tooltipKey = 0;
    state->tooltipAbove = false;
    state->tooltipPath = (String){0};
    state->cursor = PlatformCursor_Arrow;
    for (u32 i = 0; i < state->panelCount; ++i) state->panels[i].inspectorRect = (R_Rect){0};
    state->inspectorTextRect = (R_Rect){0};
    state->bookmarkDropShown = 0;
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
    UI->squareCorners = state->squareCorners;
    UI->reverseScroll = state->reverseScroll;
    metrics.detail = state->smallFont ? state->smallFont : font;
    metrics.iconsSmall = state->smallIcons ? state->smallIcons : icons;
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
    f32 shown = UI_Slide(UI_KeyFromStr(0, S("SidebarSlide")), state->sidebarHidden ? 0.0f : 1.0f, Prog__SlideCap(m) / MaxF32(full, 1));
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
    if (state->tabDrag) {
        if (!UI__Contains(view, UI->mouse)) state->tabDrop.kind = Prog_TabDrop_Window;
        Prog__TabGhost(state, scale, m);
    }
    Prog__Transfers(state, m);
    Prog__ContextMenu(state, m);
    Prog__TypesPopup(state, m);
    Prog__FilesPopup(state, m);
    Prog__ZoomPopup(state, m);
    Prog__Palette(state, m);
    Prog__SchemesDropdown(state, m);
    Prog__HotkeyQuestion(state, m);
    Prog__Tooltip(state, m);
    Prog__DeleteConfirmation(state, m);
    Prog__ColorPicker(state, m);
    Prog__LayoutSaveBox(state, m);
    Prog__LayoutQuestion(state, m);
    Prog__WindowBorder(m);
    // A chip being rebound that went out of sight, with its menu or its palette row, stops.
    if (state->hotkeyKind && !state->hotkeyChipShown) Prog_QueueCommand(state, Prog_CommandKind_HotkeyCancel, 0, 0, 0);
    state->hotkeyChipShown = false;
}
