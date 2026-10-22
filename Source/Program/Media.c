// Audio and video through the FFmpeg tools found on the PATH: the details ffprobe reads of a file, an audio file's waveform and a video's frame,
// and videos compressed, on the graphics card when it has an encoder.

function b32 Prog__HasExtension(String path, const String *extensions, u32 count)
{
    Index dot = StrIndexChar(path, '.', SSF_SearchBackwards);
    if (!dot.exists) return false;
    String extension = StrFrom(path, dot.value + 1);
    for (u32 i = 0; i < count; ++i) if (Prog__EqualFolded(extension, extensions[i])) return true;
    return false;
}

function b32 Prog_IsAudio(String path)
{
    const String extensions[] = { S("mp3"), S("flac"), S("wav"), S("ogg"), S("oga"), S("opus"), S("m4a"), S("aac"), S("wma"), S("aif"), S("aiff"), S("ape"),
                                  S("wv"), S("mka"), S("ac3"), S("dts") };
    return Prog__HasExtension(path, extensions, CArrayCount(extensions));
}

function b32 Prog_IsVideo(String path)
{
    const String extensions[] = { S("mp4"), S("mkv"), S("mov"), S("avi"), S("webm"), S("wmv"), S("flv"), S("m4v"), S("mpg"), S("mpeg"), S("ts"), S("m2ts"),
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
function String Prog_MediaDetails(Arena *arena, String path, f64 *seconds)
{
    *seconds = 0;
    Scratch scratch = GetScratchEx(arena);
    String arguments[] = { S("ffprobe"), S("-v"), S("error"), S("-show_entries"),
                           S("format=duration,bit_rate:stream=codec_type,codec_name,width,height,avg_frame_rate,sample_rate,channels"), S("-of"), S("compact=p=0"), path };
    String output = {0};
    StrBuilder text = {0};
    if (P_RunCommand(scratch.arena, Prog_ParentPath(path), arguments, CArrayCount(arguments), 10000, &output, false)) {
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
function b32 Prog__MediaPicture(Arena *arena, String path, String kind, String before, String filter, i32 side, P_Image *image, i32 *width, i32 *height)
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
        arguments[count++] = S("-frames:v"), arguments[count++] = S("1"), arguments[count++] = picture;
        String output = {0};
        b32 drawn = P_PathExists(picture) || P_RunCommand(scratch.arena, folder, arguments, count, 60000, &output, true);
        result = drawn && P_LoadImageFile(arena, picture, side, image, width, height);
    }
    ReleaseScratch(scratch);
    return result;
}

// An audio file's waveform, both channels in one, its louder parts drawn taller on a clear ground.
function b32 Prog_MediaWaveform(Arena *arena, String path, i32 side, P_Image *image, i32 *width, i32 *height)
{
    return Prog__MediaPicture(arena, path, S("wave"), (String){0}, S("showwavespic=s=1600x400:colors=0x4FA3E0:split_channels=0:scale=sqrt"), side, image, width, height);
}

// A video's frame a tenth of the way in, or its first in one too short to tell, for one the system has no picture of.
function b32 Prog_MediaFrame(Arena *arena, String path, f64 duration, i32 side, P_Image *image, i32 *width, i32 *height)
{
    Scratch scratch = GetScratchEx(arena);
    String at = StrFormat(scratch.arena, "%.2f", duration * 0.1);
    b32 result = Prog__MediaPicture(arena, path, S("frame"), at, S("scale='min(1600,iw)':-2"), side, image, width, height);
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
// encoders whether or not such a card is in the machine. Found once a run, on whichever thread asks first.
function Prog_Encoder Prog_VideoEncoder(u32 codec)
{
    local_persist atomic_uint found[3];
    codec = Min(codec, 2u);
    u32 known = atomic_load(&found[codec]);
    if (known) return (Prog_Encoder)(known - 1);
    Prog_Encoder result = Prog_Encoder_Software;
    Scratch scratch = GetScratch();
    for (u32 e = 0; e < Prog_Encoder_Software; ++e) {
        String arguments[] = { S("ffmpeg"), S("-v"), S("error"), S("-f"), S("lavfi"), S("-i"), S("color=s=256x256:d=0.1"), S("-frames:v"), S("1"), S("-c:v"),
                               StrFromCStr(Prog_EncoderNames[codec][e]), S("-f"), S("null"), S("-") };
        String output = {0};
        if (P_RunCommand(scratch.arena, P_RunFolder(scratch.arena), arguments, CArrayCount(arguments), 20000, &output, true)) {
            result = (Prog_Encoder)e;
            break;
        }
    }
    ReleaseScratch(scratch);
    atomic_store(&found[codec], (u32)result + 1);
    return result;
}

// The first audio track's codec of the media file at `path`, empty for none.
function String Prog__MediaAudioCodec(Arena *arena, String path)
{
    String arguments[] = { S("ffprobe"), S("-v"), S("error"), S("-select_streams"), S("a:0"), S("-show_entries"), S("stream=codec_name"), S("-of"), S("csv=p=0"), path };
    String output = {0};
    if (!P_RunCommand(arena, Prog_ParentPath(path), arguments, CArrayCount(arguments), 10000, &output, false)) return (String){0};
    while (output.length && (output.value[output.length - 1] == '\n' || output.value[output.length - 1] == '\r')) --output.length;
    return output;
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

// Encodes the video at `path` again into the MP4 file `output` with `codec` (HEVC, H.264 or AV1) at the constant quality `quality`, 0 best
// to 51, on the graphics card when one has an encoder for it. Every audio track goes along, copied when MP4 holds it as it is and AAC
// otherwise; subtitles stay behind, as MP4 holds few kinds. `progress` counts the video's milliseconds after `base`; a cancel or a failure
// leaves no output.
function P_FileError Prog_CompressVideo(Arena *arena, String path, String output, u32 codec, u32 quality, u64 base, P_Progress *progress, const P_CancelToken *cancel)
{
    Scratch scratch = GetScratchEx(arena);
    codec = Min(codec, 2u);
    Prog_Encoder encoder = Prog_VideoEncoder(codec);
    String audio = Prog__MediaAudioCodec(scratch.arena, path);
    const String copied[] = { S("aac"), S("mp3"), S("opus"), S("ac3"), S("eac3"), S("flac"), S("alac") };
    b32 copy = false;
    for (u32 i = 0; i < CArrayCount(copied); ++i) copy |= StrIsEqual(audio, copied[i]);
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
    ReleaseScratch(scratch);
    return result;
}

// The length of the media file at `path` in milliseconds, 0 when ffprobe does not tell.
function u64 Prog_MediaLength(Arena *arena, String path)
{
    String arguments[] = { S("ffprobe"), S("-v"), S("error"), S("-show_entries"), S("format=duration"), S("-of"), S("csv=p=0"), path };
    String output = {0};
    Scratch scratch = GetScratchEx(arena);
    u64 result = P_RunCommand(scratch.arena, Prog_ParentPath(path), arguments, CArrayCount(arguments), 10000, &output, false) ? (u64)(Prog__MediaNumber(output) * 1000) : 0;
    ReleaseScratch(scratch);
    return result;
}
