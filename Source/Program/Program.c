// Browser UI. Sizes derive from the font's line height so the layout keeps its
// proportions at any font size and display scale; the ratios were measured
// against the reference.

#include <math.h>

#include "Meta/Icons.meta.h"

#define Prog_Gray(v)     { (v) / 255.0f, (v) / 255.0f, (v) / 255.0f, 1 }
#define Prog_Rgb(r, g, b) { (r) / 255.0f, (g) / 255.0f, (b) / 255.0f, 1 }

typedef struct Prog_Theme {
    R_Color background;    // window, toolbar and list base
    R_Color panel;         // sidebar and bottom pills
    R_Color alternateRow;
    R_Color separator;
    R_Color hover;
    R_Color selection;
    R_Color focus;         // outline of the focused row
    R_Color text;
    R_Color textDim;
    R_Color textHeading;   // sidebar section titles
    R_Color icon;
    R_Color iconDisabled;
} Prog_Theme;

// The neutral "custom" scheme from the reference screenshots.
global const Prog_Theme Prog_ThemeCustom = {
    .background   = Prog_Gray(25),
    .panel        = Prog_Gray(31),
    .alternateRow = Prog_Gray(28),
    .separator    = Prog_Gray(28),
    .hover        = Prog_Gray(80),
    .selection    = Prog_Gray(44),
    .focus        = Prog_Gray(80),
    .text         = Prog_Rgb(221, 222, 224),
    .textDim      = Prog_Rgb(107, 112, 120),
    .textHeading  = Prog_Gray(162),
    .icon         = Prog_Gray(154),
    .iconDisabled = Prog_Gray(70),
};

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
    UI_SetNextColors((UI_Colors){ .background = Prog_ThemeCustom.panel, .text = !enabled ? disabledColor : hot ? hotColor : color });
    UI_SetNextText(text);
    if (font) UI_SetNextFont(font);
    UI_Block *block = UI_AddBlockFromKey(key, UI_Flag_DrawText | (hot ? UI_Flag_DrawBackground : 0) | (enabled ? UI_Flag_LeftMouseClickable : 0));
    UI_Interaction result = UI_InteractionFromBlock(block);
    if (!enabled) result.clicked = false;
    return result;
}

function void Prog__ToolbarButton(Prog_State *state, String identity, String icon, Prog_CommandKind kind, b32 enabled, const Prog_Metrics *m)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
    UI_Interaction interaction = Prog__TextButton(identity, icon, m->icons, UI_Pixels(m->bar * 0.75f), m->bar * 0.75f, 0,
                                                  theme->icon, theme->text, theme->iconDisabled, enabled, m);
    if (interaction.clicked) Prog_QueueCommand(state, kind, 0, 0, interaction.clickOrder);
}

function void Prog__TabStrip(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
    UI_Block *strip = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(strip) {
        Prog__Spacer(UI_Pixels(m->sidebar + m->separator + m->inset), UI_Pixels(m->bar));
        // ponytail: a single tab for the current folder; real tabs arrive with multiple panels.
        String path = state->directory.path;
        Index slash = StrIndexChar(path, '/', SSF_SearchBackwards | SSF_IndexPlusOne | SSF_CountOnFail);
        String name = path.length && slash.value < path.length ? StrFrom(path, slash.value) : path;
        UI_SetNextPrefWidth(UI_Pixels(floorf(m->line * 9.5f)));
        UI_SetNextPrefHeight(UI_Pixels(m->bar - 2 * floorf(m->bar * 0.17f)));
        UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
        UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_SetNextColors((UI_Colors){ .border = theme->separator, .text = theme->text });
        UI_SetNextText(name);
        UI_AddBlock(UI_Flag_DrawBorder | UI_Flag_DrawText | UI_Flag_Clip);
    }
}

// "C:/Git/Filey" -> "C:" › "Git" › "Filey"; every segment but the current one opens its folder.
function void Prog__Breadcrumbs(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
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
    const Prog_Theme *theme = &Prog_ThemeCustom;
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
        Prog__Breadcrumbs(state, m);
    }
}

