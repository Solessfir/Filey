typedef struct TestUIDraw {
    R_Rect rect, clip;
    R_Color color;
    u32 kind;
} TestUIDraw;

global TestUIDraw TestUIDraws[64];
global u32 TestUIDrawCount, TestUIClipDepth;
global R_Rect TestUIClips[64];
global const R_Font TestUIFont = { .lineHeight = 16 };

function void TestUIRecordDraw(R_Rect rect, R_Color color, u32 kind)
{
    Check(TestUIDrawCount < CArrayCount(TestUIDraws));
    if (TestUIDrawCount < CArrayCount(TestUIDraws)) {
        TestUIDraws[TestUIDrawCount++] = (TestUIDraw){
            .rect = rect, .color = color, .kind = kind,
            .clip = TestUIClipDepth ? TestUIClips[TestUIClipDepth - 1] : (R_Rect){0},
        };
    }
}

function f32 R_TextWidth(const R_Font *font, String text)
{
    u64 position = 0, count = 0;
    while (position < text.length) {
        StrDecodeUtf8(text, &position);
        ++count;
    }
    return font ? (f32)count * 8 : 0;
}

function void R_DrawRect(R_Rect rect, R_Color color, f32 cornerRadius)
{
    TestUIRecordDraw(rect, color, 0);
}

function void R_DrawRectOutline(R_Rect rect, R_Color color, f32 cornerRadius, f32 thickness)
{
    TestUIRecordDraw(rect, color, 1);
}

function void R_DrawText(const R_Font *font, f32 x, f32 y, R_Color color, String text)
{
    TestUIRecordDraw((R_Rect){x, y, x + R_TextWidth(font, text), y + font->lineHeight}, color, 2);
}

// The tests never touch the real clipboard.
global Arena TestClipboardArena;
global String TestClipboard;

global ArrayStr TestClipboardFiles;

function void PlatformClipboardSetText(String text)
{
    ArenaClear(&TestClipboardArena);
    TestClipboardFiles = (ArrayStr){0};
    TestClipboard = StrCopy(&TestClipboardArena, text);
}

function String PlatformClipboardGetText(Arena *arena)
{
    return StrCopy(arena, TestClipboard);
}

global b32 TestClipboardCut;

function void PlatformClipboardSetFiles(const String *paths, u64 count, b32 cut)
{
    ArenaClear(&TestClipboardArena);
    TestClipboard = (String){0};
    TestClipboardFiles = (ArrayStr){0};
    for (u64 i = 0; i < count; ++i) ArrayPush(&TestClipboardArena, TestClipboardFiles, StrCopy(&TestClipboardArena, paths[i]));
    TestClipboardCut = cut;
}

function b32 PlatformClipboardGetFiles(Arena *arena, ArrayStr *paths, b32 *cut)
{
    *paths = (ArrayStr){0};
    for (u64 i = 0; i < TestClipboardFiles.header.count; ++i) ArrayPush(arena, *paths, StrCopy(arena, TestClipboardFiles.v[i]));
    *cut = TestClipboardCut;
    return paths->header.count != 0;
}

// The window code is left out too.
global u32 TestWindowCalls[4];  // minimize, maximize, close, move

function b32 PlatformWindowHasButtons(void) { return true; }
function b32 PlatformHasBackdrops(void) { return true; }
function b32 PlatformHasVSync(void) { return true; }
function b32 PlatformWindowIsMaximized(void) { return false; }
function b32 PlatformWindowNeedsBorder(void) { return false; }
function PlatformWindowInfo PlatformWindowGetInfo(void) { return (PlatformWindowInfo){0}; }
function void PlatformWindowMinimize(void) { ++TestWindowCalls[0]; }
function void PlatformWindowToggleMaximize(void) { ++TestWindowCalls[1]; }
function void PlatformWindowToggleFullscreen(void) {}
function void PlatformWindowClose(void) { ++TestWindowCalls[2]; }
function void PlatformWindowStartMove(void) { ++TestWindowCalls[3]; }

#if defined(__linux__)
// Drags start in the window code, which the tests leave out.
function b32 LinuxStartDrag(String directory, const String *names, u64 count)
{
    return false;
}
#endif

function R_TextureHandle R_TextureCreateRGBA(i32 width, i32 height, const u32 *pixels)
{
    return (R_TextureHandle){ { 1 } };
}

function void R_TextureUpdateRGBA(R_TextureHandle texture, i32 x, i32 y, i32 width, i32 height, const u32 *pixels)
{
}

function void R_TextureRelease(R_TextureHandle texture)
{
}

function void R_BlurBehind(R_Rect rect, f32 cornerRadius, f32 radius, f32 opacity)
{
}

function void R_DrawSoftRect(R_Rect rect, R_Color color, f32 cornerRadius, f32 softness)
{
}

function void R_DrawImage(R_Rect rect, R_Rect uv, R_TextureHandle texture, R_Color tint)
{
    TestUIRecordDraw(rect, tint, 3);
}

