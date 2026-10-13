// Browser UI.
// Sizes derive from the font's line height so the layout keeps its proportions at any font size and display scale; the ratios were measured against the reference.

#include <math.h>

#include "Meta/Icons.meta.h"

#define Prog_Gray(v)     { (v) / 255.0f, (v) / 255.0f, (v) / 255.0f, 1 }
#define Prog_Rgb(r, g, b) { (r) / 255.0f, (g) / 255.0f, (b) / 255.0f, 1 }

typedef struct Prog_Theme {
    R_Color window;        // cleared behind everything
    R_Color background;    // toolbar and list base
    R_Color menu;          // context menus, which cover other content
    R_Color panel;         // sidebar and bottom pills
    R_Color alternateRow;
    R_Color separator;
    R_Color hover;
    R_Color selection;
    R_Color focus;         // outline of the focused row
    R_Color tabBorder;     // outline of the shown tab
    R_Color closeHover;    // the window's close button under the pointer
    R_Color text;
    R_Color textDim;
    R_Color textHeading;   // sidebar section titles
    R_Color textMatch;     // letters a filter matched
    R_Color danger;        // actions that cannot be undone
    R_Color icon;
    R_Color iconDisabled;
} Prog_Theme;

// The neutral "custom" scheme from the reference screenshots.
global const Prog_Theme Prog_ThemeCustom = {
    .window       = Prog_Gray(25),
    .background   = Prog_Gray(25),
    .menu         = Prog_Gray(31),
    .panel        = Prog_Gray(31),
    .alternateRow = Prog_Gray(28),
    .separator    = Prog_Gray(28),
    .hover        = Prog_Gray(80),
    .selection    = Prog_Gray(44),
    .focus        = Prog_Gray(80),
    .tabBorder    = Prog_Gray(29),
    .closeHover   = Prog_Rgb(196, 43, 28),
    .text         = Prog_Rgb(221, 222, 224),
    .textDim      = Prog_Rgb(107, 112, 120),
    .textHeading  = Prog_Gray(162),
    .textMatch    = Prog_Rgb(0, 170, 255),
    .danger       = Prog_Rgb(255, 99, 88),
    .icon         = Prog_Gray(154),
    .iconDisabled = Prog_Gray(70),
};

#define Prog_White(a) { 1, 1, 1, (a) }

// The custom scheme over a see-through window: the window is one translucent layer and the surfaces on it are faint white tints, so they do not stack into an opaque color.
global const Prog_Theme Prog_ThemeGlass = {
    .window       = { 25 / 255.0f, 25 / 255.0f, 25 / 255.0f, 0.55f },
    .background   = { 0, 0, 0, 0 },
    .menu         = { 31 / 255.0f, 31 / 255.0f, 31 / 255.0f, 0.97f },
    .panel        = Prog_White(0.025f),
    .alternateRow = Prog_White(0.012f),
    .separator    = Prog_White(0.03f),
    .hover        = Prog_White(0.2f),
    .selection    = Prog_White(0.09f),
    .focus        = Prog_White(0.22f),
    .tabBorder    = Prog_White(0.04f),
    .closeHover   = Prog_Rgb(196, 43, 28),
    .text         = Prog_Rgb(221, 222, 224),
    .textDim      = Prog_Rgb(150, 155, 162),
    .textHeading  = Prog_Gray(170),
    .textMatch    = Prog_Rgb(0, 170, 255),
    .danger       = Prog_Rgb(255, 99, 88),
    .icon         = Prog_Gray(160),
    .iconDisabled = Prog_Gray(90),
};

// A distinct loud color for every role, so that screenshots show which surface draws where.
global const Prog_Theme Prog_ThemeDebug = {
    .window       = Prog_Rgb(60, 0, 60),
    .background   = Prog_Rgb(10, 25, 80),
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
    .danger       = Prog_Rgb(255, 99, 88),
    .icon         = Prog_Rgb(255, 230, 120),
    .iconDisabled = Prog_Rgb(120, 100, 60),
};

global const Prog_Theme *Prog_Theme_ = &Prog_ThemeCustom;

function const Prog_Theme *Prog_CurrentTheme(void) { return Prog_Theme_; }

