#include "s3g_coreaudio_output.h"
#include "s3g_cocoa_gui.h"
#include "s3g_no_input_mixer.h"
#include "s3g_no_input_mixer_standalone_engine.h"

#import <Cocoa/Cocoa.h>
#import <CoreMIDI/CoreMIDI.h>

#include <clap/ext/gui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <limits>
#include <string>
#include <vector>

extern "C" const clap_plugin_entry_t s3g_no_input_mixer_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_nim_gesture_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_mc_to_stereo_autogain_embedded_entry;
extern "C" const clap_plugin_entry_t s3g_mc_to_quad_autogain_embedded_entry;

namespace {

constexpr CGFloat kNativePluginWidth = 1356.0;
constexpr CGFloat kNativePluginHeight = 820.0;
constexpr CGFloat kOutputStripHeight = 92.0;

using s3g::standalone::CoreAudioDeviceInfo;
using s3g::standalone::CoreAudioOutput;
using s3g::standalone::EmbeddedClapPlugin;
using s3g::standalone::NoInputMixerStandaloneEngine;
using s3g::standalone::NoInputOutputMode;
using s3g::standalone::outputChannelsForMode;

struct AppState {
    struct MidiDestination {
        MIDIEndpointRef endpoint = 0;
        MIDIUniqueID uniqueId = 0;
        std::string name;
    };

    NoInputMixerStandaloneEngine engine;
    CoreAudioOutput audio;
    std::vector<CoreAudioDeviceInfo> devices;
    uint32_t selectedDevice = 0u;
    std::vector<float> hardwareScratch;
    std::vector<float*> hardwarePointers;
    MIDIClientRef midiClient = 0;
    MIDIPortRef midiInputPort = 0;
    MIDIPortRef midiOutputPort = 0;
    std::vector<MidiDestination> midiDestinations;
    uint32_t selectedE16MidiDestination =
        std::numeric_limits<uint32_t>::max();
    uint32_t selectedGridMidiDestination =
        std::numeric_limits<uint32_t>::max();
    uint32_t connectedMidiSourceCount = 0u;
    std::atomic<bool> deviceOpen { false };
    std::string audioError;
};

void clampOutputChannelOffset(AppState& state, NoInputOutputMode mode)
{
    const uint32_t required = outputChannelsForMode(mode);
    const uint32_t available = state.selectedDevice < state.devices.size()
        ? state.devices[state.selectedDevice].outputChannels : 0u;
    if (available < required) {
        state.engine.setOutputChannelOffset(mode, 0u);
        return;
    }
    const uint32_t lastAlignedBank = ((available - required) / required)
        * required;
    const uint32_t requested = state.engine.outputChannelOffset(mode);
    const uint32_t aligned = (requested / required) * required;
    state.engine.setOutputChannelOffset(mode,
        std::min(aligned, lastAlignedBank));
}

void clearAudioBufferList(AudioBufferList* output)
{
    if (!output) return;
    for (UInt32 buffer = 0u; buffer < output->mNumberBuffers; ++buffer) {
        auto& target = output->mBuffers[buffer];
        if (target.mData && target.mDataByteSize > 0u)
            std::memset(target.mData, 0, target.mDataByteSize);
    }
}

OSStatus renderAudio(void* context, AudioBufferList* output, uint32_t frames)
{
    auto* state = static_cast<AppState*>(context);
    if (!state || !output || !state->deviceOpen.load(
            std::memory_order_acquire)
        || frames > state->engine.maximumFrames()) {
        clearAudioBufferList(output);
        return noErr;
    }
    const uint32_t renderChannels = state->audio.config().renderChannels;
    state->engine.render(state->hardwarePointers.data(), renderChannels,
        frames);

    uint32_t channelBase = 0u;
    for (UInt32 buffer = 0u; buffer < output->mNumberBuffers; ++buffer) {
        auto& target = output->mBuffers[buffer];
        auto* samples = static_cast<float*>(target.mData);
        const uint32_t bufferChannels = std::max<UInt32>(1u,
            target.mNumberChannels);
        const size_t capacity = target.mDataByteSize / sizeof(float);
        if (!samples || capacity == 0u) {
            channelBase += bufferChannels;
            continue;
        }
        std::fill_n(samples, capacity, 0.0f);
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            for (uint32_t channel = 0u; channel < bufferChannels; ++channel) {
                const size_t index = static_cast<size_t>(frame)
                    * bufferChannels + channel;
                const uint32_t sourceChannel = channelBase + channel;
                if (index < capacity && sourceChannel < renderChannels)
                    samples[index] = state->hardwarePointers[sourceChannel][frame];
            }
        }
        channelBase += bufferChannels;
    }
    return noErr;
}

bool openSelectedAudioDevice(AppState& state)
{
    state.engine.setAudioEnabled(false);
    state.deviceOpen.store(false, std::memory_order_release);
    state.audio.close();
    state.engine.release();
    state.hardwareScratch.clear();
    state.hardwarePointers.clear();
    if (state.selectedDevice >= state.devices.size()) {
        state.audioError = "No output device is selected";
        return false;
    }
    const auto& device = state.devices[state.selectedDevice];
    const uint32_t renderChannels = device.outputChannels;
    if (renderChannels < 2u || !state.audio.open(device.id, renderChannels,
            renderAudio, &state, &state.audioError)) return false;
    const auto& config = state.audio.config();
    if (!state.engine.prepare(config.sampleRate, config.maximumFrames)) {
        state.audioError = "Could not activate the embedded CLAP processors";
        state.audio.close();
        return false;
    }
    state.hardwareScratch.assign(static_cast<size_t>(renderChannels)
        * config.maximumFrames, 0.0f);
    state.hardwarePointers.assign(renderChannels, nullptr);
    for (uint32_t channel = 0u; channel < renderChannels; ++channel) {
        state.hardwarePointers[channel] = state.hardwareScratch.data()
            + static_cast<size_t>(channel) * config.maximumFrames;
    }
    if (!state.audio.start(&state.audioError)) {
        state.engine.release();
        state.audio.close();
        state.hardwareScratch.clear();
        state.hardwarePointers.clear();
        return false;
    }
    clampOutputChannelOffset(state, NoInputOutputMode::StereoAutogain);
    clampOutputChannelOffset(state, NoInputOutputMode::QuadAutogain);
    clampOutputChannelOffset(state, NoInputOutputMode::DirectEight);
    state.deviceOpen.store(true, std::memory_order_release);
    return true;
}