function void R_PushClip(R_Rect rect)
{
    Check(TestUIClipDepth < CArrayCount(TestUIClips));
    if (TestUIClipDepth) {
        R_Rect enclosing = TestUIClips[TestUIClipDepth - 1];
        rect = (R_Rect){MaxF32(rect.x0, enclosing.x0), MaxF32(rect.y0, enclosing.y0),
                        MinF32(rect.x1, enclosing.x1), MinF32(rect.y1, enclosing.y1)};
    }
    if (TestUIClipDepth < CArrayCount(TestUIClips)) {
        TestUIClips[TestUIClipDepth++] = rect;
    }
}

function void R_PopClip(void)
{
    Check(TestUIClipDepth != 0);
    if (TestUIClipDepth) --TestUIClipDepth;
}

function void R_SetShape(u32 shape) {}
function void R_BeginBlurSource(void) {}
function void R_EndBlurSource(void) {}

global u32 TestUIScaleDepth;
function void R_PushScale(f32 x, f32 y, f32 scale) { ++TestUIScaleDepth; }
function void R_PopScale(void)
{
    Check(TestUIScaleDepth != 0);
    if (TestUIScaleDepth) --TestUIScaleDepth;
}

global u32 TestUILayerDepth;
function void R_PushLayer(R_Rect rect) { ++TestUILayerDepth; }
function void R_PopLayer(f32 opacity)
{
    Check(TestUILayerDepth != 0);
    if (TestUILayerDepth) --TestUILayerDepth;
}

function void TestUIBegin(UI_Context *context, Arena *frame, PlatformEventList events, u64 now)
{
    ArenaClear(frame);
    context->defaultFont = &TestUIFont;
    UI_BeginFrame(context, frame, (R_Rect){0, 0, 300, 200}, events, now);
}

function UI_Block *TestUIBlock(UI_Key key, UI_Flags flags, UI_Size width, UI_Size height)
{
    UI_SetNextPrefWidth(width);
    UI_SetNextPrefHeight(height);
    return UI_AddBlockFromKey(key, flags);
}

function void TestUIRect(UI_Block *block, f32 x0, f32 y0, f32 x1, f32 y1)
{
    Check(block->rect.x0 == x0 && block->rect.y0 == y0);
    Check(block->rect.x1 == x1 && block->rect.y1 == y1);
}

function PlatformEventList TestUIEvents(PlatformEvent *events, u64 count)
{
    for (u64 i = 0; i < count; ++i) events[i].next = i + 1 < count ? &events[i + 1] : NULL;
    return (PlatformEventList){.first = count ? events : NULL, .last = count ? &events[count - 1] : NULL, .count = count};
}

function void TestUILayout(void)
{
    UI_Context context = {0};
    Arena frame = {0};
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 0);
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenPadding((UI_Padding){10, 10, 10, 10});
    UI_Block *horizontal = TestUIBlock(1, 0, UI_Pixels(220), UI_Pixels(100));
    UI_Block *pixels, *percent, *flex;
    UI_Parent(horizontal) {
        pixels = TestUIBlock(2, 0, UI_Pixels(40), UI_Pixels(20));
        percent = TestUIBlock(3, 0, UI_Percent(0.25f), UI_Percent(0.5f));
        flex = TestUIBlock(4, 0, UI_Flex(), UI_Flex());
    }
    UI_EndFrame();
    TestUIRect(horizontal, 0, 0, 220, 100);
    TestUIRect(pixels, 10, 10, 50, 30);
    TestUIRect(percent, 50, 10, 100, 50);
    TestUIRect(flex, 100, 10, 210, 90);
    Check(context.parent == context.root);

    TestUIBegin(&context, &frame, (PlatformEventList){0}, 1);
    UI_SetNextChildrenPadding((UI_Padding){10, 10, 10, 10});
    UI_Block *vertical = TestUIBlock(1, 0, UI_Pixels(100), UI_Pixels(180));
    UI_Block *flexA, *flexB;
    UI_Parent(vertical) {
        pixels = TestUIBlock(2, 0, UI_Pixels(20), UI_Pixels(40));
        percent = TestUIBlock(3, 0, UI_Percent(0.5f), UI_Percent(0.25f));
        flexA = TestUIBlock(4, 0, UI_Flex(), UI_Flex());
        flexB = TestUIBlock(5, 0, UI_Flex(), UI_Flex());
    }
    UI_EndFrame();
    TestUIRect(pixels, 10, 10, 30, 50);
    TestUIRect(percent, 10, 50, 50, 90);
    TestUIRect(flexA, 10, 90, 90, 130);
    TestUIRect(flexB, 10, 130, 90, 170);

    TestUIBegin(&context, &frame, (PlatformEventList){0}, 2);
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenPadding((UI_Padding){5, 6, 7, 8});
    UI_Block *fill = TestUIBlock(1, 0, UI_Fill(), UI_Fill());
    UI_Block *text, *floating;
    UI_Parent(fill) {
        UI_SetNextText(S("ab\xC3\xA9\xE2\x82\xAC"));
        text = TestUIBlock(2, UI_Flag_DrawText, UI_TextContent(), UI_TextContent());
        pixels = TestUIBlock(3, 0, UI_Pixels(20), UI_Pixels(10));
        UI_SetNextFloatingOffset((UI_Vec2){100, 40});
        floating = TestUIBlock(4, UI_Flag_Floating, UI_Pixels(200), UI_Pixels(200));
    }
    UI_EndFrame();
    TestUIRect(fill, 0, 0, 64, 30);
    TestUIRect(text, 5, 6, 37, 22);
    TestUIRect(pixels, 37, 6, 57, 16);
    TestUIRect(floating, 105, 46, 305, 246);

    TestUIBegin(&context, &frame, (PlatformEventList){0}, 3);
    UI_SetNextChildrenAlignment((UI_Vec2){0.5f, 1});
    vertical = TestUIBlock(1, 0, UI_Pixels(100), UI_Pixels(100));
    UI_Parent(vertical) {
        pixels = TestUIBlock(2, 0, UI_Pixels(20), UI_Pixels(10));
    }
    UI_EndFrame();
    TestUIRect(pixels, 40, 90, 60, 100);

    UI_Release(&context);
    ArenaRelease(&frame);
}

