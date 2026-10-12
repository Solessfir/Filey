function void Prog__Label(String text, UI_Size width, f32 height, f32 scale)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(UI_Pixels(height));
    UI_SetNextChildrenPadding((UI_Padding){ 12.0f * scale, 0, 12.0f * scale, 0 });
    UI_SetNextTextAlignment((UI_Vec2){ 0, 0.5f });
    UI_SetNextText(text);
    UI_AddBlock(UI_Flag_DrawText | UI_Flag_Clip);
}

function void Prog__Columns(String name, String type, String modified, String size, f32 height, f32 scale)
{
    Prog__Label(name, UI_Flex(), height, scale);
    Prog__Label(type, UI_Percent(0.18f), height, scale);
    Prog__Label(modified, UI_Pixels(170.0f * scale), height, scale);
    Prog__Label(size, UI_Pixels(100.0f * scale), height, scale);
}

function void Prog_BuildUI(Prog_State *state, const R_Font *font, f32 scale)
{
    UI_Colors colors = UI->defaultColors;
    UI->root->props.childrenPadding = (UI_Padding){ 20.0f * scale, 16.0f * scale, 20.0f * scale, 16.0f * scale };
    Prog__Label(S("Filey - demo files"), UI_Flex(), 44.0f * scale, scale);

    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(34.0f * scale));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextColors((UI_Colors){ .background = { 0.17f, 0.18f, 0.21f, 1 }, .text = colors.text });
    UI_Block *header = UI_AddBlock(UI_Flag_DrawBackground);
    UI_Parent(header) {
        Prog__Columns(S("Name"), S("Type"), S("Date modified"), S("Size"), 34.0f * scale, scale);
    }

    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Flex());
    UI_SetNextColors((UI_Colors){ .background = { 0.10f, 0.11f, 0.13f, 1 }, .border = colors.border, .text = colors.text });
    UI_Block *list = UI_AddBlockFromKey(UI_KeyFromStr(0, S("Files")), UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_Clip | UI_Flag_Scrollable);
    UI_Parent(list) {
        for (u32 i = 0; i < 80; ++i) {
            String identity = StrFormat(&state->frameArena, "File%u", i);
            UI_Key key = UI_KeyFromStr(list->key, identity);
            R_Color background = { 0.12f, 0.13f, 0.15f, 1 };
            if ((i & 1) == 0) {
                background = (R_Color){ 0.10f, 0.11f, 0.13f, 1 };
            }
            if (UI->hotKey == key) {
                background = (R_Color){ 0.19f, 0.22f, 0.27f, 1 };
            }
            if (state->hasSelection && state->selectedRow == i) {
                background = (R_Color){ 0.18f, 0.32f, 0.50f, 1 };
            }
            UI_SetNextPrefWidth(UI_Flex());
            UI_SetNextPrefHeight(UI_Pixels(30.0f * scale));
            UI_SetNextChildrenLayout(UI_Layout_Horizontal);
            UI_SetNextColors((UI_Colors){ .background = background, .text = colors.text });
            UI_Block *row = UI_AddBlockFromKey(key, UI_Flag_DrawBackground | UI_Flag_LeftMouseClickable);
            UI_Interaction interaction = UI_InteractionFromBlock(row);
            if (interaction.pressed) {
                Prog_QueueCommand(state, Prog_CommandKind_SelectRow, i, interaction.pressCount, interaction.pressOrder);
            }
            UI_Parent(row) {
                String name = i == 0 ? S("Документы") : StrFormat(&state->frameArena, "Document %02u.txt", i);
                String size = i == 0 ? S("") : StrFormat(&state->frameArena, "%u KB", i * 7 + 1);
                Prog__Columns(name, i == 0 ? S("Folder") : S("Text document"), S("12.10.2026 18:00"), size, 30.0f * scale, scale);
            }
        }
    }

    UI_SetNextPrefWidth(UI_Flex());
    UI_SetNextPrefHeight(UI_Pixels(50.0f * scale));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenAlignment((UI_Vec2){ 0, 0.5f });
    UI_Block *footer = UI_AddBlock(0);
    UI_Parent(footer) {
        String status = state->hasSelection
            ? StrFormat(&state->frameArena, "80 demo items | Selected %u | Opened %u | Refreshed %u", state->selectedRow + 1, state->openCount, state->refreshCount)
            : StrFormat(&state->frameArena, "80 demo items | Scroll to browse | Refreshed %u", state->refreshCount);
        Prog__Label(status, UI_Flex(), font->lineHeight, scale);
        UI_SetNextPrefWidth(UI_TextContent());
        UI_SetNextPrefHeight(UI_Pixels(34.0f * scale));
        UI_SetNextChildrenPadding((UI_Padding){ 16.0f * scale, 0, 16.0f * scale, 0 });
        UI_SetNextTextAlignment((UI_Vec2){ 0.5f, 0.5f });
        UI_SetNextText(S("Refresh demo"));
        UI_Key buttonKey = UI_KeyFromStr(0, S("Refresh"));
        UI_SetNextColors((UI_Colors){
            .background = UI->hotKey == buttonKey ? (R_Color){ 0.29f, 0.48f, 0.73f, 1 } : (R_Color){ 0.23f, 0.39f, 0.62f, 1 },
            .border = colors.border, .text = colors.text,
        });
        UI_Block *button = UI_AddBlockFromKey(buttonKey, UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText | UI_Flag_LeftMouseClickable);
        UI_Interaction interaction = UI_InteractionFromBlock(button);
        if (interaction.clicked) {
            Prog_QueueCommand(state, Prog_CommandKind_Refresh, 0, 0, interaction.clickOrder);
        }
    }
}
