// File icon cache. Rows look icons up by key while building UI; unknown keys
// are queued and loaded by one background job per frame, then packed into
// RGBA atlas pages on the main thread.

#define Prog_IconPageSize 512

// Scales `source` to fit a size x size square, centered. Downscaling averages
// the covered source pixels, weighted by alpha so transparent edges do not darken.
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
    if (state->iconSize <= 0) return NULL;
    String path = Prog_JoinPath(&state->frameArena, directory, entry->file.name);
    String key = P_FileIconKey(&state->frameArena, path, entry->file.kind);
    Prog_Icon *icon = MapsGetPtrNull(state->icons, key);
    if (icon == NULL) {
        key = StrCopy(&state->iconArena, key);
        MapsPut(&state->iconArena, state->icons, key, (Prog_Icon){ .state = Prog_IconState_Pending });
        // Requests outlive the frame when nothing submits them before the next one.
        Prog_IconRequest request = { .path = StrCopy(&state->iconArena, path), .key = key, .kind = entry->file.kind };
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

function void Prog__TakeIcons(Prog_State *state, P_Job *job)
{
    Prog_JobInput *input = job->input;
    u32 **pixels = job->result;
    // Results for an old icon size are stale; their keys were dropped with the reset.
    if (input->iconSize != state->iconSize || pixels == NULL) return;
    i32 size = state->iconSize;
    i32 perRow = Prog_IconPageSize / size;
    for (u64 i = 0; i < input->iconCount; ++i) {
        Prog_Icon *icon = MapsGetPtrNull(state->icons, input->icons[i].key);
        if (icon == NULL) continue;
        if (pixels[i] == NULL) {
            icon->state = Prog_IconState_Failed;
            continue;
        }
        u32 page = state->iconCell / (u32)(perRow * perRow);
        u32 cell = state->iconCell % (u32)(perRow * perRow);
        if (page >= CArrayCount(state->iconPages)) {
            icon->state = Prog_IconState_Failed;
            continue;
        }
        if (page >= state->iconPageCount) {
            if (!state->iconPages[page].u64[0]) state->iconPages[page] = R_TextureCreateRGBA(Prog_IconPageSize, Prog_IconPageSize, NULL);
            state->iconPageCount = page + 1;
        }
        ++state->iconCell;
        i32 x = (i32)(cell % (u32)perRow) * size, y = (i32)(cell / (u32)perRow) * size;
        R_TextureUpdateRGBA(state->iconPages[page], x, y, size, size, pixels[i]);
        f32 scale = 1.0f / Prog_IconPageSize;
        icon->texture = state->iconPages[page];
        icon->uv = (R_Rect){ (f32)x * scale, (f32)y * scale, (f32)(x + size) * scale, (f32)(y + size) * scale };
        icon->state = Prog_IconState_Loaded;
    }
}