function void Prog__ApplyTheme(Prog_State *state)
{
    Prog_Theme_ = state->debugTheme ? &Prog_ThemeDebug : state->transparent ? &Prog_ThemeGlass : &Prog_ThemeCustom;
    state->ui.defaultColors = (UI_Colors){ .background = Prog_Theme_->background, .border = Prog_Theme_->separator, .text = Prog_Theme_->text };
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
    const R_Font *font;
    const R_Font *icons;
} Prog_Metrics;

function Prog_Metrics Prog__Metrics(const R_Font *font, const R_Font *icons, f32 scale)
{
    f32 line = font->lineHeight;
    return (Prog_Metrics){
        .font = font,
        .icons = icons,
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

// The title bar, as in the reference: one tab per slot, the shown one filled like the toolbar it joins, the others parted by faint lines.
// Tabs share the strip's width up to a fixed size; dragging one moves it between the others, and the strip's empty space moves the window.
function void Prog__TabStrip(Prog_State *state, f32 scale, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog__EnsureTab(state);
    f32 windowWidth = UI->root->rect.x1 - UI->root->rect.x0;
    f32 top = floorf(m->bar * 0.15f);
    UI_SetNextChildrenPadding((UI_Padding){ 0, top, 0, 0 });
    UI_Block *strip = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->panel, 0);
    // Children start at the top, so that tabs taller than the strip hang below it.
    strip->props.childrenAlignment.y = 0;
    UI_Parent(strip) {
        // Under everything else on the strip, so that only its empty space moves the window.
        UI_SetNextFloatingOffset((UI_Vec2){ 0, -top });
        UI_SetNextPrefWidth(UI_Pixels(windowWidth));
        UI_SetNextPrefHeight(UI_Pixels(m->bar));
        UI_Interaction caption = UI_InteractionFromBlock(UI_AddBlockFromKey(UI_KeyFromStr(0, S("Caption")), UI_Flag_Floating | UI_Flag_LeftMouseClickable));
        if (caption.pressed && caption.button == MouseButton_Left) {
            Prog_QueueCommand(state, caption.pressCount == 2 ? Prog_CommandKind_WindowToggleMaximize : Prog_CommandKind_WindowMove, 0, 0, caption.pressOrder);
        }
        // The line under the strip, which the shown tab opens into the toolbar; it is as thick as the sidebar's edge it meets.
        f32 stroke = m->separator;
        UI_SetNextFloatingOffset((UI_Vec2){ 0, m->bar - top - stroke });
        UI_SetNextPrefWidth(UI_Pixels(windowWidth));
        UI_SetNextPrefHeight(UI_Pixels(stroke));
        UI_SetNextColors((UI_Colors){ .background = theme->tabBorder });
        UI_AddBlock(UI_Flag_Floating | UI_Flag_DrawBackground);

        // The sidebar button sits at the sidebar's right edge, or at the window's left edge while the sidebar is hidden.
        f32 button = floorf(m->bar * 0.75f);
        f32 lead = state->sidebarHidden ? button + m->inset : m->sidebar + m->separator + floorf(6.0f * scale + 0.5f);
        UI_SetNextPrefWidth(UI_Pixels(lead));
        UI_SetNextPrefHeight(UI_Pixels(m->bar - top));
        UI_SetNextChildrenLayout(UI_Layout_Horizontal);
        UI_SetNextChildrenAlignment((UI_Vec2){ state->sidebarHidden ? 0.5f : 1, 0.5f });
        // Centered on the whole strip rather than on the tabs.
        UI_SetNextChildrenPadding((UI_Padding){ 0, 0, state->sidebarHidden ? 0 : m->inset, top });
        UI_Block *leading = UI_AddBlock(0);
        UI_Parent(leading) {
            UI_Interaction toggle = Prog__TextButton(S("ToggleQuickAccess"), Icon_PanelLeft, m->icons, UI_Pixels(button), button, 0,
                                                     theme->icon, theme->text, theme->iconDisabled, true, m);
            if (toggle.clicked) Prog_QueueCommand(state, Prog_CommandKind_ToggleQuickAccess, 0, 0, toggle.clickOrder);
        }
        b32 controls = PlatformWindowHasButtons();
        f32 controlWidth = floorf(40.0f * scale);
        f32 plus = floorf(m->bar * 0.9f);
        f32 room = MaxF32(0, windowWidth - lead - plus - m->inset - (controls ? controlWidth * 3 : 0));
        f32 slot = floorf(MinF32(m->line * 12.0f, room / (f32)state->tabCount));
        f32 width = MaxF32(0, slot - m->separator);
        f32 height = m->bar - top;
        // The shown tab hangs past the strip by its corner radius, so the toolbar drawn after it covers its lower corners and border.
        f32 tabRadius = floorf(8.0f * scale + 0.5f);
        f32 hang = tabRadius + stroke + 1;
        u32 dragFrom = UINT32_MAX;
        f32 dragX = 0;
        for (u32 i = 0; i < state->tabCount; ++i) {
            b32 shown = i == state->activeTab;
            String path = Prog_TabPath(state, i);
            String name = Prog__FolderName(path);
            UI_Key key = Prog__TabKey(state, i);
            UI_Key closeKey = UI_KeyFromStr(key, S("Close"));
            b32 hot = Prog__TabHot(state, i);
            UI_SetNextPrefWidth(UI_Pixels(width));
            UI_SetNextPrefHeight(UI_Pixels(height));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            f32 tabInset = floorf(14.0f * scale);
            UI_SetNextChildrenPadding((UI_Padding){ tabInset, 0, floorf(m->gap * 0.75f), 0 });
            UI_Block *tab = UI_AddBlockFromKey(key, UI_Flag_LeftMouseClickable | UI_Flag_MiddleMouseClickable);
            UI_Interaction interaction = UI_InteractionFromBlock(tab);
            if (interaction.pressed && interaction.button == MouseButton_Left) Prog_QueueCommand(state, Prog_CommandKind_SelectTab, i, 0, interaction.pressOrder);
            if (interaction.clicked && interaction.button == MouseButton_Middle) Prog_QueueCommand(state, Prog_CommandKind_CloseTabAt, i, 0, interaction.clickOrder);
            if (interaction.active && interaction.isDragging && interaction.button == MouseButton_Left) {
                dragFrom = i;
                dragX = UI->mouse.x;
            }
            UI_Parent(tab) {
                if (shown || hot) {
                    // Floating offsets start inside the padding.
                    // A hovered tab has the shown tab's shape cut off square above the strip's bottom line.
                    UI_SetNextFloatingOffset((UI_Vec2){ -tabInset, 0 });
                    UI_SetNextPrefWidth(UI_Pixels(width));
                    UI_SetNextPrefHeight(UI_Pixels(shown ? height + hang : height - stroke));
                    UI_Block *outline = UI_AddBlock(UI_Flag_Floating | UI_Flag_Clip);
                    UI_Parent(outline) {
                        UI_SetNextFloatingOffset((UI_Vec2){ 0, 0 });
                        UI_SetNextPrefWidth(UI_Pixels(width));
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
                        UI_SetNextFloatingOffset((UI_Vec2){ side ? width - tabInset - stroke : -tabInset - flare, height - flare });
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
                            UI_SetNextColors((UI_Colors){ .background = theme->panel, .border = theme->tabBorder });
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
        // The window buttons fill the strip's full height at its right end.
        if (controls) {
            UI_SetNextFloatingOffset((UI_Vec2){ windowWidth - controlWidth * 3, -top });
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
        // A dragged tab takes the slot under the pointer.
        if (dragFrom != UINT32_MAX && slot > 0) {
            f32 x = dragX - (UI->root->rect.x0 + lead);
            u32 to = (u32)MinF32(MaxF32(0, x / slot), (f32)(state->tabCount - 1));
            if (to != dragFrom) {
                Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_MoveTab, dragFrom, 0, 0);
                command->target = to;
            }
        }
    }
}

// "C:/Git/Filey" -> "C:" › "Git" › "Filey"; every segment but the current one opens its folder.
function void Prog__Breadcrumbs(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    String path = state->directory.path;
    u64 start = path.length && path.value[0] == '/' ? 1 : 0;
    if (start) {
        Prog__Spacer(UI_Pixels(m->gap), UI_Pixels(m->bar));
    }
    for (u64 at = start; at < path.length;) {
        Index slash = StrIndexChar(StrFrom(path, at), '/', SSF_CountOnFail);
        u64 end = at + slash.value;
        String segment = StrFromTo(path, at, (i64)end);
        b32 current = end >= path.length;
        if (segment.length) {
            if (at > start) Prog__Icon(Icon_ChevronRight, floorf(m->line * 1.6f), m->bar, theme->textDim, 0.5f, m);
            String identity = StrFormat(&state->frameArena, "Crumb/%S", StrFromTo(path, 0, (i64)end));
            UI_Interaction interaction = Prog__TextButton(identity, segment, NULL, UI_TextContent(), m->bar * 0.75f, m->gap * 0.5f,
                                                          current ? theme->text : theme->textDim, theme->text, theme->textDim, !current, m);
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
        Prog__Breadcrumbs(state, m);
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

function void Prog__Columns(Prog_Column *columns, f32 scale)
{
    // Logical widths measured from the reference; Name flexes.
    columns[Prog_ColumnId_Name] = (Prog_Column){ S("Name"), 0, 0 };
    columns[Prog_ColumnId_Type] = (Prog_Column){ S("Type"), floorf(192 * scale), 0 };
    columns[Prog_ColumnId_Modified] = (Prog_Column){ S("Date Modified"), floorf(193 * scale), 0 };
    columns[Prog_ColumnId_Folders] = (Prog_Column){ S("Folders"), floorf(90 * scale), 0 };
    columns[Prog_ColumnId_Files] = (Prog_Column){ S("Files"), floorf(105 * scale), 0 };
    columns[Prog_ColumnId_Size] = (Prog_Column){ S("Size"), floorf(105 * scale), 1 };
}

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
                if (i == Prog_ColumnId_Name) Prog__Spacer(UI_Pixels(m->iconSlot - m->gap), UI_Pixels(m->row));
                R_Color color = sorted || hot ? theme->text : theme->textDim;
                if (columns[i].alignX > 0) Prog__Spacer(UI_Flex(), UI_Pixels(m->row));
                Prog__Label(columns[i].title, columns[i].alignX > 0 ? UI_TextContent() : UI_Flex(), m->row, m->gap, color, 0);
                if (sorted) Prog__Icon(state->descending ? Icon_ArrowDown : Icon_ArrowUp, m->line + m->gap, m->row, theme->textDim, 0.5f, m);
                if (i + 1 < Prog_ColumnId_Count) Prog__Box(UI_Pixels(m->separator), UI_Pixels(m->line), UI_Layout_Horizontal, theme->separator, 0);
            }
        }
    }
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

function void Prog__Row(Prog_State *state, u64 index, u64 position, UI_Key key, const Prog_Column *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog_Entry *entry = &state->directory.entries[index];
    b32 focused = state->hasFocus && state->focusedRow == index;
    b32 hot = UI->hotKey == key && !state->loading;
    // Hover wins over selection, matching the reference.
    R_Color background = position & 1 ? theme->alternateRow : theme->background;
    if (entry->selected) background = theme->selection;
    if (hot) background = theme->hover;
    b32 highlighted = entry->selected || hot;
    // Hidden and dot-named items are faded whether or not the OS marks them hidden.
    b32 faded = entry->file.hidden || (entry->file.name.length && entry->file.name.value[0] == '.');
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(m->row));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextCornerRadius(highlighted || focused ? m->radius : 0);
    UI_SetNextColors((UI_Colors){ .background = background, .border = theme->focus });
    UI_Flags flags = UI_Flag_DrawBackground | (!state->loading ? UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable : 0) | (focused ? UI_Flag_DrawBorder : 0);
    UI_Block *row = UI_AddBlockFromKey(key, flags);
    if (focused) state->focusedRowRect = row->rect;
    if (hot) {
        state->hoverRow = index;
        state->hasHoverRow = true;
    }
    UI_Interaction interaction = UI_InteractionFromBlock(row);
    // A plain press inside a multi-selection keeps it so that it can be dragged; the click narrows it.
    b32 keepSelection = entry->selected && state->selectedCount > 1 && !interaction.mods && interaction.pressCount == 1;
    b32 renaming = state->renaming && StrIsEqual(entry->file.name, StrMake((const char *)state->renameFrom, state->renameFromLength));
    // Clicks inside the name being edited stay in the box instead of reopening or dragging the row.
    if (renaming && UI__Contains(state->renameRect, UI->mouse)) {
    } else if (!state->loading && interaction.pressed && interaction.button == MouseButton_Right) {
        Prog_QueueCommand(state, Prog_CommandKind_ContextMenuAt, index, interaction.pressCount, interaction.pressOrder);
    } else if (!state->loading && ((interaction.pressed && !keepSelection) || (interaction.clicked && keepSelection && interaction.button == MouseButton_Left))) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SelectRow, index, interaction.pressCount,
                                                  interaction.pressed ? interaction.pressOrder : interaction.clickOrder);
        command->mods = interaction.mods;
    }
    if (!state->loading && !renaming && interaction.active && interaction.isDragging && interaction.button == MouseButton_Left && entry->selected) {
        Prog_QueueCommand(state, Prog_CommandKind_DragSelection, index, 0, 0);
    }
    // Folders show their totals once measured; zero counts and files' counts read "--", as in the reference.
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
    String values[Prog_ColumnId_Count] = { entry->file.name, entry->type, entry->modified, folders, files, size };
    UI_Parent(row) {
        const Prog_Icon *icon = Prog_FileIcon(state, state->directory.path, entry);
        if (icon) {
            UI_SetNextPrefWidth(UI_Pixels(m->iconSlot - m->gap));
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextTextAlignment((UI_Vec2){ 1, 0.5f });
            UI_SetNextImage(icon->texture, icon->uv, (UI_Vec2){ (f32)state->iconSize, (f32)state->iconSize }, (R_Color){ 1, 1, 1, faded ? 0.35f : 1 });
            UI_AddBlock(UI_Flag_DrawImage);
        } else {
            // Glyphs stand in while the system icon loads or when it has none.
            Prog__Icon(entry->file.kind == P_FileKind_Directory ? Icon_Folder : Icon_File, m->iconSlot - m->gap, m->row,
                       highlighted ? theme->text : faded ? theme->iconDisabled : theme->icon, 1, m);
        }
        for (u32 i = 0; i < Prog_ColumnId_Count; ++i) {
            if (i == Prog_ColumnId_Name && renaming) {
                Prog__RenameBox(state, columns[i].width > 0 ? UI_Pixels(columns[i].width) : UI_Flex(), m);
                continue;
            }
            R_Color color = (i == Prog_ColumnId_Name && !faded) || highlighted ? theme->text : theme->textDim;
            if (i == Prog_ColumnId_Name && (state->filter.length || state->directory.expanded)) {
                String leaf = Prog_EntryLeaf(entry->file.name);
                u64 prefix = entry->file.name.length - leaf.length;
                u8 *marks = state->filter.length ? ArenaPushArrayNoZero(frame, Max(leaf.length, (u64)1), u8) : NULL;
                if (marks && !Prog_FilterMatch(leaf, Prog_EditText(&state->filter), marks)) marks = NULL;
                Prog__NameLabel(entry->file.name, prefix, marks, columns[i].width > 0 ? UI_Pixels(columns[i].width) : UI_Flex(), color, theme->textDim, m);
                continue;
            }
            Prog__Label(values[i], columns[i].width > 0 ? UI_Pixels(columns[i].width) : UI_Flex(), m->row, m->gap, color, columns[i].alignX);
        }
    }
}

function void Prog__List(Prog_State *state, const Prog_Column *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = Prog_CurrentTheme();
    f32 rowHeight = m->row;
    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
    UI_SetNextColors((UI_Colors){ .background = theme->background });
    UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Files")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable |
                                        (!state->loading ? UI_Flag_RightMouseClickable : 0));
    UI_Interaction listInteraction = UI_InteractionFromBlock(list);
    if (listInteraction.pressed && listInteraction.button == MouseButton_Right) {
        Prog_QueueCommand(state, Prog_CommandKind_ContextMenuAt, UINT64_MAX, listInteraction.pressCount, listInteraction.pressOrder);
    }
    if (state->resetScroll) { list->scrollOffset = (UI_Vec2){0}; state->resetScroll = false; }
    state->pageRows = (u64)(Max(0.0f, list->rect.y1 - list->rect.y0) / rowHeight);
    if (state->revealFocus && state->hasFocus) {
        u64 position = Prog_ViewPosition(state, state->focusedRow);
        f32 top = (f32)(position == UINT64_MAX ? 0 : position) * rowHeight;
        f32 height = Max(0.0f, list->rect.y1 - list->rect.y0);
        if (top < list->scrollOffset.y) list->scrollOffset.y = top;
        else if (top + rowHeight > list->scrollOffset.y + height) list->scrollOffset.y = Max(0.0f, top + rowHeight - height);
        state->revealFocus = false;
    }
    // Only rows near the viewport get blocks; spacers stand in for the rest.
    u64 first = (u64)(Max(0.0f, list->scrollOffset.y) / rowHeight);
    if (first > 2) first -= 2; else first = 0;
    first = Min(first, state->viewCount);
    f32 visibleHeight = Max(rowHeight, UI->root->rect.y1 - UI->root->rect.y0);
    u64 end = Min(state->viewCount, first + (u64)(visibleHeight / rowHeight) + 6);
    UI_Parent(list) {
        if (first) Prog__Spacer(UI_Flex(), UI_Pixels((f32)first * rowHeight));
        UI_Key directoryKey = UI_KeyFromStr(list->key, state->directory.path);
        for (u64 i = first; i < end; ++i) {
            u64 row = state->view[i];
            Prog__Row(state, row, i, UI_KeyFromStr(directoryKey, state->directory.entries[row].file.name), columns, m);
        }
        if (end < state->viewCount) Prog__Spacer(UI_Flex(), UI_Pixels((f32)(state->viewCount - end) * rowHeight));
        if (!state->viewCount && !state->loading) {
            String empty = state->directory.count ? S("No items match the filter") : S("This folder is empty");
            Prog__Label(empty, UI_Flex(), rowHeight, m->iconSlot, theme->textDim, 0);
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
            // Shown out of everything the folder holds, as in the reference; one number when nothing is left out.
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
                Prog__Icon(Icon_List, 0, m->pill, theme->icon, 0.5f, m);
                Prog__Label(S("Details"), UI_TextContent(), m->pill, m->gap * 0.5f, theme->text, 0);
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

// A sidebar section title, which opens and closes the section.
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
        Prog__Icon(icon, m->line * 1.5f, m->row, color, 0, m);
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
        const Prog_Icon *icon = drive ? NULL : Prog_FileIcon(state, Prog_ParentPath(path), &folder);
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
        if (Prog_GroupOpen(state, Prog_Group_Recents)) {
            for (u64 i = 0; i < state->recents.header.count; ++i) {
                String path = state->recents.v[i];
                Prog__SidebarItem(state, Prog_Group_Recents, i, Prog__FolderName(path), path, NULL, m);
            }
        }
        Prog__GroupTitle(state, Prog_Group_Bookmarks, S("Bookmarks"), Icon_Bookmark, m);
        if (Prog_GroupOpen(state, Prog_Group_Bookmarks)) {
            for (u64 i = 0; i < state->bookmarks.header.count; ++i) {
                String path = state->bookmarks.v[i];
                Prog__SidebarItem(state, Prog_Group_Bookmarks, i, Prog__FolderName(path), path, NULL, m);
            }
        }
        Prog__GroupTitle(state, Prog_Group_Storage, S("Storage"), Icon_HardDrive, m);
        if (Prog_GroupOpen(state, Prog_Group_Storage)) {
            for (u64 i = 0; i < state->drives.header.count; ++i) {
                const P_Place *drive = &state->drives.v[i];
                Prog__SidebarItem(state, Prog_Group_Storage, i, drive->name, drive->path, drive, m);
            }
        }
        Prog__GroupTitle(state, Prog_Group_Places, S("Places"), Icon_Folder, m);
        if (Prog_GroupOpen(state, Prog_Group_Places)) {
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
    f32 itemHeight = m->row, separatorHeight = m->gap + m->separator, pad = m->gap * 0.5f;
    f32 labelWidth = 0, height = 2 * pad;
    b32 anySubmenu = false;
    for (u32 i = 0; i < shown; ++i) {
        const P_MenuItem *item = &state->menu.items[visible[i]];
        height += item->separator ? separatorHeight : itemHeight;
        labelWidth = MaxF32(labelWidth, R_TextWidth(m->font, item->label));
        anySubmenu |= item->childCount != 0;
    }
    f32 arrow = anySubmenu ? m->line * 1.5f : 0;
    f32 width = floorf(MinF32(MaxF32(labelWidth + 2 * m->inset + arrow + 2 * pad, m->line * 12), m->line * 30));
    R_Rect view = UI->root->rect;
    f32 x = place.x, y = place.y;
    // Submenus open to the right of their item, or to the left when there is no room.
    if (x + width > view.x1) x = place.openerWidth > 0 ? x - place.openerWidth - width + 2 * pad : view.x1 - width;
    if (y + height > view.y1) y = view.y1 - height;
    x = floorf(MaxF32(view.x0, x));
    y = floorf(MaxF32(view.y0, y));

    UI_SetNextPrefWidth(UI_Pixels(width));
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextFloatingOffset((UI_Vec2){ x - view.x0, y - view.y0 });
    UI_SetNextChildrenLayout(UI_Layout_Vertical);
    UI_SetNextChildrenPadding((UI_Padding){ pad, pad, pad, pad });
    UI_SetNextCornerRadius(m->radius * 2);
    UI_SetNextColors((UI_Colors){ .background = theme->menu, .border = theme->hover });
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
                UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, m->gap, 0 });
                UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
                UI_Block *line = UI_AddBlock(0);
                UI_Parent(line) Prog__Box(UI_Flex(), UI_Pixels(m->separator), UI_Layout_Horizontal, theme->hover, 0);
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
            UI_SetNextCornerRadius(m->radius);
            UI_SetNextColors((UI_Colors){ .background = theme->selection });
            UI_Block *row = UI_AddBlockFromKey(key, (highlighted ? UI_Flag_DrawBackground : 0) | (enabled ? UI_Flag_LeftMouseClickable : 0));
            UI_Interaction interaction = UI_InteractionFromBlock(row);
            if (interaction.clicked && !item->childCount) {
                Prog_QueueCommand(state, Prog_CommandKind_ContextMenuInvoke, index, 0, interaction.clickOrder);
            }
            UI_Parent(row) {
                R_Color color = !enabled ? theme->iconDisabled : highlighted ? theme->text : theme->textHeading;
                Prog__Label(item->label, UI_Flex(), itemHeight, m->inset - pad, color, 0);
                if (item->childCount) Prog__Icon(Icon_ChevronRight, arrow, itemHeight, color, 0.5f, m);
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

function void Prog_BuildUI(Prog_State *state, const R_Font *font, const R_Font *icons, f32 scale)
{
    state->hasHoverRow = false;
    const Prog_Theme *theme = Prog_CurrentTheme();
    Prog_Metrics metrics = Prog__Metrics(font, icons, scale);
    i32 iconSize = (i32)floorf(16.0f * scale + 0.5f);
    if (iconSize != state->iconSize) Prog__ResetIcons(state, iconSize);
    const Prog_Metrics *m = &metrics;
    Prog_Column columns[Prog_ColumnId_Count];
    Prog__Columns(columns, scale);

    UI->root->props.childrenPadding = (UI_Padding){0};
    Prog__TabStrip(state, scale, m);
    UI_Block *body = Prog__Box(UI_Flex(), UI_Flex(), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(body) {
        if (!state->sidebarHidden) {
            Prog__Sidebar(state, m);
            Prog__Box(UI_Pixels(m->separator), UI_Flex(), UI_Layout_Horizontal, theme->separator, 0);
        }
        UI_Block *main = Prog__Box(UI_Flex(), UI_Flex(), UI_Layout_Vertical, theme->background, 0);
        UI_Parent(main) {
            Prog__Toolbar(state, m);
            Prog__Header(state, columns, m);
            Prog__List(state, columns, m);
            Prog__StatusBar(state, m);
        }
    }
    Prog__ContextMenu(state, m);
    Prog__DeleteConfirmation(state, m);
}