void midiReadProc(const MIDIPacketList* packetList, void* context,
    void*)
{
    auto* state = static_cast<AppState*>(context);
    if (!state || !packetList) return;
    const MIDIPacket* packet = &packetList->packet[0];
    for (UInt32 packetIndex = 0u; packetIndex < packetList->numPackets;
         ++packetIndex) {
        UInt16 offset = 0u;
        while (offset < packet->length) {
            const uint8_t status = packet->data[offset];
            if ((status & 0x80u) == 0u) break;
            if (status >= 0xf8u) {
                ++offset;
                continue;
            }
            const uint8_t type = status & 0xf0u;
            if (type == 0xf0u) break;
            const uint32_t length = type == 0xc0u || type == 0xd0u ? 2u : 3u;
            if (offset + length > packet->length) break;
            const uint8_t dataOne = packet->data[offset + 1u] & 0x7fu;
            const uint8_t dataTwo = length == 3u
                ? packet->data[offset + 2u] & 0x7fu : 0u;
            state->engine.enqueueMidi(status, dataOne, dataTwo);
            offset = static_cast<UInt16>(offset + length);
        }
        packet = MIDIPacketNext(packet);
    }
}

void connectMidiInputs(AppState& state)
{
    MIDIClientCreate(CFSTR("s3g No Input Mixer"), nullptr, nullptr,
        &state.midiClient);
    if (!state.midiClient) return;
    MIDIInputPortCreate(state.midiClient, CFSTR("Controller Input"),
        midiReadProc, &state, &state.midiInputPort);
    if (!state.midiInputPort) return;
    const ItemCount count = MIDIGetNumberOfSources();
    for (ItemCount index = 0u; index < count; ++index) {
        MIDIEndpointRef source = MIDIGetSource(index);
        if (source && MIDIPortConnectSource(state.midiInputPort, source,
                nullptr) == noErr) ++state.connectedMidiSourceCount;
    }

    MIDIOutputPortCreate(state.midiClient, CFSTR("Controller Feedback"),
        &state.midiOutputPort);
    const ItemCount destinationCount = MIDIGetNumberOfDestinations();
    for (ItemCount index = 0u; index < destinationCount; ++index) {
        MIDIEndpointRef endpoint = MIDIGetDestination(index);
        if (!endpoint) continue;
        CFStringRef displayName = nullptr;
        MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName,
            &displayName);
        char name[512] {};
        if (displayName) {
            CFStringGetCString(displayName, name, sizeof(name),
                kCFStringEncodingUTF8);
            CFRelease(displayName);
        }
        MIDIUniqueID uniqueId = 0;
        MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID,
            &uniqueId);
        AppState::MidiDestination destination;
        destination.endpoint = endpoint;
        destination.uniqueId = uniqueId;
        destination.name = name[0] != '\0' ? name : "MIDI Destination";
        state.midiDestinations.push_back(std::move(destination));
    }

    auto restoreDestination = [&](NSString* key, uint32_t& selected) {
        const NSInteger savedUniqueId = [[NSUserDefaults standardUserDefaults]
            integerForKey:key];
        if (savedUniqueId == 0) return;
        for (uint32_t index = 0u; index < state.midiDestinations.size();
             ++index) {
            if (state.midiDestinations[index].uniqueId != savedUniqueId)
                continue;
            selected = index;
            break;
        }
    };
    restoreDestination(@"MidiFeedbackDestinationUniqueID",
        state.selectedE16MidiDestination);
    restoreDestination(@"GridFeedbackDestinationUniqueID",
        state.selectedGridMidiDestination);
}

void drainMidiOutput(AppState& state)
{
    uint8_t status = 0u;
    uint8_t dataOne = 0u;
    uint8_t dataTwo = 0u;
    while (state.engine.dequeueMidiOutput(status, dataOne, dataTwo)) {
        const uint8_t command = status & 0xf0u;
        const uint8_t channel = status & 0x0fu;
        uint32_t destinationIndex = std::numeric_limits<uint32_t>::max();
        if (command == 0xb0u && channel == 15u) {
            destinationIndex = state.selectedE16MidiDestination;
        } else if (command == s3g::kNoInputMatrixFeedbackCommand
            && channel < s3g::kNoInputMatrixGridChannels) {
            destinationIndex = state.selectedGridMidiDestination;
        }
        if (!state.midiOutputPort
            || destinationIndex >= state.midiDestinations.size()) continue;
        MIDIPacketList packetList {};
        MIDIPacket* packet = MIDIPacketListInit(&packetList);
        const Byte bytes[3] { status, dataOne, dataTwo };
        packet = MIDIPacketListAdd(&packetList, sizeof(packetList), packet,
            0u, sizeof(bytes), bytes);
        if (packet) MIDISend(state.midiOutputPort,
            state.midiDestinations[destinationIndex].endpoint,
            &packetList);
    }
}

NSData* dataFromBytes(const std::vector<uint8_t>& bytes)
{
    return bytes.empty() ? nil : [NSData dataWithBytes:bytes.data()
        length:bytes.size()];
}

std::vector<uint8_t> bytesFromData(NSData* data)
{
    if (!data || [data length] == 0u) return {};
    const auto* begin = static_cast<const uint8_t*>([data bytes]);
    return std::vector<uint8_t>(begin, begin + [data length]);
}

void restoreProcessorState(AppState& state)
{
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    auto restore = [defaults](EmbeddedClapPlugin& plugin, NSString* key) {
        NSData* data = [defaults dataForKey:key];
        const auto bytes = bytesFromData(data);
        if (!bytes.empty()) plugin.loadState(bytes);
    };
    restore(state.engine.noInputPlugin(), @"NoInputMixerState");
    restore(state.engine.gesturePlugin(), @"NimGestureState");
    restore(state.engine.stereoPlugin(), @"StereoAutogainState");
    restore(state.engine.quadPlugin(), @"QuadAutogainState");
    const NSInteger mode = [defaults integerForKey:@"OutputMode"];
    state.engine.setOutputMode(static_cast<NoInputOutputMode>(
        std::clamp<NSInteger>(mode, 0, 2)));
    for (uint32_t index = 0u; index < 3u; ++index) {
        NSString* key = [NSString stringWithFormat:@"OutputOffset%u", index];
        state.engine.setOutputChannelOffset(
            static_cast<NoInputOutputMode>(index),
            static_cast<uint32_t>(std::max<NSInteger>(0,
                [defaults integerForKey:key])));
    }
    const double tempo = [defaults doubleForKey:@"TempoBpm"];
    state.engine.setTempo(tempo > 0.0 ? tempo : 120.0);
}