typedef struct Prog_Column {
    String title;
    f32 width;   // 0 takes the remaining width
    f32 alignX;
} Prog_Column;

function void Prog__Columns(Prog_Column *columns, f32 scale)
{
    // Logical widths measured from the reference; Name flexes.
    columns[Prog_Sort_Name] = (Prog_Column){ S("Name"), 0, 0 };
    columns[Prog_Sort_Type] = (Prog_Column){ S("Type"), floorf(192 * scale), 0 };
    columns[Prog_Sort_Modified] = (Prog_Column){ S("Date Modified"), floorf(193 * scale), 0 };
    columns[Prog_Sort_Size] = (Prog_Column){ S("Size"), floorf(105 * scale), 1 };
}

function void Prog__Header(Prog_State *state, const Prog_Column *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->inset, 0 });
    UI_Block *header = Prog__Box(UI_Flex(), UI_Pixels(m->row), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(header) {
        for (u32 i = 0; i < Prog_Sort_Count; ++i) {
            b32 sorted = state->sort == i;
            UI_Key key = UI_KeyFromStr(0, StrFormat(&state->frameArena, "Header/%S", columns[i].title));
            b32 hot = UI->hotKey == key && !state->loading;
            UI_SetNextPrefWidth(columns[i].width > 0 ? UI_Pixels(columns[i].width) : UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(m->row));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
            UI_Block *cell = UI_AddBlockFromKey(key, !state->loading ? UI_Flag_LeftMouseClickable : 0);
            UI_Interaction interaction = UI_InteractionFromBlock(cell);
            if (interaction.clicked) Prog_QueueCommand(state, Prog_CommandKind_Sort, i, 0, interaction.clickOrder);
            UI_Parent(cell) {
                if (i == Prog_Sort_Name) Prog__Spacer(UI_Pixels(m->iconSlot - m->gap), UI_Pixels(m->row));
                R_Color color = sorted || hot ? theme->text : theme->textDim;
                if (columns[i].alignX > 0) Prog__Spacer(UI_Flex(), UI_Pixels(m->row));
                Prog__Label(columns[i].title, columns[i].alignX > 0 ? UI_TextContent() : UI_Flex(), m->row, m->gap, color, 0);
                if (sorted) Prog__Icon(state->descending ? Icon_ArrowDown : Icon_ArrowUp, m->line + m->gap, m->row, theme->textDim, 0.5f, m);
                if (i + 1 < Prog_Sort_Count) Prog__Box(UI_Pixels(m->separator), UI_Pixels(m->line), UI_Layout_Horizontal, theme->separator, 0);
            }
        }
    }
}

function void Prog__Row(Prog_State *state, u64 index, UI_Key key, const Prog_Column *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
    Prog_Entry *entry = &state->directory.entries[index];
    b32 focused = state->hasFocus && state->focusedRow == index;
    b32 hot = UI->hotKey == key && !state->loading;
    // Hover wins over selection, matching the reference.
    R_Color background = index & 1 ? theme->alternateRow : theme->background;
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
    if (!state->loading && interaction.pressed && interaction.button == MouseButton_Right) {
        Prog_QueueCommand(state, Prog_CommandKind_ContextMenuAt, index, interaction.pressCount, interaction.pressOrder);
    } else if (!state->loading && ((interaction.pressed && !keepSelection) || (interaction.clicked && keepSelection && interaction.button == MouseButton_Left))) {
        Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SelectRow, index, interaction.pressCount,
                                                  interaction.pressed ? interaction.pressOrder : interaction.clickOrder);
        command->mods = interaction.mods;
    }
    if (!state->loading && interaction.active && interaction.isDragging && interaction.button == MouseButton_Left && entry->selected) {
        Prog_QueueCommand(state, Prog_CommandKind_DragSelection, index, 0, 0);
    }
    String values[Prog_Sort_Count] = { entry->file.name, entry->type, entry->modified, entry->size };
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
        for (u32 i = 0; i < Prog_Sort_Count; ++i) {
            R_Color color = (i == Prog_Sort_Name && !faded) || highlighted ? theme->text : theme->textDim;
            Prog__Label(values[i], columns[i].width > 0 ? UI_Pixels(columns[i].width) : UI_Flex(), m->row, m->gap, color, columns[i].alignX);
        }
    }
}

