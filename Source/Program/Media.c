// Audio and video through the FFmpeg tools found on the PATH: the details ffprobe reads of a file, an audio file's waveform and a video's frame,
// videos compressed, on the graphics card when it has an encoder, and audio decoded by ffmpeg for the platform's sound; downloads by URL with yt-dlp, and torrents
// with aria2c.

function b32 Prog__HasExtension(String path, const String *extensions, u32 count)
{
    Index dot = StrIndexChar(path, '.', SSF_SearchBackwards);
    if (!dot.exists) return false;
    String extension = StrFrom(path, dot.value + 1);
    for (u32 i = 0; i < count; ++i) if (Prog__EqualFolded(extension, extensions[i])) return true;
    return false;
}

// TypeScript's .ts and device trees' .dts are left out, which are more often those than media; camcorders' .mts stays, rarer as TypeScript.
function b32 Prog_IsAudio(String path)
{
    const String extensions[] = { S("mp3"), S("flac"), S("wav"), S("ogg"), S("oga"), S("opus"), S("m4a"), S("aac"), S("wma"), S("aif"), S("aiff"), S("ape"),
                                  S("wv"), S("mka"), S("ac3") };
    return Prog__HasExtension(path, extensions, CArrayCount(extensions));
}

function b32 Prog_IsVideo(String path)
{
    const String extensions[] = { S("mp4"), S("mkv"), S("mov"), S("avi"), S("webm"), S("wmv"), S("flv"), S("m4v"), S("mpg"), S("mpeg"), S("m2ts"),
                                  S("mts"), S("3gp"), S("ogv") };
    return Prog__HasExtension(path, extensions, CArrayCount(extensions));
}

// A number as ffprobe writes one, "48000", "5.000000" or a ratio such as a frame rate's "30000/1001"; 0 for one it does not.
function f64 Prog__MediaNumber(String text)
{
    f64 result = 0, scale = 0;
    u64 i = 0;
    for (; i < text.length && ((text.value[i] >= '0' && text.value[i] <= '9') || text.value[i] == '.'); ++i) {
        if (text.value[i] == '.') scale = 1;
        else if (scale > 0) result += (text.value[i] - '0') * (scale /= 10);
        else result = result * 10 + (text.value[i] - '0');
    }
    if (i < text.length && text.value[i] == '/') {
        f64 below = Prog__MediaNumber(StrFrom(text, i + 1));
        result = below > 0 ? result / below : 0;
    }
    return result;
}

// The value of `key` in one of ffprobe's compact lines, "key=value|key=value".
function String Prog__MediaField(String line, String key)
{
    for (u64 at = 0; at < line.length;) {
        Index bar = StrIndexChar(StrFrom(line, at), '|', SSF_CountOnFail);
        String field = StrFromTo(line, at, (i64)(at + bar.value));
        at += bar.value + 1;
        if (field.length > key.length && field.value[key.length] == '=' && StrIsEqual(StrFromTo(field, 0, (i64)key.length), key)) return StrFrom(field, key.length + 1);
    }
    return (String){0};
}

// The details ffprobe reads of the media file at `path`, a "label\tvalue" line each, with the labels in English, translated where shown;
// empty when ffprobe is not there or reads nothing.
function String Prog_MediaDetails(Arena *arena, String path, const P_CancelToken *cancel, f64 *seconds)
{
    *seconds = 0;
    Scratch scratch = GetScratchEx(arena);
    String arguments[] = { S("ffprobe"), S("-v"), S("error"), S("-show_entries"),
                           S("format=duration,bit_rate:stream=codec_type,codec_name,width,height,avg_frame_rate,sample_rate,channels"), S("-of"), S("compact=p=0"), path };
    String output = {0};
    StrBuilder text = {0};
    if (P_RunCommandWith(scratch.arena, Prog_ParentPath(path), arguments, CArrayCount(arguments), NULL, 0, 10000, &output, false, cancel)) {
        ArrayStr lines = StrSplit(scratch.arena, output, '\n');
        f64 duration = 0, bitrate = 0;
        b32 video = false, audio = false;
        for (u64 i = 0; i < lines.header.count; ++i) {
            String line = lines.v[i];
            if (line.length && line.value[line.length - 1] == '\r') --line.length;
            String type = Prog__MediaField(line, S("codec_type")), codec = Prog__MediaField(line, S("codec_name"));
            if (StrIsEqual(type, S("video")) && !video) {
                video = true;
                f64 fps = Prog__MediaNumber(Prog__MediaField(line, S("avg_frame_rate")));
                StrBuildStr(arena, &text, StrFormat(scratch.arena, "%S\t%S, %u×%u", TrMark("Video"), codec, (u32)Prog__MediaNumber(Prog__MediaField(line, S("width"))),
                                                    (u32)Prog__MediaNumber(Prog__MediaField(line, S("height")))));
                // A whole rate without decimals, as "30 fps", and NTSC's as "29.97 fps".
                if (fps > 0) StrBuildStr(arena, &text, StrFormat(scratch.arena, fps - floor(fps) < 0.005 ? ", %.0f fps" : ", %.2f fps", fps));
                StrBuildChar(arena, &text, '\n');
            } else if (StrIsEqual(type, S("audio")) && !audio) {
                audio = true;
                StrBuildStr(arena, &text, StrFormat(scratch.arena, "%S\t%S, %.1f kHz, %u ch\n", TrMark("Audio"), codec, Prog__MediaNumber(Prog__MediaField(line, S("sample_rate"))) / 1000,
                                                    (u32)Prog__MediaNumber(Prog__MediaField(line, S("channels")))));
            } else if (!type.length) {
                duration = Prog__MediaNumber(Prog__MediaField(line, S("duration")));
                bitrate = Prog__MediaNumber(Prog__MediaField(line, S("bit_rate")));
            }
        }
        *seconds = duration;
        if (duration > 0) {
            u32 seconds = (u32)(duration + 0.5), hours = seconds / 3600;
            String clock = hours ? StrFormat(scratch.arena, "%u:%02u:%02u", hours, seconds / 60 % 60, seconds % 60) : StrFormat(scratch.arena, "%u:%02u", seconds / 60, seconds % 60);
            StrBuildStr(arena, &text, StrFormat(scratch.arena, "%S\t%S\n", TrMark("Duration"), clock));
        }
        if (bitrate > 0) StrBuildStr(arena, &text, StrFormat(scratch.arena, "%S\t%.0f kb/s\n", TrMark("Bitrate"), bitrate / 1000));
    }
    ReleaseScratch(scratch);
    return text.buffer;
}

