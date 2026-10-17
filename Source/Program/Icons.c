// File icon cache.
// Rows look icons up by key while building UI; unknown keys are queued and loaded by background jobs, then packed into RGBA atlas pages on the main thread.
// Each frame's icons load in one job, and its thumbnails, which the shell or a thumbnailer makes slowly, one a job on workers of their own.

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
        b32 thumbnail = request->size != input->iconSize && !request->plain;
        b32 loaded = false;
        if (thumbnail) ProfileScope("LoadThumbnail") loaded = P_LoadFileThumbnail(scratch.arena, request->path, request->kind, request->size, false, &job->cancelToken, &image);
        else if (request->exact) ProfileScope("LoadExactIcon") loaded = P_LoadFileIcon(scratch.arena, request->path, request->kind, request->size, input->iconScale, true, &image);
        else ProfileScope("LoadIcon") loaded = P_LoadFileIcon(scratch.arena, request->path, request->kind, request->size, input->iconScale, false, &image);
        if (loaded) pixels[i] = Prog_ResizeIcon(&job->arena, image, request->size);
        ReleaseScratch(scratch);
    }
}

// NULL until the icon is loaded; failed and pending icons both return NULL.
function const Prog_Icon *Prog_FileIcon(Prog_State *state, String directory, const Prog_Entry *entry)
{
    return Prog_PathIcon(state, Prog_JoinPath(&state->frameArena, directory, entry->file.name), entry->drive ? P_FileKind_Drive : entry->file.kind);
}

// The cached icon for `key` in `map`, or NULL after queueing a load of `path` at `size` when it is new.
function const Prog_Icon *Prog__LookUpIcon(Prog_State *state, Arena *arena, Prog_IconMap *map, String key, String path, P_FileKind kind, i32 size, b32 exact, b32 plain)
{
    Prog_Icon *icon = MapsGetPtrNull(*map, key);
    if (icon == NULL) {
        MapsPut(arena, *map, StrCopy(arena, key), (Prog_Icon){ .state = Prog_IconState_Pending });
        // Requests outlive the frame when nothing submits them before the next one.
        Prog_IconRequest request = { .path = StrCopy(&state->iconArena, path), .key = StrCopy(&state->iconArena, key), .kind = (u8)kind, .size = size,
                                     .exact = (b8)(exact != 0), .plain = (b8)(plain != 0) };
        ArrayPush(&state->iconArena, state->iconRequests, request);
        if (map == &state->icons) ++state->iconsPending;
        return NULL;
    }
    if (icon->state == Prog_IconState_Pending && map == &state->icons) ++state->iconsPending;
    return icon->state == Prog_IconState_Loaded ? icon : NULL;
}

function const Prog_Icon *Prog_PathIcon(Prog_State *state, String path, P_FileKind kind)
{
    if (state->iconSize <= 0) return NULL;
    String key = P_FileIconKey(&state->frameArena, path, kind);
    return Prog__LookUpIcon(state, &state->iconArena, &state->icons, key, path, kind, state->iconSize, false, false);
}

// The folder's own icon, as the shell shows the desktop or downloads, for the few folders named in the sidebar and the tabs.
function const Prog_Icon *Prog_FolderIcon(Prog_State *state, String path)
{
    // This PC has the system's icon for the computer, where there is one.
    if (StrIsEqual(path, Prog_ThisPC)) path = P_ComputerIconPath();
    if (state->iconSize <= 0 || !path.length) return NULL;
    String key = StrFormat(&state->frameArena, "exact:%S", path);
    return Prog__LookUpIcon(state, &state->iconArena, &state->icons, key, path, P_FileKind_Directory, state->iconSize, true, false);
}

// Asks for a folder's own icon again, for one that changes, such as the Recycle Bin's when it fills or empties; the old one shows until the new one comes.
function void Prog_ReloadFolderIcon(Prog_State *state, String path)
{
    String key = StrFormat(&state->frameArena, "exact:%S", path);
    Prog_Icon *icon = MapsGetPtrNull(state->icons, key);
    if (!icon || icon->state == Prog_IconState_Pending) return;
    if (icon->state == Prog_IconState_Failed) icon->state = Prog_IconState_Pending;
    Prog_IconRequest request = { .path = StrCopy(&state->iconArena, path), .key = StrCopy(&state->iconArena, key), .kind = P_FileKind_Directory, .size = state->iconSize, .exact = true };
    ArrayPush(&state->iconArena, state->iconRequests, request);
}