function void Prog__List(Prog_State *state, const Prog_Column *columns, const Prog_Metrics *m)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
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
        f32 top = (f32)state->focusedRow * rowHeight;
        f32 height = Max(0.0f, list->rect.y1 - list->rect.y0);
        if (top < list->scrollOffset.y) list->scrollOffset.y = top;
        else if (top + rowHeight > list->scrollOffset.y + height) list->scrollOffset.y = Max(0.0f, top + rowHeight - height);
        state->revealFocus = false;
    }
    // Only rows near the viewport get blocks; spacers stand in for the rest.
    u64 first = (u64)(Max(0.0f, list->scrollOffset.y) / rowHeight);
    if (first > 2) first -= 2; else first = 0;
    first = Min(first, state->directory.count);
    f32 visibleHeight = Max(rowHeight, UI->root->rect.y1 - UI->root->rect.y0);
    u64 end = Min(state->directory.count, first + (u64)(visibleHeight / rowHeight) + 6);
    UI_Parent(list) {
        if (first) Prog__Spacer(UI_Flex(), UI_Pixels((f32)first * rowHeight));
        UI_Key directoryKey = UI_KeyFromStr(list->key, state->directory.path);
        for (u64 i = first; i < end; ++i) {
            Prog__Row(state, i, UI_KeyFromStr(directoryKey, state->directory.entries[i].file.name), columns, m);
        }
        if (end < state->directory.count) Prog__Spacer(UI_Flex(), UI_Pixels((f32)(state->directory.count - end) * rowHeight));
        if (!state->directory.count && !state->loading) {
            Prog__Label(S("This folder is empty"), UI_Flex(), rowHeight, m->iconSlot, theme->textDim, 0);
        }
    }
}

