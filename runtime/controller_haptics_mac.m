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
@property(nonatomic, strong) id<CHHapticPatternPlayer> player;
@property(nonatomic) BOOL restart;
@property(nonatomic) BOOL logged;
@end
@implementation ViperHaptics
@end
static ViperHaptics *slot;

void controller_haptics_init(void) { (void)GCController.controllers; }

void controller_haptics_stop(void) {
    [slot.player stopAtTime:0 error:nil];
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
            if (![slot.engine startAndReturnError:&error]) return failed(error);
            slot.restart = NO;
        }
        controller_haptics_stop();
        CHHapticEventParameter *intensity = [[CHHapticEventParameter alloc]
            initWithParameterID:CHHapticEventParameterIDHapticIntensity value:fminf(1, strength)];
        CHHapticEventParameter *sharpness = [[CHHapticEventParameter alloc]
            initWithParameterID:CHHapticEventParameterIDHapticSharpness value:.5f];
        CHHapticEvent *event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
            parameters:@[intensity, sharpness] relativeTime:0 duration:seconds];
        CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event] parameters:@[] error:&error];
        if (!pattern) return failed(error);
        id<CHHapticPatternPlayer> player = [slot.engine createPlayerWithPattern:pattern error:&error];
        if (!player || ![player startAtTime:0 error:&error]) return failed(error);
        slot.player = player;
        return 1;
    }
}