function void TestUIPropertiesAndLifetime(void)
{
    UI_Context context = {0};
    Arena frame = {0};
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 0);
    UI_Block *plain = UI_AddBlock(0);
    UI_Properties defaults = plain->props;
    UI_SetNextPrefWidth(UI_Pixels(41));
    UI_SetNextPrefHeight(UI_Pixels(17));
    UI_SetNextChildrenLayout(UI_Layout_Horizontal);
    UI_SetNextChildrenPadding((UI_Padding){1, 2, 3, 4});
    UI_SetNextChildrenAlignment((UI_Vec2){1, 1});
    UI_SetNextTextAlignment((UI_Vec2){1, 1});
    UI_SetNextColors((UI_Colors){.background = {1, 0, 0, 1}});
    UI_SetNextFont(NULL);
    UI_SetNextText(S("temporary"));
    UI_SetNextFloatingOffset((UI_Vec2){7, 8});
    UI_SetNextScrollOffset((UI_Vec2){2, 3});
    UI_Block *retained = UI_AddBlockFromKey(11, UI_Flag_Floating);
    plain = UI_AddBlock(0);
    Check(retained->props.width.value == 41 && retained->props.height.value == 17);
    Check(retained->props.childrenPadding.bottom == 4 && retained->props.font == NULL);
    Check(retained->props.colors.background.r == 1 && StrIsEqual(retained->props.text, S("temporary")));
    Check(plain->props.width.kind == defaults.width.kind && plain->props.height.kind == defaults.height.kind);
    Check(plain->props.childrenLayout == defaults.childrenLayout && plain->props.childrenPadding.bottom == defaults.childrenPadding.bottom);
    Check(plain->props.childrenAlignment.x == defaults.childrenAlignment.x && plain->props.textAlignment.x == defaults.textAlignment.x);
    Check(plain->props.font == defaults.font && plain->props.colors.background.r == defaults.colors.background.r);
    Check(plain->props.text.length == 0 && plain->props.floatingOffset.x == 0);
    Check(plain->scrollOffset.x == 0 && plain->scrollOffset.y == 0);
    UI_EndFrame();

    TestUIBegin(&context, &frame, (PlatformEventList){0}, 1);
    UI_Block *same = UI_AddBlockFromKey(11, 0);
    Check(same == retained && same->props.text.length == 0 && same->flags == 0);
    UI_EndFrame();
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 2);
    UI_EndFrame();
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 3);
    UI_Block *reused = UI_AddBlockFromKey(12, 0);
    Check(reused == retained && reused->key == 12 && reused->interaction.key != 11);
    UI_EndFrame();
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 4);
    UI_EndFrame();
    u64 permanentPos = ArenaPos(&context.arena);
    for (u64 i = 0; i < 100; ++i) {
        TestUIBegin(&context, &frame, (PlatformEventList){0}, 5 + i);
        UI_SetNextText(StrCopy(&frame, S("frame-owned")));
        plain = UI_AddBlock(0);
        Check(plain->key == 0 && StrIsEqual(plain->props.text, S("frame-owned")));
        Check((u8 *)plain >= (u8 *)frame.base && (u8 *)plain < (u8 *)frame.base + ArenaPos(&frame));
        UI_EndFrame();
        Check(ArenaPos(&context.arena) == permanentPos);
    }
    Check(UI_KeyFromStr(0, S("row")) != 0);
    Check(UI_KeyFromStr(1, S("row")) == UI_KeyFromStr(1, StrCopy(&frame, S("row"))));
    Check(UI_KeyFromStr(1, S("row")) != UI_KeyFromStr(2, S("row")));
    UI_Release(&context);
    Check(UI == NULL && context.arena.base == NULL);
    ArenaRelease(&frame);
}

