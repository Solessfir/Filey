#pragma once

typedef u64 UI_Key;
typedef u64 UI_Flags;

#ifndef BBit
#define BBit(n) Bit(n)
#endif

#define UI_Flag_DrawBackground       BBit(0)
#define UI_Flag_DrawBorder           BBit(1)
#define UI_Flag_DrawText             BBit(2)
#define UI_Flag_LeftMouseClickable   BBit(3)
#define UI_Flag_RightMouseClickable  BBit(4)
#define UI_Flag_MiddleMouseClickable BBit(5)
#define UI_Flag_MouseClickable (UI_Flag_LeftMouseClickable | UI_Flag_RightMouseClickable | UI_Flag_MiddleMouseClickable)
#define UI_Flag_Floating             BBit(6)
#define UI_Flag_Clip                 BBit(7)
#define UI_Flag_Scrollable           BBit(8)
#define UI_Flag_DrawImage            BBit(9)

typedef struct UI_Vec2 { f32 x, y; } UI_Vec2;
typedef struct UI_Padding { f32 left, top, right, bottom; } UI_Padding;
typedef struct UI_Colors { R_Color background, border, text; } UI_Colors;

typedef enum UI_SizeKind {
    UI_SizeKind_Pixels,
    UI_SizeKind_TextContent,
    UI_SizeKind_Percent,
    UI_SizeKind_Flex,
    UI_SizeKind_Fill,
    UI_SizeKind_Count,
} UI_SizeKind;

typedef struct UI_Size { enum8(UI_SizeKind) kind; f32 value; } UI_Size;
typedef enum UI_Layout {
    UI_Layout_Vertical,
    UI_Layout_Horizontal,
    UI_Layout_Count,
} UI_Layout;

typedef struct UI_Properties {
    UI_Size width, height;
    enum8(UI_Layout) childrenLayout;
    UI_Padding childrenPadding;
    UI_Vec2 childrenAlignment, textAlignment;
    UI_Colors colors;
    const R_Font *font;
    String text;
    // Drawn at imageSize, placed inside the padding by textAlignment.
    R_TextureHandle image;
    R_Rect imageUV;
    UI_Vec2 imageSize;
    R_Color imageTint;
    UI_Vec2 floatingOffset;
    f32 cornerRadius;  // background and border
    f32 borderThickness;  // 0 draws one pixel
} UI_Properties;

typedef struct UI_Interaction {
    UI_Key key;
    b32 hot, active, pressed, released, clicked, isDragging;
    u64 pressOrder, clickOrder;
    u32 pressCount;
    MouseButton button;
    KeyMods mods;
} UI_Interaction;

typedef struct UI_Block UI_Block;
struct UI_Block {
    UI_Block *parent, *first, *last, *next;
    UI_Block *hashNext;
    UI_Key key;
    UI_Flags flags;
    u64 lastFrameTouched, drawOrder;
    UI_Properties props;
    R_Rect rect, clipRect;
    UI_Vec2 measuredSize, contentSize, scrollOffset;
    f32 scrollStep;
    UI_Interaction interaction;
};

#define UI_HashSlotCount 1024
typedef struct UI_Context {
    Arena arena;
    PoolNode *blockPool;
    UI_Block *hashSlots[UI_HashSlotCount];
    Arena *frameArena;
    UI_Block *root, *parent;
    UI_Properties next;
    b32 nextFontSet, nextColorsSet, nextScrollSet;
    UI_Vec2 nextScroll;
    const R_Font *defaultFont;
    UI_Colors defaultColors;
    u64 frameIndex, drawOrder, nowMicroseconds;
    UI_Key activeKey, hotKey, lastPressKey;
    MouseButton activeButton, lastPressButton;
    KeyMods activeMods;
    KeyMods mods;  // held by the last mouse or key event
    UI_Vec2 mouse, pressOrigin, lastPressPosition, wheel;
    u64 lastPressTime;
    u32 pressCount;
    // Native double-click limits from the platform; zero selects 500 ms and 4 px.
    u64 doubleClickMicroseconds;
    f32 doubleClickDistance;
    b32 mouseKnown;
    // Keys made without a parent start from this, so that copies of one UI, such as one per panel, keep apart.
    UI_Key keySeed;
} UI_Context;

global UI_Context *UI;

// Pixels are physical.
// TextContent measures this block's text; Fill measures its children and padding.
// Percent uses the parent's inner size.
// Flex shares remaining main-axis space and stretches across it.
// Percent/Flex children of Fill-sized parents use the inherited constraint during measurement.
function UI_Size UI_Pixels(f32 value);
function UI_Size UI_TextContent(void);
function UI_Size UI_Percent(f32 fraction);
function UI_Size UI_Flex(void);
function UI_Size UI_Fill(void);
function UI_Key UI_KeyFromStr(UI_Key parent, String text);

// The caller clears the frame arena before BeginFrame and retains fonts through Draw.
// Input resolves against the previous completed layout, in event order.
function void UI_BeginFrame(UI_Context *context, Arena *frameArena, R_Rect viewport, PlatformEventList events, u64 nowMicroseconds);
function void UI_EndFrame(void);
function void UI_Draw(void);
function void UI_Release(UI_Context *context);
function UI_Block *UI_AddBlockFromKey(UI_Key key, UI_Flags flags);
function UI_Block *UI_AddBlock(UI_Flags flags);
function UI_Interaction UI_InteractionFromBlock(UI_Block *block);
// Ends the current press without a release, for when the OS took the button up.
function void UI_CancelActive(void);
function UI_Block *UI_PushParent(UI_Block *block);
function void UI_PopParent(UI_Block *previous);
#define UI_Parent(block) for (UI_Block *UI__previous = UI_PushParent(block), *UI__once = (block); UI__once; UI_PopParent(UI__previous), UI__once = NULL)

function void UI_SetNextPrefWidth(UI_Size size);
function void UI_SetNextPrefHeight(UI_Size size);
function void UI_SetNextChildrenLayout(UI_Layout layout);
function void UI_SetNextChildrenPadding(UI_Padding padding);
function void UI_SetNextChildrenAlignment(UI_Vec2 alignment);
function void UI_SetNextTextAlignment(UI_Vec2 alignment);
function void UI_SetNextColors(UI_Colors colors);
function void UI_SetNextFont(const R_Font *font);
function void UI_SetNextText(String text);
function void UI_SetNextImage(R_TextureHandle image, R_Rect uv, UI_Vec2 size, R_Color tint);
function void UI_SetNextFloatingOffset(UI_Vec2 offset);
function void UI_SetNextCornerRadius(f32 radius);
function void UI_SetNextBorderThickness(f32 thickness);
function void UI_SetNextScrollOffset(UI_Vec2 offset);