void saveProcessorState(AppState& state)
{
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    auto save = [defaults](const EmbeddedClapPlugin& plugin, NSString* key) {
        std::vector<uint8_t> bytes;
        if (plugin.saveState(bytes)) [defaults setObject:dataFromBytes(bytes)
            forKey:key];
    };
    save(state.engine.noInputPlugin(), @"NoInputMixerState");
    save(state.engine.gesturePlugin(), @"NimGestureState");
    save(state.engine.stereoPlugin(), @"StereoAutogainState");
    save(state.engine.quadPlugin(), @"QuadAutogainState");
    [defaults setInteger:static_cast<NSInteger>(state.engine.outputMode())
        forKey:@"OutputMode"];
    for (uint32_t index = 0u; index < 3u; ++index) {
        NSString* key = [NSString stringWithFormat:@"OutputOffset%u", index];
        [defaults setInteger:state.engine.outputChannelOffset(
            static_cast<NoInputOutputMode>(index)) forKey:key];
    }
    [defaults setDouble:state.engine.tempo() forKey:@"TempoBpm"];
    if (state.selectedE16MidiDestination < state.midiDestinations.size()) {
        [defaults setInteger:state.midiDestinations[
            state.selectedE16MidiDestination].uniqueId
            forKey:@"MidiFeedbackDestinationUniqueID"];
    } else {
        [defaults removeObjectForKey:@"MidiFeedbackDestinationUniqueID"];
    }
    if (state.selectedGridMidiDestination < state.midiDestinations.size()) {
        [defaults setInteger:state.midiDestinations[
            state.selectedGridMidiDestination].uniqueId
            forKey:@"GridFeedbackDestinationUniqueID"];
    } else {
        [defaults removeObjectForKey:@"GridFeedbackDestinationUniqueID"];
    }
    if (state.selectedDevice < state.devices.size()) {
        [defaults setObject:[NSString stringWithUTF8String:
            state.devices[state.selectedDevice].uid.c_str()]
            forKey:@"OutputDeviceUID"];
    }
}

const clap_plugin_gui_t* pluginGui(EmbeddedClapPlugin& plugin)
{
    return plugin.extension<clap_plugin_gui_t>(CLAP_EXT_GUI);
}

bool attachPluginGui(EmbeddedClapPlugin& plugin, NSView* parent,
    uint32_t* width = nullptr, uint32_t* height = nullptr)
{
    const auto* gui = pluginGui(plugin);
    if (!gui || !parent || !gui->is_api_supported
        || !gui->is_api_supported(plugin.plugin(), CLAP_WINDOW_API_COCOA,
            false)
        || !gui->create
        || !gui->create(plugin.plugin(), CLAP_WINDOW_API_COCOA, false))
        return false;
    uint32_t guiWidth = static_cast<uint32_t>([parent bounds].size.width);
    uint32_t guiHeight = static_cast<uint32_t>([parent bounds].size.height);
    if (gui->get_size) gui->get_size(plugin.plugin(), &guiWidth, &guiHeight);
    clap_window_t window {};
    window.api = CLAP_WINDOW_API_COCOA;
    window.cocoa = parent;
    if (!gui->set_parent || !gui->set_parent(plugin.plugin(), &window)) {
        gui->destroy(plugin.plugin());
        return false;
    }
    if (gui->show) gui->show(plugin.plugin());
    if (width) *width = guiWidth;
    if (height) *height = guiHeight;
    return true;
}

void detachPluginGui(EmbeddedClapPlugin& plugin)
{
    const auto* gui = pluginGui(plugin);
    if (!gui) return;
    if (gui->hide) gui->hide(plugin.plugin());
    if (gui->destroy) gui->destroy(plugin.plugin());
}

} // namespace

@interface S3GStandaloneModeControl : NSSegmentedControl
- (NSRect)segmentRect:(NSInteger)segment;
@end

@implementation S3GStandaloneModeControl

- (NSRect)segmentRect:(NSInteger)segment
{
    constexpr CGFloat gap = 5.0;
    const NSInteger count = std::max<NSInteger>(1, [self segmentCount]);
    const NSRect bounds = [self bounds];
    const CGFloat width = (bounds.size.width - gap * (count - 1)) / count;
    return NSMakeRect(bounds.origin.x + segment * (width + gap),
        bounds.origin.y, width, bounds.size.height);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const NSInteger selected = [self selectedSegment];
    for (NSInteger segment = 0; segment < [self segmentCount]; ++segment) {
        const NSRect rect = [self segmentRect:segment];
        const bool enabled = [self isEnabled]
            && [self isEnabledForSegment:segment];
        const bool active = enabled && segment == selected;
        [s3g::clap_gui::color(active ? 0x303030 : 0x151515) setFill];
        NSRectFill(rect);
        [s3g::clap_gui::color(active ? 0xd4d4d4
            : enabled ? 0x666666 : 0x454545) setStroke];
        NSFrameRect(rect);
        if (active) {
            [s3g::clap_gui::color(0xb8b8b8) setFill];
            NSRectFill(NSMakeRect(rect.origin.x + 1.0,
                NSMaxY(rect) - 3.0, rect.size.width - 2.0, 2.0));
        }
        NSString* label = [self labelForSegment:segment] ?: @"";
        NSDictionary* attrs = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(active ? 0xf2f2f2
                : enabled ? 0xb8b8b8 : 0x707070), 10.5);
        const NSSize size = [label sizeWithAttributes:attrs];
        [label drawAtPoint:NSMakePoint(
            rect.origin.x + (rect.size.width - size.width) * 0.5,
            rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
            withAttributes:attrs];
    }
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    for (NSInteger segment = 0; segment < [self segmentCount]; ++segment) {
        if (!NSPointInRect(point, [self segmentRect:segment])
            || ![self isEnabledForSegment:segment]) continue;
        [self setSelectedSegment:segment];
        [self setNeedsDisplay:YES];
        [self sendAction:[self action] to:[self target]];
        return;
    }
}

@end

@interface S3GStandalonePopupButton : NSPopUpButton
@end

@implementation S3GStandalonePopupButton

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const bool enabled = [self isEnabled];
    const NSRect rect = NSInsetRect([self bounds], 0.5, 0.5);
    [s3g::clap_gui::color(enabled ? 0x181818 : 0x111111) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(enabled ? 0x707070 : 0x454545) setStroke];
    NSFrameRect(rect);

    NSMutableParagraphStyle* paragraph = [[[NSMutableParagraphStyle alloc]
        init] autorelease];
    [paragraph setLineBreakMode:NSLineBreakByTruncatingMiddle];
    [paragraph setAlignment:NSTextAlignmentLeft];
    NSDictionary* attrs = @{
        NSForegroundColorAttributeName: s3g::clap_gui::color(
            enabled ? 0xc8c8c8 : 0x707070),
        NSFontAttributeName: s3g::clap_gui::uiFont(10.0),
        NSParagraphStyleAttributeName: paragraph,
    };
    NSString* title = [self titleOfSelectedItem] ?: @"—";
    const NSRect textRect = NSMakeRect(rect.origin.x + 9.0,
        rect.origin.y + (rect.size.height - 14.0) * 0.5,
        std::max<CGFloat>(0.0, rect.size.width - 29.0), 14.0);
    [title drawInRect:textRect withAttributes:attrs];

    const CGFloat arrowX = NSMaxX(rect) - 13.0;
    const CGFloat arrowY = NSMidY(rect) - 1.5;
    NSBezierPath* arrow = [NSBezierPath bezierPath];
    [arrow moveToPoint:NSMakePoint(arrowX - 4.0, arrowY + 3.0)];
    [arrow lineToPoint:NSMakePoint(arrowX + 4.0, arrowY + 3.0)];
    [arrow lineToPoint:NSMakePoint(arrowX, arrowY - 2.0)];
    [arrow closePath];
    [s3g::clap_gui::color(enabled ? 0xb8b8b8 : 0x3d3d3d) setFill];
    [arrow fill];
}

