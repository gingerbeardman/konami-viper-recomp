/* Standalone physical-controller reproduction. No game/runtime dependencies. */
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

static volatile sig_atomic_t interrupted;
static Uint32 epoch;
static void interrupt_test(int sig) { (void)sig; interrupted = 1; }
static const char *value(const char *s) { return s ? s : "(unset)"; }

static int pump_for(Uint32 ms, SDL_GameController *pad) {
    Uint32 start = SDL_GetTicks();
    while (!interrupted && SDL_GetTicks() - start < ms) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) interrupted = 1;
        }
        if (pad && !SDL_GameControllerGetAttached(pad)) {
            fprintf(stderr, "Controller disconnected.\n");
            return 0;
        }
        SDL_Delay(2);
    }
    return !interrupted;
}

static int send_rumble(SDL_GameController *pad, Uint16 strength, Uint32 ms, int log) {
    SDL_ClearError();
    int result = SDL_GameControllerRumble(pad, strength, strength, ms);
    if (log || result < 0)
        printf("t=%.3fs low=%u high=%u duration=%ums result=%d%s%s\n",
               (SDL_GetTicks() - epoch) / 1000.0, strength, strength, ms, result,
               result < 0 ? " error=" : "", result < 0 ? SDL_GetError() : "");
    return result == 0;
}

int main(int argc, char **argv) {
    int list_only = 0;
    const char *driver = "default", *mode = "all";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list")) list_only = 1;
        else if (!strcmp(argv[i], "--driver") && i + 1 < argc) driver = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
        else if (!strcmp(argv[i], "--help")) {
            puts("Usage: rumble-repro [--list] [--driver default|hidapi|native]\n"
                 "                    [--mode all|pulses|hold|refresh]\n"
                 "pulses: ten identical 1s effects, with 1s silence between them.\n"
                 "hold: one 10s effect. refresh: 100ms effects every 50ms for 20s.\n"
                 "All effects request constant 50% strength on both motors.\n"
                 "Driver flags request SDL routing; logs identify the device path.\n"
                 "Connect exactly one controller and quit other controller apps.\n"
                 "Ctrl-C stops rumble and exits. --list does not send rumble.");
            return 0;
        } else { fprintf(stderr, "Unknown/incomplete option: %s\n", argv[i]); return 2; }
    }
    if ((strcmp(driver, "default") && strcmp(driver, "hidapi") && strcmp(driver, "native")) ||
        (strcmp(mode, "all") && strcmp(mode, "pulses") && strcmp(mode, "hold") && strcmp(mode, "refresh"))) {
        fprintf(stderr, "Invalid driver or mode; see --help.\n"); return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, interrupt_test);
    signal(SIGTERM, interrupt_test);
    struct utsname system;
    if (!uname(&system)) printf("System: %s %s %s %s\n", system.sysname, system.release, system.machine, system.version);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (strcmp(driver, "default")) {
        const char *enabled = !strcmp(driver, "hidapi") ? "1" : "0";
        SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, enabled, SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, enabled, SDL_HINT_OVERRIDE);
    }
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError()); return 1;
    }
    SDL_version version; SDL_GetVersion(&version);
    printf("SDL %u.%u.%u revision=%s\n", version.major, version.minor, version.patch, SDL_GetRevision());
    printf("Driver request=%s HIDAPI=%s Switch HIDAPI=%s mode=%s\n", driver,
           value(SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI)), value(SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH)), mode);
    if (!pump_for(2000, NULL)) { SDL_Quit(); return 130; }
    int count = 0, selected = -1;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        char guid[33];
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i), guid, sizeof guid);
        printf("Device %d: %s recognized=%d GUID=%s VID=%04x PID=%04x path=%s\n",
               i, value(SDL_JoystickNameForIndex(i)), SDL_IsGameController(i), guid,
               SDL_JoystickGetDeviceVendor(i), SDL_JoystickGetDeviceProduct(i), value(SDL_JoystickPathForIndex(i)));
        if (SDL_IsGameController(i)) { ++count; selected = i; }
    }
    printf("Recognized controllers: %d\n", count);
    if (list_only) { SDL_Quit(); return 0; }
    if (count != 1) {
        fprintf(stderr, "Connect exactly one recognized controller. No rumble sent.\n");
        SDL_Quit(); return 1;
    }
    SDL_GameController *pad = SDL_GameControllerOpen(selected);
    if (!pad) { fprintf(stderr, "Open failed: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    printf("Type=%d firmware=%u rumble advertised=%d\n", SDL_GameControllerGetType(pad),
           SDL_GameControllerGetFirmwareVersion(pad), SDL_GameControllerHasRumble(pad));
    puts("Observe whether equal commands feel weaker over time. This program sends no fade envelope.\nStarting in 2 seconds.");
    epoch = SDL_GetTicks();
    int ok = pump_for(2000, pad);
    if (ok && (!strcmp(mode, "all") || !strcmp(mode, "pulses"))) {
        puts("PHASE pulses: 10 x 1 second at low=high=32768 (50%).");
        for (int pulse = 1; pulse <= 10 && ok; ++pulse) {
            printf("Pulse %d/10\n", pulse);
            ok = send_rumble(pad, 32768, 1000, 1) && pump_for(1000, pad);
            int stopped = send_rumble(pad, 0, 0, 1);
            ok = ok && stopped && pump_for(1000, pad);
        }
    }
    if (ok && (!strcmp(mode, "all") || !strcmp(mode, "hold"))) {
        puts("PHASE hold: one 10 second effect at low=high=32768 (50%).");
        ok = send_rumble(pad, 32768, 10000, 1);
        for (int second = 1; second <= 10 && ok; ++second) {
            ok = pump_for(1000, pad);
            printf("Hold elapsed=%ds; no new strength command\n", second);
        }
        int stopped = send_rumble(pad, 0, 0, 1);
        ok = ok && stopped && pump_for(2000, pad);
    }
    if (ok && (!strcmp(mode, "all") || !strcmp(mode, "refresh"))) {
        puts("PHASE refresh: 20 seconds, low=high=32768 (50%), duration=100ms, every ~50ms.\n"
             "This matches the game's constant-torque refresh cadence.");
        Uint32 start = SDL_GetTicks(), report = start;
        unsigned calls = 0;
        while (ok && SDL_GetTicks() - start < 20000) {
            ok = send_rumble(pad, 32768, 100, calls == 0);
            if (ok) ++calls;
            Uint32 now = SDL_GetTicks();
            if (now - report >= 1000) {
                printf("t=%.3fs refresh accepted=%u low=32768 high=32768 duration=100ms\n",
                       (now - epoch) / 1000.0, calls);
                report = now;
            }
            if (ok) ok = pump_for(50, pad);
        }
        printf("Refresh accepted calls: %u\n", calls);
    }
    int stopped = send_rumble(pad, 0, 0, 1);
    SDL_GameControllerClose(pad);
    SDL_Quit();
    puts("Finished. API acceptance cannot measure physical vibration; record your observations separately.");
    return interrupted ? 130 : (ok && stopped ? 0 : 1);
}
