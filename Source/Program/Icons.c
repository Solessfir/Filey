// File icon cache.
// Rows look icons up by key while building UI; unknown keys are queued and loaded by one background job per frame, then packed into RGBA atlas pages on the main thread.

#define Prog_IconPageSize 512

// Scales `source` to fit a size x size square, centered.
// Downscaling averages the covered source pixels, weighted by alpha so transparent edges do not darken.
function u32 *Prog_ResizeIcon(Arena *arena, P_Image source, i32 size)
{
    u32 *result = ArenaPushArray(arena, (u64)size * (u64)size, u32);
    i32 longest = Max(source.width, source.height);
    if (longest <= 0 || source.pixels == NULL) return result;
    i32 width = Max(1, (i32)((i64)source.width * size / longest));
    i32 height = Max(1, (i32)((i64)source.height * size / longest));
    i32 left = (size - width) / 2, top = (size - height) / 2;
    for (i32 y = 0; y < height; ++y) {
        i32 sy0 = (i32)((i64)y * source.height / height);
        i32 sy1 = Max(sy0 + 1, (i32)((i64)(y + 1) * source.height / height));
        for (i32 x = 0; x < width; ++x) {
            i32 sx0 = (i32)((i64)x * source.width / width);
            i32 sx1 = Max(sx0 + 1, (i32)((i64)(x + 1) * source.width / width));
            u64 r = 0, g = 0, b = 0, a = 0, count = 0;
            for (i32 sy = sy0; sy < sy1; ++sy) {
                for (i32 sx = sx0; sx < sx1; ++sx) {
                    u32 p = source.pixels[(i64)sy * source.width + sx];
                    u64 alpha = p >> 24;
                    r += (p & 0xFF) * alpha;
                    g += ((p >> 8) & 0xFF) * alpha;
                    b += ((p >> 16) & 0xFF) * alpha;
                    a += alpha;
                    ++count;
                }
            }
            u32 pixel = 0;
            if (a) {
                pixel = (u32)(r / a) | (u32)(g / a) << 8 | (u32)(b / a) << 16 | (u32)((a + count / 2) / count) << 24;
            }
            result[(i64)(top + y) * size + left + x] = pixel;
        }
    }
    return result;
}

function void Prog__IconJob(P_Job *job)
{
    Prog_JobInput *input = job->input;
    u32 **pixels = ArenaPushArray(&job->arena, input->iconCount, u32 *);
    job->result = pixels;
    for (u64 i = 0; i < input->iconCount && !P_IsCancelled(&job->cancelToken); ++i) {
        Prog_IconRequest *request = &input->icons[i];
        Scratch scratch = GetScratchEx(&job->arena);
        P_Image image = {0};
        if (P_LoadFileIcon(scratch.arena, request->path, request->kind, input->iconSize, &image)) {
            pixels[i] = Prog_ResizeIcon(&job->arena, image, input->iconSize);
        }
        ReleaseScratch(scratch);
    }
}

// NULL until the icon is loaded; failed and pending icons both return NULL.
function const Prog_Icon *Prog_FileIcon(Prog_State *state, String directory, const Prog_Entry *entry)
{
    return Prog_PathIcon(state, Prog_JoinPath(&state->frameArena, directory, entry->file.name), entry->file.kind);
}

function const Prog_Icon *Prog_PathIcon(Prog_State *state, String path, P_FileKind kind)
{
    if (state->iconSize <= 0) return NULL;
    String key = P_FileIconKey(&state->frameArena, path, kind);
    Prog_Icon *icon = MapsGetPtrNull(state->icons, key);
    if (icon == NULL) {
        key = StrCopy(&state->iconArena, key);
        MapsPut(&state->iconArena, state->icons, key, (Prog_Icon){ .state = Prog_IconState_Pending });
        // Requests outlive the frame when nothing submits them before the next one.
        Prog_IconRequest request = { .path = StrCopy(&state->iconArena, path), .key = key, .kind = (u8)kind };
        ArrayPush(&state->iconArena, state->iconRequests, request);
        return NULL;
    }
    return icon->state == Prog_IconState_Loaded ? icon : NULL;
}

// Drops every cached icon, e.g. when the display scale changes the icon size.
function void Prog__ResetIcons(Prog_State *state, i32 size)
{
    state->icons = (typeof(state->icons)){0};
    state->iconRequests = (Prog_ArrayIconRequest){0};
    state->iconSize = size;
    state->iconCell = 0;
    state->iconPageCount = 0;  // pages are kept in iconPages and reused
    state->spinnerSize = 0;
    ArenaClear(&state->iconArena);
}

function void Prog_SubmitIconRequests(Prog_State *state)
{
    if (!state->iconRequests.header.count) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__IconJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Icons;
    input->iconSize = state->iconSize;
    input->iconCount = state->iconRequests.header.count;
    input->icons = ArenaPushArray(&job->arena, input->iconCount, Prog_IconRequest);
    for (u64 i = 0; i < input->iconCount; ++i) {
        Prog_IconRequest *source = &state->iconRequests.v[i];
        input->icons[i] = (Prog_IconRequest){ StrCopy(&job->arena, source->path), StrCopy(&job->arena, source->key), source->kind };
    }
    job->input = input;
    state->iconRequests = (Prog_ArrayIconRequest){0};
    P_JobSubmit(job);
}