// A picture ffmpeg draws of the media file at `path` with `filter`, before the input with `before` (such as a time to seek to), into `image`;
// false without ffmpeg. Drawn once a run for each file and kind, into the run's folder.
// It is drawn under a name of its own first, so that one stopped halfway leaves no part of a picture to be taken for the whole.
function b32 Prog__MediaPicture(Arena *arena, String path, String kind, String before, String filter, const P_CancelToken *cancel, i32 side, P_Image *image, i32 *width,
                                i32 *height)
{
    Scratch scratch = GetScratchEx(arena);
    String folder = P_RunFolder(scratch.arena);
    b32 result = false;
    if (folder.length) {
        String picture = Prog_JoinPath(scratch.arena, folder, StrFormat(scratch.arena, "%S-%016" PRIx64 ".png", kind, MapHashBytes(path.value, path.length)));
        String arguments[16];
        u32 count = 0;
        arguments[count++] = S("ffmpeg"), arguments[count++] = S("-v"), arguments[count++] = S("error"), arguments[count++] = S("-y");
        if (before.length) arguments[count++] = S("-ss"), arguments[count++] = before;
        arguments[count++] = S("-i"), arguments[count++] = path, arguments[count++] = S("-lavfi"), arguments[count++] = filter;
        local_persist atomic_uint drawings;
        String drawing = StrFormat(scratch.arena, "%S-%u.png", StrFromTo(picture, 0, (i64)picture.length - 4), atomic_fetch_add(&drawings, 1));
        arguments[count++] = S("-frames:v"), arguments[count++] = S("1"), arguments[count++] = drawing;
        String output = {0};
        b32 drawn = P_PathExists(picture);
        if (!drawn) {
            if (P_RunCommandWith(scratch.arena, folder, arguments, count, NULL, 0, 60000, &output, true, cancel)) P_ReplaceFile(scratch.arena, drawing, picture);
            if (P_PathExists(drawing)) P_DeleteItems(scratch.arena, &drawing, 1, false, NULL);
            // Another job's drawing of the same file may have been moved there meanwhile.
            drawn = P_PathExists(picture);
        }
        result = drawn && P_LoadImageFile(arena, picture, side, image, width, height);
    }
    ReleaseScratch(scratch);
    return result;
}

// An audio file's waveform, both channels in one, its louder parts drawn taller on a clear ground.
function b32 Prog_MediaWaveform(Arena *arena, String path, const P_CancelToken *cancel, i32 side, P_Image *image, i32 *width, i32 *height)
{
    return Prog__MediaPicture(arena, path, S("wave"), (String){0}, S("showwavespic=s=1600x400:colors=0x4FA3E0:split_channels=0:scale=sqrt"), cancel, side, image, width,
                              height);
}

// A video's frame a tenth of the way in, or its first in one too short to tell, for one the system has no picture of.
function b32 Prog_MediaFrame(Arena *arena, String path, f64 duration, const P_CancelToken *cancel, i32 side, P_Image *image, i32 *width, i32 *height)
{
    Scratch scratch = GetScratchEx(arena);
    String at = StrFormat(scratch.arena, "%.2f", duration * 0.1);
    b32 result = Prog__MediaPicture(arena, path, S("frame"), at, S("scale='min(1600,iw)':-2"), cancel, side, image, width, height);
    ReleaseScratch(scratch);
    return result;
}

// The graphics cards' encoders, tried in this order, then the processor's.
typedef enum Prog_Encoder {
    Prog_Encoder_Nvidia,
    Prog_Encoder_Amd,
    Prog_Encoder_Intel,
    Prog_Encoder_Software,
    Prog_Encoder_Count,
} Prog_Encoder;

// The encoder of each card for each codec (HEVC, H.264, AV1), and the processor's.
global const char *const Prog_EncoderNames[3][Prog_Encoder_Count] = {
    { "hevc_nvenc", "hevc_amf", "hevc_qsv", "libx265" },
    { "h264_nvenc", "h264_amf", "h264_qsv", "libx264" },
    { "av1_nvenc", "av1_amf", "av1_qsv", "libsvtav1" },
};

// The first encoder for `codec` that encodes here, found by encoding a moment of a plain picture with each, since FFmpeg lists the cards'
// encoders whether or not such a card is in the machine. Found once a run, on whichever thread asks first; one stopped by `cancel` is not kept.
function Prog_Encoder Prog_VideoEncoder(u32 codec, const P_CancelToken *cancel)
{
    local_persist atomic_uint found[3];
    codec = Min(codec, 2u);
    u32 known = atomic_load(&found[codec]);
    if (known) return (Prog_Encoder)(known - 1);
    Prog_Encoder result = Prog_Encoder_Software;
    Scratch scratch = GetScratch();
    for (u32 e = 0; e < Prog_Encoder_Software && !P_IsCancelled(cancel); ++e) {
        String arguments[] = { S("ffmpeg"), S("-v"), S("error"), S("-f"), S("lavfi"), S("-i"), S("color=s=256x256:d=0.1"), S("-frames:v"), S("1"), S("-c:v"),
                               StrFromCStr(Prog_EncoderNames[codec][e]), S("-f"), S("null"), S("-") };
        String output = {0};
        if (P_RunCommandWith(scratch.arena, P_RunFolder(scratch.arena), arguments, CArrayCount(arguments), NULL, 0, 20000, &output, true, cancel)) {
            result = (Prog_Encoder)e;
            break;
        }
    }
    ReleaseScratch(scratch);
    if (!P_IsCancelled(cancel)) atomic_store(&found[codec], (u32)result + 1);
    return result;
}

// Whether MP4 holds every audio track of the media file at `path` as it is, which a file without any does too.
function b32 Prog__AudioFitsMp4(Arena *arena, String path, const P_CancelToken *cancel)
{
    String arguments[] = { S("ffprobe"), S("-v"), S("error"), S("-select_streams"), S("a"), S("-show_entries"), S("stream=index,codec_name"), S("-of"), S("csv=p=0"), path };
    String output = {0};
    if (!P_RunCommandWith(arena, Prog_ParentPath(path), arguments, CArrayCount(arguments), NULL, 0, 10000, &output, false, cancel)) return false;
    const String copied[] = { S("aac"), S("mp3"), S("opus"), S("ac3"), S("eac3"), S("flac"), S("alac") };
    ArrayStr lines = StrSplit(arena, output, '\n');
    for (u64 i = 0; i < lines.header.count; ++i) {
        // "1,aac", a track's index and codec; one ffprobe does not know has no codec, and does not fit.
        String line = lines.v[i];
        if (line.length && line.value[line.length - 1] == '\r') --line.length;
        if (!line.length) continue;
        Index comma = StrIndexChar(line, ',', SSF_None);
        String codec = comma.exists ? StrFrom(line, comma.value + 1) : (String){0};
        b32 fits = false;
        for (u32 c = 0; c < CArrayCount(copied); ++c) fits |= StrIsEqual(codec, copied[c]);
        if (!fits) return false;
    }
    return true;
}

// What ffmpeg's -progress lines and messages say as they come: how far it is, at what speed, and the last message, for an error.
typedef struct Prog__Encoding {
    P_Progress *progress;
    u64 base;
    String name;
    char pending[512];
    u64 pendingLength;
    char message[256];
    u64 messageLength;
} Prog__Encoding;

