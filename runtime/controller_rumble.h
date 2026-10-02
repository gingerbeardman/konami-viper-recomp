/* SDL rumble adapter for the cabinet's K-type steering motor. Host thread only. */
#pragma once
#include <SDL.h>
#include <math.h>
#include <stdint.h>

#ifndef CONTROLLER_RUMBLE_SEND
#define CONTROLLER_RUMBLE_SEND SDL_GameControllerRumble
#endif

typedef struct {
    SDL_JoystickID id;
    Uint16 strength;
    Uint32 refreshed;
    int unsupported;
} ControllerRumble;

static inline Uint16 controller_motor_strength(uint8_t motor, double gain) {
    if (!(motor & 0x80) || !isfinite(gain) || gain <= 0) return 0;
    return (Uint16)lround((motor & 15) * (65535.0 / 15.0) * fmin(gain, 1.0));
}

static void controller_rumble_update(ControllerRumble *state, SDL_GameController *pad,
                                     uint8_t motor, double gain, int active, Uint32 now) {
    SDL_JoystickID id = pad ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) : -1;
    if (id != state->id) {
        *state = (ControllerRumble){ .id = id };
    }
    if (!pad || state->unsupported) return;
    Uint16 strength = active ? controller_motor_strength(motor, gain) : 0;
    /* Effects expire even if the window stalls. Refresh at 20 Hz, stop immediately. */
    if (strength == state->strength && (!strength || (Uint32)(now - state->refreshed) < 50)) return;
#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (CONTROLLER_RUMBLE_SEND(pad, strength, strength, strength ? 100 : 0) < 0) {
        SDL_Log("Controller rumble failed: %s", SDL_GetError());
        state->unsupported = 1;  /* no per-frame retries on unsupported devices */
        return;
    }
#endif
    state->strength = strength;
    state->refreshed = now;
}
