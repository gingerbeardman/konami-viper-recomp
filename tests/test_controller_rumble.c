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
    /* Default gain preserves proportional torque, without boosting weak forces. */
    assert(controller_motor_strength(0x83,.5) == 6554);
    assert(controller_motor_strength(0x85,.5) == 10923);
    Uint16 previous = 0;
    for (int torque=1; torque<=15; torque++) {
        Uint16 strength = controller_motor_strength((uint8_t)(0x80 | torque),.5);
        assert(strength > previous);
        assert(abs((int)controller_motor_strength((uint8_t)(0x80 | torque),1) - 2*(int)strength) <= 1);
        previous = strength;
    }
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
    controller_rumble_update(&state,pad,0x8f,1,1,100,0);
    assert(calls == 1 && last_low == 65535 && last_high == 65535 && last_duration == 100);
    controller_rumble_update(&state,pad,0x8f,1,1,120,0); assert(calls == 1);
    controller_rumble_update(&state,pad,0x8f,1,0,121,0);
    assert(calls == 2 && last_low == 0 && last_high == 0);
    controller_rumble_update(&state,pad,0x8f,1,0,180,0); assert(calls == 2);
    controller_rumble_update(&state,pad,0x81,.5,1,200,0);
    assert(last_low > 0);
    controller_rumble_update(&state,pad,0x01,.5,1,201,0);
    assert(last_low == 0);
    controller_rumble_update(&state,NULL,0x8f,1,1,210,0); assert(state.id == -1);
    fail = 1;
    controller_rumble_update(&state,pad,0x8f,1,1,300,0);
    assert(state.failed);
    int count = calls;
    controller_rumble_update(&state,pad,0x8f,1,1,400,0); assert(calls == count);
    fail = 0;
    controller_rumble_update(&state,pad,0x8f,1,1,1299,0);assert(calls == count);
    controller_rumble_update(&state,pad,0x8f,1,1,1300,0);
    assert(calls == count+1 && !state.failed && last_low==65535);
    /* A failed pause stop must retry even though the desired strength is zero. */
    fail=1;controller_rumble_update(&state,pad,0x8f,1,0,1400,0);assert(state.failed);
    fail=0;controller_rumble_update(&state,pad,0x8f,1,0,2400,0);
    assert(!state.failed && last_low==0);
    controller_rumble_update(&state,pad,0x8f,.25,1,2401,0);assert(last_low==0);
    controller_rumble_update(&state,pad,0x8f,.25,1,2450,0);assert(last_low==16384);
    controller_rumble_update(&state,pad,0x8f,.5,1,2500,0);assert(last_low==32768);
    controller_rumble_update(&state,pad,0x8f,1,1,2550,0);assert(last_low==65535);
    controller_rumble_update(&state,pad,0x8f,0,1,2551,0);assert(last_low==0);
    state = (ControllerRumble){ .id = -1 };
    controller_rumble_update(&state,pad,0x83,.5,1,5000,0);count=calls;
    controller_rumble_update(&state,pad,0x84,.5,1,5010,0);assert(calls==count);
    controller_rumble_update(&state,pad,0x85,.5,1,5049,0);assert(calls==count);
    controller_rumble_update(&state,pad,0x85,.5,1,5050,0);assert(calls==count+1);
    controller_rumble_update(&state,pad,0x85,.5,0,5051,0);assert(calls==count+2 && last_low==0);
    controller_rumble_update(&state,pad,0x86,.5,1,5052,0);assert(calls==count+2);
    controller_rumble_update(&state,pad,0x86,.5,1,5101,0);assert(calls==count+3 && last_low>0);
    state = (ControllerRumble){ .id = -1 };
    controller_rumble_update(&state,pad,0x8f,1,1,UINT32_MAX - 20,0);
    count = calls;
    controller_rumble_update(&state,pad,0x8f,1,1,40,0);
    assert(calls == count + 1 && last_duration == 100); /* tick wrap and keepalive */
    /* Repeated isolated tests send one full-duration command at equal strength. */
    for (int pulse=0; pulse<5; ++pulse) {
        Uint32 now=6000 + pulse*2000;
        count=calls;
        assert(controller_rumble_pulse(&state,pad,.5,now)==0);
        assert(calls==count+1 && last_low==32768 && last_high==32768 && last_duration==1000);
        controller_rumble_update(&state,pad,0,0,0,now+1000,0);
        assert(last_low==0);
    }
    assert(controller_rumble_pulse(&state,pad,0,16000)==0 && last_low==0);
    fail=1;
    assert(controller_rumble_pulse(&state,pad,.5,17000)<0);
    fail=0;
    /* the game's shakes: bit 5 alternating with torque, and direction flips */
    assert(controller_shake_level(0x89,0xa0) == 9);
    assert(controller_shake_level(0xa0,0x89) == 0);         /* the torque half of the pair */
    assert(controller_shake_level(0x80,0xa0) == 8);         /* no torque yet: a mid shake */
    assert(controller_shake_level(0x8b,0x9b) == 11 && controller_shake_level(0x9b,0x8b) == 11);
    assert(controller_shake_level(0x83,0x84) == 0);         /* steady pull is force, not shake */
    assert(controller_shake_level(0x93,0x83) == 3);
    assert(controller_shake_level(0x93,0x80) == 0 && controller_shake_level(0x93,0x03) == 0);
    /* a shake drives the high-frequency motor above the steering torque */
    state = (ControllerRumble){ .id = -1 };
    controller_rumble_update(&state,pad,0x83,1,1,20000,9);
    assert(last_low == controller_motor_strength(0x83,1) && last_high == controller_motor_strength(0x89,1));
    controller_rumble_update(&state,pad,0x80,1,1,20060,9);
    assert(last_low == 0 && last_high == controller_motor_strength(0x89,1));
    controller_rumble_update(&state,pad,0x80,1,1,20120,0);
    assert(last_low == 0 && last_high == 0);
    controller_rumble_update(&state,pad,0x8f,1,1,20180,3);
    assert(last_high == 65535);                              /* never below the torque */
    controller_rumble_update(&state,pad,0x80,1,0,20240,9);
    assert(last_low == 0 && last_high == 0);                 /* paused: silent */
    SDL_GameControllerClose(pad);
    SDL_JoystickDetachVirtual(index);
    SDL_Quit();
    puts("cabinet torque and SDL virtual rumble: passed");
}