function void Prog__EncodingLine(Prog__Encoding *encoding, String line)
{
    if (line.length && line.value[line.length - 1] == '\r') --line.length;
    Index equals = StrIndexChar(line, '=', SSF_None);
    String key = equals.exists ? StrFromTo(line, 0, (i64)equals.value) : (String){0}, value = equals.exists ? StrFrom(line, equals.value + 1) : (String){0};
    while (value.length && value.value[0] == ' ') value = StrFrom(value, 1);
    if (StrIsEqual(key, S("out_time_us")) && encoding->progress) {
        // Done is counted in milliseconds of video, after those of the videos before this one.
        u64 done = encoding->base + (u64)(Prog__MediaNumber(value) / 1000);
        atomic_store(&encoding->progress->doneBytes, Min(done, atomic_load(&encoding->progress->totalBytes)));
    } else if (StrIsEqual(key, S("speed")) && encoding->progress && value.length && value.value[0] != 'N') {
        Scratch scratch = GetScratch();
        P_ProgressName(encoding->progress, StrFormat(scratch.arena, "%S \u00B7 %S", encoding->name, value));
        ReleaseScratch(scratch);
    } else if (!equals.exists && line.length) {
        encoding->messageLength = Min(line.length, sizeof(encoding->message));
        MemCopy(encoding->message, line.value, encoding->messageLength);
    }
}

function void Prog__EncodingOutput(void *user, String text)
{
    Prog__Encoding *encoding = user;
    for (u64 i = 0; i < text.length; ++i) {
        char c = text.value[i];
        if (c == '\n') {
            Prog__EncodingLine(encoding, StrMake(encoding->pending, encoding->pendingLength));
            encoding->pendingLength = 0;
        } else if (encoding->pendingLength < sizeof(encoding->pending)) {
            encoding->pending[encoding->pendingLength++] = c;
        }
    }
}

// Encodes the video at `path` again into an MP4 file beside it, "<name> (compressed).mp4" or numbered past one taken, whose path goes into `placed`.
// It encodes with `codec` (HEVC, H.264 or AV1) at the constant quality `quality`, 0 best to 51, on the graphics card when one has an encoder for it.
// Every audio track goes along, copied when MP4 holds it as it is and AAC otherwise; subtitles stay behind, as MP4 holds few kinds.
// `progress` counts the video's milliseconds after `base`; a cancel or a failure leaves no output.
function P_FileError Prog_CompressVideo(Arena *arena, String path, u32 codec, u32 quality, u64 base, P_Progress *progress, const P_CancelToken *cancel, String *placed)
{
    Scratch scratch = GetScratchEx(arena);
    codec = Min(codec, 2u);
    Prog_Encoder encoder = Prog_VideoEncoder(codec, cancel);
    b32 copy = Prog__AudioFitsMp4(scratch.arena, path, cancel);
    if (P_IsCancelled(cancel)) {
        ReleaseScratch(scratch);
        return (P_FileError){ .code = 1 };
    }
    // The name is taken by an empty file just before ffmpeg starts, which another compression of the same video then numbers past.
    String folder = Prog_ParentPath(path), made = {0};
    P_FileError taken = P_CreateItem(arena, folder, StrFormat(scratch.arena, "%S (compressed).mp4", Prog__NameStem(Prog_EntryLeaf(path))), false, &made);
    if (taken.code) {
        ReleaseScratch(scratch);
        return taken;
    }
    String output = Prog_JoinPath(arena, folder, made);
    String q = StrFormat(scratch.arena, "%u", quality);
    String arguments[48];
    u32 count = 0;
#define Prog__Argument(text) (arguments[count++] = (text))
    Prog__Argument(S("ffmpeg")), Prog__Argument(S("-hide_banner")), Prog__Argument(S("-nostdin")), Prog__Argument(S("-v")), Prog__Argument(S("error")), Prog__Argument(S("-y"));
    Prog__Argument(S("-i")), Prog__Argument(path), Prog__Argument(S("-map")), Prog__Argument(S("0:v:0")), Prog__Argument(S("-map")), Prog__Argument(S("0:a?"));
    Prog__Argument(S("-c:v")), Prog__Argument(StrFromCStr(Prog_EncoderNames[codec][encoder]));
    if (encoder == Prog_Encoder_Nvidia) {
        Prog__Argument(S("-preset")), Prog__Argument(S("p6")), Prog__Argument(S("-tune")), Prog__Argument(S("hq")), Prog__Argument(S("-rc")), Prog__Argument(S("vbr"));
        Prog__Argument(S("-cq")), Prog__Argument(q), Prog__Argument(S("-b:v")), Prog__Argument(S("0"));
    } else if (encoder == Prog_Encoder_Amd) {
        Prog__Argument(S("-quality")), Prog__Argument(S("quality")), Prog__Argument(S("-rc")), Prog__Argument(S("cqp"));
        Prog__Argument(S("-qp_i")), Prog__Argument(q), Prog__Argument(S("-qp_p")), Prog__Argument(q);
        if (codec != 2) Prog__Argument(S("-qp_b")), Prog__Argument(q);
    } else if (encoder == Prog_Encoder_Intel) {
        Prog__Argument(S("-preset")), Prog__Argument(S("slower")), Prog__Argument(S("-global_quality")), Prog__Argument(q);
    } else {
        Prog__Argument(S("-crf")), Prog__Argument(q), Prog__Argument(S("-preset")), Prog__Argument(codec == 2 ? S("8") : S("medium"));
    }
    // H.264's encoders take 8 bits only; HEVC is tagged so that Apple's players take it.
    if (codec == 1) Prog__Argument(S("-pix_fmt")), Prog__Argument(S("yuv420p"));
    if (codec == 0) Prog__Argument(S("-tag:v")), Prog__Argument(S("hvc1"));
    Prog__Argument(S("-c:a")), Prog__Argument(copy ? S("copy") : S("aac"));
    if (!copy) Prog__Argument(S("-b:a")), Prog__Argument(S("192k"));
    Prog__Argument(S("-sn")), Prog__Argument(S("-movflags")), Prog__Argument(S("+faststart")), Prog__Argument(S("-progress")), Prog__Argument(S("pipe:1"));
    Prog__Argument(S("-nostats")), Prog__Argument(output);
#undef Prog__Argument
    Prog__Encoding encoding = { .progress = progress, .base = base, .name = Prog_EntryLeaf(path) };
    if (progress) P_ProgressName(progress, encoding.name);
    b32 done = P_RunCommandStream(Prog_ParentPath(output), arguments, count, true, Prog__EncodingOutput, &encoding, cancel, progress);
    P_FileError result = {0};
    if (!done) {
        P_DeleteItems(scratch.arena, &output, 1, false, NULL);
        String message = encoding.messageLength ? StrCopy(arena, StrMake(encoding.message, encoding.messageLength)) : TrMark("FFmpeg is not installed, or could not read the video");
        result = (P_FileError){ .code = 1, .message = message };
    }
    *placed = output;
    ReleaseScratch(scratch);
    return result;
}

// The length of the media file at `path` in milliseconds, 0 when ffprobe does not tell.
function u64 Prog_MediaLength(Arena *arena, String path, const P_CancelToken *cancel)
{
    String arguments[] = { S("ffprobe"), S("-v"), S("error"), S("-show_entries"), S("format=duration"), S("-of"), S("csv=p=0"), path };
    String output = {0};
    Scratch scratch = GetScratchEx(arena);
    b32 read = P_RunCommandWith(scratch.arena, Prog_ParentPath(path), arguments, CArrayCount(arguments), NULL, 0, 10000, &output, false, cancel);
    u64 result = read ? (u64)(Prog__MediaNumber(output) * 1000) : 0;
    ReleaseScratch(scratch);
    return result;
}

