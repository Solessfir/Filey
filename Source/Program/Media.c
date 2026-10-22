// Audio and video through the FFmpeg tools found on the PATH: the details ffprobe reads of a file, an audio file's waveform and a video's frame.

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
