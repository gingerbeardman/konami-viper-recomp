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
    Uint16 shake;
    Uint32 refreshed;
    int sent;
    int failed;
    Uint32 retry_at;
} ControllerRumble;

static inline Uint16 controller_motor_strength(uint8_t motor, double gain) {
    if (!(motor & 0x80) || !isfinite(gain) || gain <= 0) return 0;
    /* Preserve proportional cabinet torque, then apply the user's gain. */
    double force = (motor & 15) / 15.0;
    return (Uint16)lround(fmin(65535.0, force * 65535.0 * fmin(gain, 2.0)));
}

/* The game shakes the wheel over rough surfaces (cobbles, tram tracks) and impacts in two ways:
 * commands with bit 5 set (0xa0) alternating with torque, and torque that flips direction
 * (bit 4) between consecutive energized commands. The shake's level is the torque of the pair;
 * 0 when the pair is steady force. */
static inline unsigned controller_shake_level(uint8_t prev, uint8_t command) {
    unsigned level = (command & 15) > (prev & 15) ? (command & 15) : (prev & 15);
    if (!(command & 0x80)) return 0;
    if (command & 0x20) return level ? level : 8;
    if ((prev & 0x80) && (prev & 15) && (command & 15) && ((prev ^ command) & 0x10)) return level;
    return 0;
}

/* The steering torque drives the low-frequency motor; a recent shake (level 0-15) the
 * high-frequency one, so a rough surface buzzes on top of the wheel's pull. */
static void controller_rumble_update(ControllerRumble *state, SDL_GameController *pad,
                                     uint8_t motor, double gain, int active, Uint32 now, unsigned shake) {
    SDL_JoystickID id = pad ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) : -1;
    if (id != state->id) {
        *state = (ControllerRumble){ .id = id };
    }
    if (!pad) return;
    if (state->failed && (Sint32)(now - state->retry_at) < 0) return;
    Uint16 strength = active ? controller_motor_strength(motor, gain) : 0;
    Uint16 buzz = active && shake ? controller_motor_strength((uint8_t)(0x80 | (shake > 15 ? 15 : shake)), gain) : 0;
    if (buzz < strength) buzz = strength;
    /* Rate-limit changing strengths too: rapid Switch Bluetooth output can
     * disconnect the controller. Stops remain immediate; resume waits one interval. */
    if (!state->failed && !strength && !buzz && !state->strength && !state->shake) return;
    if ((strength || buzz) && state->sent && (Uint32)(now - state->refreshed) < 50) return;
#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (CONTROLLER_RUMBLE_SEND(pad, strength, buzz, (strength || buzz) ? 100 : 0) < 0) {
        if (!state->failed) SDL_Log("Controller rumble failed; retrying: %s", SDL_GetError());
        state->failed = 1;
        state->retry_at = now + 1000; /* recover transient errors without per-frame retries */
        return;
    }
#endif
    state->sent = 1;
    state->failed = 0;
    state->strength = strength;
    state->shake = buzz;
    state->refreshed = now;
}

/* Diagnostic pulse: one finite effect, without the race loop's refreshes. */
static int controller_rumble_pulse(ControllerRumble *state, SDL_GameController *pad,
                                    double gain, Uint32 now) {
    if (!pad) return -1;
    Uint16 strength = controller_motor_strength(0x8f, gain);
    if (CONTROLLER_RUMBLE_SEND(pad, strength, strength, strength ? 1000 : 0) < 0)
        return -1;
    *state = (ControllerRumble){
        .id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)),
        .strength = strength, .refreshed = now, .sent = 1
    };
    return 0;
}
