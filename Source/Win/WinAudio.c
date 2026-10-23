// Sound out through waveOut, which the system mixes as any program's: a few short buffers that take turns, refilled as each is heard.

#pragma push_macro("function")
#pragma push_macro("global")
#undef function
#undef global
#include <mmsystem.h>
#pragma pop_macro("global")
#pragma pop_macro("function")

enum { P__AudioBuffers = 4, P__AudioBufferFrames = 1536 };

struct P_Audio {
    HWAVEOUT device;
    HANDLE done;  // set by waveOut as each buffer has been played
    WAVEHDR headers[P__AudioBuffers];
    i16 *samples[P__AudioBuffers];
    u32 channels;
    u32 next;     // the buffer being filled
    u32 filled;   // its frames so far
};

function P_Audio *P_AudioOpen(Arena *arena, u32 rate, u32 channels)
{
    P_Audio *audio = ArenaPushStruct(arena, P_Audio);
    audio->channels = channels;
    audio->done = CreateEventW(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX format = { .wFormatTag = WAVE_FORMAT_PCM, .nChannels = (WORD)channels, .nSamplesPerSec = rate, .wBitsPerSample = 16 };
    format.nBlockAlign = (WORD)(channels * 2);
    format.nAvgBytesPerSec = rate * format.nBlockAlign;
    if (!audio->done || waveOutOpen(&audio->device, WAVE_MAPPER, &format, (DWORD_PTR)audio->done, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        if (audio->done) CloseHandle(audio->done);
        return NULL;
    }
    for (u32 i = 0; i < P__AudioBuffers; ++i) {
        audio->samples[i] = ArenaPushArray(arena, P__AudioBufferFrames * channels, i16);
        audio->headers[i] = (WAVEHDR){ .lpData = (LPSTR)audio->samples[i], .dwBufferLength = P__AudioBufferFrames * channels * 2 };
        waveOutPrepareHeader(audio->device, &audio->headers[i], sizeof(WAVEHDR));
        // Each starts as heard, free to fill.
        audio->headers[i].dwFlags |= WHDR_DONE;
    }
    return audio;
}

// Queues the buffer being filled, as much of it as there is, and waits for the next to be free.
function void P__AudioQueue(P_Audio *audio)
{
    WAVEHDR *header = &audio->headers[audio->next];
    header->dwBufferLength = audio->filled * audio->channels * 2;
    header->dwFlags &= ~(DWORD)WHDR_DONE;
    waveOutWrite(audio->device, header, sizeof(WAVEHDR));
    audio->next = (audio->next + 1) % P__AudioBuffers;
    audio->filled = 0;
    while (!(audio->headers[audio->next].dwFlags & WHDR_DONE)) WaitForSingleObject(audio->done, 100);
}

function u64 P_AudioWrite(P_Audio *audio, const i16 *samples, u32 frames)
{
    while (frames) {
        u32 take = Min(frames, P__AudioBufferFrames - audio->filled);
        MemCopy(audio->samples[audio->next] + audio->filled * audio->channels, samples, take * audio->channels * sizeof(i16));
        audio->filled += take;
        samples += take * audio->channels;
        frames -= take;
        if (audio->filled == P__AudioBufferFrames) P__AudioQueue(audio);
    }
    MMTIME played = { .wType = TIME_SAMPLES };
    waveOutGetPosition(audio->device, &played, sizeof(played));
    return played.wType == TIME_SAMPLES ? played.u.sample : 0;
}

function void P_AudioClose(P_Audio *audio, b32 drain)
{
    if (!audio) return;
    if (drain && audio->filled) P__AudioQueue(audio);
    if (!drain) waveOutReset(audio->device);
    for (u32 i = 0; i < P__AudioBuffers; ++i) {
        while (!(audio->headers[i].dwFlags & WHDR_DONE)) WaitForSingleObject(audio->done, 100);
        waveOutUnprepareHeader(audio->device, &audio->headers[i], sizeof(WAVEHDR));
    }
    waveOutClose(audio->device);
    CloseHandle(audio->done);
}
