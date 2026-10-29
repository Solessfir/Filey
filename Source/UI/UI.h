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
#define UI_Flag_TakesWheel           BBit(10)  // the wheel over it is the program's, and scrolls nothing under it
#define UI_Flag_KeepCorners          BBit(11)  // round even when every other corner is drawn square, as the window's outline is
#define UI_Flag_DrawLine             BBit(12)  // a line between lineFrom and lineTo, in its background color
#define UI_Flag_Detached             BBit(13)  // left out of the window's drawing, for UI_DrawDetached to draw on its own
#define UI_Flag_ImageSubpixel        BBit(14)  // its image goes where it falls rather than on whole pixels, as one that grows and shrinks smoothly

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

// A rectangle drawn over a block's background, from the block's top left corner, shaded from its corners' colors: top left, top right, bottom left and bottom right.
typedef struct UI_Quad {
    R_Rect rect;
    R_Color corners[4];
} UI_Quad;

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
    // From the block's top left corner.
    UI_Vec2 lineFrom, lineTo;
    f32 lineThickness;
    UI_Vec2 floatingOffset;
    f32 cornerRadius;  // background and border
    f32 borderThickness;  // 0 draws one pixel
    f32 fade;  // 0 is opaque and 1 invisible, for this block and everything inside it
    f32 scale; // 0 draws it at its size; otherwise it and everything inside it are drawn that much larger around its center
    f32 blur;  // 0 draws over what is under it; otherwise it first blurs that, this many pixels across, inside its corners
    f32 softness;  // 0 gives the background a sharp edge; otherwise it fades in from its edge over this many pixels
    f32 edgeFade;  // a scrolling block's content fades into its background over this many pixels at an edge it has more past
    u32 shape;     // R_Shape flags for its blur, background and border
    // Drawn over its background and border, inside it, in the order given; they live in the frame's memory.
    const UI_Quad *quads;
    u32 quadCount;
    // 1 has the blurs drawn from it on read the frame as it was before it, until a block with 2, for boxes side by side to blur as one.
    u32 blurSource;
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
    // scrollOffset is where scrolling is headed; scrollShown follows it and places the children.
    UI_Vec2 measuredSize, contentSize, scrollOffset, scrollShown;
    f32 scrollStep;
    UI_Interaction interaction;
};

// A value that eases toward a target, kept while its key is used every frame.
typedef struct UI_Animation { UI_Key key; f32 value; u64 startMicroseconds, lastFrameTouched; } UI_Animation;
#define UI_MaxAnimations 128

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
    b32 drawingLayer;  // a fading block is being drawn into a layer, inside which its parts draw unfaded
    b32 squareCorners; // every corner is drawn square
    b32 reverseScroll; // the wheel scrolls the other way
    u64 lastPressTime;
    u32 pressCount;
    // Native double-click limits from the platform; zero selects 500 ms and 4 px.
    u64 doubleClickMicroseconds;
    f32 doubleClickDistance;
    b32 mouseKnown;
    // Keys made without a parent start from this, so that copies of one UI, such as one per panel, keep apart.
    UI_Key keySeed;
    // Off by default, so that a layout depends only on its input; animating says the last frame left something moving.
    b32 animate, animating, wasAnimating;
    // When something still to change without input next needs a frame, rather than every frame meanwhile; 0 for none.
    u64 wakeAt;
    f32 dt;  // seconds the animations advance this frame
    u32 noInput;  // inside UI_NoInput, blocks take no presses
    UI_Animation animations[UI_MaxAnimations];
    u32 animationCount;
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
// Draws a UI_Flag_Detached block and what is in it, where it was laid out, as into an image of its own.
function void UI_DrawDetached(UI_Block *block);
function void UI_Release(UI_Context *context);
function UI_Block *UI_AddBlockFromKey(UI_Key key, UI_Flags flags);
function UI_Block *UI_AddBlock(UI_Flags flags);
function UI_Interaction UI_InteractionFromBlock(UI_Block *block);
// Ends the current press without a release, for when the OS took the button up.
function void UI_CancelActive(void);
function UI_Block *UI_PushParent(UI_Block *block);
function void UI_PopParent(UI_Block *previous);
// Blocks added inside take no presses, for something still drawn as it fades out after it closed.
#define UI_NoInput() for (u32 UI__once = (++UI->noInput, 1u); UI__once; --UI->noInput, UI__once = 0)
// The block is named once, as a call that makes it would otherwise make a second one inside the first.
#define UI_Parent(block) for (UI_Block *UI__once = (block), *UI__previous = UI_PushParent(UI__once); UI__once; UI_PopParent(UI__previous), UI__once = NULL)

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
function void UI_SetNextLine(UI_Vec2 from, UI_Vec2 to, f32 thickness);
function void UI_SetNextFloatingOffset(UI_Vec2 offset);
function void UI_SetNextCornerRadius(f32 radius);
function void UI_SetNextBorderThickness(f32 thickness);
function void UI_SetNextScrollOffset(UI_Vec2 offset);
function void UI_SetNextOpacity(f32 opacity);
function void UI_SetNextBlur(f32 radius);
function void UI_SetNextShape(u32 shape);
function void UI_SetNextBlurSource(u32 source);
function void UI_SetNextSoftness(f32 softness);
function void UI_SetNextEdgeFade(f32 pixels);
function void UI_SetNextQuads(const UI_Quad *quads, u32 count);
function void UI_SetNextScale(f32 scale);

// Eases `value` toward `target`, snapping within `epsilon`; anything still moving asks for another frame.
function f32 UI_Approach(f32 value, f32 target, f32 epsilon);
// Keeps a scrollable block's eased scroll within two of its views of where it goes, so that a long jump eases in over its last stretch only.
// A list calls it before it works out which rows to build from the two, which then never takes in the rows on the way.
function void UI_LimitScrollEase(UI_Block *block);
// The value kept under `key`, starting at `initial` the first frame it is asked for, eased toward `target`.
function f32 UI_Animate(UI_Key key, f32 target, f32 initial);
// A position or size kept under `key`: a change starts at most `cap` from `target` and eases the rest.
function f32 UI_Slide(UI_Key key, f32 target, f32 cap);
// The value kept under `key`, which rises to `target` at once and eases only on the way down, as hover washes fade.
function f32 UI_Fall(UI_Key key, f32 target);
// The value last kept under `key`, or 0 without one, without keeping it for another frame.
function f32 UI_Kept(UI_Key key);
// Microseconds since `key` was first asked for in an unbroken run of frames.
function u64 UI_TimeSince(UI_Key key);
// Asks for another frame, for animations that are not eased values.
function void UI_KeepAnimating(void);
function void UI_WakeAt(u64 microseconds);