function void Prog__ResetAtlas(Prog_IconAtlas *atlas, i32 size, i32 pageSize, u32 pageLimit)
{
    atlas->size = size;
    atlas->pageSize = pageSize;
    atlas->pageLimit = Min(pageLimit, (u32)CArrayCount(atlas->pages));
    atlas->cell = 0;
    atlas->pageCount = 0;  // pages are kept and reused
}

// Drops a slot's thumbnails, when its atlas is full, the icons start over or the slot is taken for another tier; the items still shown ask
// for theirs again.
function void Prog__ResetThumbnails(Prog_ThumbnailSlot *slot, i32 size)
{
    slot->map = (Prog_IconMap){0};
    ArenaClear(&slot->arena);
    // At most 64 MB of pages.
    i32 pageSize = 2048;
    Prog__ResetAtlas(&slot->atlas, size, pageSize, (u32)Max((i64)1, (i64)(UINT64_C(64) << 20) / ((i64)pageSize * pageSize * 4)));
}

// The size thumbnails load at for `size`: the next of a few tiers, each half again the last, so that what is drawn is never scaled down by
// more than that and a zoom step rarely crosses into another.
function i32 Prog__ThumbnailTier(i32 size)
{
    local_persist const i32 tiers[] = { 64, 96, 128, 192, 256, 384, 512, 768 };
    for (u32 i = 0; i < CArrayCount(tiers); ++i) if (size <= tiers[i]) return tiers[i];
    return size;
}

// The slot holding `tier`'s thumbnails, marked drawn this frame: its own, or else the one drawn from longest ago, started over for it.
function Prog_ThumbnailSlot *Prog__ThumbnailSlot(Prog_State *state, i32 tier)
{
    Prog_ThumbnailSlot *result = NULL, *oldest = &state->thumbnailSlots[0];
    for (u32 i = 0; i < Prog_ThumbnailSlots && !result; ++i) {
        Prog_ThumbnailSlot *slot = &state->thumbnailSlots[i];
        if (slot->atlas.size == tier) result = slot;
        else if (!slot->atlas.size || (oldest->atlas.size && slot->drawn < oldest->drawn)) oldest = slot;
    }
    if (!result) {
        result = oldest;
        Prog__ResetThumbnails(result, tier);
    }
    result->drawn = state->ui.frameIndex;
    return result;
}

// `key`'s picture at another tier, drawn until its own comes, or NULL.
function const Prog_Icon *Prog__ThumbnailElsewhere(Prog_State *state, const Prog_ThumbnailSlot *own, String key)
{
    for (u32 i = 0; i < Prog_ThumbnailSlots; ++i) {
        const Prog_ThumbnailSlot *slot = &state->thumbnailSlots[i];
        const Prog_Icon *icon = slot != own && slot->atlas.size ? MapsGetPtrNull(slot->map, key) : NULL;
        if (icon && icon->state == Prog_IconState_Loaded) return icon;
    }
    return NULL;
}

// Drops every cached icon, e.g. when the display scale changes the icon size.
function void Prog__ResetIcons(Prog_State *state, i32 size)
{
    state->icons = (Prog_IconMap){0};
    state->iconRequests = (Prog_ArrayIconRequest){0};
    state->iconSize = size;
    Prog__ResetAtlas(&state->iconAtlas, size, Prog_IconPageSize, 8);
    state->spinnerSize = 0;
    ArenaClear(&state->iconArena);
    for (u32 i = 0; i < Prog_ThumbnailSlots; ++i) Prog__ResetThumbnails(&state->thumbnailSlots[i], 0);
    state->folderPictures = (Prog_IconMap){0};
    ArenaClear(&state->folderPictureArena);
    state->folderPictureAtlas.size = 0;
}