// Copies `pixels` (iconSize square) into the next atlas cell; false when the atlas is full.
function b32 Prog__PlaceIcon(Prog_State *state, Prog_Icon *icon, const u32 *pixels)
{
    i32 size = state->iconSize;
    i32 perRow = Prog_IconPageSize / size;
    u32 page = state->iconCell / (u32)(perRow * perRow);
    u32 cell = state->iconCell % (u32)(perRow * perRow);
    if (page >= CArrayCount(state->iconPages)) return false;
    if (page >= state->iconPageCount) {
        if (!state->iconPages[page].u64[0]) state->iconPages[page] = R_TextureCreateRGBA(Prog_IconPageSize, Prog_IconPageSize, NULL);
        state->iconPageCount = page + 1;
    }
    ++state->iconCell;
    i32 x = (i32)(cell % (u32)perRow) * size, y = (i32)(cell / (u32)perRow) * size;
    R_TextureUpdateRGBA(state->iconPages[page], x, y, size, size, pixels);
    f32 scale = 1.0f / Prog_IconPageSize;
    icon->texture = state->iconPages[page];
    icon->uv = (R_Rect){ (f32)x * scale, (f32)y * scale, (f32)(x + size) * scale, (f32)(y + size) * scale };
    icon->state = Prog_IconState_Loaded;
    return true;
}

// An icon for pixels already in hand, such as a menu item's picture, cached by content so that reopening a menu reuses its cells.
function const Prog_Icon *Prog_ImageIcon(Prog_State *state, P_Image image)
{
    if (state->iconSize <= 0 || !image.pixels) return NULL;
    u64 hash = MapHashBytes(image.pixels, (u64)image.width * (u64)image.height * sizeof(u32));
    String key = StrFormat(&state->frameArena, "/image/%dx%d/%016" PRIx64, image.width, image.height, hash);
    Prog_Icon *icon = MapsGetPtrNull(state->icons, key);
    if (icon == NULL) {
        Scratch scratch = GetScratch();
        Prog_Icon placed = {0};
        u32 *pixels = Prog_ResizeIcon(scratch.arena, image, state->iconSize);
        if (!pixels || !Prog__PlaceIcon(state, &placed, pixels)) placed.state = Prog_IconState_Failed;
        ReleaseScratch(scratch);
        MapsPut(&state->iconArena, state->icons, StrCopy(&state->iconArena, key), placed);
        icon = MapsGetPtrNull(state->icons, key);
    }
    return icon && icon->state == Prog_IconState_Loaded ? icon : NULL;
}

// The busy spinner: eight rounded rays around an empty middle, drawn white so that the draw tints it.
// The renderer cannot rotate, so each frame is the spinner turned a little further.
function void Prog__DrawSpinners(Prog_State *state)
{
    i32 size = state->iconSize;
    Scratch scratch = GetScratch();
    u32 *pixels = ArenaPushArray(scratch.arena, (u64)size * (u64)size, u32);
    f32 radius = (f32)size * 0.5f, inner = radius * 0.45f, outer = radius * 0.82f, half = MaxF32(0.75f, radius * 0.15f);
    f32 pi = 3.14159265f;
    for (u32 frame = 0; frame < Prog_SpinnerFrames; ++frame) {
        f32 turn = (f32)frame / Prog_SpinnerFrames * (pi / 4);
        for (i32 y = 0; y < size; ++y) {
            for (i32 x = 0; x < size; ++x) {
                f32 px = (f32)x + 0.5f - radius, py = (f32)y + 0.5f - radius;
                f32 nearest = 1e9f;
                for (u32 ray = 0; ray < 8; ++ray) {
                    f32 angle = turn + (f32)ray * (pi / 4), dx = cosf(angle), dy = sinf(angle);
                    // Distance to the ray's segment, which runs from `inner` to `outer` along its direction.
                    f32 along = UI__Clamp(px * dx + py * dy, inner, outer);
                    f32 ex = px - dx * along, ey = py - dy * along;
                    nearest = MinF32(nearest, sqrtf(ex * ex + ey * ey));
                }
                f32 coverage = UI__Clamp(half + 0.5f - nearest, 0, 1);
                pixels[y * size + x] = ((u32)(coverage * 255.0f + 0.5f) << 24) | 0xFFFFFFu;
            }
        }
        if (!Prog__PlaceIcon(state, &state->spinner[frame], pixels)) state->spinner[frame].state = Prog_IconState_Failed;
    }
    ReleaseScratch(scratch);
    state->spinnerSize = size;
}

// The spinner frame for this moment: a full turn every 1.2 seconds.
function const Prog_Icon *Prog_SpinnerIcon(Prog_State *state, u64 nowMicroseconds)
{
    if (state->iconSize <= 0) return NULL;
    if (state->spinnerSize != state->iconSize) Prog__DrawSpinners(state);
    u64 period = 1200000 / 8;
    const Prog_Icon *icon = &state->spinner[nowMicroseconds % period * Prog_SpinnerFrames / period];
    return icon->state == Prog_IconState_Loaded ? icon : NULL;
}

function void Prog__TakeIcons(Prog_State *state, P_Job *job)
{
    Prog_JobInput *input = job->input;
    u32 **pixels = job->result;
    // Results for an old icon size are stale; their keys were dropped with the reset.
    if (input->iconSize != state->iconSize || pixels == NULL) return;
    for (u64 i = 0; i < input->iconCount; ++i) {
        Prog_Icon *icon = MapsGetPtrNull(state->icons, input->icons[i].key);
        if (icon == NULL) continue;
        if (pixels[i] == NULL || !Prog__PlaceIcon(state, icon, pixels[i])) icon->state = Prog_IconState_Failed;
    }
}
