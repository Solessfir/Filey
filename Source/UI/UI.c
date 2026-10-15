#include <math.h>

#include "UI.h"

function UI_Size UI_Pixels(f32 value) { return (UI_Size){UI_SizeKind_Pixels, value}; }
function UI_Size UI_TextContent(void) { return (UI_Size){UI_SizeKind_TextContent, 0}; }
function UI_Size UI_Percent(f32 fraction) { return (UI_Size){UI_SizeKind_Percent, fraction}; }
function UI_Size UI_Flex(void) { return (UI_Size){UI_SizeKind_Flex, 0}; }
function UI_Size UI_Fill(void) { return (UI_Size){UI_SizeKind_Fill, 0}; }

function UI_Key UI_KeyFromStr(UI_Key parent, String text)
{
    if (!parent && UI) parent = UI->keySeed;
    u64 result = UINT64_C(14695981039346656037) ^ parent;
    for (u64 i = 0; i < text.length; ++i) {
        result ^= (u8)text.value[i];
        result *= UINT64_C(1099511628211);
    }
    return result ? result : 1;
}

function f32 UI__Clamp(f32 value, f32 min, f32 max)
{
    return MinF32(MaxF32(value, min), max);
}

function R_Rect UI__Intersect(R_Rect a, R_Rect b)
{
    R_Rect result = {MaxF32(a.x0, b.x0), MaxF32(a.y0, b.y0), MinF32(a.x1, b.x1), MinF32(a.y1, b.y1)};
    result.x1 = MaxF32(result.x0, result.x1);
    result.y1 = MaxF32(result.y0, result.y1);
    return result;
}

function b32 UI__Contains(R_Rect rect, UI_Vec2 point)
{
    return point.x >= rect.x0 && point.y >= rect.y0 && point.x < rect.x1 && point.y < rect.y1;
}

function f32 UI__DistanceSquared(UI_Vec2 a, UI_Vec2 b)
{
    f32 x = a.x - b.x, y = a.y - b.y;
    return x * x + y * y;
}

// Everything eases at this rate, leaving 56% of the distance after each 60 Hz frame.
#define UI_AnimationRate 34.0f

function f32 UI_Approach(f32 value, f32 target, f32 epsilon)
{
    if (!UI->animate || fabsf(target - value) <= epsilon) return target;
    UI->animating = true;
    return value + (target - value) * (1.0f - expf(-UI_AnimationRate * UI->dt));
}

function UI_Animation *UI__Animation(UI_Key key, f32 initial)
{
    for (u32 i = 0; i < UI->animationCount; ++i) {
        if (UI->animations[i].key == key) {
            UI->animations[i].lastFrameTouched = UI->frameIndex;
            return &UI->animations[i];
        }
    }
    if (UI->animationCount == UI_MaxAnimations) return NULL;
    UI_Animation *result = &UI->animations[UI->animationCount++];
    *result = (UI_Animation){ .key = key, .value = initial, .startMicroseconds = UI->nowMicroseconds, .lastFrameTouched = UI->frameIndex };
    return result;
}

function f32 UI_Animate(UI_Key key, f32 target, f32 initial)
{
    UI_Animation *animation = UI__Animation(key, initial);
    if (!animation) return target;
    animation->value = UI_Approach(animation->value, target, 0.001f);
    return animation->value;
}

function f32 UI_Slide(UI_Key key, f32 target, f32 cap)
{
    UI_Animation *animation = UI__Animation(key, target);
    if (!animation) return target;
    animation->value = UI_Approach(UI__Clamp(animation->value, target - cap, target + cap), target, cap * (1.0f / 64.0f));
    return animation->value;
}

function f32 UI_Fall(UI_Key key, f32 target)
{
    UI_Animation *animation = UI__Animation(key, target);
    if (!animation) return target;
    animation->value = target >= animation->value ? target : UI_Approach(animation->value, target, 0.001f);
    return animation->value;
}

function f32 UI_Kept(UI_Key key)
{
    for (u32 i = 0; i < UI->animationCount; ++i) {
        if (UI->animations[i].key == key) return UI->animations[i].value;
    }
    return 0;
}

function u64 UI_TimeSince(UI_Key key)
{
    UI_Animation *animation = UI__Animation(key, 0);
    return animation ? UI->nowMicroseconds - animation->startMicroseconds : 0;
}