// The icon views' picture of `entry` for drawing at `size`, loaded at its tier; the last tier's while this one's loads.
function const Prog_Icon *Prog_ThumbnailIcon(Prog_State *state, String directory, const Prog_Entry *entry, i32 size)
{
    if (state->iconSize <= 0 || size <= 0) return NULL;
    Prog_ThumbnailSlot *slot = Prog__ThumbnailSlot(state, Prog__ThumbnailTier(size));
    String path = Prog_JoinPath(&state->frameArena, directory, entry->file.name);
    P_FileKind kind = entry->drive ? P_FileKind_Drive : entry->file.kind;
    // A file that changes, such as a screenshot shown while it is still being written, gets its picture again.
    String key = StrFormat(&state->frameArena, "%S|%lld|%llu", P_FileThumbnailKey(&state->frameArena, path, kind), (long long)entry->file.modifiedTimeUnix,
                           (unsigned long long)entry->file.size);
    const Prog_Icon *icon = Prog__LookUpIcon(state, &slot->arena, &slot->map, key, path, kind, slot->atlas.size, false, false);
    return icon ? icon : Prog__ThumbnailElsewhere(state, slot, key);
}

// The picture of the folder at `path`, as its thumbnail shows it, at `size` squared, for the tab list.
function const Prog_Icon *Prog_FolderPicture(Prog_State *state, String path, i32 size)
{
    if (state->iconSize <= 0 || size <= 0 || size == state->iconSize) return NULL;
    if (size != state->folderPictureAtlas.size) {
        state->folderPictures = (Prog_IconMap){0};
        ArenaClear(&state->folderPictureArena);
        Prog__ResetAtlas(&state->folderPictureAtlas, size, Prog_IconPageSize, 1);
    }
    String key = StrFormat(&state->frameArena, "picture:%S", P_FileThumbnailKey(&state->frameArena, path, P_FileKind_Directory));
    return Prog__LookUpIcon(state, &state->folderPictureArena, &state->folderPictures, key, path, P_FileKind_Directory, size, false, false);
}

// The icon of `entry`'s type at its thumbnail's tier, shared by every item of that type, for the icon views to show until the thumbnail comes.
function const Prog_Icon *Prog_ThumbnailStandIn(Prog_State *state, String directory, const Prog_Entry *entry, i32 size)
{
    if (state->iconSize <= 0 || size <= 0) return NULL;
    Prog_ThumbnailSlot *slot = Prog__ThumbnailSlot(state, Prog__ThumbnailTier(size));
    String path = Prog_JoinPath(&state->frameArena, directory, entry->file.name);
    P_FileKind kind = entry->drive ? P_FileKind_Drive : entry->file.kind;
    String key = StrFormat(&state->frameArena, "type:%S", P_FileIconKey(&state->frameArena, path, kind));
    const Prog_Icon *icon = Prog__LookUpIcon(state, &slot->arena, &slot->map, key, path, kind, slot->atlas.size, false, true);
    return icon ? icon : Prog__ThumbnailElsewhere(state, slot, key);
}

