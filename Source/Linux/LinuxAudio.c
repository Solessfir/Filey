// Sound out through PulseAudio's simple API, which PipeWire serves too, loaded as the first sound plays: a stream that keeps about an
// eighth of a second queued, as writes to it wait for room.

typedef struct P__PulseSpec { i32 format; u32 rate; u8 channels; } P__PulseSpec;
typedef struct P__PulseBuffer { u32 maxlength, tlength, prebuf, minreq, fragsize; } P__PulseBuffer;

struct P_Audio {
    void *stream;
    u32 rate, channels;
    u64 written;  // frames
};

function void *P__PulseSymbol(const char *name)
{
    local_persist b32 tried;
    local_persist void *library;
    if (!tried) {
        tried = true;
        library = dlopen("libpulse-simple.so.0", RTLD_NOW | RTLD_LOCAL);
    }
    return library ? dlsym(library, name) : NULL;
}

function P_Audio *P_AudioOpen(Arena *arena, u32 rate, u32 channels)
{
    void *(*open)(const char *, const char *, i32, const char *, const char *, const P__PulseSpec *, const void *, const P__PulseBuffer *, i32 *);
    *(void **)&open = P__PulseSymbol("pa_simple_new");
    if (!open) return NULL;
    // 16-bit little-endian samples, PA_SAMPLE_S16LE, played back, PA_STREAM_PLAYBACK.
    P__PulseSpec spec = { .format = 3, .rate = rate, .channels = (u8)channels };
    P__PulseBuffer buffer = { .maxlength = UINT32_MAX, .tlength = rate * channels * 2 / 8, .prebuf = UINT32_MAX, .minreq = UINT32_MAX, .fragsize = UINT32_MAX };
    i32 error = 0;
    void *stream = open(NULL, "Filey", 1, NULL, "Inspector", &spec, NULL, &buffer, &error);
    if (!stream) return NULL;
    P_Audio *audio = ArenaPushStruct(arena, P_Audio);
    *audio = (P_Audio){ .stream = stream, .rate = rate, .channels = channels };
    return audio;
}

function u64 P_AudioWrite(P_Audio *audio, const i16 *samples, u32 frames)
{
    i32 (*write)(void *, const void *, u64, i32 *);
    u64 (*latency)(void *, i32 *);
    *(void **)&write = P__PulseSymbol("pa_simple_write");
    *(void **)&latency = P__PulseSymbol("pa_simple_get_latency");
    i32 error = 0;
    if (write && frames) write(audio->stream, samples, (u64)frames * audio->channels * 2, &error);
    audio->written += frames;
    u64 queued = latency ? latency(audio->stream, &error) * audio->rate / 1000000 : 0;
    return audio->written > queued ? audio->written - queued : 0;
}

function void P_AudioClose(P_Audio *audio, b32 drain)
{
    if (!audio) return;
    i32 (*finish)(void *, i32 *);
    void (*release)(void *);
    *(void **)&finish = P__PulseSymbol(drain ? "pa_simple_drain" : "pa_simple_flush");
    *(void **)&release = P__PulseSymbol("pa_simple_free");
    i32 error = 0;
    if (finish) finish(audio->stream, &error);
    if (release) release(audio->stream);
}