function void Prog__StatusBar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
    UI_Block *bar = Prog__Box(UI_Flex(), UI_Pixels(m->pill), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(bar) {
        // ponytail: the filter pill is a placeholder until text editing exists.
        String placeholder = StrFormat(&state->frameArena, "Filter %" PRIu64 " files...", state->directory.count);
        UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, m->gap, 0 });
        UI_SetNextCornerRadius(m->radius * 2);
        UI_Block *filter = Prog__Box(UI_Pixels(floorf(m->line * 15.3f)), UI_Pixels(m->pill), UI_Layout_Horizontal, theme->panel, UI_Flag_Clip);
        UI_Parent(filter) {
            Prog__Icon(Icon_Search, m->iconSlot - m->gap, m->pill, theme->textDim, 0.5f, m);
            Prog__Label(placeholder, UI_Flex(), m->pill, m->gap * 0.5f, theme->textDim, 0);
        }

        Prog__Spacer(UI_Flex(), UI_Pixels(m->pill));

        String status = state->error;
        if (!status.length && state->loading) {
            status = StrFormat(&state->frameArena, "Loading %S...", ((Prog_JobInput *)state->directoryJob->input)->path);
        }
        if (!status.length) {
            u64 folders = state->directory.folderCount;
            u64 files = state->directory.count - folders;
            u64 selectedFolders = 0;
            if (state->selectedCount) {
                for (u64 i = 0; i < state->directory.count; ++i) {
                    Prog_Entry *entry = &state->directory.entries[i];
                    selectedFolders += entry->selected && entry->file.kind == P_FileKind_Directory;
                }
            }
            u64 selectedFiles = state->selectedCount - selectedFolders;
            String folderText = selectedFolders ? StrFormat(&state->frameArena, "%" PRIu64 "/%" PRIu64, selectedFolders, folders)
                                                : StrFormat(&state->frameArena, "%" PRIu64, folders);
            String fileText = selectedFiles ? StrFormat(&state->frameArena, "%" PRIu64 "/%" PRIu64, selectedFiles, files)
                                            : StrFormat(&state->frameArena, "%" PRIu64, files);
            UI_SetNextChildrenPadding((UI_Padding){ m->gap, 0, m->gap, 0 });
            UI_SetNextCornerRadius(m->radius * 2);
            UI_Block *counts = Prog__Box(UI_Fill(), UI_Pixels(m->pill), UI_Layout_Horizontal, theme->panel, 0);
            UI_Parent(counts) {
                Prog__Icon(Icon_Folder, 0, m->pill, theme->icon, 0.5f, m);
                Prog__Label(folderText, UI_TextContent(), m->pill, m->gap * 0.5f, theme->text, 0);
                Prog__Icon(Icon_File, 0, m->pill, theme->icon, 0.5f, m);
                Prog__Label(fileText, UI_TextContent(), m->pill, m->gap * 0.5f, theme->text, 0);
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

function void Prog__Sidebar(Prog_State *state, const Prog_Metrics *m)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
    UI_SetNextChildrenPadding((UI_Padding){ m->inset, 0, m->gap, 0 });
    UI_Block *sidebar = Prog__Box(UI_Pixels(m->sidebar), UI_Flex(), UI_Layout_Vertical, theme->panel, UI_Flag_Clip);
    UI_Parent(sidebar) {
        // ponytail: places, bookmarks and drives come with the sidebar step; only the filter placeholder for now.
        UI_Block *filter = Prog__Box(UI_Flex(), UI_Pixels(m->bar), UI_Layout_Horizontal, theme->panel, 0);
        UI_Parent(filter) {
            Prog__Icon(Icon_Search, 0, m->bar, theme->textDim, 0, m);
            Prog__Label(S("Filter..."), UI_Flex(), m->bar, m->gap * 0.5f, theme->textDim, 0);
        }
    }
}

typedef struct Prog_MenuPlace {
    u32 first, count;
    f32 x, y;
    f32 openerWidth;  // width of the panel that opened this one; 0 for the top level
} Prog_MenuPlace;

// One menu level as a floating panel, kept inside the window. Sets `child`
// when one of its items has its submenu open.
function b32 Prog__MenuPanel(Prog_State *state, u32 level, Prog_MenuPlace place, const Prog_Metrics *m, Prog_MenuPlace *child)
{
    const Prog_Theme *theme = &Prog_ThemeCustom;
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
    UI_SetNextColors((UI_Colors){ .background = theme->panel, .border = theme->hover });
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
            // Moving the mouse onto an item decides the highlight and which submenu is open,
            // as in native menus; the keyboard takes over until the mouse moves to another item.
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
    const Prog_Theme *theme = &Prog_ThemeCustom;
    Prog_Metrics metrics = Prog__Metrics(font, icons, scale);
    i32 iconSize = (i32)floorf(16.0f * scale + 0.5f);
    if (iconSize != state->iconSize) Prog__ResetIcons(state, iconSize);
    const Prog_Metrics *m = &metrics;
    Prog_Column columns[Prog_Sort_Count];
    Prog__Columns(columns, scale);

    UI->root->props.childrenPadding = (UI_Padding){0};
    Prog__TabStrip(state, m);
    Prog__Box(UI_Flex(), UI_Pixels(m->separator), UI_Layout_Horizontal, theme->separator, 0);
    UI_Block *body = Prog__Box(UI_Flex(), UI_Flex(), UI_Layout_Horizontal, theme->background, 0);
    UI_Parent(body) {
        Prog__Sidebar(state, m);
        Prog__Box(UI_Pixels(m->separator), UI_Flex(), UI_Layout_Horizontal, theme->separator, 0);
        UI_Block *main = Prog__Box(UI_Flex(), UI_Flex(), UI_Layout_Vertical, theme->background, 0);
        UI_Parent(main) {
            Prog__Toolbar(state, m);
            Prog__Header(state, columns, m);
            Prog__List(state, columns, m);
            Prog__StatusBar(state, m);
        }
    }
    Prog__ContextMenu(state, m);
}
