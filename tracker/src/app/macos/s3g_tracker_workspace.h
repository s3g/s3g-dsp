#pragma once

#import <Cocoa/Cocoa.h>

#include "s3g/tracker/command.h"
#include "s3g/tracker/coremidi_output.h"
#include "s3g/tracker/instrument_rack.h"
#include "s3g/tracker/pattern_bank.h"
#include "s3g_tracker_audio_device.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace s3g::tracker::app {

constexpr std::size_t kVisibleLaneCount = kMaximumTrackCount;

struct TrackerViewState {
    TrackerSession session;
    // TrackerSession remains the command engine's mutable active-pattern
    // workspace. Pattern changes are synchronized to this ordered bank by the
    // app coordinator before selection, persistence, and playback publication.
    PatternBank patternBank = makeDefaultPatternBank();
    InstrumentRackState instrumentRack = makeDefaultInstrumentRack();
    uint32_t selectedRackInstrument = 0u;
    std::array<std::size_t, kVisibleLaneCount> notePlayheads {};
    std::array<bool, kVisibleLaneCount> noteHits {};
    std::array<std::size_t, kVisibleLaneCount> noteHitRows {};
    std::array<uint64_t, kVisibleLaneCount> noteHitSampleTimes {};
    std::array<std::size_t, kVisibleLaneCount> instrumentPlayheads {};
    std::array<std::size_t, kVisibleLaneCount> velocityPlayheads {};
    std::array<std::array<std::size_t, kFxPairCount>, kVisibleLaneCount>
        fxActionPlayheads {};
    std::array<std::array<std::size_t, kFxPairCount>, kVisibleLaneCount>
        fxValuePlayheads {};
    bool playing = false;
    bool paused = false;
    bool songPlaybackEnabled = false;
    bool songPlaybackActive = false;
    std::size_t songPlaybackRow = 0u;
    bool songPlaybackRowValid = false;
    bool audioAvailable = false;
    bool audioProcessing = false;
    double audioSampleRate = 0.0;
    float audioPeak = 0.0f;
    // Mixer page state. Track strips store their event trim in Track; MAIN
    // OUT is a real internal-audio post-decode fader and does not affect MIDI.
    float mainOutputGain = 1.0f;
    bool mainOutputMuted = false;
    bool mixerPageVisible = false;
    std::size_t mixerSelectedStrip = 0u;
    bool mixerSoloActive = false;
    std::size_t mixerSoloTrack = kVisibleLaneCount;
    std::vector<bool> mixerSoloRestoreMutes;
    uint64_t audioCallbackCount = 0u;
    uint64_t audioLateEventCount = 0u;
    uint64_t audioDroppedEventCount = 0u;
    uint64_t audioClockFaultCount = 0u;
    uint64_t audioRenderErrorCount = 0u;
    std::string midiRoute = "1 MIDI DEVICE";
    std::string audioOutputDevice = "DEFAULT OUTPUT";
    // The embedded CLAP build displays the host clock and applies this
    // persisted musical ratio to the tracker scheduler.
    double hostBpm = 0.0;
    double tempoScale = 1.0;
    std::string status = "MIDI ready";
    std::string lastEvent = "No MIDI sent yet";
    uint64_t sentEventCount = 0u;
    uint64_t droppedEventCount = 0u;
};

struct WorkspaceCallbacks {
    std::function<void()> togglePlayback;
    std::function<void()> restartPlayback;
    std::function<void(std::size_t)> resyncTrack;
    std::function<void()> panic;
    std::function<void()> showSongWindow;
    // Standalone builds may still present these modules as windows. Embedded
    // CLAP hosts provide page callbacks so the same workspace actions stay
    // inside the plug-in editor.
    std::function<void()> showGeometryPage;
    std::function<void()> showWarpPage;
    std::function<void()> showInstrumentWindow;
    std::function<void(uint32_t)> editRackInstrument;
    std::function<void()> showConsoleHelp;
    std::function<void(uint32_t, uint32_t, float)>
        instrumentParameterChanged;
    std::function<void(uint32_t, uint8_t, float)> auditionInstrument;
    std::function<void(uint32_t)> resetInstrumentPatch;
    std::function<void()> instrumentRackChanged;
    // Republishes derived runtime assets (for example decoded sample PCM)
    // without marking the native project document as edited.
    std::function<void()> instrumentRackReloaded;
    std::function<void(const std::string&)> reportError;
    std::function<void()> refreshMidiDestinations;
    std::function<void()> refreshAudioOutputDevices;
    std::function<void(uint32_t)> selectAudioOutputDevice;
    std::function<void()> patternChanged;
    std::function<void(const std::string&)> selectPattern;
    std::function<void(bool)> addPattern;
    std::function<void()> renamePattern;
    std::function<void()> deletePattern;
    std::function<void()> transportChanged;
    std::function<void()> outputChanged;
    std::function<void(float)> mainOutputGainChanged;
    std::function<void(const std::string&)> executeCommand;
};

} // namespace s3g::tracker::app

@interface S3GTrackerWorkspaceController : NSViewController

- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState*)state
    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks;

- (void)reloadModel;
- (void)refreshPlaybackDisplay;
- (void)setMidiDestinations:
    (const std::vector<s3g::tracker::MidiDestination>&)destinations
    selectedTarget:(const s3g::tracker::MidiOutputTarget&)target;
- (void)setAudioOutputDevices:
    (const std::vector<s3g::tracker::app::AudioOutputDevice>&)devices
    selectedDeviceId:(uint32_t)selectedDeviceId;
- (void)appendConsoleMessage:(const std::string&)message error:(BOOL)isError;
- (void)showGeometryWindow:(id)sender;
- (void)showWarpWindow:(id)sender;
- (void)showMixerPage:(id)sender;
- (void)showTrackerPage:(id)sender;
- (NSView*)geometryPageView;
- (NSView*)warpPageView;
- (NSView*)consolePageView;
- (void)focusConsole;
- (void)focusTracker;
- (void)zoomTrackerIn;
- (void)zoomTrackerOut;
- (void)resetTrackerZoom;

@end