// `seconds` as "0:42" or "1:02:03", for how long a download has left.
function String Prog__MediaClock(Arena *arena, u64 seconds)
{
    u64 hours = seconds / 3600;
    return hours ? StrFormat(arena, "%u:%02u:%02u", (u32)hours, (u32)(seconds / 60 % 60), (u32)(seconds % 60)) : StrFormat(arena, "%u:%02u", (u32)(seconds / 60), (u32)(seconds % 60));
}

// What yt-dlp writes as it downloads, through the lines its templates mark: the title, how far each part is with its speed and time left
// and where it is written, the finished file, and its errors.
typedef struct Prog__Fetch {
    P_Progress *progress;
    char pending[2048];
    u64 pendingLength;
    char title[256];
    u64 titleLength;
    char part[512];       // the part being written, removed when the download stops short
    u64 partLength;
    char done[1024];      // the finished file
    u64 doneLength;
    char message[256];
    u64 messageLength;
} Prog__Fetch;

function void Prog__FetchKeep(char *into, u64 room, u64 *length, String text)
{
    *length = Min(text.length, room);
    MemCopy(into, text.value, *length);
}

function void Prog__FetchLine(Prog__Fetch *fetch, String line)
{
    if (line.length && line.value[line.length - 1] == '\r') --line.length;
    Scratch scratch = GetScratch();
    if (line.length > 11 && StrIsEqual(StrFromTo(line, 0, 11), S("FILEYTITLE "))) {
        Prog__FetchKeep(fetch->title, sizeof(fetch->title), &fetch->titleLength, StrFrom(line, 11));
        if (fetch->progress) P_ProgressName(fetch->progress, StrMake(fetch->title, fetch->titleLength));
    } else if (line.length > 10 && StrIsEqual(StrFromTo(line, 0, 10), S("FILEYDONE "))) {
        Prog__FetchKeep(fetch->done, sizeof(fetch->done), &fetch->doneLength, StrFrom(line, 10));
    } else if (line.length > 6 && StrIsEqual(StrFromTo(line, 0, 6), S("FILEY "))) {
        // Downloaded, total, estimated total, speed and seconds left, "NA" for those not known yet, then the part's name, which may hold spaces.
        String fields[5] = {0}, rest = StrFrom(line, 6);
        for (u32 i = 0; i < 5; ++i) {
            Index space = StrIndexChar(rest, ' ', SSF_CountOnFail);
            fields[i] = StrFromTo(rest, 0, (i64)space.value);
            rest = StrFrom(rest, Min(space.value + 1, rest.length));
        }
        Prog__FetchKeep(fetch->part, sizeof(fetch->part), &fetch->partLength, rest);
        if (fetch->progress) {
            u64 total = (u64)Prog__MediaNumber(fields[1]);
            if (!total) total = (u64)Prog__MediaNumber(fields[2]);
            atomic_store(&fetch->progress->totalBytes, total);
            atomic_store(&fetch->progress->doneBytes, Min((u64)Prog__MediaNumber(fields[0]), total));
            StrBuilder name = {0};
            StrBuildStr(scratch.arena, &name, fetch->titleLength ? StrMake(fetch->title, fetch->titleLength) : rest);
            f64 speed = Prog__MediaNumber(fields[3]);
            if (speed > 0) StrBuildStr(scratch.arena, &name, StrFormat(scratch.arena, " \u00B7 %S/s", Prog_FormatSize(scratch.arena, (u64)speed)));
            if (fields[4].length && fields[4].value[0] != 'N') StrBuildStr(scratch.arena, &name, StrFormat(scratch.arena, " \u00B7 %S", Prog__MediaClock(scratch.arena, (u64)Prog__MediaNumber(fields[4]))));
            P_ProgressName(fetch->progress, name.buffer);
        }
    } else if (line.length > 7 && StrIsEqual(StrFromTo(line, 0, 7), S("ERROR: "))) {
        Prog__FetchKeep(fetch->message, sizeof(fetch->message), &fetch->messageLength, StrFrom(line, 7));
    }
    ReleaseScratch(scratch);
}

function void Prog__FetchOutput(void *user, String text)
{
    Prog__Fetch *fetch = user;
    for (u64 i = 0; i < text.length; ++i) {
        char c = text.value[i];
        if (c == '\n') {
            Prog__FetchLine(fetch, StrMake(fetch->pending, fetch->pendingLength));
            fetch->pendingLength = 0;
        } else if (fetch->pendingLength < sizeof(fetch->pending)) {
            fetch->pending[fetch->pendingLength++] = c;
        }
    }
}

// Whether `text` is a web address yt-dlp may download from: http or https, without spaces.
function b32 Prog_IsWebUrl(String text)
{
    b32 web = (text.length > 8 && Prog__EqualFolded(StrFromTo(text, 0, 8), S("https://"))) || (text.length > 7 && Prog__EqualFolded(StrFromTo(text, 0, 7), S("http://")));
    return web && !StrIndexChar(text, ' ', SSF_None).exists && !StrIndexChar(text, '\n', SSF_None).exists;
}

// Downloads the video at the web address `url` into `folder` with yt-dlp, at its best quality as MP4, or with `audio` its sound alone as MP3,
// named after its title. The finished file goes into `placed`; a cancel or a failure leaves no part behind.
function P_FileError Prog_DownloadUrl(Arena *arena, String url, String folder, b32 audio, P_Progress *progress, const P_CancelToken *cancel, ArrayStr *placed)
{
    Scratch scratch = GetScratchEx(arena);
    String arguments[32];
    u32 count = 0;
#define Prog__Argument(text) (arguments[count++] = (text))
    Prog__Argument(S("yt-dlp")), Prog__Argument(S("--encoding")), Prog__Argument(S("utf-8")), Prog__Argument(S("--newline")), Prog__Argument(S("--no-colors"));
    Prog__Argument(S("--no-playlist")), Prog__Argument(S("--progress")), Prog__Argument(S("--no-simulate"));
    if (audio) {
        Prog__Argument(S("-f")), Prog__Argument(S("ba/b")), Prog__Argument(S("-x")), Prog__Argument(S("--audio-format")), Prog__Argument(S("mp3"));
        Prog__Argument(S("--audio-quality")), Prog__Argument(S("0"));
    } else {
        Prog__Argument(S("-f")), Prog__Argument(S("bv*+ba/b")), Prog__Argument(S("--merge-output-format")), Prog__Argument(S("mp4"));
    }
    Prog__Argument(S("--progress-template"));
    Prog__Argument(S("download:FILEY %(progress.downloaded_bytes)s %(progress.total_bytes)s %(progress.total_bytes_estimate)s %(progress.speed)s %(progress.eta)s %(progress.tmpfilename)s"));
    Prog__Argument(S("--print")), Prog__Argument(S("before_dl:FILEYTITLE %(title)s"));
    Prog__Argument(S("--print")), Prog__Argument(S("after_move:FILEYDONE %(filepath)s"));
    Prog__Argument(S("-o")), Prog__Argument(S("%(title)s.%(ext)s")), Prog__Argument(S("--")), Prog__Argument(url);
#undef Prog__Argument
    Prog__Fetch *fetch = ArenaPushStruct(scratch.arena, Prog__Fetch);
    fetch->progress = progress;
    if (progress) {
        atomic_store(&progress->totalItems, 1);
        P_ProgressName(progress, url);
    }
    b32 done = P_RunCommandStream(folder, arguments, count, true, Prog__FetchOutput, fetch, cancel, progress);
    P_FileError result = {0};
    if (done && fetch->doneLength) {
        ArrayPush(arena, *placed, StrCopy(arena, StrMake(fetch->done, fetch->doneLength)));
        if (progress) atomic_store(&progress->doneItems, 1);
    } else {
        if (fetch->partLength) {
            String part = Prog_JoinPath(scratch.arena, folder, StrMake(fetch->part, fetch->partLength));
            if (P_PathExists(part)) P_DeleteItems(scratch.arena, &part, 1, false, NULL);
        }
        String message = fetch->messageLength ? StrCopy(arena, StrMake(fetch->message, fetch->messageLength)) : TrMark("yt-dlp is not installed, or found nothing to download there");
        result = (P_FileError){ .code = 1, .message = message };
    }
    ReleaseScratch(scratch);
    return result;
}