function void TestUINestedSizing(void)
{
    UI_Context context = {0};
    Arena frame = {0};
    for (u32 axis = 0; axis < 2; ++axis) {
        ArenaClear(&frame);
        UI_BeginFrame(&context, &frame, (R_Rect){0, 0, 300, 300}, (PlatformEventList){0}, axis);
        context.root->props.childrenLayout = axis ? UI_Layout_Vertical : UI_Layout_Horizontal;
        TestUIBlock(1, 0, UI_Pixels(100), UI_Pixels(100));
        UI_Block *flex = TestUIBlock(2, 0, axis ? UI_Pixels(100) : UI_Flex(), axis ? UI_Flex() : UI_Pixels(100));
        UI_Block *fill, *percent;
        UI_Parent(flex) {
            fill = TestUIBlock(3, 0, axis ? UI_Pixels(100) : UI_Fill(), axis ? UI_Fill() : UI_Pixels(100));
            UI_Parent(fill) {
                percent = TestUIBlock(4, 0, axis ? UI_Pixels(100) : UI_Percent(0.5f), axis ? UI_Percent(0.5f) : UI_Pixels(100));
            }
        }
        UI_EndFrame();
        Check((axis ? flex->rect.y1 - flex->rect.y0 : flex->rect.x1 - flex->rect.x0) == 200);
        Check((axis ? fill->rect.y1 - fill->rect.y0 : fill->rect.x1 - fill->rect.x0) == 100);
        Check((axis ? percent->rect.y1 - percent->rect.y0 : percent->rect.x1 - percent->rect.x0) == 50);
    }
    UI_Release(&context);
    ArenaRelease(&frame);
}

