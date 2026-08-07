#pragma once

#import <Cocoa/Cocoa.h>

#include "s3g/tracker/coremidi_output.h"

#include <vector>

namespace s3g::tracker::app {
struct TrackerViewState;
struct WorkspaceCallbacks;
}

NS_ASSUME_NONNULL_BEGIN

// Dedicated editor for indexed MIDI instruments and their owned CoreMIDI
// source / physical destination plus one-based channel route.
@interface S3GTrackerMidiWindowController : NSWindowController

- (instancetype)initWithState:
        (s3g::tracker::app::TrackerViewState*)state
    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;

- (void)setDestinations:
    (const std::vector<s3g::tracker::MidiDestination>&)destinations;
- (void)reloadModel;
- (void)showWindow:(nullable id)sender;

@end

NS_ASSUME_NONNULL_END