function b32 Prog_IsMagnet(String text)
{
    return text.length > 8 && Prog__EqualFolded(StrFromTo(text, 0, 8), S("magnet:?"));
}

// Whether `text` is a torrent aria2c downloads: a magnet link, or a .torrent file or web address.
function b32 Prog_IsTorrent(String text)
{
    if (Prog_IsMagnet(text)) return true;
    return text.length > 8 && Prog__EqualFolded(StrFrom(text, text.length - 8), S(".torrent")) && !StrIndexChar(text, '\n', SSF_None).exists;
}

// A size as aria2c writes one, "263MiB" or "1.4GiB", in bytes.
function u64 Prog__TorrentBytes(String text)
{
    f64 number = Prog__MediaNumber(text);
    u64 unit = 1;
    if (StrIndexChar(text, 'K', SSF_None).exists) unit = UINT64_C(1) << 10;
    else if (StrIndexChar(text, 'M', SSF_None).exists) unit = UINT64_C(1) << 20;
    else if (StrIndexChar(text, 'G', SSF_None).exists) unit = UINT64_C(1) << 30;
    else if (StrIndexChar(text, 'T', SSF_None).exists) unit = UINT64_C(1) << 40;
    return (u64)(number * (f64)unit);
}

// A time as aria2c writes one, "1h5m30s", in seconds.
function u64 Prog__TorrentSeconds(String text)
{
    u64 result = 0, number = 0;
    for (u64 i = 0; i < text.length; ++i) {
        char c = text.value[i];
        if (c >= '0' && c <= '9') number = number * 10 + (u64)(c - '0');
        else result += number * (c == 'h' ? 3600 : c == 'm' ? 60 : 1), number = 0;
    }
    return result;
}

// What aria2c writes as it downloads: its readout of how far it is, "[#gid 30MiB/263MiB(11%) CN:31 SD:3 DL:22MiB ETA:10s]", the finished
// download's path and its errors.
typedef struct Prog__Torrent {
    P_Progress *progress;
    String name;
    char pending[1024];
    u64 pendingLength;
    char done[1024];
    u64 doneLength;
    char message[256];
    u64 messageLength;
} Prog__Torrent;

function void Prog__TorrentLine(Prog__Torrent *torrent, String line)
{
    while (line.length && line.value[0] == ' ') line = StrFrom(line, 1);
    Index notice = StrIndexStr(line, NoTr("Download complete: "), SSF_None);
    Index error = StrIndexStr(line, S("[ERROR] "), SSF_None);
    if (notice.exists) {
        // The metadata of a magnet link completes first, in memory.
        String path = StrFrom(line, notice.value + 19);
        if (path.length && path.value[0] != '[') Prog__FetchKeep(torrent->done, sizeof(torrent->done), &torrent->doneLength, path);
    } else if (error.exists) {
        Prog__FetchKeep(torrent->message, sizeof(torrent->message), &torrent->messageLength, StrFrom(line, error.value + 8));
    } else if (line.length > 2 && line.value[0] == '[' && line.value[1] == '#' && torrent->progress) {
        Scratch scratch = GetScratch();
        StrBuilder name = {0};
        StrBuildStr(scratch.arena, &name, torrent->name);
        // The peers go last, as the least of what a card cut short leaves out.
        String peers = {0};
        u64 at = StrIndexChar(line, ' ', SSF_CountOnFail).value + 1;
        for (String rest = StrFrom(line, Min(at, line.length)); rest.length;) {
            Index space = StrIndexChar(rest, ' ', SSF_CountOnFail);
            String field = StrFromTo(rest, 0, (i64)space.value);
            rest = StrFrom(rest, Min(space.value + 1, rest.length));
            if (field.length && field.value[field.length - 1] == ']') --field.length;
            Index slash = StrIndexChar(field, '/', SSF_None);
            if (slash.exists && !StrIndexChar(field, ':', SSF_None).exists) {
                u64 total = Prog__TorrentBytes(StrFrom(field, slash.value + 1));
                atomic_store(&torrent->progress->totalBytes, total);
                atomic_store(&torrent->progress->doneBytes, Min(Prog__TorrentBytes(StrFromTo(field, 0, (i64)slash.value)), total));
            } else if (field.length > 3 && StrIsEqual(StrFromTo(field, 0, 3), S("DL:"))) {
                StrBuildStr(scratch.arena, &name, StrFormat(scratch.arena, " \u00B7 %S/s", Prog_FormatSize(scratch.arena, Prog__TorrentBytes(StrFrom(field, 3)))));
            } else if (field.length > 3 && StrIsEqual(StrFromTo(field, 0, 3), S("CN:"))) {
                peers = StrFormat(scratch.arena, " \u00B7 %S %S", StrFrom(field, 3), S("peers"));
            } else if (field.length > 4 && StrIsEqual(StrFromTo(field, 0, 4), S("ETA:"))) {
                StrBuildStr(scratch.arena, &name, StrFormat(scratch.arena, " \u00B7 %S", Prog__MediaClock(scratch.arena, Prog__TorrentSeconds(StrFrom(field, 4)))));
            }
        }
        if (peers.length) StrBuildStr(scratch.arena, &name, peers);
        P_ProgressName(torrent->progress, name.buffer);
        ReleaseScratch(scratch);
    }
}