function void TestUIInteractions(void)
{
    UI_Context context = {0};
    Arena frame = {0};
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 1000000);
    TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    UI_EndFrame();

    PlatformEvent click[] = {
        {.kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = 20, .y = 20, .mods = KeyMod_Control},
        {.kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = 20, .y = 20},
    };
    TestUIBegin(&context, &frame, TestUIEvents(click, CArrayCount(click)), 1100000);
    UI_Block *button = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    UI_Interaction interaction = UI_InteractionFromBlock(button);
    Check(interaction.pressed && interaction.released && interaction.clicked && !interaction.active);
    Check(interaction.key == 1 && interaction.pressCount == 1 && interaction.mods == KeyMod_Control);
    UI_EndFrame();

    TestUIBegin(&context, &frame, TestUIEvents(click, CArrayCount(click)), 1200000);
    button = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    interaction = UI_InteractionFromBlock(button);
    Check(interaction.clicked && interaction.pressCount == 2);
    TestUIBlock(2, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    UI_EndFrame();

    // The pointer leaving the window is over nothing.
    PlatformEvent leave[] = { {.kind = PlatformEventKind_MouseLeave} };
    Check(context.hotKey == 1);
    TestUIBegin(&context, &frame, TestUIEvents(leave, CArrayCount(leave)), 1250000);
    Check(context.hotKey == 0 && !context.mouseKnown);
    TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    TestUIBlock(2, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    UI_EndFrame();

    PlatformEvent twoClicks[] = {
        click[0], click[1],
        {.kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = 20, .y = 60, .mods = KeyMod_Shift},
        {.kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = 20, .y = 60},
    };
    TestUIBegin(&context, &frame, TestUIEvents(twoClicks, CArrayCount(twoClicks)), 1300000);
    button = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    UI_Block *otherButton = TestUIBlock(2, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    interaction = UI_InteractionFromBlock(button);
    UI_Interaction otherInteraction = UI_InteractionFromBlock(otherButton);
    Check(interaction.clicked && interaction.pressCount == 3 && interaction.mods == KeyMod_Control);
    Check(otherInteraction.clicked && otherInteraction.pressCount == 1 && otherInteraction.mods == KeyMod_Shift);
    UI_EndFrame();

    PlatformEvent reversed[] = {click[1], click[0]};
    TestUIBegin(&context, &frame, TestUIEvents(reversed, CArrayCount(reversed)), 2000000);
    button = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    interaction = UI_InteractionFromBlock(button);
    Check(interaction.pressed && interaction.active && !interaction.clicked);
    Check(context.activeKey == 1 && interaction.pressCount == 1);
    UI_EndFrame();

    PlatformEvent focusLost = {.kind = PlatformEventKind_FocusChanged, .focused = false};
    TestUIBegin(&context, &frame, TestUIEvents(&focusLost, 1), 2100000);
    button = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    Check(!UI_InteractionFromBlock(button).active && context.activeKey == 0);
    UI_EndFrame();

    TestUIBegin(&context, &frame, TestUIEvents(click, 1), 3000000);
    button = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    Check(UI_InteractionFromBlock(button).active);
    UI_EndFrame();
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 3100000);
    UI_EndFrame();
    Check(context.activeKey == 0);
    TestUIBegin(&context, &frame, TestUIEvents(&click[1], 1), 3200000);
    button = TestUIBlock(2, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    Check(!UI_InteractionFromBlock(button).clicked && !UI_InteractionFromBlock(button).active);
    UI_EndFrame();

    // Event timestamps and the native interval decide double-clicks, not the frame time.
    context.doubleClickMicroseconds = 300000;
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 4000000);
    TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    UI_EndFrame();
    PlatformEvent slow[] = {
        {.kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = 20, .y = 20, .timeMicroseconds = 5000000},
        {.kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = 20, .y = 20, .timeMicroseconds = 5000000},
        {.kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = 20, .y = 20, .timeMicroseconds = 5400000},
        {.kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = 20, .y = 20, .timeMicroseconds = 5400000},
    };
    TestUIBegin(&context, &frame, TestUIEvents(slow, CArrayCount(slow)), 5400000);
    button = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    Check(UI_InteractionFromBlock(button).pressCount == 1);
    UI_EndFrame();
    PlatformEvent fast[] = {
        {.kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = 20, .y = 20, .timeMicroseconds = 5600000},
        {.kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = 20, .y = 20, .timeMicroseconds = 5600000},
    };
    TestUIBegin(&context, &frame, TestUIEvents(fast, CArrayCount(fast)), 9000000);
    button = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
    Check(UI_InteractionFromBlock(button).pressCount == 2);
    UI_EndFrame();
    UI_Release(&context);
    ArenaRelease(&frame);
}

function void TestUIClippingAndDrawOrder(void)
{
    UI_Context context = {0};
    Arena frame = {0};
    UI_Block *child = NULL, *overlay = NULL;
    for (u32 iteration = 0; iteration < 3; ++iteration) {
        PlatformEvent click[] = {
            {.kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = iteration == 1 ? 60 : 10, .y = 10},
            {.kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = iteration == 1 ? 60 : 10, .y = 10},
        };
        TestUIBegin(&context, &frame, iteration ? TestUIEvents(click, 2) : (PlatformEventList){0}, 1000000 + iteration * 100000);
        UI_SetNextColors((UI_Colors){.background = {1, 0, 0, 1}});
        UI_Block *clip = TestUIBlock(1, UI_Flag_Clip | UI_Flag_DrawBackground, UI_Pixels(50), UI_Pixels(50));
        UI_Parent(clip) {
            UI_SetNextColors((UI_Colors){.background = {2, 0, 0, 1}});
            child = TestUIBlock(2, UI_Flag_DrawBackground | UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(30));
        }
        UI_SetNextColors((UI_Colors){.background = {3, 0, 0, 1}, .border = {4, 0, 0, 1}, .text = {5, 0, 0, 1}});
        UI_SetNextText(S("A"));
        UI_SetNextTextAlignment((UI_Vec2){1, 1});
        overlay = TestUIBlock(3, UI_Flag_Floating | UI_Flag_DrawBackground | UI_Flag_DrawBorder | UI_Flag_DrawText | UI_Flag_LeftMouseClickable, UI_Pixels(20), UI_Pixels(30));
        if (iteration == 1) {
            Check(!UI_InteractionFromBlock(child).clicked && !UI_InteractionFromBlock(overlay).clicked);
        } else if (iteration == 2) {
            Check(!UI_InteractionFromBlock(child).clicked && UI_InteractionFromBlock(overlay).clicked);
        }
        UI_EndFrame();
    }
    TestUIRect(child, 0, 0, 100, 30);
    Check(child->clipRect.x1 == 50);
    TestUIDrawCount = TestUIClipDepth = 0;
    UI_Draw();
    Check(TestUIClipDepth == 0 && TestUIDrawCount == 5);
    Check(TestUIDraws[0].color.r == 1 && TestUIDraws[1].color.r == 2 && TestUIDraws[2].color.r == 3);
    Check(TestUIDraws[0].clip.x1 == 0 && TestUIDraws[2].clip.x1 == 0);
    Check(TestUIDraws[1].clip.x1 == 50 && TestUIDraws[1].clip.y1 == 50);
    Check(TestUIDraws[3].kind == 1 && TestUIDraws[3].color.r == 4);
    Check(TestUIDraws[4].kind == 2 && TestUIDraws[4].color.r == 5);
    Check(TestUIDraws[4].rect.x0 == 12 && TestUIDraws[4].rect.y0 == 14);
    UI_Release(&context);
    ArenaRelease(&frame);
}

function void TestUIScrolling(void)
{
    UI_Context context = {0};
    Arena frame = {0};
    for (u32 iteration = 0; iteration < 4; ++iteration) {
        PlatformEvent wheel = {
            .kind = PlatformEventKind_MouseWheel,
            .x = iteration == 3 ? 90 : 10, .y = iteration == 3 ? 80 : 10,
            .wheelY = iteration == 2 ? 1000 : -1000,
        };
        TestUIBegin(&context, &frame, iteration ? TestUIEvents(&wheel, 1) : (PlatformEventList){0}, iteration);
        if (!iteration) UI_SetNextScrollOffset((UI_Vec2){-10, -10});
        UI_Block *outer = TestUIBlock(1, UI_Flag_Scrollable, UI_Pixels(100), UI_Pixels(100));
        UI_Block *inner, *content;
        UI_Parent(outer) {
            if (!iteration) UI_SetNextScrollOffset((UI_Vec2){-10, -10});
            inner = TestUIBlock(2, UI_Flag_Scrollable, UI_Pixels(80), UI_Pixels(50));
            UI_Parent(inner) {
                content = TestUIBlock(3, UI_Flag_LeftMouseClickable, UI_Pixels(40), UI_Pixels(200));
            }
            TestUIBlock(4, 0, UI_Pixels(80), UI_Pixels(200));
        }
        UI_EndFrame();
        Check(outer->scrollOffset.x == 0 && inner->scrollOffset.x == 0);
        Check(outer->scrollOffset.y == (iteration == 3 ? 150 : 0));
        Check(inner->scrollOffset.y == (iteration == 1 ? 150 : 0));
        if (iteration == 1) {
            TestUIRect(content, 0, -150, 40, 50);
            Check(content->clipRect.y0 == 0 && content->clipRect.y1 == 50);
        }
    }
    UI_Release(&context);
    ArenaRelease(&frame);
}

// A slide starts no further than its cap from where it goes, and a fall rises at once and eases down.
function void TestUISlides(void)
{
    UI_Context context = { .animate = true };
    Arena frame = {0};
    UI_Key slide = UI_KeyFromStr(0, S("Slide")), fall = UI_KeyFromStr(0, S("Fall"));
    f32 values[4] = {0};
    for (u32 i = 0; i < 4; ++i) {
        TestUIBegin(&context, &frame, (PlatformEventList){0}, 1000000 + (u64)i * 16667);
        values[i] = UI_Slide(slide, i ? 300.0f : 0.0f, 30);
        f32 washed = UI_Fall(fall, i == 1 || i == 3 ? 1.0f : 0.0f);
        Check(i == 2 ? washed > 0 && washed < 1 : washed == (i ? 1.0f : 0.0f));
        UI_EndFrame();
    }
    Check(values[0] == 0 && values[1] > 270 && values[1] < 300 && values[2] > values[1] && values[3] > values[2]);
    UI_Release(&context);
    ArenaRelease(&frame);
}

function void TestUIFrameFontLifetime(void)
{
    UI_Context context = {0};
    Arena frame = {0};
    TestUIBegin(&context, &frame, (PlatformEventList){0}, 0);
    // Put the font in a chained block released by the next frame's clear.
    _ArenaPush(&frame, frame.reserved - frame.used, 1, 0);
    R_Font *font = ArenaPushStruct(&frame, R_Font);
    font->lineHeight = 32;
    Check(frame.prev != NULL);
    UI_SetNextFont(font);
    UI_SetNextText(S("scroll"));
    UI_Block *scroll = TestUIBlock(1, UI_Flag_Scrollable | UI_Flag_DrawText, UI_Pixels(100), UI_Pixels(100));
    UI_Parent(scroll) {
        TestUIBlock(2, 0, UI_Pixels(100), UI_Pixels(300));
    }
    UI_EndFrame();
    TestUIDrawCount = TestUIClipDepth = 0;
    UI_Draw();
    Check(TestUIDrawCount == 1 && TestUIDraws[0].rect.y1 == 32);
    PlatformEvent wheel = {.kind = PlatformEventKind_MouseWheel, .x = 10, .y = 10, .wheelY = -1};
    TestUIBegin(&context, &frame, TestUIEvents(&wheel, 1), 1);
    Check(frame.prev == NULL);
    scroll = TestUIBlock(1, UI_Flag_Scrollable, UI_Pixels(100), UI_Pixels(100));
    UI_Parent(scroll) {
        TestUIBlock(2, 0, UI_Pixels(100), UI_Pixels(300));
    }
    UI_EndFrame();
    Check(scroll->scrollOffset.y == 96);
    UI_Release(&context);
    ArenaRelease(&frame);
}

function void TestUICommandEventOrder(void)
{
    Prog_State state = {0};
    Prog_Entry entries[2] = {0};
    state.directory.entries = entries;
    state.directory.count = CArrayCount(entries);
    Prog_ApplyFilter(&state, false);
    PlatformEvent clicks[] = {
        {.kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = 20, .y = 60},
        {.kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = 20, .y = 60},
        {.kind = PlatformEventKind_MouseDown, .button = MouseButton_Left, .x = 20, .y = 20},
        {.kind = PlatformEventKind_MouseUp, .button = MouseButton_Left, .x = 20, .y = 20},
    };
    for (u32 frameIndex = 0; frameIndex < 2; ++frameIndex) {
        TestUIBegin(&state.ui, &state.frameArena, frameIndex ? TestUIEvents(clicks, CArrayCount(clicks)) : (PlatformEventList){0}, frameIndex);
        UI_Block *upper = TestUIBlock(1, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
        UI_Interaction upperInteraction = UI_InteractionFromBlock(upper);
        if (upperInteraction.pressed) Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, 0, upperInteraction.pressCount, upperInteraction.pressOrder);
        UI_Block *lower = TestUIBlock(2, UI_Flag_LeftMouseClickable, UI_Pixels(100), UI_Pixels(50));
        UI_Interaction lowerInteraction = UI_InteractionFromBlock(lower);
        if (lowerInteraction.pressed) Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, 1, lowerInteraction.pressCount, lowerInteraction.pressOrder);
        UI_EndFrame();
        if (frameIndex) {
            Check(upperInteraction.pressed && upperInteraction.clicked && upperInteraction.pressOrder == 3 && upperInteraction.clickOrder == 4);
            Check(lowerInteraction.pressed && lowerInteraction.clicked && lowerInteraction.pressOrder == 1 && lowerInteraction.clickOrder == 2);
            Check(state.commandFirst->row == 1 && state.commandLast->row == 0);
            Check(Prog_ProcessCommands(&state) && state.hasFocus && state.focusedRow == 0 && entries[0].selected && !entries[1].selected);
        }
    }
    UI_Release(&state.ui);
    ArenaRelease(&state.frameArena);
    ArenaRelease(&state.viewArena);
}

