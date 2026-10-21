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
        if (request->warm) {
            // Nothing comes back: the system's cache keeps it, unless it had it already.
            ProfileScope("WarmThumbnail") if (!P_LoadCachedThumbnail(scratch.arena, request->path, request->kind, request->size, &image)) {
                P_LoadFileThumbnail(scratch.arena, request->path, request->kind, request->size, true, &job->cancelToken, &image);
            }
            ReleaseScratch(scratch);
            continue;
        }
        // A 3D model is drawn here, in every format the readers know, so that they look alike and a .blend shows its scene rather than the
        // screenshot Blender saves; the cache pass leaves it for the thumbnail threads, which go easy on a spinning disk.
        b32 model = thumbnail && request->kind == P_FileKind_File && Prog_IsModel(request->path);
        b32 blp = thumbnail && request->kind == P_FileKind_File && Prog_IsBlp(request->path);
        // A game's texture is read here too, as the system has no thumbnails for it; it is quick, so the cache pass does it.
        if (blp) {
            P_Image picture = {0};
            if (Prog_LoadBlp(scratch.arena, request->path, request->size, &picture, NULL, NULL)) pixels[i] = Prog_ResizeIcon(&job->arena, picture, request->size);
            ReleaseScratch(scratch);
            continue;
        }
        if (model && request->cacheOnly) {
            ReleaseScratch(scratch);
            continue;
        }
        if (model) {
            ProfileScope("ModelThumbnail") pixels[i] = Prog_ModelThumbnail(&job->arena, request->path, request->size, &job->cancelToken);
            if (pixels[i]) {
                ReleaseScratch(scratch);
                continue;
            }
        }
        if (thumbnail && request->cacheOnly) ProfileScope("CachedThumbnail") loaded = P_LoadCachedThumbnail(scratch.arena, request->path, request->kind, request->size, &image);
        else if (thumbnail) ProfileScope("LoadThumbnail") loaded = P_LoadFileThumbnail(scratch.arena, request->path, request->kind, request->size, false, &job->cancelToken, &image);
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
        // Requests outlive the frame when nothing submits them before the next one.
        b8 slowDisk = size != state->iconSize && !plain && state->directory.driveKind == P_DriveKind_Spinning;
        Prog_IconRequest request = { .path = StrCopy(&state->iconArena, path), .key = StrCopy(&state->iconArena, key), .kind = (u8)kind, .size = size,
                                     .exact = (b8)(exact != 0), .plain = (b8)(plain != 0), .slowDisk = slowDisk };
        Prog_Icon pending = { .state = Prog_IconState_Pending, .exact = request.exact, .plain = request.plain, .kind = request.kind, .path = request.path,
                              .wanted = state->ui.frameIndex };
        MapsPut(arena, *map, StrCopy(arena, key), pending);
        ArrayPush(&state->iconArena, state->iconRequests, request);
        ++state->iconsPending;
        return NULL;
    }
    if (icon->state == Prog_IconState_Pending) icon->wanted = state->ui.frameIndex;
    if (icon->state == Prog_IconState_Pending && !icon->making) ++state->iconsPending;
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

// Asks for a loaded or failed icon again; a loaded one shows until the new one takes its cell.
function void Prog__ReloadIcon(Prog_State *state, String key, Prog_Icon *icon, i32 size)
{
    if (icon->state == Prog_IconState_Pending || icon->reloading || !icon->path.length) return;
    if (icon->state == Prog_IconState_Failed) icon->state = Prog_IconState_Pending;
    else icon->reloading = true;
    Prog_IconRequest request = { .path = icon->path, .key = StrCopy(&state->iconArena, key), .kind = icon->kind, .size = size, .exact = icon->exact, .plain = icon->plain };
    ArrayPush(&state->iconArena, state->iconRequests, request);
}

// Asks for a folder's own icon again, for one that changes, such as the Recycle Bin's when it fills or empties.
function void Prog_ReloadFolderIcon(Prog_State *state, String path)
{
    Prog_Icon *icon = MapsGetPtrNull(state->icons, StrFormat(&state->frameArena, "exact:%S", path));
    if (icon) Prog__ReloadIcon(state, StrFormat(&state->frameArena, "exact:%S", path), icon, state->iconSize);
}