function void UI_KeepAnimating(void) { if (UI->animate) UI->animating = true; }

function UI_Block *UI__Find(UI_Key key)
{
    if (!key) return NULL;
    for (UI_Block *block = UI->hashSlots[key % UI_HashSlotCount]; block; block = block->hashNext) {
        if (block->key == key) return block;
    }
    return NULL;
}

function UI_Block *UI__Hit(UI_Flags flags)
{
    UI_Block *result = NULL;
    if (UI->mouseKnown) {
        for (u32 slot = 0; slot < UI_HashSlotCount; ++slot) {
            for (UI_Block *block = UI->hashSlots[slot]; block; block = block->hashNext) {
                if (block->lastFrameTouched == UI->frameIndex - 1 && FlagIntersects(block->flags, flags) &&
                    UI__Contains(block->clipRect, UI->mouse) && (!result || block->drawOrder > result->drawOrder)) {
                    result = block;
                }
            }
        }
    }
    return result;
}

function void UI__ResetNext(void)
{
    UI->next = (UI_Properties){.width = UI_TextContent(), .height = UI_TextContent()};
    UI->nextFontSet = UI->nextColorsSet = UI->nextScrollSet = false;
}

function void UI_CancelActive(void)
{
    UI_Block *block = UI__Find(UI->activeKey);
    if (block) {
        block->interaction.active = false;
        block->interaction.isDragging = false;
    }
    UI->activeKey = 0;
}

global const UI_Flags UI__ButtonFlags[MouseButton_Count] = {
    UI_Flag_LeftMouseClickable, UI_Flag_RightMouseClickable, UI_Flag_MiddleMouseClickable,
};

function void UI__Input(PlatformEventList events)
{
    u64 eventOrder = 0;
    for (PlatformEvent *event = events.first; event; event = event->next) {
        ++eventOrder;
        if (event->kind == PlatformEventKind_FocusChanged && !event->focused) {
            UI_CancelActive();
            UI->mouseKnown = false;
            UI->lastPressKey = 0;
            continue;
        }
        // A pointer that left the window is over nothing, unless it is dragging, which keeps following it.
        if (event->kind == PlatformEventKind_MouseLeave && !UI->activeKey) {
            UI->mouseKnown = false;
            UI->mouse = (UI_Vec2){ -1e9f, -1e9f };
            continue;
        }
        if (event->kind == PlatformEventKind_MouseMove || event->kind == PlatformEventKind_MouseDown ||
            event->kind == PlatformEventKind_MouseUp || event->kind == PlatformEventKind_MouseWheel) {
            UI->mouse = (UI_Vec2){event->x, event->y};
            UI->mouseKnown = true;
            UI->mods = event->mods;
        }
        if (event->kind == PlatformEventKind_KeyDown || event->kind == PlatformEventKind_KeyUp) UI->mods = event->mods;
        if (event->kind == PlatformEventKind_MouseDown && event->button < MouseButton_Count && !UI->activeKey) {
            UI_Block *block = UI__Hit(UI__ButtonFlags[event->button]);
            if (block) {
                // Synthetic events have no timestamp and fall back to the frame time.
                u64 time = event->timeMicroseconds ? event->timeMicroseconds : UI->nowMicroseconds;
                u64 interval = UI->doubleClickMicroseconds ? UI->doubleClickMicroseconds : UINT64_C(500000);
                f32 distance = UI->doubleClickDistance > 0 ? UI->doubleClickDistance : 4.0f;
                b32 repeated = UI->lastPressKey == block->key && UI->lastPressButton == event->button &&
                    time >= UI->lastPressTime && time - UI->lastPressTime <= interval &&
                    UI__DistanceSquared(UI->mouse, UI->lastPressPosition) <= distance * distance;
                UI->pressCount = repeated && UI->pressCount < UINT32_MAX ? UI->pressCount + 1 : 1;
                UI->lastPressKey = UI->activeKey = block->key;
                UI->lastPressButton = UI->activeButton = event->button;
                UI->activeMods = event->mods;
                UI->lastPressTime = time;
                UI->lastPressPosition = UI->pressOrigin = UI->mouse;
                block->interaction.pressed = true;
                block->interaction.pressOrder = eventOrder;
                block->interaction.pressCount = UI->pressCount;
                block->interaction.button = event->button;
                block->interaction.mods = event->mods;
            }
        } else if (event->kind == PlatformEventKind_MouseUp && event->button == UI->activeButton && UI->activeKey) {
            UI_Block *block = UI__Find(UI->activeKey);
            if (block) {
                block->interaction.released = true;
                if (UI__Contains(block->clipRect, UI->mouse)) {
                    block->interaction.clicked = true;
                    block->interaction.clickOrder = eventOrder;
                }
                block->interaction.pressCount = UI->pressCount;
                block->interaction.button = UI->activeButton;
                block->interaction.mods = UI->activeMods;
            }
            UI_CancelActive();
        } else if (event->kind == PlatformEventKind_MouseWheel) {
            UI->wheel.x += event->wheelX;
            UI->wheel.y += event->wheelY;
            // Ctrl+wheel is left to the program, which zooms with it.
            UI_Block *block = (event->mods & KeyMod_Control) ? NULL : UI__Hit(UI_Flag_Scrollable | UI_Flag_TakesWheel);
            if (block && !FlagExists(block->flags, UI_Flag_TakesWheel)) {
                f32 turn = UI->reverseScroll ? -1.0f : 1.0f;
                block->scrollOffset.x += event->wheelX * block->scrollStep * turn;
                block->scrollOffset.y -= event->wheelY * block->scrollStep * turn;
            }
        }
    }
    UI_Block *hot = UI__Hit(UI_Flag_MouseClickable);
    UI->hotKey = hot ? hot->key : 0;
}

