#include <SDL.h>
static int test_rumble(SDL_GameController *, Uint16, Uint16, Uint32);
#define SDL_GameControllerRumble test_rumble
#include "controller_rumble.h"
#undef SDL_GameControllerRumble
#include <assert.h>
#include <stdio.h>
static int calls, fail;
static Uint32 last_duration;
static Uint16 last_low, last_high;
static int test_rumble(SDL_GameController *pad, Uint16 low, Uint16 high, Uint32 duration) {
    (void)pad; calls++; last_low = low; last_high = high; last_duration = duration; return fail ? -1 : 0;
}
int main(void) {
    assert(controller_motor_strength(0x0f,1) == 0);
    assert(controller_motor_strength(0x80,1) == 0);
    assert(controller_motor_strength(0x8f,1) == 65535);
    assert(controller_motor_strength(0x9f,1) == 65535); /* direction has no vibration equivalent */
    assert(controller_motor_strength(0x8f,.5) == 32768);
    assert(controller_motor_strength(0x8f,0) == 0);
    assert(controller_motor_strength(0x84,2) > controller_motor_strength(0x84,1));
    assert(controller_motor_strength(0x8f,2) == 65535);
    assert(controller_motor_strength(0x8f,100) == 65535);
    assert(controller_motor_strength(0x8f,NAN) == 0);
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    SDL_VirtualJoystickDesc desc = {0};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    int index = SDL_JoystickAttachVirtualEx(&desc);
    assert(index >= 0);
    SDL_GameController *pad = SDL_GameControllerOpen(index);
    assert(pad);
    ControllerRumble state = { .id = -1 };
    controller_rumble_update(&state,pad,0x8f,1,1,100);
    assert(calls == 1 && last_low == 65535 && last_high == 65535 && last_duration == 100);
    controller_rumble_update(&state,pad,0x8f,1,1,120); assert(calls == 1);
    controller_rumble_update(&state,pad,0x8f,1,0,121);
    assert(calls == 2 && last_low == 0 && last_high == 0);
    controller_rumble_update(&state,pad,0x8f,1,0,180); assert(calls == 2);
    controller_rumble_update(&state,pad,0x81,.5,1,200);
    assert(last_low > 0);
    controller_rumble_update(&state,pad,0x01,.5,1,201);
    assert(last_low == 0);
    controller_rumble_update(&state,NULL,0x8f,1,1,210); assert(state.id == -1);
    fail = 1;
    controller_rumble_update(&state,pad,0x8f,1,1,300);
    assert(state.unsupported);
    int count = calls;
    controller_rumble_update(&state,pad,0x8f,1,1,400); assert(calls == count);
    fail = 0;
    state = (ControllerRumble){ .id = -1 };
    controller_rumble_update(&state,pad,0x8f,1,1,UINT32_MAX - 20);
    count = calls;
    controller_rumble_update(&state,pad,0x8f,1,1,40);
    assert(calls == count + 1 && last_duration == 100); /* tick wrap and keepalive */
    SDL_GameControllerClose(pad);
    SDL_JoystickDetachVirtual(index);
    SDL_Quit();
    puts("cabinet torque and SDL virtual rumble: passed");
}
