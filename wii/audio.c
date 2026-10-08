/* Wii audio output. The game mixes its own sound: every 256 samples (44.1 kHz)
 * the sound IRQ hands over a RAM block of 256 stereo frames, two big-endian
 * signed 32-bit words each (runtime/hw.c sound_tick). The guest thread
 * converts each block to 16-bit with the desktop build's gain and queues it;
 * the audio DMA interrupt resamples the queue to the Wii's 48 kHz and starts
 * the next buffer. Integer only: the DMA callback runs in interrupt context.
 * Underrun repeats the last frame (silence once the game stops); a backlog of
 * more than ~100 ms is skipped, like the desktop frontend. */
#include <gccore.h>
#include <stdint.h>
#include <string.h>
#include "audio.h"

#ifndef VIPER_WII_AUDIO_GAIN
#define VIPER_WII_AUDIO_GAIN 16        /* runtime/frontend_sdl.c g_audio_gain */
#endif
enum {
    RING = 8192,                       /* 44.1 kHz frames, power of two */
    DMA_FRAMES = 512,                  /* 48 kHz frames per DMA buffer (~10.7 ms) */
    BACKLOG_MAX = 44100 / 10,
    BACKLOG_KEEP = 44100 / 25,
    STEP = (int)(65536.0 * 44100.0 / 48000.0 + 0.5)   /* 16.16 source frames per output frame */
};
static int16_t ring[RING][2];
static volatile unsigned ring_w, ring_r;
static int16_t dma[2][DMA_FRAMES][2] ATTRIBUTE_ALIGN(32);
static unsigned dma_next, phase;
static int16_t last[2];
static int started;
volatile unsigned wii_audio_underruns, wii_audio_skips;

static inline int16_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }

void wii_audio_push_block(const uint8_t *blk) {
    if (!started) return;
    unsigned w = ring_w, r = ring_r;
    for (int i = 0; i < 256; i++) {
        if (w - r >= RING - 1) break;   /* full: drop */
        const uint8_t *p = blk + 8 * i;
        int32_t l = (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
        int32_t rr = (int32_t)(((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7]);
        ring[w % RING][0] = sat16((int32_t)(((int64_t)l * VIPER_WII_AUDIO_GAIN) >> 16));
        ring[w % RING][1] = sat16((int32_t)(((int64_t)rr * VIPER_WII_AUDIO_GAIN) >> 16));
        w++;
    }
    ring_w = w;
}

/* Fill one 48 kHz buffer by linear interpolation from the 44.1 kHz ring. */
static void fill(int16_t (*out)[2]) {
    unsigned r = ring_r, w = ring_w;
    if (w - r > BACKLOG_MAX) { r = w - BACKLOG_KEEP; wii_audio_skips++; }
    for (int i = 0; i < DMA_FRAMES; i++) {
        if (w - r >= 2) {
            int32_t f = (int32_t)(phase >> 1);   /* 15 bits: the product fits 32 bits */
            const int16_t *a = ring[r % RING], *b = ring[(r + 1) % RING];
            last[0] = (int16_t)(a[0] + (((b[0] - a[0]) * f) >> 15));
            last[1] = (int16_t)(a[1] + (((b[1] - a[1]) * f) >> 15));
            phase += STEP;
            r += phase >> 16;
            phase &= 0xffff;
        } else if (i == 0) {
            wii_audio_underruns++;
        }
        out[i][0] = last[0];
        out[i][1] = last[1];
    }
    ring_r = r;
    DCFlushRange(out, DMA_FRAMES * 4);
}

/* Called as a DMA buffer starts playing: the other one has finished, so refill
 * it and queue it to follow. */
static void dma_done(void) {
    int16_t (*buf)[2] = dma[dma_next];
    fill(buf);
    AUDIO_InitDMA((u32)buf, DMA_FRAMES * 4);
    dma_next ^= 1;
}

void wii_audio_init(void) {
    AUDIO_Init(NULL);
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    memset(dma, 0, sizeof dma);
    DCFlushRange(dma, sizeof dma);
    dma_next = 1;
    AUDIO_RegisterDMACallback(dma_done);
    AUDIO_InitDMA((u32)dma[0], DMA_FRAMES * 4);
    started = 1;
    AUDIO_StartDMA();
}

void wii_audio_shutdown(void) {
    if (!started) return;
    started = 0;
    AUDIO_StopDMA();
    AUDIO_RegisterDMACallback(NULL);
}
