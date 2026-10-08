/* Exercise the actual frontend input path with SDL virtual controllers, no ROMs/window. */
#include <SDL.h>
static int emulate_switch_type;
static SDL_GameControllerType controller_type(SDL_GameController *pad) {
    return emulate_switch_type ? SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO : SDL_GameControllerGetType(pad);
}
#define SDL_GameControllerGetType controller_type
#include "../runtime/frontend_sdl.c"
#include <assert.h>
#include <stdio.h>
uint8_t g_in[8];
int16_t g_analog[4];
int g_enhanced;
int explorer_active(void) { return 0; }
int explorer_free(void) { return 0; }
void explorer_drive(float forward, float vertical) { (void)forward; (void)vertical; }
void explorer_adjust(float speed, float height) { (void)speed; (void)height; }
void explorer_look(float steering) { (void)steering; }
void explorer_pitch(float tilt) { (void)tilt; }
static int menu_active, paused;
uint64_t rt_now(void) { return 0; }
void rt_log(const char *fmt, ...) { (void)fmt; }
void nvram_save(void) {}
int enh_turbo(void) { return 0; }
int enh_paused(void) { return paused; }
int enh_start_held(void) { return 0; }
static int enhanced_owns_inputs;
int enh_inputs_owned(void) { return enhanced_owns_inputs; }
int enh_menu_active(void) { return menu_active; }
int enh_name_entry_active(void) { return 0; }
static int menu_calls, last_menu_action;
void enh_menu_action(int a) { menu_calls++; last_menu_action=a; }
int enh_name_type(int a) { return a; }
void enh_name_step(int a) { (void)a; }
static int pause_calls;
int enh_escape(void) { pause_calls++; paused = !paused; return 1; }
void enh_focus_lost(void) { if (!paused && !menu_active) enh_escape(); }
int enh_texture_filter(void) { return 0; }
int enh_want_fullscreen(void) { return 0; }
void enh_set_fullscreen(int a) { (void)a; }
void enh_controller_settings_changed(void) {}
int enh_quit_requested(void) { return 0; }
int enh_restart_requested(void) { return 0; }
void enh_track_debug_toggle(void) {}
int enh_track_debug_position_text(char *text,size_t size) {(void)text;(void)size;return 0;}
int enh_track_debug_heading_text(char *text,size_t size) {(void)text;(void)size;return 0;}
int enh_gate_editor_mouse(float x,float y) {(void)x;(void)y;return 0;}
int enh_gate_editor_active(void) {return 0;}
int enh_gate_editor_button(int a) {(void)a;return 0;}
void enh_gate_editor_axes(float a,float b,float c,float d,float e,float f,float t) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)t;}
int enh_mission_retry(void) {return 0;}
int enh_track_debug_step(int direction) {(void)direction;return 0;}
void enh_draw_overlay(uint32_t *f, int w, int h) { (void)f; (void)w; (void)h; }
uint64_t voodoo_get_frame(uint32_t *f, int n, int *w, int *h) { (void)f; (void)n; *w=*h=0; return 0; }

static void axis(SDL_GameControllerAxis a, Sint16 value) {
    assert(SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(g_pad), a, value) == 0);
    SDL_JoystickUpdate();
}

static void button(SDL_GameControllerButton b, int down) {
    assert(SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(g_pad), b, down) == 0);
    SDL_JoystickUpdate();
}