// Asks for every icon and thumbnail stand-in again, as the system's icons changed, without the blank moment starting over would show.
function void Prog_ReloadIcons(Prog_State *state)
{
    for (u64 i = 0; i < state->icons.header.count; ++i) Prog__ReloadIcon(state, state->icons.v[i].key, &state->icons.v[i].value, state->iconSize);
    for (u32 t = 0; t < Prog_ThumbnailSlots; ++t) {
        Prog_ThumbnailSlot *slot = &state->thumbnailSlots[t];
        for (u64 i = 0; slot->atlas.size && i < slot->map.header.count; ++i) {
            if (slot->map.v[i].value.plain) Prog__ReloadIcon(state, slot->map.v[i].key, &slot->map.v[i].value, slot->atlas.size);
        }
    }
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

// The size warmed thumbnails are made at, which the system's cache scales down for the icon views' tiers.
enum { Prog_WarmSize = 256, Prog_MaxWarmed = 50000 };

function void Prog_WarmThumbnail(Prog_State *state, String directory, const Prog_Entry *entry)
{
    if (state->iconSize <= 0 || entry->drive || entry->file.kind != P_FileKind_File || !Prog_ShowsThumbnails(state)) return;
    String path = Prog_JoinPath(&state->frameArena, directory, entry->file.name);
    // Models are drawn by Filey, not by the system's cache.
    if (Prog_IsModel(path) || Prog_IsBlp(path)) return;
    String key = StrFormat(&state->frameArena, "%S|%lld|%llu", P_FileThumbnailKey(&state->frameArena, path, P_FileKind_File), (long long)entry->file.modifiedTimeUnix,
                           (unsigned long long)entry->file.size);
    if (MapsGetPtrNull(state->warmed, key)) return;
    if (state->warmed.header.count >= Prog_MaxWarmed) {
        state->warmed = (Prog_PathSet){0};
        state->thumbnailWarming = (Prog_ArrayIconRequest){0};
        ArenaClear(&state->warmArena);
    }
    key = StrCopy(&state->warmArena, key);
    MapsPut(&state->warmArena, state->warmed, key, true);
    Prog_IconRequest request = { .path = StrCopy(&state->warmArena, path), .key = key, .kind = P_FileKind_File, .size = Prog_WarmSize, .warm = true,
                                 .slowDisk = state->directory.driveKind == P_DriveKind_Spinning };
    ArrayPush(&state->warmArena, state->thumbnailWarming, request);
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

// A job for `count` of the queued requests that are thumbnails, or that are not; the thumbnails' asks the system's cache only.
function void Prog__SubmitIconJob(Prog_State *state, b32 thumbnails, u64 count)
{
    if (!count) return;
    P_Job *job = P_JobCreate(&state->jobs, Prog__IconJob);
    Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
    input->kind = Prog_JobKind_Icons;
    input->iconSize = state->iconSize;
    input->iconScale = state->iconScale;
    input->icons = ArenaPushArray(&job->arena, count, Prog_IconRequest);
    for (u64 i = 0; i < state->iconRequests.header.count && input->iconCount < count; ++i) {
        Prog_IconRequest *source = &state->iconRequests.v[i];
        if ((source->size != state->iconSize && !source->plain) != (thumbnails != 0)) continue;
        Prog_IconRequest *copy = &input->icons[input->iconCount++];
        *copy = *source;
        copy->cacheOnly = (b8)(thumbnails != 0);
        copy->path = StrCopy(&job->arena, source->path);
        copy->key = StrCopy(&job->arena, source->key);
    }
    job->input = input;
    P_JobSubmit(job);
}

// The pending entry a thumbnail request fills, NULL once its tier or the icon size it was for has gone.
function Prog_Icon *Prog__RequestedThumbnail(Prog_State *state, const Prog_IconRequest *request)
{
    if (request->size == state->iconSize) return NULL;
    // The tab list's folder pictures have their own atlas.
    Prog_Icon *picture = request->size == state->folderPictureAtlas.size ? MapsGetPtrNull(state->folderPictures, request->key) : NULL;
    if (picture) return picture;
    for (u32 t = 0; t < Prog_ThumbnailSlots; ++t) {
        if (state->thumbnailSlots[t].atlas.size == request->size) return MapsGetPtrNull(state->thumbnailSlots[t].map, request->key);
    }
    return NULL;
}

// Starts making the waiting thumbnails, those drawn last first, as many at once as the thumbnail jobs have threads, or one at a time on
// spinning disks, whose heads seeking between files take longer than making them one after another.
// A job each, so that each shows as soon as it is made rather than once a batch of large videos is.
function void Prog__MakeThumbnails(Prog_State *state)
{
    Prog_ArrayIconRequest *waiting = &state->thumbnailWaiting;
    for (;;) {
        u64 best = UINT64_MAX, bestWanted = 0;
        for (u64 i = 0; i < waiting->header.count;) {
            Prog_IconRequest *request = &waiting->v[i];
            Prog_Icon *icon = Prog__RequestedThumbnail(state, request);
            if (!icon || icon->state != Prog_IconState_Pending) {
                MemMove(request, request + 1, (waiting->header.count - i - 1) * sizeof(*request));
                --waiting->header.count;
                continue;
            }
            b32 room = state->thumbnailsMaking < state->thumbnailJobs.threadCount && (!request->slowDisk || !state->thumbnailsMakingSlow);
            if (room && (best == UINT64_MAX || icon->wanted > bestWanted)) best = i, bestWanted = icon->wanted;
            ++i;
        }
        if (best == UINT64_MAX) break;
        Prog_IconRequest request = waiting->v[best];
        MemMove(&waiting->v[best], &waiting->v[best + 1], (waiting->header.count - best - 1) * sizeof(request));
        --waiting->header.count;
        P_Job *job = P_JobCreate(&state->thumbnailJobs, Prog__IconJob);
        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
        input->kind = Prog_JobKind_Icons;
        input->iconSize = state->iconSize;
        input->iconScale = state->iconScale;
        input->icons = ArenaPushStruct(&job->arena, Prog_IconRequest);
        input->icons[0] = request;
        input->icons[0].path = StrCopy(&job->arena, request.path);
        input->icons[0].key = StrCopy(&job->arena, request.key);
        input->iconCount = 1;
        job->input = input;
        ++state->thumbnailsMaking;
        state->thumbnailsMakingSlow += request.slowDisk ? 1u : 0u;
        P_JobSubmit(job);
    }
    // Only with nothing else to make, one at a time, the last one shown first.
    if (!waiting->header.count && !state->thumbnailsMaking && state->thumbnailWarming.header.count) {
        Prog_IconRequest request = state->thumbnailWarming.v[--state->thumbnailWarming.header.count];
        P_Job *job = P_JobCreate(&state->thumbnailJobs, Prog__IconJob);
        Prog_JobInput *input = ArenaPushStruct(&job->arena, Prog_JobInput);
        input->kind = Prog_JobKind_Icons;
        input->iconSize = state->iconSize;
        input->iconScale = state->iconScale;
        input->icons = ArenaPushStruct(&job->arena, Prog_IconRequest);
        input->icons[0] = request;
        input->icons[0].path = StrCopy(&job->arena, request.path);
        input->icons[0].key = StrCopy(&job->arena, request.key);
        input->iconCount = 1;
        job->input = input;
        ++state->thumbnailsMaking;
        state->thumbnailsMakingSlow += request.slowDisk ? 1u : 0u;
        P_JobSubmit(job);
    }
    if (!waiting->header.count) {
        *waiting = (Prog_ArrayIconRequest){0};
        ArenaClear(&state->thumbnailWaitArena);
    }
}

function void Prog_SubmitIconRequests(Prog_State *state)
{
    u64 total = state->iconRequests.header.count, thumbnails = 0;
    for (u64 i = 0; i < total; ++i) thumbnails += state->iconRequests.v[i].size != state->iconSize && !state->iconRequests.v[i].plain;
    Prog__SubmitIconJob(state, false, total - thumbnails);
    // The thumbnails ask the system's cache first, all in one job, as that answers at once; those it lacks wait to be made.
    Prog__SubmitIconJob(state, true, thumbnails);
    state->iconRequests = (Prog_ArrayIconRequest){0};
    Prog__MakeThumbnails(state);
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

// Draws new pixels into a loaded icon's own cell.
function void Prog__ReplaceIcon(const Prog_IconAtlas *atlas, const Prog_Icon *icon, const u32 *pixels)
{
    i32 x = (i32)(icon->uv.x0 * (f32)atlas->pageSize + 0.5f), y = (i32)(icon->uv.y0 * (f32)atlas->pageSize + 0.5f);
    R_TextureUpdateRGBA(icon->texture, x, y, atlas->size, atlas->size, pixels);
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
    if (job->queue == &state->thumbnailJobs && input->iconCount) {
        --state->thumbnailsMaking;
        state->thumbnailsMakingSlow -= input->icons[0].slowDisk ? 1u : 0u;
    }
    // Results for an old icon size are stale; their keys were dropped with the reset.
    if (input->iconSize != state->iconSize || pixels == NULL) return;
    for (u64 i = 0; i < input->iconCount; ++i) {
        Prog_IconRequest *request = &input->icons[i];
        b32 thumbnail = request->size != state->iconSize;
        if (request->warm) continue;
        // Not in the system's cache: it waits to be made.
        Prog_Icon *missed = request->cacheOnly && !pixels[i] ? Prog__RequestedThumbnail(state, request) : NULL;
        if (missed) {
            missed->making = true;
            Prog_IconRequest made = *request;
            made.cacheOnly = false;
            made.path = StrCopy(&state->thumbnailWaitArena, request->path);
            made.key = StrCopy(&state->thumbnailWaitArena, request->key);
            ArrayPush(&state->thumbnailWaitArena, state->thumbnailWaiting, made);
            continue;
        }
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
        if (icon->reloading) {
            icon->reloading = false;
            if (pixels[i]) Prog__ReplaceIcon(slot ? &slot->atlas : &state->iconAtlas, icon, pixels[i]);
        } else if (pixels[i] == NULL) {
            icon->state = Prog_IconState_Failed;
        } else if (!Prog__PlaceIcon(slot ? &slot->atlas : &state->iconAtlas, icon, pixels[i])) {
            if (slot) Prog__ResetThumbnails(slot, request->size);
            else icon->state = Prog_IconState_Failed;
        }
    }
}