function void UI_BeginFrame(UI_Context *context, Arena *frameArena, R_Rect viewport, PlatformEventList events, u64 nowMicroseconds)
{
    Assert(context && frameArena);
    UI = context;
    UI->frameArena = frameArena;
    // After an idle wait, the first step is one frame's worth rather than the whole wait.
    UI->dt = UI->wasAnimating && nowMicroseconds > UI->nowMicroseconds ? MinF32((f32)(nowMicroseconds - UI->nowMicroseconds) / 1e6f, 0.05f) : 1.0f / 60.0f;
    UI->animating = false;
    UI->nowMicroseconds = nowMicroseconds;
    ++UI->frameIndex;
    UI->wheel = (UI_Vec2){0};
    for (u32 slot = 0; slot < UI_HashSlotCount; ++slot) {
        for (UI_Block *block = UI->hashSlots[slot]; block; block = block->hashNext) {
            block->interaction = (UI_Interaction){.key = block->key};
        }
    }
    UI__Input(events);
    UI__ResetNext();
    UI->root = ArenaPushStruct(frameArena, UI_Block);
    UI->root->lastFrameTouched = UI->frameIndex;
    UI->root->rect = viewport;
    UI->root->props.width = UI_Pixels(MaxF32(0, viewport.x1 - viewport.x0));
    UI->root->props.height = UI_Pixels(MaxF32(0, viewport.y1 - viewport.y0));
    UI->root->props.font = UI->defaultFont;
    UI->root->props.colors = UI->defaultColors;
    UI->parent = UI->root;
}

function UI_Block *UI_AddBlockFromKey(UI_Key key, UI_Flags flags)
{
    Assert(UI && UI->parent);
    Assert(key || !FlagIntersects(flags, UI_Flag_MouseClickable | UI_Flag_Scrollable));
    UI_Block *block = UI__Find(key);
    if (!block) {
        block = key ? PoolAlloc(&UI->arena, UI->blockPool, UI_Block) : ArenaPushStruct(UI->frameArena, UI_Block);
        block->key = key;
        if (key) {
            u64 slot = key % UI_HashSlotCount;
            StackPushN(UI->hashSlots[slot], block, hashNext);
        }
    }
    Assert(!key || block->lastFrameTouched != UI->frameIndex);
    block->lastFrameTouched = UI->frameIndex;
    if (UI->noInput) flags &= ~UI_Flag_MouseClickable;
    block->flags = flags;
    block->parent = UI->parent;
    block->first = block->last = block->next = NULL;
    block->props = UI->next;
    if (!UI->nextFontSet) block->props.font = UI->defaultFont;
    if (!UI->nextColorsSet) block->props.colors = UI->defaultColors;
    block->props.text = StrCopy(UI->frameArena, block->props.text);
    if (UI->nextScrollSet) block->scrollOffset = block->scrollShown = UI->nextScroll;
    QueuePush(UI->parent->first, UI->parent->last, block);
    UI__ResetNext();
    if (UI->activeKey == key && key && !FlagExists(flags, UI__ButtonFlags[UI->activeButton])) UI_CancelActive();
    return block;
}

