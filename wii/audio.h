#pragma once
#include <stdint.h>
/* See audio.c: the game's mixed sound blocks out through the Wii audio DMA. */
void wii_audio_init(void);
void wii_audio_push_block(const uint8_t *blk);
void wii_audio_shutdown(void);
extern volatile unsigned wii_audio_underruns, wii_audio_skips;
