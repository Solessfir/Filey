function void Prog__Label(String text, UI_Size width, f32 height, f32 scale)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){ 8 * scale, 0, 8 * scale, 0 });
    UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextText(text);
    UI_AddBlock(UI_Flag_DrawText | UI_Flag_Clip);
}

function void Prog__Button(Prog_State *state, String identity, String text, UI_Size width, f32 height, f32 scale, Prog_CommandKind kind, u64 value, b32 enabled)
{
    UI_Key key = UI_KeyFromStr(0, identity);
    UI_Colors colors = UI->defaultColors;
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){ 8 * scale, 0, 8 * scale, 0 });
    UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextText(text);
    colors.background = UI->hotKey == key && enabled ? (R_Color){ 0.22f, 0.23f, 0.25f, 1 } : (R_Color){ 0.15f, 0.16f, 0.18f, 1 };
    if (!enabled) colors.text = (R_Color){ 0.43f, 0.44f, 0.46f, 1 };
    UI_SetNextColors(colors);
    UI_Block *block = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_DrawText | UI_Flag_Clip | (enabled ? UI_Flag_LeftMouseClickable : 0));
    UI_Interaction interaction = UI_InteractionFromBlock(block);
    if (enabled && interaction.clicked) Prog_QueueCommand(state, kind, value, 0, interaction.clickOrder);
}

function void Prog__Columns(Prog_Entry *entry, f32 height, f32 scale)
{
    Prog__Label(entry->file.name, UI_Flex(), height, scale);
    Prog__Label(entry->type, UI_Pixels(150 * scale), height, scale);
    Prog__Label(entry->modified, UI_Pixels(150 * scale), height, scale);
    Prog__Label(entry->size, UI_Pixels(100 * scale), height, scale);
}

function void Prog_BuildUI(Prog_State *state, const R_Font *font, f32 scale)
{
    UI_Colors colors = UI->defaultColors;
    UI->root->props.childrenPadding = (UI_Padding){ 8 * scale, 8 * scale, 8 * scale, 4 * scale };
    f32 rowHeight = font->lineHeight + 6 * scale;

    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(30 * scale));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *navigation = UI_AddBlock(0);
    UI_Parent(navigation) {
        Prog__Button(state, S("Back"), S("<"), UI_Pixels(30 * scale), 30 * scale, scale, Prog_CommandKind_Back, 0, state->historyAt > 0);
        Prog__Button(state, S("Forward"), S(">"), UI_Pixels(30 * scale), 30 * scale, scale, Prog_CommandKind_Forward, 0, state->historyAt + 1 < state->history.header.count);
        String parent = Prog_ParentPath(state->directory.path);
        Prog__Button(state, S("Up"), S("Up"), UI_Pixels(40 * scale), 30 * scale, scale, Prog_CommandKind_Up, 0, parent.length && !StrIsEqual(parent, state->directory.path));
        Prog__Label(state->directory.path, UI_Flex(), 30 * scale, scale);
        Prog__Button(state, S("Hidden"), state->showHidden ? S("Hide hidden") : S("Show hidden"), UI_TextContent(), 30 * scale, scale, Prog_CommandKind_ToggleHidden, 0, !state->loading && state->directory.path.length);
        Prog__Button(state, S("Refresh"), S("Refresh"), UI_TextContent(), 30 * scale, scale, Prog_CommandKind_Refresh, 0, state->directory.path.length || state->loading);
    }

    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(rowHeight + 6 * scale));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_Block *header = UI_AddBlock(0);
    UI_Parent(header) {
        String names[] = { S("Name"), S("Type"), S("Date modified"), S("Size") };
        UI_Size widths[] = { UI_Flex(), UI_Pixels(150 * scale), UI_Pixels(150 * scale), UI_Pixels(100 * scale) };
        for (u32 i = 0; i < Prog_Sort_Count; ++i) {
            String label = state->sort == i ? StrFormat(&state->frameArena, "%S %s", names[i], state->descending ? "v" : "^") : names[i];
            Prog__Button(state, names[i], label, widths[i], rowHeight + 6 * scale, scale, Prog_CommandKind_Sort, i, !state->loading);
        }
    }

    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextColors((UI_Colors){ .background = { 0.10f, 0.11f, 0.12f, 1 }, .text = colors.text });
    UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Files")), UI_Flag_DrawBackground | UI_Flag_Clip | UI_Flag_Scrollable);
    if (state->resetScroll) { list->scrollOffset = (UI_Vec2){0}; state->resetScroll = false; }
    state->pageRows = (u64)(Max(0.0f, list->rect.y1 - list->rect.y0) / rowHeight);
    if (state->revealFocus && state->hasFocus) {
        f32 top = (f32)state->focusedRow * rowHeight;
        f32 height = Max(0.0f, list->rect.y1 - list->rect.y0);
        if (top < list->scrollOffset.y) list->scrollOffset.y = top;
        else if (top + rowHeight > list->scrollOffset.y + height) list->scrollOffset.y = Max(0.0f, top + rowHeight - height);
        state->revealFocus = false;
    }
    u64 first = (u64)(Max(0.0f, list->scrollOffset.y) / rowHeight);
    if (first > 2) first -= 2; else first = 0;
    first = Min(first, state->directory.count);
    f32 visibleHeight = Max(rowHeight, UI->root->rect.y1 - UI->root->rect.y0);
    u64 end = Min(state->directory.count, first + (u64)(visibleHeight / rowHeight) + 6);
    UI_Parent(list) {
        if (first) {
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels((f32)first * rowHeight));
            UI_AddBlock(0);
        }
        UI_Key directoryKey = UI_KeyFromStr(list->key, state->directory.path);
        for (u64 i = first; i < end; ++i) {
            Prog_Entry *entry = &state->directory.entries[i];
            UI_Key key = UI_KeyFromStr(directoryKey, entry->file.name);
            R_Color background = { 0.10f, 0.11f, 0.12f, 1 };
            if (UI->hotKey == key && !state->loading) background = (R_Color){ 0.18f, 0.19f, 0.21f, 1 };
            if (entry->selected) background = (R_Color){ 0.24f, 0.25f, 0.27f, 1 };
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(rowHeight));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextColors((UI_Colors){ .background = background, .border = { 0.40f, 0.41f, 0.43f, 1 }, .text = colors.text });
            UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | (!state->loading ? UI_Flag_LeftMouseClickable : 0) | (state->hasFocus && state->focusedRow == i ? UI_Flag_DrawBorder : 0));
            UI_Interaction interaction = UI_InteractionFromBlock(row);
            if (!state->loading && interaction.pressed) {
                Prog_Command *command = Prog_QueueCommand(state, Prog_CommandKind_SelectRow, i, interaction.pressCount, interaction.pressOrder);
                command->mods = interaction.mods;
            }
            UI_Parent(row) { Prog__Columns(entry, rowHeight, scale); }
        }
        if (end < state->directory.count) {
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels((f32)(state->directory.count - end) * rowHeight));
            UI_AddBlock(0);
        }
        if (!state->directory.count && !state->loading) Prog__Label(S("This folder is empty"), UI_Flex(), rowHeight, scale);
    }

    String status = state->error;
    if (!status.length) {
        status = state->loading ? StrFormat(&state->frameArena, "Loading %S...", ((Prog_JobInput *)state->directoryJob->input)->path)
            : StrFormat(&state->frameArena, "%" PRIu64 " items | %" PRIu64 " selected", state->directory.count, state->selectedCount);
    }
    Prog__Label(status, UI_Flex(), 26 * scale, scale);
}