function void TestProgramCommands(void)
{
    Prog_State state = {0};
    Prog_Entry entries[8] = {0};
    state.directory.entries = entries;
    state.directory.count = CArrayCount(entries);
    Prog_ApplyFilter(&state, false);
    Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, 7, 1, 0);
    Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, 3, 1, 1);
    Check(state.hasFocus && state.focusedRow == 0 && !entries[7].selected);
    Check(state.commandFirst->row == 7 && state.commandLast->row == 3);
    Check(Prog_ProcessCommands(&state));
    Check(state.hasFocus && state.focusedRow == 3 && entries[3].selected && !entries[7].selected);
    Check(state.commandFirst == NULL && state.commandLast == NULL && !Prog_ProcessCommands(&state));
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, 100, 1, 0);
    Check(Prog_ProcessCommands(&state));
    Check(state.hasFocus && state.focusedRow == 3 && entries[3].selected);
    Check(state.commandFirst == NULL && state.commandLast == NULL);
    ArenaClear(&state.frameArena);
    Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, 2, 1, 3);
    Prog_QueueCommand(&state, Prog_CommandKind_SelectRow, 7, 1, 1);
    Check(state.commandFirst->row == 7 && state.commandLast->row == 2);
    Check(Prog_ProcessCommands(&state) && state.focusedRow == 2 && entries[2].selected && !entries[7].selected);
    ArenaClear(&state.frameArena);
    state.pageRows = 4;
    Prog_QueueCommand(&state, Prog_CommandKind_ScrollScrollSelectDown, 0, 0, 0);
    Check(Prog_ProcessCommands(&state) && state.focusedRow == 5 && entries[5].selected && !entries[2].selected);
    Prog_QueueCommand(&state, Prog_CommandKind_ScrollScrollSelectDown, 0, 0, 0);
    Check(Prog_ProcessCommands(&state) && state.focusedRow == 7);
    Prog_QueueCommand(&state, Prog_CommandKind_ScrollScrollSelectUp, 0, 0, 0);
    Check(Prog_ProcessCommands(&state) && state.focusedRow == 4);
    ArenaRelease(&state.frameArena);
    ArenaRelease(&state.viewArena);
}

