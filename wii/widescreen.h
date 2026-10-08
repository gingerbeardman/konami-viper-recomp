#pragma once
#include <stdint.h>
/* See widescreen.c: Hor+ widescreen for the MULTI build. */
extern int wii_wide_margin;            /* M: game pixels added each side (0: off) */
void wii_wide_set(int margin);         /* guest thread, frame boundary */
int wii_wide_hook(uint32_t pc);        /* rt_hook: 1 when pc was a widescreen hook */