function void Prog__TorrentOutput(void *user, String text)
{
    Prog__Torrent *torrent = user;
    // The readout is rewritten in place with carriage returns, which end its lines as newlines do.
    for (u64 i = 0; i < text.length; ++i) {
        char c = text.value[i];
        if (c == '\n' || c == '\r') {
            Prog__TorrentLine(torrent, StrMake(torrent->pending, torrent->pendingLength));
            torrent->pendingLength = 0;
        } else if (torrent->pendingLength < sizeof(torrent->pending)) {
            torrent->pending[torrent->pendingLength++] = c;
        }
    }
}

// Downloads the torrent file `source` into `folder` with aria2c, only the files `selection` names when it names any, without seeding after. What
// came down goes into `placed`. aria2c notes how far it is every few seconds, so that a cancel or a quit keeps what is down already, which
// downloading it again goes on from.
function P_FileError Prog_DownloadTorrent(Arena *arena, String source, String folder, String selection, P_Progress *progress, const P_CancelToken *cancel, ArrayStr *placed)
{
    Scratch scratch = GetScratchEx(arena);
    String arguments[12];
    u32 count = 0;
    arguments[count++] = S("aria2c");
    arguments[count++] = StrFormat(scratch.arena, "--dir=%S", folder);
    arguments[count++] = S("--seed-time=0");
    arguments[count++] = S("--auto-save-interval=5");
    arguments[count++] = S("--summary-interval=0");
    arguments[count++] = S("--console-log-level=notice");
    arguments[count++] = S("--enable-color=false");
    arguments[count++] = S("--file-allocation=none");
    arguments[count++] = S("--show-console-readout=true");
    if (selection.length) arguments[count++] = StrFormat(scratch.arena, "--select-file=%S", selection);
    arguments[count++] = S("--");
    arguments[count++] = source;
    Prog__Torrent *torrent = ArenaPushStruct(scratch.arena, Prog__Torrent);
    torrent->progress = progress;
    torrent->name = Prog__NameStem(Prog_EntryLeaf(source));
    if (progress) {
        atomic_store(&progress->totalItems, 1);
        P_ProgressName(progress, torrent->name);
    }
    b32 done = P_RunCommandStream(folder, arguments, count, true, Prog__TorrentOutput, torrent, cancel, progress);
    P_FileError result = {0};
    if (done) {
        if (torrent->doneLength) ArrayPush(arena, *placed, StrCopy(arena, StrMake(torrent->done, torrent->doneLength)));
        if (progress) atomic_store(&progress->doneItems, 1);
    } else {
        String message = torrent->messageLength ? StrCopy(arena, StrMake(torrent->message, torrent->messageLength)) : TrMark("aria2c is not installed, or could not download the torrent");
        result = (P_FileError){ .code = 1, .message = message };
    }
    ReleaseScratch(scratch);
    return result;
}

// The bytes of the bencoded string at `at` in `b`, its length, a colon and them; {0} where there is none.
function String Prog__BencodeString(String b, u64 at)
{
    u64 length = 0, digits = 0;
    for (; at < b.length && b.value[at] >= '0' && b.value[at] <= '9' && digits < 18; ++at, ++digits) length = length * 10 + (u64)(b.value[at] - '0');
    if (!digits || at >= b.length || b.value[at] != ':' || length > b.length - at - 1) return (String){0};
    return StrMake(b.value + at + 1, length);
}

// Where the bencoded value at `at` in `b` ends, or 0 for one that is not whole; lists and dictionaries nest at most 64 deep.
function u64 Prog__BencodeEnd(String b, u64 at, u32 depth)
{
    if (at >= b.length || depth > 64) return 0;
    char c = b.value[at];
    if (c == 'i') {
        Index end = StrIndexChar(StrFrom(b, at), 'e', SSF_None);
        return end.exists ? at + end.value + 1 : 0;
    }
    if (c == 'l' || c == 'd') {
        for (++at; at < b.length && b.value[at] != 'e';) {
            at = Prog__BencodeEnd(b, at, depth + 1);
            if (!at) return 0;
        }
        return at < b.length ? at + 1 : 0;
    }
    String text = Prog__BencodeString(b, at);
    return text.value ? (u64)(text.value - b.value) + text.length : 0;
}

// The bencoded number at `at`, a size, which is never below zero.
function u64 Prog__BencodeNumber(String b, u64 at)
{
    u64 result = 0;
    if (at >= b.length || b.value[at] != 'i') return 0;
    for (++at; at < b.length && b.value[at] >= '0' && b.value[at] <= '9'; ++at) result = result * 10 + (u64)(b.value[at] - '0');
    return result;
}

// Where the value under `key` in the bencoded dictionary at `at` starts, or 0 without one.
function u64 Prog__BencodeFind(String b, u64 at, String key)
{
    if (at >= b.length || b.value[at] != 'd') return 0;
    for (u64 p = at + 1; p < b.length && b.value[p] != 'e';) {
        String name = Prog__BencodeString(b, p);
        if (!name.value) return 0;
        u64 value = (u64)(name.value - b.value) + name.length;
        if (StrIsEqual(name, key)) return value;
        p = Prog__BencodeEnd(b, value, 1);
        if (!p) return 0;
    }
    return 0;
}

// The text under `key` in the dictionary at `at`, or its UTF-8 copy under `utf8` where an old client wrote its own code page in the plain one.
function u64 Prog__BencodeFindText(String b, u64 at, String key, String utf8)
{
    u64 found = Prog__BencodeFind(b, at, utf8);
    return found ? found : Prog__BencodeFind(b, at, key);
}

function b32 Prog_ReadTorrent(Arena *arena, String bytes, Prog_TorrentInfo *info)
{
    *info = (Prog_TorrentInfo){0};
    // Checked whole first, so that what follows reads only values that end.
    if (!Prog__BencodeEnd(bytes, 0, 0)) return false;
    u64 at = Prog__BencodeFind(bytes, 0, S("info"));
    if (!at) return false;
    info->name = StrCopy(arena, Prog__BencodeString(bytes, Prog__BencodeFindText(bytes, at, S("name"), S("name.utf-8"))));
    u64 files = Prog__BencodeFind(bytes, at, S("files"));
    if (!files) {
        // One file, which the name names.
        u64 length = Prog__BencodeFind(bytes, at, S("length"));
        if (!length || !info->name.length) return false;
        info->files = ArenaPushArray(arena, 1, Prog_TorrentFile);
        info->files[0] = (Prog_TorrentFile){ .path = info->name, .size = Prog__BencodeNumber(bytes, length) };
        info->fileCount = 1;
        info->single = true;
        info->size = info->files[0].size;
        return true;
    }
    if (bytes.value[files] != 'l') return false;
    u32 count = 0;
    for (u64 p = files + 1; bytes.value[p] != 'e'; p = Prog__BencodeEnd(bytes, p, 1)) ++count;
    info->files = ArenaPushArray(arena, Max(count, 1u), Prog_TorrentFile);
    for (u64 p = files + 1; bytes.value[p] != 'e'; p = Prog__BencodeEnd(bytes, p, 1)) {
        Prog_TorrentFile *file = &info->files[info->fileCount++];
        file->size = Prog__BencodeNumber(bytes, Prog__BencodeFind(bytes, p, S("length")));
        String attributes = Prog__BencodeString(bytes, Prog__BencodeFind(bytes, p, S("attr")));
        file->pad = StrIndexChar(attributes, 'p', SSF_None).exists;
        u64 path = Prog__BencodeFindText(bytes, p, S("path"), S("path.utf-8"));
        StrBuilder joined = {0};
        for (u64 q = path + 1; path && bytes.value[path] == 'l' && bytes.value[q] != 'e'; q = Prog__BencodeEnd(bytes, q, 2)) {
            if (joined.buffer.length) StrBuildStr(arena, &joined, S("/"));
            StrBuildStr(arena, &joined, Prog__BencodeString(bytes, q));
        }
        file->path = joined.buffer;
        info->size += file->size;
    }
    return info->fileCount > 0;
}