// A job for `count` of the queued requests that are thumbnails, or that are not, from the `skip`th of those on.
function void Prog__SubmitIconJob(Prog_State *state, b32 thumbnails, u64 skip, u64 count)
{
    if (!count) return;
    P_Job *job = P_JobCreate(thumbnails ? &state->thumbnailJobs : &state->jobs, Prog__IconJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Icons;
    input->iconSize = state->iconSize;
    input->iconScale = state->iconScale;
    input->icons = ArenaPushArray(&job->arena, count, Prog_IconRequest);
    for (u64 i = 0, seen = 0; i < state->iconRequests.header.count && input->iconCount < count; ++i) {
        Prog_IconRequest *source = &state->iconRequests.v[i];
        if ((source->size != state->iconSize && !source->plain) != (thumbnails != 0) || seen++ < skip) continue;
        Prog_IconRequest *copy = &input->icons[input->iconCount++];
        *copy = *source;
        copy->path = StrCopy(&job->arena, source->path);
        copy->key = StrCopy(&job->arena, source->key);
    }
    job->input = input;
    P_JobSubmit(job);
}

function void Prog_SubmitIconRequests(Prog_State *state)
{
    u64 total = state->iconRequests.header.count, thumbnails = 0;
    if (!total) return;
    for (u64 i = 0; i < total; ++i) thumbnails += state->iconRequests.v[i].size != state->iconSize && !state->iconRequests.v[i].plain;
    Prog__SubmitIconJob(state, false, 0, total - thumbnails);
    // A job each, so that each shows as soon as it is made rather than once a batch of large photos is.
    for (u64 first = 0; first < thumbnails; ++first) Prog__SubmitIconJob(state, true, first, 1);
    state->iconRequests = (Prog_ArrayIconRequest){0};
}

// Copies `pixels` (the atlas's size square) into its next cell; false when it is full.
function b32 Prog__PlaceIcon(Prog_IconAtlas *atlas, Prog_Icon *icon, const u32 *pixels)
{
    i32 size = atlas->size;
    if (size <= 0 || size > atlas->pageSize) return false;
    i32 perRow = atlas->pageSize / size;
    u32 page = atlas->cell / (u32)(perRow * perRow);
    u32 cell = atlas->cell % (u32)(perRow * perRow);
    if (page >= atlas->pageLimit) return false;
    if (page >= atlas->pageCount) {
        if (!atlas->pages[page].u64[0]) atlas->pages[page] = R_TextureCreateRGBA(atlas->pageSize, atlas->pageSize, NULL);
        atlas->pageCount = page + 1;
    }
    ++atlas->cell;
    i32 x = (i32)(cell % (u32)perRow) * size, y = (i32)(cell / (u32)perRow) * size;
    R_TextureUpdateRGBA(atlas->pages[page], x, y, size, size, pixels);
    f32 scale = 1.0f / (f32)atlas->pageSize;
    icon->texture = atlas->pages[page];
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
        if (!pixels || !Prog__PlaceIcon(&state->iconAtlas, &placed, pixels)) placed.state = Prog_IconState_Failed;
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
    // Centered on a pixel, so that the straight rays are one sharp column wide with soft edges.
    f32 radius = (f32)size * 0.5f, center = floorf(radius) + 0.5f, inner = radius * 0.42f, outer = radius * 0.78f, half = MaxF32(0.6f, radius * 0.1f);
    f32 pi = 3.14159265f;
    for (u32 frame = 0; frame < Prog_SpinnerFrames; ++frame) {
        f32 turn = (f32)frame / Prog_SpinnerFrames * (pi / 4);
        for (i32 y = 0; y < size; ++y) {
            for (i32 x = 0; x < size; ++x) {
                f32 px = (f32)x + 0.5f - center, py = (f32)y + 0.5f - center;
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
        if (!Prog__PlaceIcon(&state->iconAtlas, &state->spinner[frame], pixels)) state->spinner[frame].state = Prog_IconState_Failed;
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
        Prog_IconRequest *request = &input->icons[i];
        b32 thumbnail = request->size != state->iconSize;
        // The tab list's folder pictures have their own atlas.
        Prog_Icon *picture = thumbnail && request->size == state->folderPictureAtlas.size ? MapsGetPtrNull(state->folderPictures, request->key) : NULL;
        if (picture) {
            if (!pixels[i] || !Prog__PlaceIcon(&state->folderPictureAtlas, picture, pixels[i])) picture->state = Prog_IconState_Failed;
            continue;
        }
        // Thumbnails and their stand-ins of a tier no slot holds any longer are dropped; the items ask again at theirs.
        Prog_ThumbnailSlot *slot = NULL;
        for (u32 t = 0; thumbnail && t < Prog_ThumbnailSlots; ++t) if (state->thumbnailSlots[t].atlas.size == request->size) slot = &state->thumbnailSlots[t];
        if (thumbnail && !slot) continue;
        Prog_IconMap *map = slot ? &slot->map : &state->icons;
        Prog_Icon *icon = MapsGetPtrNull(*map, request->key);
        if (icon == NULL) continue;
        if (pixels[i] == NULL) {
            icon->state = Prog_IconState_Failed;
        } else if (!Prog__PlaceIcon(slot ? &slot->atlas : &state->iconAtlas, icon, pixels[i])) {
            if (slot) Prog__ResetThumbnails(slot, request->size);
            else icon->state = Prog_IconState_Failed;
        }
    }
}
