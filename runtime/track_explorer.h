/* Private GTI Club 2 JAB experiment. No persisted settings or ROM modifications. */
#pragma once
#include "ppc_rt.h"
void explorer_camera(PPCContext *c, uint64_t frame);
void explorer_race(PPCContext *c);
void explorer_toggle(void);
int explorer_active(void);
float explorer_speed(void);  /* metres per second */
float explorer_height(void); /* selected clearance in metres */
void explorer_adjust(float speed, float height);
void explorer_look(float steering); /* -1..1 maps to -90..90 degrees */
void explorer_on_frame(uint64_t frame);