// Go To scores, worked out by hand from its rules.
// The inspector wraps: a space that does not fit starts the next row, a word that does not moves down whole,
// and a word longer than the row is cut; tabs count to their stops.
function void TestInspectorWrap(void)
{
    u32 starts[8];
    Check(Prog__WrapLine(S("aaaa bbbb"), 9, starts, 8) == 1);
    Check(Prog__WrapLine(S("aaaa bbbb cc"), 9, starts, 8) == 2 && starts[1] == 9);
    Check(Prog__WrapLine(S("aaaa bbbbb"), 9, starts, 8) == 2 && starts[1] == 5);
    Check(Prog__WrapLine(S("abcdefghijk"), 4, starts, 8) == 3 && starts[1] == 4 && starts[2] == 8);
    Check(Prog__TextCells(S("a	b")) == 5 && Prog__WrapLine(S("		x"), 8, starts, 8) == 2 && starts[1] == 2);
    Check(Prog__WrapLine(S(""), 4, starts, 8) == 1);
}

function void TestGoToScore(void)
{
    Check(Prog_FuzzyScore(S("Downloads"), (String){0}, S("down"), NULL, NULL) == 735);
    Check(Prog_FuzzyScore(S("Someone12\\Downloads"), (String){0}, S("DOWN"), NULL, NULL) == 645);
    Check(Prog_FuzzyScore(S("Windows NT"), (String){0}, S("down"), NULL, NULL) == 504);
    Check(Prog_FuzzyScore(S("Downloads\\That"), (String){0}, S("down"), NULL, NULL) == 720);
    Check(Prog_FuzzyScore(S("That"), (String){0}, S("down"), NULL, NULL) == 0);
    // Each word scores on its own, and the later "n" right after the space beats the earlier one.
    Check(Prog_FuzzyScore(S("Windows NT"), (String){0}, S("win nt"), NULL, NULL) == 529 + 293);
    // The filter's cases, in order: the best place of the letters, and its folder in a listing of the subfolders.
    u8 marks[16];
    Check(Prog_EntryScore(S("abc.txt"), S("abc"), NULL) == 538);
    Check(Prog_EntryScore(S("aXbXc_abc.txt"), S("abc"), marks) == 489 && !marks[0] && marks[6] && marks[7] && marks[8] && !marks[9]);
    Check(Prog_EntryScore(S("xxabc.txt"), S("abc"), NULL) == 480 && Prog_EntryScore(S("a1b2c3.txt"), S("abc"), NULL) == 129);
    Check(Prog_EntryScore(S("a_________b_________c.txt"), S("abc"), NULL) <= 0);
    Check(Prog_EntryScore(S("act.txt"), S("c a"), NULL) == 213 && Prog_EntryScore(S("abc.txt"), S("c a"), NULL) == 212);
    Check(Prog_EntryScore(S("abc/zzz.txt"), S("abc"), marks) == 413 && marks[0] && marks[2] && !marks[3] && !marks[4]);
}