function UI_Block *UI_AddBlock(UI_Flags flags) { return UI_AddBlockFromKey(0, flags); }

function UI_Interaction UI_InteractionFromBlock(UI_Block *block)
{
    UI_Interaction result = block->interaction;
    result.key = block->key;
    result.active = block->key && block->key == UI->activeKey;
    result.hot = block->key && block->key == UI->hotKey && (!UI->activeKey || result.active);
    if (result.active) {
        result.pressCount = UI->pressCount;
        result.button = UI->activeButton;
        result.mods = UI->activeMods;
        result.isDragging = UI__DistanceSquared(UI->mouse, UI->pressOrigin) > 16.0f;
    }
    return result;
}

function UI_Block *UI_PushParent(UI_Block *block)
{
    Assert(UI && block && block->lastFrameTouched == UI->frameIndex);
    UI_Block *result = UI->parent;
    UI->parent = block;
    return result;
}

function void UI_PopParent(UI_Block *previous) { UI->parent = previous; }

function void UI_SetNextPrefWidth(UI_Size size) { UI->next.width = size; }
function void UI_SetNextPrefHeight(UI_Size size) { UI->next.height = size; }
function void UI_SetNextChildrenLayout(UI_Layout layout) { UI->next.childrenLayout = (u8)layout; }
function void UI_SetNextChildrenPadding(UI_Padding padding) { UI->next.childrenPadding = padding; }
function void UI_SetNextChildrenAlignment(UI_Vec2 alignment) { UI->next.childrenAlignment = alignment; }
function void UI_SetNextTextAlignment(UI_Vec2 alignment) { UI->next.textAlignment = alignment; }
function void UI_SetNextColors(UI_Colors colors) { UI->next.colors = colors; UI->nextColorsSet = true; }
function void UI_SetNextFont(const R_Font *font) { UI->next.font = font; UI->nextFontSet = true; }
function void UI_SetNextText(String text) { UI->next.text = text; }
function void UI_SetNextFloatingOffset(UI_Vec2 offset) { UI->next.floatingOffset = offset; }
function void UI_SetNextCornerRadius(f32 radius) { UI->next.cornerRadius = radius; }
function void UI_SetNextBorderThickness(f32 thickness) { UI->next.borderThickness = thickness; }
function void UI_SetNextImage(R_TextureHandle image, R_Rect uv, UI_Vec2 size, R_Color tint)
{
    UI->next.image = image;
    UI->next.imageUV = uv;
    UI->next.imageSize = size;
    UI->next.imageTint = tint;
}
function void UI_SetNextScrollOffset(UI_Vec2 offset) { UI->nextScroll = offset; UI->nextScrollSet = true; }
function void UI_SetNextOpacity(f32 opacity) { UI->next.fade = 1.0f - UI__Clamp(opacity, 0, 1); }
function void UI_SetNextBlur(f32 radius) { UI->next.blur = radius; }
function void UI_SetNextSoftness(f32 softness) { UI->next.softness = softness; }
function void UI_SetNextScale(f32 scale) { UI->next.scale = scale; }

function UI_Vec2 UI__TextSize(UI_Block *block)
{
    const R_Font *font = block->props.font;
    return (UI_Vec2){font ? R_TextWidth(font, block->props.text) : 0, font && block->props.text.length ? font->lineHeight : 0};
}

function f32 UI__Size(UI_Size size, f32 text, f32 content, f32 constraint)
{
    f32 result = 0;
    switch (size.kind) {
        case UI_SizeKind_Pixels: result = size.value; break;
        case UI_SizeKind_TextContent: result = text; break;
        case UI_SizeKind_Percent: result = constraint * size.value; break;
        case UI_SizeKind_Flex: result = constraint; break;
        case UI_SizeKind_Fill: result = content; break;
        default: InvalidPath; break;
    }
    return MaxF32(0, result);
}