function String Prog_MagnetHash(Arena *arena, String magnet)
{
    Index at = StrIndexStr(magnet, S("btih:"), SSF_None);
    if (!at.exists) return (String){0};
    String hash = StrFrom(magnet, at.value + 5);
    hash.length = StrIndexChar(hash, '&', SSF_CountOnFail).value;
    u8 bytes[20] = {0};
    if (hash.length == 40) {
        for (u32 i = 0; i < 40; ++i) {
            u8 c = Prog__FoldAscii((u8)hash.value[i]);
            u32 digit = c >= '0' && c <= '9' ? (u32)(c - '0') : c >= 'a' && c <= 'f' ? (u32)(c - 'a' + 10) : 16;
            if (digit > 15) return (String){0};
            bytes[i / 2] |= (u8)(digit << (i % 2 ? 0 : 4));
        }
    } else if (hash.length == 32) {
        // Base32: five bits a letter.
        u64 bits = 0;
        u32 held = 0, filled = 0;
        for (u32 i = 0; i < 32; ++i) {
            u8 c = Prog__FoldAscii((u8)hash.value[i]);
            u32 value = c >= 'a' && c <= 'z' ? (u32)(c - 'a') : c >= '2' && c <= '7' ? (u32)(c - '2' + 26) : 32;
            if (value > 31) return (String){0};
            bits = (bits << 5) | value;
            held += 5;
            if (held >= 8) held -= 8, bytes[filled++] = (u8)(bits >> held);
        }
    } else {
        return (String){0};
    }
    StrBuilder text = {0};
    for (u32 i = 0; i < 20; ++i) StrBuildStr(arena, &text, StrFormat(arena, "%02x", (u32)bytes[i]));
    return text.buffer;
}

function String Prog_TorrentSelection(Arena *arena, const Prog_TorrentInfo *info, const b8 *skip)
{
    StrBuilder text = {0};
    b32 all = true;
    for (u32 i = 0; i < info->fileCount;) {
        if (skip[i] || info->files[i].pad) {
            all = false;
            ++i;
            continue;
        }
        u32 last = i;
        while (last + 1 < info->fileCount && !skip[last + 1] && !info->files[last + 1].pad) ++last;
        if (text.buffer.length) StrBuildStr(arena, &text, S(","));
        StrBuildStr(arena, &text, last > i ? StrFormat(arena, "%u-%u", i + 1, last + 1) : StrFormat(arena, "%u", i + 1));
        i = last + 1;
    }
    return all ? (String){0} : text.buffer;
}

function void Prog_TorrentSkip(String selection, const Prog_TorrentInfo *info, b8 *skip)
{
    for (u32 i = 0; i < info->fileCount; ++i) skip[i] = selection.length > 0 || info->files[i].pad;
    for (String rest = selection; rest.length;) {
        Index comma = StrIndexChar(rest, ',', SSF_CountOnFail);
        String range = StrFromTo(rest, 0, (i64)comma.value);
        rest = StrFrom(rest, Min(comma.value + 1, rest.length));
        Index dash = StrIndexChar(range, '-', SSF_None);
        u64 first = 0, last = 0;
        String from = dash.exists ? StrFromTo(range, 0, (i64)dash.value) : range, to = dash.exists ? StrFrom(range, dash.value + 1) : range;
        for (u64 i = 0; i < from.length && from.value[i] >= '0' && from.value[i] <= '9'; ++i) first = first * 10 + (u64)(from.value[i] - '0');
        for (u64 i = 0; i < to.length && to.value[i] >= '0' && to.value[i] <= '9'; ++i) last = last * 10 + (u64)(to.value[i] - '0');
        for (u64 i = first; i && i <= last && i <= info->fileCount; ++i) skip[i - 1] = false;
    }
}

// The big-endian number of `bytes` bytes at `*at` in `text`, moving past it; false where the text ends first.
function b32 Prog__BigEndian(String text, u64 *at, u32 bytes, u64 *value)
{
    if (*at + bytes > text.length) return false;
    *value = 0;
    for (u32 i = 0; i < bytes; ++i) *value = (*value << 8) | (u8)text.value[*at + i];
    *at += bytes;
    return true;
}

// Each file's bytes downloaded, by the pieces aria2c's control file `control` marks as done, the files `sizes` long one after another, as many
// as the torrent's own; false for bytes that are not such a file, or one of another torrent's length. A piece under way counts once it is whole.
function b32 Prog_TorrentControlDone(String control, const u64 *sizes, u64 count, u64 *done)
{
    // Its version, 1 for big-endian numbers, flags and the info hash; the piece length, the total and the uploaded lengths, and the bitfield.
    u64 at = 0, version = 0, flags = 0, hashLength = 0, pieceLength = 0, total = 0, uploaded = 0, bitfieldLength = 0, sum = 0;
    if (!Prog__BigEndian(control, &at, 2, &version) || version != 1 || !Prog__BigEndian(control, &at, 4, &flags) || !Prog__BigEndian(control, &at, 4, &hashLength)) return false;
    at += hashLength;
    if (!Prog__BigEndian(control, &at, 4, &pieceLength) || !Prog__BigEndian(control, &at, 8, &total) || !Prog__BigEndian(control, &at, 8, &uploaded) ||
        !Prog__BigEndian(control, &at, 4, &bitfieldLength) || at + bitfieldLength > control.length || !pieceLength) return false;
    for (u64 i = 0; i < count; ++i) sum += sizes[i];
    if (sum != total) return false;
    const u8 *bits = (const u8 *)control.value + at;
    for (u64 i = 0, start = 0; i < count; start += sizes[i], ++i) {
        u64 end = start + sizes[i];
        done[i] = 0;
        for (u64 piece = start / pieceLength; piece * pieceLength < end && piece / 8 < bitfieldLength; ++piece) {
            if (bits[piece / 8] & (0x80 >> (piece % 8))) done[i] += Min(end, (piece + 1) * pieceLength) - Max(start, piece * pieceLength);
        }
    }
    return true;
}

// A torrent's name made fit for a file's: the characters Windows forbids and line breaks as underscores, no dot or space at its end, and a
// little over 200 bytes at most, cut between characters.
function String Prog__TorrentFileName(Arena *arena, String name)
{
    u64 length = Min(name.length, (u64)200);
    while (length < name.length && ((u8)name.value[length] & 0xC0) == 0x80) --length;
    u8 *text = ArenaPushArray(arena, length + 1, u8);
    for (u64 i = 0; i < length; ++i) {
        u8 c = (u8)name.value[i];
        text[i] = c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' ? '_' : c;
    }
    while (length && (text[length - 1] == '.' || text[length - 1] == ' ')) --length;
    return length ? StrMake((const char *)text, length) : S("Torrent");
}

