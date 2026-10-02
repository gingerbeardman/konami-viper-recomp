/* Private GTI Club 2 JAB experiment. No persisted settings or ROM modifications. */
#pragma once
#include "ppc_rt.h"
void explorer_camera(PPCContext *c, uint64_t frame);
void explorer_race(PPCContext *c);
void explorer_toggle(void);
int explorer_active(void);
void explorer_adjust(int speed, int height);
void explorer_on_frame(uint64_t frame);