function void UI__Measure(UI_Block *block, UI_Vec2 constraint)
{
    UI_Padding pad = block->props.childrenPadding;
    UI_Vec2 text = UI__TextSize(block);
    f32 padX = pad.left + pad.right, padY = pad.top + pad.bottom;
    UI_Vec2 available = {
        UI__Size(block->props.width, text.x + padX, constraint.x, constraint.x),
        UI__Size(block->props.height, text.y + padY, constraint.y, constraint.y),
    };
    available.x = MaxF32(0, available.x - padX);
    available.y = MaxF32(0, available.y - padY);
    UI_Vec2 content = {0};
    for (UI_Block *child = block->first; child; child = child->next) {
        UI__Measure(child, available);
        if (FlagExists(child->flags, UI_Flag_Floating)) continue;
        if (block->props.childrenLayout == UI_Layout_Horizontal) {
            content.x += child->measuredSize.x;
            content.y = MaxF32(content.y, child->measuredSize.y);
        } else {
            content.x = MaxF32(content.x, child->measuredSize.x);
            content.y += child->measuredSize.y;
        }
    }
    block->measuredSize = (UI_Vec2){
        UI__Size(block->props.width, text.x + padX, content.x + padX, constraint.x),
        UI__Size(block->props.height, text.y + padY, content.y + padY, constraint.y),
    };
}

function UI_Vec2 UI__ChildSize(UI_Block *child, UI_Vec2 inner)
{
    UI_Vec2 result = child->measuredSize;
    if (child->props.width.kind == UI_SizeKind_Percent) result.x = MaxF32(0, inner.x * child->props.width.value);
    if (child->props.height.kind == UI_SizeKind_Percent) result.y = MaxF32(0, inner.y * child->props.height.value);
    if (child->props.width.kind == UI_SizeKind_Flex) result.x = inner.x;
    if (child->props.height.kind == UI_SizeKind_Flex) result.y = inner.y;
    return result;
}

function void UI__Arrange(UI_Block *block, R_Rect rect, R_Rect clip)
{
    block->rect = rect;
    block->clipRect = UI__Intersect(rect, clip);
    block->drawOrder = ++UI->drawOrder;
    UI_Padding pad = block->props.childrenPadding;
    UI_Vec2 origin = {rect.x0 + pad.left, rect.y0 + pad.top};
    UI_Vec2 inner = {MaxF32(0, rect.x1 - rect.x0 - pad.left - pad.right), MaxF32(0, rect.y1 - rect.y0 - pad.top - pad.bottom)};
    block->scrollStep = block->props.font && block->props.font->lineHeight > 0 ? 3.0f * block->props.font->lineHeight : 48.0f;
    b32 horizontal = block->props.childrenLayout == UI_Layout_Horizontal;
    f32 mainSize = horizontal ? inner.x : inner.y;
    f32 occupied = 0;
    u32 flexCount = 0;
    // shortcut: resolved parents remeasure child subtrees; cache constraints if deep-tree profiling shows a cost.
    for (UI_Block *child = block->first; child; child = child->next) {
        UI__Measure(child, inner);
        if (FlagExists(child->flags, UI_Flag_Floating)) continue;
        UI_Size size = horizontal ? child->props.width : child->props.height;
        UI_Vec2 childSize = UI__ChildSize(child, inner);
        if (size.kind == UI_SizeKind_Flex) ++flexCount;
        else occupied += horizontal ? childSize.x : childSize.y;
    }
    f32 flexSize = flexCount ? MaxF32(0, mainSize - occupied) / (f32)flexCount : 0;
    UI_Vec2 content = {0};
    for (UI_Block *child = block->first; child; child = child->next) {
        if (FlagExists(child->flags, UI_Flag_Floating)) continue;
        UI_Vec2 childSize = UI__ChildSize(child, inner);
        if (horizontal) {
            content.x += child->props.width.kind == UI_SizeKind_Flex ? flexSize : childSize.x;
            content.y = MaxF32(content.y, childSize.y);
        } else {
            content.x = MaxF32(content.x, childSize.x);
            content.y += child->props.height.kind == UI_SizeKind_Flex ? flexSize : childSize.y;
        }
    }
    block->contentSize = content;
    // Both are clamped before easing, so that an offset past the end, as the breadcrumbs ask for every frame, settles instead of animating forever.
    f32 maxX = MaxF32(0, content.x - inner.x), maxY = MaxF32(0, content.y - inner.y);
    block->scrollOffset.x = UI__Clamp(block->scrollOffset.x, 0, maxX);
    block->scrollOffset.y = UI__Clamp(block->scrollOffset.y, 0, maxY);
    block->scrollShown.x = UI_Approach(UI__Clamp(block->scrollShown.x, 0, maxX), block->scrollOffset.x, 0.5f);
    block->scrollShown.y = UI_Approach(UI__Clamp(block->scrollShown.y, 0, maxY), block->scrollOffset.y, 0.5f);
    f32 alignment = horizontal ? block->props.childrenAlignment.x : block->props.childrenAlignment.y;
    f32 cursor = MaxF32(0, mainSize - (horizontal ? content.x : content.y)) * UI__Clamp(alignment, 0, 1);
    if (FlagIntersects(block->flags, UI_Flag_Clip | UI_Flag_Scrollable)) clip = block->clipRect;
    for (UI_Block *child = block->first; child; child = child->next) {
        UI_Vec2 size = UI__ChildSize(child, inner);
        UI_Vec2 position = origin;
        if (FlagExists(child->flags, UI_Flag_Floating)) {
            position.x += child->props.floatingOffset.x;
            position.y += child->props.floatingOffset.y;
        } else {
            if (horizontal) {
                if (child->props.width.kind == UI_SizeKind_Flex) size.x = flexSize;
                position.x += cursor;
                position.y += MaxF32(0, inner.y - size.y) * UI__Clamp(block->props.childrenAlignment.y, 0, 1);
                cursor += size.x;
            } else {
                if (child->props.height.kind == UI_SizeKind_Flex) size.y = flexSize;
                position.x += MaxF32(0, inner.x - size.x) * UI__Clamp(block->props.childrenAlignment.x, 0, 1);
                position.y += cursor;
                cursor += size.y;
            }
            position.x -= block->scrollShown.x;
            position.y -= block->scrollShown.y;
        }
        UI__Arrange(child, (R_Rect){position.x, position.y, position.x + size.x, position.y + size.y}, clip);
    }
}

