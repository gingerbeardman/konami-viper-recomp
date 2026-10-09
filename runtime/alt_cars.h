/* GTI Club 2 experiment: alternate player car models. */
#pragma once
#include "ppc_rt.h"
void alt_cars_init(const char *work);
void alt_cars_file_loaded(PPCContext *c);
void alt_cars_on_frame(uint64_t frame);
void alt_cars_car_select(PPCContext *c);