@end

@interface S3GStandaloneActionButton : NSButton
@end

@implementation S3GStandaloneActionButton

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const bool enabled = [self isEnabled];
    const bool live = [self tag] == 1
        && [self state] == NSControlStateValueOn;
    const bool danger = [self tag] == 2;
    const bool pressed = [self isHighlighted];
    const NSRect rect = NSInsetRect([self bounds], 0.5, 0.5);
    const int fill = pressed ? 0x3a3a3a : live ? 0x30201e : 0x181818;
    const int border = !enabled ? 0x303030 : live ? 0xe06a58
        : danger ? 0x98564d : 0x707070;
    [s3g::clap_gui::color(fill) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(border) setStroke];
    NSFrameRect(rect);
    if (live) {
        [s3g::clap_gui::color(0xe06a58) setFill];
        NSRectFill(NSMakeRect(rect.origin.x + 1.0,
            NSMaxY(rect) - 3.0, rect.size.width - 2.0, 2.0));
    }
    NSDictionary* attrs = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(enabled
            ? live ? 0xffb3a8 : 0xc8c8c8 : 0x707070), 9.5);
    NSString* title = [self title] ?: @"";
    const NSSize size = [title sizeWithAttributes:attrs];
    [title drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
}

@end

@interface S3GStandaloneSlider : NSSlider
@end

@implementation S3GStandaloneSlider

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const NSRect bounds = [self bounds];
    const NSRect track = NSMakeRect(bounds.origin.x + 7.0,
        NSMidY(bounds) - 3.0, std::max<CGFloat>(1.0,
            bounds.size.width - 14.0), 6.0);
    const double span = std::max(1.0e-9,
        [self maxValue] - [self minValue]);
    const CGFloat normalized = std::clamp<CGFloat>(
        ([self doubleValue] - [self minValue]) / span, 0.0, 1.0);
    [s3g::clap_gui::color(0x151515) setFill];
    NSRectFill(track);
    [s3g::clap_gui::color(0x565656) setStroke];
    NSFrameRect(track);
    [s3g::clap_gui::color(0x7f7f7f) setFill];
    NSRectFill(NSMakeRect(track.origin.x + 1.0, track.origin.y + 1.0,
        std::max<CGFloat>(0.0, (track.size.width - 2.0) * normalized),
        track.size.height - 2.0));
    const CGFloat thumbX = track.origin.x
        + (track.size.width - 1.0) * normalized;
    const NSRect thumb = NSMakeRect(thumbX - 4.0,
        NSMidY(bounds) - 9.0, 8.0, 18.0);
    [s3g::clap_gui::color(0x303030) setFill];
    NSRectFill(thumb);
    [s3g::clap_gui::color(0xb8b8b8) setStroke];
    NSFrameRect(thumb);
}

@end

@interface S3GStandalonePanelView : NSView
@end

@implementation S3GStandalonePanelView

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    [s3g::clap_gui::color(0x0c0c0c) setFill];
    NSRectFill([self bounds]);
    const NSRect panel = NSInsetRect([self bounds], 12.0, 12.0);
    [s3g::clap_gui::color(0x131313) setFill];
    NSRectFill(panel);
    [s3g::clap_gui::color(0x565656) setStroke];
    NSFrameRect(panel);
}

@end

@interface S3GNoInputStandaloneView : NSView {
    AppState* _state;
    NSView* _pluginContainer;
    NSSegmentedControl* _modeControl;
    NSPopUpButton* _deviceMenu;
    NSPopUpButton* _outputBankMenu;
    NSButton* _audioButton;
    NSButton* _editButton;
    NSButton* _midiButton;
    NSSlider* _tempoSlider;
    NSTextField* _tempoLabel;
    NSTextField* _statusLabel;
    NSTimer* _timer;
    NSPanel* _outputPanel;
    NSView* _outputPluginContainer;
    EmbeddedClapPlugin* _outputEditorPlugin;
    NSPanel* _midiPanel;
    NSView* _midiPanelContent;
    NSPopUpButton* _e16MidiOutputMenu;
    NSPopUpButton* _gridMidiOutputMenu;
    NSPanel* _gesturePanel;
    NSView* _gesturePluginContainer;
    bool _gestureGuiAttached;
}
- (id)initWithFrame:(NSRect)frame state:(AppState*)state;
- (NSView*)pluginContainer;
- (void)refreshDeviceMenu;
- (void)refreshOutputBankMenu;
- (void)refreshControls;
- (void)closeOutputEditor;
- (void)closeMidiPanels;
@end

@implementation S3GNoInputStandaloneView

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    [s3g::clap_gui::color(0x0c0c0c) setFill];
    NSRectFill([self bounds]);
    const NSRect strip = NSMakeRect(8.0,
        [self bounds].size.height - kOutputStripHeight + 7.0,
        [self bounds].size.width - 16.0, kOutputStripHeight - 14.0);
    [s3g::clap_gui::color(0x131313) setFill];
    NSRectFill(strip);
    [s3g::clap_gui::color(0x565656) setStroke];
    NSFrameRect(strip);
    [s3g::clap_gui::color(0x2b2b2b) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(8.0, strip.origin.y - 1.0)
        toPoint:NSMakePoint(NSMaxX(strip), strip.origin.y - 1.0)];
}

