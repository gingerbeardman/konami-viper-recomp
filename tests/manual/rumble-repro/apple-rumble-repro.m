/* Standalone reproduction of the game's Apple haptics event construction. */
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static volatile sig_atomic_t interrupted;
static void interrupt_test(int sig) { (void)sig; interrupted = 1; }
@interface RumbleTarget : NSObject
@property(nonatomic, strong) GCController *controller;
@property(nonatomic, strong) CHHapticEngine *engine;
@property(nonatomic, strong) id<CHHapticPatternPlayer> player;
@end
@implementation RumbleTarget
@end

static BOOL connected(NSArray<RumbleTarget *> *targets) {
    for (RumbleTarget *target in targets) {
        if (![GCController.controllers containsObject:target.controller]) {
            fprintf(stderr, "Controller disconnected: %s\n", target.controller.vendorName.UTF8String);
            return NO;
        }
    }
    return !interrupted;
}
static BOOL pump(double seconds, NSArray<RumbleTarget *> *targets) {
    double end = NSProcessInfo.processInfo.systemUptime + seconds;
    while (connected(targets) && NSProcessInfo.processInfo.systemUptime < end)
        @autoreleasepool {
            [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.002]];
        }
    return connected(targets);
}
static BOOL failure(const char *stage, NSError *error) {
    fprintf(stderr, "%s: %s [%s %ld]\n", stage,
            (error.localizedDescription ?: @"No engine/player returned").UTF8String,
            (error.domain ?: @"unknown").UTF8String, (long)error.code);
    return NO;
}
static BOOL stop(id<CHHapticPatternPlayer> player) {
    if (!player) return YES;
    NSError *error = nil;
    return [player stopAtTime:0 error:&error] || failure("Stop player", error);
}
static BOOL stop_all(NSArray<RumbleTarget *> *targets) {
    BOOL ok = YES;
    for (RumbleTarget *target in targets) {
        if (!stop(target.player)) {
            fprintf(stderr, "Stop failed for: %s\n", target.controller.vendorName.UTF8String);
            ok = NO;
        }
        target.player = nil;
    }
    return ok;
}
static BOOL prepare_effect(RumbleTarget *target, double duration, float strength) {
    NSError *error = nil;
    if (![target.engine startAndReturnError:&error]) return failure("Start engine", error);
    CHHapticEventParameter *intensity = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticIntensity value:strength];
    CHHapticEventParameter *sharpness = [[CHHapticEventParameter alloc]
        initWithParameterID:CHHapticEventParameterIDHapticSharpness value:.5f];
    CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
        parameters:@[intensity, sharpness] relativeTime:0 duration:duration];
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
    if (!pattern) return failure("Create pattern", error);
    target.player = [target.engine createPlayerWithPattern:pattern error:&error];
    if (!target.player) return failure("Create player", error);
    return YES;
}
static BOOL send_effects(NSArray<RumbleTarget *> *targets, double duration, float strength, BOOL log) {
    if (!stop_all(targets)) return NO;
    for (RumbleTarget *target in targets) {
        if (!prepare_effect(target, duration, strength)) {
            fprintf(stderr, "Effect preparation failed for: %s\n", target.controller.vendorName.UTF8String);
            stop_all(targets); return NO;
        }
    }
    /* Prepare every player first, then schedule a common host-time start.
     * Translate that deadline into each controller engine's own timebase. */
    double deadline = NSProcessInfo.processInfo.systemUptime + .02;
    for (RumbleTarget *target in targets) {
        NSError *error = nil;
        double engineTime = target.engine.currentTime;
        double delay = fmax(0, deadline - NSProcessInfo.processInfo.systemUptime);
        if (![target.player startAtTime:engineTime + delay error:&error]) {
            fprintf(stderr, "Start failed for: %s\n", target.controller.vendorName.UTF8String);
            failure("Start player", error); stop_all(targets); return NO;
        }
        if (log) printf("Accepted Apple effect: controller=%s intensity=%.3f sharpness=0.5 duration=%.3fs group-start=%.6f\n",
                        target.controller.vendorName.UTF8String, strength, duration, deadline);
    }
    return YES;
}
static BOOL pulse_sequence(NSArray<RumbleTarget *> *targets, float strength) {
    for (int i = 1; i <= 3; ++i) {
        printf("Pulse %d/3 at %.0f%%\n", i, strength * 100);
        BOOL ok = send_effects(targets, 1, strength, YES) && pump(1.2, targets);
        BOOL stopped = stop_all(targets);
        if (!ok || !stopped || !pump(1, targets)) return NO;
    }
    return YES;
}
static BOOL double_taps(NSArray<RumbleTarget *> *targets) {
    puts("PHASE double taps: three pairs of 150ms effects at 100%, 150ms gap within each pair.");
    for (int pair = 1; pair <= 3; ++pair) {
        for (int tap = 1; tap <= 2; ++tap) {
            printf("Pair %d/3 tap %d/2\n", pair, tap);
            BOOL ok = send_effects(targets, .15, 1, YES) && pump(.17, targets);
            BOOL stopped = stop_all(targets);
            if (!ok || !stopped || !pump(tap == 1 ? .15 : .8, targets)) return NO;
        }
    }
    return YES;
}
int main(int argc, char **argv) {
    @autoreleasepool {
        BOOL list = NO;
        const char *mode = "comparison";
        NSString *target = nil;
        float strength = .5f;
        BOOL modeSpecified = NO, strengthSpecified = NO;
        for (int i = 1; i < argc; ++i) {
            if (!strcmp(argv[i], "--list")) list = YES;
            else if (!strcmp(argv[i], "--mode") && i + 1 < argc) { mode = argv[++i]; modeSpecified = YES; }
            else if (!strcmp(argv[i], "--controller") && i + 1 < argc) target = [NSString stringWithUTF8String:argv[++i]];
            else if (!strcmp(argv[i], "--strength") && i + 1 < argc) {
                strengthSpecified = YES;
                char *end = NULL;
                const char *argument = argv[++i];
                strength = strtof(argument, &end);
                if (end == argument || *end || !isfinite(strength) || strength <= 0 || strength > 1) {
                    fprintf(stderr, "Strength must be greater than 0 and at most 1.\n"); return 2;
                }
            }
            else if (!strcmp(argv[i], "--help")) {
                puts("Usage: rumble-test [--list] [--mode comparison|pulses|double-taps|all|hold|refresh]\n"
                     "                          [--controller EXACT_VENDOR_NAME] [--strength 0..1]\n"
                     "Direct Apple GameController/CoreHaptics, no SDL.\n"
                     "Default: ALL haptics-capable controllers together, 3 x 50%, 3 x 100%,\n"
                     "then three pairs of 150ms 100% pulses. Sharpness=0.5. Ctrl-C stops.\n"
                     "--strength alone selects three pulses at that intensity.\n"
                     "--controller selects one unique exact name from --list."); return 0;
            } else { fprintf(stderr, "Unknown/incomplete option: %s\n", argv[i]); return 2; }
        }
        if (strengthSpecified && !modeSpecified) mode = "pulses";
        if (strcmp(mode, "comparison") && strcmp(mode, "double-taps") && strcmp(mode, "all") && strcmp(mode, "pulses") && strcmp(mode, "hold") && strcmp(mode, "refresh")) {
            fprintf(stderr, "Invalid mode; see --help.\n"); return 2;
        }
        if (strengthSpecified && (!strcmp(mode, "comparison") || !strcmp(mode, "double-taps"))) {
            fprintf(stderr, "This mode uses fixed strengths. Use --mode pulses with --strength.\n"); return 2;
        }
        setvbuf(stdout, NULL, _IONBF, 0);
        signal(SIGINT, interrupt_test);
        signal(SIGTERM, interrupt_test);
        printf("macOS: %s; backend=GameController/CoreHaptics mode=%s\n",
               NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String, mode);
        (void)GCController.controllers;
        if (!pump(2, nil)) return 130;
        NSArray<GCController *> *controllers = GCController.controllers;
        for (GCController *controller in controllers) {
            printf("Controller: vendor=%s category=%s haptics=%s localities=%s\n",
                   (controller.vendorName ?: @"unknown").UTF8String,
                   controller.productCategory.UTF8String, controller.haptics ? "yes" : "no",
                   (controller.haptics.supportedLocalities.description ?: @"none").UTF8String);
        }
        printf("Apple controllers: %lu\n", (unsigned long)controllers.count);
        if (list) return 0;
        NSArray<GCController *> *matches = controllers;
        if (target) matches = [controllers filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:
            ^BOOL(GCController *controller, NSDictionary *bindings) {
                (void)bindings; return [controller.vendorName isEqualToString:target];
            }]];
        if (target && (matches.count != 1 || !matches.firstObject.haptics)) {
            fprintf(stderr, "Named controller must match exactly one device with haptics. No effects sent.\n"); return 1;
        }
        NSMutableArray<RumbleTarget *> *targets = [NSMutableArray array];
        for (GCController *controller in matches) {
            if (!controller.haptics) continue;
            RumbleTarget *slot = [RumbleTarget new];
            slot.controller = controller;
            slot.engine = [controller.haptics createEngineWithLocality:GCHapticsLocalityDefault];
            [targets addObject:slot];
            if (!slot.engine) {
                fprintf(stderr, "Create engine failed for: %s\n", controller.vendorName.UTF8String);
                for (RumbleTarget *created in targets) [created.engine stopWithCompletionHandler:nil];
                return 1;
            }
            NSString *name = controller.vendorName ?: @"unknown";
            slot.engine.resetHandler = ^{ fprintf(stderr, "Apple engine reset: %s\n", name.UTF8String); };
            slot.engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
                fprintf(stderr, "Apple engine stopped: controller=%s reason=%ld\n", name.UTF8String, (long)reason);
            };
            printf("TARGET: %s; sharpness=0.5\n", name.UTF8String);
        }
        if (!targets.count) { fprintf(stderr, "No haptics-capable controllers. No effects sent.\n"); return 1; }
        printf("Testing %lu controller(s) together. Hold them now; starting in 2 seconds.\n", (unsigned long)targets.count);
        BOOL ok = pump(2, targets);
        if (ok && !strcmp(mode, "comparison")) {
            puts("PHASE 50%: three equal 1s effects together.");
            ok = pulse_sequence(targets, .5f) && pump(2, targets);
            if (ok) {
                puts("PHASE 100%: three equal 1s effects together.");
                ok = pulse_sequence(targets, 1) && pump(2, targets);
            }
            if (ok) ok = double_taps(targets);
        }
        if (ok && !strcmp(mode, "double-taps")) ok = double_taps(targets);
        if (ok && (!strcmp(mode, "all") || !strcmp(mode, "pulses"))) {
            puts("PHASE pulses: three equal 1s effects; wait 1.2s, stop, then 1s gap.");
            ok = pulse_sequence(targets, strength);
        }
        if (ok && (!strcmp(mode, "all") || !strcmp(mode, "hold"))) {
            puts("PHASE hold: one 10s effect.");
            ok = send_effects(targets, 10, strength, YES);
            for (int i = 1; i <= 10 && ok; ++i) {
                ok = pump(1, targets);
                printf("Hold elapsed=%ds; no new intensity command\n", i);
            }
            BOOL stopped = stop_all(targets);
            ok = ok && stopped && pump(2, targets);
        }
        if (ok && (!strcmp(mode, "all") || !strcmp(mode, "refresh"))) {
            puts("PHASE refresh: 100ms effects, replaced every ~50ms for 20s.");
            double start = NSProcessInfo.processInfo.systemUptime, report = start;
            unsigned calls = 0;
            while (ok && NSProcessInfo.processInfo.systemUptime - start < 20) {
                ok = send_effects(targets, .1, strength, calls == 0);
                if (ok) ++calls;
                double now = NSProcessInfo.processInfo.systemUptime;
                if (now - report >= 1) {
                    printf("elapsed=%.3fs accepted=%u intensity=%.3f sharpness=0.5 duration=0.100s\n", now - start, calls, strength);
                    report = now;
                }
                if (ok) ok = pump(.05, targets);
            }
            printf("Refresh accepted calls: %u\n", calls);
        }
        BOOL stopped = stop_all(targets);
        for (RumbleTarget *slot in targets) [slot.engine stopWithCompletionHandler:nil];
        puts("Finished. API acceptance cannot measure physical vibration.");
        return interrupted ? 130 : (ok && stopped ? 0 : 1);
    }
}