// The .torrent aria2c saved in `folder` for a magnet link, named by its info hash `hash`, renamed after the torrent, or the file of that name
// already there with the same bytes in its place; its path, empty without it.
function String Prog__NameFetchedTorrent(Arena *arena, String folder, String hash)
{
    String saved = StrFormat(arena, "%S.torrent", hash), savedPath = Prog_JoinPath(arena, folder, saved), bytes = {0}, there = {0};
    Prog_TorrentInfo info = {0};
    if (!hash.length || !P_ReadFile(arena, savedPath, &bytes)) return (String){0};
    if (!Prog_ReadTorrent(arena, bytes, &info)) return savedPath;
    String stem = Prog__TorrentFileName(arena, info.name), named = Prog_JoinPath(arena, folder, StrFormat(arena, "%S.torrent", stem));
    if (P_ReadFile(arena, named, &there) && StrIsEqual(there, bytes)) return !P_DeleteItems(arena, &savedPath, 1, false, NULL).code ? named : savedPath;
    String name = Prog__FreeName(arena, folder, stem, S(".torrent"));
    return !P_RenamePath(arena, folder, saved, name).code ? Prog_JoinPath(arena, folder, name) : savedPath;
}

function P_FileError Prog_FetchTorrent(Arena *arena, String source, String folder, P_Progress *progress, const P_CancelToken *cancel, ArrayStr *placed)
{
    Scratch scratch = GetScratchEx(arena);
    b32 magnet = Prog_IsMagnet(source);
    String arguments[] = { S("aria2c"), StrFormat(scratch.arena, "--dir=%S", folder), magnet ? S("--bt-metadata-only=true") : S("--follow-torrent=false"), S("--bt-save-metadata=true"),
                           S("--summary-interval=0"), S("--console-log-level=notice"), S("--enable-color=false"), S("--show-console-readout=true"), S("--"), source };
    Prog__Torrent *torrent = ArenaPushStruct(scratch.arena, Prog__Torrent);
    torrent->progress = progress;
    torrent->name = magnet ? TrMark("Magnet link") : Prog__NameStem(Prog_EntryLeaf(source));
    if (progress) {
        atomic_store(&progress->totalItems, 1);
        P_ProgressName(progress, torrent->name);
    }
    b32 done = P_RunCommandStream(folder, arguments, CArrayCount(arguments), true, Prog__TorrentOutput, torrent, cancel, progress);
    String file = {0};
    if (done && magnet) {
        file = Prog__NameFetchedTorrent(scratch.arena, folder, Prog_MagnetHash(scratch.arena, source));
    } else if (done && torrent->doneLength) {
        file = StrMake(torrent->done, torrent->doneLength);
    }
    P_FileError result = {0};
    if (file.length) {
        ArrayPush(arena, *placed, StrCopy(arena, file));
        if (progress) atomic_store(&progress->doneItems, 1);
    } else {
        String message = torrent->messageLength ? StrCopy(arena, StrMake(torrent->message, torrent->messageLength)) : TrMark("aria2c is not installed, or could not fetch the torrent");
        result = (P_FileError){ .code = 1, .message = message };
    }
    ReleaseScratch(scratch);
    return result;
}

// The sound ffmpeg decodes, as 16-bit stereo at 48 kHz, played as it comes at the volume set as it goes, and how far it has been heard.
#define Prog_PlayerRate 48000
typedef struct Prog__Player {
    P_Progress *progress;
    P_Audio *audio;
    const atomic_uint *volume;
    u64 start;    // milliseconds in
    u8 carry[4];  // a frame the last piece cut in two
    u32 carried;
} Prog__Player;

// The whole frames of what came, after the part of one the last piece left, at the volume set now, into `arena`.
function i16 *Prog__PlayerFrames(Prog__Player *player, Arena *arena, String text, u64 *frames)
{
    u64 bytes = player->carried + text.length, whole = bytes / 4;
    i16 *samples = ArenaPushArrayNoZero(arena, Max(whole * 2, (u64)1), i16);
    if (whole) {
        u8 *into = (u8 *)samples;
        MemCopy(into, player->carry, player->carried);
        MemCopy(into + player->carried, text.value, whole * 4 - player->carried);
        player->carried = (u32)(bytes - whole * 4);
        MemCopy(player->carry, text.value + text.length - player->carried, player->carried);
    } else {
        MemCopy(player->carry + player->carried, text.value, text.length);
        player->carried = (u32)bytes;
    }
    // Loudness grows as the square of the setting, which the ear hears as even steps.
    f32 level = (f32)Min(atomic_load(player->volume), 100u) / 100.0f, gain = level * level;
    for (u64 i = 0; i < whole * 2; ++i) samples[i] = (i16)((f32)samples[i] * gain);
    *frames = whole;
    return samples;
}

function void Prog__PlayerOutput(void *user, String text)
{
    Prog__Player *player = user;
    if (!player->audio) return;
    Scratch scratch = GetScratch();
    u64 frames = 0;
    i16 *samples = Prog__PlayerFrames(player, scratch.arena, text, &frames);
    u64 heard = P_AudioWrite(player->audio, samples, (u32)frames);
    if (player->progress) atomic_store(&player->progress->doneBytes, player->start + heard * 1000 / Prog_PlayerRate);
    ReleaseScratch(scratch);
}

// Plays the audio file at `path` from `start` milliseconds in, decoded by ffmpeg and played by the system's sound at `volume`, 0 to 100,
// read as it plays, keeping `progress`'s done count at the milliseconds heard; pausing `progress` pauses it, and a cancel stops it at once.
// False when ffmpeg is not there or cannot decode it, or there is no sound to play it on.
function b32 Prog_PlayAudio(Arena *arena, String path, u64 start, const atomic_uint *volume, P_Progress *progress, const P_CancelToken *cancel)
{
    Scratch scratch = GetScratchEx(arena);
    String arguments[] = { S("ffmpeg"), S("-hide_banner"), S("-loglevel"), S("error"), S("-nostdin"), S("-ss"), StrFormat(scratch.arena, "%.3f", (f64)start / 1000),
                           S("-i"), path, S("-vn"), S("-f"), S("s16le"), S("-ac"), S("2"), S("-ar"), StrFormat(scratch.arena, "%u", Prog_PlayerRate), S("-") };
    Prog__Player player = { .progress = progress, .audio = P_AudioOpen(arena, Prog_PlayerRate, 2), .volume = volume, .start = start };
    b32 result = player.audio && P_RunCommandStream(Prog_ParentPath(path), arguments, CArrayCount(arguments), false, Prog__PlayerOutput, &player, cancel, progress);
    // To its end, what is queued is heard first.
    P_AudioClose(player.audio, result && !P_IsCancelled(cancel));
    ReleaseScratch(scratch);
    return result;
}
