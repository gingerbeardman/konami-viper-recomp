/* Native Switch Pro rumble, using the GameController/CoreHaptics path used
 * by the middleware desktop player. SDL remains responsible for input. */
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>
#include "controller_haptics_mac.h"
#include <math.h>

@interface ViperHaptics : NSObject
@property(nonatomic, strong) GCController *controller;
@property(nonatomic, strong) CHHapticEngine *engine;
@property(nonatomic, strong) id<CHHapticAdvancedPatternPlayer> player;
@property(nonatomic) NSUInteger generation;
@property(nonatomic) float strength;
@property(nonatomic) BOOL restart;
@property(nonatomic) BOOL logged;
@end
@implementation ViperHaptics
@end
static ViperHaptics *slot;

void controller_haptics_init(void) { (void)GCController.controllers; }

void controller_haptics_stop(void) {
    slot.generation++;
    [slot.player stopAtTime:0 error:NULL];
    slot.player = nil;
}

static void discard_slot(void) {
    controller_haptics_stop();
    [slot.engine stopWithCompletionHandler:nil];
    slot = nil;
}

static int failed(NSError *error) {
    if (!slot.logged) {
        NSLog(@"Viper controller haptics: %@", error.localizedDescription ?: @"could not create or start an effect");
        slot.logged = YES;
    }
    controller_haptics_stop();
    slot.restart = YES;
    return -1;
}

int controller_haptics_rumble(float strength, double seconds) {
    @autoreleasepool {
        /* SDL2 cannot expose its GCController identity. Only select a native
         * target when there is exactly one pad, of the matching product type.
         * Ambiguous multi-controller setups retain SDL's device routing. */
        NSArray<GCController *> *controllers = GCController.controllers;
        if (controllers.count != 1 ||
            ![controllers.firstObject.productCategory isEqualToString:@"Switch Pro Controller"]) {
            discard_slot();
            return 0;
        }
        GCController *controller = controllers.firstObject;
        if (slot.controller != controller) discard_slot();
        if (!controller.haptics) return 0;
        if (strength <= 0 || seconds <= 0) {
            controller_haptics_stop();
            return 1;
        }
        if (!slot) {
            slot = [ViperHaptics new];
            slot.controller = controller;
            slot.engine = [controller.haptics createEngineWithLocality:GCHapticsLocalityDefault];
            slot.restart = YES;
            __weak ViperHaptics *weakSlot = slot;
            slot.engine.resetHandler = ^{
                dispatch_async(dispatch_get_main_queue(), ^{ weakSlot.restart = YES; weakSlot.player = nil; });
            };
            slot.engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
                (void)reason;
                dispatch_async(dispatch_get_main_queue(), ^{ weakSlot.restart = YES; weakSlot.player = nil; });
            };
        }
        if (!slot.engine) return failed(nil);
        NSError *error = nil;
        if (slot.restart) {
            controller_haptics_stop();
            slot.restart = NO;
        }
        BOOL starting = slot.player == nil;
        if (starting) {
            if (![slot.engine startAndReturnError:&error]) return failed(error);
            CHHapticEventParameter *intensity = [[CHHapticEventParameter alloc]
                initWithParameterID:CHHapticEventParameterIDHapticIntensity value:1];
            CHHapticEventParameter *sharpness = [[CHHapticEventParameter alloc]
                initWithParameterID:CHHapticEventParameterIDHapticSharpness value:.5f];
            CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                parameters:@[intensity, sharpness] relativeTime:0 duration:1];
            CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
            if (!pattern) return failed(error);
            slot.player = [slot.engine createAdvancedPlayerWithPattern:pattern error:&error];
            if (!slot.player) return failed(error);
            slot.player.loopEnabled = YES;
        }
        /* Refreshes extend the watchdog without sending another stop/start pair.
         * Only a changed force needs an output command. */
        strength = fmaxf(0, fminf(1, strength));
        if (starting || strength != slot.strength) {
            CHHapticDynamicParameter *intensity = [[CHHapticDynamicParameter alloc]
                initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                value:strength relativeTime:0];
            if (![slot.player sendParameters:@[intensity] atTime:0 error:&error]) return failed(error);
            slot.strength = strength;
        }
        if (starting && ![slot.player startAtTime:0 error:&error]) return failed(error);
        slot.logged = NO;
        NSUInteger generation = ++slot.generation;
        __weak ViperHaptics *weakSlot = slot;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            ViperHaptics *owner = weakSlot;
            if (owner && owner == slot && owner.generation == generation)
                controller_haptics_stop();
        });
        return 1;
    }
}
