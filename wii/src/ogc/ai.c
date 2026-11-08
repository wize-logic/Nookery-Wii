/* The SDK's AI (the audio interface's DMA from memory to the DAC) on
 * libogc's AUDIO: the same hardware. The game's mixer (jaudio's rspsim, on
 * the CPU: the port runs no DSP microcode, as the PC port doesn't) hands it
 * 32 kHz stereo frames, and refills from the DMA's interrupt.
 *
 * libogc's headers only here: the SDK's types are spelled out (AIDCallback
 * is void (*)(void), AISCallback void (*)(u32)). */

#include "nk_wii.h"

#include <gccore.h>
#include <string.h>

typedef void (*SdkAIDCallback)(void);
typedef void (*SdkAISCallback)(u32 count);

static int s_init;
static SdkAIDCallback s_dma_callback;
static SdkAISCallback s_stream_callback;
static u32 s_dma_addr;
static u32 s_dma_length;
static u32 s_dsp_rate = AI_SAMPLERATE_32KHZ; /* (the SDK numbers them as libogc) */
static u32 s_stream_rate;
static u32 s_stream_trigger;
static u32 s_stream_state;
static u8 s_vol_left = 0xFF, s_vol_right = 0xFF;

/* The NES emulator's samples (nk_wii_ai_mix_samples), 32 kHz stereo frames:
 * the PC queues them to its audio device beside the game's mixer's; here
 * they are added into the mixer's next buffers as the AI is given them. A
 * ring of frames, written by the game's thread with interrupts disabled and
 * read here from the DMA's interrupt. After running dry it fills again to
 * MIX_PRIME before playing, so a late NES frame is one gap, not many. */
#define MIX_FRAMES 8192 /* (a power of two: 256 ms) */
#define MIX_PRIME 1600  /* (50 ms: three NES frames) */
static s16 s_mix[MIX_FRAMES * 2];
static volatile u32 s_mix_head; /* frames written */
static volatile u32 s_mix_tail; /* frames played */
static int s_mix_primed;

void nk_wii_ai_mix_samples(const short* frames, unsigned int bytes)
{
    u32 n = bytes / 4;
    if (frames == NULL || n == 0) {
        return;
    }
    if (n > MIX_FRAMES) {
        frames += (n - MIX_FRAMES) * 2;
        n = MIX_FRAMES;
    }
    u32 level = IRQ_Disable();
    u32 head = s_mix_head;
    if (head - s_mix_tail + n > MIX_FRAMES) {
        s_mix_tail = head + n - MIX_FRAMES; /* full: the oldest go */
    }
    for (u32 i = 0; i < n; i++) {
        u32 at = ((head + i) & (MIX_FRAMES - 1)) * 2;
        s_mix[at] = frames[i * 2];
        s_mix[at + 1] = frames[i * 2 + 1];
    }
    s_mix_head = head + n;
    IRQ_Restore(level);
}

unsigned int nk_wii_ai_mix_queued(void)
{
    return s_mix_head - s_mix_tail;
}

static s16 clamp16(s32 v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : (s16) v;
}

/* With interrupts disabled (the DMA's interrupt, or AIInitDMA's callers) */
static void mix_into(u32 start_addr, u32 length)
{
    u32 avail = s_mix_head - s_mix_tail;
    if (avail == 0) {
        s_mix_primed = 0;
        return;
    }
    if (!s_mix_primed) {
        if (avail < MIX_PRIME) {
            return;
        }
        s_mix_primed = 1;
    }
    s16* dst = (s16*) start_addr;
    u32 n = length / 4;
    if (n > avail) {
        n = avail;
    }
    u32 tail = s_mix_tail;
    for (u32 i = 0; i < n; i++) {
        u32 at = ((tail + i) & (MIX_FRAMES - 1)) * 2;
        dst[i * 2] = clamp16(dst[i * 2] + s_mix[at]);
        dst[i * 2 + 1] = clamp16(dst[i * 2 + 1] + s_mix[at + 1]);
    }
    s_mix_tail = tail + n;
    DCFlushRange(dst, length);
}

static void dma_done(void)
{
    if (s_dma_callback != NULL) {
        s_dma_callback();
    }
}

void AIInit(u8* stack)
{
    (void) stack;
    if (s_init) {
        return;
    }
    s_init = 1;
    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_32KHZ);
    AUDIO_RegisterDMACallback(dma_done);
}

void AIReset(void)
{
    AUDIO_StopDMA();
}

BOOL AICheckInit(void)
{
    return s_init;
}

SdkAIDCallback AIRegisterDMACallback(SdkAIDCallback callback)
{
    u32 level = IRQ_Disable();
    SdkAIDCallback old = s_dma_callback;
    s_dma_callback = callback;
    IRQ_Restore(level);
    return old;
}

void AIInitDMA(u32 start_addr, u32 length)
{
    u32 level = IRQ_Disable();
    s_dma_addr = start_addr;
    s_dma_length = length;
    if (start_addr != 0 && length != 0) {
        mix_into(start_addr, length);
    }
    AUDIO_InitDMA(start_addr, length);
    IRQ_Restore(level);
}

BOOL AIGetDMAEnableFlag(void)
{
    return (*(volatile u16*) 0xCC005036 & 0x8000) != 0;
}

void AIStartDMA(void)
{
    AUDIO_StartDMA();
}

void AIStopDMA(void)
{
    AUDIO_StopDMA();
}

u32 AIGetDMABytesLeft(void)
{
    return AUDIO_GetDMABytesLeft();
}

u32 AIGetDMAStartAddr(void)
{
    return s_dma_addr;
}

u32 AIGetDMALength(void)
{
    return s_dma_length;
}

/* The drive's streaming audio (DVD ADPCM through the AI): none in Wii mode */
SdkAISCallback AIRegisterStreamCallback(SdkAISCallback callback)
{
    SdkAISCallback old = s_stream_callback;
    s_stream_callback = callback;
    return old;
}

u32 AIGetStreamSampleCount(void) { return 0; }
void AIResetStreamSampleCount(void) {}
void AISetStreamTrigger(u32 trigger) { s_stream_trigger = trigger; }
u32 AIGetStreamTrigger(void) { return s_stream_trigger; }
void AISetStreamPlayState(u32 state) { s_stream_state = state; }
u32 AIGetStreamPlayState(void) { return s_stream_state; }
void AISetDSPSampleRate(u32 rate)
{
    s_dsp_rate = rate;
    AUDIO_SetDSPSampleRate(rate);
}
u32 AIGetDSPSampleRate(void) { return s_dsp_rate; }
void AISetStreamSampleRate(u32 rate) { s_stream_rate = rate; }
u32 AIGetStreamSampleRate(void) { return s_stream_rate; }
void AISetStreamVolLeft(u8 vol) { s_vol_left = vol; }
u8 AIGetStreamVolLeft(void) { return s_vol_left; }
void AISetStreamVolRight(u8 vol) { s_vol_right = vol; }
u8 AIGetStreamVolRight(void) { return s_vol_right; }