function void UI_EndFrame(void)
{
    Assert(UI && UI->parent == UI->root);
    UI_Vec2 size = {UI->root->props.width.value, UI->root->props.height.value};
    UI->root->measuredSize = size;
    UI->drawOrder = 0;
    UI__Arrange(UI->root, UI->root->rect, UI->root->rect);
    for (u32 slot = 0; slot < UI_HashSlotCount; ++slot) {
        UI_Block **link = &UI->hashSlots[slot];
        while (*link) {
            UI_Block *block = *link;
            if (block->lastFrameTouched != UI->frameIndex) {
                if (block->key == UI->activeKey) UI_CancelActive();
                if (block->key == UI->hotKey) UI->hotKey = 0;
                if (block->key == UI->lastPressKey) UI->lastPressKey = 0;
                *link = block->hashNext;
                PoolFree(UI->blockPool, block);
            } else {
                link = &block->hashNext;
            }
        }
    }
    for (u32 i = 0; i < UI->animationCount;) {
        if (UI->animations[i].lastFrameTouched != UI->frameIndex) UI->animations[i] = UI->animations[--UI->animationCount];
        else ++i;
    }
    UI->wasAnimating = UI->animating;
}

// The active renderer clip is always the nearest clipping ancestor's clipRect, so a block's own rect needs no clip of its own.
// Clips are pushed only where they cut something, because every clip change splits the renderer's batch.
function R_Color UI__Faded(R_Color color, f32 opacity) { color.a *= opacity; return color; }

function void UI__DrawBlockScaled(UI_Block *block, f32 opacity);

function void UI__DrawBlock(UI_Block *block, f32 opacity)
{
    f32 scale = block->props.scale;
    if (scale <= 0 || scale == 1) {
        UI__DrawBlockScaled(block, opacity);
        return;
    }
    R_PushScale((block->rect.x0 + block->rect.x1) * 0.5f, (block->rect.y0 + block->rect.y1) * 0.5f, scale);
    UI__DrawBlockScaled(block, opacity);
    R_PopScale();
}