- (id)initWithFrame:(NSRect)frame state:(AppState*)state
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _state = state;
    _outputEditorPlugin = nullptr;
    _gestureGuiAttached = false;
    [self setWantsLayer:YES];
    [[self layer] setBackgroundColor:[[NSColor colorWithCalibratedWhite:0.055
        alpha:1.0] CGColor]];

    _pluginContainer = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0,
        frame.size.width, frame.size.height - kOutputStripHeight)];
    [_pluginContainer setAutoresizingMask:NSViewWidthSizable
        | NSViewHeightSizable];
    [self addSubview:_pluginContainer];

    const CGFloat y = frame.size.height - 64.0;
    NSTextField* title = [NSTextField labelWithString:@"OUTPUT"];
    [title setStringValue:@"ROUTE"];
    [title setTextColor:s3g::clap_gui::color(0xb8b8b8)];
    [title setFont:s3g::clap_gui::uiFont(10.5)];
    [title setFrame:NSMakeRect(16.0, y + 18.0, 68.0, 20.0)];
    [title setAutoresizingMask:NSViewMinYMargin];
    [self addSubview:title];

    _modeControl = [[S3GStandaloneModeControl alloc]
        initWithFrame:NSMakeRect(82.0, y + 9.0, 330.0, 34.0)];
    [_modeControl setSegmentCount:3];
    [_modeControl setLabel:@"STEREO · 2CH" forSegment:0];
    [_modeControl setLabel:@"QUAD · 4CH" forSegment:1];
    [_modeControl setLabel:@"DIRECT · 8CH" forSegment:2];
    [_modeControl setTrackingMode:NSSegmentSwitchTrackingSelectOne];
    [_modeControl setTarget:self];
    [_modeControl setAction:@selector(changeMode:)];
    [_modeControl setAutoresizingMask:NSViewMinYMargin];
    [self addSubview:_modeControl];

    _deviceMenu = [[S3GStandalonePopupButton alloc]
        initWithFrame:NSMakeRect(424.0, y + 9.0, 250.0, 34.0)
        pullsDown:NO];
    [_deviceMenu setBordered:NO];
    [_deviceMenu setTarget:self];
    [_deviceMenu setAction:@selector(changeDevice:)];
    [_deviceMenu setAutoresizingMask:NSViewMinYMargin];
    [self addSubview:_deviceMenu];

    _outputBankMenu = [[S3GStandalonePopupButton alloc]
        initWithFrame:NSMakeRect(682.0, y + 9.0, 96.0, 34.0)
        pullsDown:NO];
    [_outputBankMenu setBordered:NO];
    [_outputBankMenu setTarget:self];
    [_outputBankMenu setAction:@selector(changeOutputBank:)];
    [_outputBankMenu setAutoresizingMask:NSViewMinYMargin];
    [self addSubview:_outputBankMenu];

    _editButton = [[S3GStandaloneActionButton alloc]
        initWithFrame:NSMakeRect(786.0, y + 9.0, 100.0, 34.0)];
    [_editButton setTitle:@"EDIT OUTPUT"];
    [_editButton setBordered:NO];
    [_editButton setTarget:self];
    [_editButton setAction:@selector(editOutput:)];
    [_editButton setAutoresizingMask:NSViewMinYMargin];
    [self addSubview:_editButton];

    _audioButton = [[S3GStandaloneActionButton alloc]
        initWithFrame:NSMakeRect(894.0, y + 9.0, 88.0, 34.0)];
    [_audioButton setTitle:@"AUDIO ON"];
    [_audioButton setButtonType:NSButtonTypeToggle];
    [_audioButton setBordered:NO];
    [_audioButton setTag:1];
    [_audioButton setTarget:self];
    [_audioButton setAction:@selector(toggleAudio:)];
    [_audioButton setAutoresizingMask:NSViewMinYMargin];
    [self addSubview:_audioButton];

    NSButton* panic = [[S3GStandaloneActionButton alloc]
        initWithFrame:NSMakeRect(990.0, y + 9.0, 66.0, 34.0)];
    [panic setTitle:@"PANIC"];
    [panic setBordered:NO];
    [panic setTag:2];
    [panic setTarget:self];
    [panic setAction:@selector(panic:)];
    [panic setAutoresizingMask:NSViewMinYMargin];
    [self addSubview:panic];
    [panic release];

    _tempoSlider = [[S3GStandaloneSlider alloc]
        initWithFrame:NSMakeRect(1068.0, y + 9.0, 136.0, 34.0)];
    [_tempoSlider setMinValue:20.0];
    [_tempoSlider setMaxValue:300.0];
    [_tempoSlider setDoubleValue:_state->engine.tempo()];
    [_tempoSlider setTarget:self];
    [_tempoSlider setAction:@selector(changeTempo:)];
    [_tempoSlider setContinuous:YES];
    [_tempoSlider setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [self addSubview:_tempoSlider];

    _tempoLabel = [[NSTextField labelWithString:@"120 BPM"] retain];
    [_tempoLabel setAlignment:NSTextAlignmentRight];
    [_tempoLabel setTextColor:s3g::clap_gui::color(0xa8a8a8)];
    [_tempoLabel setFont:s3g::clap_gui::uiFont(10.0)];
    [_tempoLabel setFrame:NSMakeRect(1210.0, y + 17.0, 74.0, 20.0)];
    [_tempoLabel setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [self addSubview:_tempoLabel];

    _midiButton = [[S3GStandaloneActionButton alloc]
        initWithFrame:NSMakeRect(1292.0, y + 9.0, 52.0, 34.0)];
    [_midiButton setTitle:@"MIDI"];
    [_midiButton setBordered:NO];
    [_midiButton setTarget:self];
    [_midiButton setAction:@selector(showMidiSetup:)];
    [_midiButton setAutoresizingMask:NSViewMinXMargin | NSViewMinYMargin];
    [self addSubview:_midiButton];

    _statusLabel = [[NSTextField labelWithString:@""] retain];
    [_statusLabel setTextColor:s3g::clap_gui::color(0x8f8f8f)];
    [_statusLabel setFont:s3g::clap_gui::uiFont(10.0)];
    [_statusLabel setFrame:NSMakeRect(82.0, y - 12.0, 1200.0, 18.0)];
    [_statusLabel setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    [self addSubview:_statusLabel];

    [self refreshDeviceMenu];
    [self refreshOutputBankMenu];
    [self refreshControls];
    _timer = [[NSTimer scheduledTimerWithTimeInterval:1.0 / 20.0 target:self
        selector:@selector(refreshTimer:) userInfo:nil repeats:YES] retain];
    return self;
}

- (void)dealloc
{
    [_timer invalidate];
    [_timer release];
    [self closeOutputEditor];
    [self closeMidiPanels];
    [_pluginContainer release];
    [_modeControl release];
    [_deviceMenu release];
    [_outputBankMenu release];
    [_audioButton release];
    [_editButton release];
    [_midiButton release];
    [_tempoSlider release];
    [_tempoLabel release];
    [_statusLabel release];
    [super dealloc];
}

- (NSView*)pluginContainer { return _pluginContainer; }

- (void)refreshDeviceMenu
{
    [_deviceMenu removeAllItems];
    for (uint32_t index = 0u; index < _state->devices.size(); ++index) {
        const auto& device = _state->devices[index];
        NSString* title = [NSString stringWithFormat:@"%s — %u ch",
            device.name.c_str(), device.outputChannels];
        [_deviceMenu addItemWithTitle:title];
        [[_deviceMenu lastItem] setTag:index];
    }
    if (_state->selectedDevice < _state->devices.size())
        [_deviceMenu selectItemAtIndex:_state->selectedDevice];
}

- (void)refreshOutputBankMenu
{
    [_outputBankMenu removeAllItems];
    const auto mode = _state->engine.outputMode();
    const uint32_t required = outputChannelsForMode(mode);
    const uint32_t available = _state->selectedDevice < _state->devices.size()
        ? _state->devices[_state->selectedDevice].outputChannels : 0u;
    if (available < required) {
        [_outputBankMenu addItemWithTitle:@"OUT unavailable"];
        [_outputBankMenu setEnabled:NO];
        return;
    }

    clampOutputChannelOffset(*_state, mode);
    const uint32_t selectedOffset = _state->engine.outputChannelOffset(mode);
    for (uint32_t offset = 0u; offset + required <= available;
         offset += required) {
        NSString* title = [NSString stringWithFormat:@"OUT %u–%u",
            offset + 1u, offset + required];
        [_outputBankMenu addItemWithTitle:title];
        [[_outputBankMenu lastItem] setTag:static_cast<NSInteger>(offset)];
        if (offset == selectedOffset)
            [_outputBankMenu selectItem:[_outputBankMenu lastItem]];
    }
    [_outputBankMenu setEnabled:_state->deviceOpen.load(
        std::memory_order_acquire)];
}

- (void)refreshControls
{
    const auto mode = _state->engine.outputMode();
    [_modeControl setSelectedSegment:static_cast<NSInteger>(mode)];
    uint32_t available = 0u;
    if (_state->selectedDevice < _state->devices.size())
        available = _state->devices[_state->selectedDevice].outputChannels;
    [_modeControl setEnabled:available >= 2u forSegment:0];
    [_modeControl setEnabled:available >= 4u forSegment:1];
    [_modeControl setEnabled:available >= 8u forSegment:2];
    [_editButton setEnabled:mode != NoInputOutputMode::DirectEight];
    [_audioButton setState:_state->engine.audioEnabled()
        ? NSControlStateValueOn : NSControlStateValueOff];
    [_audioButton setTitle:_state->engine.audioEnabled()
        ? @"AUDIO OFF" : @"AUDIO ON"];
    [_tempoLabel setStringValue:[NSString stringWithFormat:@"%.0f BPM",
        _state->engine.tempo()]];

    const auto& config = _state->audio.config();
    const uint32_t routedChannels = outputChannelsForMode(mode);
    const uint32_t outputOffset = _state->engine.outputChannelOffset(mode);
    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < routedChannels; ++channel)
        peak = std::max(peak, _state->engine.outputPeak(channel));
    NSString* modeName = mode == NoInputOutputMode::StereoAutogain
        ? @"STEREO AUTOGAIN" : mode == NoInputOutputMode::QuadAutogain
            ? @"QUAD AUTOGAIN" : @"DIRECT 8";
    if (_state->deviceOpen.load(std::memory_order_acquire)) {
        [_statusLabel setStringValue:[NSString stringWithFormat:
            @"%@  •  OUT %u–%u  •  %.0f Hz  •  %u hardware channels  •  peak %.3f  •  %@",
            modeName, outputOffset + 1u, outputOffset + routedChannels,
            config.sampleRate, config.renderChannels, peak,
            _state->engine.audioEnabled() ? @"LIVE" : @"SAFE MUTED"]];
    } else {
        [_statusLabel setStringValue:[NSString stringWithFormat:
            @"AUDIO ERROR  •  %s", _state->audioError.c_str()]];
    }
}

- (void)refreshTimer:(NSTimer*)timer
{
    (void)timer;
    _state->engine.noInputPlugin().serviceMainThreadCallback();
    _state->engine.gesturePlugin().serviceMainThreadCallback();
    _state->engine.stereoPlugin().serviceMainThreadCallback();
    _state->engine.quadPlugin().serviceMainThreadCallback();
    drainMidiOutput(*_state);
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (_state->engine.noInputPlugin().takeGuiResizeRequest(width, height)
        && [self window]) {
        [[self window] setContentSize:NSMakeSize(width,
            height + kOutputStripHeight)];
    }
    if (_outputEditorPlugin && _outputPanel
        && _outputEditorPlugin->takeGuiResizeRequest(width, height)) {
        [_outputPanel setContentSize:NSMakeSize(width, height)];
    }
    if (_gestureGuiAttached && _gesturePanel
        && _state->engine.gesturePlugin().takeGuiResizeRequest(width, height)) {
        [_gesturePanel setContentSize:NSMakeSize(width, height)];
    }
    [self refreshControls];
}

- (void)changeMode:(NSSegmentedControl*)sender
{
    const NSInteger selected = [sender selectedSegment];
    if (selected < 0 || selected > 2) return;
    const auto mode = static_cast<NoInputOutputMode>(selected);
    uint32_t available = 0u;
    if (_state->selectedDevice < _state->devices.size())
        available = _state->devices[_state->selectedDevice].outputChannels;
    if (available < outputChannelsForMode(mode)) {
        [self refreshControls];
        return;
    }
    _state->engine.setOutputMode(mode);
    clampOutputChannelOffset(*_state, mode);
    [self refreshOutputBankMenu];
    if (_outputPanel && [_outputPanel isVisible]) {
        [self closeOutputEditor];
        if (mode != NoInputOutputMode::DirectEight) [self editOutput:nil];
    }
    [self refreshControls];
}

- (void)changeDevice:(NSPopUpButton*)sender
{
    const NSInteger selected = [sender indexOfSelectedItem];
    if (selected < 0 || static_cast<uint32_t>(selected)
        >= _state->devices.size()) return;
    _state->selectedDevice = static_cast<uint32_t>(selected);
    const uint32_t available = _state->devices[_state->selectedDevice]
        .outputChannels;
    if (available < outputChannelsForMode(_state->engine.outputMode())) {
        _state->engine.setOutputMode(available >= 4u
            ? NoInputOutputMode::QuadAutogain
            : NoInputOutputMode::StereoAutogain);
    }
    openSelectedAudioDevice(*_state);
    [self refreshOutputBankMenu];
    [self refreshControls];
}

- (void)changeOutputBank:(NSPopUpButton*)sender
{
    NSMenuItem* selected = [sender selectedItem];
    if (!selected) return;
    _state->engine.setOutputChannelOffset(_state->engine.outputMode(),
        static_cast<uint32_t>(std::max<NSInteger>(0, [selected tag])));
    [self refreshControls];
}

- (void)toggleAudio:(NSButton*)sender
{
    (void)sender;
    if (_state->deviceOpen.load(std::memory_order_acquire))
        _state->engine.setAudioEnabled(!_state->engine.audioEnabled());
    [self refreshControls];
}

- (void)panic:(NSButton*)sender
{
    (void)sender;
    _state->engine.requestPanic();
    _state->engine.setAudioEnabled(false);
    [self refreshControls];
}

- (void)changeTempo:(NSSlider*)sender
{
    _state->engine.setTempo([sender doubleValue]);
    [self refreshControls];
}

- (void)refreshMidiOutputMenus
{
    auto populate = [&](NSPopUpButton* menu, uint32_t selected) {
        if (!menu) return;
        [menu removeAllItems];
        [menu addItemWithTitle:@"FEEDBACK OFF"];
        [[menu lastItem] setTag:0];
        for (uint32_t index = 0u; index < _state->midiDestinations.size();
             ++index) {
            NSString* title = [NSString stringWithUTF8String:
                _state->midiDestinations[index].name.c_str()];
            [menu addItemWithTitle:title];
            [[menu lastItem] setTag:index + 1u];
            if (index == selected) [menu selectItem:[menu lastItem]];
        }
    };
    populate(_e16MidiOutputMenu, _state->selectedE16MidiDestination);
    populate(_gridMidiOutputMenu, _state->selectedGridMidiDestination);
}

- (void)showMidiSetup:(id)sender
{
    (void)sender;
    if (_midiPanel) {
        [_midiPanel makeKeyAndOrderFront:nil];
        return;
    }
    const NSRect frame = NSMakeRect(0.0, 0.0, 620.0, 320.0);
    _midiPanel = [[NSPanel alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    [_midiPanel setReleasedWhenClosed:NO];
    [_midiPanel setTitle:@"No Input Mixer MIDI"];
    _midiPanelContent = [[S3GStandalonePanelView alloc] initWithFrame:frame];
    [_midiPanel setContentView:_midiPanelContent];

    auto addLabel = [&](NSString* text, NSRect rect, int color, CGFloat size) {
        NSTextField* label = [NSTextField labelWithString:text];
        [label setTextColor:s3g::clap_gui::color(color)];
        [label setFont:s3g::clap_gui::uiFont(size)];
        [label setFrame:rect];
        [_midiPanelContent addSubview:label];
    };
    addLabel(@"MIDI ROUTING", NSMakeRect(28.0, 277.0, 200.0, 18.0),
        0xc8c8c8, 11.0);
    addLabel(@"INPUT", NSMakeRect(28.0, 240.0, 130.0, 18.0),
        0xa8a8a8, 10.0);
    addLabel([NSString stringWithFormat:@"ALL CONNECTED SOURCES  •  %u",
        _state->connectedMidiSourceCount],
        NSMakeRect(180.0, 240.0, 400.0, 18.0), 0xc8c8c8, 10.0);
    addLabel(@"BU16 MATRIX", NSMakeRect(28.0, 211.0, 130.0, 18.0),
        0xa8a8a8, 10.0);
    addLabel(@"CH 1–4  •  NOTES 0–15  •  FLIP / LATCH  •  SHARED RAMP",
        NSMakeRect(180.0, 211.0, 410.0, 18.0), 0x929292, 9.5);
    addLabel(@"GRID FEEDBACK", NSMakeRect(28.0, 170.0, 130.0, 18.0),
        0xa8a8a8, 10.0);

    _gridMidiOutputMenu = [[S3GStandalonePopupButton alloc]
        initWithFrame:NSMakeRect(180.0, 162.0, 400.0, 34.0)
        pullsDown:NO];
    [_gridMidiOutputMenu setBordered:NO];
    [_gridMidiOutputMenu setTarget:self];
    [_gridMidiOutputMenu setAction:@selector(changeGridMidiOutput:)];
    [_midiPanelContent addSubview:_gridMidiOutputMenu];

    addLabel(@"E16 FEEDBACK", NSMakeRect(28.0, 116.0, 130.0, 18.0),
        0xa8a8a8, 10.0);
    _e16MidiOutputMenu = [[S3GStandalonePopupButton alloc]
        initWithFrame:NSMakeRect(180.0, 108.0, 400.0, 34.0)
        pullsDown:NO];
    [_e16MidiOutputMenu setBordered:NO];
    [_e16MidiOutputMenu setTarget:self];
    [_e16MidiOutputMenu setAction:@selector(changeE16MidiOutput:)];
    [_midiPanelContent addSubview:_e16MidiOutputMenu];
    [self refreshMidiOutputMenus];

    NSButton* gestures = [[S3GStandaloneActionButton alloc]
        initWithFrame:NSMakeRect(28.0, 34.0, 150.0, 34.0)];
    [gestures setTitle:@"EDIT NIM GESTURES"];
    [gestures setBordered:NO];
    [gestures setTarget:self];
    [gestures setAction:@selector(editGestures:)];
    [_midiPanelContent addSubview:gestures];
    [gestures release];
    addLabel(@"E16 USB THRU MUST BE OFF WHEN FEEDBACK IS ENABLED",
        NSMakeRect(196.0, 42.0, 390.0, 18.0), 0xb56c61, 9.5);

    [_midiPanel center];
    [_midiPanel makeKeyAndOrderFront:nil];
}

- (void)changeGridMidiOutput:(NSPopUpButton*)sender
{
    const NSInteger tag = [[sender selectedItem] tag];
    _state->selectedGridMidiDestination = tag <= 0
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(tag - 1);
}

- (void)changeE16MidiOutput:(NSPopUpButton*)sender
{
    const NSInteger tag = [[sender selectedItem] tag];
    _state->selectedE16MidiDestination = tag <= 0
        ? std::numeric_limits<uint32_t>::max()
        : static_cast<uint32_t>(tag - 1);
}

- (void)editGestures:(id)sender
{
    (void)sender;
    if (_gesturePanel) {
        [_gesturePanel makeKeyAndOrderFront:nil];
        return;
    }
    _gesturePanel = [[NSPanel alloc] initWithContentRect:NSMakeRect(
        0.0, 0.0, 1180.0, 820.0)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    [_gesturePanel setReleasedWhenClosed:NO];
    [_gesturePanel setTitle:@"NIM Gesture Recorder"];
    _gesturePluginContainer = [[NSView alloc] initWithFrame:NSMakeRect(
        0.0, 0.0, 1180.0, 820.0)];
    [_gesturePluginContainer setAutoresizingMask:NSViewWidthSizable
        | NSViewHeightSizable];
    [_gesturePanel setContentView:_gesturePluginContainer];
    _gestureGuiAttached = attachPluginGui(_state->engine.gesturePlugin(),
        _gesturePluginContainer);
    if (!_gestureGuiAttached) {
        [_gesturePanel release];
        _gesturePanel = nil;
        [_gesturePluginContainer release];
        _gesturePluginContainer = nil;
        return;
    }
    [_gesturePanel center];
    [_gesturePanel makeKeyAndOrderFront:nil];
}

- (void)closeMidiPanels
{
    if (_gestureGuiAttached)
        detachPluginGui(_state->engine.gesturePlugin());
    _gestureGuiAttached = false;
    if (_gesturePanel) [_gesturePanel orderOut:nil];
    [_gesturePanel release];
    _gesturePanel = nil;
    [_gesturePluginContainer release];
    _gesturePluginContainer = nil;
    if (_midiPanel) [_midiPanel orderOut:nil];
    [_midiPanel release];
    _midiPanel = nil;
    [_e16MidiOutputMenu release];
    _e16MidiOutputMenu = nil;
    [_gridMidiOutputMenu release];
    _gridMidiOutputMenu = nil;
    [_midiPanelContent release];
    _midiPanelContent = nil;
}

- (void)editOutput:(id)sender
{
    (void)sender;
    const auto mode = _state->engine.outputMode();
    if (mode == NoInputOutputMode::DirectEight) return;
    [self closeOutputEditor];
    _outputEditorPlugin = mode == NoInputOutputMode::QuadAutogain
        ? &_state->engine.quadPlugin() : &_state->engine.stereoPlugin();
    _outputPanel = [[NSPanel alloc] initWithContentRect:NSMakeRect(
        0.0, 0.0, 920.0, 560.0)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    [_outputPanel setReleasedWhenClosed:NO];
    [_outputPanel setTitle:mode == NoInputOutputMode::QuadAutogain
        ? @"Quad Autogain Output" : @"Stereo Autogain Output"];
    _outputPluginContainer = [[NSView alloc] initWithFrame:NSMakeRect(
        0.0, 0.0, 920.0, 560.0)];
    [_outputPluginContainer setAutoresizingMask:NSViewWidthSizable
        | NSViewHeightSizable];
    [_outputPanel setContentView:_outputPluginContainer];
    if (!attachPluginGui(*_outputEditorPlugin, _outputPluginContainer)) {
        [self closeOutputEditor];
        return;
    }
    [_outputPanel center];
    [_outputPanel makeKeyAndOrderFront:nil];
}

- (void)closeOutputEditor
{
    if (_outputEditorPlugin) detachPluginGui(*_outputEditorPlugin);
    _outputEditorPlugin = nullptr;
    if (_outputPanel) [_outputPanel orderOut:nil];
    [_outputPanel release];
    _outputPanel = nil;
    [_outputPluginContainer release];
    _outputPluginContainer = nil;
}

@end

@interface S3GNoInputAppDelegate : NSObject <NSApplicationDelegate> {
    AppState* _state;
    NSWindow* _window;
    S3GNoInputStandaloneView* _rootView;
    bool _mainGuiAttached;
}
@end

@implementation S3GNoInputAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    (void)notification;
    _state = new AppState();
    if (!_state->engine.create(&s3g_no_input_mixer_embedded_entry,
            &s3g_nim_gesture_embedded_entry,
            &s3g_mc_to_stereo_autogain_embedded_entry,
            &s3g_mc_to_quad_autogain_embedded_entry)) {
        [NSApp terminate:nil];
        return;
    }
    restoreProcessorState(*_state);
    _state->devices = CoreAudioOutput::enumerateDevices();
    const AudioDeviceID defaultDevice = CoreAudioOutput::defaultOutputDevice();
    NSString* savedUid = [[NSUserDefaults standardUserDefaults]
        stringForKey:@"OutputDeviceUID"];
    bool foundDevice = false;
    if (savedUid && [savedUid length] > 0u) {
        for (uint32_t index = 0u; index < _state->devices.size(); ++index) {
            if (_state->devices[index].uid == [savedUid UTF8String]) {
                _state->selectedDevice = index;
                foundDevice = true;
                break;
            }
        }
    }
    if (!foundDevice) {
        for (uint32_t index = 0u; index < _state->devices.size(); ++index) {
            if (_state->devices[index].id != defaultDevice) continue;
            _state->selectedDevice = index;
            foundDevice = true;
            break;
        }
    }
    if (!_state->devices.empty()) {
        const uint32_t available = _state->devices[_state->selectedDevice]
            .outputChannels;
        if (available < outputChannelsForMode(_state->engine.outputMode()))
            _state->engine.setOutputMode(available >= 4u
                ? NoInputOutputMode::QuadAutogain
                : NoInputOutputMode::StereoAutogain);
        openSelectedAudioDevice(*_state);
    } else {
        _state->audioError = "No Core Audio output devices found";
    }
    connectMidiInputs(*_state);

    const NSRect frame = NSMakeRect(0.0, 0.0, kNativePluginWidth,
        kNativePluginHeight + kOutputStripHeight);
    _window = [[NSWindow alloc] initWithContentRect:frame
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
            | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    [_window setTitle:@"s3g No Input Mixer"];
    [_window setMinSize:NSMakeSize(720.0, 480.0)];
    _rootView = [[S3GNoInputStandaloneView alloc] initWithFrame:frame
        state:_state];
    [_window setContentView:_rootView];
    _mainGuiAttached = attachPluginGui(_state->engine.noInputPlugin(),
        [_rootView pluginContainer]);
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    (void)notification;
    if (!_state) return;
    _state->engine.setAudioEnabled(false);
    _state->deviceOpen.store(false, std::memory_order_release);
    _state->audio.close();
    _state->engine.release();
    saveProcessorState(*_state);
    [_rootView closeOutputEditor];
    [_rootView closeMidiPanels];
    if (_mainGuiAttached) detachPluginGui(_state->engine.noInputPlugin());
    _mainGuiAttached = false;
    if (_state->midiInputPort) MIDIPortDispose(_state->midiInputPort);
    if (_state->midiOutputPort) MIDIPortDispose(_state->midiOutputPort);
    if (_state->midiClient) MIDIClientDispose(_state->midiClient);
    _state->engine.destroy();
    delete _state;
    _state = nullptr;
    [_rootView release];
    _rootView = nil;
    [_window release];
    _window = nil;
}

@end

int main(int argc, const char* argv[])
{
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSMenu* menuBar = [[[NSMenu alloc] init] autorelease];
        NSMenuItem* appMenuItem = [[[NSMenuItem alloc] init] autorelease];
        [menuBar addItem:appMenuItem];
        [application setMainMenu:menuBar];
        NSMenu* appMenu = [[[NSMenu alloc] initWithTitle:
            @"s3g No Input Mixer"] autorelease];
        NSMenuItem* quit = [[[NSMenuItem alloc] initWithTitle:
            @"Quit s3g No Input Mixer" action:@selector(terminate:)
            keyEquivalent:@"q"] autorelease];
        [appMenu addItem:quit];
        [appMenuItem setSubmenu:appMenu];

        S3GNoInputAppDelegate* delegate =
            [[[S3GNoInputAppDelegate alloc] init] autorelease];
        [application setDelegate:delegate];
        [application run];
    }
    return 0;
}