int main(void) {
    g_analog[3] = -200;
    /* Polling inactive flags pauses even without a focus-loss event. */
    menu_active=0; paused=0; g_input_focus=1; ctl.gas=SRC_KEY;
    window_focus(SDL_WINDOW_SHOWN);
    assert(paused && pause_calls==1 && !g_input_focus && !ctl.gas);
    window_focus(SDL_WINDOW_SHOWN); assert(paused && pause_calls==1);
    window_focus(SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS);
    assert(paused && g_input_focus && pause_calls==1); /* never resume on focus gain */
    paused=0; window_focus(SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MINIMIZED);
    assert(paused && !g_input_focus && pause_calls==2);
    paused=0; menu_active=1; window_focus(SDL_WINDOW_SHOWN);
    assert(!paused && pause_calls==2); /* menus remain interactive */
    paused=0; pause_calls=0;
    menu_active=1; g_input_focus=1;
    menu_pad_press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 1000);
    assert(menu_calls==1 && last_menu_action==ENH_RIGHT);
    menu_pad_repeat(1399);assert(menu_calls==1);
    menu_pad_repeat(1400);assert(menu_calls==2);
    menu_pad_repeat(1479);assert(menu_calls==2);
    menu_pad_repeat(1480);assert(menu_calls==3);
    g_input_focus=0;menu_pad_repeat(2000);assert(menu_calls==3 && g_menu_repeat_button==-1);
    g_input_focus=1;menu_pad_press(SDL_CONTROLLER_BUTTON_A,2100);
    menu_pad_repeat(3000);assert(menu_calls==4 && last_menu_action==ENH_OK);
    menu_pad_press(SDL_CONTROLLER_BUTTON_DPAD_LEFT,3100);
    menu_active=0;menu_pad_repeat(4000);assert(menu_calls==5 && g_menu_repeat_button==-1);
    menu_active=1;menu_pad_press(SDL_CONTROLLER_BUTTON_DPAD_LEFT,UINT32_MAX-200);
    menu_pad_repeat(198);assert(menu_calls==6);
    menu_pad_repeat(199);assert(menu_calls==7 && last_menu_action==ENH_LEFT);
    assert(menu_action_repeats(ENH_UP) && menu_action_repeats(ENH_DOWN));
    assert(!menu_action_repeats(ENH_OK) && !menu_action_repeats(ENH_BACK));
    menu_pad_press(SDL_CONTROLLER_BUTTON_DPAD_DOWN,5000);
    assert(menu_calls==8 && last_menu_action==ENH_DOWN);
    menu_pad_repeat(5399);assert(menu_calls==8);
    menu_pad_repeat(5400);assert(menu_calls==9 && last_menu_action==ENH_DOWN);
    menu_pad_press(SDL_CONTROLLER_BUTTON_DPAD_UP,5500);
    menu_pad_repeat(5900);assert(menu_calls==11 && last_menu_action==ENH_UP);
    menu_active=0;g_menu_repeat_button=-1;
    frontend_gyro_set_sensitivity(40);assert(frontend_gyro_sensitivity()==50);

    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER) == 0);
    int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
                                         SDL_CONTROLLER_BUTTON_MAX, 0);
    assert(index >= 0);
    open_pad();
    assert(g_pad);
    assert(pad_matches(g_pad_id));
    assert(!pad_matches(g_pad_id + 1));
    /* A press consumed by Pause/Resume must still count as held accelerator. */
    axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    button(SDL_CONTROLLER_BUTTON_A, 1); pad_button(SDL_CONTROLLER_BUTTON_A, 1);
    apply_inputs(.016); assert(g_analog[1]==200);
    paused=1;apply_inputs(.016);
    paused=0;apply_inputs(.016);assert(g_analog[1]==200);
    paused=1;
    button(SDL_CONTROLLER_BUTTON_A, 0);pad_button(SDL_CONTROLLER_BUTTON_A, 0);
    button(SDL_CONTROLLER_BUTTON_A, 1); /* menu handles this press, so no pad_button down */
    paused=0;apply_inputs(.016);assert(g_analog[1]==200);
    button(SDL_CONTROLLER_BUTTON_A, 0);apply_inputs(.016);assert(g_analog[1]==-200);
    button(SDL_CONTROLLER_BUTTON_B, 1);paused=1;apply_inputs(.016);
    paused=0;apply_inputs(.016);assert(g_analog[2]==200);
    button(SDL_CONTROLLER_BUTTON_B, 0);apply_inputs(.016);assert(g_analog[2]==-200);
    /* SDL's virtual joystick maps -32768..32767 to controller trigger 0..32767. */
    axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    apply_inputs(.016);
    assert(g_analog[0] == 0 && g_analog[1] == -200 && g_analog[2] == -200);
    axis(SDL_CONTROLLER_AXIS_LEFTX, -32768); apply_inputs(.016); assert(g_analog[0] == -200);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 32767); apply_inputs(.016); assert(g_analog[0] == 200);
    /* Partial travel must survive the real input path as distinct ADC values. */
    axis(SDL_CONTROLLER_AXIS_LEFTX, 8192); apply_inputs(.016); int quarter = g_analog[0];
    axis(SDL_CONTROLLER_AXIS_LEFTX, 16384); apply_inputs(.016); int half = g_analog[0];
    axis(SDL_CONTROLLER_AXIS_LEFTX, 24576); apply_inputs(.016); int three_quarters = g_analog[0];
    assert(quarter > 0 && half > quarter && three_quarters > half && three_quarters < 200);
    axis(SDL_CONTROLLER_AXIS_LEFTX, -16384); apply_inputs(.016); assert(abs(g_analog[0] + half) <= 1);
    axis(SDL_CONTROLLER_AXIS_LEFTX, 3000); apply_inputs(.016); assert(g_analog[0] == 0);
    axis(SDL_CONTROLLER_AXIS_RIGHTY, -32768); apply_inputs(.016); assert(g_analog[1] == 200 && g_analog[2] == -200);
    axis(SDL_CONTROLLER_AXIS_RIGHTY, 32767); apply_inputs(.016); assert(g_analog[2] == 200 && g_analog[1] == -200);
    axis(SDL_CONTROLLER_AXIS_RIGHTY, 0);
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0);
    key(SDLK_w, 1); apply_inputs(.016); assert(g_analog[1] == 200);
    button(SDL_CONTROLLER_BUTTON_A, 1); pad_button(SDL_CONTROLLER_BUTTON_A, 1);
    key(SDLK_w, 0); apply_inputs(.016); assert(g_analog[1] == 200);
    key(SDLK_w, 1); close_pad(); apply_inputs(.016); assert(g_analog[1] == 200);
    key(SDLK_w, 0); apply_inputs(.016); assert(g_analog[1] == -200);
    open_pad(); button(SDL_CONTROLLER_BUTTON_A, 0);axis(SDL_CONTROLLER_AXIS_RIGHTY, -32768);
    g_input_focus = 0; apply_inputs(.016); assert(g_analog[1] == -200);
    g_input_focus = 1; menu_active = 1; apply_inputs(.016); assert(g_analog[1] == -200);
    menu_active=0;
    frontend_set_stick_response(0); assert(frontend_stick_response()==0);
    axis(SDL_CONTROLLER_AXIS_LEFTX,16384); apply_inputs(.016); int linear=g_analog[0];
    frontend_set_stick_response(1); apply_inputs(.016); int soft=g_analog[0];
    frontend_set_stick_response(2); apply_inputs(.016); int extra_soft=g_analog[0];
    assert(linear>soft && soft>extra_soft && extra_soft>0);
    frontend_set_stick_response(99); assert(frontend_stick_response()==2);
    close_pad();
    assert(SDL_JoystickDetachVirtual(index) == 0);
    /* SDL reports virtual pads as virtual; emulate only the device classification. */
    emulate_switch_type = 1;
    index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX,
                                      SDL_CONTROLLER_BUTTON_MAX, 0);
    assert(index >= 0);
    open_pad();
    assert(g_pad);
    axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    apply_inputs(.016);
    assert(g_analog[1] == -200 && g_analog[2] == -200 && g_analog[3] == -200);
    axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767); apply_inputs(.016);
    assert(g_analog[1] == -200 && g_analog[2] == (GAME_HAS_HANDBRAKE ? -200 : 200)
           && g_analog[3] == (GAME_HAS_HANDBRAKE ? 200 : -200));
    axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767); apply_inputs(.016);
    assert(g_analog[1] == 200 && g_analog[2] == -200 && g_analog[3] == -200);
    axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767); apply_inputs(.016);
    assert(g_analog[1] == 200 && g_analog[2] == (GAME_HAS_HANDBRAKE ? -200 : 200)
           && g_analog[3] == (GAME_HAS_HANDBRAKE ? 200 : -200));
    frontend_set_switch_trigger_layout(0); apply_inputs(.016);
    assert(g_analog[1] == 200 && g_analog[2] == 200 && g_analog[3] == -200);
    frontend_set_switch_trigger_layout(1);
    g_input_focus = 0; apply_inputs(.016);
    assert(g_analog[2] == -200 && g_analog[3] == -200);
    key(SDLK_SPACE, 1); apply_inputs(.016);
    assert(g_analog[3] == (GAME_HAS_HANDBRAKE ? 200 : -200));
    key(SDLK_SPACE, 0); g_input_focus = 1;
    pad_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1); apply_inputs(.016);
    assert(!(g_in[3] & 0x40) && (g_in[4] & 0x01));
    pad_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0);
    button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
    assert(frontend_shift_up_held());
    pad_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1); apply_inputs(.016);
    assert((g_in[3] & 0x40) && !(g_in[4] & 0x01));
    g_input_focus = 0; assert(!frontend_shift_up_held()); g_input_focus = 1;
    button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0); pad_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
    assert(!frontend_shift_up_held()); key(SDLK_e, 1); assert(frontend_shift_up_held()); key(SDLK_e, 0);
    enhanced_owns_inputs=1; g_analog[0]=75; g_analog[1]=0; g_analog[2]=-200;
    apply_inputs(.016);
    assert(g_analog[0]==75 && g_analog[1]==0 && g_analog[2]==-200);
    /* Controls already held during auto-drive apply on the very first frame
     * after handover, without another button/key down event. */
    key(SDLK_RIGHT,1); key(SDLK_UP,1);
    enhanced_owns_inputs=0; apply_inputs(.016);
    assert(g_analog[0]>0 && g_analog[1]==ANALOG_RANGE);
    key(SDLK_RIGHT,0); key(SDLK_UP,0); apply_inputs(.05);
    g_enhanced=1;
    assert(pad_pause_press(SDL_CONTROLLER_BUTTON_START) && paused && pause_calls==1);
    pad_button(SDL_CONTROLLER_BUTTON_START,1); apply_inputs(.016); assert(g_in[3]&0x10);
    assert(!pad_pause_press(SDL_CONTROLLER_BUTTON_START) && paused && pause_calls==1);
    assert(menu_button(SDL_CONTROLLER_BUTTON_START)==ENH_OK);
    assert(menu_button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)==ENH_PAGE_DOWN);
    assert(menu_button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)==ENH_PAGE_UP);
    assert(menu_key(SDLK_e)==ENH_PAGE_DOWN && menu_key(SDLK_q)==ENH_PAGE_UP);
    paused=0;
    menu_active=1; assert(!pad_pause_press(SDL_CONTROLLER_BUTTON_START)); menu_active=0;
    pad_button(SDL_CONTROLLER_BUTTON_BACK,1); apply_inputs(.016);
    assert(!(g_in[3]&0x10) && (g_in[3]&0x04)); /* native Start doubles as view change */
    pad_button(SDL_CONTROLLER_BUTTON_BACK,0); apply_inputs(.016); assert(g_in[3]&0x10);
    emulate_switch_type=0; assert(!pad_pause_press(SDL_CONTROLLER_BUTTON_START));
    pad_button(SDL_CONTROLLER_BUTTON_START,1); apply_inputs(.016); assert(!(g_in[3]&0x10));
    pad_button(SDL_CONTROLLER_BUTTON_START,0); g_enhanced=0;
    close_pad(); apply_inputs(.016);
    assert(g_analog[1] == -200 && g_analog[2] == -200 && g_analog[3] == -200);
    assert(SDL_JoystickDetachVirtual(index) == 0);
    SDL_Quit();
    puts("frontend virtual-controller tests: passed");
}

uint64_t voodoo_get_frame_depth(uint32_t *f,uint16_t *d,int n,int *w,int *h,unsigned *m) { (void)d;*m=0;return voodoo_get_frame(f,n,w,h); }
void enh_overlay_depth(const uint16_t *d,int w,int h,unsigned m) {(void)d;(void)w;(void)h;(void)m;}