function void UI__DrawBlockScaled(UI_Block *block, f32 opacity)
{
    if (UI->squareCorners) block->props.cornerRadius = 0;
    f32 own = 1.0f - UI__Clamp(block->props.fade, 0, 1);
    opacity *= own;
    if (opacity <= 0) return;
    b32 visible = block->clipRect.x1 > block->clipRect.x0 && block->clipRect.y1 > block->clipRect.y0;
    b32 clipsChildren = FlagIntersects(block->flags, UI_Flag_Clip | UI_Flag_Scrollable);
    // A fading block that keeps what is inside it within its bounds fades as one layer, so that its parts do not show through one another.
    b32 layer = own < 1 && clipsChildren && block->first && visible && !UI->drawingLayer;
    if (visible && block->props.blur > 0) R_BlurBehind(block->rect, block->props.cornerRadius, block->props.blur, opacity);
    if (layer) {
        R_PushLayer(block->clipRect);
        UI->drawingLayer = true;
        opacity /= own;
    }
    if (visible) {
        if (FlagExists(block->flags, UI_Flag_DrawBackground)) {
            R_Color background = UI__Faded(block->props.colors.background, opacity);
            if (block->props.softness > 0) R_DrawSoftRect(block->rect, background, block->props.cornerRadius, block->props.softness);
            else R_DrawRect(block->rect, background, block->props.cornerRadius);
        }
        if (FlagExists(block->flags, UI_Flag_DrawBorder)) R_DrawRectOutline(block->rect, UI__Faded(block->props.colors.border, opacity), block->props.cornerRadius, MaxF32(1, block->props.borderThickness));
        if (FlagExists(block->flags, UI_Flag_DrawImage)) {
            UI_Padding pad = block->props.childrenPadding;
            UI_Vec2 size = block->props.imageSize;
            f32 innerWidth = block->rect.x1 - block->rect.x0 - pad.left - pad.right;
            f32 innerHeight = block->rect.y1 - block->rect.y0 - pad.top - pad.bottom;
            // Whole pixels, since images are uploaded at their drawn size.
            f32 x = floorf(block->rect.x0 + pad.left + (innerWidth - size.x) * UI__Clamp(block->props.textAlignment.x, 0, 1) + 0.5f);
            f32 y = floorf(block->rect.y0 + pad.top + (innerHeight - size.y) * UI__Clamp(block->props.textAlignment.y, 0, 1) + 0.5f);
            R_DrawImage((R_Rect){ x, y, x + size.x, y + size.y }, block->props.imageUV, block->props.image, UI__Faded(block->props.imageTint, opacity));
        }
        if (FlagExists(block->flags, UI_Flag_DrawText) && block->props.font) {
            UI_Padding pad = block->props.childrenPadding;
            UI_Vec2 text = UI__TextSize(block);
            f32 innerWidth = block->rect.x1 - block->rect.x0 - pad.left - pad.right;
            f32 innerHeight = block->rect.y1 - block->rect.y0 - pad.top - pad.bottom;
            f32 x = block->rect.x0 + pad.left + MaxF32(0, innerWidth - text.x) * UI__Clamp(block->props.textAlignment.x, 0, 1);
            f32 y = block->rect.y0 + pad.top + MaxF32(0, innerHeight - text.y) * UI__Clamp(block->props.textAlignment.y, 0, 1);
            b32 clipText = clipsChildren && (text.x > innerWidth || text.y > innerHeight);
            if (clipText) R_PushClip(block->clipRect);
            R_DrawText(block->props.font, x, y, UI__Faded(block->props.colors.text, opacity), block->props.text);
            if (clipText) R_PopClip();
        }
    }
    if (clipsChildren && !visible) return;
    if (clipsChildren && block->first) R_PushClip(block->clipRect);
    for (UI_Block *child = block->first; child; child = child->next) UI__DrawBlock(child, opacity);
    if (clipsChildren && block->first) R_PopClip();
    if (layer) {
        UI->drawingLayer = false;
        R_PopLayer(own);
    }
}

function void UI_Draw(void) { Assert(UI && UI->root); UI__DrawBlock(UI->root, 1); }

function void UI_Release(UI_Context *context)
{
    if (UI == context) UI = NULL;
    ArenaRelease(&context->arena);
    ZeroStruct(context);
}