function void TestProgramVirtualRows(void)
{
    Prog_State state = {0};
    state.directory.path = S("/fixture");
    state.directory.count = 1000;
    state.directory.entries = ArenaPushArray(&state.directoryArena, state.directory.count, Prog_Entry);
    for (u64 i = 0; i < state.directory.count; ++i) {
        state.directory.entries[i].file.name = StrFormat(&state.directoryArena, "item-%u.txt", (u32)i);
        state.directory.entries[i].type = S("txt file");
    }
    Prog_ApplyFilter(&state, false);
    // The panel's blocks are keyed from its seed.
    Prog__EnsureTab(&state);
    UI_Key listKey = UI_KeyFromStr(Prog_PanelKeySeed(state.panels[0].id), S("Files"));
    UI_Key directoryKey = UI_KeyFromStr(listKey, state.directory.path);
    for (u32 frame = 0; frame < 2; ++frame) {
        if (frame) {
            state.hasFocus = true;
            state.focusedRow = state.directory.count - 1;
            state.revealFocus = true;
        }
        TestUIBegin(&state.ui, &state.frameArena, (PlatformEventList){0}, frame);
        Prog_BuildUI(&state, &TestUIFont, &TestUIFont, 1);
        UI_EndFrame();
        UI_Block *list = UI__Find(listKey);
        // The rows, and past them the overscroll's room, which once laid out leaves the last row at the top.
        f32 tall = (f32)state.directory.count * Prog_RowHeight(&TestUIFont), view = list ? list->rect.y1 - list->rect.y0 : 0;
        Check(list && (list->contentSize.y == tall || (frame && list->contentSize.y == tall + view - Prog_RowHeight(&TestUIFont))));
        if (!list) break;
        u64 rows = 0, retained = 0;
        for (UI_Block *child = list->first; child; child = child->next) {
            if (FlagIntersects(child->flags, UI_Flag_LeftMouseClickable)) {
                ++rows;
                Check(child->clipRect.x0 >= list->clipRect.x0 && child->clipRect.x1 <= list->clipRect.x1);
                if (child->clipRect.y1 > child->clipRect.y0) Check(child->clipRect.y0 >= list->clipRect.y0 && child->clipRect.y1 <= list->clipRect.y1);
            }
        }
        for (u64 slot = 0; slot < UI_HashSlotCount; ++slot) {
            for (UI_Block *block = state.ui.hashSlots[slot]; block; block = block->hashNext) {
                if (block->parent == list && FlagIntersects(block->flags, UI_Flag_LeftMouseClickable)) ++retained;
            }
        }
        Check(rows > 0 && rows < 20 && retained == rows);
        u64 row = frame ? state.directory.count - 1 : 0;
        UI_Block *visible = UI__Find(UI_KeyFromStr(directoryKey, state.directory.entries[row].file.name));
        Check(visible && visible->clipRect.y1 > visible->clipRect.y0);
        if (frame) {
            Check(list->scrollOffset.y > 0 && !state.revealFocus);
            Check(visible && visible->first && FlagIntersects(visible->first->flags, UI_Flag_DrawBorder));
            Check(!UI__Find(UI_KeyFromStr(directoryKey, state.directory.entries[0].file.name)));
        }
    }
    Prog_Release(&state);
}
