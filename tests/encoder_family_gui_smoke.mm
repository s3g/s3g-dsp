#import <Cocoa/Cocoa.h>

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include "../plugins/common/s3g_cocoa_gui.h"
#include "../dsp/s3g_ambi_cryosphere_encoder.h"
#include "../dsp/s3g_ambi_insect_encoder.h"
#include "../dsp/s3g_ambi_pyrosphere_encoder.h"
#include "../dsp/s3g_ambi_stochastic_encoder.h"
#include "../dsp/s3g_ambi_water_encoder.h"
#include "../dsp/s3g_ambi_wind_encoder.h"
#include "../dsp/s3g_ambi_wrangler_encoder.h"
#include "../dsp/s3g_musical_scales.h"
#include "../dsp/s3g_parameter_surface.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <vector>

// Optional documentation-only selectors exposed by specific native views.
@interface NSView (S3GDocumentationCapture)
- (void)loadAtlasAtIndex:(NSUInteger)index;
- (void)loadDocumentationScore;
- (void)loadDocumentationPaths;
- (BOOL)loadDocumentationBreaks;
- (BOOL)loadDocumentationSample;
- (void)setDocumentationPage:(NSUInteger)page;
- (void)setViewPreset:(int)mode;
- (NSPoint)projectWorldPointX:(double)x y:(double)y z:(double)z;
- (NSPoint)projectGroundPointX:(double)x y:(double)y;
- (void)setDocumentationViewAzimuth:(double)azimuth elevation:(double)elevation;
- (void)textDidChange:(NSNotification*)notification;
- (void)refresh:(NSTimer*)timer;
- (void)captureDocumentationHistorySample;
@end

namespace {

struct HostContext {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    bool servicingParamFlush = false;
    bool deferParamFlush = false;
    bool paramFlushRequested = false;
    bool callbackRequested = false;
    bool processRequested = false;
    uint32_t paramRescanCount = 0u;
};

void hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags flags)
{
    auto* context = host
        ? static_cast<HostContext*>(host->host_data) : nullptr;
    if (context && (flags & CLAP_PARAM_RESCAN_VALUES) != 0u) {
        ++context->paramRescanCount;
    }
}
void hostParamsClear(const clap_host_t*, clap_id,
    clap_param_clear_flags) {}

void hostParamsRequestFlush(const clap_host_t* host)
{
    auto* context = host
        ? static_cast<HostContext*>(host->host_data) : nullptr;
    if (!context || !context->plugin || !context->params
        || !context->params->flush) return;
    context->paramFlushRequested = true;
    if (context->deferParamFlush || context->servicingParamFlush) return;
    context->servicingParamFlush = true;
    context->paramFlushRequested = false;
    context->params->flush(context->plugin, nullptr, nullptr);
    context->servicingParamFlush = false;
}

const clap_host_params_t hostParams {
    hostParamsRescan,
    hostParamsClear,
    hostParamsRequestFlush,
};

const void* hostGetExtension(const clap_host_t*, const char* id)
{
    return id && std::strcmp(id, CLAP_EXT_PARAMS) == 0
        ? &hostParams : nullptr;
}
void hostRequest(const clap_host_t*) {}

void hostRequestProcess(const clap_host_t* host)
{
    auto* context = host
        ? static_cast<HostContext*>(host->host_data) : nullptr;
    if (context) context->processRequested = true;
}

void hostRequestCallback(const clap_host_t* host)
{
    auto* context = host
        ? static_cast<HostContext*>(host->host_data) : nullptr;
    if (context) context->callbackRequested = true;
}

struct CapturedParamEvent {
    uint16_t type = 0u;
    clap_id paramId = CLAP_INVALID_ID;
    double value = 0.0;
};

struct CapturedOutputEvents {
    clap_output_events_t events {};
    std::vector<CapturedParamEvent> values;
    size_t capacity = std::numeric_limits<size_t>::max();
};

bool captureOutputEvent(const clap_output_events_t* events,
    const clap_event_header_t* header)
{
    if (!events || !header
        || header->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
    auto* capture = static_cast<CapturedOutputEvents*>(events->ctx);
    if (!capture) return false;
    if (capture->values.size() >= capture->capacity) return false;
    CapturedParamEvent result {};
    result.type = header->type;
    if (header->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* value = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        result.paramId = value->param_id;
        result.value = value->value;
    } else if (header->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
        || header->type == CLAP_EVENT_PARAM_GESTURE_END) {
        result.paramId = reinterpret_cast<
            const clap_event_param_gesture_t*>(header)->param_id;
    } else {
        return true;
    }
    capture->values.push_back(result);
    return true;
}

bool closeEnough(CGFloat a, CGFloat b)
{
    return std::fabs(a - b) < 0.5;
}

struct MemoryPluginState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

template <typename Params>
struct WorldSphereSavedState {
    uint32_t version = 0u;
    Params params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    s3g::ParameterSurfaceState<Params> surface {};
};

struct StochasticSavedState {
    uint32_t version = 0u;
    s3g::AmbiStochasticParams params {};
    int32_t factoryPresetIndex = 0;
    char presetName[64] {};
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    s3g::ParameterSurfaceState<s3g::AmbiStochasticParams> surface {};
};

// Frozen state fixtures used only by documentation capture. Loading them
// through CLAP state exercises the same public path a host session uses while
// keeping sample-file knowledge out of the production plug-ins.
struct DocumentationLoopState {
    uint32_t version = 6u;
    double baseRate = 0.90;
    double rateSpread = 0.55;
    double driftAmount = 0.06;
    double relationCenter = 0.46;
    double relationGlideMs = 180.0;
    double loopStart = 0.12;
    double loopLength = 0.66;
    double xfadePct = 0.10;
    double seamDuck = 0.18;
    double gainDb = -9.0;
    uint32_t launchMode = 0u;
    uint32_t laneMask = 0xffu;
    uint32_t playing = 1u;
    char samplePath[1024] {};
};

struct DocumentationMultiLoopState {
    uint32_t version = 8u;
    double baseRate = 0.92;
    double rateSpread = 0.48;
    double driftAmount = 0.045;
    double relationCenter = 0.43;
    double relationGlideMs = 180.0;
    double loopStart = 0.08;
    double loopLength = 0.76;
    double xfadePct = 0.10;
    double seamDuck = 0.20;
    double gainDb = -10.0;
    double sourceRateSpread = 0.35;
    double sourceBlend = 0.72;
    double midiMode = 0.0;
    double midiRoot = 60.0;
    uint32_t launchMode = 0u;
    uint32_t laneMask = 0xffu;
    uint32_t rule = 3u;
    uint32_t playing = 1u;
    char samplePaths[4][1024] {};
};

struct DocumentationAmbiGrainState {
    uint32_t version = 1u;
    double order = 3.0;
    double mode = 1.0;
    double density = 100.0;
    double grainMs = 220.0;
    double sourcePosition = 0.33;
    double scanSpeed = 0.65;
    double positionJitter = 0.28;
    double rate = 0.78;
    double rateJitter = 0.16;
    double reverse = 0.20;
    double freeze = 0.58;
    double jumpSteps = 12.0;
    double gainDb = -10.0;
    double sync = 1.0;
    double envelope = 2.0;
    uint32_t playing = 1u;
    char samplePath[1024] {};
};

static_assert(sizeof(DocumentationLoopState) == 1128u,
              "Loop Processor documentation state fixture changed");
static_assert(sizeof(DocumentationMultiLoopState) == 4232u,
              "Multi Loop Processor documentation state fixture changed");
static_assert(sizeof(DocumentationAmbiGrainState) == 1160u,
              "Ambi Grain documentation state fixture changed");

bool decodeStochasticState(const MemoryPluginState& memory,
                           StochasticSavedState& decoded)
{
    if (memory.bytes.size() != sizeof(decoded)) return false;
    std::memcpy(&decoded, memory.bytes.data(), sizeof(decoded));
    return true;
}

template <typename Params>
bool decodeWorldSphereState(const MemoryPluginState& memory,
                            WorldSphereSavedState<Params>& decoded)
{
    if (memory.bytes.size() != sizeof(decoded)) return false;
    std::memcpy(&decoded, memory.bytes.data(), sizeof(decoded));
    return true;
}

struct SingleParamEventInput {
    clap_input_events_t events {};
    clap_event_param_value_t value {};
};

uint32_t singleParamEventSize(const clap_input_events_t*)
{
    return 1u;
}

const clap_event_header_t* singleParamEventGet(
    const clap_input_events_t* events, uint32_t index)
{
    if (!events || index != 0u) return nullptr;
    const auto* input = static_cast<const SingleParamEventInput*>(events->ctx);
    return input ? &input->value.header : nullptr;
}

void setSingleParamEvent(
    SingleParamEventInput& input, clap_id paramId, double value)
{
    input.events.ctx = &input;
    input.events.size = singleParamEventSize;
    input.events.get = singleParamEventGet;
    input.value = {};
    input.value.header.size = sizeof(input.value);
    input.value.header.time = 0u;
    input.value.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    input.value.header.type = CLAP_EVENT_PARAM_VALUE;
    input.value.param_id = paramId;
    input.value.note_id = -1;
    input.value.port_index = -1;
    input.value.channel = -1;
    input.value.key = -1;
    input.value.value = value;
}

struct SingleNoteEventInput {
    clap_input_events_t events {};
    clap_event_note_t value {};
};

uint32_t singleNoteEventSize(const clap_input_events_t*)
{
    return 1u;
}

const clap_event_header_t* singleNoteEventGet(
    const clap_input_events_t* events, uint32_t index)
{
    if (!events || index != 0u) return nullptr;
    const auto* input = static_cast<const SingleNoteEventInput*>(events->ctx);
    return input ? &input->value.header : nullptr;
}

void setSingleNoteOnEvent(SingleNoteEventInput& input, int16_t key)
{
    input.events.ctx = &input;
    input.events.size = singleNoteEventSize;
    input.events.get = singleNoteEventGet;
    input.value = {};
    input.value.header.size = sizeof(input.value);
    input.value.header.time = 0u;
    input.value.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    input.value.header.type = CLAP_EVENT_NOTE_ON;
    input.value.note_id = -1;
    input.value.port_index = 0;
    input.value.channel = 0;
    input.value.key = key;
    input.value.velocity = 0.78;
}

// Frozen public-state fixture for Delay Processor v10. Keeping the previous
// payload here makes migration coverage independent of the plug-in's current
// C++ type and exercises the same byte stream a host session provides.
struct __attribute__((packed)) DelayProcessorStateV10 {
    uint32_t version = 10u;
    uint64_t patchRows[64] {};
    uint32_t clearUnused = 0u;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double tapAmount = 0.0;
    double outputTrimDb = -6.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0u;
    uint32_t topologyMotionMode = 0u;
    uint32_t topologyMotionVariant = 0u;
    double topologyMotionRateHz = 0.10;
    double topologyMotionDepth = 0.0;
    uint32_t topologyNeighborCount = 2u;
    double topologyRadius = 0.65;
    double topologyCentroid = 0.22;
};

static_assert(sizeof(DelayProcessorStateV10) == 704u,
              "Delay Processor v10 state fixture changed");

int64_t stateWrite(const clap_ostream_t* stream,
                   const void* source,
                   uint64_t requested)
{
    auto* state = static_cast<MemoryPluginState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    if (requested == 0u) return 0;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 13u);
    const auto* first = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), first, first + count);
    return static_cast<int64_t>(count);
}

int64_t stateWriteWhole(const clap_ostream_t* stream,
                        const void* source,
                        uint64_t requested)
{
    auto* state = static_cast<MemoryPluginState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    if (requested == 0u) return 0;
    const auto* first = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), first, first + requested);
    return static_cast<int64_t>(requested);
}

int64_t stateRead(const clap_istream_t* stream,
                  void* destination,
                  uint64_t requested)
{
    auto* state = static_cast<MemoryPluginState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset
        : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 11u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

int64_t stateReadWhole(const clap_istream_t* stream,
                       void* destination,
                       uint64_t requested)
{
    auto* state = static_cast<MemoryPluginState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset
        : 0u;
    const size_t count = std::min<size_t>(
        available, static_cast<size_t>(requested));
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5 && argc != 7) {
        std::cerr
            << "usage: s3g_encoder_family_gui_smoke <plugin binary> <plugin id>"
            << " <native width> <native height>"
            << " [host-name prefix responsive|responsive-wide|dynamic|fixed]\n";
        return 2;
    }

    const char* pluginId = argv[2];
    const uint32_t nativeWidth = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
    const uint32_t nativeHeight = static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10));
    const char* expectedNamePrefix =
        argc == 7 ? argv[5] : "s3g Ambi Encoder ";
    const bool responsiveWide =
        argc == 7 && std::strcmp(argv[6], "responsive-wide") == 0;
    const bool responsive = argc != 7
        || std::strcmp(argv[6], "responsive") == 0
        || responsiveWide;
    const bool dynamic = argc == 7 && std::strcmp(argv[6], "dynamic") == 0;
    const bool fixed = argc == 7 && std::strcmp(argv[6], "fixed") == 0;
    const char* documentationCaptureValue = std::getenv(
        "S3G_GUI_DOCUMENTATION_CAPTURE");
    const bool documentationCapture = documentationCaptureValue
        && documentationCaptureValue[0]
        && std::strcmp(documentationCaptureValue, "0") != 0;
    const bool breakbeatSlicer = std::strcmp(
        pluginId, "org.s3g.s3g-dsp.breakbeat-slicer") == 0;
    if ((!responsive && !dynamic && !fixed)
        || nativeWidth < 320u
        || nativeHeight < ((responsive || dynamic) ? 360u : 240u)) {
        return 2;
    }

    @autoreleasepool {
        const char* failureStage = "shared text helpers";
        NSString* fittedValue = s3g::clap_gui::sliderValueTextToFit(
            @"12.34567 dB", 36.0, s3g::clap_gui::softValueAttrs());
        if ([fittedValue sizeWithAttributes:s3g::clap_gui::softValueAttrs()].width > 36.0) {
            std::cerr << "Shared slider value fitting exceeded its value cell\n";
            return 1;
        }
        NSDictionary* menuAttrs = s3g::clap_gui::softValueAttrs();
        NSString* uppercaseMenu = s3g::clap_gui::menuDisplayText(
            @"Virtual field", 180.0, menuAttrs);
        NSString* compactMenu = s3g::clap_gui::menuDisplayText(
            @"Energy-normalized cardioid", 76.0, menuAttrs);
        if (![uppercaseMenu isEqualToString:@"VIRTUAL FIELD"]
            || ![compactMenu isEqualToString:[compactMenu uppercaseString]]
            || [compactMenu sizeWithAttributes:menuAttrs].width > 76.0) {
            std::cerr << "Shared menu capitalization or fitting contract failed\n";
            return 1;
        }
        for (const auto& scale : s3g::kMusicalScales) {
            NSString* name = [NSString stringWithUTF8String:scale.name];
            if ([name sizeWithAttributes:menuAttrs].width > 162.0) {
                std::cerr << "Musical scale name exceeds its menu column: "
                    << scale.name << "\n";
                return 1;
            }
        }
        const NSRect topologyField = NSMakeRect(
            12.0, 34.0, 620.0, 638.0);
        const NSRect topologyContent =
            s3g::clap_gui::topologyProcessorFieldContentRect(
                topologyField);
        const NSRect firstPageButton =
            s3g::clap_gui::topologyProcessorFieldPageButtonRect(
                topologyField, 0u);
        const NSRect lastCameraButton =
            s3g::clap_gui::topologyProcessorCameraButtonRect(
                topologyField, 2u);
        const auto eightChannelGrid =
            s3g::clap_gui::topologyProcessorChannelGrid(
                topologyContent, 8u);
        const auto twentyFourChannelGrid =
            s3g::clap_gui::topologyProcessorChannelGrid(
                topologyContent, 24u);
        if (!closeEnough(topologyContent.origin.x, 22.0)
            || !closeEnough(topologyContent.origin.y, 62.0)
            || !closeEnough(topologyContent.size.width, 600.0)
            || !closeEnough(topologyContent.size.height, 600.0)
            || !closeEnough(firstPageButton.origin.x, 260.0)
            || !closeEnough(lastCameraButton.origin.x, 574.0)
            || eightChannelGrid.columns != 2u
            || eightChannelGrid.rows != 4u
            || !closeEnough(eightChannelGrid.cellHeight, 134.0)
            || twentyFourChannelGrid.columns != 4u
            || twentyFourChannelGrid.rows != 6u) {
            std::cerr << "Shared topology field interaction geometry failed\n";
            return 1;
        }

        failureStage = "bundle load";
        void* library = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW);
        if (!library) {
            std::cerr << "Could not load family member: " << dlerror() << "\n";
            return 1;
        }
        const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
        if (!entry || !entry->init(argv[1])) {
            std::cerr << "Could not initialize family CLAP entry\n";
            return 1;
        }

        HostContext hostContext {};
        clap_host_t host {};
        host.clap_version = CLAP_VERSION_INIT;
        host.host_data = &hostContext;
        host.name = "s3g family GUI smoke";
        host.vendor = "s3g";
        host.url = "https://github.com/s3g/s3g-dsp";
        host.version = "1";
        host.get_extension = hostGetExtension;
        host.request_restart = hostRequest;
        host.request_process = hostRequestProcess;
        host.request_callback = hostRequestCallback;

        const auto* factory = static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        const clap_plugin_descriptor_t* requestedDescriptor = nullptr;
        if (factory) {
            const uint32_t descriptorCount =
                factory->get_plugin_count(factory);
            for (uint32_t index = 0u; index < descriptorCount; ++index) {
                const auto* candidate =
                    factory->get_plugin_descriptor(factory, index);
                if (candidate && candidate->id
                    && std::strcmp(candidate->id, pluginId) == 0) {
                    requestedDescriptor = candidate;
                    break;
                }
            }
        }
        if (!requestedDescriptor || !requestedDescriptor->name
            || std::strncmp(requestedDescriptor->name, expectedNamePrefix,
                std::strlen(expectedNamePrefix)) != 0) {
            std::cerr << "Host name is outside the requested family order: "
                << (requestedDescriptor && requestedDescriptor->name
                    ? requestedDescriptor->name : "(missing)") << "\n";
            return 1;
        }
        const clap_plugin_t* plugin = factory
            ? factory->create_plugin(factory, &host, pluginId)
            : nullptr;
        if (!plugin || !plugin->init(plugin)) {
            std::cerr << "Could not create family member " << pluginId << "\n";
            return 1;
        }

        failureStage = "parameter defaults";
        const auto* params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        hostContext.plugin = plugin;
        hostContext.params = params;
        clap_param_info_t firstParam {};
        NSEvent* doubleClick = [NSEvent
            mouseEventWithType:NSEventTypeLeftMouseDown
            location:NSZeroPoint
            modifierFlags:0
            timestamp:0.0
            windowNumber:0
            context:nil
            eventNumber:0
            clickCount:2
            pressure:1.0];
        NSEvent* singleClick = [NSEvent
            mouseEventWithType:NSEventTypeLeftMouseDown
            location:NSZeroPoint
            modifierFlags:0
            timestamp:0.0
            windowNumber:0
            context:nil
            eventNumber:0
            clickCount:1
            pressure:1.0];
        double resolvedDefault = 0.0;
        bool ok = params && params->count && params->get_info
            && params->count(plugin) > 0u
            && params->get_info(plugin, 0u, &firstParam)
            && s3g::clap_gui::sliderDoubleClickDefault(
                doubleClick, plugin, firstParam.id, &resolvedDefault)
            && resolvedDefault == firstParam.default_value
            && !s3g::clap_gui::sliderDoubleClickDefault(
                singleClick, plugin, firstParam.id, &resolvedDefault);

        if (ok && std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-wave-terrain-encoder-64") == 0) {
            bool foundScale = false;
            bool foundInterpretation = false;
            bool foundVoices = false;
            const uint32_t parameterCount = params->count(plugin);
            for (uint32_t index = 0u; index < parameterCount; ++index) {
                clap_param_info_t info {};
                if (!params->get_info(plugin, index, &info)) {
                    ok = false;
                    break;
                }
                if (std::strcmp(info.name, "Pitch Scale") == 0) {
                    char text[64] {};
                    double parsed = -1.0;
                    double removed = -1.0;
                    foundScale = info.max_value
                            == static_cast<double>(
                                s3g::kMusicalScaleCount)
                        && params->value_to_text(
                            plugin, info.id, info.max_value,
                            text, sizeof(text))
                        && std::strcmp(text, "BLUES COMPOSITE") == 0
                        && params->text_to_value(
                            plugin, info.id, "PENTATONIC MINOR", &parsed)
                        && parsed == 32.0
                        && !params->text_to_value(
                            plugin, info.id, "VECTOR", &removed);
                } else if (std::strcmp(info.name, "Interpretation") == 0) {
                    double removed = -1.0;
                    foundInterpretation = info.max_value == 8.0
                        && !params->text_to_value(
                            plugin, info.id, "VECTOR", &removed);
                } else if (std::strcmp(info.name, "Voices") == 0) {
                    char text[16] {};
                    double parsed = -1.0;
                    foundVoices =
                        (info.flags & CLAP_PARAM_IS_STEPPED) != 0u
                        && info.min_value == 1.0
                        && info.max_value == 64.0
                        && info.default_value == 12.0
                        && params->value_to_text(
                            plugin, info.id, 12.0, text, sizeof(text))
                        && std::strcmp(text, "12") == 0
                        && params->text_to_value(
                            plugin, info.id, "17", &parsed)
                        && parsed == 17.0;
                }
            }
            ok = ok && foundScale && foundInterpretation
                && foundVoices;
        }

        if (ok && (std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-vot-encoder-64") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-vox-encoder-64") == 0)) {
            bool foundScale = false;
            const uint32_t parameterCount = params->count(plugin);
            for (uint32_t index = 0u; index < parameterCount; ++index) {
                clap_param_info_t info {};
                if (!params->get_info(plugin, index, &info)) {
                    ok = false;
                    break;
                }
                if (std::strcmp(info.name, "Pitch Scale") != 0) continue;
                char text[64] {};
                double parsed = -1.0;
                foundScale =
                    info.max_value
                        == static_cast<double>(
                            s3g::kMusicalScaleCount - 1u)
                    && params->value_to_text(
                        plugin, info.id, 3.0, text, sizeof(text))
                    && std::strcmp(text, "PENTATONIC MAJOR") == 0
                    && params->text_to_value(
                        plugin, info.id, "PENTATONIC MINOR", &parsed)
                    && parsed == 31.0;
            }
            ok = ok && foundScale;
        }

        if (ok && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-effect-dj-filter-64") == 0) {
            bool foundBody = false;
            const uint32_t parameterCount = params->count(plugin);
            for (uint32_t index = 0u; index < parameterCount; ++index) {
                clap_param_info_t info {};
                if (!params->get_info(plugin, index, &info)) {
                    ok = false;
                    break;
                }
                if (std::strcmp(info.name, "Auditory body") != 0) continue;
                SingleParamEventInput event {};
                double reported = -1.0;
                char text[32] {};
                foundBody = params->flush
                    && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u
                    && info.min_value == 0.0
                    && info.max_value == 5.0;
                if (foundBody) {
                    setSingleParamEvent(event, info.id, 4.0);
                    params->flush(plugin, &event.events, nullptr);
                    foundBody = params->get_value(
                            plugin, info.id, &reported)
                        && reported == 4.0
                        && params->value_to_text(
                            plugin, info.id, reported, text, sizeof(text))
                        && std::strcmp(text, "DODECA 20") == 0;
                }
                if (foundBody) {
                    setSingleParamEvent(event, info.id, 5.0);
                    params->flush(plugin, &event.events, nullptr);
                    foundBody = params->get_value(
                            plugin, info.id, &reported)
                        && reported == 5.0
                        && params->value_to_text(
                            plugin, info.id, reported, text, sizeof(text))
                        && std::strcmp(text, "SPHERE 24") == 0;
                }
                break;
            }
            ok = ok && foundBody;
        }

        const bool queuedOwnershipEncoder =
            std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-wave-terrain-encoder-64") == 0
            || std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-terrain-navigator-64") == 0
            || std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-water-encoder-64") == 0
            || std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-wind-encoder-64") == 0
            || std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64") == 0
            || std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-cryosphere-encoder-64") == 0;
        if (ok && queuedOwnershipEncoder && !documentationCapture) {
            failureStage = "active state queue publication";
            const auto* pluginState =
                static_cast<const clap_plugin_state_t*>(
                    plugin->get_extension(plugin, CLAP_EXT_STATE));
            const auto* audioPorts =
                static_cast<const clap_plugin_audio_ports_t*>(
                    plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
            clap_param_info_t outputInfo {};
            bool foundOutput = false;
            for (uint32_t index = 0u;
                 params && index < params->count(plugin); ++index) {
                clap_param_info_t candidate {};
                if (!params->get_info(plugin, index, &candidate)) break;
                if (std::strcmp(candidate.name, "Output") == 0) {
                    outputInfo = candidate;
                    foundOutput = true;
                    break;
                }
            }

            MemoryPluginState saved;
            clap_ostream_t outputState { &saved, stateWrite };
            double original = 0.0;
            clap_audio_port_info_t outputPort {};
            ok = pluginState && pluginState->save && pluginState->load
                && params && params->flush && foundOutput
                && params->get_value(plugin, outputInfo.id, &original)
                && pluginState->save(plugin, &outputState)
                && !saved.bytes.empty()
                && audioPorts && audioPorts->count && audioPorts->get
                && audioPorts->count(plugin, false) > 0u
                && audioPorts->get(plugin, 0u, false, &outputPort)
                && outputPort.channel_count > 0u;

            constexpr uint32_t kQueueTestFrames = 64u;
            const double low = outputInfo.min_value
                + 0.2 * (outputInfo.max_value - outputInfo.min_value);
            const double high = outputInfo.min_value
                + 0.8 * (outputInfo.max_value - outputInfo.min_value);
            const double mutation = std::fabs(original - low)
                    > std::fabs(original - high)
                ? low : high;
            SingleParamEventInput mutationEvent {};
            if (ok) {
                setSingleParamEvent(
                    mutationEvent, outputInfo.id, mutation);
                params->flush(plugin, &mutationEvent.events, nullptr);
                double reported = 0.0;
                ok = params->get_value(plugin, outputInfo.id, &reported)
                    && std::fabs(reported - mutation) < 1.0e-5;
            }

            std::vector<std::vector<float>> outputStorage;
            std::vector<float*> outputPointers;
            if (ok) {
                outputStorage.resize(outputPort.channel_count);
                outputPointers.resize(outputPort.channel_count);
                for (uint32_t channel = 0u;
                     channel < outputPort.channel_count; ++channel) {
                    outputStorage[channel].assign(kQueueTestFrames, 0.0f);
                    outputPointers[channel] = outputStorage[channel].data();
                }
            }
            clap_audio_buffer_t outputBuffer {};
            outputBuffer.data32 = outputPointers.empty()
                ? nullptr : outputPointers.data();
            outputBuffer.channel_count = outputPort.channel_count;
            clap_process_t processBlock {};
            processBlock.frames_count = kQueueTestFrames;
            processBlock.audio_outputs = &outputBuffer;
            processBlock.audio_outputs_count = 1u;

            bool activated = false;
            bool processing = false;
            if (ok) {
                activated = plugin->activate(
                    plugin, 48000.0, 1u, kQueueTestFrames);
                processing = activated && plugin->start_processing(plugin);
                ok = activated && processing;
            }
            hostContext.paramFlushRequested = false;
            hostContext.deferParamFlush = true;
            hostContext.callbackRequested = false;
            const uint32_t stateRescansBefore =
                hostContext.paramRescanCount;
            bool stateLoaded = false;
            bool stateConsumed = false;
            bool flushRequested = false;
            bool processSucceeded = false;
            if (ok) {
                saved.offset = 0u;
                clap_istream_t inputState { &saved, stateRead };
                stateLoaded = pluginState->load(plugin, &inputState);
                stateConsumed = saved.offset == saved.bytes.size();
                flushRequested = hostContext.paramFlushRequested;
                processSucceeded = plugin->process(plugin, &processBlock)
                    != CLAP_PROCESS_ERROR;
                ok = stateLoaded && stateConsumed && flushRequested
                    && processSucceeded;
            }
            hostContext.deferParamFlush = false;
            hostContext.paramFlushRequested = false;
            if (hostContext.callbackRequested
                && plugin->on_main_thread) {
                plugin->on_main_thread(plugin);
            }
            hostContext.callbackRequested = false;
            const bool stateRescanned = hostContext.paramRescanCount
                > stateRescansBefore;
            ok = ok && stateRescanned;
            if (ok) {
                double restored = 0.0;
                ok = params->get_value(
                        plugin, outputInfo.id, &restored)
                    && std::fabs(restored - original) < 1.0e-5;
            }
            if (processing) plugin->stop_processing(plugin);
            if (activated) plugin->deactivate(plugin);
            if (!ok) {
                std::cerr << "Queued active state restore failed for "
                    << pluginId
                    << " (loaded=" << stateLoaded
                    << ", consumed=" << stateConsumed
                    << ", flush-requested=" << flushRequested
                    << ", processed=" << processSucceeded
                    << ", rescanned=" << stateRescanned
                    << ")\n";
            }
        }

        const bool documentationLoopProcessor = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.loop-processor-8ch") == 0;
        const bool documentationMultiLoopProcessor = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.multi-loop-processor-8ch") == 0;
        const bool documentationAmbiGrain = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-grain-processor") == 0;
        const bool documentationSampleProcessor =
            documentationLoopProcessor
            || documentationMultiLoopProcessor
            || documentationAmbiGrain;
        if (ok && documentationSampleProcessor) {
            failureStage = "documentation sample state";
            const auto* pluginState =
                static_cast<const clap_plugin_state_t*>(
                    plugin->get_extension(plugin, CLAP_EXT_STATE));
            auto copyDocumentationPath = [](char* destination,
                                            size_t capacity,
                                            const char* environmentName) {
                const char* source = std::getenv(environmentName);
                if (!source || !source[0]) return false;
                const int length = std::snprintf(
                    destination, capacity, "%s", source);
                return length > 0
                    && static_cast<size_t>(length) < capacity;
            };
            auto loadDocumentationState = [&](const void* fixture,
                                              size_t fixtureSize) {
                if (!pluginState || !pluginState->load) return false;
                MemoryPluginState memory;
                const auto* first = static_cast<const uint8_t*>(fixture);
                memory.bytes.assign(first, first + fixtureSize);
                clap_istream_t input { &memory, stateReadWhole };
                return pluginState->load(plugin, &input)
                    && memory.offset == fixtureSize;
            };

            if (documentationLoopProcessor) {
                DocumentationLoopState fixture {};
                ok = copyDocumentationPath(
                        fixture.samplePath, sizeof(fixture.samplePath),
                        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH")
                    && loadDocumentationState(&fixture, sizeof(fixture));
            } else if (documentationMultiLoopProcessor) {
                DocumentationMultiLoopState fixture {};
                constexpr const char* environmentNames[] = {
                    "S3G_GUI_DOCUMENTATION_SAMPLE_PATH",
                    "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_2",
                    "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_3",
                    "S3G_GUI_DOCUMENTATION_SAMPLE_PATH_4",
                };
                for (size_t index = 0u;
                     ok && index < std::size(environmentNames); ++index) {
                    ok = copyDocumentationPath(
                        fixture.samplePaths[index],
                        sizeof(fixture.samplePaths[index]),
                        environmentNames[index]);
                }
                ok = ok && loadDocumentationState(&fixture, sizeof(fixture));
            } else {
                DocumentationAmbiGrainState fixture {};
                ok = copyDocumentationPath(
                        fixture.samplePath, sizeof(fixture.samplePath),
                        "S3G_GUI_DOCUMENTATION_SAMPLE_PATH")
                    && loadDocumentationState(&fixture, sizeof(fixture));
            }
            if (!ok) {
                std::cerr << "Could not load documentation sample fixture for "
                    << pluginId << "\n";
            }
        }

        if (ok) failureStage = "GUI API";
        const auto* gui = static_cast<const clap_plugin_gui_t*>(
            plugin->get_extension(plugin, CLAP_EXT_GUI));
        ok = ok && gui
            && gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false);

        uint32_t width = 0u;
        uint32_t height = 0u;
        if (ok) failureStage = "resize contract";
        if (responsive || dynamic) {
            const uint32_t expectedMinimumWidth = breakbeatSlicer
                ? 620u
                : responsiveWide
                ? nativeWidth
                : std::min(dynamic ? 720u : 480u, nativeWidth);
            const uint32_t expectedMinimumHeight = breakbeatSlicer
                ? 420u
                : std::min(dynamic ? 430u : 360u, nativeHeight);
            clap_gui_resize_hints_t hints {};
            ok = ok && gui->can_resize(plugin)
                && gui->get_resize_hints(plugin, &hints)
                && hints.can_resize_horizontally && hints.can_resize_vertically
                && !hints.preserve_aspect_ratio
                && gui->get_size(plugin, &width, &height)
                && width >= expectedMinimumWidth && width <= nativeWidth
                && height >= expectedMinimumHeight && height <= nativeHeight;
            uint32_t minimumWidth = 1u;
            uint32_t minimumHeight = 1u;
            ok = ok && gui->adjust_size(plugin, &minimumWidth, &minimumHeight)
                && minimumWidth == expectedMinimumWidth
                && minimumHeight == expectedMinimumHeight;
            if (!ok) {
                std::cerr << "Resize details: size=" << width << "x" << height
                    << " minimum=" << minimumWidth << "x" << minimumHeight
                    << " expected minimum=" << expectedMinimumWidth << "x"
                    << expectedMinimumHeight << "\n";
            }
        } else {
            ok = ok && !gui->can_resize(plugin)
                && gui->get_size(plugin, &width, &height)
                && width == nativeWidth && height == nativeHeight;
        }
        if (ok) failureStage = "GUI create";
        ok = ok && gui->create(plugin, CLAP_WINDOW_API_COCOA, false);

        const uint32_t testWidth = documentationCapture && dynamic
            ? nativeWidth
            : responsiveWide
            ? nativeWidth
            : ((responsive || dynamic)
                ? std::min(720u, nativeWidth) : nativeWidth);
        const uint32_t testHeight = documentationCapture && dynamic
            ? nativeHeight
            : ((responsive || dynamic)
                ? std::min(540u, nativeHeight) : nativeHeight);
        if (ok) failureStage = "GUI resize";
        if (responsive || dynamic) {
            ok = ok && gui->set_size(plugin, testWidth, testHeight)
                && gui->get_size(plugin, &width, &height)
                && width == testWidth && height == testHeight;
        }

        NSView* parent = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, testWidth, testHeight)];
        clap_window_t window {};
        window.api = CLAP_WINDOW_API_COCOA;
        window.cocoa = parent;
        if (ok) failureStage = "GUI parent";
        ok = ok && gui->set_parent(plugin, &window) && [[parent subviews] count] == 1u;
        NSView* root = ok ? [[parent subviews] objectAtIndex:0u] : nil;
        NSScrollView* scroll = nil;
        NSView* document = root;
        if (ok) failureStage = "responsive document";
        if (responsive) {
            ok = ok && [root isKindOfClass:[NSScrollView class]];
            scroll = ok ? static_cast<NSScrollView*>(root) : nil;
            document = scroll ? [scroll documentView] : nil;
            ok = ok && [scroll hasHorizontalScroller] && [scroll hasVerticalScroller]
                && document
                && closeEnough([document frame].size.width, nativeWidth)
                && closeEnough([document frame].size.height, nativeHeight);
        } else if (dynamic) {
            ok = ok && document
                && ![root isKindOfClass:[NSScrollView class]]
                && closeEnough([document frame].size.width, testWidth)
                && closeEnough([document frame].size.height, testHeight);
        } else {
            ok = ok && document
                && closeEnough([document frame].size.width, nativeWidth)
                && closeEnough([document frame].size.height, nativeHeight);
        }
        const bool formantMatrix = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.formant-matrix") == 0;
        const bool documentationBreakbeatSlicer = documentationCapture
            && breakbeatSlicer;
        if (ok && documentationBreakbeatSlicer) {
            failureStage = "documentation Slicer break bank";
            @try {
                ok = [document respondsToSelector:
                        @selector(loadDocumentationBreaks)]
                    && [document loadDocumentationBreaks];
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok && formantMatrix && !documentationCapture) {
            failureStage = "Formant Matrix effect identity and MIDI note input";
            const auto hasDescriptorFeature = [&](const char* expectedFeature) {
                if (!requestedDescriptor->features || !expectedFeature) {
                    return false;
                }
                for (const char* const* feature = requestedDescriptor->features;
                     *feature; ++feature) {
                    if (std::strcmp(*feature, expectedFeature) == 0) return true;
                }
                return false;
            };
            const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
                plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
            clap_note_port_info_t noteInputInfo {};
            ok = requestedDescriptor->version
                && std::strcmp(requestedDescriptor->version, "5.10.0") == 0
                && hasDescriptorFeature(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT)
                && hasDescriptorFeature(CLAP_PLUGIN_FEATURE_FILTER)
                && hasDescriptorFeature(CLAP_PLUGIN_FEATURE_STEREO)
                && !hasDescriptorFeature(CLAP_PLUGIN_FEATURE_INSTRUMENT)
                && !hasDescriptorFeature(CLAP_PLUGIN_FEATURE_SYNTHESIZER)
                && notePorts && notePorts->count && notePorts->get
                && notePorts->count(plugin, true) == 1u
                && notePorts->count(plugin, false) == 0u
                && notePorts->get(plugin, 0u, true, &noteInputInfo)
                && std::strcmp(noteInputInfo.name, "MIDI In") == 0
                && (noteInputInfo.supported_dialects
                    & CLAP_NOTE_DIALECT_CLAP) != 0u
                && (noteInputInfo.supported_dialects
                    & CLAP_NOTE_DIALECT_MIDI) != 0u;
            if (!ok) {
                std::cerr << "Formant Matrix descriptor/note details: version="
                    << (requestedDescriptor->version
                        ? requestedDescriptor->version : "(missing)")
                    << " effect="
                    << hasDescriptorFeature(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT)
                    << " filter="
                    << hasDescriptorFeature(CLAP_PLUGIN_FEATURE_FILTER)
                    << " stereo="
                    << hasDescriptorFeature(CLAP_PLUGIN_FEATURE_STEREO)
                    << " instrument="
                    << hasDescriptorFeature(CLAP_PLUGIN_FEATURE_INSTRUMENT)
                    << " synth="
                    << hasDescriptorFeature(CLAP_PLUGIN_FEATURE_SYNTHESIZER)
                    << " noteIn=" << (notePorts && notePorts->count
                        ? notePorts->count(plugin, true) : 0u)
                    << " noteOut=" << (notePorts && notePorts->count
                        ? notePorts->count(plugin, false) : 0u) << "\n";
            }
            if (ok) failureStage =
                "Formant Matrix text, routing, and audition handoff";
            @try {
                const auto formantMouseEvent =
                    [&](NSEventType type, NSPoint documentPoint,
                        NSEventModifierFlags flags = 0u) {
                        return [NSEvent
                            mouseEventWithType:type
                            location:[document convertPoint:
                                documentPoint toView:nil]
                            modifierFlags:flags
                            timestamp:0.0
                            windowNumber:0
                            context:nil
                            eventNumber:0
                            clickCount:1
                            pressure:1.0];
                    };
                NSTextField* phraseField = nil;
                bool legacySystemControls = false;
                for (NSView* child in [document subviews]) {
                    if ([child isKindOfClass:[NSTextField class]]) {
                        NSTextField* field = static_cast<NSTextField*>(child);
                        if ([field placeholderString] != nil) phraseField = field;
                    } else if ([child isKindOfClass:[NSSlider class]]
                        || [child isKindOfClass:[NSPopUpButton class]]
                        || [child isKindOfClass:[NSButton class]]) {
                        legacySystemControls = true;
                    }
                }

                bool hasPhraseMode = false;
                bool hasEchoHeads = false;
                bool hasEchoClock = false;
                bool hasDefinition = false;
                bool hasModulatorSource = false;
                bool hasMicGain = false;
                bool hasClassicMicProfile = false;
                uint32_t hiddenAnalysisPitchParams = 0u;
                uint32_t voiceBankSurfaceMatches = 0u;
                uint32_t expandedScalarMatches = 0u;
                uint32_t analysisPitchSurfaceMatches = 0u;
                uint32_t bandTrimSurfaceMatches = 0u;
                uint32_t matrixASurfaceMatches = 0u;
                uint32_t matrixBSurfaceMatches = 0u;
                const uint32_t parameterCount = params
                    ? params->count(plugin) : 0u;
                for (uint32_t index = 0u; index < parameterCount; ++index) {
                    clap_param_info_t info {};
                    if (!params->get_info(plugin, index, &info)) continue;
                    hasPhraseMode |= info.id == 54u
                        && std::strcmp(info.name, "Phrase Mode") == 0
                        && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u;
                    hasDefinition |= info.id == 57u
                        && std::strcmp(info.name, "Definition") == 0
                        && std::strcmp(info.module, "Analysis") == 0
                        && info.min_value == 0.0
                        && info.max_value == 1.0
                        && info.default_value == 0.78;
                    hasEchoHeads |= info.id == 58u
                        && std::strcmp(info.name, "Echo Heads") == 0
                        && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u;
                    hasEchoClock |= info.id == 59u
                        && std::strcmp(info.name, "Echo Clock") == 0
                        && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u;
                    hasModulatorSource |= info.id == 98u
                        && std::strcmp(info.name, "Modulator Source") == 0
                        && std::strcmp(info.module, "Modulator") == 0
                        && info.min_value == 0.0
                        && info.max_value == 2.0
                        && info.default_value == 0.0
                        && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u;
                    hasMicGain |= info.id == 99u
                        && std::strcmp(info.name, "Mic Gain") == 0
                        && std::strcmp(info.module, "Modulator") == 0
                        && info.min_value == -24.0
                        && info.max_value == 24.0
                        && info.default_value == 0.0
                        && (info.flags & CLAP_PARAM_IS_STEPPED) == 0u;
                    if ((info.id >= 16u && info.id <= 19u)
                        || info.id == 22u || info.id == 23u) {
                        hiddenAnalysisPitchParams +=
                            (info.flags & CLAP_PARAM_IS_HIDDEN) != 0u;
                    }
                    if (info.id == 1u && info.max_value == 31.0
                        && info.default_value == 14.0) {
                        char classicMic[64] {};
                        char formantGlide[64] {};
                        char vocalAlloy[64] {};
                        char mouthCircuit[64] {};
                        char impulseMatrix[64] {};
                        char custom[64] {};
                        hasClassicMicProfile = params->value_to_text
                            && params->value_to_text(plugin, 1u, 14.0,
                                classicMic, sizeof(classicMic))
                            && params->value_to_text(plugin, 1u, 15.0,
                                formantGlide, sizeof(formantGlide))
                            && params->value_to_text(plugin, 1u, 23.0,
                                vocalAlloy, sizeof(vocalAlloy))
                            && params->value_to_text(plugin, 1u, 24.0,
                                mouthCircuit, sizeof(mouthCircuit))
                            && params->value_to_text(plugin, 1u, 25.0,
                                impulseMatrix, sizeof(impulseMatrix))
                            && params->value_to_text(plugin, 1u, 31.0,
                                custom, sizeof(custom))
                            && std::strcmp(classicMic, "Classic Mic") == 0
                            && std::strcmp(formantGlide, "Formant Glide") == 0
                            && std::strcmp(vocalAlloy, "Vocal Alloy") == 0
                            && std::strcmp(mouthCircuit,
                                "Mouth Circuit") == 0
                            && std::strcmp(impulseMatrix,
                                "Impulse Matrix") == 0
                            && std::strcmp(custom, "Custom") == 0;
                    }
                    if (info.id >= 65u && info.id <= 86u) {
                        constexpr const char* names[] {
                            "Bank Level", "Bank Mode",
                            "Carrier Shape", "Carrier Harmonics",
                            "Carrier Color", "Carrier Noise",
                            "Analysis / Phoneme", "Band Attack",
                            "Band Release", "Bank Resonance", "Bank Drive",
                            "Band Shift", "Band Stretch", "Band Tilt",
                            "Sibilance", "Matrix Mode", "Matrix Depth",
                            "Bank Stereo Spread", "Envelope Freeze",
                            "Freeze Trigger", "Envelope Blur", "Gesture Follow"
                        };
                        constexpr bool stepped[] {
                            false, true, true, false, false, false,
                            false, false, false, false, false, false,
                            false, false, false, true, false, false,
                            false, true, false, false
                        };
                        const uint32_t surfaceIndex = info.id - 65u;
                        const bool isStepped =
                            (info.flags & CLAP_PARAM_IS_STEPPED) != 0u;
                        if (std::strcmp(info.name, names[surfaceIndex]) == 0
                            && isStepped == stepped[surfaceIndex]) {
                            ++voiceBankSurfaceMatches;
                        }
                    }
                    if (info.id >= 87u && info.id <= 107u) {
                        constexpr const char* names[] {
                            "Band Layout", "Voiced / Unvoiced Mode",
                            "Voicing Threshold", "Voiced Level",
                            "Unvoiced Level", "To Voiced", "To Unvoiced",
                            "Open Level", "Band Coupling",
                            "Articulation Level", "Stereo Pattern",
                            "Modulator Source", "Mic Gain",
                            "Pulse Width", "Carrier LFO Shape",
                            "Carrier LFO Rate", "Carrier FM", "Carrier PWM",
                            "Carrier LFO Sync", "Carrier LFO Division",
                            "Matrix A / B"
                        };
                        constexpr double minima[] {
                            0.0, 0.0, 0.0, 0.0, 0.0, 10.0, 10.0,
                            0.0, -3.0, 0.0, 0.0, 0.0, -24.0,
                            0.05, 0.0, 0.02, 0.0, 0.0, 0.0, 0.0, 0.0
                        };
                        constexpr double maxima[] {
                            1.0, 3.0, 1.0, 1.0, 1.0, 250.0, 250.0,
                            1.0, 3.0, 1.0, 2.0, 2.0, 24.0,
                            0.95, 1.0, 13.0, 24.0, 1.0, 1.0, 11.0, 1.0
                        };
                        constexpr bool stepped[] {
                            true, true, false, false, false, false, false,
                            false, true, false, true, true, false,
                            false, true, false, false, false, true, true, false
                        };
                        const uint32_t surfaceIndex = info.id - 87u;
                        const bool isStepped =
                            (info.flags & CLAP_PARAM_IS_STEPPED) != 0u;
                        if (std::strcmp(info.name, names[surfaceIndex]) == 0
                            && std::fabs(info.min_value
                                - minima[surfaceIndex]) < 1.0e-9
                            && std::fabs(info.max_value
                                - maxima[surfaceIndex]) < 1.0e-9
                            && isStepped == stepped[surfaceIndex]) {
                            ++expandedScalarMatches;
                        }
                    } else if (info.id >= 108u && info.id <= 129u) {
                        const uint32_t band = info.id - 108u;
                        char expected[64] {};
                        std::snprintf(expected, sizeof(expected),
                            "Band %02u Trim", band + 1u);
                        if (std::strcmp(info.name, expected) == 0
                            && std::strcmp(info.module, "Band Levels") == 0
                            && info.min_value == 0.0
                            && info.max_value == 2.0
                            && info.default_value == 1.0
                            && (info.flags & CLAP_PARAM_IS_STEPPED) == 0u) {
                            ++bandTrimSurfaceMatches;
                        }
                    } else if (info.id >= 130u && info.id <= 1097u) {
                        const bool sceneB = info.id >= 614u;
                        const uint32_t cell = info.id
                            - (sceneB ? 614u : 130u);
                        const uint32_t destination = cell / 22u;
                        const uint32_t source = cell % 22u;
                        char expected[64] {};
                        std::snprintf(expected, sizeof(expected),
                            "%c B%02u to B%02u", sceneB ? 'B' : 'A',
                            source + 1u, destination + 1u);
                        const bool match = std::strcmp(
                                info.name, expected) == 0
                            && std::strcmp(info.module,
                                sceneB ? "Routing B" : "Routing A") == 0
                            && info.min_value == -1.0
                            && info.max_value == 1.0
                            && info.default_value
                                == (source == destination ? 1.0 : 0.0)
                            && (info.flags & CLAP_PARAM_IS_STEPPED) == 0u;
                        if (match) {
                            if (sceneB) ++matrixBSurfaceMatches;
                            else ++matrixASurfaceMatches;
                        }
                    }
                    if (info.id >= 1098u && info.id <= 1119u) {
                        constexpr const char* names[] {
                            "Analysis Response", "Carrier Pitch Source",
                            "Scale Root", "Pitch Scale", "Pitch Hold",
                            "Mouth Focus", "Transfer Mode", "Voice Focus",
                            "Analysis Leveler", "Consonant Color",
                            "Consonant Speed", "Carrier Density",
                            "Analysis Width", "HF Detail Mode",
                            "HF Detail Level", "HF Detail Cutoff",
                            "Analysis Low EQ", "Analysis Mid EQ",
                            "Analysis Air EQ", "Analysis Compression",
                            "Analysis Noise Reject",
                            "Analysis Spectral Balance"
                        };
                        constexpr double minima[] {
                            0.0, 0.0, 0.0, 0.0, 20.0, 0.0,
                            0.0, -1.0, 0.0, -1.0, 0.0, 0.0,
                            0.0, 0.0, 0.0, 2200.0, -12.0, -12.0,
                            -12.0, 0.0, 0.0, 0.0
                        };
                        constexpr double maxima[] {
                            2.0, 1.0, 11.0, 101.0, 2000.0, 1.0,
                            1.0, 1.0, 1.0, 1.0, 1.0, 1.0,
                            1.0, 2.0, 1.0, 9000.0, 12.0, 12.0,
                            12.0, 1.0, 1.0, 1.0
                        };
                        constexpr bool stepped[] {
                            true, true, true, true, false, false,
                            true, false, false, false, false, false,
                            false, true, false, false, false, false,
                            false, false, false, false
                        };
                        const uint32_t surfaceIndex = info.id - 1098u;
                        const bool isStepped =
                            (info.flags & CLAP_PARAM_IS_STEPPED) != 0u;
                        if (std::strcmp(info.name, names[surfaceIndex]) == 0
                            && info.min_value == minima[surfaceIndex]
                            && info.max_value == maxima[surfaceIndex]
                            && isStepped == stepped[surfaceIndex]) {
                            ++analysisPitchSurfaceMatches;
                        }
                    }
                }

                // Model a sleeping effect. The editor's Compile action
                // must still publish the phrase and select Text Phrase before
                // the host services the outbound gesture queue.
                hostContext.deferParamFlush = true;
                hostContext.paramFlushRequested = false;
                char externalMicChoice[64] {};
                char internalSpeechChoice[64] {};
                char blendChoice[64] {};
                const bool modulatorChoices = params && params->value_to_text
                    && params->value_to_text(plugin, 98u, 0.0,
                        externalMicChoice, sizeof(externalMicChoice))
                    && params->value_to_text(plugin, 98u, 1.0,
                        internalSpeechChoice, sizeof(internalSpeechChoice))
                    && params->value_to_text(plugin, 98u, 2.0,
                        blendChoice, sizeof(blendChoice))
                    && std::strcmp(externalMicChoice, "External Mic") == 0
                    && std::strcmp(internalSpeechChoice,
                        "Internal Speech") == 0
                    && std::strcmp(blendChoice, "Blend") == 0;
                ok = phraseField && !legacySystemControls
                    && [phraseField isEditable]
                    && [phraseField focusRingType] == NSFocusRingTypeNone
                    && [phraseField isHidden]
                    && [[phraseField stringValue]
                        isEqualToString:@"hello worlds"]
                    && [document respondsToSelector:@selector(commitPhrase:)]
                    && params && params->get_value && params->flush
                    && parameterCount == 1119u
                    && hasPhraseMode && hasDefinition
                    && hasEchoHeads && hasEchoClock
                    && hasModulatorSource && hasMicGain
                    && hasClassicMicProfile && modulatorChoices
                    && hiddenAnalysisPitchParams == 6u
                    && voiceBankSurfaceMatches == 22u
                    && expandedScalarMatches == 21u
                    && analysisPitchSurfaceMatches == 22u
                    && bandTrimSurfaceMatches == 22u
                    && matrixASurfaceMatches == 484u
                    && matrixBSurfaceMatches == 484u;
                if (!ok) {
                    std::cerr << "Formant Matrix surface details: params="
                        << parameterCount << " old="
                        << voiceBankSurfaceMatches << " expanded="
                        << expandedScalarMatches << " trims="
                        << " pitch=" << analysisPitchSurfaceMatches
                        << bandTrimSurfaceMatches << " matrix="
                        << matrixASurfaceMatches << "/"
                        << matrixBSurfaceMatches << " field="
                        << (phraseField != nil) << " hidden="
                        << (phraseField && [phraseField isHidden])
                        << " modulator=" << hasModulatorSource
                        << " micGain=" << hasMicGain
                        << " classicMic=" << hasClassicMicProfile
                        << " hiddenPitch=" << hiddenAnalysisPitchParams
                        << " choices=" << modulatorChoices << "\n";
                }
                if (ok) {
                    [phraseField setStringValue:@"red violence rising"];
                    [phraseField sendAction:[phraseField action]
                        to:[phraseField target]];
                    double immediateSequence = -1.0;
                    ok = params->get_value(
                            plugin, 51u, &immediateSequence)
                        && immediateSequence == 5.0;
                    if (!ok) {
                        std::cerr << "Immediate phrase sequence="
                            << immediateSequence << "\n";
                    }
                }
                if (ok) {
                    failureStage = "Formant Matrix state migration";
                    const auto* articulatorState =
                        static_cast<const clap_plugin_state_t*>(
                            plugin->get_extension(plugin, CLAP_EXT_STATE));
                    MemoryPluginState currentState;
                    clap_ostream_t stateOutput {
                        &currentState, stateWriteWhole
                    };
                    constexpr size_t headerBytes = 2u * sizeof(uint32_t);
                    constexpr size_t oldValueBytes = 76u * sizeof(double);
                    constexpr size_t currentValueBytes = 1118u * sizeof(double);
                    constexpr size_t phraseBytes = sizeof(uint32_t) + 256u;
                    ok = articulatorState && articulatorState->save
                        && articulatorState->load
                        && articulatorState->save(plugin, &stateOutput)
                        && currentState.bytes.size()
                            == headerBytes + currentValueBytes + phraseBytes;
                    if (ok) {
                        MemoryPluginState versionEleven;
                        versionEleven.bytes.reserve(
                            headerBytes + oldValueBytes + phraseBytes);
                        versionEleven.bytes.insert(versionEleven.bytes.end(),
                            currentState.bytes.begin(),
                            currentState.bytes.begin() + headerBytes
                                + oldValueBytes);
                        constexpr uint32_t oldVersion = 11u;
                        std::memcpy(versionEleven.bytes.data(),
                            &oldVersion, sizeof(oldVersion));
                        versionEleven.bytes.insert(versionEleven.bytes.end(),
                            currentState.bytes.begin() + headerBytes
                                + currentValueBytes,
                            currentState.bytes.end());
                        clap_istream_t stateInput {
                            &versionEleven, stateReadWhole
                        };
                        double migratedBankAmount = -1.0;
                        ok = articulatorState->load(plugin, &stateInput)
                            && versionEleven.offset
                                == versionEleven.bytes.size()
                            && params->get_value(
                                plugin, 65u, &migratedBankAmount)
                            && std::fabs(migratedBankAmount - 1.0) < 1.0e-6;
                    }
                    if (ok) {
                        // Version 16 used the same IDs for the retired FFT
                        // engine. Ensure those values are reset while an old
                        // effect-led profile becomes Custom.
                        MemoryPluginState versionSixteen;
                        constexpr size_t versionSixteenValueBytes =
                            85u * sizeof(double);
                        versionSixteen.bytes.reserve(headerBytes
                            + versionSixteenValueBytes + phraseBytes);
                        versionSixteen.bytes.insert(versionSixteen.bytes.end(),
                            currentState.bytes.begin(),
                            currentState.bytes.begin() + headerBytes
                                + versionSixteenValueBytes);
                        versionSixteen.bytes.insert(versionSixteen.bytes.end(),
                            currentState.bytes.begin() + headerBytes
                                + currentValueBytes,
                            currentState.bytes.end());
                        constexpr uint32_t oldVersion = 16u;
                        std::memcpy(versionSixteen.bytes.data(),
                            &oldVersion, sizeof(oldVersion));
                        const auto setOldValue = [&](uint32_t id,
                                                     double value) {
                            const size_t savedIndex = id <= 24u
                                ? id - 1u : id - 2u;
                            std::memcpy(versionSixteen.bytes.data()
                                    + headerBytes
                                    + savedIndex * sizeof(double),
                                &value, sizeof(value));
                        };
                        setOldValue(1u, 8.0);
                        setOldValue(65u, 0.93);
                        setOldValue(66u, 5.0);
                        setOldValue(67u, 8000.0);
                        clap_istream_t stateInput {
                            &versionSixteen, stateReadWhole
                        };
                        double migratedProfile = -1.0;
                        double migratedAmount = -1.0;
                        double migratedMode = -1.0;
                        double migratedCarrier = -1.0;
                        ok = articulatorState->load(plugin, &stateInput)
                            && versionSixteen.offset
                                == versionSixteen.bytes.size()
                            && params->get_value(
                                plugin, 1u, &migratedProfile)
                            && params->get_value(
                                plugin, 65u, &migratedAmount)
                            && params->get_value(
                                plugin, 66u, &migratedMode)
                            && params->get_value(
                                plugin, 67u, &migratedCarrier)
                            && migratedProfile == 31.0
                            && std::fabs(migratedAmount - 1.0) < 1.0e-6
                            && migratedMode == 0.0
                            && migratedCarrier == 1.0;
                    }
                    if (ok) {
                        // Version 18 treated the host input as a carrier and
                        // used slot 14 for Custom. Version 19 reverses that
                        // bus into a mic/modulator, so those values must not
                        // be silently reinterpreted as source selection/gain.
                        MemoryPluginState versionEighteen;
                        versionEighteen.bytes = currentState.bytes;
                        constexpr size_t versionEighteenValueBytes =
                            1096u * sizeof(double);
                        versionEighteen.bytes.erase(
                            versionEighteen.bytes.begin() + headerBytes
                                + versionEighteenValueBytes,
                            versionEighteen.bytes.begin() + headerBytes
                                + currentValueBytes);
                        constexpr uint32_t oldVersion = 18u;
                        std::memcpy(versionEighteen.bytes.data(),
                            &oldVersion, sizeof(oldVersion));
                        const auto setVersionEighteenValue =
                            [&](uint32_t id, double value) {
                                const size_t savedIndex = id <= 24u
                                    ? id - 1u : id - 2u;
                                std::memcpy(versionEighteen.bytes.data()
                                        + headerBytes
                                        + savedIndex * sizeof(double),
                                    &value, sizeof(value));
                            };
                        setVersionEighteenValue(1u, 14.0);
                        setVersionEighteenValue(98u, 0.72);
                        setVersionEighteenValue(99u, 12.0);
                        clap_istream_t stateInput {
                            &versionEighteen, stateReadWhole
                        };
                        double migratedProfile = -1.0;
                        double migratedModulator = -1.0;
                        double migratedMicGain = -99.0;
                        ok = articulatorState->load(plugin, &stateInput)
                            && versionEighteen.offset
                                == versionEighteen.bytes.size()
                            && params->get_value(
                                plugin, 1u, &migratedProfile)
                            && params->get_value(
                                plugin, 98u, &migratedModulator)
                            && params->get_value(
                                plugin, 99u, &migratedMicGain)
                            && migratedProfile == 31.0
                            && migratedModulator == 1.0
                            && migratedMicGain == 0.0;
                    }
                }
                if (ok) {
                    failureStage = "Formant Matrix profiles";
                    SingleParamEventInput bankProfile {};
                    constexpr double expectedBankModes[] {
                        1.0, 0.0, 1.0, 2.0, 1.0, 1.0, 2.0, 1.0
                    };
                    for (uint32_t index = 0u;
                         ok && index < std::size(expectedBankModes);
                         ++index) {
                        const double profile = 6.0
                            + static_cast<double>(index);
                        setSingleParamEvent(bankProfile, 1u, profile);
                        params->flush(plugin,
                            &bankProfile.events, nullptr);
                        double profileValue = -1.0;
                        double bankMode = -1.0;
                        double bankAmount = 0.0;
                        double gestureSequence = -1.0;
                        ok = params->get_value(plugin, 1u, &profileValue)
                            && params->get_value(plugin, 66u, &bankMode)
                            && params->get_value(plugin, 65u, &bankAmount)
                            && params->get_value(
                                plugin, 51u, &gestureSequence)
                            && profileValue == profile
                            && bankMode == expectedBankModes[index]
                            && bankAmount > 0.80
                            && gestureSequence == 5.0;
                        if (!ok) {
                            std::cerr << "Formant profile " << profile
                                << ": value=" << profileValue << " mode="
                                << bankMode << " amount=" << bankAmount
                                << " gesture=" << gestureSequence << "\n";
                        }
                    }
                    if (ok) {
                        setSingleParamEvent(bankProfile, 1u, 14.0);
                        params->flush(plugin, &bankProfile.events, nullptr);
                        double profileValue = -1.0;
                        double modulatorSource = -1.0;
                        double micGain = -99.0;
                        double bankMode = -1.0;
                        double articulationThru = -1.0;
                        ok = params->get_value(plugin, 1u, &profileValue)
                            && params->get_value(
                                plugin, 98u, &modulatorSource)
                            && params->get_value(plugin, 99u, &micGain)
                            && params->get_value(plugin, 66u, &bankMode)
                            && params->get_value(
                                plugin, 96u, &articulationThru)
                            && profileValue == 14.0
                            && modulatorSource == 0.0
                            && micGain == 0.0
                            && bankMode == 0.0
                            && articulationThru == 0.0;
                        if (!ok) {
                            std::cerr << "Classic Mic profile values: profile="
                                << profileValue << " source="
                                << modulatorSource << " gain=" << micGain
                                << " mode=" << bankMode << " thru="
                                << articulationThru << "\n";
                        }
                    }
                    setSingleParamEvent(bankProfile, 1u, 0.0);
                    params->flush(plugin, &bankProfile.events, nullptr);
                }
                if (ok) {
                    failureStage = "Formant Matrix preset overlay";
                    const auto titleBand =
                        s3g::clap_gui::encoderTitleBand(
                            static_cast<double>(nativeWidth),
                            static_cast<double>(nativeHeight));
                    const NSRect preset =
                        s3g::clap_gui::cocoaRect(titleBand.presetMenu);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(NSMidX(preset), NSMidY(preset)))];
                    ok = [phraseField isHidden];
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(700.0, 300.0))];
                    ok = ok && [phraseField isHidden];
                }
                if (ok) {
                    failureStage = "Formant Matrix routing and pages";
                    // Exercise the stepped menus owned by the compact ROUTE
                    // column. Source and pitch menus are deliberately tested
                    // on SOURCES below instead of being duplicated here.
                    const auto chooseRouteMenu = [&](NSPoint control,
                                                     NSPoint item,
                                                     clap_id id,
                                                     double expected) {
                        [document mouseDown:formantMouseEvent(
                            NSEventTypeLeftMouseDown, control)];
                        [document mouseDown:formantMouseEvent(
                            NSEventTypeLeftMouseDown, item)];
                        double actual = -99.0;
                        const bool matched = params->get_value(
                            plugin, id, &actual) && actual == expected;
                        if (!matched) {
                            std::cerr << "Formant ROUTE menu " << id
                                << " expected " << expected << " got "
                                << actual << "\n";
                        }
                        return matched;
                    };
                    ok = chooseRouteMenu(NSMakePoint(1100.0, 316.0),
                             NSMakePoint(1100.0, 432.0), 80u, 5.0)
                        && chooseRouteMenu(NSMakePoint(1100.0, 394.0),
                             NSMakePoint(1100.0, 528.0), 95u, 3.0)
                        && chooseRouteMenu(NSMakePoint(1100.0, 420.0),
                             NSMakePoint(1100.0, 465.0), 87u, 1.0)
                        && chooseRouteMenu(NSMakePoint(1100.0, 495.0),
                             NSMakePoint(1100.0, 558.0), 66u, 2.0)
                        && chooseRouteMenu(NSMakePoint(1100.0, 848.5),
                             NSMakePoint(1100.0, 891.5), 88u, 1.0);
                    // Mouth Focus is directly reachable on ROUTE. Its row is
                    // a slider rather than an overlapping or inert menu.
                    const NSPoint mouthFocusControl =
                        NSMakePoint(1054.0, 730.5);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, mouthFocusControl)];
                    [document mouseUp:formantMouseEvent(
                        NSEventTypeLeftMouseUp, mouthFocusControl)];
                    double routeMouthFocus = -1.0;
                    ok = ok && params->get_value(
                            plugin, 1103u, &routeMouthFocus)
                        && routeMouthFocus > 0.20
                        && routeMouthFocus < 0.30;
                    if (!ok) {
                        std::cerr << "Formant ROUTE Mouth Focus got "
                            << routeMouthFocus << "\n";
                    }
                    // Wide 16 expands its sixteen active rows and columns to
                    // the full ROUTE area. Edit a signed off-diagonal
                    // crosspoint and its final visible trim at the adapted
                    // 35-pixel grid pitch.
                    constexpr clap_id routeA0203 = 130u + 2u * 22u + 1u;
                    const NSPoint routeCell = NSMakePoint(
                        146.0 + 1.0 * 35.0, 178.0 + 2.0 * 35.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, routeCell,
                        NSEventModifierFlagOption)];
                    double routeValue = 0.0;
                    double matrixMode = 0.0;
                    ok = params->get_value(plugin, routeA0203, &routeValue)
                        && params->get_value(plugin, 80u, &matrixMode)
                        && std::fabs(routeValue + 0.25) < 1.0e-6
                        && matrixMode == 5.0;
                    if (!ok) {
                        std::cerr << "Formant ROUTE cell/mode got "
                            << routeValue << '/' << matrixMode << "\n";
                    }
                    const NSPoint trim16 = NSMakePoint(
                        146.0 + 15.0 * 35.0, 750.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, trim16)];
                    [document mouseUp:formantMouseEvent(
                        NSEventTypeLeftMouseUp, trim16)];
                    double trimValue = 0.0;
                    ok = ok && params->get_value(plugin, 123u, &trimValue)
                        && trimValue > 1.85;
                    if (!ok) {
                        std::cerr << "Formant ROUTE trim got " << trimValue
                            << "\n";
                    }

                    failureStage = "Formant Matrix scene transaction";
                    // Scene B is independently editable. Copy the currently
                    // selected A scene into B; the operation selects its B
                    // destination after publication. Drain earlier GUI edits
                    // so the full scene transaction starts at an empty bounded
                    // queue, then require Mode=Custom and every route value to
                    // reach the host as one complete ordered batch.
                    CapturedOutputEvents precedingRouteEvents {};
                    precedingRouteEvents.events.ctx = &precedingRouteEvents;
                    precedingRouteEvents.events.try_push = captureOutputEvent;
                    if (ok) {
                        params->flush(plugin, nullptr,
                            &precedingRouteEvents.events);
                        hostContext.paramFlushRequested = false;
                    }

                    // Leave only 484 queue slots available, three fewer than
                    // the complete scene transaction. The rejected operation
                    // must change neither Mode nor any B route locally.
                    SingleParamEventInput identityModeEvent {};
                    setSingleParamEvent(identityModeEvent, 80u, 0.0);
                    if (ok) {
                        params->flush(
                            plugin, &identityModeEvent.events, nullptr);
                    }
                    double modeBeforeRejectedCopy = -1.0;
                    double routeBeforeRejectedCopy = -2.0;
                    ok = ok && params->get_value(
                            plugin, 80u, &modeBeforeRejectedCopy)
                        && params->get_value(
                            plugin, 614u + 2u * 22u + 1u,
                            &routeBeforeRejectedCopy)
                        && modeBeforeRejectedCopy == 0.0;
                    const NSPoint queuePressurePoint =
                        NSMakePoint(1100.0, 100.0);
                    for (uint32_t edit = 0u; ok && edit < 9u; ++edit) {
                        [document mouseDown:formantMouseEvent(
                            NSEventTypeLeftMouseDown, queuePressurePoint)];
                        [document mouseUp:formantMouseEvent(
                            NSEventTypeLeftMouseUp, queuePressurePoint)];
                    }
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(1066.0, 54.0))];
                    double modeAfterRejectedCopy = -1.0;
                    double routeAfterRejectedCopy = -2.0;
                    ok = ok && params->get_value(
                            plugin, 80u, &modeAfterRejectedCopy)
                        && params->get_value(
                            plugin, 614u + 2u * 22u + 1u,
                            &routeAfterRejectedCopy)
                        && modeAfterRejectedCopy == modeBeforeRejectedCopy
                        && routeAfterRejectedCopy == routeBeforeRejectedCopy;
                    CapturedOutputEvents pressureEvents {};
                    pressureEvents.events.ctx = &pressureEvents;
                    pressureEvents.events.try_push = captureOutputEvent;
                    if (ok) {
                        params->flush(plugin, nullptr, &pressureEvents.events);
                        hostContext.paramFlushRequested = false;
                    }
                    ok = ok && pressureEvents.values.size() == 27u;
                    for (uint32_t event = 0u; ok && event < 27u; ++event) {
                        const uint32_t phase = event % 3u;
                        ok = pressureEvents.values[event].paramId == 24u
                            && pressureEvents.values[event].type
                                == (phase == 0u
                                    ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                    : phase == 1u
                                    ? CLAP_EVENT_PARAM_VALUE
                                    : CLAP_EVENT_PARAM_GESTURE_END);
                    }

                    // Give an inactive Wide-16 cell a sentinel value. A
                    // visible-scene operation must preserve all six hidden
                    // Speech-22 rows and columns so layout switching remains
                    // lossless.
                    constexpr clap_id hiddenRouteB =
                        614u + 21u * 22u + 21u;
                    SingleParamEventInput hiddenRouteEvent {};
                    setSingleParamEvent(hiddenRouteEvent, hiddenRouteB, 0.37);
                    if (ok) {
                        params->flush(
                            plugin, &hiddenRouteEvent.events, nullptr);
                    }
                    std::array<double, 484u> destinationBeforeCopy {};
                    for (uint32_t cell = 0u; ok && cell < 484u; ++cell) {
                        ok = params->get_value(plugin, 614u + cell,
                            &destinationBeforeCopy[cell]);
                    }

                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(1066.0, 54.0))];
                    std::vector<CapturedParamEvent> copiedMatrixValues;
                    bool continuationMatches = true;
                    for (uint32_t chunkIndex = 0u;
                         ok && copiedMatrixValues.size() < 487u
                             && chunkIndex < 16u;
                         ++chunkIndex) {
                        CapturedOutputEvents chunk {};
                        chunk.capacity = 61u;
                        chunk.events.ctx = &chunk;
                        chunk.events.try_push = captureOutputEvent;
                        hostContext.callbackRequested = false;
                        params->flush(plugin, nullptr, &chunk.events);
                        copiedMatrixValues.insert(copiedMatrixValues.end(),
                            chunk.values.begin(), chunk.values.end());
                        const bool complete =
                            copiedMatrixValues.size() == 487u;
                        if (complete) {
                            continuationMatches = continuationMatches
                                && !hostContext.callbackRequested;
                        } else {
                            continuationMatches = continuationMatches
                                && chunk.values.size() == chunk.capacity
                                && hostContext.callbackRequested;
                            hostContext.callbackRequested = false;
                            hostContext.paramFlushRequested = false;
                            if (plugin->on_main_thread) {
                                plugin->on_main_thread(plugin);
                            }
                            continuationMatches = continuationMatches
                                && hostContext.paramFlushRequested;
                        }
                        hostContext.paramFlushRequested = false;
                    }
                    constexpr clap_id routeB0203 = 614u + 2u * 22u + 1u;
                    double copiedRouteValue = 0.0;
                    bool copiedTransactionMatches =
                        copiedMatrixValues.size() == 487u
                        && copiedMatrixValues.front().type
                            == CLAP_EVENT_PARAM_GESTURE_BEGIN
                        && copiedMatrixValues.front().paramId == 80u
                        && copiedMatrixValues[1u].type
                            == CLAP_EVENT_PARAM_VALUE
                        && copiedMatrixValues[1u].paramId == 80u
                        && copiedMatrixValues[1u].value == 5.0
                        && copiedMatrixValues.back().type
                            == CLAP_EVENT_PARAM_GESTURE_END
                        && copiedMatrixValues.back().paramId == 80u;
                    for (uint32_t cell = 0u;
                         copiedTransactionMatches && cell < 484u; ++cell) {
                        const auto& event = copiedMatrixValues[2u + cell];
                        const uint32_t destination = cell / 22u;
                        const uint32_t source = cell % 22u;
                        double expectedValue = destinationBeforeCopy[cell];
                        if (destination < 16u && source < 16u) {
                            copiedTransactionMatches = params->get_value(
                                plugin, 130u + cell, &expectedValue);
                        }
                        copiedTransactionMatches = event.type
                                == CLAP_EVENT_PARAM_VALUE
                            && event.paramId == 614u + cell
                            && copiedTransactionMatches
                            && std::fabs(event.value - expectedValue) < 1.0e-6;
                    }
                    double preservedHiddenRoute = -2.0;
                    ok = ok && params->get_value(
                            plugin, routeB0203, &copiedRouteValue)
                        && params->get_value(
                            plugin, hiddenRouteB, &preservedHiddenRoute)
                        && std::fabs(copiedRouteValue + 0.25) < 1.0e-6
                        && std::fabs(preservedHiddenRoute - 0.37) < 1.0e-6;
                    ok = ok && continuationMatches
                        && copiedTransactionMatches
                        && [document acceptsFirstResponder];

                    failureStage = "Formant Matrix layout restore";
                    // Switch back without recreating the editor. The six
                    // Speech-22-only rows and columns must immediately regain
                    // their 25-pixel hit geometry and stored values.
                    SingleParamEventInput speech22LayoutEvent {};
                    setSingleParamEvent(speech22LayoutEvent, 87u, 0.0);
                    constexpr clap_id routeB2221 =
                        614u + 21u * 22u + 20u;
                    SingleParamEventInput speech22RouteEvent {};
                    setSingleParamEvent(
                        speech22RouteEvent, routeB2221, 0.37);
                    if (ok) {
                        params->flush(plugin,
                            &speech22LayoutEvent.events, nullptr);
                        params->flush(plugin,
                            &speech22RouteEvent.events, nullptr);
                    }
                    const NSPoint speech22OnlyCell = NSMakePoint(
                        146.0 + 20.0 * 25.0,
                        178.0 + 21.0 * 25.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, speech22OnlyCell)];
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, speech22OnlyCell)];
                    double restoredSpeech22Cell = -2.0;
                    ok = ok && params->get_value(
                            plugin, routeB2221, &restoredSpeech22Cell)
                        && std::fabs(restoredSpeech22Cell) < 1.0e-6;

                    failureStage = "Formant Matrix BANK ownership";
                    // Page ownership is functional, not only visual. BANK
                    // controls respond on BANK while PHRASE controls remain
                    // inert there and the native phrase field stays hidden.
                    const NSPoint bankPage = NSMakePoint(158.0, 54.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, bankPage)];
                    ok = ok && [phraseField isHidden];
                    // Analysis / Phoneme is BANK-owned; Bank Level is tested on
                    // ROUTE so it cannot silently return here as a duplicate.
                    const NSPoint bankAnalysisBlend =
                        NSMakePoint(620.0, 443.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, bankAnalysisBlend)];
                    [document mouseUp:formantMouseEvent(
                        NSEventTypeLeftMouseUp, bankAnalysisBlend)];
                    double bankValue = 0.0;
                    ok = ok && params->get_value(plugin, 71u, &bankValue)
                        && bankValue > 0.85;
                    double hiddenRateBefore = -1.0;
                    double hiddenAuditionBefore = -1.0;
                    ok = ok && params->get_value(
                            plugin, 52u, &hiddenRateBefore)
                        && params->get_value(
                            plugin, 25u, &hiddenAuditionBefore);
                    const NSPoint hiddenRate = NSMakePoint(540.0, 736.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, hiddenRate)];
                    [document mouseUp:formantMouseEvent(
                        NSEventTypeLeftMouseUp, hiddenRate)];
                    const NSPoint hiddenAudition = NSMakePoint(500.0, 814.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, hiddenAudition)];
                    [document mouseUp:formantMouseEvent(
                        NSEventTypeLeftMouseUp, hiddenAudition)];
                    double hiddenRateAfter = -1.0;
                    double hiddenAuditionAfter = -1.0;
                    ok = ok && params->get_value(
                            plugin, 52u, &hiddenRateAfter)
                        && params->get_value(
                            plugin, 25u, &hiddenAuditionAfter)
                        && std::fabs(hiddenRateAfter
                            - hiddenRateBefore) < 1.0e-9
                        && std::fabs(hiddenAuditionAfter
                            - hiddenAuditionBefore) < 1.0e-9;
                    const auto* articulatorTail =
                        static_cast<const clap_plugin_tail_t*>(
                            plugin->get_extension(plugin, CLAP_EXT_TAIL));
                    const uint32_t bankTail = articulatorTail
                        ? articulatorTail->get(plugin) : 0u;
                    ok = ok && articulatorTail && bankTail > 1024u
                        && bankTail < 0xffffffffu;
                    failureStage = "Formant Matrix SOURCES ownership";
                    // SOURCES owns the explicit modulator selector. Exercise
                    // its stepped menu so the editor cannot regress to an
                    // input-on-carrier topology while the parameter surface
                    // still happens to pass.
                    const NSPoint sourcesPage = NSMakePoint(250.0, 54.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, sourcesPage)];
                    ok = ok && [phraseField isHidden];
                    const NSPoint modulatorMenu = NSMakePoint(200.0, 113.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, modulatorMenu)];
                    const NSPoint blendItem = NSMakePoint(200.0, 175.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, blendItem)];
                    double guiModulatorSource = -1.0;
                    ok = ok && params->get_value(
                            plugin, 98u, &guiModulatorSource)
                        && guiModulatorSource == 2.0;
                    if (!ok) {
                        std::cerr << "Formant SOURCES modulator got "
                            << guiModulatorSource << "\n";
                    }
                    // Pitch Source is the third SOURCES-row menu. Select its
                    // Voice Pitch item through the actual native dropdown,
                    // not by writing the CLAP parameter directly.
                    const NSPoint pitchSourceMenu =
                        NSMakePoint(200.0, 173.0);
                    hostContext.processRequested = false;
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, pitchSourceMenu)];
                    const NSPoint voicePitchItem =
                        NSMakePoint(200.0, 217.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, voicePitchItem)];
                    double guiPitchSource = -1.0;
                    ok = ok && params->get_value(
                            plugin, 1099u, &guiPitchSource)
                        && guiPitchSource == 1.0
                        && hostContext.processRequested;
                    if (!ok) {
                        std::cerr << "Formant SOURCES pitch got "
                            << guiPitchSource << " request="
                            << hostContext.processRequested << "\n";
                    }
                    // Pitch Scale uses the shared 102-entry multi-column
                    // menu. Select WHOLE TONE at canonical menu index 12;
                    // Continuous occupies index zero.
                    const NSPoint pitchScaleMenu = NSMakePoint(200.0, 233.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, pitchScaleMenu)];
                    const NSPoint wholeToneItem = NSMakePoint(
                        300.0, 238.0 + 12.0 * 18.0 + 9.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, wholeToneItem)];
                    double guiPitchScale = -1.0;
                    ok = ok && params->get_value(
                            plugin, 1101u, &guiPitchScale)
                        && guiPitchScale == 8.0;
                    if (!ok) {
                        std::cerr << "Formant SOURCES scale got "
                            << guiPitchScale << "\n";
                    }
                    const NSPoint phrasePage = NSMakePoint(342.0, 54.0);
                    [document mouseDown:formantMouseEvent(
                        NSEventTypeLeftMouseDown, phrasePage)];
                    ok = ok && ![phraseField isHidden];
                    if (!ok) {
                        std::cerr << "Formant PHRASE field hidden="
                            << [phraseField isHidden] << "\n";
                    }
                }
                if (ok) {
                    failureStage = "Formant Matrix phrase commit";
                    // Earlier migration cases legitimately queue several text
                    // programs while the synthetic host is sleeping. Run one
                    // short block so this GUI check begins with the same
                    // available handoff capacity as a host entering playback.
                    constexpr uint32_t kDrainFrames = 32u;
                    std::array<float, kDrainFrames> drainLeft {};
                    std::array<float, kDrainFrames> drainRight {};
                    std::array<float*, 2u> drainChannels {{
                        drainLeft.data(), drainRight.data(),
                    }};
                    clap_audio_buffer_t drainOutput {};
                    drainOutput.data32 = drainChannels.data();
                    drainOutput.channel_count = 2u;
                    clap_process_t drainBlock {};
                    drainBlock.frames_count = kDrainFrames;
                    drainBlock.audio_outputs = &drainOutput;
                    drainBlock.audio_outputs_count = 1u;
                    const bool drainActivated = plugin->activate(
                        plugin, 48000.0, 1u, kDrainFrames);
                    const bool drainProcessing = drainActivated
                        && plugin->start_processing(plugin);
                    ok = drainProcessing
                        && plugin->process(plugin, &drainBlock)
                            != CLAP_PROCESS_ERROR;
                    if (drainProcessing) plugin->stop_processing(plugin);
                    if (drainActivated) plugin->deactivate(plugin);
                }
                if (ok) {
                    [phraseField setStringValue:@"red violence rising"];
                    [phraseField sendAction:[phraseField action]
                        to:[phraseField target]];
                }
                double sequence = -1.0;
                ok = ok && params->get_value(plugin, 51u, &sequence)
                    && sequence == 5.0;

                const auto flushValue = [&](clap_id id, double value) {
                    SingleParamEventInput event {};
                    setSingleParamEvent(event, id, value);
                    params->flush(plugin, &event.events, nullptr);
                    double reported = -1.0;
                    return params->get_value(plugin, id, &reported)
                        && std::fabs(reported - value) < 1.0e-6;
                };
                ok = ok
                    && flushValue(54u, 0.0)
                    && flushValue(32u, 9.0)
                    && flushValue(58u, 3.0)
                    && flushValue(59u, 7.0)
                    && flushValue(60u, 0.42)
                    && flushValue(37u, 0.24)
                    && flushValue(25u, 1.0);

                double phraseMode = -1.0;
                double echoHeads = -1.0;
                double echoClock = -1.0;
                double echoMix = -1.0;
                double audition = -1.0;
                ok = ok
                    && params->get_value(plugin, 54u, &phraseMode)
                    && params->get_value(plugin, 58u, &echoHeads)
                    && params->get_value(plugin, 59u, &echoClock)
                    && params->get_value(plugin, 37u, &echoMix)
                    && params->get_value(plugin, 25u, &audition)
                    && phraseMode == 0.0 && echoHeads == 3.0
                    && echoClock == 7.0
                    && std::fabs(echoMix - 0.24) < 1.0e-6
                    && audition == 1.0;

                constexpr uint32_t kFrames = 256u;
                std::array<float, kFrames> left {};
                std::array<float, kFrames> right {};
                std::array<float*, 2u> channels {{
                    left.data(), right.data(),
                }};
                clap_audio_buffer_t outputBuffer {};
                outputBuffer.data32 = channels.data();
                outputBuffer.channel_count = 2u;
                CapturedOutputEvents captured {};
                captured.events.ctx = &captured;
                captured.events.try_push = captureOutputEvent;
                clap_process_t processBlock {};
                processBlock.frames_count = kFrames;
                processBlock.audio_outputs = &outputBuffer;
                processBlock.audio_outputs_count = 1u;
                processBlock.out_events = &captured.events;
                bool activated = false;
                bool processing = false;
                double energy = 0.0;
                if (ok) {
                    activated = plugin->activate(
                        plugin, 48000.0, 1u, kFrames);
                    processing = activated
                        && plugin->start_processing(plugin);
                    ok = processing;
                    for (uint32_t block = 0u; ok && block < 6u; ++block) {
                        std::fill(left.begin(), left.end(), 0.0f);
                        std::fill(right.begin(), right.end(), 0.0f);
                        ok = plugin->process(plugin, &processBlock)
                            != CLAP_PROCESS_ERROR;
                        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                            energy += std::fabs(left[frame])
                                + std::fabs(right[frame]);
                        }
                    }
                }
                ok = ok && energy > 1.0e-5;

                if (params) (void)flushValue(25u, 0.0);
                audition = -1.0;
                const bool auditionRead = params->get_value(
                    plugin, 25u, &audition);
                ok = ok && auditionRead && audition == 0.0;
                if (processing) {
                    std::fill(left.begin(), left.end(), 0.0f);
                    std::fill(right.begin(), right.end(), 0.0f);
                    (void)plugin->process(plugin, &processBlock);
                    plugin->stop_processing(plugin);
                }
                if (activated) plugin->deactivate(plugin);

                double silentMicEnergy = 0.0;
                double drivenMicEnergy = 0.0;
                if (ok) {
                    failureStage = "Formant Matrix classic mic topology";
                    ok = flushValue(1u, 14.0)
                        && flushValue(98u, 0.0)
                        && flushValue(99u, 0.0)
                        && flushValue(66u, 0.0)
                        && flushValue(65u, 1.0)
                        && flushValue(94u, 0.0)
                        && flushValue(96u, 0.0)
                        && flushValue(37u, 0.0)
                        && flushValue(25u, 1.0);
                }
                std::array<float, kFrames> micLeft {};
                std::array<float, kFrames> micRight {};
                std::array<float*, 2u> micChannels {{
                    micLeft.data(), micRight.data(),
                }};
                clap_audio_buffer_t micInputBuffer {};
                micInputBuffer.data32 = micChannels.data();
                micInputBuffer.channel_count = 2u;
                clap_process_t micProcessBlock = processBlock;
                micProcessBlock.audio_inputs = nullptr;
                micProcessBlock.audio_inputs_count = 0u;
                bool micActivated = false;
                bool micProcessing = false;
                if (ok) {
                    micActivated = plugin->activate(
                        plugin, 48000.0, 1u, kFrames);
                    micProcessing = micActivated
                        && plugin->start_processing(plugin);
                    ok = micProcessing;
                }
                for (uint32_t block = 0u;
                     ok && block < 4u; ++block) {
                    std::fill(left.begin(), left.end(), 0.0f);
                    std::fill(right.begin(), right.end(), 0.0f);
                    ok = plugin->process(plugin, &micProcessBlock)
                        != CLAP_PROCESS_ERROR;
                    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                        silentMicEnergy += std::fabs(left[frame])
                            + std::fabs(right[frame]);
                    }
                }
                micProcessBlock.audio_inputs = &micInputBuffer;
                micProcessBlock.audio_inputs_count = 1u;
                uint32_t micNoise = 0x6d2b79f5u;
                for (uint32_t block = 0u;
                     ok && block < 8u; ++block) {
                    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                        micNoise = micNoise * 1664525u + 1013904223u;
                        const float noise = static_cast<float>(
                            static_cast<int32_t>(micNoise))
                            / 2147483648.0f;
                        const float gate = ((block + frame / 48u) & 1u)
                            ? 0.24f : 0.08f;
                        micLeft[frame] = noise * gate;
                        micRight[frame] = micLeft[frame] * 0.92f;
                    }
                    std::fill(left.begin(), left.end(), 0.0f);
                    std::fill(right.begin(), right.end(), 0.0f);
                    ok = plugin->process(plugin, &micProcessBlock)
                        != CLAP_PROCESS_ERROR;
                    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                        drivenMicEnergy += std::fabs(left[frame])
                            + std::fabs(right[frame]);
                    }
                }
                ok = ok && drivenMicEnergy > 1.0e-4
                    && drivenMicEnergy > silentMicEnergy * 2.0 + 1.0e-5;
                if (micProcessing) plugin->stop_processing(plugin);
                if (micActivated) plugin->deactivate(plugin);
                if (params) (void)flushValue(25u, 0.0);
                hostContext.deferParamFlush = false;
                hostContext.paramFlushRequested = false;
                if (!ok) {
                    std::cerr << "Formant Matrix GUI details: sequence="
                        << sequence << " mode=" << phraseMode
                        << " heads=" << echoHeads << " clock=" << echoClock
                        << " mix=" << echoMix << " audition=" << audition
                        << " energy=" << energy << " events="
                        << captured.values.size() << " mic="
                        << drivenMicEnergy << " silentMic="
                        << silentMicEnergy << "\n";
                }
            } @catch (NSException* exception) {
                hostContext.deferParamFlush = false;
                hostContext.paramFlushRequested = false;
                std::cerr << "Formant Matrix GUI exception: "
                    << [[exception reason] UTF8String] << "\n";
                ok = false;
            }
        }
        const bool topologyProcessor = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.delay-processor-8ch") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.delay-processor-24ch") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.wave-geometry-processor") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.spectral-topology-processor") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.spectral-topology-processor-24ch") == 0;
        const bool spectralTopology = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.spectral-topology-processor") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.spectral-topology-processor-24ch") == 0;
        const bool waveGeometry = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.wave-geometry-processor") == 0;
        const bool delayProcessor = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.delay-processor-8ch") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.delay-processor-24ch") == 0;
        const bool documentationMacroRelationship = documentationCapture
            && (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.macro-delay-8ch") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.macro-delay-24ch") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.macro-pitch-8ch") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.macro-pitch-24ch") == 0);
        const bool documentationOutputAutogain = documentationCapture
            && (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.mc-to-stereo-autogain") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.mc-to-quad-autogain") == 0);
        const bool documentationLayoutPanner = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.layout-panner") == 0;
        const bool documentationDbapPanner = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.dbap-panner") == 0;
        const bool documentationLbapPanner = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.lbap-panner") == 0;
        const bool documentationVbapPanner = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.vbap-panner") == 0;
        const bool documentationGroupMatrix = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.group-matrix") == 0;
        const bool documentationNodeBusMixer = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.node-bus-mixer") == 0;
        const bool documentationSubCrossover = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.sub-crossover") == 0;
        const bool documentationArrayDelay = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.array-delay-16") == 0;
        const bool documentationArrayTrim = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.array-trim-16") == 0;
        const bool faultProcessor = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.fault") == 0;
        const bool errantProcessor = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.processor-errant") == 0;
        const bool noInputMixer = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.no-input-mixer-8ch") == 0;
        const bool feedbackShift = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.feedback-shift") == 0;
        const bool horizonEncoder = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-horizon-encoder-64") == 0;
        const bool cartographyEncoder = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-cartography-encoder-64") == 0;
        const bool analyzer = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.multichannel-meter-64") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambisonic-energy-visualizer-64") == 0;
        const bool partialTrace = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-effect-partial-trace-64") == 0;
        const bool responseTrace = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-effect-response-trace-64") == 0;
        const bool ambiEffectTrace = partialTrace || responseTrace;
        const bool documentationSpeakerDecoder = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-speaker-decoder-64") == 0;
        const bool documentationObjectDecoder = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-object-decoder-64") == 0;
        const bool documentationAdaptiveDecoder = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-adaptive-decoder-64") == 0;
        const bool documentationStereoDecoder = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambisonic-stereo-decoder") == 0;
        const bool documentationHeadDecoder = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambisonic-head-decoder") == 0;
        const bool documentationEffectDelay = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-effect-delay-64") == 0;
        const bool documentationEffectPitch = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-effect-pitch-64") == 0;
        const bool documentationEffectGain = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-effect-gain-64") == 0;
        const bool documentationResonancePrint = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-effect-resonance-print-64") == 0;
        const bool documentationDisplacement = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-effect-displacement-64") == 0;
        const bool documentationRotate = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambisonic-rotate-64") == 0;
        const bool documentationOrderBand = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambisonic-order-band-tool-64") == 0;
        const bool documentationAmbiGroupMatrix = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-group-matrix") == 0;
        const bool parameterSurfaceEncoder = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-stochastic-encoder-64") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-wrangler-encoder-64") == 0;
        const bool drumInstrument = std::strcmp(
                pluginId, "org.s3g.s3g-dsp.drum-kick") == 0
            || std::strcmp(
                pluginId, "org.s3g.s3g-dsp.drum-snare") == 0
            || std::strcmp(
                pluginId, "org.s3g.s3g-dsp.drum-floor-tom") == 0
            || std::strcmp(
                pluginId, "org.s3g.s3g-dsp.drum-toms") == 0;
        const bool drumOverload = std::strcmp(
                pluginId, "org.s3g.s3g-dsp.drum-overload") == 0;
        const bool samplePlayer = std::strcmp(
                pluginId, "org.s3g.s3g-dsp.sample-player") == 0
            || std::strcmp(
                pluginId, "org.s3g.s3g-dsp.sample-player-16") == 0;
        if (ok && samplePlayer && documentationCapture) {
            failureStage = "Sample Player documentation sample";
            @try {
                ok = [document respondsToSelector:
                        @selector(loadDocumentationSample)]
                    && [document loadDocumentationSample];
            } @catch (NSException*) {
                ok = false;
            }
        }
        NSPanel* parameterSurfacePanel = nil;
        auto mouseEvent = [&](NSEventType type, NSPoint documentPoint) {
            return [NSEvent
                mouseEventWithType:type
                location:[document convertPoint:documentPoint toView:nil]
                modifierFlags:0
                timestamp:0.0
                windowNumber:0
                context:nil
                eventNumber:0
                clickCount:1
                pressure:1.0];
        };
        if (ok && samplePlayer && !documentationCapture) {
            failureStage = "Sample Player play-mode/filter menu hover";
            @try {
                const NSPoint playModeMenu = NSMakePoint(200.0, 378.0);
                const NSPoint reverseItem = NSMakePoint(200.0, 437.0);
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, playModeMenu)];
                ok = [[document valueForKey:@"playModeMenuOpen"] boolValue]
                    && [[document valueForKey:@"playModeMenuHover"]
                        intValue] == -1;
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, reverseItem)];
                    ok = [[document valueForKey:@"playModeMenuHover"]
                        intValue] == 2;
                }
                if (ok) {
                    [document mouseExited:mouseEvent(
                        NSEventTypeMouseMoved, reverseItem)];
                    ok = [[document valueForKey:@"playModeMenuOpen"]
                            boolValue]
                        && [[document valueForKey:@"playModeMenuHover"]
                            intValue] == -1;
                }
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, reverseItem)];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, reverseItem)];
                }
                double playMode = -1.0;
                ok = ok && params->get_value(plugin, 1u, &playMode)
                    && std::fabs(playMode - 2.0) < 0.000001
                    && ![[document valueForKey:@"playModeMenuOpen"]
                        boolValue]
                    && [[document valueForKey:@"playModeMenuHover"]
                        intValue] == -1;
                const NSPoint filterMenu = NSMakePoint(700.0, 378.0);
                const NSPoint bandPassItem = NSMakePoint(700.0, 437.0);
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, filterMenu)];
                    ok = [[document valueForKey:@"filterTypeMenuOpen"]
                            boolValue]
                        && [[document valueForKey:@"filterTypeMenuHover"]
                            intValue] == -1;
                }
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, bandPassItem)];
                    ok = [[document valueForKey:@"filterTypeMenuHover"]
                        intValue] == 2;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, bandPassItem)];
                }
                double filterType = -1.0;
                ok = ok && params->get_value(plugin, 17u, &filterType)
                    && std::fabs(filterType - 2.0) < 0.000001
                    && ![[document valueForKey:@"filterTypeMenuOpen"]
                        boolValue]
                    && [[document valueForKey:@"filterTypeMenuHover"]
                        intValue] == -1;
                const NSPoint pitchModeMenu = NSMakePoint(200.0, 580.0);
                const NSPoint stretchItem = NSMakePoint(200.0, 619.0);
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, pitchModeMenu)];
                    ok = [[document valueForKey:@"pitchModeMenuOpen"]
                            boolValue]
                        && [[document valueForKey:@"pitchModeMenuHover"]
                            intValue] == -1;
                }
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, stretchItem)];
                    ok = [[document valueForKey:@"pitchModeMenuHover"]
                        intValue] == 1;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, stretchItem)];
                }
                double pitchMode = -1.0;
                ok = ok && params->get_value(plugin, 21u, &pitchMode)
                    && std::fabs(pitchMode - 1.0) < 0.000001
                    && ![[document valueForKey:@"pitchModeMenuOpen"]
                        boolValue]
                    && [[document valueForKey:@"pitchModeMenuHover"]
                        intValue] == -1;
                if (!ok) {
                    std::cerr << "Sample Player menu details: open="
                        << [[document valueForKey:@"playModeMenuOpen"]
                            boolValue]
                        << " hover="
                        << [[document valueForKey:@"playModeMenuHover"]
                            intValue]
                        << " value=" << playMode
                        << " filterOpen="
                        << [[document valueForKey:@"filterTypeMenuOpen"]
                            boolValue]
                        << " filterHover="
                        << [[document valueForKey:@"filterTypeMenuHover"]
                            intValue]
                        << " filterValue=" << filterType
                        << " pitchOpen="
                        << [[document valueForKey:@"pitchModeMenuOpen"]
                            boolValue]
                        << " pitchHover="
                        << [[document valueForKey:@"pitchModeMenuHover"]
                            intValue]
                        << " pitchValue=" << pitchMode << "\n";
                }
            } @catch (NSException* exception) {
                std::cerr << "Sample Player menu exception: "
                    << [[exception reason] UTF8String] << "\n";
                ok = false;
            }
        }
        if (ok && drumOverload && !documentationCapture) {
            failureStage = "Drum Overload dropdown and DRIVE hit map";
            @try {
                // The compact-effect DRIVE panel begins below the three-row
                // OUTPUT panel. Its first control is the CIRCUIT menu.
                const NSPoint circuitMenu = NSMakePoint(235.0, 202.0);
                const NSPoint transformerItem = NSMakePoint(235.0, 312.0);
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, circuitMenu)];
                ok = [[document valueForKey:@"openMenu"] intValue] == 1;
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, transformerItem)];
                    ok = [[document valueForKey:@"hoverMenuItem"] intValue]
                        == 5;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, transformerItem)];
                }
                double circuit = -1.0;
                ok = ok && params->get_value(plugin, 1u, &circuit)
                    && std::fabs(circuit - 5.0) < 0.000001
                    && [[document valueForKey:@"openMenu"] intValue] == 0;
                if (!ok) {
                    std::cerr << "Drum Overload dropdown details: open="
                        << [[document valueForKey:@"openMenu"] intValue]
                        << " hover="
                        << [[document valueForKey:@"hoverMenuItem"] intValue]
                        << " circuit=" << circuit << "\n";
                }
                SingleParamEventInput restoreCircuit {};
                setSingleParamEvent(restoreCircuit, 1u, 0.0);
                params->flush(plugin, &restoreCircuit.events, nullptr);

                // CIRCUIT occupies row zero, so the DRIVE sliders must map
                // to their drawn rows one through four.
                const NSPoint inputSlider = NSMakePoint(275.0, 222.0);
                const NSPoint punchSlider = NSMakePoint(275.0, 300.0);
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, inputSlider)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, inputSlider)];
                }
                double driveInput = 0.0;
                double overload = 0.0;
                ok = ok && params->get_value(plugin, 2u, &driveInput)
                    && params->get_value(plugin, 3u, &overload)
                    && driveInput > 23.0
                    && std::fabs(overload - 0.62) < 0.000001;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, punchSlider)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, punchSlider)];
                }
                double punch = 0.0;
                ok = ok && params->get_value(plugin, 5u, &punch)
                    && punch > 0.95;
                if (!ok) {
                    std::cerr << "Drum Overload DRIVE details: input="
                        << driveInput << " overload=" << overload
                        << " punch=" << punch << "\n";
                }
                SingleParamEventInput restoreDrive {};
                setSingleParamEvent(restoreDrive, 2u, 0.0);
                params->flush(plugin, &restoreDrive.events, nullptr);
                setSingleParamEvent(restoreDrive, 5u, 0.24);
                params->flush(plugin, &restoreDrive.events, nullptr);
            } @catch (NSException* exception) {
                std::cerr << "Drum Overload dropdown exception: "
                    << [[exception reason] UTF8String] << "\n";
                ok = false;
            }
        }
        if (ok && drumInstrument && !documentationCapture) {
            failureStage = "drum RANDOM safe parameter ownership";
            constexpr clap_id kNoteTrackingId = 2u;
            constexpr clap_id kStereoWidthId = 24u;
            constexpr clap_id kVelocityId = 25u;
            constexpr clap_id kOutputId = 26u;
            constexpr clap_id kTriggerId = 27u;
            constexpr std::array<clap_id, 23u> kRandomizedIds {{
                1u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u,
                13u, 14u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u,
                23u, kStereoWidthId,
            }};

            const auto setParam = [&](clap_id id, double value) {
                SingleParamEventInput input {};
                setSingleParamEvent(input, id, value);
                params->flush(plugin, &input.events, nullptr);
                double actual = 0.0;
                return params->get_value(plugin, id, &actual)
                    && std::fabs(actual - value) < 1.0e-6;
            };
            ok = params && params->flush && params->get_value
                && setParam(kNoteTrackingId, 0.371)
                && setParam(kVelocityId, 0.427)
                && setParam(kOutputId, -17.25)
                && setParam(kStereoWidthId, 0.0);

            std::array<double, 28u> before {};
            std::array<double, 28u> after {};
            for (clap_id id = 1u; ok && id <= kTriggerId; ++id) {
                ok = params->get_value(plugin, id, &before[id]);
            }

            CapturedOutputEvents captured {};
            captured.events.ctx = &captured;
            captured.events.try_push = captureOutputEvent;
            hostContext.deferParamFlush = true;
            hostContext.paramFlushRequested = false;
            if (ok) {
                const auto titleBand = s3g::clap_gui::encoderTitleBand(
                    nativeWidth, nativeHeight);
                const NSPoint randomPoint = NSMakePoint(
                    titleBand.randomButton.x
                        + titleBand.randomButton.width * 0.5,
                    titleBand.randomButton.y
                        + titleBand.randomButton.height * 0.5);
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, randomPoint)];
                [document mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, randomPoint)];
            }
            hostContext.deferParamFlush = false;
            if (ok) params->flush(plugin, nullptr, &captured.events);
            hostContext.paramFlushRequested = false;

            uint32_t changed = 0u;
            for (clap_id id = 1u; ok && id <= kTriggerId; ++id) {
                ok = params->get_value(plugin, id, &after[id])
                    && std::isfinite(after[id]);
                if (ok && std::fabs(after[id] - before[id]) > 1.0e-6) {
                    ++changed;
                }
            }
            ok = ok
                && after[kNoteTrackingId] == before[kNoteTrackingId]
                && after[kVelocityId] == before[kVelocityId]
                && after[kOutputId] == before[kOutputId]
                && after[kTriggerId] == before[kTriggerId]
                && changed >= 16u
                && captured.values.size() == kRandomizedIds.size() * 3u;

            for (size_t index = 0u; ok && index < kRandomizedIds.size();
                 ++index) {
                const clap_id id = kRandomizedIds[index];
                const size_t eventIndex = index * 3u;
                clap_param_info_t info {};
                ok = params->get_info(plugin, id - 1u, &info)
                    && info.id == id
                    && after[id] >= info.min_value
                    && after[id] <= info.max_value
                    && captured.values[eventIndex].type
                        == CLAP_EVENT_PARAM_GESTURE_BEGIN
                    && captured.values[eventIndex].paramId == id
                    && captured.values[eventIndex + 1u].type
                        == CLAP_EVENT_PARAM_VALUE
                    && captured.values[eventIndex + 1u].paramId == id
                    && std::fabs(captured.values[eventIndex + 1u].value
                        - after[id]) < 1.0e-6
                    && captured.values[eventIndex + 2u].type
                        == CLAP_EVENT_PARAM_GESTURE_END
                    && captured.values[eventIndex + 2u].paramId == id;
            }
            if (!ok) {
                std::cerr << "Drum RANDOM changed an owned parameter or "
                    "published an incomplete host gesture set for "
                          << pluginId << " (events="
                          << captured.values.size() << ", changed="
                          << changed << ")\n";
            }
        }
        if (ok && cartographyEncoder && !documentationCapture) {
            failureStage = "Cartography camera contract";
            @try {
                const NSRect fieldPanel = NSMakeRect(
                    18.0, 42.0, 650.0, 696.0);
                const auto cameraButton = [&](uint32_t index) {
                    return s3g::clap_gui::topologyProcessorCameraButtonRect(
                        fieldPanel, index);
                };
                const auto clickDocument = [&](NSPoint point) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, point)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, point)];
                };
                const auto clickRect = [&](NSRect rect) {
                    clickDocument(NSMakePoint(NSMidX(rect), NSMidY(rect)));
                };
                const auto setParamValue = [&](clap_id id, double value) {
                    SingleParamEventInput input {};
                    setSingleParamEvent(input, id, value);
                    params->flush(plugin, &input.events, nullptr);
                    double actual = 0.0;
                    return params->get_value(plugin, id, &actual)
                        && std::fabs(actual - value) < 0.0001;
                };
                const auto getParamValue = [&](clap_id id) {
                    double value = std::numeric_limits<double>::quiet_NaN();
                    (void)params->get_value(plugin, id, &value);
                    return value;
                };

                clickRect(cameraButton(0u));
                ok = [[document valueForKey:@"viewMode"] intValue] == 0
                    && std::fabs([[document valueForKey:@"viewAzDeg"]
                        doubleValue]) < 0.001
                    && std::fabs([[document valueForKey:@"viewElDeg"]
                        doubleValue]) < 0.001;
                NSPoint topGround = NSZeroPoint;
                NSPoint sideGround = NSZeroPoint;
                NSPoint threeQuarterGround = NSZeroPoint;
                if (ok) {
                    ok = [document respondsToSelector:
                        @selector(projectGroundPointX:y:)]
                        && [document respondsToSelector:
                            @selector(projectWorldPointX:y:z:)];
                    if (ok) {
                        topGround = [document
                            projectGroundPointX:0.0 y:0.75];
                        const NSPoint topRaised = [document
                            projectWorldPointX:0.0 y:0.75 z:0.5];
                        ok = std::fabs(topRaised.y - topGround.y) < 0.001;
                    }
                }

                if (ok) clickRect(cameraButton(1u));
                ok = ok
                    && [[document valueForKey:@"viewMode"] intValue] == 1
                    && std::fabs([[document valueForKey:@"viewAzDeg"]
                        doubleValue]) < 0.001
                    && std::fabs([[document valueForKey:@"viewElDeg"]
                        doubleValue] - 90.0) < 0.001;
                if (ok) {
                    sideGround = [document
                        projectGroundPointX:0.0 y:0.75];
                    const NSPoint sideRaised = [document
                        projectWorldPointX:0.0 y:0.75 z:0.5];
                    ok = std::fabs(topGround.y - sideGround.y) > 20.0
                        && sideRaised.y < sideGround.y - 20.0;
                }

                if (ok) clickRect(cameraButton(2u));
                ok = ok
                    && [[document valueForKey:@"viewMode"] intValue] == 2
                    && std::fabs([[document valueForKey:@"viewAzDeg"]
                        doubleValue] - 38.0) < 0.001
                    && std::fabs([[document valueForKey:@"viewElDeg"]
                        doubleValue] - 32.0) < 0.001;
                if (ok) {
                    threeQuarterGround = [document
                        projectGroundPointX:0.0 y:0.75];
                    const NSPoint threeQuarterRaised = [document
                        projectWorldPointX:0.0 y:0.75 z:0.5];
                    ok = std::fabs(threeQuarterGround.y
                        - sideGround.y) > 20.0
                        && threeQuarterRaised.y
                            < threeQuarterGround.y - 20.0;
                }

                const NSRect firstCamera = cameraButton(0u);
                const NSRect zoomPlus = NSMakeRect(
                    firstCamera.origin.x - 30.0,
                    fieldPanel.origin.y + 3.0, 18.0, 15.0);
                const double zoomBefore = [[document
                    valueForKey:@"viewZoom"] doubleValue];
                if (ok) clickRect(zoomPlus);
                const double zoomAfter = [[document
                    valueForKey:@"viewZoom"] doubleValue];
                ok = ok && zoomAfter > zoomBefore
                    && zoomAfter <= 2.20;

                const double listenerXBefore = getParamValue(8u);
                const double listenerYBefore = getParamValue(9u);
                const double listenerZBefore = getParamValue(10u);
                const double siteXBefore = getParamValue(30u);
                const double azimuthBefore = [[document
                    valueForKey:@"viewAzDeg"] doubleValue];
                const double elevationBefore = [[document
                    valueForKey:@"viewElDeg"] doubleValue];
                const NSPoint orbitStart = NSMakePoint(48.0, 690.0);
                const NSPoint orbitEnd = NSMakePoint(80.0, 704.0);
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, orbitStart)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, orbitEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, orbitEnd)];
                }
                const double customAzimuth = [[document
                    valueForKey:@"viewAzDeg"] doubleValue];
                const double customElevation = [[document
                    valueForKey:@"viewElDeg"] doubleValue];
                ok = ok
                    && [[document valueForKey:@"viewMode"] intValue] == -1
                    && std::fabs(customAzimuth - azimuthBefore) > 5.0
                    && std::fabs(customElevation - elevationBefore) > 2.0
                    && std::fabs(getParamValue(8u) - listenerXBefore) < 0.0001
                    && std::fabs(getParamValue(9u) - listenerYBefore) < 0.0001
                    && std::fabs(getParamValue(10u) - listenerZBefore) < 0.0001
                    && std::fabs(getParamValue(30u) - siteXBefore) < 0.0001;

                struct CameraSuffix {
                    uint32_t magic;
                    uint32_t version;
                    int32_t mode;
                    float azimuth;
                    float elevation;
                    float zoom;
                };
                static_assert(sizeof(CameraSuffix) == 24u);
                const auto* cartographyState = static_cast<
                    const clap_plugin_state_t*>(plugin->get_extension(
                        plugin, CLAP_EXT_STATE));
                MemoryPluginState cameraState;
                clap_ostream_t cameraOutput { &cameraState, stateWrite };
                CameraSuffix cameraSuffix {};
                ok = ok && cartographyState && cartographyState->save
                    && cartographyState->save(plugin, &cameraOutput)
                    && cameraState.bytes.size() >= sizeof(cameraSuffix);
                if (ok) {
                    std::memcpy(&cameraSuffix,
                        cameraState.bytes.data() + cameraState.bytes.size()
                            - sizeof(cameraSuffix),
                        sizeof(cameraSuffix));
                    ok = cameraSuffix.magic == 0x43414d52u
                        && cameraSuffix.version == 2u
                        && cameraSuffix.mode == -1
                        && std::fabs(cameraSuffix.azimuth
                            - customAzimuth) < 0.001
                        && std::fabs(cameraSuffix.elevation
                            - customElevation) < 0.001
                        && std::fabs(cameraSuffix.zoom
                            - zoomAfter) < 0.001;
                }

                const auto resetListener = [&] {
                    return setParamValue(8u, 0.0)
                        && setParamValue(9u, 0.0)
                        && setParamValue(10u, 0.0);
                };
                const NSPoint mapCenter = NSMakePoint(344.0, 397.0);
                const NSPoint positionEnd = NSMakePoint(404.0, 352.0);
                if (ok) {
                    ok = resetListener();
                    clickRect(cameraButton(0u));
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, mapCenter)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, positionEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, positionEnd)];
                    ok = ok && std::fabs(getParamValue(8u)) > 0.10
                        && std::fabs(getParamValue(9u)) > 0.10
                        && std::fabs(getParamValue(10u)) < 0.0001;
                }
                if (ok) {
                    ok = resetListener();
                    clickRect(cameraButton(1u));
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, mapCenter)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, positionEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, positionEnd)];
                    ok = ok && std::fabs(getParamValue(8u)) > 0.10
                        && std::fabs(getParamValue(9u)) < 0.0001
                        && std::fabs(getParamValue(10u)) > 0.10;
                }
                if (ok) {
                    ok = resetListener();
                    clickRect(cameraButton(2u));
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, mapCenter)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, positionEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, positionEnd)];
                    ok = ok
                        && [[document valueForKey:@"viewMode"] intValue] == 2
                        && std::fabs(getParamValue(8u)) < 0.0001
                        && std::fabs(getParamValue(9u)) < 0.0001
                        && std::fabs(getParamValue(10u)) < 0.0001;
                }

                const auto resetFirstSite = [&] {
                    return setParamValue(2u, 1.0)
                        && setParamValue(30u, 0.0)
                        && setParamValue(31u, 1.0)
                        && setParamValue(32u, 0.0);
                };
                const auto parkListener = [&] {
                    return setParamValue(8u, -1.0)
                        && setParamValue(9u, -1.0)
                        && setParamValue(10u, -1.0);
                };
                if (ok) {
                    ok = resetFirstSite() && parkListener();
                    clickRect(cameraButton(0u));
                    const NSPoint sitePoint = [document
                        projectGroundPointX:0.0 y:1.0];
                    const NSPoint siteEnd = NSMakePoint(
                        sitePoint.x + 54.0, sitePoint.y - 42.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, sitePoint)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, siteEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, siteEnd)];
                    ok = ok && std::fabs(getParamValue(30u)) > 0.10
                        && std::fabs(getParamValue(31u) - 1.0) > 0.10
                        && std::fabs(getParamValue(32u)) < 0.0001;
                }
                if (ok) {
                    ok = resetFirstSite();
                    clickRect(cameraButton(1u));
                    const NSPoint sitePoint = [document
                        projectGroundPointX:0.0 y:1.0];
                    const NSPoint siteEnd = NSMakePoint(
                        sitePoint.x + 54.0, sitePoint.y - 42.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, sitePoint)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, siteEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, siteEnd)];
                    ok = ok && std::fabs(getParamValue(30u)) > 0.10
                        && std::fabs(getParamValue(31u) - 1.0) < 0.0001
                        && std::fabs(getParamValue(32u)) > 0.10;
                }
                if (ok) {
                    ok = resetFirstSite();
                    clickRect(cameraButton(2u));
                    const NSPoint sitePoint = [document
                        projectGroundPointX:0.0 y:1.0];
                    const NSPoint siteEnd = NSMakePoint(
                        sitePoint.x + 54.0, sitePoint.y - 42.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, sitePoint)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, siteEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, siteEnd)];
                    ok = ok
                        && [[document valueForKey:@"viewMode"] intValue] == 2
                        && std::fabs(getParamValue(30u)) < 0.0001
                        && std::fabs(getParamValue(31u) - 1.0) < 0.0001
                        && std::fabs(getParamValue(32u)) < 0.0001;
                }

                // Restore the documented default view for the render check.
                if (ok) {
                    const NSRect zoomMinus = NSMakeRect(
                        firstCamera.origin.x - 52.0,
                        fieldPanel.origin.y + 3.0, 18.0, 15.0);
                    clickRect(zoomMinus);
                    clickRect(cameraButton(2u));
                    ok = resetListener() && resetFirstSite();
                }
            } @catch (NSException* exception) {
                std::cerr << "Cartography camera exception: "
                          << [[exception reason] UTF8String] << "\n";
                ok = false;
            }
        }
        if (ok && horizonEncoder) {
            // Reproduce a host such as REAPER that drains the Cocoa
            // autorelease pool between opening a menu and redrawing it.
            // Dynamically generated static labels must own their strings.
            failureStage = "Horizon preset menu autorelease lifetime";
            const NSRect presetAnchor =
                s3g::clap_gui::encoderTitleActionRect(
                    nativeWidth, nativeHeight,
                    s3g::gui_layout::EncoderTitleAction::Preset);
            @try {
                @autoreleasepool {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(NSMidX(presetAnchor),
                            NSMidY(presetAnchor)))];
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                }
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];
                const NSPoint secondPreset = NSMakePoint(
                    NSMidX(presetAnchor),
                    NSMaxY(presetAnchor) + 2.0 + 21.0 * 1.5);
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, secondPreset)];
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];

                // The row has a generous hit target, but value mapping must
                // use the exact 82 px track drawn from panelX + 108.
                const auto clickHorizonSlider = [&](CGFloat x) {
                    const NSPoint point = NSMakePoint(x, 84.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, point)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, point)];
                };
                double reported = 0.0;
                clickHorizonSlider(738.0);
                ok = params->get_value(plugin, 24u, &reported)
                    && std::fabs(reported - (-60.0)) < 0.001;
                if (ok) {
                    clickHorizonSlider(820.0);
                    ok = params->get_value(plugin, 24u, &reported)
                        && std::fabs(reported - 12.0) < 0.001;
                }
                if (ok) clickHorizonSlider(799.5); // Restore -6 dB.
                const auto clickAzimuth = [&](CGFloat x) {
                    const NSPoint point = NSMakePoint(x, 110.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, point)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, point)];
                };
                if (ok) {
                    clickAzimuth(1004.0);
                    ok = params->get_value(plugin, 14u, &reported)
                        && std::fabs(reported - 180.0) < 0.001;
                }
                if (ok) {
                    clickAzimuth(1086.0);
                    ok = params->get_value(plugin, 14u, &reported)
                        && std::fabs(reported - (-180.0)) < 0.001;
                }
                if (ok) clickAzimuth(1045.0); // Restore 0 degrees.

                // Listener menus and Amount occupy a separately stacked
                // panel. Guard their exact drawn hit targets so additions to
                // the Horizon surface cannot revive the earlier row-offset
                // mismatch.
                failureStage = "Horizon listener control hit targets";
                const auto clickListenerAmount = [&](CGFloat x) {
                    const NSPoint point = NSMakePoint(x, 496.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, point)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, point)];
                };
                if (ok) {
                    clickListenerAmount(1004.0);
                    ok = params->get_value(plugin, 48u, &reported)
                        && std::fabs(reported) < 0.001;
                }
                if (ok) {
                    clickListenerAmount(1086.0);
                    ok = params->get_value(plugin, 48u, &reported)
                        && std::fabs(reported - 1.0) < 0.001;
                }
                if (ok) clickListenerAmount(1057.3); // Restore about 65%.
                if (ok) {
                    const NSPoint listenMenu = NSMakePoint(1066.0, 470.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, listenMenu)];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(1066.0, 518.5))];
                    ok = params->get_value(plugin, 47u, &reported)
                        && std::fabs(reported - 1.0) < 0.001;
                }
                if (ok) {
                    const NSPoint responseMenu = NSMakePoint(1066.0, 522.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, responseMenu)];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(1066.0, 612.5))];
                    ok = params->get_value(plugin, 49u, &reported)
                        && std::fabs(reported - 3.0) < 0.001;
                }

                // Identity now occupies the open space below the right-hand
                // listener column instead of touching the bottom canvas edge.
                failureStage = "Horizon right-column Identity hit target";
                const auto clickSeed = [&](CGFloat x) {
                    const NSPoint point = NSMakePoint(x, 588.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, point)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, point)];
                };
                if (ok) {
                    clickSeed(1004.0);
                    ok = params->get_value(plugin, 25u, &reported)
                        && std::fabs(reported - 1.0) < 0.001;
                }
                if (ok) {
                    clickSeed(1086.0);
                    ok = params->get_value(plugin, 25u, &reported)
                        && std::fabs(reported - 65535.0) < 0.001;
                }
                if (ok) clickSeed(1006.5); // Restore near the default seed.

                // Horizon follows the shared TOP / SIDE / 3/4 camera order.
                // TOP is the XY azimuth plane; SIDE is the XZ elevation
                // plane. Guard the preset transforms as well as the labels.
                failureStage = "Horizon TOP/SIDE AED camera convention";
                if (ok) [document setViewPreset:0];
                ok = ok
                    && [[document valueForKey:@"viewMode"] intValue] == 0
                    && std::fabs([[document valueForKey:@"viewAzDeg"]
                        doubleValue]) < 0.001
                    && std::fabs([[document valueForKey:@"viewElDeg"]
                        doubleValue]) < 0.001;
                NSPoint topGridPoint = NSZeroPoint;
                NSPoint sideGridPoint = NSZeroPoint;
                NSPoint threeQuarterGridPoint = NSZeroPoint;
                if (ok) {
                    ok = [document respondsToSelector:
                        @selector(projectGroundPointX:y:)];
                    if (ok) topGridPoint = [document
                        projectGroundPointX:0.0 y:0.75];
                }
                if (ok) [document setViewPreset:1];
                ok = ok
                    && [[document valueForKey:@"viewMode"] intValue] == 1
                    && std::fabs([[document valueForKey:@"viewAzDeg"]
                        doubleValue]) < 0.001
                    && std::fabs([[document valueForKey:@"viewElDeg"]
                        doubleValue] + 90.0) < 0.001;
                if (ok) {
                    sideGridPoint = [document
                        projectGroundPointX:0.0 y:0.75];
                    // The XY ground plane is face-on in TOP and edge-on in
                    // SIDE. Its projected guide must therefore move onto the
                    // field horizon rather than remaining screen-fixed.
                    ok = std::fabs(topGridPoint.y - sideGridPoint.y) > 20.0;
                }
                if (ok) {
                    [document setViewPreset:2];
                    threeQuarterGridPoint = [document
                        projectGroundPointX:0.0 y:0.75];
                    ok = std::fabs(threeQuarterGridPoint.y
                        - sideGridPoint.y) > 20.0;
                }
                if (ok) {
                    const NSPoint dragStart = NSMakePoint(300.0, 390.0);
                    const NSPoint dragEnd = NSMakePoint(332.0, 408.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, dragStart)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, dragEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, dragEnd)];
                    const NSPoint draggedGridPoint = [document
                        projectGroundPointX:0.0 y:0.75];
                    ok = std::hypot(
                        draggedGridPoint.x - threeQuarterGridPoint.x,
                        draggedGridPoint.y - threeQuarterGridPoint.y) > 8.0;
                    [document setViewPreset:2];
                }

                // RANDOM is a non-factory scene with a new identity. It keeps
                // monitoring and listener choices and advertises that state in
                // the title instead of retaining a borrowed factory name.
                failureStage = "Horizon safe RANDOM scene and title";
                double seedBeforeRandom = 0.0;
                double outputBeforeRandom = 0.0;
                double orderBeforeRandom = 0.0;
                double listenModeBeforeRandom = 0.0;
                double listenAmountBeforeRandom = 0.0;
                double responseBeforeRandom = 0.0;
                if (ok) {
                    ok = params->get_value(plugin, 25u, &seedBeforeRandom)
                        && params->get_value(plugin, 24u, &outputBeforeRandom)
                        && params->get_value(plugin, 2u, &orderBeforeRandom)
                        && params->get_value(plugin, 47u, &listenModeBeforeRandom)
                        && params->get_value(plugin, 48u, &listenAmountBeforeRandom)
                        && params->get_value(plugin, 49u, &responseBeforeRandom);
                }
                if (ok) {
                    const NSRect randomButton =
                        s3g::clap_gui::encoderTitleActionRect(
                            nativeWidth, nativeHeight,
                            s3g::gui_layout::EncoderTitleAction::Random);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(NSMidX(randomButton),
                            NSMidY(randomButton)))];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp,
                        NSMakePoint(NSMidX(randomButton),
                            NSMidY(randomButton)))];
                }
                double randomizedSeed = 0.0;
                double randomizedActivity = 0.0;
                double preserved = 0.0;
                if (ok) {
                    ok = [[document valueForKey:@"presetName"]
                            isEqualToString:@"RANDOM"]
                        && params->get_value(plugin, 25u, &randomizedSeed)
                        && randomizedSeed != seedBeforeRandom
                        && params->get_value(plugin, 5u, &randomizedActivity)
                        && randomizedActivity >= 0.30
                        && randomizedActivity <= 0.68;
                }
                if (ok) {
                    ok = params->get_value(plugin, 24u, &preserved)
                        && std::fabs(preserved - outputBeforeRandom) < 0.001
                        && params->get_value(plugin, 2u, &preserved)
                        && std::fabs(preserved - orderBeforeRandom) < 0.001
                        && params->get_value(plugin, 47u, &preserved)
                        && std::fabs(preserved - listenModeBeforeRandom) < 0.001
                        && params->get_value(plugin, 48u, &preserved)
                        && std::fabs(preserved - listenAmountBeforeRandom) < 0.001
                        && params->get_value(plugin, 49u, &preserved)
                        && std::fabs(preserved - responseBeforeRandom) < 0.001;
                }
                if (ok) {
                    double hostPresetValue = 0.0;
                    ok = params->get_value(plugin, 1u, &hostPresetValue)
                        && hostPresetValue >= 0.0
                        && hostPresetValue < 16.0;
                }

                // The internal RANDOM marker is serialized separately from
                // the host's bounded factory-preset parameter.
                if (ok) {
                    const auto* horizonState =
                        static_cast<const clap_plugin_state_t*>(
                            plugin->get_extension(plugin, CLAP_EXT_STATE));
                    MemoryPluginState randomState;
                    clap_ostream_t stateOutput { &randomState, stateWrite };
                    ok = horizonState && horizonState->save
                        && horizonState->load
                        && horizonState->save(plugin, &stateOutput)
                        && !randomState.bytes.empty();
                    SingleParamEventInput factoryMutation {};
                    if (ok) {
                        setSingleParamEvent(factoryMutation, 1u, 0.0);
                        params->flush(plugin, &factoryMutation.events, nullptr);
                        randomState.offset = 0u;
                        clap_istream_t stateInput { &randomState, stateRead };
                        ok = horizonState->load(plugin, &stateInput)
                            && randomState.offset == randomState.bytes.size();
                    }
                    if (ok) {
                        [document performSelector:@selector(refreshSnapshot)];
                        double recalledSeed = 0.0;
                        ok = [[document valueForKey:@"presetName"]
                                isEqualToString:@"RANDOM"]
                            && params->get_value(plugin, 25u, &recalledSeed)
                            && std::fabs(recalledSeed - randomizedSeed) < 0.001;
                    }
                }
            } @catch (NSException* exception) {
                std::cerr << "Horizon preset lifetime exception: "
                          << [[exception reason] UTF8String] << "\n";
                ok = false;
            }
        }
        if (ok && queuedOwnershipEncoder && !documentationCapture) {
            failureStage = "queued GUI gesture publication";
            clap_param_info_t outputInfo {};
            bool foundOutput = false;
            for (uint32_t index = 0u; index < params->count(plugin); ++index) {
                clap_param_info_t candidate {};
                if (!params->get_info(plugin, index, &candidate)) break;
                if (std::strcmp(candidate.name, "Output") == 0) {
                    outputInfo = candidate;
                    foundOutput = true;
                    break;
                }
            }
            const NSPoint outputPoint = NSMakePoint(760.0, 78.0);
            NSView* hit = document;
            hostContext.deferParamFlush = true;
            hostContext.paramFlushRequested = false;
            if (foundOutput && hit == document) {
                [hit mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, outputPoint)];
                [hit mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, outputPoint)];
            }
            CapturedOutputEvents captured {};
            captured.events.ctx = &captured;
            captured.events.try_push = captureOutputEvent;
            hostContext.deferParamFlush = false;
            if (params->flush) {
                params->flush(plugin, nullptr, &captured.events);
            }
            hostContext.paramFlushRequested = false;
            size_t begin = captured.values.size();
            size_t value = captured.values.size();
            size_t end = captured.values.size();
            for (size_t index = 0u; index < captured.values.size(); ++index) {
                const auto& event = captured.values[index];
                if (event.paramId != outputInfo.id) continue;
                if (event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                    && begin == captured.values.size()) begin = index;
                else if (event.type == CLAP_EVENT_PARAM_VALUE
                    && value == captured.values.size()) value = index;
                else if (event.type == CLAP_EVENT_PARAM_GESTURE_END
                    && end == captured.values.size()) end = index;
            }
            ok = foundOutput && hit == document
                && begin < value && value < end;
            if (!ok) {
                std::cerr << "Queued GUI gesture publication failed for "
                    << pluginId << " (events=" << captured.values.size()
                    << ")\n";
            }
        }
        const bool ambiEncoderAcid = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-encoder-acid-16") == 0;
        if (ok && ambiEncoderAcid && !documentationCapture) {
            failureStage = "Ambi Encoder Acid compact note interaction";
            constexpr clap_id noteId = 112u;
            constexpr clap_id gateId = 113u;
            double originalNote = 0.0;
            double originalGate = 0.0;
            ok = params && params->flush
                && params->get_value(plugin, noteId, &originalNote)
                && params->get_value(plugin, gateId, &originalGate);
            CapturedOutputEvents captured {};
            captured.events.ctx = &captured;
            captured.events.try_push = captureOutputEvent;
            if (ok) {
                hostContext.deferParamFlush = true;
                const NSPoint notePoint = NSMakePoint(213.0, 100.0);
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, notePoint)];
                [document mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, notePoint)];
                const NSPoint gatePoint = NSMakePoint(198.0, 234.0);
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, gatePoint)];
                [document mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, gatePoint)];
                hostContext.deferParamFlush = false;
                params->flush(plugin, nullptr, &captured.events);
                hostContext.paramFlushRequested = false;
            }
            double editedNote = 0.0;
            double editedGate = 0.0;
            size_t begin = captured.values.size();
            size_t value = captured.values.size();
            size_t end = captured.values.size();
            for (size_t index = 0u; index < captured.values.size(); ++index) {
                const auto& event = captured.values[index];
                if (event.paramId != noteId) continue;
                if (event.type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                    && begin == captured.values.size()) begin = index;
                else if (event.type == CLAP_EVENT_PARAM_VALUE
                    && value == captured.values.size()) value = index;
                else if (event.type == CLAP_EVENT_PARAM_GESTURE_END
                    && end == captured.values.size()) end = index;
            }
            ok = ok
                && [[document valueForKey:@"selectedStep"] intValue] == 3
                && params->get_value(plugin, noteId, &editedNote)
                && params->get_value(plugin, gateId, &editedGate)
                && std::fabs(editedNote - 33.0) < 0.000001
                && std::fabs(editedGate - (originalGate >= 0.5 ? 0.0 : 1.0))
                    < 0.000001
                && begin < value && value < end;
            if (!ok) {
                std::cerr << "Acid note strip interaction failed (note="
                    << editedNote << ", gate=" << editedGate
                    << ", events=" << captured.values.size() << ")\n";
            }
            if (params && params->flush) {
                SingleParamEventInput restore {};
                setSingleParamEvent(restore, noteId, originalNote);
                params->flush(plugin, &restore.events, nullptr);
                setSingleParamEvent(restore, gateId, originalGate);
                params->flush(plugin, &restore.events, nullptr);
            }
            if (ok) {
                failureStage =
                    "Ambi Encoder Acid linked spatial step path";
                constexpr clap_id pathXId = 209u;
                constexpr clap_id pathYId = 210u;
                constexpr clap_id pathZId = 211u;
                double originalX = 0.0;
                double originalY = 0.0;
                double originalZ = 0.0;
                ok = params->get_value(plugin, pathXId, &originalX)
                    && params->get_value(plugin, pathYId, &originalY)
                    && params->get_value(plugin, pathZId, &originalZ);
                CapturedOutputEvents pathEvents {};
                pathEvents.events.ctx = &pathEvents;
                pathEvents.events.try_push = captureOutputEvent;
                if (ok) {
                    hostContext.deferParamFlush = true;
                    const NSPoint topPoint = NSMakePoint(500.0, 470.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, topPoint)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, topPoint)];
                    const NSPoint sidePoint = NSMakePoint(790.0, 410.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, sidePoint)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, sidePoint)];
                    hostContext.deferParamFlush = false;
                    params->flush(plugin, nullptr, &pathEvents.events);
                    hostContext.paramFlushRequested = false;
                }
                double editedX = 0.0;
                double editedY = 0.0;
                double editedZ = 0.0;
                bool xBegin = false, xValue = false, xEnd = false;
                bool yBegin = false, yValue = false, yEnd = false;
                bool zBegin = false, zValue = false, zEnd = false;
                for (const auto& emitted : pathEvents.values) {
                    bool* begin = emitted.paramId == pathXId
                        ? &xBegin : emitted.paramId == pathYId ? &yBegin
                            : emitted.paramId == pathZId ? &zBegin : nullptr;
                    bool* value = emitted.paramId == pathXId
                        ? &xValue : emitted.paramId == pathYId ? &yValue
                            : emitted.paramId == pathZId ? &zValue : nullptr;
                    bool* end = emitted.paramId == pathXId
                        ? &xEnd : emitted.paramId == pathYId ? &yEnd
                            : emitted.paramId == pathZId ? &zEnd : nullptr;
                    if (!begin) continue;
                    if (emitted.type == CLAP_EVENT_PARAM_GESTURE_BEGIN) *begin = true;
                    else if (emitted.type == CLAP_EVENT_PARAM_VALUE) *value = true;
                    else if (emitted.type == CLAP_EVENT_PARAM_GESTURE_END) *end = true;
                }
                ok = ok
                    && [[document valueForKey:@"selectedStep"] intValue] == 3
                    && params->get_value(plugin, pathXId, &editedX)
                    && params->get_value(plugin, pathYId, &editedY)
                    && params->get_value(plugin, pathZId, &editedZ)
                    && std::fabs(editedX - 0.304) < 0.015
                    && std::fabs(editedY + 0.338) < 0.015
                    && std::fabs(editedZ - 0.279) < 0.015
                    && xBegin && xValue && xEnd
                    && yBegin && yValue && yEnd
                    && zBegin && zValue && zEnd;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(272.0, 279.0))];
                    ok = [[document valueForKey:@"controlPage"] intValue] == 3;
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(224.0, 279.0))];
                    ok = ok
                        && [[document valueForKey:@"controlPage"] intValue] == 2;
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(176.0, 279.0))];
                    ok = ok
                        && [[document valueForKey:@"controlPage"] intValue] == 1;
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(128.0, 279.0))];
                    ok = ok
                        && [[document valueForKey:@"controlPage"] intValue] == 0;
                }
                if (!ok) {
                    std::cerr << "Acid spatial path interaction failed (x="
                              << editedX << ", y=" << editedY
                              << ", z=" << editedZ
                              << ", events=" << pathEvents.values.size()
                              << ")\n";
                }
                SingleParamEventInput restorePath {};
                setSingleParamEvent(restorePath, pathXId, originalX);
                params->flush(plugin, &restorePath.events, nullptr);
                setSingleParamEvent(restorePath, pathYId, originalY);
                params->flush(plugin, &restorePath.events, nullptr);
                setSingleParamEvent(restorePath, pathZId, originalZ);
                params->flush(plugin, &restorePath.events, nullptr);
            }
            if (ok) {
                failureStage = "Ambi Encoder Acid musical scale menu";
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, NSMakePoint(235.0, 434.0))];
                ok = [[document valueForKey:@"scaleMenuOpen"] boolValue];
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(130.0, 136.0))];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(130.0, 136.0))];
                }
                double selectedScale = 0.0;
                ok = ok
                    && ![[document valueForKey:@"scaleMenuOpen"] boolValue]
                    && params->get_value(plugin, 28u, &selectedScale)
                    && std::fabs(selectedScale - 31.0) < 0.000001;
                if (!ok) {
                    std::cerr << "Acid scale menu interaction failed (scale="
                              << selectedScale << ")\n";
                }
                SingleParamEventInput restoreScale {};
                setSingleParamEvent(restoreScale, 28u, 0.0);
                params->flush(plugin, &restoreScale.events, nullptr);
            }
            if (ok) {
                failureStage = "Ambi Encoder Acid stepped parameter menus";
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, NSMakePoint(235.0, 356.0))];
                ok = [[document valueForKey:@"parameterMenuOpen"] boolValue]
                    && [[document valueForKey:@"parameterMenuId"]
                        unsignedIntValue] == 3u;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(130.0, 311.0))];
                }
                double division = 0.0;
                ok = ok && params->get_value(plugin, 3u, &division)
                    && std::fabs(division - 6.0) < 0.000001;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(235.0, 535.0))];
                    ok = [[document valueForKey:@"parameterMenuOpen"] boolValue]
                        && [[document valueForKey:@"parameterMenuId"]
                            unsignedIntValue] == 0x7ffffff0u;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(130.0, 472.0))];
                }
                double order = 0.0;
                double formatMode = 1.0;
                ok = ok && params->get_value(plugin, 1u, &order)
                    && std::fabs(order - 1.0) < 0.000001
                    && params->get_value(plugin, 33u, &formatMode)
                    && std::fabs(formatMode) < 0.000001;
                if (!ok) {
                    std::cerr << "Acid stepped menu interaction failed (divide="
                              << division << ", order=" << order
                              << ", mode=" << formatMode << ")\n";
                }
                SingleParamEventInput restoreDiscrete {};
                setSingleParamEvent(restoreDiscrete, 3u, 4.0);
                params->flush(plugin, &restoreDiscrete.events, nullptr);
                setSingleParamEvent(restoreDiscrete, 1u, 3.0);
                params->flush(plugin, &restoreDiscrete.events, nullptr);
            }
            if (ok) {
                failureStage =
                    "Ambi Encoder Acid sub, drive, and output controls";
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, NSMakePoint(176.0, 279.0))];
                ok = [[document valueForKey:@"controlPage"] intValue] == 1;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(275.0, 382.0))];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, NSMakePoint(275.0, 382.0))];
                }
                double subLevel = 0.0;
                ok = ok && params->get_value(plugin, 30u, &subLevel)
                    && subLevel > 0.85;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(235.0, 356.0))];
                    ok = [[document valueForKey:@"parameterMenuOpen"]
                        boolValue]
                        && [[document valueForKey:@"parameterMenuId"]
                            unsignedIntValue] == 29u;
                }
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(130.0, 347.0))];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(130.0, 347.0))];
                }
                double subOctave = -1.0;
                ok = ok
                    && ![[document valueForKey:@"parameterMenuOpen"] boolValue]
                    && params->get_value(plugin, 29u, &subOctave)
                    && std::fabs(subOctave) < 0.000001;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(224.0, 279.0))];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(235.0, 460.0))];
                    ok = [[document valueForKey:@"circuitMenuOpen"] boolValue];
                }
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(130.0, 451.0))];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(130.0, 451.0))];
                }
                double circuit = 0.0;
                ok = ok
                    && ![[document valueForKey:@"circuitMenuOpen"] boolValue]
                    && params->get_value(plugin, 31u, &circuit)
                    && std::fabs(circuit - 8.0) < 0.000001;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(272.0, 279.0))];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(235.0, 535.0))];
                    ok = [[document valueForKey:@"parameterMenuOpen"]
                        boolValue]
                        && [[document valueForKey:@"parameterMenuId"]
                            unsignedIntValue] == 0x7ffffff0u;
                }
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(130.0, 526.0))];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(130.0, 526.0))];
                }
                double outputMode = 0.0;
                ok = ok && params->get_value(plugin, 33u, &outputMode)
                    && std::fabs(outputMode - 1.0) < 0.000001;
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(275.0, 568.0))];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, NSMakePoint(275.0, 568.0))];
                }
                double outputLevel = -10.0;
                ok = ok && params->get_value(plugin, 26u, &outputLevel)
                    && outputLevel > 5.0;
                if (!ok) {
                    std::cerr << "Acid voice/output interaction failed (sub="
                              << subLevel << ", octave=" << subOctave
                              << ", circuit=" << circuit
                              << ", mode=" << outputMode
                              << ", level=" << outputLevel << ")\n";
                }
                SingleParamEventInput restoreVoice {};
                setSingleParamEvent(restoreVoice, 29u, -1.0);
                params->flush(plugin, &restoreVoice.events, nullptr);
                setSingleParamEvent(restoreVoice, 30u, 0.0);
                params->flush(plugin, &restoreVoice.events, nullptr);
                setSingleParamEvent(restoreVoice, 31u, 0.0);
                params->flush(plugin, &restoreVoice.events, nullptr);
                setSingleParamEvent(restoreVoice, 33u, 0.0);
                params->flush(plugin, &restoreVoice.events, nullptr);
                setSingleParamEvent(restoreVoice, 26u, -10.0);
                params->flush(plugin, &restoreVoice.events, nullptr);
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, NSMakePoint(128.0, 279.0))];
            }
            if (ok) {
                failureStage =
                    "Ambi Encoder Acid preset, random, and host clock controls";
                @try {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(374.0, 20.0))];
                    ok = [[document valueForKey:@"presetMenuOpen"] boolValue];
                    if (ok) {
                        [document mouseMoved:mouseEvent(
                            NSEventTypeMouseMoved, NSMakePoint(374.0, 57.0))];
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown, NSMakePoint(374.0, 57.0))];
                    }
                    double secondNote = 0.0;
                    double firstSlide = 0.0;
                    ok = ok
                        && [[document valueForKey:@"presetIndex"] intValue] == 1
                        && params->get_value(plugin, 104u, &secondNote)
                        && params->get_value(plugin, 103u, &firstSlide)
                        && std::fabs(secondNote - 1.0) < 0.000001
                        && std::fabs(firstSlide - 1.0) < 0.000001;
                    if (ok) {
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown,
                            NSMakePoint(585.0, 20.0))];
                        ok = [[document valueForKey:@"presetIndex"] intValue]
                            == -1;
                    }
                    if (ok) {
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown,
                            NSMakePoint(235.0, 330.0))];
                        ok = [[document valueForKey:@"parameterMenuOpen"]
                            boolValue]
                            && [[document valueForKey:@"parameterMenuId"]
                                unsignedIntValue] == 27u;
                    }
                    if (ok) {
                        [document mouseMoved:mouseEvent(
                            NSEventTypeMouseMoved,
                            NSMakePoint(130.0, 321.0))];
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown,
                            NSMakePoint(130.0, 321.0))];
                        double clock = 0.0;
                        ok = params->get_value(plugin, 27u, &clock)
                            && std::fabs(clock - 1.0) < 0.000001;
                    }
                    SingleParamEventInput restoreClock {};
                    setSingleParamEvent(restoreClock, 27u, 0.0);
                    params->flush(plugin, &restoreClock.events, nullptr);
                    if (ok) {
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown,
                            NSMakePoint(374.0, 20.0))];
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown,
                            NSMakePoint(374.0, 39.0))];
                        ok = [[document valueForKey:@"presetIndex"] intValue]
                            == 0;
                    }
                } @catch (NSException* exception) {
                    std::cerr << "Acid preset/random interaction exception: "
                        << [[exception reason] UTF8String] << "\n";
                    ok = false;
                }
            }
        }
        const bool ambiEncoderMedium = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-encoder-medium-16") == 0;
        if (ok && ambiEncoderMedium && !documentationCapture) {
            failureStage = "Ambi Encoder Medium camera and dropdown contract";
            @try {
                ok = [document respondsToSelector:@selector(setViewPreset:)];
                if (ok) {
                    [document setViewPreset:0];
                    ok = [[document valueForKey:@"viewMode"] intValue] == 0;
                }

                // The standard title PRESET field is also a real dropdown,
                // backed by selectable Medium voicings.
                if (ok) {
                    const auto titleBand =
                        s3g::clap_gui::encoderTitleBand(920.0, 680.0);
                    const NSRect preset =
                        s3g::clap_gui::cocoaRect(titleBand.presetMenu);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(NSMidX(preset), NSMidY(preset)))];
                    ok = [[document valueForKey:@"menuItemCount"]
                            unsignedIntValue] == 7u;
                    if (ok) {
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown,
                            NSMakePoint(NSMidX(preset),
                                NSMaxY(preset) + 2.0 + 27.0))];
                        double speed = 0.0;
                        ok = params->get_value(plugin, 2u, &speed)
                            && std::fabs(speed - 210.0) < 0.000001;
                    }
                }

                // ORDER opens a real overlay and resolves an explicit row.
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 92.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                        == 1u;
                }
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(760.0, 135.0))];
                    ok = [[document valueForKey:@"hoverMenuItem"] intValue]
                        == 1;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 135.0))];
                    double order = 0.0;
                    ok = params->get_value(plugin, 1u, &order)
                        && std::fabs(order - 2.0) < 0.000001;
                }

                // NODE uses the same dropdown behavior and supports all eight
                // explicit choices rather than cycling on every click.
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 459.0))];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 592.0))];
                    double node = 0.0;
                    ok = params->get_value(plugin, 8u, &node)
                        && std::fabs(node - 7.0) < 0.000001;
                }

                // Continuous excitation follows the same explicit dropdown
                // contract: Percussive, Bow, Reed, or Air Jet.
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 478.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                        == 31u;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 521.0))];
                    double exciter = 0.0;
                    ok = params->get_value(plugin, 31u, &exciter)
                        && std::fabs(exciter - 1.0) < 0.000001;
                }

                // MASK edits the selected node's independent radiation weight
                // relative to the other seven nodes.
                if (ok) {
                    const NSPoint directivityPoint = NSMakePoint(795.0, 616.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, directivityPoint)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, directivityPoint)];
                    double directivity = 0.0;
                    ok = params->get_value(plugin, 46u, &directivity)
                        && std::fabs(directivity - 0.75) < 0.000001;
                }

                // SEQ exposes the same physical-exciter dropdown so Euclidean
                // gestures can select Off, Bow, Reed, or Air directly.
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(820.0, 434.0))];
                    ok = [[document valueForKey:@"excitationPage"] intValue]
                        == 1;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 460.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                        == 31u;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 539.0))];
                    double exciter = 0.0;
                    ok = params->get_value(plugin, 31u, &exciter)
                        && std::fabs(exciter - 3.0) < 0.000001;
                }

                // PULSES and ROTATE address the independently automatable
                // Euclidean lane belonging to the currently selected node.
                if (ok) {
                    const NSPoint pulsesPoint = NSMakePoint(729.0, 564.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, pulsesPoint)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, pulsesPoint)];
                    const NSPoint rotationPoint = NSMakePoint(762.0, 590.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, rotationPoint)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, rotationPoint)];
                    double pulses = 0.0;
                    double rotation = 0.0;
                    ok = params->get_value(plugin, 21u, &pulses)
                        && params->get_value(plugin, 29u, &rotation)
                        && std::fabs(pulses - 8.0) < 0.000001
                        && std::fabs(rotation - 16.0) < 0.000001;
                }

                // The sequencer pitch pool uses a scale dropdown and a
                // standard stepped slider locking the pool to 1-8 notes.
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 616.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                            == 48u
                        && [[document valueForKey:@"menuItemCount"]
                            unsignedIntValue] == 8u;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 514.0))];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(840.0, 642.0))];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, NSMakePoint(840.0, 642.0))];
                    double scale = 0.0;
                    double notes = 0.0;
                    ok = params->get_value(plugin, 48u, &scale)
                        && params->get_value(plugin, 49u, &notes)
                        && std::fabs(scale - 2.0) < 0.000001
                        && std::fabs(notes - 8.0) < 0.000001;
                }

                // MIDI is a third paged section so its full-size controls use
                // the same 36/26/24 row rhythm as the encoder family.
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(872.0, 434.0))];
                    ok = [[document valueForKey:@"excitationPage"] intValue]
                        == 2;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 460.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                        == 35u
                        && [[document valueForKey:@"menuItemCount"]
                            unsignedIntValue] == 4u;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 539.0))];
                    double midiMode = 0.0;
                    ok = params->get_value(plugin, 35u, &midiMode)
                        && std::fabs(midiMode - 3.0) < 0.000001;
                }

                // Shared camera buttons select canonical views, while an
                // empty-field drag transitions into a custom orbit.
                if (ok) {
                    const NSRect field = NSMakeRect(16.0, 50.0, 560.0, 614.0);
                    const NSRect threeQuarter =
                        s3g::clap_gui::topologyProcessorCameraButtonRect(
                            field, 2u);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(NSMidX(threeQuarter),
                            NSMidY(threeQuarter)))];
                    ok = [[document valueForKey:@"viewMode"] intValue] == 2;
                }
                if (ok) {
                    const double before =
                        [[document valueForKey:@"viewAzDeg"] doubleValue];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(30.0, 300.0))];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged,
                        NSMakePoint(54.0, 314.0))];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, NSMakePoint(54.0, 314.0))];
                    const double after =
                        [[document valueForKey:@"viewAzDeg"] doubleValue];
                    ok = [[document valueForKey:@"viewMode"] intValue] == -1
                        && after > before + 8.0;
                }
            } @catch (NSException* exception) {
                std::cerr << "Ambi Encoder Medium interaction exception: "
                    << [[exception reason] UTF8String] << "\n";
                ok = false;
            }
        }
        const bool ambiEncoderMembraneKick = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-encoder-membrane-kick-16") == 0;
        if (ok && ambiEncoderMembraneKick && !documentationCapture) {
            failureStage =
                "Ambi Encoder Membrane Kick factory preset dropdown";
            @try {
                double orderBefore = 0.0;
                double outputBefore = 0.0;
                ok = params->get_value(plugin, 1u, &orderBefore)
                    && params->get_value(plugin, 19u, &outputBefore);
                const auto titleBand =
                    s3g::clap_gui::encoderTitleBand(920.0, 680.0);
                const NSRect preset =
                    s3g::clap_gui::cocoaRect(titleBand.presetMenu);
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(NSMidX(preset), NSMidY(preset)))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                        == 0x7ffffff0u;
                }
                if (ok) {
                    const NSPoint quadItem = NSMakePoint(NSMidX(preset),
                        NSMaxY(preset) + 2.0 + 27.0);
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, quadItem)];
                    ok = [[document valueForKey:@"hoverMenuItem"] intValue]
                        == 1;
                }
                if (ok) {
                    // Select QUAD BASS 43, the second item in the bank.
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown,
                        NSMakePoint(NSMidX(preset),
                            NSMaxY(preset) + 2.0 + 27.0))];
                    double orderAfter = 0.0;
                    double outputAfter = 0.0;
                    double tune = 0.0;
                    double drop = 0.0;
                    ok = params->get_value(plugin, 1u, &orderAfter)
                        && params->get_value(plugin, 19u, &outputAfter)
                        && params->get_value(plugin, 3u, &tune)
                        && params->get_value(plugin, 4u, &drop)
                        && std::fabs(orderAfter - orderBefore) < 0.000001
                        && std::fabs(outputAfter - outputBefore) < 0.000001
                        && std::fabs(tune - 43.0) < 0.000001
                        && std::fabs(drop - 38.0) < 0.000001;
                }
                // FORMAT and SHAPE are explicit hoverable dropdowns, not
                // click-to-cycle controls.
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 80.0))];
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(760.0, 123.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                            == 1u
                        && [[document valueForKey:@"hoverMenuItem"] intValue]
                            == 1;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 123.0))];
                    double order = 0.0;
                    ok = params->get_value(plugin, 1u, &order)
                        && std::fabs(order - 2.0) < 0.000001;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 80.0))];
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(760.0, 159.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                            == 1u
                        && [[document valueForKey:@"menuItemCount"]
                            unsignedIntValue] == 5u
                        && [[document valueForKey:@"hoverMenuItem"] intValue]
                            == 3;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 159.0))];
                    double format = 0.0;
                    ok = params->get_value(plugin, 1u, &format)
                        && std::fabs(format - 4.0) < 0.000001;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 80.0))];
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(760.0, 177.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                            == 1u
                        && [[document valueForKey:@"menuItemCount"]
                            unsignedIntValue] == 5u
                        && [[document valueForKey:@"hoverMenuItem"] intValue]
                            == 4;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 177.0))];
                    double format = 0.0;
                    ok = params->get_value(plugin, 1u, &format)
                        && std::fabs(format - 5.0) < 0.000001;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 184.0))];
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(760.0, 263.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                            == 2u
                        && [[document valueForKey:@"hoverMenuItem"] intValue]
                            == 3;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 263.0))];
                    double shape = 0.0;
                    ok = params->get_value(plugin, 2u, &shape)
                        && std::fabs(shape - 3.0) < 0.000001;
                }
                // The STRIKE page exposes the automatable placement mode
                // and independent X/Y automation lanes.
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(862.0, 164.0))];
                    ok = [[document valueForKey:@"membranePage"] intValue]
                        == 1;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 184.0))];
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, NSMakePoint(760.0, 227.0))];
                    ok = [[document valueForKey:@"openMenu"] unsignedIntValue]
                            == 21u
                        && [[document valueForKey:@"hoverMenuItem"] intValue]
                            == 1;
                }
                if (ok) {
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, NSMakePoint(760.0, 227.0))];
                    const CGFloat controlX = static_cast<CGFloat>(
                        s3g::gui_layout::processorControlX(580.0));
                    const CGFloat trackWidth = static_cast<CGFloat>(
                        s3g::gui_layout::processorTrackWidth(324.0));
                    const NSPoint xPoint = NSMakePoint(
                        controlX + trackWidth * 0.75, 208.0);
                    const NSPoint yPoint = NSMakePoint(
                        controlX + trackWidth * 0.25, 232.0);
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, xPoint)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, xPoint)];
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, yPoint)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, yPoint)];
                    double mode = 0.0;
                    double strikeX = 0.0;
                    double strikeY = 0.0;
                    ok = params->get_value(plugin, 21u, &mode)
                        && params->get_value(plugin, 11u, &strikeX)
                        && params->get_value(plugin, 12u, &strikeY)
                        && std::fabs(mode - 1.0) < 0.000001
                        && std::fabs(strikeX - 0.5) < 0.02
                        && std::fabs(strikeY + 0.5) < 0.02;
                }
            } @catch (NSException* exception) {
                std::cerr
                    << "Ambi Encoder Membrane Kick preset exception: "
                    << [[exception reason] UTF8String] << "\n";
                ok = false;
            }
        }
        const bool cryosphere = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-cryosphere-encoder-64") == 0;
        const bool pyrosphere = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64") == 0;
        const bool water = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-water-encoder-64") == 0;
        const bool wind = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-wind-encoder-64") == 0;
        const bool insect = std::strcmp(
            pluginId,
            "org.s3g.s3g-dsp.ambi-insect-encoder-64") == 0;
        const bool environmentalSurface =
            cryosphere || pyrosphere || water || wind || insect;
        const bool documentationEncoder = documentationCapture
            && std::strncmp(requestedDescriptor->name,
                "s3g Ambi Encoder ",
                std::strlen("s3g Ambi Encoder ")) == 0;
        const bool documentationAmbiImprint = documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.ambi-imprint-64") == 0;
        if (ok && documentationAmbiImprint) {
            failureStage = "documentation Ambi Imprint atlas";
            ok = [document respondsToSelector:@selector(loadAtlasAtIndex:)];
            if (ok) {
                // ECHO / TWIN CHAMBERS exposes its coupled-room geometry and
                // long inter-chamber paths without requiring external media.
                [document loadAtlasAtIndex:14u];
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];
            }
        }
        if (ok && ambiEffectTrace) {
            failureStage = "Ambi Effect Trace AED elevation direction";
            if (scroll) {
                [[scroll contentView] scrollToPoint:NSMakePoint(200.0, 280.0)];
                [scroll reflectScrolledClipView:[scroll contentView]];
            }
            constexpr CGFloat maskPanelX = 648.0;
            constexpr CGFloat maskPanelWidth = 258.0;
            const CGFloat maskPanelY = partialTrace ? 582.0 : 438.0;
            const CGFloat elevationRowY = maskPanelY
                + static_cast<CGFloat>(
                    s3g::gui_layout::kStandardMetrics.firstRowOffset)
                + 2.0 * static_cast<CGFloat>(
                    s3g::gui_layout::kStandardMetrics.rowPitch);
            const CGFloat trackX = static_cast<CGFloat>(
                s3g::gui_layout::processorControlX(maskPanelX));
            const CGFloat trackWidth = static_cast<CGFloat>(
                s3g::gui_layout::processorTrackWidth(maskPanelWidth));
            constexpr clap_id maskElevationParam = 23u;
            auto clickElevation = [&](double normalized,
                                      double expectedDegrees) {
                const NSPoint point = NSMakePoint(
                    trackX + trackWidth * normalized, elevationRowY);
                NSView* hitView = [parent hitTest:
                    [parent convertPoint:point fromView:document]];
                if (hitView != document) return false;
                [hitView mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, point)];
                [hitView mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, point)];
                double reported = 0.0;
                return params->get_value(
                        plugin, maskElevationParam, &reported)
                    && std::fabs(reported - expectedDegrees) < 0.02;
            };
            ok = clickElevation(0.0, 90.0)
                && clickElevation(0.25, 45.0)
                && clickElevation(0.75, -45.0)
                && clickElevation(1.0, -90.0);
            if (!ok) {
                std::cerr << "Trace elevation did not run +90 left to -90 right\n";
            }
            if (scroll) {
                [[scroll contentView] scrollToPoint:NSZeroPoint];
                [scroll reflectScrolledClipView:[scroll contentView]];
            }
        }
        if (ok && environmentalSurface) {
            failureStage = "environmental RANDOM and SURF contract";
            const auto* pluginState =
                static_cast<const clap_plugin_state_t*>(
                    plugin->get_extension(plugin, CLAP_EXT_STATE));
            if (cryosphere || pyrosphere) {
                clap_param_info_t presetInfo {};
                bool foundPreset = false;
                for (uint32_t index = 0u; index < params->count(plugin);
                    ++index) {
                    if (params->get_info(plugin, index, &presetInfo)
                        && presetInfo.id == 1u) {
                        foundPreset = true;
                        break;
                    }
                }
                const std::array<const char*, 4u> expectedAlienNames =
                    cryosphere
                        ? std::array<const char*, 4u> {
                            "Tidal Ocean-Moon Rift",
                            "Methane-Ice Dune Creep",
                            "Subsurface Brine Upwelling",
                            "Quasicrystal Plate Bloom" }
                        : std::array<const char*, 4u> {
                            "Silicate Convection Sea",
                            "Dense-Atmosphere Hydrocarbon Front",
                            "Sulfur Vent Colony",
                            "Mineral Lattice Collapse" };
                ok = foundPreset && presetInfo.max_value == 17.0;
                for (uint32_t offset = 0u;
                    ok && offset < expectedAlienNames.size(); ++offset) {
                    char text[64] {};
                    ok = params->value_to_text(plugin, 1u,
                            static_cast<double>(14u + offset),
                            text, sizeof(text))
                        && std::strcmp(text,
                            expectedAlienNames[offset]) == 0;
                }
            }
            const std::array<clap_id, 6u> randomizedIds =
                (cryosphere || water)
                    ? std::array<clap_id, 6u> {
                        3u, 4u, 5u, 11u, 12u, 20u }
                    : (pyrosphere || wind)
                        ? std::array<clap_id, 6u> {
                            3u, 4u, 5u, 9u, 15u, 43u }
                        : std::array<clap_id, 6u> {
                            3u, 4u, 5u, 6u, 9u, 14u };
            const std::array<clap_id, 5u> protectedIds =
                (cryosphere || water)
                    ? std::array<clap_id, 5u> {
                        2u, 34u, 40u, 41u, 42u }
                    : (pyrosphere || wind)
                        ? std::array<clap_id, 5u> {
                            2u, 32u, 52u, 53u, 54u }
                        : std::array<clap_id, 5u> {
                            2u, 32u, 39u, 40u, 41u };
            auto readValues = [&](const auto& ids, auto& values) {
                for (size_t index = 0u; index < ids.size(); ++index) {
                    if (!params->get_value(
                            plugin, ids[index], &values[index])) {
                        return false;
                    }
                }
                return true;
            };
            auto changedValueCount = [](const auto& before,
                                        const auto& after) {
                size_t changed = 0u;
                for (size_t index = 0u; index < before.size(); ++index) {
                    if (std::fabs(before[index] - after[index]) > 1.0e-6)
                        ++changed;
                }
                return changed;
            };
            auto clickDocument = [&](NSPoint point) {
                NSView* hit = [parent hitTest:
                    [parent convertPoint:point fromView:document]];
                if (hit != document) return false;
                [hit mouseDown:mouseEvent(NSEventTypeLeftMouseDown, point)];
                [hit mouseUp:mouseEvent(NSEventTypeLeftMouseUp, point)];
                return true;
            };
            auto dragDocument = [&](NSPoint start, NSPoint end) {
                NSView* hit = [parent hitTest:
                    [parent convertPoint:start fromView:document]];
                if (hit != document) return false;
                [hit mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, start)];
                [hit mouseDragged:mouseEvent(
                    NSEventTypeLeftMouseDragged, end)];
                [hit mouseUp:mouseEvent(NSEventTypeLeftMouseUp, end)];
                return true;
            };
            auto saveState = [&](MemoryPluginState& memory) {
                clap_ostream_t output { &memory, stateWrite };
                return pluginState && pluginState->save
                    && pluginState->save(plugin, &output)
                    && !memory.bytes.empty();
            };

            const NSRect randomRect =
                s3g::clap_gui::encoderTitleActionRect(
                    nativeWidth, nativeHeight,
                    s3g::gui_layout::EncoderTitleAction::Random);
            const CGFloat visibleRight = scroll
                ? NSMaxX([[scroll contentView] bounds])
                : static_cast<CGFloat>(nativeWidth);
            const NSPoint randomPoint = NSMakePoint(
                std::clamp(NSMidX(randomRect), NSMinX(randomRect) + 3.0,
                    visibleRight - 3.0),
                NSMidY(randomRect));
            const NSRect fieldPanel = NSMakeRect(18.0, 42.0, 596.0, 608.0);
            const NSRect field = NSMakeRect(34.0, 76.0, 564.0, 558.0);
            const NSRect surfTab =
                s3g::clap_gui::environmentalFieldPageButtonRect(
                    fieldPanel, 1u);
            auto surfaceButton = [&](uint32_t index) {
                return NSMakeRect(field.origin.x + 10.0 + index * 52.0,
                    field.origin.y + 10.0, 46.0, 16.0);
            };

            std::array<double, randomizedIds.size()> initialValues {};
            std::array<double, randomizedIds.size()> firstRandom {};
            std::array<double, randomizedIds.size()> secondRandom {};
            std::array<double, randomizedIds.size()> thirdRandom {};
            std::array<double, protectedIds.size()> protectedBefore {};
            std::array<double, protectedIds.size()> protectedAfter {};
            auto environmentalCheck = [&](bool condition,
                                            const char* detail) {
                if (!condition) {
                    std::cerr << "Environmental surface failure: "
                        << detail << "\n";
                }
                return condition;
            };
            ok = environmentalCheck(
                    readValues(randomizedIds, initialValues),
                    "initial random parameters")
                && environmentalCheck(
                    readValues(protectedIds, protectedBefore),
                    "initial protected parameters")
                && environmentalCheck(
                    clickDocument(randomPoint), "first RANDOM click")
                && environmentalCheck(
                    readValues(randomizedIds, firstRandom),
                    "first RANDOM publication")
                && environmentalCheck(
                    changedValueCount(initialValues, firstRandom) >= 4u,
                    "first RANDOM variation")
                && environmentalCheck(clickDocument(NSMakePoint(
                        NSMidX(surfTab), NSMidY(surfTab))),
                    "SURF page selection")
                && environmentalCheck(clickDocument(NSMakePoint(
                        NSMidX(surfaceButton(2u)),
                        NSMidY(surfaceButton(2u)))),
                    "first surface capture")
                && environmentalCheck(
                    clickDocument(randomPoint), "second RANDOM click")
                && environmentalCheck(
                    readValues(randomizedIds, secondRandom),
                    "second RANDOM publication")
                && environmentalCheck(
                    changedValueCount(firstRandom, secondRandom) >= 4u,
                    "second RANDOM variation")
                && environmentalCheck(clickDocument(NSMakePoint(
                        NSMidX(surfaceButton(2u)),
                        NSMidY(surfaceButton(2u)))),
                    "second surface capture")
                && environmentalCheck(clickDocument(NSMakePoint(
                        NSMidX(surfaceButton(1u)),
                        NSMidY(surfaceButton(1u)))),
                    "surface enable");

            MemoryPluginState beforeRandomState;
            MemoryPluginState afterRandomState;
            MemoryPluginState reenabledState;
            ok = ok && environmentalCheck(
                    saveState(beforeRandomState), "enabled surface save")
                && environmentalCheck(
                    clickDocument(randomPoint), "third RANDOM click")
                && environmentalCheck(
                    readValues(randomizedIds, thirdRandom),
                    "third RANDOM publication")
                && environmentalCheck(
                    changedValueCount(secondRandom, thirdRandom) >= 4u,
                    "third RANDOM variation")
                && environmentalCheck(
                    readValues(protectedIds, protectedAfter),
                    "protected parameter publication")
                && environmentalCheck(protectedBefore == protectedAfter,
                    "protected parameters changed")
                && environmentalCheck(
                    saveState(afterRandomState), "bypassed surface save");

            auto surfaceContractHolds = [&](const auto& before,
                                            const auto& after) {
                return before.surface.enabled == 1u
                    && before.surface.cellCount == 2u
                    && after.surface.enabled == 0u
                    && after.surface.cellCount == before.surface.cellCount
                    && std::memcmp(before.surface.cells.data(),
                        after.surface.cells.data(),
                        sizeof(before.surface.cells)) == 0;
            };
            if (ok && cryosphere) {
                WorldSphereSavedState<s3g::AmbiCryosphereParams> before {};
                WorldSphereSavedState<s3g::AmbiCryosphereParams> after {};
                ok = decodeWorldSphereState(beforeRandomState, before)
                    && decodeWorldSphereState(afterRandomState, after)
                    && surfaceContractHolds(before, after);
            } else if (ok && pyrosphere) {
                WorldSphereSavedState<s3g::AmbiPyrosphereParams> before {};
                WorldSphereSavedState<s3g::AmbiPyrosphereParams> after {};
                ok = decodeWorldSphereState(beforeRandomState, before)
                    && decodeWorldSphereState(afterRandomState, after)
                    && surfaceContractHolds(before, after);
            } else if (ok && water) {
                WorldSphereSavedState<s3g::AmbiWaterParams> before {};
                WorldSphereSavedState<s3g::AmbiWaterParams> after {};
                ok = decodeWorldSphereState(beforeRandomState, before)
                    && decodeWorldSphereState(afterRandomState, after)
                    && surfaceContractHolds(before, after);
            } else if (ok && wind) {
                WorldSphereSavedState<s3g::AmbiWindParams> before {};
                WorldSphereSavedState<s3g::AmbiWindParams> after {};
                const bool decodedBefore = decodeWorldSphereState(
                    beforeRandomState, before);
                const bool decodedAfter = decodeWorldSphereState(
                    afterRandomState, after);
                const bool contract = decodedBefore && decodedAfter
                    && surfaceContractHolds(before, after);
                if (!contract) {
                    std::cerr << "Wind surface state before="
                        << before.surface.enabled << "/"
                        << before.surface.cellCount << " after="
                        << after.surface.enabled << "/"
                        << after.surface.cellCount << " bytes="
                        << beforeRandomState.bytes.size() << "/"
                        << afterRandomState.bytes.size() << " expected="
                        << sizeof(before) << "\n";
                }
                ok = contract;
            } else if (ok) {
                WorldSphereSavedState<s3g::AmbiInsectParams> before {};
                WorldSphereSavedState<s3g::AmbiInsectParams> after {};
                ok = decodeWorldSphereState(beforeRandomState, before)
                    && decodeWorldSphereState(afterRandomState, after)
                    && surfaceContractHolds(before, after);
            }

            // RANDOM bypasses rather than destroys the map: it can be
            // re-enabled immediately with both captured cells intact.
            ok = ok && clickDocument(NSMakePoint(
                    NSMidX(surfaceButton(1u)), NSMidY(surfaceButton(1u))))
                && saveState(reenabledState);
            auto reenabledContractHolds = [](const auto& state) {
                return state.surface.enabled == 1u
                    && state.surface.cellCount == 2u;
            };
            if (ok && cryosphere) {
                WorldSphereSavedState<s3g::AmbiCryosphereParams> reenabled {};
                ok = decodeWorldSphereState(reenabledState, reenabled)
                    && reenabledContractHolds(reenabled);
            } else if (ok && pyrosphere) {
                WorldSphereSavedState<s3g::AmbiPyrosphereParams> reenabled {};
                ok = decodeWorldSphereState(reenabledState, reenabled)
                    && reenabledContractHolds(reenabled);
            } else if (ok && water) {
                WorldSphereSavedState<s3g::AmbiWaterParams> reenabled {};
                ok = decodeWorldSphereState(reenabledState, reenabled)
                    && reenabledContractHolds(reenabled);
            } else if (ok && wind) {
                WorldSphereSavedState<s3g::AmbiWindParams> reenabled {};
                ok = decodeWorldSphereState(reenabledState, reenabled)
                    && environmentalCheck(reenabledContractHolds(reenabled),
                        "Wind surface re-enable state");
            } else if (ok) {
                WorldSphereSavedState<s3g::AmbiInsectParams> reenabled {};
                ok = decodeWorldSphereState(reenabledState, reenabled)
                    && reenabledContractHolds(reenabled);
            }
            if (ok) {
                ok = clickDocument(NSMakePoint(
                    NSMidX(surfaceButton(1u)), NSMidY(surfaceButton(1u))));
            }
            if (ok && !documentationCapture && queuedOwnershipEncoder) {
                failureStage = "ordered RANDOM and SURF publication";
                if (hostContext.callbackRequested && plugin->on_main_thread) {
                    plugin->on_main_thread(plugin);
                }
                hostContext.callbackRequested = false;
                const uint32_t rescansBefore = hostContext.paramRescanCount;
                hostContext.deferParamFlush = true;
                hostContext.paramFlushRequested = false;
                const bool queuedRandom = clickDocument(randomPoint);
                const bool queuedSurfaceEnable = queuedRandom
                    && clickDocument(NSMakePoint(
                        NSMidX(surfaceButton(1u)),
                        NSMidY(surfaceButton(1u))));
                CapturedOutputEvents captured {};
                captured.events.ctx = &captured;
                captured.events.try_push = captureOutputEvent;
                hostContext.deferParamFlush = false;
                if (queuedSurfaceEnable && params->flush) {
                    params->flush(plugin, nullptr, &captured.events);
                }
                hostContext.paramFlushRequested = false;
                if (hostContext.callbackRequested
                    && plugin->on_main_thread) {
                    plugin->on_main_thread(plugin);
                }
                hostContext.callbackRequested = false;
                MemoryPluginState orderedState;
                const bool savedOrdered = queuedSurfaceEnable
                    && saveState(orderedState);
                bool surfaceEnabled = false;
                if (savedOrdered && cryosphere) {
                    WorldSphereSavedState<s3g::AmbiCryosphereParams> state {};
                    surfaceEnabled = decodeWorldSphereState(
                            orderedState, state)
                        && state.surface.enabled == 1u;
                } else if (savedOrdered && pyrosphere) {
                    WorldSphereSavedState<s3g::AmbiPyrosphereParams> state {};
                    surfaceEnabled = decodeWorldSphereState(
                            orderedState, state)
                        && state.surface.enabled == 1u;
                } else if (savedOrdered && water) {
                    WorldSphereSavedState<s3g::AmbiWaterParams> state {};
                    surfaceEnabled = decodeWorldSphereState(
                            orderedState, state)
                        && state.surface.enabled == 1u;
                } else if (savedOrdered && wind) {
                    WorldSphereSavedState<s3g::AmbiWindParams> state {};
                    surfaceEnabled = decodeWorldSphereState(
                            orderedState, state)
                        && state.surface.enabled == 1u;
                }
                bool publicValueEvent = false;
                bool leakedInternalAction = false;
                for (const auto& event : captured.values) {
                    if (event.type != CLAP_EVENT_PARAM_VALUE) continue;
                    bool publicId = false;
                    for (uint32_t index = 0u;
                         index < params->count(plugin); ++index) {
                        clap_param_info_t info {};
                        if (params->get_info(plugin, index, &info)
                            && info.id == event.paramId) {
                            publicId = true;
                            break;
                        }
                    }
                    publicValueEvent |= publicId;
                    leakedInternalAction |= !publicId;
                }
                const bool hostNotified = publicValueEvent
                    || hostContext.paramRescanCount > rescansBefore;
                ok = queuedRandom && queuedSurfaceEnable
                    && savedOrdered && surfaceEnabled
                    && hostNotified && !leakedInternalAction;
                if (!ok) {
                    std::cerr << "Ordered RANDOM/SURF publication failed for "
                        << pluginId << " (queued=" << queuedRandom << "/"
                        << queuedSurfaceEnable << ", surface="
                        << surfaceEnabled << ", host=" << hostNotified
                        << ", leaked=" << leakedInternalAction << ")\n";
                }
            }
            if (ok && documentationCapture) {
                failureStage = "environmental documentation SURF and FIELD";
                for (uint32_t cell = 2u; ok && cell < 6u; ++cell) {
                    if (cell > 2u) ok = clickDocument(randomPoint);
                    ok = ok && clickDocument(NSMakePoint(
                        NSMidX(surfaceButton(2u)),
                        NSMidY(surfaceButton(2u))));
                }
                if (ok && water) {
                    // Give the full-editor example a river-like asymmetric
                    // map instead of the shared six-cell default used by the
                    // detached Stochastic and Wrangler examples.
                    constexpr std::array<std::array<double, 2>, 6u>
                        defaultPositions {{
                            {{ 0.18, 0.18 }}, {{ 0.82, 0.18 }},
                            {{ 0.82, 0.82 }}, {{ 0.18, 0.82 }},
                            {{ 0.50, 0.18 }}, {{ 0.82, 0.50 }},
                        }};
                    constexpr std::array<std::array<double, 2>, 6u>
                        waterPositions {{
                            {{ 0.10, 0.18 }}, {{ 0.26, 0.80 }},
                            {{ 0.42, 0.38 }}, {{ 0.62, 0.14 }},
                            {{ 0.70, 0.70 }}, {{ 0.92, 0.44 }},
                        }};
                    const NSRect plot = NSMakeRect(
                        field.origin.x + 10.0, field.origin.y + 76.0,
                        field.size.width - 20.0, field.size.height - 88.0);
                    ok = clickDocument(NSMakePoint(
                        NSMidX(surfaceButton(0u)),
                        NSMidY(surfaceButton(0u))));
                    for (uint32_t cell = 0u; ok && cell < 6u; ++cell) {
                        const auto point = [&](const auto& position) {
                            return NSMakePoint(
                                plot.origin.x
                                    + position[0] * plot.size.width,
                                NSMaxY(plot)
                                    - position[1] * plot.size.height);
                        };
                        ok = dragDocument(point(defaultPositions[cell]),
                            point(waterPositions[cell]));
                    }
                    ok = ok && clickDocument(NSMakePoint(
                        NSMidX(surfaceButton(0u)),
                        NSMidY(surfaceButton(0u))));
                }
                ok = ok && clickDocument(NSMakePoint(
                    NSMidX(surfaceButton(1u)),
                    NSMidY(surfaceButton(1u))));
                if (ok) {
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                }
                NSData* surfaceRender = ok
                    ? [document dataWithPDFInsideRect:[document bounds]]
                    : nil;
                ok = ok && surfaceRender && [surfaceRender length] > 0u;
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                if (ok && captureDirectory && captureDirectory[0]) {
                    NSString* directory = [NSString
                        stringWithUTF8String:captureDirectory];
                    [[NSFileManager defaultManager]
                        createDirectoryAtPath:directory
                        withIntermediateDirectories:YES
                        attributes:nil error:nil];
                    NSString* surfaceName = [[NSString
                        stringWithFormat:@"%s.surf", pluginId]
                        stringByAppendingPathExtension:@"pdf"];
                    ok = [surfaceRender writeToFile:
                        [directory stringByAppendingPathComponent:surfaceName]
                        atomically:YES];
                }
                const NSRect fieldTab =
                    s3g::clap_gui::environmentalFieldPageButtonRect(
                        fieldPanel, 0u);
                // Preserve the enabled SURF state in its variant capture,
                // then bypass it so the main FIELD screenshot can show the
                // explicit large documentation preset staged below.
                ok = ok && clickDocument(NSMakePoint(
                        NSMidX(surfaceButton(1u)),
                        NSMidY(surfaceButton(1u))))
                    && clickDocument(NSMakePoint(
                        NSMidX(fieldTab), NSMidY(fieldTab)))
                    && [[document valueForKey:@"fieldPage"] intValue] == 0;
            }
        }
        if (ok && parameterSurfaceEncoder) {
            failureStage = "Parameter Surface POP window";
            const bool wrangler = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-wrangler-encoder-64") == 0;
            const uint32_t surfaceCellCount =
                documentationCapture ? 6u : 2u;
            const int surfacePage = wrangler ? 3 : 2;
            const CGFloat fieldY = wrangler ? 76.0 : 72.0;
            const NSPoint surfaceTab = NSMakePoint(
                18.0 + 98.0 + surfacePage * 74.0 + 35.0,
                42.0 + 4.0 + 6.5);
            const NSPoint popButton = NSMakePoint(
                34.0 + 448.0 + 13.0, fieldY + 10.0 + 9.0);
            const NSPoint addButton = NSMakePoint(
                34.0 + 116.0 + 21.0, fieldY + 10.0 + 9.0);
            const NSPoint enableButton = NSMakePoint(
                34.0 + 64.0 + 24.0, fieldY + 10.0 + 9.0);
            const NSPoint editPlayButton = NSMakePoint(
                34.0 + 10.0 + 25.0, fieldY + 10.0 + 9.0);
            const NSRect randomRect =
                s3g::clap_gui::encoderTitleActionRect(
                    nativeWidth, nativeHeight,
                    s3g::gui_layout::EncoderTitleAction::Random);
            const CGFloat visibleRight = scroll
                ? NSMaxX([[scroll contentView] bounds])
                : static_cast<CGFloat>(nativeWidth);
            const NSPoint randomPoint = NSMakePoint(
                std::clamp(NSMidX(randomRect),
                    NSMinX(randomRect) + 3.0, visibleRight - 3.0),
                NSMidY(randomRect));
            auto clickDocument = [&](NSPoint point) {
                NSView* hit = [parent hitTest:
                    [parent convertPoint:point fromView:document]];
                if (hit != document) return false;
                [hit mouseDown:mouseEvent(NSEventTypeLeftMouseDown, point)];
                [hit mouseUp:mouseEvent(NSEventTypeLeftMouseUp, point)];
                return true;
            };
            auto dragSurfaceCell = [&](NSPoint start, NSPoint end) {
                NSView* hit = [parent hitTest:
                    [parent convertPoint:start fromView:document]];
                if (hit != document) return false;
                [hit mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, start)];
                [hit mouseDragged:mouseEvent(
                    NSEventTypeLeftMouseDragged, end)];
                [hit mouseUp:mouseEvent(NSEventTypeLeftMouseUp, end)];
                return true;
            };
            @try {
                ok = gui->show(plugin)
                    && clickDocument(surfaceTab)
                    && [[document valueForKey:@"fieldPage"] intValue]
                        == surfacePage
                    && clickDocument(addButton);
                if (documentationCapture) {
                    for (uint32_t cell = 1u;
                         ok && cell < surfaceCellCount; ++cell) {
                        ok = clickDocument(randomPoint)
                            && [[document valueForKey:@"fieldPage"] intValue]
                                == surfacePage
                            && clickDocument(addButton);
                    }
                } else {
                    ok = ok && clickDocument(addButton);
                }
                if (ok && documentationCapture) {
                    constexpr std::array<std::array<double, 2>, 6u>
                        defaultPositions {{
                            {{ 0.18, 0.18 }}, {{ 0.82, 0.18 }},
                            {{ 0.82, 0.82 }}, {{ 0.18, 0.82 }},
                            {{ 0.50, 0.18 }}, {{ 0.82, 0.50 }},
                        }};
                    constexpr std::array<std::array<double, 2>, 6u>
                        stochasticPositions {{
                            {{ 0.12, 0.12 }}, {{ 0.36, 0.22 }},
                            {{ 0.76, 0.10 }}, {{ 0.18, 0.72 }},
                            {{ 0.58, 0.54 }}, {{ 0.88, 0.82 }},
                        }};
                    constexpr std::array<std::array<double, 2>, 6u>
                        wranglerPositions {{
                            {{ 0.50, 0.08 }}, {{ 0.84, 0.26 }},
                            {{ 0.82, 0.72 }}, {{ 0.50, 0.90 }},
                            {{ 0.16, 0.70 }}, {{ 0.14, 0.28 }},
                        }};
                    const auto& positions = wrangler
                        ? wranglerPositions : stochasticPositions;
                    const CGFloat plotHeight = wrangler ? 478.0 : 374.0;
                    const NSRect plot = NSMakeRect(
                        44.0, fieldY + 68.0, 544.0, plotHeight);
                    for (uint32_t cell = 0u; ok && cell < 6u; ++cell) {
                        const auto point = [&](const auto& position) {
                            return NSMakePoint(
                                plot.origin.x
                                    + position[0] * plot.size.width,
                                NSMaxY(plot)
                                    - position[1] * plot.size.height);
                        };
                        ok = dragSurfaceCell(point(defaultPositions[cell]),
                            point(positions[cell]));
                    }
                }
                ok = ok && clickDocument(enableButton)
                    && clickDocument(editPlayButton)
                    && clickDocument(popButton)
                    && [[document valueForKey:@"fieldPage"] intValue] == 0;
                parameterSurfacePanel = ok
                    ? [document valueForKey:@"surfacePanel"] : nil;
                NSClipView* clip = parameterSurfacePanel
                    ? static_cast<NSClipView*>(
                        [parameterSurfacePanel contentView]) : nil;
                NSView* popupDocument = clip ? [clip documentView] : nil;
                ok = ok && parameterSurfacePanel
                    && [parameterSurfacePanel isVisible]
                    && [clip isKindOfClass:[NSClipView class]]
                    && popupDocument
                    && [[popupDocument valueForKey:@"fieldPage"] intValue]
                        == surfacePage;
                if (ok) {
                    const CGFloat plotHeight = wrangler ? 478.0 : 374.0;
                    const NSPoint cursorPoint = NSMakePoint(
                        44.0 + 544.0 * 0.73,
                        fieldY + 68.0 + plotHeight * (1.0 - 0.27));
                    auto popupEvent = [&](NSEventType type, NSPoint point) {
                        return [NSEvent
                            mouseEventWithType:type
                            location:[popupDocument convertPoint:point toView:nil]
                            modifierFlags:0 timestamp:0.0
                            windowNumber:[parameterSurfacePanel windowNumber]
                            context:nil eventNumber:0 clickCount:1 pressure:1.0];
                    };
                    [popupDocument mouseDown:popupEvent(
                        NSEventTypeLeftMouseDown, cursorPoint)];
                    [popupDocument mouseUp:popupEvent(
                        NSEventTypeLeftMouseUp, cursorPoint)];
                    double cursorX = -1.0;
                    double cursorY = -1.0;
                    ok = params->get_value(plugin, wrangler ? 63u : 43u,
                            &cursorX)
                        && params->get_value(plugin, wrangler ? 64u : 44u,
                            &cursorY)
                        && std::fabs(cursorX - 0.73) < 0.01
                        && std::fabs(cursorY - 0.27) < 0.01;
                }
                NSData* popupRender = ok
                    ? [clip dataWithPDFInsideRect:[clip bounds]] : nil;
                ok = ok && popupRender && [popupRender length] > 0u;
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                if (ok && captureDirectory && captureDirectory[0]) {
                    NSString* directory = [NSString
                        stringWithUTF8String:captureDirectory];
                    [[NSFileManager defaultManager]
                        createDirectoryAtPath:directory
                        withIntermediateDirectories:YES
                        attributes:nil error:nil];
                    NSString* popupName = [[NSString
                        stringWithFormat:@"%s.surf-pop", pluginId]
                        stringByAppendingPathExtension:@"pdf"];
                    ok = [popupRender writeToFile:
                        [directory stringByAppendingPathComponent:popupName]
                        atomically:YES];
                }

                if (ok) {
                    NSEvent* popupDown = [NSEvent
                        mouseEventWithType:NSEventTypeLeftMouseDown
                        location:[popupDocument convertPoint:popButton toView:nil]
                        modifierFlags:0 timestamp:0.0
                        windowNumber:[parameterSurfacePanel windowNumber]
                        context:nil eventNumber:0 clickCount:1 pressure:1.0];
                    NSEvent* popupUp = [NSEvent
                        mouseEventWithType:NSEventTypeLeftMouseUp
                        location:[popupDocument convertPoint:popButton toView:nil]
                        modifierFlags:0 timestamp:0.0
                        windowNumber:[parameterSurfacePanel windowNumber]
                        context:nil eventNumber:0 clickCount:1 pressure:1.0];
                    [popupDocument mouseDown:popupDown];
                    [popupDocument mouseUp:popupUp];
                    ok = ![parameterSurfacePanel isVisible];
                }

                // Both non-environmental surface encoders use different
                // view layouts, but RANDOM has the same state contract: the
                // base scene wins immediately and every captured cell
                // survives.
                if (ok && !documentationCapture) {
                    const auto* pluginState =
                        static_cast<const clap_plugin_state_t*>(
                            plugin->get_extension(plugin, CLAP_EXT_STATE));
                    auto saveState = [&](MemoryPluginState& memory) {
                        clap_ostream_t output { &memory, stateWriteWhole };
                        return pluginState && pluginState->save
                            && pluginState->save(plugin, &output)
                            && !memory.bytes.empty();
                    };
                    MemoryPluginState beforeRandom;
                    MemoryPluginState afterRandom;
                    MemoryPluginState reenabled;
                    const bool savedBeforeRandom = saveState(beforeRandom);
                    const bool clickedRandom = savedBeforeRandom
                        && clickDocument(randomPoint);
                    ok = savedBeforeRandom && clickedRandom;
                    if (ok && params->flush) {
                        params->flush(plugin, nullptr, nullptr);
                    }
                    const bool savedAfterRandom = ok
                        && saveState(afterRandom);
                    ok = ok && savedAfterRandom;
                    if (!ok) {
                        std::cerr << "Surface RANDOM interaction failed: save="
                            << savedBeforeRandom << " click=" << clickedRandom
                            << " resave=" << savedAfterRandom << "\n";
                    }
                    if (ok && wrangler) {
                        WorldSphereSavedState<s3g::AmbiWranglerParams>
                            before {};
                        WorldSphereSavedState<s3g::AmbiWranglerParams>
                            after {};
                        ok = decodeWorldSphereState(beforeRandom, before)
                            && decodeWorldSphereState(afterRandom, after)
                            && before.surface.enabled == 1u
                            && before.surface.cellCount == surfaceCellCount
                            && after.surface.enabled == 0u
                            && after.surface.cellCount == surfaceCellCount
                            && std::memcmp(before.surface.cells.data(),
                                after.surface.cells.data(),
                                sizeof(before.surface.cells)) == 0;
                        if (!ok) {
                            std::cerr << "Wrangler surface state before/after: "
                                << beforeRandom.bytes.size() << "/"
                                << afterRandom.bytes.size() << " bytes, enabled "
                                << before.surface.enabled << "/"
                                << after.surface.enabled << ", cells "
                                << before.surface.cellCount << "/"
                                << after.surface.cellCount << "\n";
                        }
                    } else if (ok) {
                        StochasticSavedState before {};
                        StochasticSavedState after {};
                        ok = decodeStochasticState(beforeRandom, before)
                            && decodeStochasticState(afterRandom, after)
                            && before.surface.enabled == 1u
                            && before.surface.cellCount == surfaceCellCount
                            && after.surface.enabled == 0u
                            && after.surface.cellCount == surfaceCellCount
                            && std::memcmp(before.surface.cells.data(),
                                after.surface.cells.data(),
                                sizeof(before.surface.cells)) == 0;
                        if (!ok) {
                            std::cerr << "Stochastic surface state before/after: "
                                << beforeRandom.bytes.size() << "/"
                                << afterRandom.bytes.size() << " bytes, enabled "
                                << before.surface.enabled << "/"
                                << after.surface.enabled << ", cells "
                                << before.surface.cellCount << "/"
                                << after.surface.cellCount << "\n";
                        }
                    }
                    ok = ok && clickDocument(surfaceTab)
                        && clickDocument(enableButton)
                        && saveState(reenabled);
                    if (ok && wrangler) {
                        WorldSphereSavedState<s3g::AmbiWranglerParams>
                            state {};
                        ok = decodeWorldSphereState(reenabled, state)
                            && state.surface.enabled == 1u
                            && state.surface.cellCount == surfaceCellCount;
                        if (!ok) {
                            std::cerr << "Wrangler surface re-enable state: "
                                << reenabled.bytes.size() << " bytes, enabled "
                                << state.surface.enabled << ", cells "
                                << state.surface.cellCount << "\n";
                        }
                    } else if (ok) {
                        StochasticSavedState state {};
                        ok = decodeStochasticState(reenabled, state)
                            && state.surface.enabled == 1u
                            && state.surface.cellCount == surfaceCellCount;
                    }
                }

                // Leave it open once more so guiHide() proves that the CLAP
                // lifecycle also hides the auxiliary panel.
                ok = ok && clickDocument(surfaceTab)
                    && clickDocument(popButton)
                    && [parameterSurfacePanel isVisible];
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok && delayProcessor) {
            failureStage = "delay route parameter/default contract";
            constexpr clap_id routeParam = 26u;
            constexpr clap_id turnParam = 27u;
            constexpr clap_id branchParam = 28u;
            constexpr clap_id lossParam = 29u;
            constexpr std::array<clap_id, 4u> routeParams {
                routeParam, turnParam, branchParam, lossParam,
            };
            constexpr std::array<double, 4u> routeMinimums {
                0.0, -1.0, 0.0, 0.0,
            };
            constexpr std::array<double, 4u> routeMaximums {
                1.0, 1.0, 1.0, 1.0,
            };
            constexpr std::array<double, 4u> routeDefaults {
                0.0, 0.0, 0.35, 0.25,
            };
            std::array<bool, routeParams.size()> foundRouteParams {};
            for (uint32_t index = 0u;
                 ok && index < params->count(plugin); ++index) {
                clap_param_info_t info {};
                ok = params->get_info(plugin, index, &info);
                for (size_t routeIndex = 0u;
                     ok && routeIndex < routeParams.size(); ++routeIndex) {
                    if (info.id != routeParams[routeIndex]) continue;
                    foundRouteParams[routeIndex] = true;
                    ok = std::fabs(info.min_value
                            - routeMinimums[routeIndex]) < 0.000001
                        && std::fabs(info.max_value
                            - routeMaximums[routeIndex]) < 0.000001
                        && std::fabs(info.default_value
                            - routeDefaults[routeIndex]) < 0.000001;
                }
            }
            ok = ok && std::all_of(
                foundRouteParams.begin(), foundRouteParams.end(),
                [](bool found) { return found; });

            const auto* delayTail = static_cast<const clap_plugin_tail_t*>(
                plugin->get_extension(plugin, CLAP_EXT_TAIL));
            const auto* delayState = static_cast<const clap_plugin_state_t*>(
                plugin->get_extension(plugin, CLAP_EXT_STATE));
            const uint32_t originalTail = delayTail && delayTail->get
                ? delayTail->get(plugin)
                : 0u;
            ok = ok && delayTail && delayTail->get
                && delayState && delayState->save && delayState->load
                && originalTail > 0u
                && originalTail
                    < static_cast<uint32_t>(std::numeric_limits<int32_t>::max());

            // Delay uses legacy content coordinates translated to the shared
            // 42 px top inset. ECHO ROUTES follows the 16-row TOPOLOGY panel.
            const CGFloat legacyContentTop = 34.0;
            const CGFloat contentTranslation = static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.contentTop
                - legacyContentTop);
            const CGFloat secondControlX = static_cast<CGFloat>(
                s3g::gui_layout::processorControlX(
                    s3g::gui_layout::kTopologyProcessorColumns.second.x));
            const CGFloat trackWidth = static_cast<CGFloat>(
                s3g::gui_layout::processorTrackWidth(
                    s3g::gui_layout::kTopologyProcessorColumns.second.width));
            const CGFloat rowPitch = static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.rowPitch);
            const CGFloat echoPanelY = legacyContentTop
                + static_cast<CGFloat>(
                    s3g::gui_layout::toolboxHeightForRows(16u))
                + static_cast<CGFloat>(
                    s3g::gui_layout::kStandardMetrics.panelGap);
            const CGFloat routeRowY = echoPanelY
                + static_cast<CGFloat>(
                    s3g::gui_layout::kStandardMetrics.firstRowOffset)
                + contentTranslation;
            const std::array<NSPoint, 4u> routeRows {
                NSMakePoint(secondControlX, routeRowY),
                NSMakePoint(secondControlX, routeRowY + rowPitch),
                NSMakePoint(secondControlX, routeRowY + 2.0 * rowPitch),
                NSMakePoint(secondControlX, routeRowY + 3.0 * rowPitch),
            };
            auto routeMouseEvent = [&](NSEventType type,
                                       NSPoint documentPoint,
                                       NSInteger clickCount) {
                return [NSEvent
                    mouseEventWithType:type
                    location:[document convertPoint:documentPoint toView:nil]
                    modifierFlags:0
                    timestamp:0.0
                    windowNumber:0
                    context:nil
                    eventNumber:0
                    clickCount:clickCount
                    pressure:1.0];
            };
            auto dragRouteSlider = [&](size_t index) {
                const NSPoint downPoint = NSMakePoint(
                    routeRows[index].x + trackWidth * 0.25,
                    routeRows[index].y);
                const NSPoint dragPoint = NSMakePoint(
                    routeRows[index].x + trackWidth * 0.75,
                    routeRows[index].y);
                NSView* hitView = [parent hitTest:
                    [parent convertPoint:downPoint fromView:document]];
                if (hitView != document) return false;
                [hitView mouseDown:routeMouseEvent(
                    NSEventTypeLeftMouseDown, downPoint, 1)];
                double value = -10.0;
                const double expectedDown = routeMinimums[index]
                    + (routeMaximums[index] - routeMinimums[index]) * 0.25;
                bool passed = params->get_value(
                        plugin, routeParams[index], &value)
                    && std::fabs(value - expectedDown) < 0.02;
                if (passed) {
                    [hitView mouseDragged:routeMouseEvent(
                        NSEventTypeLeftMouseDragged, dragPoint, 1)];
                    const double expectedDrag = routeMinimums[index]
                        + (routeMaximums[index] - routeMinimums[index]) * 0.75;
                    passed = params->get_value(
                            plugin, routeParams[index], &value)
                        && std::fabs(value - expectedDrag) < 0.02;
                    [hitView mouseUp:routeMouseEvent(
                        NSEventTypeLeftMouseUp, dragPoint, 1)];
                }
                return passed;
            };
            auto clickRouteSlider = [&](size_t index, double normalized) {
                const NSPoint point = NSMakePoint(
                    routeRows[index].x + trackWidth * normalized,
                    routeRows[index].y);
                NSView* hitView = [parent hitTest:
                    [parent convertPoint:point fromView:document]];
                if (hitView != document) return false;
                [hitView mouseDown:routeMouseEvent(
                    NSEventTypeLeftMouseDown, point, 1)];
                [hitView mouseUp:routeMouseEvent(
                    NSEventTypeLeftMouseUp, point, 1)];
                double value = -10.0;
                const double expected = routeMinimums[index]
                    + (routeMaximums[index] - routeMinimums[index])
                        * normalized;
                return params->get_value(plugin, routeParams[index], &value)
                    && std::fabs(value - expected) < 0.02;
            };
            auto resetRouteSlider = [&](size_t index) {
                if (!clickRouteSlider(index, 0.82)) return false;
                const NSPoint point = NSMakePoint(
                    routeRows[index].x + trackWidth * 0.50,
                    routeRows[index].y);
                NSView* hitView = [parent hitTest:
                    [parent convertPoint:point fromView:document]];
                if (hitView != document) return false;
                [hitView mouseDown:routeMouseEvent(
                    NSEventTypeLeftMouseDown, point, 2)];
                [hitView mouseUp:routeMouseEvent(
                    NSEventTypeLeftMouseUp, point, 2)];
                double restored = -10.0;
                return params->get_value(
                        plugin, routeParams[index], &restored)
                    && std::fabs(restored - routeDefaults[index]) < 0.000001;
            };

            if (ok) {
                failureStage = "delay route slider hit/default";
                [[scroll contentView] scrollToPoint:NSMakePoint(0.0, 150.0)];
                [scroll reflectScrolledClipView:[scroll contentView]];
                for (size_t index = 0u;
                     ok && index < routeParams.size(); ++index) {
                    ok = dragRouteSlider(index);
                }
                for (size_t index = 0u;
                     ok && index < routeParams.size(); ++index) {
                    ok = resetRouteSlider(index);
                }
                // Establish non-default values for the current-state round
                // trip after the default-reset path has been exercised.
                for (size_t index = 0u;
                     ok && index < routeParams.size(); ++index) {
                    ok = dragRouteSlider(index);
                }
            }

            if (ok) {
                failureStage = "delay route v11 state/migration";
                const uint32_t routedTail = delayTail->get(plugin);
                std::array<double, routeParams.size()> expected {};
                for (size_t index = 0u;
                     ok && index < routeParams.size(); ++index) {
                    ok = params->get_value(
                        plugin, routeParams[index], &expected[index]);
                }
                MemoryPluginState currentState;
                clap_ostream_t stateOutput { &currentState, stateWrite };
                ok = ok && delayState->save(plugin, &stateOutput)
                    && currentState.bytes.size() > sizeof(uint32_t)
                    && routedTail >= originalTail
                    && routedTail
                        < static_cast<uint32_t>(
                            std::numeric_limits<int32_t>::max());
                uint32_t savedVersion = 0u;
                if (ok) {
                    std::memcpy(&savedVersion,
                                currentState.bytes.data(),
                                sizeof(savedVersion));
                    ok = savedVersion == 11u;
                }
                for (size_t index = 0u;
                     ok && index < routeParams.size(); ++index) {
                    ok = clickRouteSlider(index, 0.10);
                }
                currentState.offset = 0u;
                clap_istream_t stateInput { &currentState, stateRead };
                ok = ok && delayState->load(plugin, &stateInput);
                for (size_t index = 0u;
                     ok && index < routeParams.size(); ++index) {
                    double restored = -10.0;
                    ok = params->get_value(
                            plugin, routeParams[index], &restored)
                        && std::fabs(restored - expected[index]) < 0.000001;
                }

                DelayProcessorStateV10 legacyState;
                legacyState.patchRows[2] = uint64_t { 1 } << 2u;
                legacyState.patchRows[6] = uint64_t { 1 } << 6u;
                legacyState.clearUnused = 1u;
                legacyState.delayMs = 20.0;
                legacyState.feedback = 0.68;
                legacyState.mix = 1.0;
                legacyState.tone = 1.0;
                MemoryPluginState migrationState;
                const auto* legacyFirst = reinterpret_cast<const uint8_t*>(
                    &legacyState);
                migrationState.bytes.assign(
                    legacyFirst, legacyFirst + sizeof(legacyState));
                clap_istream_t migrationInput {
                    &migrationState, stateRead
                };
                ok = ok && delayState->load(plugin, &migrationInput);
                for (size_t index = 0u;
                     ok && index < routeParams.size(); ++index) {
                    double migrated = -10.0;
                    ok = params->get_value(
                            plugin, routeParams[index], &migrated)
                        && std::fabs(migrated - routeDefaults[index])
                            < 0.000001;
                }
                if (!ok) {
                    std::cerr << "Delay route state v11 or v10 migration failed\n";
                }
            }

            if (ok) {
                failureStage = "delay route CLAP sparse propagation";
                // The v10 migration fixture left only physical rows 3 and 7
                // active. Engage routing after migration and verify that an
                // impulse on node 3 reaches node 7, not a contiguous-prefix
                // substitute.
                ok = clickRouteSlider(0u, 0.90)
                    && clickRouteSlider(2u, 0.01)
                    && clickRouteSlider(3u, 0.01);
                const uint32_t activeRouteTail = delayTail->get(plugin);
                const uint32_t audioChannels = std::strcmp(
                        pluginId,
                        "org.s3g.s3g-dsp.delay-processor-24ch") == 0
                    ? 24u : 8u;
                constexpr uint32_t audioFrames = 128u;
                std::vector<std::array<float, audioFrames>> audioInput(
                    audioChannels);
                std::vector<std::array<float, audioFrames>> audioOutput(
                    audioChannels);
                std::vector<float*> inputPointers(audioChannels, nullptr);
                std::vector<float*> outputPointers(audioChannels, nullptr);
                for (uint32_t channel = 0u;
                     channel < audioChannels; ++channel) {
                    inputPointers[channel] = audioInput[channel].data();
                    outputPointers[channel] = audioOutput[channel].data();
                }
                clap_audio_buffer_t inputBuffer {};
                inputBuffer.data32 = inputPointers.data();
                inputBuffer.channel_count = audioChannels;
                clap_audio_buffer_t outputBuffer {};
                outputBuffer.data32 = outputPointers.data();
                outputBuffer.channel_count = audioChannels;
                clap_process_t processBlock {};
                processBlock.frames_count = audioFrames;
                processBlock.audio_inputs = &inputBuffer;
                processBlock.audio_outputs = &outputBuffer;
                processBlock.audio_inputs_count = 1u;
                processBlock.audio_outputs_count = 1u;
                ok = ok && activeRouteTail > 0u
                    && activeRouteTail
                        < static_cast<uint32_t>(
                            std::numeric_limits<int32_t>::max())
                    && plugin->activate(plugin, 48000.0, 1u, audioFrames)
                    && plugin->start_processing(plugin);
                float remotePeak = 0.0f;
                uint32_t residualTail = 0u;
                for (uint32_t block = 0u; ok && block < 36u; ++block) {
                    for (auto& channel : audioInput) channel.fill(0.0f);
                    for (auto& channel : audioOutput) channel.fill(0.0f);
                    if (block == 0u) audioInput[2][0] = 0.5f;
                    ok = plugin->process(plugin, &processBlock)
                        != CLAP_PROCESS_ERROR;
                    if (block == 10u) {
                        ok = ok && clickRouteSlider(0u, 0.0);
                        residualTail = delayTail->get(plugin);
                    }
                    if (block >= 11u) {
                        for (float sample : audioOutput[6]) {
                            remotePeak = std::max(
                                remotePeak, std::fabs(sample));
                        }
                    }
                }
                plugin->stop_processing(plugin);
                plugin->deactivate(plugin);
                ok = ok && remotePeak > 0.000001f
                    && residualTail > 0u
                    && residualTail
                        < static_cast<uint32_t>(
                            std::numeric_limits<int32_t>::max());
                if (!ok) {
                    std::cerr
                        << "Delay sparse route/residual tail failed: remote/tail="
                        << remotePeak << "/" << residualTail << "\n";
                }

                if (ok) {
                    failureStage = "delay legacy feedback tail high-water";
                    constexpr clap_id feedbackParam = 2u;
                    SingleParamEventInput paramEvent;
                    ok = params->flush != nullptr;
                    if (ok) {
                        setSingleParamEvent(paramEvent, routeParam, 0.0);
                        params->flush(plugin, &paramEvent.events, nullptr);
                        setSingleParamEvent(paramEvent, feedbackParam, 0.0);
                        params->flush(plugin, &paramEvent.events, nullptr);
                    }

                    for (auto& channel : audioInput) channel.fill(0.0f);
                    for (auto& channel : audioOutput) channel.fill(0.0f);
                    processBlock.in_events = nullptr;
                    ok = ok
                        && plugin->activate(plugin, 48000.0, 1u, audioFrames)
                        && plugin->start_processing(plugin)
                        && plugin->process(plugin, &processBlock)
                            != CLAP_PROCESS_ERROR;
                    const uint32_t lowFeedbackTail = ok
                        ? delayTail->get(plugin)
                        : 0u;

                    setSingleParamEvent(paramEvent, feedbackParam, 0.82);
                    processBlock.in_events = &paramEvent.events;
                    ok = ok && plugin->process(plugin, &processBlock)
                        != CLAP_PROCESS_ERROR;
                    const uint32_t highFeedbackTail = ok
                        ? delayTail->get(plugin)
                        : 0u;

                    setSingleParamEvent(paramEvent, feedbackParam, 0.0);
                    ok = ok && plugin->process(plugin, &processBlock)
                        != CLAP_PROCESS_ERROR;
                    const uint32_t preservedFeedbackTail = ok
                        ? delayTail->get(plugin)
                        : 0u;
                    processBlock.in_events = nullptr;
                    plugin->stop_processing(plugin);
                    plugin->deactivate(plugin);

                    const uint32_t finiteTailCeiling = static_cast<uint32_t>(
                        std::numeric_limits<int32_t>::max());
                    ok = ok && lowFeedbackTail > 0u
                        && highFeedbackTail > lowFeedbackTail
                        && preservedFeedbackTail > lowFeedbackTail
                        && preservedFeedbackTail <= highFeedbackTail
                        && preservedFeedbackTail < finiteTailCeiling;
                    if (!ok) {
                        std::cerr
                            << "Delay feedback tail collapsed after automation: "
                            << lowFeedbackTail << " / " << highFeedbackTail
                            << " / " << preservedFeedbackTail << "\n";
                    }
                }
                [[scroll contentView] scrollToPoint:NSZeroPoint];
                [scroll reflectScrolledClipView:[scroll contentView]];
            }
        }
        if (ok && spectralTopology) {
            failureStage = "spectral slider hit and drag";
            const auto* spectralTail = static_cast<const clap_plugin_tail_t*>(
                plugin->get_extension(plugin, CLAP_EXT_TAIL));
            const uint32_t instantaneousTail = spectralTail && spectralTail->get
                ? spectralTail->get(plugin)
                : 0u;
            // Processor Spectral draws its content in legacy coordinates and
            // translates the complete content region to the shared 42 px top
            // inset. Exercise a slider in each control section through AppKit
            // hit testing so drawing/hit-coordinate drift cannot disable it.
            const CGFloat legacyContentTop = 34.0;
            const CGFloat contentTranslation = static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.contentTop
                - legacyContentTop);
            const CGFloat trackWidth = static_cast<CGFloat>(
                s3g::gui_layout::processorTrackWidth(
                    s3g::gui_layout::kTopologyProcessorColumns.first.width));
            constexpr clap_id mixParam = 10u;
            constexpr clap_id outputParam = 11u;
            constexpr clap_id transientParam = 17u;
            constexpr clap_id velocityParam = 18u;
            constexpr clap_id dispersionParam = 19u;
            constexpr clap_id dampingParam = 20u;
            constexpr clap_id centroidParam = 45u;
            auto dragSlider = [&](clap_id id, NSPoint rowPoint,
                                  double minimum = 0.0,
                                  double maximum = 1.0) {
                const NSPoint downPoint = NSMakePoint(
                    rowPoint.x + trackWidth * 0.25, rowPoint.y);
                const NSPoint dragPoint = NSMakePoint(
                    rowPoint.x + trackWidth * 0.75, rowPoint.y);
                NSView* hitView = [parent hitTest:
                    [parent convertPoint:downPoint fromView:document]];
                double clickedValue = -1.0;
                double draggedValue = -1.0;
                bool passed = hitView == document;
                if (passed) {
                    [hitView mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, downPoint)];
                    const double expectedClick = minimum
                        + (maximum - minimum) * 0.25;
                    passed = params->get_value(plugin, id, &clickedValue)
                        && std::fabs(clickedValue - expectedClick) < 0.02;
                }
                if (passed) {
                    [hitView mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, dragPoint)];
                    const double expectedDrag = minimum
                        + (maximum - minimum) * 0.75;
                    passed = params->get_value(plugin, id, &draggedValue)
                        && std::fabs(draggedValue - expectedDrag) < 0.02;
                    [hitView mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, dragPoint)];
                }
                return passed;
            };

            const CGFloat firstControlX = static_cast<CGFloat>(
                s3g::gui_layout::processorControlX(
                    s3g::gui_layout::kTopologyProcessorColumns.first.x));
            const CGFloat secondControlX = static_cast<CGFloat>(
                s3g::gui_layout::processorControlX(
                    s3g::gui_layout::kTopologyProcessorColumns.second.x));
            const CGFloat firstRowOffset = static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.firstRowOffset);
            const CGFloat rowPitch = static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.rowPitch);
            const CGFloat outputRowY = legacyContentTop + firstRowOffset
                + contentTranslation;
            const CGFloat mixRowY = legacyContentTop + firstRowOffset
                + rowPitch + contentTranslation;
            ok = dragSlider(outputParam,
                     NSMakePoint(firstControlX, outputRowY), -60.0, 12.0)
                && dragSlider(mixParam,
                     NSMakePoint(firstControlX, mixRowY));

            if (ok) {
                [[scroll contentView] scrollToPoint:NSMakePoint(0.0, 220.0)];
                [scroll reflectScrolledClipView:[scroll contentView]];
                const CGFloat enginePanelY = legacyContentTop + 80.0
                    + s3g::gui_layout::kStandardMetrics.panelGap;
                const CGFloat transientRowY = enginePanelY + firstRowOffset
                    + 13.0 * rowPitch + contentTranslation;
                const CGFloat centroidRowY = legacyContentTop + firstRowOffset
                    + 15.0 * rowPitch + contentTranslation;
                const CGFloat propagationPanelY = legacyContentTop
                    + s3g::gui_layout::toolboxHeightForRows(16u)
                    + s3g::gui_layout::kStandardMetrics.panelGap;
                const CGFloat velocityRowY = propagationPanelY
                    + firstRowOffset + contentTranslation;
                const CGFloat dispersionRowY = velocityRowY + rowPitch;
                const CGFloat dampingRowY = dispersionRowY + rowPitch;
                ok = dragSlider(transientParam,
                         NSMakePoint(firstControlX, transientRowY))
                    && dragSlider(centroidParam,
                         NSMakePoint(secondControlX, centroidRowY))
                    && dragSlider(velocityParam,
                         NSMakePoint(secondControlX, velocityRowY))
                    && dragSlider(dispersionParam,
                         NSMakePoint(secondControlX, dispersionRowY), -1.0, 1.0)
                    && dragSlider(dampingParam,
                         NSMakePoint(secondControlX, dampingRowY));
                [[scroll contentView] scrollToPoint:NSZeroPoint];
                [scroll reflectScrolledClipView:[scroll contentView]];
            }
            if (ok) {
                failureStage = "spectral propagation tail";
                constexpr uint32_t propagationSpanSamples = 127u * 512u;
                const uint32_t delayedTail = spectralTail && spectralTail->get
                    ? spectralTail->get(plugin)
                    : 0u;
                ok = spectralTail && delayedTail > instantaneousTail
                    && delayedTail - instantaneousTail
                        >= 3u * propagationSpanSamples;
                if (!ok) {
                    std::cerr << "Spectral propagation tail did not cover "
                        "recirculation: immediate/delayed="
                        << instantaneousTail << "/" << delayedTail << "\n";
                }
            }
        }
        if (ok && waveGeometry) {
            failureStage = "wave mesh slider hit and drag";
            const auto* waveTail = static_cast<const clap_plugin_tail_t*>(
                plugin->get_extension(plugin, CLAP_EXT_TAIL));
            const uint32_t localTail = waveTail && waveTail->get
                ? waveTail->get(plugin)
                : 1u;
            const uint32_t minimumTapeTail = 72000u;
            ok = waveTail && waveTail->get
                && localTail >= minimumTapeTail;
            if (!ok) {
                std::cerr << "Wave Geometry tail does not cover its tape ring: "
                          << localTail << "\n";
            }
            // Wave Geometry uses the same translated legacy content space as
            // the other topology processors. Exercise output, topology, and
            // every mesh row through actual AppKit hit testing.
            const CGFloat legacyContentTop = 34.0;
            const CGFloat contentTranslation = static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.contentTop
                - legacyContentTop);
            const CGFloat trackWidth = static_cast<CGFloat>(
                s3g::gui_layout::processorTrackWidth(
                    s3g::gui_layout::kTopologyProcessorColumns.first.width));
            const CGFloat firstControlX = static_cast<CGFloat>(
                s3g::gui_layout::processorControlX(
                    s3g::gui_layout::kTopologyProcessorColumns.first.x));
            const CGFloat secondControlX = static_cast<CGFloat>(
                s3g::gui_layout::processorControlX(
                    s3g::gui_layout::kTopologyProcessorColumns.second.x));
            const CGFloat firstRowOffset = static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.firstRowOffset);
            const CGFloat rowPitch = static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.rowPitch);
            constexpr clap_id outputParam = 11u;
            constexpr clap_id couplingParam = 17u;
            constexpr clap_id tensionParam = 18u;
            constexpr clap_id decayParam = 19u;
            constexpr clap_id dampingParam = 20u;
            constexpr clap_id centroidParam = 45u;
            auto dragSlider = [&](clap_id id, NSPoint rowPoint,
                                  double minimum = 0.0,
                                  double maximum = 1.0) {
                const NSPoint downPoint = NSMakePoint(
                    rowPoint.x + trackWidth * 0.25, rowPoint.y);
                const NSPoint dragPoint = NSMakePoint(
                    rowPoint.x + trackWidth * 0.75, rowPoint.y);
                NSView* hitView = [parent hitTest:
                    [parent convertPoint:downPoint fromView:document]];
                double clickedValue = -1.0;
                double draggedValue = -1.0;
                bool passed = hitView == document;
                if (passed) {
                    [hitView mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, downPoint)];
                    const double expectedClick = minimum
                        + (maximum - minimum) * 0.25;
                    passed = params->get_value(plugin, id, &clickedValue)
                        && std::fabs(clickedValue - expectedClick) < 0.02;
                }
                if (passed) {
                    [hitView mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, dragPoint)];
                    const double expectedDrag = minimum
                        + (maximum - minimum) * 0.75;
                    passed = params->get_value(plugin, id, &draggedValue)
                        && std::fabs(draggedValue - expectedDrag) < 0.02;
                    [hitView mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, dragPoint)];
                }
                return passed;
            };
            auto clickSliderAt = [&](clap_id id, NSPoint rowPoint,
                                     double normalized) {
                const NSPoint point = NSMakePoint(
                    rowPoint.x + trackWidth * normalized, rowPoint.y);
                NSView* hitView = [parent hitTest:
                    [parent convertPoint:point fromView:document]];
                if (hitView != document) return false;
                [hitView mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, point)];
                [hitView mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, point)];
                double value = -1.0;
                return params->get_value(plugin, id, &value)
                    && std::fabs(value - normalized) < 0.02;
            };

            const CGFloat outputRowY = legacyContentTop + firstRowOffset
                + contentTranslation;
            ok = dragSlider(outputParam,
                NSMakePoint(firstControlX, outputRowY), -60.0, 12.0);
            if (ok) {
                [[scroll contentView] scrollToPoint:NSMakePoint(0.0, 170.0)];
                [scroll reflectScrolledClipView:[scroll contentView]];
                const CGFloat topologyHeight = static_cast<CGFloat>(
                    s3g::gui_layout::toolboxHeightForRows(16u));
                const CGFloat meshPanelY = legacyContentTop + topologyHeight
                    + static_cast<CGFloat>(
                        s3g::gui_layout::kStandardMetrics.panelGap);
                const CGFloat couplingRowY = meshPanelY + firstRowOffset
                    + contentTranslation;
                const CGFloat centroidRowY = legacyContentTop + firstRowOffset
                    + 15.0 * rowPitch + contentTranslation;
                const std::array<NSPoint, 4u> meshRows {
                    NSMakePoint(secondControlX, couplingRowY),
                    NSMakePoint(secondControlX, couplingRowY + rowPitch),
                    NSMakePoint(secondControlX, couplingRowY + 2.0 * rowPitch),
                    NSMakePoint(secondControlX, couplingRowY + 3.0 * rowPitch),
                };
                constexpr std::array<clap_id, 4u> meshParams {
                    couplingParam, tensionParam, decayParam, dampingParam,
                };
                ok = dragSlider(couplingParam,
                         meshRows[0])
                    && dragSlider(tensionParam,
                         meshRows[1])
                    && dragSlider(decayParam,
                         meshRows[2])
                    && dragSlider(dampingParam,
                         meshRows[3])
                    && dragSlider(centroidParam,
                         NSMakePoint(secondControlX, centroidRowY));

                if (ok) {
                    failureStage = "wave mesh state and tail";
                    const uint32_t meshTail = waveTail->get(plugin);
                    const auto* state = static_cast<const clap_plugin_state_t*>(
                        plugin->get_extension(plugin, CLAP_EXT_STATE));
                    MemoryPluginState savedState;
                    clap_ostream_t outputState { &savedState, stateWrite };
                    std::array<double, meshParams.size()> expected {};
                    ok = meshTail > localTail && state && state->save && state->load;
                    for (size_t i = 0u; ok && i < meshParams.size(); ++i) {
                        ok = params->get_value(
                            plugin, meshParams[i], &expected[i]);
                    }
                    char tensionText[32] {};
                    double parsedTension = -1.0;
                    ok = ok && params->value_to_text(
                            plugin, tensionParam, expected[1],
                            tensionText, sizeof(tensionText))
                        && params->text_to_value(
                            plugin, tensionParam, tensionText,
                            &parsedTension)
                        && std::fabs(parsedTension - expected[1]) < 0.000001;
                    ok = ok && state->save(plugin, &outputState)
                        && !savedState.bytes.empty();
                    for (size_t i = 0u; ok && i < meshParams.size(); ++i) {
                        ok = clickSliderAt(meshParams[i], meshRows[i], 0.10);
                    }
                    savedState.offset = 0u;
                    clap_istream_t inputState { &savedState, stateRead };
                    ok = ok && state->load(plugin, &inputState);
                    for (size_t i = 0u; ok && i < meshParams.size(); ++i) {
                        double restored = -1.0;
                        ok = params->get_value(
                                plugin, meshParams[i], &restored)
                            && std::fabs(restored - expected[i]) < 0.000001;
                    }
                    ok = ok && waveTail->get(plugin) == meshTail;
                    if (!ok) {
                        std::cerr << "Wave mesh tail/state round trip failed: local/mesh="
                                  << localTail << "/" << meshTail << "\n";
                    }

                    if (ok) {
                        failureStage = "wave mesh CLAP audio handoff";
                        constexpr uint32_t audioFrames = 128u;
                        constexpr uint32_t audioChannels = 8u;
                        std::array<std::array<float, audioFrames>, audioChannels> audioInput {};
                        std::array<std::array<float, audioFrames>, audioChannels> audioOutput {};
                        std::array<float*, audioChannels> inputPointers {};
                        std::array<float*, audioChannels> outputPointers {};
                        for (uint32_t channel = 0u; channel < audioChannels; ++channel) {
                            inputPointers[channel] = audioInput[channel].data();
                            outputPointers[channel] = audioOutput[channel].data();
                        }
                        clap_audio_buffer_t inputBuffer {};
                        inputBuffer.data32 = inputPointers.data();
                        inputBuffer.channel_count = audioChannels;
                        clap_audio_buffer_t outputBuffer {};
                        outputBuffer.data32 = outputPointers.data();
                        outputBuffer.channel_count = audioChannels;
                        clap_process_t processBlock {};
                        processBlock.frames_count = audioFrames;
                        processBlock.audio_inputs = &inputBuffer;
                        processBlock.audio_outputs = &outputBuffer;
                        processBlock.audio_inputs_count = 1u;
                        processBlock.audio_outputs_count = 1u;
                        ok = plugin->activate(plugin, 48000.0, 1u, audioFrames)
                            && plugin->start_processing(plugin);
                        float remotePeak = 0.0f;
                        for (uint32_t block = 0u; ok && block < 40u; ++block) {
                            for (auto& channel : audioInput) channel.fill(0.0f);
                            for (auto& channel : audioOutput) channel.fill(0.0f);
                            if (block == 0u) audioInput[0][0] = 0.35f;
                            ok = plugin->process(plugin, &processBlock)
                                != CLAP_PROCESS_ERROR;
                            for (uint32_t channel = 1u; channel < audioChannels; ++channel) {
                                for (float sample : audioOutput[channel]) {
                                    remotePeak = std::max(remotePeak, std::fabs(sample));
                                }
                            }
                        }
                        plugin->stop_processing(plugin);
                        plugin->deactivate(plugin);
                        ok = ok && remotePeak > 0.000001f;
                        if (!ok) {
                            std::cerr << "Wave mesh atomic parameter handoff did not reach a remote lane: "
                                      << remotePeak << "\n";
                        }
                        if (ok) {
                            failureStage = "wave mesh residual tail";
                            ok = clickSliderAt(
                                    couplingParam, meshRows[0], 0.0)
                                && waveTail->get(plugin) > localTail;
                            if (!ok) {
                                std::cerr << "Wave mesh residual tail was dropped when COUP reached zero\n";
                            }
                        }
                    }
                }
                [[scroll contentView] scrollToPoint:NSZeroPoint];
                [scroll reflectScrolledClipView:[scroll contentView]];
            }
        }
        if (ok && feedbackShift) {
            const auto clickFeedback = [&](NSPoint point) {
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, point)];
                [document mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, point)];
            };
            @try {
                failureStage = "Feedback Shift page navigation";
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                for (uint32_t page = 1u; ok && page < 4u; ++page) {
                    clickFeedback(NSMakePoint(71.0 + page * 92.0, 54.0));
                    ok = [[document valueForKey:@"page"] unsignedIntValue]
                        == page;
                    if (page == 1u && ok) {
                        // The first node's adjacent RND control is deliberately
                        // separate from selection and exercises the node-local
                        // insert-only randomization path.
                        clickFeedback(NSMakePoint(125.0, 115.0));
                    }
                    if (ok) {
                        [document setNeedsDisplay:YES];
                        [document displayIfNeeded];
                        NSData* pageRender = [document dataWithPDFInsideRect:
                            [document bounds]];
                        ok = pageRender && [pageRender length] > 0u;
                        if (ok && captureDirectory && captureDirectory[0]) {
                            NSString* directory = [NSString
                                stringWithUTF8String:captureDirectory];
                            [[NSFileManager defaultManager]
                                createDirectoryAtPath:directory
                                withIntermediateDirectories:YES
                                attributes:nil error:nil];
                            NSString* pageName = [[NSString stringWithFormat:
                                @"%s.page%u", pluginId, page + 1u]
                                stringByAppendingPathExtension:@"pdf"];
                            ok = [pageRender writeToFile:[directory
                                    stringByAppendingPathComponent:pageName]
                                atomically:YES];
                        }
                    }
                }
                clickFeedback(NSMakePoint(71.0, 54.0));
                ok = ok && [[document valueForKey:@"page"] unsignedIntValue]
                    == 0u;
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok && noInputMixer) {
            const auto clickNoInput = [&](NSPoint point) {
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, point)];
                [document mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, point)];
            };
            @try {
                failureStage = "No Input Mixer focused arrow navigation";
                auto pageArrow = [&](unsigned short keyCode,
                                     NSString* characters) {
                    NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                        location:NSZeroPoint modifierFlags:0 timestamp:0.0
                        windowNumber:0 context:nil characters:characters
                        charactersIgnoringModifiers:characters
                        isARepeat:NO keyCode:keyCode];
                    [document keyDown:key];
                };
                ok = [document acceptsFirstResponder]
                    && [[document valueForKey:@"activePage"]
                        unsignedIntValue] == 0u;
                if (ok) pageArrow(124u, @"\uF703");
                ok = ok && [[document valueForKey:@"activePage"]
                    unsignedIntValue] == 1u;
                if (ok) pageArrow(123u, @"\uF702");
                ok = ok && [[document valueForKey:@"activePage"]
                    unsignedIntValue] == 0u;

                failureStage = "No Input Mixer presets and randomization";
                double feedbackBefore = 0.0;
                double bodyBefore = 0.0;
                double feedbackAfter = 0.0;
                double bodyAfter = 0.0;
                ok = params->get_value(plugin, 5u, &feedbackBefore)
                    && params->get_value(plugin, 1000u, &bodyBefore);
                const auto& network =
                    s3g::gui_layout::kNoInputMixerFamilyLayout.network;
                const CGFloat randomX = static_cast<CGFloat>(
                    s3g::gui_layout::processorControlX(network.frame.x)
                    + 58.0 + 6.0 + 33.0);
                const CGFloat randomY = static_cast<CGFloat>(
                    s3g::gui_layout::rowY(network, 0u) + 6.5);
                const NSRect randomAnchor = NSMakeRect(
                    randomX - 33.0, randomY - 7.5, 66.0, 15.0);
                const auto randomEnergyPoint = [&](uint32_t profile) {
                    return NSMakePoint(NSMidX(randomAnchor),
                        NSMaxY(randomAnchor) + 2.0
                            + 18.0 * (profile + 0.5));
                };
                const auto chooseRandomEnergy = [&](uint32_t profile) {
                    clickNoInput(NSMakePoint(randomX, randomY));
                    clickNoInput(randomEnergyPoint(profile));
                };
                failureStage = "No Input Mixer random energy menu";
                if (ok) {
                    clickNoInput(NSMakePoint(randomX, randomY));
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, randomEnergyPoint(0u))];
                    [document displayIfNeeded];
                    ok = [[document valueForKey:@"hoverMenuItem"]
                        integerValue] == 0;
                    const char* captureDirectory = std::getenv(
                        "S3G_GUI_SMOKE_PDF_DIR");
                    if (ok && captureDirectory && captureDirectory[0]) {
                        NSString* directory = [NSString
                            stringWithUTF8String:captureDirectory];
                        [[NSFileManager defaultManager]
                            createDirectoryAtPath:directory
                            withIntermediateDirectories:YES
                            attributes:nil error:nil];
                        NSData* randomMenuRender = [document
                            dataWithPDFInsideRect:[document bounds]];
                        NSString* randomMenuName = [[NSString
                            stringWithFormat:@"%s.random-energy-menu",
                                pluginId]
                            stringByAppendingPathExtension:@"pdf"];
                        ok = randomMenuRender
                            && [randomMenuRender writeToFile:[directory
                                stringByAppendingPathComponent:
                                    randomMenuName]
                                atomically:YES];
                    }
                    if (ok) clickNoInput(randomEnergyPoint(0u));
                }
                ok = ok
                    && params->get_value(plugin, 5u, &feedbackAfter)
                    && params->get_value(plugin, 1000u, &bodyAfter)
                    && (std::fabs(feedbackAfter - feedbackBefore) > 0.000001
                        || std::fabs(bodyAfter - bodyBefore) > 0.000001);
                double randomMotion = 0.0;
                double randomBehavior = 0.0;
                double randomSlow = 0.0;
                ok = ok && params->get_value(plugin, 19u, &randomMotion)
                    && randomMotion >= 0.72
                    && params->get_value(plugin, 35u, &randomBehavior)
                    && randomBehavior >= 2.0
                    && params->get_value(plugin, 51u, &randomSlow)
                    && randomSlow == 0.0;
                if (ok) chooseRandomEnergy(1u);
                ok = ok && params->get_value(plugin, 19u, &randomMotion)
                    && randomMotion >= 0.42 && randomMotion <= 0.681
                    && params->get_value(plugin, 35u, &randomBehavior)
                    && randomBehavior >= 1.0
                    && params->get_value(plugin, 51u, &randomSlow)
                    && randomSlow == 0.0;
                if (ok) chooseRandomEnergy(2u);
                ok = ok && params->get_value(plugin, 19u, &randomMotion)
                    && randomMotion >= 0.38 && randomMotion <= 0.561
                    && params->get_value(plugin, 35u, &randomBehavior)
                    && randomBehavior <= 1.0
                    && params->get_value(plugin, 51u, &randomSlow)
                    && randomSlow == 1.0;

                const auto titleBand = s3g::gui_layout::matrixTitleBand(
                    s3g::gui_layout::kNoInputMixerFamilyLayout.canvas);
                const NSRect presetAnchor =
                    s3g::clap_gui::cocoaRect(titleBand.presetMenu);
                if (ok) clickNoInput(NSMakePoint(
                    NSMidX(presetAnchor), NSMidY(presetAnchor)));
                const uint32_t zoneWebIndex = 5u;
                const NSPoint zoneWebMenuPoint = NSMakePoint(
                    NSMidX(presetAnchor),
                    NSMaxY(presetAnchor) + 2.0
                        + 18.0 * (zoneWebIndex + 0.5));
                failureStage = "No Input Mixer dropdown hover highlight";
                if (ok) {
                    [document mouseMoved:mouseEvent(
                        NSEventTypeMouseMoved, zoneWebMenuPoint)];
                    [document displayIfNeeded];
                    ok = [[document valueForKey:@"hoverMenuItem"]
                        integerValue] == static_cast<NSInteger>(zoneWebIndex);
                    const char* captureDirectory = std::getenv(
                        "S3G_GUI_SMOKE_PDF_DIR");
                    if (ok && captureDirectory && captureDirectory[0]) {
                        NSString* directory = [NSString
                            stringWithUTF8String:captureDirectory];
                        [[NSFileManager defaultManager]
                            createDirectoryAtPath:directory
                            withIntermediateDirectories:YES
                            attributes:nil error:nil];
                        NSData* hoverRender = [document
                            dataWithPDFInsideRect:[document bounds]];
                        NSString* hoverName = [[NSString stringWithFormat:
                            @"%s.menu-hover", pluginId]
                            stringByAppendingPathExtension:@"pdf"];
                        ok = hoverRender && [hoverRender writeToFile:
                            [directory stringByAppendingPathComponent:
                                hoverName]
                            atomically:YES];
                    }
                }
                if (ok) clickNoInput(zoneWebMenuPoint);
                ok = ok && [[document valueForKey:@"hoverMenuItem"]
                    integerValue] == -1;
                double presetFeedback = 0.0;
                double presetType = 0.0;
                ok = ok
                    && params->get_value(plugin, 5u, &presetFeedback)
                    && params->get_value(plugin, 1020u, &presetType)
                    && std::fabs(presetFeedback - 0.89) < 0.01
                    && presetType == 3.0;

                if (ok) {
                    failureStage = "No Input Mixer CLEAR ALL connections";
                    const NSRect wiring = NSMakeRect(
                        28.0, 78.0, 896.0, 714.0);
                    clickNoInput(NSMakePoint(
                        NSMaxX(wiring) - 196.0 + 37.0,
                        wiring.origin.y + 19.0));
                    for (clap_id route = 100u; ok && route < 164u;
                         ++route) {
                        double value = 1.0;
                        ok = params->get_value(plugin, route, &value)
                            && std::abs(value) < 0.000001;
                    }
                    if (ok) clickNoInput(NSMakePoint(
                        NSMidX(presetAnchor), NSMidY(presetAnchor)));
                    if (ok) clickNoInput(NSMakePoint(
                        NSMidX(presetAnchor),
                        NSMaxY(presetAnchor) + 2.0
                            + 18.0 * (zoneWebIndex + 0.5)));
                    double restoredDiagonal = 0.0;
                    ok = ok && params->get_value(
                            plugin, 100u, &restoredDiagonal)
                        && restoredDiagonal > 0.8;
                }

                if (ok) {
                    failureStage = "No Input Mixer wiring patch gesture";
                    const NSRect wiring = NSMakeRect(
                        28.0, 78.0, 896.0, 714.0);
                    const CGFloat wireGap =
                        (wiring.size.height - 144.0) / 7.0;
                    const NSPoint sourcePort = NSMakePoint(
                        wiring.origin.x + 54.0,
                        wiring.origin.y + 72.0 + wireGap);
                    const NSPoint destinationPort = NSMakePoint(
                        NSMaxX(wiring) - 54.0,
                        wiring.origin.y + 72.0);
                    double routeBefore = 0.0;
                    double routePatched = 0.0;
                    double routeCleared = 0.0;
                    ok = params->get_value(plugin, 101u, &routeBefore);
                    if (ok) {
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown, sourcePort)];
                        [document mouseDragged:mouseEvent(
                            NSEventTypeLeftMouseDragged, destinationPort)];
                        [document mouseUp:mouseEvent(
                            NSEventTypeLeftMouseUp, destinationPort)];
                    }
                    ok = ok && params->get_value(plugin, 101u, &routePatched);
                    if (ok) {
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown, sourcePort)];
                        [document mouseDragged:mouseEvent(
                            NSEventTypeLeftMouseDragged, destinationPort)];
                        [document mouseUp:mouseEvent(
                            NSEventTypeLeftMouseUp, destinationPort)];
                    }
                    ok = ok && params->get_value(plugin, 101u, &routeCleared)
                        && std::abs(routeBefore) < 0.000001
                        && std::abs(routePatched - 0.25) < 0.000001
                        && std::abs(routeCleared) < 0.000001;
                    if (ok) {
                        [document mouseDown:mouseEvent(
                            NSEventTypeLeftMouseDown, sourcePort)];
                        [document mouseDragged:mouseEvent(
                            NSEventTypeLeftMouseDragged, destinationPort)];
                        [document mouseUp:mouseEvent(
                            NSEventTypeLeftMouseUp, destinationPort)];
                    }
                    double routeRestored = 0.0;
                    ok = ok && params->get_value(
                            plugin, 101u, &routeRestored)
                        && std::abs(routeRestored - 0.25) < 0.000001;
                }
                if (ok) {
                    failureStage = "No Input Mixer routed-audio wire scope";
                    constexpr uint32_t audioFrames = 128u;
                    std::array<std::array<float, audioFrames>, 8u>
                        audioOutput {};
                    std::array<float*, 8u> outputPointers {};
                    for (uint32_t channel = 0u; channel < 8u; ++channel) {
                        outputPointers[channel] = audioOutput[channel].data();
                    }
                    clap_audio_buffer_t outputBuffer {};
                    outputBuffer.data32 = outputPointers.data();
                    outputBuffer.channel_count = 8u;
                    clap_process_t processBlock {};
                    processBlock.frames_count = audioFrames;
                    processBlock.audio_outputs = &outputBuffer;
                    processBlock.audio_outputs_count = 1u;
                    ok = plugin->activate(plugin, 48000.0, 1u, audioFrames)
                        && plugin->start_processing(plugin);
                    float renderedPeak = 0.0f;
                    for (uint32_t block = 0u; ok && block < 96u; ++block) {
                        for (auto& channel : audioOutput) channel.fill(0.0f);
                        ok = plugin->process(plugin, &processBlock)
                            != CLAP_PROCESS_ERROR;
                        for (const auto& channel : audioOutput) {
                            for (float sample : channel) {
                                renderedPeak = std::max(
                                    renderedPeak, std::abs(sample));
                            }
                        }
                    }
                    plugin->stop_processing(plugin);
                    plugin->deactivate(plugin);
                    ok = ok && renderedPeak > 1.0e-7f;
                }
                if (ok) {
                    failureStage = "No Input Mixer dB matrix grid";
                    const NSRect wiring = NSMakeRect(
                        28.0, 78.0, 896.0, 714.0);
                    clickNoInput(NSMakePoint(
                        NSMaxX(wiring) - 38.0, wiring.origin.y + 19.0));
                    const NSPoint firstGridCell = NSMakePoint(
                        wiring.origin.x + 154.0 + 54.0,
                        wiring.origin.y + 34.0 + 36.0);
                    double diagonalBefore = 0.0;
                    double diagonalSelected = 0.0;
                    double diagonalDissolved = 1.0;
                    double diagonalRestored = 0.0;
                    ok = params->get_value(plugin, 100u, &diagonalBefore);
                    if (ok) clickNoInput(firstGridCell);
                    ok = ok && params->get_value(
                        plugin, 100u, &diagonalSelected)
                        && std::abs(diagonalSelected - diagonalBefore)
                            < 0.000001;
                    if (ok) clickNoInput(firstGridCell);
                    ok = ok && params->get_value(
                        plugin, 100u, &diagonalDissolved)
                        && std::abs(diagonalDissolved) < 0.000001;
                    if (ok) clickNoInput(firstGridCell);
                    ok = ok && params->get_value(
                        plugin, 100u, &diagonalRestored)
                        && std::abs(diagonalRestored - 0.94) < 0.000001;
                    const auto& movement =
                        s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
                    if (ok) clickNoInput(NSMakePoint(
                        movement.frame.x + movement.frame.width - 153.0
                            + 48.0 + 22.0,
                        movement.frame.y + 14.5));
                    NSData* gridRender = [document dataWithPDFInsideRect:
                        [document bounds]];
                    ok = gridRender && [gridRender length] > 0u;
                    const char* captureDirectory = std::getenv(
                        "S3G_GUI_SMOKE_PDF_DIR");
                    if (ok && captureDirectory && captureDirectory[0]) {
                        NSString* directory = [NSString
                            stringWithUTF8String:captureDirectory];
                        [[NSFileManager defaultManager]
                            createDirectoryAtPath:directory
                            withIntermediateDirectories:YES
                            attributes:nil error:nil];
                        NSString* gridName = [[NSString stringWithFormat:
                            @"%s.grid", pluginId]
                            stringByAppendingPathExtension:@"pdf"];
                        ok = [gridRender writeToFile:[directory
                                stringByAppendingPathComponent:gridName]
                            atomically:YES];
                    }
                    clickNoInput(NSMakePoint(
                        NSMaxX(wiring) - 92.0, wiring.origin.y + 19.0));
                }
                if (ok) {
                    failureStage = "No Input Mixer movement behavior menu";
                    const auto& movement =
                        s3g::gui_layout::kNoInputMixerFamilyLayout.movement;
                    clickNoInput(NSMakePoint(
                        movement.frame.x + movement.frame.width - 153.0
                            + 48.0 + 22.0,
                        movement.frame.y + 14.5));
                    const NSRect behaviorAnchor = NSMakeRect(
                        s3g::gui_layout::processorControlX(
                            movement.frame.x),
                        s3g::gui_layout::rowY(movement, 0u) - 1.0,
                        s3g::gui_layout::processorMenuWidth(
                            movement.frame.width), 15.0);
                    clickNoInput(NSMakePoint(
                        NSMidX(behaviorAnchor), NSMidY(behaviorAnchor)));
                    clickNoInput(NSMakePoint(NSMidX(behaviorAnchor),
                        NSMaxY(behaviorAnchor) + 2.0 + 18.0 * 3.5));
                    double behavior = 0.0;
                    ok = params->get_value(plugin, 35u, &behavior)
                        && behavior == 3.0;
                    if (ok) {
                        failureStage = "No Input Mixer REACT controls";
                        clickNoInput(NSMakePoint(
                            movement.frame.x + movement.frame.width - 153.0
                                + 2.0 * 48.0 + 22.0,
                            movement.frame.y + 14.5));
                        clickNoInput(NSMakePoint(
                            NSMidX(behaviorAnchor), NSMidY(behaviorAnchor)));
                        clickNoInput(NSMakePoint(NSMidX(behaviorAnchor),
                            NSMaxY(behaviorAnchor) + 2.0 + 18.0 * 1.5));
                        const CGFloat reactTrackX =
                            s3g::gui_layout::processorControlX(
                                movement.frame.x)
                            + s3g::gui_layout::processorTrackWidth(
                                movement.frame.width) * 0.78;
                        clickNoInput(NSMakePoint(reactTrackX,
                            s3g::gui_layout::rowY(movement, 1u) + 5.0));
                        const CGFloat toggleX =
                            s3g::gui_layout::processorControlX(
                                movement.frame.x);
                        const CGFloat toggleY =
                            s3g::gui_layout::rowY(movement, 8u) + 6.0;
                        clickNoInput(NSMakePoint(toggleX + 26.0, toggleY));
                        clickNoInput(NSMakePoint(toggleX + 83.0, toggleY));
                        clickNoInput(NSMakePoint(toggleX + 140.0, toggleY));
                        double reactMode = 0.0;
                        double reactDepth = 0.0;
                        double hold = 0.0;
                        double slow = 0.0;
                        double sync = 0.0;
                        ok = params->get_value(plugin, 44u, &reactMode)
                            && reactMode == 1.0
                            && params->get_value(plugin, 45u, &reactDepth)
                            && reactDepth > 0.70
                            && params->get_value(plugin, 50u, &hold)
                            && hold == 1.0
                            && params->get_value(plugin, 51u, &slow)
                            && slow == 1.0
                            && params->get_value(plugin, 52u, &sync)
                            && sync == 1.0;
                        if (!ok) {
                            std::cerr << "No Input Mixer REACT values: mode="
                                      << reactMode << " depth=" << reactDepth
                                      << " hold=" << hold << " slow=" << slow
                                      << " sync=" << sync << "\n";
                        }
                    }
                }
                if (ok) {
                    failureStage =
                        "No Input Mixer smooth mixer drag and POP window";
                    const auto& family =
                        s3g::gui_layout::kNoInputMixerFamilyLayout;
                    const NSRect plot = s3g::clap_gui::cocoaRect(
                        family.fieldPlot);
                    constexpr CGFloat tabWidth = 52.0;
                    constexpr CGFloat tabGap = 3.0;
                    constexpr CGFloat pageCount = 5.0;
                    const CGFloat tabStart = family.fieldPanel.x
                        + family.fieldPanel.width - pageCount * tabWidth
                        - (pageCount - 1.0) * tabGap - 10.0;
                    clickNoInput(NSMakePoint(
                        tabStart + (tabWidth + tabGap) + tabWidth * 0.5,
                        family.fieldPanel.y + 11.0));

                    failureStage = "No Input Mixer smooth mixer drag";

                    constexpr CGFloat mixerContentWidth = 1216.0;
                    constexpr CGFloat mixerContentHeight = 706.0;
                    const NSPoint mixerOffset = NSMakePoint(
                        plot.origin.x - 12.0
                            + (plot.size.width - mixerContentWidth) * 0.5,
                        plot.origin.y - 42.0
                            + (plot.size.height - mixerContentHeight) * 0.5);
                    constexpr CGFloat popupStripWidth =
                        (858.0 - 6.0 * 7.0) / 8.0;
                    const NSRect bodyTrack = NSMakeRect(
                        mixerOffset.x + 12.0 + 10.0,
                        mixerOffset.y + 42.0 + 54.0,
                        popupStripWidth - 20.0, 10.0);
                    const NSPoint bodyStart = NSMakePoint(
                        NSMinX(bodyTrack) + bodyTrack.size.width * 0.18,
                        NSMidY(bodyTrack));
                    const NSPoint bodyEnd = NSMakePoint(
                        NSMinX(bodyTrack) + bodyTrack.size.width * 0.86,
                        NSMidY(bodyTrack));
                    [document mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, bodyStart)];
                    [document mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, bodyEnd)];
                    [document mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, bodyEnd)];
                    double draggedBody = 0.0;
                    ok = params->get_value(plugin, 1000u, &draggedBody)
                        && std::fabs(draggedBody - 0.86) < 0.03;

                    const NSRect laneInsert = NSMakeRect(
                        mixerOffset.x + 12.0 + 8.0,
                        mixerOffset.y + 42.0 + 500.0,
                        popupStripWidth - 16.0, 18.0);
                    if (ok) clickNoInput(NSMakePoint(
                        NSMidX(laneInsert), NSMidY(laneInsert)));
                    const CGFloat laneMenuY = laneInsert.origin.y
                        - 18.0 * 23.0 - 2.0;
                    if (ok) clickNoInput(NSMakePoint(
                        NSMinX(laneInsert) + 36.0,
                        laneMenuY + 18.0 * 11.5));
                    double laneEffect = 0.0;
                    ok = ok && params->get_value(
                            plugin, 1020u, &laneEffect)
                        && laneEffect == 11.0;

                    failureStage = "No Input Mixer mixer effect editor";
                    const NSRect laneInsertEdit = NSMakeRect(
                        NSMaxX(laneInsert) - 28.0,
                        laneInsert.origin.y, 28.0, laneInsert.size.height);
                    if (ok) clickNoInput(NSMakePoint(
                        NSMidX(laneInsertEdit), NSMidY(laneInsertEdit)));
                    NSPanel* effectPanel = ok
                        ? [document valueForKey:@"effectPanel"] : nil;
                    NSView* effectEditor = effectPanel
                        ? [effectPanel contentView] : nil;
                    ok = ok && effectPanel && [effectPanel isVisible]
                        && effectEditor;
                    if (ok) {
                        auto effectEvent = [&](NSEventType type,
                                               NSPoint point) {
                            return [NSEvent mouseEventWithType:type
                                location:[effectEditor convertPoint:point
                                    toView:nil]
                                modifierFlags:0 timestamp:0.0
                                windowNumber:[effectPanel windowNumber]
                                context:nil eventNumber:0 clickCount:1
                                pressure:1.0];
                        };
                        const NSPoint lengthStart = NSMakePoint(148.0, 134.0);
                        const NSPoint lengthEnd = NSMakePoint(402.0, 134.0);
                        [effectEditor mouseDown:effectEvent(
                            NSEventTypeLeftMouseDown, lengthStart)];
                        [effectEditor mouseDragged:effectEvent(
                            NSEventTypeLeftMouseDragged, lengthEnd)];
                        [effectEditor mouseUp:effectEvent(
                            NSEventTypeLeftMouseUp, lengthEnd)];
                        double spliceLength = 0.0;
                        ok = params->get_value(plugin, 1022u, &spliceLength)
                            && spliceLength > 0.90;
                        NSData* editorRender = ok
                            ? [effectEditor dataWithPDFInsideRect:
                                [effectEditor bounds]] : nil;
                        ok = ok && editorRender
                            && [editorRender length] > 0u;
                        const char* editorDirectory = std::getenv(
                            "S3G_GUI_SMOKE_PDF_DIR");
                        if (ok && editorDirectory && editorDirectory[0]) {
                            NSString* directory = [NSString
                                stringWithUTF8String:editorDirectory];
                            [[NSFileManager defaultManager]
                                createDirectoryAtPath:directory
                                withIntermediateDirectories:YES
                                attributes:nil error:nil];
                            NSString* editorName = [[NSString
                                stringWithFormat:@"%s.effect-editor", pluginId]
                                stringByAppendingPathExtension:@"pdf"];
                            ok = [editorRender writeToFile:[directory
                                    stringByAppendingPathComponent:editorName]
                                atomically:YES];
                        }
                    }

                    const NSRect auxType = NSMakeRect(
                        mixerOffset.x + 884.0 + 114.0,
                        mixerOffset.y + 42.0 + 58.0, 204.0, 18.0);
                    if (ok) clickNoInput(NSMakePoint(
                        NSMidX(auxType), NSMidY(auxType)));
                    if (ok) clickNoInput(NSMakePoint(
                        NSMidX(auxType), NSMaxY(auxType) + 2.0
                            + 18.0 * 18.5));
                    double auxEffect = 0.0;
                    ok = ok && params->get_value(
                            plugin, 23u, &auxEffect)
                        && auxEffect == 18.0;

                    const NSRect auxEdit = NSMakeRect(
                        NSMaxX(auxType) - 54.0, auxType.origin.y,
                        54.0, auxType.size.height);
                    if (ok) clickNoInput(NSMakePoint(
                        NSMidX(auxEdit), NSMidY(auxEdit)));
                    effectPanel = ok
                        ? [document valueForKey:@"effectPanel"] : nil;
                    effectEditor = effectPanel
                        ? [effectPanel contentView] : nil;
                    ok = ok && effectPanel
                        && [effectPanel parentWindow] == nil
                        && ![effectPanel hidesOnDeactivate];
                    if (ok && effectEditor) {
                        auto effectEvent = [&](NSEventType type,
                                               NSPoint point) {
                            return [NSEvent mouseEventWithType:type
                                location:[effectEditor convertPoint:point
                                    toView:nil]
                                modifierFlags:0 timestamp:0.0
                                windowNumber:[effectPanel windowNumber]
                                context:nil eventNumber:0 clickCount:1
                                pressure:1.0];
                        };
                        const NSPoint biasStart = NSMakePoint(148.0, 174.0);
                        const NSPoint biasEnd = NSMakePoint(402.0, 174.0);
                        [effectEditor mouseDown:effectEvent(
                            NSEventTypeLeftMouseDown, biasStart)];
                        [effectEditor mouseDragged:effectEvent(
                            NSEventTypeLeftMouseDragged, biasEnd)];
                        [effectEditor mouseUp:effectEvent(
                            NSEventTypeLeftMouseUp, biasEnd)];
                        double auxBias = 0.0;
                        ok = params->get_value(plugin, 42u, &auxBias)
                            && auxBias > 0.75;
                    } else {
                        ok = false;
                    }

                    const NSPoint auxMuteA = NSMakePoint(
                        mixerOffset.x + 884.0 + 14.0 + 41.0,
                        mixerOffset.y + 42.0 + 58.0 + 9.0);
                    const NSPoint auxMuteB = NSMakePoint(
                        auxMuteA.x, auxMuteA.y + 248.0);
                    double auxMuteValue = 0.0;
                    failureStage = "No Input Mixer aux mute buttons";
                    if (ok) clickNoInput(auxMuteA);
                    ok = ok && params->get_value(
                        plugin, 33u, &auxMuteValue)
                        && auxMuteValue == 1.0;
                    if (ok) clickNoInput(auxMuteA);
                    ok = ok && params->get_value(
                        plugin, 33u, &auxMuteValue)
                        && auxMuteValue == 0.0;
                    if (ok) clickNoInput(auxMuteA);
                    if (ok) clickNoInput(auxMuteB);
                    ok = ok && params->get_value(
                        plugin, 33u, &auxMuteValue)
                        && auxMuteValue == 1.0
                        && params->get_value(plugin, 34u, &auxMuteValue)
                        && auxMuteValue == 1.0;

                    const NSPoint popPoint = NSMakePoint(
                        tabStart - 56.0 + 24.0,
                        family.fieldPanel.y + 11.0);
                    failureStage = "No Input Mixer POP window open";
                    if (ok) clickNoInput(popPoint);
                    NSPanel* mixerPanel = ok
                        ? [document valueForKey:@"mixerPanel"] : nil;
                    NSView* popup = mixerPanel
                        ? [mixerPanel contentView] : nil;
                    ok = ok && mixerPanel && [mixerPanel isVisible]
                        && popup;
                    if (ok) {
                        failureStage = "No Input Mixer POP fader drag";
                        auto popupEvent = [&](NSEventType type,
                                              NSPoint point) {
                            return [NSEvent mouseEventWithType:type
                                location:[popup convertPoint:point toView:nil]
                                modifierFlags:0 timestamp:0.0
                                windowNumber:[mixerPanel windowNumber]
                                context:nil eventNumber:0 clickCount:1
                                pressure:1.0];
                        };
                        const NSRect popupFader = NSMakeRect(
                            mixerOffset.x + 12.0
                                + popupStripWidth * 0.5 - 7.0,
                            mixerOffset.y + 42.0 + 582.0,
                            14.0, 74.0);
                        const NSPoint faderStart = NSMakePoint(
                            NSMidX(popupFader), NSMaxY(popupFader) - 4.0);
                        const NSPoint faderEnd = NSMakePoint(
                            NSMidX(popupFader), NSMinY(popupFader) + 8.0);
                        [popup mouseDown:popupEvent(
                            NSEventTypeLeftMouseDown, faderStart)];
                        [popup mouseDragged:popupEvent(
                            NSEventTypeLeftMouseDragged, faderEnd)];
                        [popup mouseUp:popupEvent(
                            NSEventTypeLeftMouseUp, faderEnd)];
                        double popupLevel = -60.0;
                        ok = params->get_value(plugin, 1002u, &popupLevel)
                            && popupLevel > 3.0;
                        const NSPoint popupInsertName = NSMakePoint(
                            mixerOffset.x + 12.0 + 8.0
                                + (popupStripWidth - 16.0) * 0.5,
                            mixerOffset.y + 42.0 + 500.0 + 9.0);
                        failureStage = "No Input Mixer POP insert menu";
                        if (ok) {
                            [popup mouseDown:popupEvent(
                                NSEventTypeLeftMouseDown,
                                popupInsertName)];
                            [popup mouseUp:popupEvent(
                                NSEventTypeLeftMouseUp,
                                popupInsertName)];
                        }
                        NSData* popupRender = ok
                            ? [popup dataWithPDFInsideRect:[popup bounds]]
                            : nil;
                        ok = ok && popupRender
                            && [popupRender length] > 0u;
                        const char* captureDirectory = std::getenv(
                            "S3G_GUI_SMOKE_PDF_DIR");
                        if (ok && captureDirectory
                            && captureDirectory[0]) {
                            NSString* directory = [NSString
                                stringWithUTF8String:captureDirectory];
                            [[NSFileManager defaultManager]
                                createDirectoryAtPath:directory
                                withIntermediateDirectories:YES
                                attributes:nil error:nil];
                            NSString* popupName = [[NSString
                                stringWithFormat:@"%s.mixer-pop", pluginId]
                                stringByAppendingPathExtension:@"pdf"];
                            ok = [popupRender writeToFile:[directory
                                    stringByAppendingPathComponent:popupName]
                                atomically:YES];
                        }
                    }
                    NSPanel* channelPanel = nil;
                    NSPanel* safetyPanel = nil;
                    NSPanel* auxPanel = nil;
                    NSPanel* patchPanel = nil;
                    failureStage = "No Input Mixer logical POP windows";
                    if (ok) clickNoInput(popPoint);
                    channelPanel = ok
                        ? [document valueForKey:@"channelPanel"] : nil;
                    if (ok) clickNoInput(popPoint);
                    safetyPanel = ok
                        ? [document valueForKey:@"safetyPanel"] : nil;
                    if (ok) clickNoInput(popPoint);
                    auxPanel = ok
                        ? [document valueForKey:@"auxPanel"] : nil;
                    if (ok) clickNoInput(popPoint);
                    patchPanel = ok
                        ? [document valueForKey:@"patchPanel"] : nil;
                    ok = ok && channelPanel && safetyPanel && auxPanel
                        && patchPanel
                        && [channelPanel isVisible]
                        && [safetyPanel isVisible]
                        && [auxPanel isVisible]
                        && [patchPanel isVisible]
                        && [channelPanel parentWindow] == nil
                        && [safetyPanel parentWindow] == nil
                        && [auxPanel parentWindow] == nil
                        && [patchPanel parentWindow] == nil
                        && ![channelPanel hidesOnDeactivate]
                        && ![safetyPanel hidesOnDeactivate]
                        && ![auxPanel hidesOnDeactivate]
                        && ![patchPanel hidesOnDeactivate]
                        && NSWidth([[channelPanel contentView] bounds])
                            == nativeWidth
                        && NSHeight([[channelPanel contentView] bounds])
                            == nativeHeight;
                    if (ok) {
                        failureStage =
                            "No Input Mixer Aux topology interaction";
                        NSView* auxView = [auxPanel contentView];
                        auto auxEvent = [&](NSEventType type,
                                            NSPoint point) {
                            return [NSEvent mouseEventWithType:type
                                location:[auxView convertPoint:point toView:nil]
                                modifierFlags:0 timestamp:0.0
                                windowNumber:[auxPanel windowNumber]
                                context:nil eventNumber:0 clickCount:1
                                pressure:1.0];
                        };
                        const NSRect tapA = NSMakeRect(
                            42.0 + 12.0, 281.0, 128.0, 21.0);
                        [auxView mouseDown:auxEvent(
                            NSEventTypeLeftMouseDown,
                            NSMakePoint(NSMidX(tapA), NSMidY(tapA)))];
                        [auxView mouseUp:auxEvent(
                            NSEventTypeLeftMouseUp,
                            NSMakePoint(NSMidX(tapA), NSMidY(tapA)))];
                        const NSPoint tapChoice = NSMakePoint(
                            NSMidX(tapA), NSMaxY(tapA) + 2.0 + 18.0 * 3.5);
                        [auxView mouseDown:auxEvent(
                            NSEventTypeLeftMouseDown, tapChoice)];
                        [auxView mouseUp:auxEvent(
                            NSEventTypeLeftMouseUp, tapChoice)];
                        const NSRect returnA = NSMakeRect(
                            54.0, 370.0, 128.0, 11.0);
                        const NSPoint returnPoint = NSMakePoint(
                            NSMinX(returnA) + returnA.size.width * 0.125,
                            NSMidY(returnA));
                        [auxView mouseDown:auxEvent(
                            NSEventTypeLeftMouseDown, returnPoint)];
                        [auxView mouseUp:auxEvent(
                            NSEventTypeLeftMouseUp, returnPoint)];
                        double tapValue = 0.0;
                        double returnValue = 0.0;
                        ok = params->get_value(plugin, 1013u, &tapValue)
                            && tapValue == 3.0
                            && params->get_value(plugin, 1015u, &returnValue)
                            && std::fabs(returnValue + 0.75) < 0.03;
                    }
                    if (ok) {
                        failureStage =
                            "No Input Mixer Channel window effect edit";
                        NSView* channelView = [channelPanel contentView];
                        const auto& insertPanel =
                            family.inserts;
                        const CGFloat channelControlX =
                            s3g::gui_layout::processorControlX(
                                insertPanel.frame.x);
                        const CGFloat channelMenuWidth =
                            s3g::gui_layout::processorMenuWidth(
                                insertPanel.frame.width);
                        const NSPoint channelEdit = NSMakePoint(
                            channelControlX + channelMenuWidth - 26.0,
                            s3g::gui_layout::rowY(insertPanel, 0u) + 6.5);
                        auto channelEvent = [&](NSEventType type,
                                                NSPoint point) {
                            return [NSEvent mouseEventWithType:type
                                location:[channelView convertPoint:point
                                    toView:nil]
                                modifierFlags:0 timestamp:0.0
                                windowNumber:[channelPanel windowNumber]
                                context:nil eventNumber:0 clickCount:1
                                pressure:1.0];
                        };
                        failureStage =
                            "No Input Mixer Channel tuning controls";
                        const auto& selectedLanePanel = family.selectedLane;
                        const CGFloat selectedControlX =
                            s3g::gui_layout::processorControlX(
                                selectedLanePanel.frame.x);
                        const CGFloat selectedTrackWidth =
                            s3g::gui_layout::processorTrackWidth(
                                selectedLanePanel.frame.width);
                        const NSPoint tunePoint = NSMakePoint(
                            selectedControlX + selectedTrackWidth * 0.72,
                            s3g::gui_layout::rowY(
                                selectedLanePanel, 3u) + 5.0);
                        [channelView mouseDown:channelEvent(
                            NSEventTypeLeftMouseDown, tunePoint)];
                        [channelView mouseUp:channelEvent(
                            NSEventTypeLeftMouseUp, tunePoint)];
                        const NSPoint lockPoint = NSMakePoint(
                            selectedControlX + 25.0,
                            s3g::gui_layout::rowY(
                                selectedLanePanel, 6u) + 6.0);
                        [channelView mouseDown:channelEvent(
                            NSEventTypeLeftMouseDown, lockPoint)];
                        [channelView mouseUp:channelEvent(
                            NSEventTypeLeftMouseUp, lockPoint)];
                        bool tunedLaneFound = false;
                        for (uint32_t laneIndex = 0u;
                             laneIndex < 8u; ++laneIndex) {
                            double tuneValue = 0.0;
                            double lockValue = 0.0;
                            const clap_id base = 1000u + laneIndex * 100u;
                            tunedLaneFound = tunedLaneFound
                                || (params->get_value(
                                        plugin, base + 10u, &tuneValue)
                                    && tuneValue > 75.0
                                    && params->get_value(
                                        plugin, base + 12u, &lockValue)
                                    && lockValue == 1.0);
                        }
                        ok = ok && tunedLaneFound;
                        failureStage =
                            "No Input Mixer Channel window effect edit";
                        [channelView mouseDown:channelEvent(
                            NSEventTypeLeftMouseDown, channelEdit)];
                        [channelView mouseUp:channelEvent(
                            NSEventTypeLeftMouseUp, channelEdit)];
                        effectPanel = [document valueForKey:@"effectPanel"];
                        effectEditor = effectPanel
                            ? [effectPanel contentView] : nil;
                        ok = effectPanel && [effectPanel isVisible]
                            && effectEditor;
                        if (ok) {
                            auto effectEvent = [&](NSEventType type,
                                                   NSPoint point) {
                                return [NSEvent mouseEventWithType:type
                                    location:[effectEditor convertPoint:point
                                        toView:nil]
                                    modifierFlags:0 timestamp:0.0
                                    windowNumber:[effectPanel windowNumber]
                                    context:nil eventNumber:0 clickCount:1
                                    pressure:1.0];
                            };
                            const NSPoint directionStart =
                                NSMakePoint(402.0, 174.0);
                            const NSPoint directionEnd =
                                NSMakePoint(148.0, 174.0);
                            [effectEditor mouseDown:effectEvent(
                                NSEventTypeLeftMouseDown, directionStart)];
                            [effectEditor mouseDragged:effectEvent(
                                NSEventTypeLeftMouseDragged, directionEnd)];
                            [effectEditor mouseUp:effectEvent(
                                NSEventTypeLeftMouseUp, directionEnd)];
                            double channelDirection = 0.0;
                            ok = params->get_value(
                                    plugin, 1023u, &channelDirection)
                                && channelDirection < -0.75;
                        }
                    }
                    const char* captureDirectory = std::getenv(
                        "S3G_GUI_SMOKE_PDF_DIR");
                    if (ok && captureDirectory && captureDirectory[0]) {
                        NSString* directory = [NSString
                            stringWithUTF8String:captureDirectory];
                        const std::array<std::pair<NSPanel*, NSString*>, 4u>
                            detached {{
                                { channelPanel, @"channel-pop" },
                                { safetyPanel, @"safety-pop" },
                                { auxPanel, @"aux-pop" },
                                { patchPanel, @"patch-pop" },
                            }};
                        for (const auto& item : detached) {
                            NSData* rendered = [[item.first contentView]
                                dataWithPDFInsideRect:
                                    [[item.first contentView] bounds]];
                            NSString* name = [[NSString stringWithFormat:
                                @"%s.%@", pluginId, item.second]
                                stringByAppendingPathExtension:@"pdf"];
                            ok = ok && rendered && [rendered writeToFile:
                                [directory stringByAppendingPathComponent:name]
                                atomically:YES];
                        }
                    }
                    [mixerPanel orderOut:nil];
                    [channelPanel orderOut:nil];
                    [safetyPanel orderOut:nil];
                    [auxPanel orderOut:nil];
                    [patchPanel orderOut:nil];
                    [effectPanel orderOut:nil];
                    clickNoInput(NSMakePoint(
                        tabStart + tabWidth * 0.5,
                        family.fieldPanel.y + 11.0));
                }
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok && faultProcessor) {
            failureStage = "Processor Fault page tabs";
            auto clickFaultPage = [&](NSPoint point) {
                [document mouseDown:mouseEvent(NSEventTypeLeftMouseDown, point)];
                [document mouseUp:mouseEvent(NSEventTypeLeftMouseUp, point)];
                return true;
            };
            @try {
                const bool bassClicked = clickFaultPage(NSMakePoint(1192.0, 240.0));
                const int bassPageValue = [[document valueForKey:@"editorPage"] intValue];
                ok = bassClicked && bassPageValue == 2;
                if (!ok) {
                    std::cerr << "Fault Bass Lab click/page: " << bassClicked
                              << "/" << bassPageValue << "\n";
                }
                if (ok) {
                    failureStage = "Processor Fault Bass Core sliders";
                    constexpr CGFloat bassPanelX = 448.0;
                    constexpr CGFloat bassPanelWidth = 414.0;
                    const CGFloat bassControlX = static_cast<CGFloat>(
                        s3g::gui_layout::processorControlX(bassPanelX));
                    const CGFloat bassTrackWidth = static_cast<CGFloat>(
                        s3g::gui_layout::processorTrackWidth(bassPanelWidth));
                    const std::array<clap_id, 3u> bassHighGainParams {
                        96u, 97u, 98u
                    };
                    const std::array<CGFloat, 3u> bassHighGainRows {
                        364.0, 390.0, 416.0
                    };
                    const std::array<double, 3u> bassHighGainValues {
                        0.34, 0.67, 0.48
                    };
                    for (size_t index = 0u;
                         ok && index < bassHighGainParams.size(); ++index) {
                        const NSPoint point = NSMakePoint(
                            bassControlX + bassTrackWidth
                                * bassHighGainValues[index],
                            bassHighGainRows[index]);
                        NSView* hitView = [parent hitTest:
                            [parent convertPoint:point fromView:document]];
                        ok = hitView == document;
                        if (ok) {
                            [hitView mouseDown:mouseEvent(
                                NSEventTypeLeftMouseDown, point)];
                            [hitView mouseUp:mouseEvent(
                                NSEventTypeLeftMouseUp, point)];
                            double reported = -1.0;
                            ok = params->get_value(plugin,
                                    bassHighGainParams[index], &reported)
                                && std::fabs(reported
                                    - bassHighGainValues[index]) < 0.02;
                            if (!ok) {
                                std::cerr
                                    << "Fault Bass Core slider did not track "
                                    << bassHighGainParams[index] << "\n";
                            }
                        }
                    }
                }
                NSData* bassPage = ok
                    ? [document dataWithPDFInsideRect:[document bounds]]
                    : nil;
                ok = ok && bassPage && [bassPage length] > 0u;
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                if (ok && captureDirectory && captureDirectory[0]) {
                    NSString* directory = [NSString
                        stringWithUTF8String:captureDirectory];
                    [[NSFileManager defaultManager]
                        createDirectoryAtPath:directory
                        withIntermediateDirectories:YES
                        attributes:nil
                        error:nil];
                    NSString* pageName = [[NSString
                        stringWithFormat:@"%s.bass", pluginId]
                        stringByAppendingPathExtension:@"pdf"];
                    ok = [bassPage writeToFile:
                        [directory stringByAppendingPathComponent:pageName]
                        atomically:YES];
                }
                ok = ok
                    && clickFaultPage(NSMakePoint(1286.0, 240.0))
                    && [[document valueForKey:@"editorPage"] intValue] == 1
                    && clickFaultPage(NSMakePoint(1104.0, 240.0))
                    && [[document valueForKey:@"editorPage"] intValue] == 0;
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok && topologyProcessor) {
            failureStage = "topology field interaction";
            const NSRect fieldPanel = s3g::clap_gui::cocoaRect(
                s3g::gui_layout::kTopologyProcessorColumns.field);
            const NSRect fieldContent =
                s3g::clap_gui::topologyProcessorFieldContentRect(
                    fieldPanel);
            auto clickRect = [&](NSRect rect) {
                const NSPoint point = NSMakePoint(
                    NSMidX(rect), NSMidY(rect));
                NSView* hitView = [parent hitTest:
                    [parent convertPoint:point fromView:document]];
                if (hitView != document) return false;
                [hitView mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, point)];
                [hitView mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, point)];
                return true;
            };

            auto setDocumentationTopologyParam = [&](
                    const char* name, double requestedValue) {
                const uint32_t parameterCount = params->count(plugin);
                for (uint32_t index = 0u;
                     index < parameterCount; ++index) {
                    clap_param_info_t info {};
                    if (!params->get_info(plugin, index, &info)) {
                        std::cerr << "Could not inspect documentation parameter "
                            << index << " while looking for " << name << "\n";
                        return false;
                    }
                    if (std::strcmp(info.name, name) != 0) continue;
                    SingleParamEventInput event {};
                    const double value = std::clamp(
                        requestedValue, info.min_value, info.max_value);
                    setSingleParamEvent(event, info.id, value);
                    params->flush(plugin, &event.events, nullptr);
                    double reported = 0.0;
                    return params->get_value(plugin, info.id, &reported)
                        && std::fabs(reported - value) < 0.000001;
                }
                return false;
            };

            const uint32_t documentationCamera = waveGeometry
                ? 0u : (spectralTopology ? 1u : 2u);

            @try {
                if (documentationCapture) {
                    failureStage = "documentation topology scene";
                    if (delayProcessor) {
                        const auto* pluginState =
                            static_cast<const clap_plugin_state_t*>(
                                plugin->get_extension(
                                    plugin, CLAP_EXT_STATE));
                        DelayProcessorStateV10 fixture {};
                        const uint32_t channelCount = std::strcmp(
                                pluginId,
                                "org.s3g.s3g-dsp.delay-processor-24ch") == 0
                            ? 24u : 8u;
                        for (uint32_t lane = 0u;
                             lane < channelCount; ++lane) {
                            fixture.patchRows[lane] =
                                uint64_t { 1 } << lane;
                        }
                        fixture.clearUnused = 1u;
                        fixture.delayMs = 420.0;
                        fixture.feedback = 0.48;
                        fixture.mix = 0.78;
                        fixture.tone = 0.64;
                        fixture.character = 0.34;
                        fixture.tapAmount = 0.28;
                        fixture.outputTrimDb = -6.0;
                        MemoryPluginState memory;
                        const auto* first =
                            reinterpret_cast<const uint8_t*>(&fixture);
                        memory.bytes.assign(
                            first, first + sizeof(fixture));
                        clap_istream_t input { &memory, stateReadWhole };
                        ok = pluginState && pluginState->load
                            && pluginState->load(plugin, &input)
                            && memory.offset == sizeof(fixture)
                            && setDocumentationTopologyParam(
                                "Topology Shape", 3.0)
                            && setDocumentationTopologyParam(
                                "Topology Amount", 0.88)
                            && setDocumentationTopologyParam(
                                "Topology Pull", 0.08)
                            && setDocumentationTopologyParam(
                                "Topology X", 0.42)
                            && setDocumentationTopologyParam(
                                "Topology Y", -0.24)
                            && setDocumentationTopologyParam(
                                "Topology Z", 0.84)
                            && setDocumentationTopologyParam(
                                "Topology Twist", 0.52)
                            && setDocumentationTopologyParam(
                                "Topology Flare", 0.30)
                            && setDocumentationTopologyParam(
                                "Topology Seed", 0.16)
                            && setDocumentationTopologyParam(
                                "Topology Neighbors", 3.0)
                            && setDocumentationTopologyParam(
                                "Topology Radius", 0.92)
                            && setDocumentationTopologyParam(
                                "Topology Centroid", 0.38)
                            && setDocumentationTopologyParam(
                                "Route", 0.68)
                            && setDocumentationTopologyParam(
                                "Turn", 0.26)
                            && setDocumentationTopologyParam(
                                "Branch", 0.62)
                            && setDocumentationTopologyParam(
                                "Loss", 0.30);
                    } else if (waveGeometry) {
                        ok = setDocumentationTopologyParam(
                                "Topology Shape", 9.0)
                            && setDocumentationTopologyParam(
                                "Topology Amount", 0.86)
                            && setDocumentationTopologyParam(
                                "Topology Pull", 0.04)
                            && setDocumentationTopologyParam(
                                "Topology X", 0.72)
                            && setDocumentationTopologyParam(
                                "Topology Y", -0.62)
                            && setDocumentationTopologyParam(
                                "Topology Z", 0.70)
                            && setDocumentationTopologyParam(
                                "Topology Twist", 0.0)
                            && setDocumentationTopologyParam(
                                "Topology Flare", 0.08)
                            && setDocumentationTopologyParam(
                                "Topology Seed", 0.04)
                            && setDocumentationTopologyParam(
                                "Topology Neighbors", 1.0)
                            && setDocumentationTopologyParam(
                                "Topology Radius", 0.88)
                            && setDocumentationTopologyParam(
                                "Topology Centroid", 0.08)
                            && setDocumentationTopologyParam("Fold", 0.58)
                            && setDocumentationTopologyParam("Drive", 0.42)
                            && setDocumentationTopologyParam(
                                "Mesh Coupling", 0.58)
                            && setDocumentationTopologyParam(
                                "Mesh Tension", 0.68)
                            && setDocumentationTopologyParam(
                                "Mesh Decay", 0.48)
                            && setDocumentationTopologyParam(
                                "Mesh Damping", 0.32);
                    } else if (spectralTopology) {
                        ok = setDocumentationTopologyParam(
                                "Topology Shape", 6.0)
                            && setDocumentationTopologyParam(
                                "Topology Amount", 0.68)
                            && setDocumentationTopologyParam(
                                "Topology Pull", 0.05)
                            && setDocumentationTopologyParam(
                                "Topology X", -0.28)
                            && setDocumentationTopologyParam(
                                "Topology Y", 0.36)
                            && setDocumentationTopologyParam(
                                "Topology Z", 0.72)
                            && setDocumentationTopologyParam(
                                "Topology Twist", -0.18)
                            && setDocumentationTopologyParam(
                                "Topology Flare", 0.12)
                            && setDocumentationTopologyParam(
                                "Topology Seed", 0.32)
                            && setDocumentationTopologyParam(
                                "Topology Neighbors", 3.0)
                            && setDocumentationTopologyParam(
                                "Topology Radius", 0.76)
                            && setDocumentationTopologyParam(
                                "Topology Centroid", 0.36)
                            && setDocumentationTopologyParam(
                                "Spray Bins", 72.0)
                            && setDocumentationTopologyParam("Drift", 0.46)
                            && setDocumentationTopologyParam("Hold", 0.56)
                            && setDocumentationTopologyParam("Smear", 0.62)
                            && setDocumentationTopologyParam("Holes", 0.24)
                            && setDocumentationTopologyParam(
                                "Spectral Damage", 0.34)
                            && setDocumentationTopologyParam(
                                "Spectral Repeat", 0.42);
                    }
                    if (ok) {
                        [document setNeedsDisplay:YES];
                        [document displayIfNeeded];
                    }
                }

                ok = clickRect(
                        s3g::clap_gui::topologyProcessorCameraButtonRect(
                            fieldPanel, 0u))
                    && [[document valueForKey:@"cameraView"] intValue]
                        == 0;
                const NSPoint dragStart = NSMakePoint(
                    fieldContent.origin.x + 180.0,
                    fieldContent.origin.y + 240.0);
                const NSPoint dragEnd = NSMakePoint(
                    dragStart.x + 42.0, dragStart.y + 28.0);
                NSView* dragView = [parent hitTest:
                    [parent convertPoint:dragStart fromView:document]];
                if (ok && dragView == document) {
                    [dragView mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, dragStart)];
                    [dragView mouseDragged:mouseEvent(
                        NSEventTypeLeftMouseDragged, dragEnd)];
                    [dragView mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, dragEnd)];
                    ok = [[document valueForKey:@"cameraView"] intValue]
                        == -1;
                } else {
                    ok = false;
                }

                ok = ok && clickRect(
                        s3g::clap_gui::topologyProcessorFieldPageButtonRect(
                            fieldPanel, 1u))
                    && [[document valueForKey:@"fieldPage"] intValue]
                        == 1;
                NSData* secondPage = ok
                    ? [document dataWithPDFInsideRect:[document bounds]]
                    : nil;
                ok = ok && secondPage && [secondPage length] > 0u;
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                if (ok && captureDirectory && captureDirectory[0]) {
                    NSString* directory = [NSString
                        stringWithUTF8String:captureDirectory];
                    [[NSFileManager defaultManager]
                        createDirectoryAtPath:directory
                        withIntermediateDirectories:YES
                        attributes:nil
                        error:nil];
                    NSString* pageName = [[NSString
                        stringWithFormat:@"%s.page2", pluginId]
                        stringByAppendingPathExtension:@"pdf"];
                    ok = [secondPage writeToFile:
                        [directory stringByAppendingPathComponent:pageName]
                        atomically:YES];
                }

                ok = ok && clickRect(
                        s3g::clap_gui::topologyProcessorFieldPageButtonRect(
                            fieldPanel, 0u))
                    && clickRect(
                        s3g::clap_gui::topologyProcessorCameraButtonRect(
                            fieldPanel, documentationCapture
                                ? documentationCamera : 2u))
                    && [[document valueForKey:@"fieldPage"] intValue]
                        == 0
                    && [[document valueForKey:@"cameraView"] intValue]
                        == static_cast<int>(documentationCapture
                            ? documentationCamera : 2u);
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok && documentationMacroRelationship) {
            failureStage = "documentation Macro relationship scene";
            auto setDocumentationMacroParam = [&](const char* name,
                                                   double requestedValue) {
                const uint32_t parameterCount = params->count(plugin);
                for (uint32_t index = 0u;
                     index < parameterCount; ++index) {
                    clap_param_info_t info {};
                    if (!params->get_info(plugin, index, &info)) {
                        return false;
                    }
                    if (std::strcmp(info.name, name) != 0) continue;
                    SingleParamEventInput event {};
                    const double value = std::clamp(
                        requestedValue, info.min_value, info.max_value);
                    setSingleParamEvent(event, info.id, value);
                    params->flush(plugin, &event.events, nullptr);
                    double reported = 0.0;
                    return params->get_value(plugin, info.id, &reported)
                        && std::fabs(reported - value) < 0.000001;
                }
                return false;
            };
            ok = setDocumentationMacroParam("Spread", 0.64)
                && setDocumentationMacroParam("Deviation", 0.38);
            if (ok) {
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];
            }
        }
        if (ok && documentationEncoder) {
            failureStage = "documentation encoder scene";
            auto setDocumentationParam = [&](const char* name,
                                             double requestedValue) {
                const uint32_t parameterCount = params->count(plugin);
                for (uint32_t index = 0u; index < parameterCount; ++index) {
                    clap_param_info_t info {};
                    if (!params->get_info(plugin, index, &info)) return false;
                    if (std::strcmp(info.name, name) != 0) continue;
                    SingleParamEventInput event {};
                    const double value = std::clamp(requestedValue,
                        info.min_value, info.max_value);
                    setSingleParamEvent(event, info.id, value);
                    params->flush(plugin, &event.events, nullptr);
                    double reported = 0.0;
                    return params->get_value(plugin, info.id, &reported)
                        && std::fabs(reported - value) < 0.000001;
                }
                return false;
            };

            if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-wind-encoder-64") == 0) {
                // TORNADO COLUMN is the widest, fastest factory wind field.
                ok = setDocumentationParam("Preset", 16.0)
                    && setDocumentationParam("Voices", 64.0)
                    && setDocumentationParam("Field Rate", 0.55)
                    && setDocumentationParam("Flow Push", 0.88)
                    && setDocumentationParam("Shear", 0.76)
                    && setDocumentationParam("Curl", 1.0)
                    && setDocumentationParam("Updraft", 0.86)
                    && setDocumentationParam("Spread", 0.82)
                    && setDocumentationParam("Deviation", 0.30);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-water-encoder-64") == 0) {
                // STORM SHEET keeps a dense parcel population in motion.
                ok = setDocumentationParam("Preset", 14.0)
                    && setDocumentationParam("Voices", 64.0)
                    && setDocumentationParam("Parcel Rate", 0.72)
                    && setDocumentationParam("Current", 0.88)
                    && setDocumentationParam("Eddy", 0.72)
                    && setDocumentationParam("Width", 0.92)
                    && setDocumentationParam("Spread", 0.90)
                    && setDocumentationParam("Deviation", 0.34);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-pyrosphere-encoder-64") == 0) {
                // FIRESTORM DEBRIS FIELD exposes the large-scale transport,
                // fragments, vortex, and causal-score layers together.
                ok = setDocumentationParam("Preset", 11.0)
                    && setDocumentationParam("Voices", 64.0)
                    && setDocumentationParam("Field Rate", 0.72)
                    && setDocumentationParam("Flow Push", 0.82)
                    && setDocumentationParam("Shear", 0.76)
                    && setDocumentationParam("Curl", 0.88)
                    && setDocumentationParam("Updraft", 0.82)
                    && setDocumentationParam("Fragments", 0.88)
                    && setDocumentationParam("Plume Vortex", 0.86)
                    && setDocumentationParam("Pressure Release", 0.82)
                    && setDocumentationParam("Score Pace", 0.90)
                    && setDocumentationParam("Entity Occupancy", 0.92)
                    && setDocumentationParam("Causal Cascade", 0.90)
                    && setDocumentationParam("Scored Rest", 0.08);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-cryosphere-encoder-64") == 0) {
                // AVALANCHE RELEASE provides a full descending mass rather
                // than the sparse isolated events used by smaller scenes.
                ok = setDocumentationParam("Preset", 7.0)
                    && setDocumentationParam("Voices", 64.0)
                    && setDocumentationParam("Drift Rate", 0.65)
                    && setDocumentationParam("Drift", 0.82)
                    && setDocumentationParam("Torque", 0.72)
                    && setDocumentationParam("Compression", 0.84)
                    && setDocumentationParam("Width", 0.92)
                    && setDocumentationParam("Spread", 0.90)
                    && setDocumentationParam("Deviation", 0.38)
                    && setDocumentationParam("Snow", 0.76)
                    && setDocumentationParam("Grinding", 0.72)
                    && setDocumentationParam("Score Pace", 0.90)
                    && setDocumentationParam("Entity Occupancy", 0.90)
                    && setDocumentationParam("Causal Cascade", 0.88)
                    && setDocumentationParam("Scored Rest", 0.08);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-insect-encoder-64") == 0) {
                // MIXED SUMMER NIGHT fills several strata with independently
                // moving colonies and makes the swarm structure legible.
                ok = setDocumentationParam("Preset", 15.0)
                    && setDocumentationParam("Voices", 64.0)
                    && setDocumentationParam("Activity", 0.92)
                    && setDocumentationParam("Field Rate", 0.36)
                    && setDocumentationParam("Roam", 0.78)
                    && setDocumentationParam("Scatter", 0.88)
                    && setDocumentationParam("Orbit", 0.64)
                    && setDocumentationParam("Lift", 0.52)
                    && setDocumentationParam("Near Pass", 0.36);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-cloud-encoder-64") == 0) {
                ok = setDocumentationParam("Clouds", 4.0)
                    && setDocumentationParam("Spread", 0.72)
                    && setDocumentationParam("Elevation Spread", 0.58)
                    && setDocumentationParam("Jitter", 0.18)
                    && setDocumentationParam("Drift", 0.70)
                    && setDocumentationParam("Force", 4.0)
                    && setDocumentationParam("Cloud 1 Azimuth", -60.0)
                    && setDocumentationParam("Cloud 1 Elevation", 25.0)
                    && setDocumentationParam("Cloud 1 Distance", 0.80)
                    && setDocumentationParam("Cloud 2 Azimuth", 35.0)
                    && setDocumentationParam("Cloud 2 Elevation", -20.0)
                    && setDocumentationParam("Cloud 2 Distance", 1.15)
                    && setDocumentationParam("Cloud 3 Azimuth", 125.0)
                    && setDocumentationParam("Cloud 3 Elevation", 35.0)
                    && setDocumentationParam("Cloud 3 Distance", 0.90)
                    && setDocumentationParam("Cloud 4 Azimuth", -150.0)
                    && setDocumentationParam("Cloud 4 Elevation", -30.0)
                    && setDocumentationParam("Cloud 4 Distance", 1.30);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-path-encoder-64") == 0) {
                ok = setDocumentationParam("Inputs", 18.0)
                    && setDocumentationParam("Active Paths", 4.0)
                    && setDocumentationParam("Phase Spread", 0.92)
                    && setDocumentationParam("Rate", 0.34);
                if (ok) {
                    @try {
                        [document setValue:@1.0 forKey:@"randomDev"];
                        [document setValue:@12 forKey:@"randomPoints"];
                        ok = [document respondsToSelector:
                            @selector(setViewPreset:)];
                        if (ok) [document setViewPreset:2];
                        if (ok) [document setValue:@YES
                            forKey:@"editMode"];
                    } @catch (NSException*) {
                        ok = false;
                    }
                }
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-pulsar-encoder-64") == 0) {
                ok = setDocumentationParam("Preset", 9.0)
                    && setDocumentationParam("Spatial Points", 32.0)
                    && setDocumentationParam("Spatial Width", 0.88)
                    && setDocumentationParam("Spatial Scatter", 0.72)
                    && setDocumentationParam("Orbit Rate", 0.38)
                    && setDocumentationParam("Orbit Depth", 0.86)
                    && setDocumentationParam("Field Motion", 4.0);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-neural-ecology-64") == 0) {
                // Ecology 64 exposes the complete recurrent organism and its
                // eight-pickup auditory body, making the FIELD capture useful.
                ok = setDocumentationParam("Preset", 4.0)
                    && setDocumentationParam("Activity Bias", 0.65)
                    && setDocumentationParam("Rotation Rate", 0.08)
                    && setDocumentationParam("Field Lattice Mode", 1.0)
                    && setDocumentationParam("Field Lattice Amount", 0.82)
                    && setDocumentationParam("Field Lattice Planes", 2.0);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-stochastic-encoder-64") == 0) {
                ok = setDocumentationParam("Voices", 24.0)
                    && setDocumentationParam("Topology Animation", 4.0)
                    && setDocumentationParam("Topology Rate", 0.08)
                    && setDocumentationParam("Topology Amount", 0.82)
                    && setDocumentationParam("Topology Depth", 0.78)
                    && setDocumentationParam("Minimum Rest", 0.05)
                    && setDocumentationParam("Field Listener", 4.0)
                    && setDocumentationParam("Listener Capture", 0.72);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-wrangler-encoder-64") == 0) {
                ok = setDocumentationParam("Preset", 19.0);
            } else if (std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-vot-encoder-64") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-vox-encoder-64") == 0) {
                ok = setDocumentationParam("Voices", 16.0)
                    && setDocumentationParam("Motion Scene",
                        std::strcmp(pluginId,
                            "org.s3g.s3g-dsp.ambi-vot-encoder-64") == 0
                            ? 3.0 : 1.0)
                    && setDocumentationParam("Motion Amount", 0.92)
                    && setDocumentationParam("Motion Spread", 0.94)
                    && setDocumentationParam("Motion Coherence", 0.40)
                    && setDocumentationParam("Motion Chaos", 0.21);
                if (ok && std::strcmp(pluginId,
                        "org.s3g.s3g-dsp.ambi-vox-encoder-64") == 0) {
                    ok = setDocumentationParam("Ensemble", 2.0);
                }
            }

            if (ok && std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-vox-encoder-64") == 0) {
                failureStage = "documentation Vox lyric field";
                @try {
                    NSTextView* editor = static_cast<NSTextView*>(
                        [document valueForKey:@"lyricsEditor"]);
                    ok = editor
                        && [document respondsToSelector:
                            @selector(textDidChange:)];
                    if (ok) {
                        [editor setString:
                            @"za a mi u\nka na ri o\nsa e ru ma\n"
                            @"no va ki e\nte ra su o\nmi o ke na"];
                        NSNotification* notification = [NSNotification
                            notificationWithName:NSTextDidChangeNotification
                            object:editor];
                        [document textDidChange:notification];
                    }
                } @catch (NSException*) {
                    ok = false;
                }
            }

            const auto* audioPorts = ok
                ? static_cast<const clap_plugin_audio_ports_t*>(
                    plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS))
                : nullptr;
            const uint32_t inputPortCount = audioPorts
                ? audioPorts->count(plugin, true) : 0u;
            const uint32_t outputPortCount = audioPorts
                ? audioPorts->count(plugin, false) : 0u;
            clap_audio_port_info_t inputInfo {};
            clap_audio_port_info_t outputInfo {};
            ok = ok && audioPorts
                && inputPortCount <= 1u
                && outputPortCount == 1u
                && (inputPortCount == 0u
                    || audioPorts->get(plugin, 0u, true, &inputInfo))
                && audioPorts->get(plugin, 0u, false, &outputInfo)
                && inputInfo.channel_count <= 64u
                && outputInfo.channel_count > 0u
                && outputInfo.channel_count <= 64u;

            constexpr uint32_t audioFrames = 128u;
            constexpr uint32_t audioChannels = 64u;
            // Two seconds is long enough for low-rate field motion, ecology
            // activity, and emitted voices to become visible without making
            // documentation capture needlessly slow.
            constexpr uint32_t audioBlocks = 750u;
            std::array<std::array<float, audioFrames>, audioChannels>
                audioInput {};
            std::array<std::array<float, audioFrames>, audioChannels>
                audioOutput {};
            std::array<float*, audioChannels> inputPointers {};
            std::array<float*, audioChannels> outputPointers {};
            for (uint32_t channel = 0u; channel < audioChannels; ++channel) {
                inputPointers[channel] = audioInput[channel].data();
                outputPointers[channel] = audioOutput[channel].data();
            }
            clap_audio_buffer_t inputBuffer {};
            inputBuffer.data32 = inputPointers.data();
            inputBuffer.channel_count = inputInfo.channel_count;
            clap_audio_buffer_t outputBuffer {};
            outputBuffer.data32 = outputPointers.data();
            outputBuffer.channel_count = outputInfo.channel_count;
            clap_process_t processBlock {};
            processBlock.frames_count = audioFrames;
            processBlock.audio_inputs = inputPortCount ? &inputBuffer : nullptr;
            processBlock.audio_inputs_count = inputPortCount;
            processBlock.audio_outputs = &outputBuffer;
            processBlock.audio_outputs_count = 1u;

            bool activated = false;
            bool processing = false;
            if (ok) {
                ok = gui->show(plugin);
                activated = ok
                    && plugin->activate(plugin, 48000.0, 1u, audioFrames);
                processing = activated && plugin->start_processing(plugin);
                ok = ok && activated && processing;
            }
            uint64_t sampleCursor = 0u;
            for (uint32_t block = 0u; ok && block < audioBlocks; ++block) {
                for (uint32_t frame = 0u; frame < audioFrames; ++frame) {
                    const double time = static_cast<double>(sampleCursor++)
                        / 48000.0;
                    for (uint32_t channel = 0u;
                         channel < inputInfo.channel_count; ++channel) {
                        const double frequency = 109.0
                            + 7.25 * static_cast<double>(channel);
                        audioInput[channel][frame] = 0.12f
                            * static_cast<float>(std::sin(
                                2.0 * s3g::kPi * frequency * time
                                + 0.17 * static_cast<double>(channel)));
                    }
                }
                for (auto& channel : audioOutput) channel.fill(0.0f);
                processBlock.steady_time =
                    static_cast<int64_t>(block) * audioFrames;
                ok = plugin->process(plugin, &processBlock)
                    != CLAP_PROCESS_ERROR;
                if (ok && (block % 32u) == 31u) {
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                }
            }
            if (ok) {
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];
            }
            if (processing) plugin->stop_processing(plugin);
            if (activated) plugin->deactivate(plugin);

            if (ok && std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.ambi-path-encoder-64") == 0) {
                failureStage = "documentation Path trajectories";
                @try {
                    ok = [document respondsToSelector:
                            @selector(randomizePaths)]
                        && [document respondsToSelector:
                            @selector(loadDocumentationPaths)];
                    if (ok) [document performSelector:
                        @selector(randomizePaths)];
                    if (ok) [document loadDocumentationPaths];
                    if (ok) {
                        [document setNeedsDisplay:YES];
                        [document displayIfNeeded];
                    }
                } @catch (NSException*) {
                    ok = false;
                }
            }

            if (ok) {
                failureStage = "documentation encoder alternate pages";
                auto scrollDocumentationTo = [&](CGFloat y) {
                    if (!scroll) return y == 0.0;
                    [[scroll contentView]
                        scrollToPoint:NSMakePoint(0.0, y)];
                    [scroll reflectScrolledClipView:[scroll contentView]];
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                    return true;
                };
                auto clickDocumentationPoint = [&](NSPoint point) {
                    NSPoint parentPoint = [parent
                        convertPoint:point fromView:document];
                    NSView* hitView = [parent hitTest:parentPoint];
                    if (!hitView
                        || (hitView != document
                            && ![hitView isDescendantOf:document])) {
                        return false;
                    }
                    [hitView mouseDown:mouseEvent(
                        NSEventTypeLeftMouseDown, point)];
                    [hitView mouseUp:mouseEvent(
                        NSEventTypeLeftMouseUp, point)];
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                    return true;
                };
                auto selectDocumentationPage = [&](NSPoint point,
                                                   NSString* key,
                                                   int expected) {
                    if (!scrollDocumentationTo(0.0)
                        || !clickDocumentationPoint(point)) {
                        return false;
                    }
                    NSNumber* page = [document valueForKey:key];
                    return page && [page intValue] == expected;
                };
                auto writeDocumentationVariant = [&](const char* suffix) {
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                    NSData* rendered = [document
                        dataWithPDFInsideRect:[document bounds]];
                    if (!rendered || [rendered length] == 0u) return false;
                    const char* captureDirectory = std::getenv(
                        "S3G_GUI_SMOKE_PDF_DIR");
                    if (!captureDirectory || !captureDirectory[0]) return true;
                    NSString* directory = [NSString
                        stringWithUTF8String:captureDirectory];
                    [[NSFileManager defaultManager]
                        createDirectoryAtPath:directory
                        withIntermediateDirectories:YES
                        attributes:nil
                        error:nil];
                    NSString* baseName = [NSString
                        stringWithFormat:@"%s.%s", pluginId, suffix];
                    NSString* fileName = [baseName
                        stringByAppendingPathExtension:@"pdf"];
                    return [rendered writeToFile:[directory
                        stringByAppendingPathComponent:fileName]
                        atomically:YES];
                };
                auto captureDocumentationPage = [&](NSPoint point,
                                                    NSString* key,
                                                    int expected,
                                                    const char* suffix) {
                    return selectDocumentationPage(point, key, expected)
                        && writeDocumentationVariant(suffix);
                };

                @try {
                    if (std::strcmp(pluginId,
                            "org.s3g.s3g-dsp.ambi-point-encoder-64") == 0) {
                        ok = captureDocumentationPage(
                                NSMakePoint(199.0, 52.5), @"leftPage", 1,
                                "mixer")
                            && selectDocumentationPage(
                                NSMakePoint(146.0, 52.5), @"leftPage", 0);
                    } else if (std::strcmp(pluginId,
                            "org.s3g.s3g-dsp.ambi-vot-encoder-64") == 0) {
                        ok = captureDocumentationPage(
                                NSMakePoint(214.0, 52.5), @"leftPage", 1,
                                "vector")
                            && captureDocumentationPage(
                                NSMakePoint(272.0, 52.5), @"leftPage", 2,
                                "score")
                            && selectDocumentationPage(
                                NSMakePoint(156.0, 52.5), @"leftPage", 0);
                    } else if (std::strcmp(pluginId,
                            "org.s3g.s3g-dsp.ambi-vox-encoder-64") == 0) {
                        ok = captureDocumentationPage(
                                NSMakePoint(214.0, 52.5), @"leftPage", 3,
                                "lyrics")
                            && selectDocumentationPage(
                                NSMakePoint(156.0, 52.5), @"leftPage", 0);
                    } else if (std::strcmp(pluginId,
                            "org.s3g.s3g-dsp.ambi-pulsar-encoder-64") == 0) {
                        ok = captureDocumentationPage(
                                NSMakePoint(225.0, 52.5), @"visualPage", 1,
                                "pulsarets")
                            && captureDocumentationPage(
                                NSMakePoint(299.0, 52.5), @"visualPage", 2,
                                "neural")
                            && captureDocumentationPage(
                                NSMakePoint(373.0, 52.5), @"visualPage", 3,
                                "listen")
                            && selectDocumentationPage(
                                NSMakePoint(151.0, 52.5), @"visualPage", 0);
                    } else if (std::strcmp(pluginId,
                            "org.s3g.s3g-dsp.ambi-neural-ecology-64") == 0) {
                        ok = selectDocumentationPage(
                            NSMakePoint(372.0, 52.5), @"scorePage", 1);
                        if (ok) {
                            scrollDocumentationTo(318.0);
                            ok = clickDocumentationPoint(
                                NSMakePoint(496.0, 792.0));
                        }
                        if (ok) {
                            scrollDocumentationTo(0.0);
                            ok = clickDocumentationPoint(
                                    NSMakePoint(207.0, 152.5))
                                && writeDocumentationVariant("score")
                                && selectDocumentationPage(
                                    NSMakePoint(321.0, 52.5),
                                    @"scorePage", 0);
                        }
                    } else if (std::strcmp(pluginId,
                            "org.s3g.s3g-dsp.ambi-stochastic-encoder-64") == 0) {
                        ok = captureDocumentationPage(
                                NSMakePoint(225.0, 52.5), @"fieldPage", 1,
                                "listen")
                            && selectDocumentationPage(
                                NSMakePoint(151.0, 52.5), @"fieldPage", 0);
                    } else if (std::strcmp(pluginId,
                            "org.s3g.s3g-dsp.ambi-wrangler-encoder-64") == 0) {
                        ok = captureDocumentationPage(
                                NSMakePoint(299.0, 52.5), @"fieldPage", 2,
                                "listen")
                            && selectDocumentationPage(
                                NSMakePoint(151.0, 52.5), @"fieldPage", 0)
                            && setDocumentationParam("Preset", 3.0)
                            && captureDocumentationPage(
                                NSMakePoint(225.0, 52.5), @"fieldPage", 1,
                                "curve")
                            && setDocumentationParam("Preset", 19.0)
                            && selectDocumentationPage(
                                NSMakePoint(151.0, 52.5), @"fieldPage", 0);
                    }
                } @catch (NSException*) {
                    ok = false;
                }
            }
        }
        if (ok && documentationSampleProcessor) {
            failureStage = "documentation sample processor scene";
            const auto* audioPorts =
                static_cast<const clap_plugin_audio_ports_t*>(
                    plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
            clap_audio_port_info_t outputInfo {};
            ok = audioPorts
                && audioPorts->count(plugin, true) == 0u
                && audioPorts->count(plugin, false) == 1u
                && audioPorts->get(plugin, 0u, false, &outputInfo)
                && outputInfo.channel_count > 0u
                && outputInfo.channel_count <= 64u;

            constexpr uint32_t audioFrames = 128u;
            constexpr uint32_t audioChannels = 64u;
            const uint32_t audioBlocks = documentationAmbiGrain
                ? 120u : 375u;
            std::array<std::array<float, audioFrames>, audioChannels>
                audioOutput {};
            std::array<float*, audioChannels> outputPointers {};
            for (uint32_t channel = 0u; channel < audioChannels; ++channel) {
                outputPointers[channel] = audioOutput[channel].data();
            }
            clap_audio_buffer_t outputBuffer {};
            outputBuffer.data32 = outputPointers.data();
            outputBuffer.channel_count = outputInfo.channel_count;
            clap_process_t processBlock {};
            processBlock.frames_count = audioFrames;
            processBlock.audio_outputs = &outputBuffer;
            processBlock.audio_outputs_count = 1u;

            bool activated = false;
            bool processing = false;
            if (ok) {
                ok = gui->show(plugin);
                activated = ok
                    && plugin->activate(plugin, 48000.0, 1u, audioFrames);
                processing = activated && plugin->start_processing(plugin);
                ok = ok && activated && processing;
            }
            for (uint32_t block = 0u; ok && block < audioBlocks; ++block) {
                for (auto& channel : audioOutput) channel.fill(0.0f);
                processBlock.steady_time =
                    static_cast<int64_t>(block) * audioFrames;
                ok = plugin->process(plugin, &processBlock)
                    != CLAP_PROCESS_ERROR;
                if (ok && (block % 16u) == 15u) {
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                }
            }
            if (ok) {
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];
            }
            if (processing) plugin->stop_processing(plugin);
            if (activated) plugin->deactivate(plugin);
        }
        const bool documentationRoutingScene =
            documentationLayoutPanner
            || documentationDbapPanner
            || documentationLbapPanner
            || documentationVbapPanner
            || documentationGroupMatrix
            || documentationNodeBusMixer
            || documentationSubCrossover
            || documentationArrayDelay
            || documentationArrayTrim;
        if (ok && documentationRoutingScene) {
            failureStage = "documentation routing scene";
            auto setDocumentationRoutingParam = [&](
                    const char* name, double requestedValue) {
                const uint32_t parameterCount = params->count(plugin);
                for (uint32_t index = 0u;
                     index < parameterCount; ++index) {
                    clap_param_info_t info {};
                    if (!params->get_info(plugin, index, &info)) {
                        return false;
                    }
                    if (std::strcmp(info.name, name) != 0) continue;
                    SingleParamEventInput event {};
                    const double value = std::clamp(
                        requestedValue, info.min_value, info.max_value);
                    setSingleParamEvent(event, info.id, value);
                    params->flush(plugin, &event.events, nullptr);
                    double reported = 0.0;
                    return params->get_value(plugin, info.id, &reported)
                        && std::fabs(reported - value) < 0.000001;
                }
                return false;
            };

            auto setPannerSource = [&](uint32_t source, double azimuth,
                                       double elevation, double distance) {
                char name[32] {};
                std::snprintf(name, sizeof(name), "S%u Azimuth", source);
                bool configured = setDocumentationRoutingParam(name, azimuth);
                std::snprintf(name, sizeof(name), "S%u Elevation", source);
                configured = configured
                    && setDocumentationRoutingParam(name, elevation);
                std::snprintf(name, sizeof(name), "S%u Distance", source);
                return configured
                    && setDocumentationRoutingParam(name, distance);
            };

            if (documentationLayoutPanner) {
                ok = setDocumentationRoutingParam("Layout", 5.0)
                    && setDocumentationRoutingParam("Active Sources", 4.0)
                    && setDocumentationRoutingParam("Focus", 1.18)
                    && setDocumentationRoutingParam(
                        "Distance Diffusion", 0.24)
                    && setPannerSource(1u, -72.0, 12.0, 0.78)
                    && setPannerSource(2u, -18.0, 38.0, 1.12)
                    && setPannerSource(3u, 48.0, 22.0, 0.92)
                    && setPannerSource(4u, 126.0, 55.0, 1.28);
            } else if (documentationDbapPanner) {
                // DBAP exposes raw preset values; value 3 is DODECA 12.
                ok = setDocumentationRoutingParam("Layout", 3.0)
                    && setDocumentationRoutingParam("Active Sources", 4.0)
                    && setDocumentationRoutingParam("Focus", 1.26)
                    && setDocumentationRoutingParam("Distance Rolloff", 7.5)
                    && setDocumentationRoutingParam(
                        "Distance Diffusion", 0.20)
                    && setPannerSource(1u, -146.0, -18.0, 0.74)
                    && setPannerSource(2u, -58.0, 34.0, 1.18)
                    && setPannerSource(3u, 24.0, -32.0, 0.94)
                    && setPannerSource(4u, 112.0, 46.0, 1.36);
            } else if (documentationLbapPanner) {
                // LBAP exposes layout menu indices; index 8 is ICOSAHEDRON 20.
                ok = setDocumentationRoutingParam("Layout", 8.0)
                    && setDocumentationRoutingParam("Active Sources", 4.0)
                    && setDocumentationRoutingParam("Focus", 1.34)
                    && setDocumentationRoutingParam(
                        "Distance Diffusion", 0.30)
                    && setPannerSource(1u, -124.0, -38.0, 0.82)
                    && setPannerSource(2u, -38.0, 28.0, 1.22)
                    && setPannerSource(3u, 54.0, 52.0, 0.88)
                    && setPannerSource(4u, 148.0, 8.0, 1.30);
            } else if (documentationVbapPanner) {
                // VBAP exposes layout menu indices; index 1 is CUBE 17.
                ok = setDocumentationRoutingParam("Layout", 1.0)
                    && setDocumentationRoutingParam("Active Sources", 4.0)
                    && setDocumentationRoutingParam("Focus", 1.42)
                    && setPannerSource(1u, -112.0, -24.0, 0.88)
                    && setPannerSource(2u, -34.0, 18.0, 1.18)
                    && setPannerSource(3u, 42.0, 48.0, 0.82)
                    && setPannerSource(4u, 138.0, -8.0, 1.34);
            } else if (documentationGroupMatrix) {
                ok = setDocumentationRoutingParam("Group Size", 1.0)
                    && setDocumentationRoutingParam("Shape", 3.0)
                    && setDocumentationRoutingParam("Depth", 0.82)
                    && setDocumentationRoutingParam("Spread", 0.66)
                    && setDocumentationRoutingParam("Vortex", 0.42)
                    && setDocumentationRoutingParam("Motion", 0.88)
                    && setDocumentationRoutingParam("Rate", 0.47)
                    && setDocumentationRoutingParam("Phase", 0.29)
                    && setDocumentationRoutingParam("Smoothing", 72.0);
                constexpr uint32_t offsets[] { 1u, 2u, 4u, 7u };
                constexpr double levels[] { -3.0, -8.0, -14.0, -20.0 };
                for (uint32_t source = 0u; ok && source < 8u; ++source) {
                    for (uint32_t route = 0u;
                         ok && route < std::size(offsets); ++route) {
                        const uint32_t destination =
                            (source + offsets[route]) % 8u;
                        char name[32] {};
                        std::snprintf(
                            name, sizeof(name), "S%u to D%u",
                            source + 1u, destination + 1u);
                        ok = setDocumentationRoutingParam(
                            name, levels[route]);
                    }
                }
            } else if (documentationNodeBusMixer) {
                ok = setDocumentationRoutingParam("Mix bed shape", 13.0)
                    && setDocumentationRoutingParam(
                        "Output channels", 16.0)
                    && setDocumentationRoutingParam("Node count", 3.0)
                    && setDocumentationRoutingParam("Cursor X", 0.12)
                    && setDocumentationRoutingParam("Cursor Y", -0.02)
                    && setDocumentationRoutingParam("Lock Z plane", 0.0)
                    && setDocumentationRoutingParam("Cursor Z", 0.04)
                    && setDocumentationRoutingParam("Cursor radius", 1.35);
                constexpr double formats[] { 6.0, 9.0, 13.0 };
                constexpr double channels[] { 8.0, 9.0, 16.0 };
                constexpr double busStarts[] { 1.0, 9.0, 18.0 };
                constexpr double positions[][3] {
                    { -0.72, 0.12, 0.16 },
                    { 0.08, -0.30, -0.12 },
                    { 0.70, 0.18, 0.10 },
                };
                constexpr double scales[] { 0.78, 0.88, 0.72 };
                constexpr double azimuths[] { 18.0, -28.0, 44.0 };
                constexpr double elevations[] { 12.0, -8.0, 20.0 };
                for (uint32_t node = 0u; ok && node < 3u; ++node) {
                    const char* fields[] {
                        "source format", "source channels", "bus start",
                        "X", "Y", "Z", "shape scale", "focus",
                        "azimuth rotate", "elevation rotate"
                    };
                    const double values[] {
                        formats[node], channels[node], busStarts[node],
                        positions[node][0], positions[node][1],
                        positions[node][2], scales[node], 1.12,
                        azimuths[node], elevations[node]
                    };
                    for (uint32_t field = 0u;
                         ok && field < std::size(fields); ++field) {
                        char name[64] {};
                        std::snprintf(
                            name, sizeof(name), "Node %02u %s",
                            node + 1u, fields[field]);
                        ok = setDocumentationRoutingParam(
                            name, values[field]);
                    }
                }
            } else if (documentationSubCrossover) {
                ok = setDocumentationRoutingParam("Sub Count", 4.0)
                    && setDocumentationRoutingParam("Sub Offset", 5.0)
                    && setDocumentationRoutingParam("Sub Focus", 1.85)
                    && setDocumentationRoutingParam("Cutoff", 86.0);
            } else if (documentationArrayDelay) {
                constexpr double delayMs[] {
                    45.0, 180.0, 420.0, 760.0,
                    1150.0, 1680.0, 2360.0, 3180.0
                };
                for (uint32_t lane = 0u;
                     ok && lane < std::size(delayMs); ++lane) {
                    char name[32] {};
                    std::snprintf(
                        name, sizeof(name), "Delay %u", lane + 1u);
                    ok = setDocumentationRoutingParam(name, delayMs[lane]);
                }
            } else if (documentationArrayTrim) {
                constexpr double trimDb[] {
                    -1.5, -6.0, 2.5, -9.0,
                    4.0, -12.0, -3.5, 7.0
                };
                for (uint32_t lane = 0u;
                     ok && lane < std::size(trimDb); ++lane) {
                    char name[32] {};
                    std::snprintf(
                        name, sizeof(name), "Trim %u", lane + 1u);
                    ok = setDocumentationRoutingParam(name, trimDb[lane]);
                }
                ok = ok
                    && setDocumentationRoutingParam("Invert 3", 1.0)
                    && setDocumentationRoutingParam("Mute 6", 1.0)
                    && setDocumentationRoutingParam("Invert 7", 1.0);
            }
            if (ok && (documentationLayoutPanner
                    || documentationDbapPanner
                    || documentationLbapPanner
                    || documentationVbapPanner)) {
                @try {
                    ok = [document respondsToSelector:
                        @selector(setViewPreset:)];
                    if (!ok) {
                        std::cerr << "Panner view does not expose "
                                  << "setViewPreset:\n";
                    }
                    if (ok) [document setViewPreset:2];
                    if (ok && documentationDbapPanner) {
                        ok = [document respondsToSelector:
                            @selector(setDocumentationViewAzimuth:elevation:)];
                        if (!ok) {
                            std::cerr << "DBAP view does not expose "
                                      << "setDocumentationViewAzimuth:elevation:\n";
                        }
                        if (ok) {
                            [document setDocumentationViewAzimuth:-82.0
                                elevation:26.0];
                        }
                    }
                } @catch (NSException* exception) {
                    std::cerr << "Panner 3/4-view exception: "
                              << [[exception reason] UTF8String] << "\n";
                    ok = false;
                }
            }
            if (ok) {
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];
            }
        }
        if (ok && documentationCapture) {
            auto setDocumentationSceneParam = [&] (
                    const char* name, double requestedValue) {
                const uint32_t parameterCount = params->count(plugin);
                for (uint32_t index = 0u;
                     index < parameterCount; ++index) {
                    clap_param_info_t info {};
                    if (!params->get_info(plugin, index, &info)) {
                        return false;
                    }
                    if (std::strcmp(info.name, name) != 0) continue;
                    SingleParamEventInput event {};
                    const double value = std::clamp(
                        requestedValue, info.min_value, info.max_value);
                    setSingleParamEvent(event, info.id, value);
                    params->flush(plugin, &event.events, nullptr);
                    double reported = 0.0;
                    bool matched = false;
                    for (uint32_t attempt = 0u;
                         attempt < 100u && !matched; ++attempt) {
                        matched = params->get_value(
                                plugin, info.id, &reported)
                            && std::fabs(reported - value) < 0.000001;
                        if (!matched) [NSThread sleepForTimeInterval:0.002];
                    }
                    if (!matched) {
                        std::cerr << "Documentation scene parameter " << name
                                  << " requested " << value
                                  << " but reported " << reported << "\n";
                    }
                    return matched;
                }
                std::cerr << "Documentation scene parameter not found: "
                          << name << "\n";
                return false;
            };

            if (formantMatrix) {
                failureStage = "documentation Formant Matrix live articulation";
                ok = setDocumentationSceneParam("Articulation Level", 0.22);
            } else if (documentationSpeakerDecoder) {
                failureStage = "documentation Speaker Decoder Dome 25 top view";
                ok = setDocumentationSceneParam("Layout", 5.0)
                    && setDocumentationSceneParam("Mode", 1.0)
                    && setDocumentationSceneParam("Order", 3.0)
                    && setDocumentationSceneParam("Weighting", 1.0)
                    && setDocumentationSceneParam("Width", 1.08);
                if (ok) {
                    @try {
                        ok = [document respondsToSelector:
                            @selector(setViewPreset:)];
                        if (!ok) {
                            std::cerr << "Speaker Decoder view does not expose "
                                      << "setViewPreset:\n";
                        }
                        if (ok) [document setViewPreset:0];
                    } @catch (NSException* exception) {
                        std::cerr << "Speaker Decoder top-view exception: "
                                  << [[exception reason] UTF8String] << "\n";
                        ok = false;
                    }
                }
            } else if (documentationObjectDecoder) {
                failureStage = "documentation Object Decoder Dome 25 scene";
                ok = setDocumentationSceneParam("Layout", 6.0);
            } else if (documentationAdaptiveDecoder) {
                failureStage =
                    "documentation Adaptive Decoder Icosahedron 20 scene";
                ok = setDocumentationSceneParam("Layout", 7.0);
            } else if (cartographyEncoder) {
                failureStage =
                    "documentation Cartography Ridge 3/4 scene";
                ok = setDocumentationSceneParam("Layout", 1.0)
                    && setDocumentationSceneParam("Selected Site", 6.0);
                if (ok) {
                    @try {
                        ok = [document respondsToSelector:
                            @selector(setViewPreset:)];
                        if (!ok) {
                            std::cerr << "Cartography view does not expose "
                                      << "setViewPreset:\n";
                        }
                        if (ok) [document setViewPreset:2];
                    } @catch (NSException* exception) {
                        std::cerr << "Cartography Ridge 3/4 exception: "
                                  << [[exception reason] UTF8String] << "\n";
                        ok = false;
                    }
                }
            } else if (documentationEffectDelay) {
                failureStage = "documentation Ambi Effect Delay scene";
                ok = setDocumentationSceneParam("Ambisonic order", 3.0)
                    && setDocumentationSceneParam("Auditory body", 5.0)
                    && setDocumentationSceneParam("Topology", 0.0)
                    && setDocumentationSceneParam("Delay time", 1026.25)
                    && setDocumentationSceneParam("Feedback", 0.58)
                    && setDocumentationSceneParam("Feedback tone", 0.50)
                    && setDocumentationSceneParam("Pickup spread", 0.27)
                    && setDocumentationSceneParam("Pickup deviation", 0.45)
                    && setDocumentationSceneParam("Topology amount", 0.88)
                    && setDocumentationSceneParam("Roaming rate", 0.10)
                    && setDocumentationSceneParam("Mix", 1.0)
                    && setDocumentationSceneParam("Output gain", 5.2)
                    && setDocumentationSceneParam(
                        "Directional mask amount", 1.0)
                    && setDocumentationSceneParam(
                        "Directional mask azimuth", -3.0)
                    && setDocumentationSceneParam(
                        "Directional mask elevation", -18.0)
                    && setDocumentationSceneParam(
                        "Directional mask width", 0.62)
                    && setDocumentationSceneParam(
                        "Directional mask curve", 0.63)
                    && setDocumentationSceneParam(
                        "Directional mask dry attenuation", 0.0)
                    && setDocumentationSceneParam(
                        "Pickup 01 time trim", 0.18)
                    && setDocumentationSceneParam(
                        "Pickup 01 feedback trim", 0.10);
            } else if (documentationEffectPitch) {
                failureStage = "documentation Ambi Effect Pitch scene";
                ok = setDocumentationSceneParam("Ambisonic order", 3.0)
                    && setDocumentationSceneParam("Auditory body", 4.0)
                    && setDocumentationSceneParam("Topology", 3.0)
                    && setDocumentationSceneParam("Pitch", 7.0)
                    && setDocumentationSceneParam("Grain window", 124.0)
                    && setDocumentationSceneParam("Pitch glide", 460.0)
                    && setDocumentationSceneParam("Pickup spread", 0.74)
                    && setDocumentationSceneParam("Pickup deviation", 0.48)
                    && setDocumentationSceneParam("Topology amount", 0.82)
                    && setDocumentationSceneParam("Roaming rate", 0.31)
                    && setDocumentationSceneParam("Mix", 0.68)
                    && setDocumentationSceneParam("Output gain", 2.4)
                    && setDocumentationSceneParam(
                        "Directional mask amount", 0.82)
                    && setDocumentationSceneParam(
                        "Directional mask azimuth", -52.0)
                    && setDocumentationSceneParam(
                        "Directional mask elevation", 24.0)
                    && setDocumentationSceneParam(
                        "Directional mask width", 0.46)
                    && setDocumentationSceneParam(
                        "Directional mask curve", 0.71)
                    && setDocumentationSceneParam(
                        "Directional mask dry attenuation", 0.0)
                    && setDocumentationSceneParam("Pickup 01 pitch trim", 0.72)
                    && setDocumentationSceneParam("Pickup 01 window trim", -0.38);
            } else if (documentationEffectGain) {
                failureStage = "documentation Ambi Effect Gain scene";
                ok = setDocumentationSceneParam("Ambisonic order", 3.0)
                    && setDocumentationSceneParam("Auditory body", 4.0)
                    && setDocumentationSceneParam("Topology", 0.0)
                    && setDocumentationSceneParam("Gain", -7.5)
                    && setDocumentationSceneParam("Pickup spread", 0.78)
                    && setDocumentationSceneParam("Pickup deviation", 0.56)
                    && setDocumentationSceneParam("Topology amount", 0.76)
                    && setDocumentationSceneParam("Roaming rate", 0.17)
                    && setDocumentationSceneParam("Mix", 0.86)
                    && setDocumentationSceneParam("Output gain", -1.5)
                    && setDocumentationSceneParam(
                        "Directional mask amount", 0.91)
                    && setDocumentationSceneParam(
                        "Directional mask azimuth", 68.0)
                    && setDocumentationSceneParam(
                        "Directional mask elevation", -22.0)
                    && setDocumentationSceneParam(
                        "Directional mask width", 0.58)
                    && setDocumentationSceneParam(
                        "Directional mask curve", 0.57)
                    && setDocumentationSceneParam(
                        "Directional mask dry attenuation", 0.0)
                    && setDocumentationSceneParam("Pickup 01 gain trim", 0.58);
            } else if (documentationResonancePrint) {
                failureStage = "documentation Resonance Print scene";
                ok = setDocumentationSceneParam("Ambisonic order", 3.0)
                    && setDocumentationSceneParam("Auditory body", 4.0)
                    && setDocumentationSceneParam("Topology", 2.0)
                    && setDocumentationSceneParam("Capture duration", 0.25)
                    && setDocumentationSceneParam("Peak sensitivity", 0.72)
                    && setDocumentationSceneParam("Modes per pickup", 9.0)
                    && setDocumentationSceneParam("Transpose", -2.0)
                    && setDocumentationSceneParam("Harmonic pull", 0.35)
                    && setDocumentationSceneParam("Harmonic stretch", 0.12)
                    && setDocumentationSceneParam("Decay", 2.4)
                    && setDocumentationSceneParam("Decay tilt", -0.18)
                    && setDocumentationSceneParam("Excitation drive", 0.52)
                    && setDocumentationSceneParam("Pickup spread", 0.62)
                    && setDocumentationSceneParam("Pickup deviation", 0.37)
                    && setDocumentationSceneParam("Topology amount", 0.72)
                    && setDocumentationSceneParam("Roaming rate", 0.16)
                    && setDocumentationSceneParam("Mix", 0.64)
                    && setDocumentationSceneParam("Output gain", -1.0)
                    && setDocumentationSceneParam(
                        "Directional mask amount", 0.76)
                    && setDocumentationSceneParam(
                        "Directional mask azimuth", -36.0)
                    && setDocumentationSceneParam(
                        "Directional mask elevation", 27.0)
                    && setDocumentationSceneParam(
                        "Directional mask width", 0.44)
                    && setDocumentationSceneParam(
                        "Directional mask curve", 0.62)
                    && setDocumentationSceneParam(
                        "Directional mask dry attenuation", -0.35);
            } else if (partialTrace) {
                failureStage = "documentation Partial Trace scene";
                ok = setDocumentationSceneParam("Partial count", 12.0)
                    && setDocumentationSceneParam("Sensitivity", 0.48)
                    && setDocumentationSceneParam(
                        "Minimum frequency", 80.0)
                    && setDocumentationSceneParam(
                        "Maximum frequency", 2400.0)
                    && setDocumentationSceneParam("Tracking time", 90.0)
                    && setDocumentationSceneParam("Trace release", 680.0)
                    && setDocumentationSceneParam(
                        "Directional mask amount", 0.84)
                    && setDocumentationSceneParam(
                        "Directional mask azimuth", -68.0)
                    && setDocumentationSceneParam(
                        "Directional mask elevation", 34.0)
                    && setDocumentationSceneParam(
                        "Directional mask width", 0.27)
                    && setDocumentationSceneParam(
                        "Directional mask curve", 0.72)
                    && setDocumentationSceneParam(
                        "Directional mask dry attenuation", -0.42);
            } else if (responseTrace) {
                failureStage = "documentation Response Trace scene";
                ok = setDocumentationSceneParam("Capture duration", 0.12)
                    && setDocumentationSceneParam("Response gain", -4.0)
                    && setDocumentationSceneParam("Response tone", 0.64)
                    && setDocumentationSceneParam(
                        "Directional mask amount", 0.72)
                    && setDocumentationSceneParam(
                        "Directional mask azimuth", 106.0)
                    && setDocumentationSceneParam(
                        "Directional mask elevation", -28.0)
                    && setDocumentationSceneParam(
                        "Directional mask width", 0.58)
                    && setDocumentationSceneParam(
                        "Directional mask curve", 0.44)
                    && setDocumentationSceneParam(
                        "Directional mask dry attenuation", -0.68);
            } else if (documentationDisplacement) {
                failureStage = "documentation Displacement score";
                ok = [document respondsToSelector:
                    @selector(loadDocumentationScore)];
                if (ok) [document loadDocumentationScore];
                ok = ok
                    && setDocumentationSceneParam("Clock", 2.0)
                    && setDocumentationSceneParam("Playback", 0.0)
                    && setDocumentationSceneParam("Position", 0.50)
                    && setDocumentationSceneParam("Amount", 1.0)
                    && setDocumentationSceneParam("Azimuth Scale", 1.35)
                    && setDocumentationSceneParam("Elevation Scale", 1.35)
                    && setDocumentationSceneParam("Radius Scale", 1.45)
                    && setDocumentationSceneParam("Ambisonic Order", 4.0)
                    && setDocumentationSceneParam("Listener Body", 5.0);
            } else if (documentationRotate) {
                failureStage = "documentation Ambi Transform Rot AED";
                ok = setDocumentationSceneParam("Ambisonic order", 7.0)
                    && setDocumentationSceneParam("Yaw / azimuth", 72.0)
                    && setDocumentationSceneParam("Pitch / elevation", -26.0)
                    && setDocumentationSceneParam("Roll", 38.0)
                    && setDocumentationSceneParam("Dispersion spread", 0.42)
                    && setDocumentationSceneParam("Dispersion tilt", -0.31)
                    && setDocumentationSceneParam("Dispersion twist", 0.58)
                    && setDocumentationSceneParam("Order width", 1.18);
            } else if (documentationOrderBand) {
                failureStage = "documentation Order Band MaxRE";
                ok = setDocumentationSceneParam("Active order", 7.0)
                    && setDocumentationSceneParam("Weighting preset", 1.0)
                    && setDocumentationSceneParam("Weighting amount", 1.0);
            } else if (documentationAmbiGroupMatrix) {
                failureStage = "documentation Ambi Group Matrix motion";
                ok = setDocumentationSceneParam("Depth", 0.94)
                    && setDocumentationSceneParam("Spread", 0.82)
                    && setDocumentationSceneParam("Vortex", 0.68)
                    && setDocumentationSceneParam("Motion", 0.96)
                    && setDocumentationSceneParam("Shape", 4.0)
                    && setDocumentationSceneParam("Mode", 0.0)
                    && setDocumentationSceneParam("Rate", 0.72)
                    && setDocumentationSceneParam("Phase", 0.18)
                    && setDocumentationSceneParam("Smoothing", 48.0);
                constexpr double matrixLevels[4][4] {
                    { 0.0, -5.0, -11.0, -16.0 },
                    { -9.0, 0.0, -6.0, -13.0 },
                    { -14.0, -7.0, 0.0, -4.0 },
                    { -6.0, -15.0, -8.0, 0.0 },
                };
                for (uint32_t source = 0u; ok && source < 4u; ++source) {
                    for (uint32_t destination = 0u;
                         ok && destination < 4u; ++destination) {
                        char name[32] {};
                        std::snprintf(name, sizeof(name), "S%u to D%u",
                            source + 1u, destination + 1u);
                        ok = setDocumentationSceneParam(
                            name, matrixLevels[source][destination]);
                    }
                }
            }
            if (ok) {
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];
            }
        }
        if (ok && documentationOutputAutogain) {
            failureStage = "documentation output autogain signal";
            const auto* audioPorts =
                static_cast<const clap_plugin_audio_ports_t*>(
                    plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
            clap_audio_port_info_t inputInfo {};
            clap_audio_port_info_t outputInfo {};
            ok = audioPorts
                && audioPorts->count(plugin, true) == 1u
                && audioPorts->count(plugin, false) == 1u
                && audioPorts->get(plugin, 0u, true, &inputInfo)
                && audioPorts->get(plugin, 0u, false, &outputInfo)
                && inputInfo.channel_count == 128u
                && (outputInfo.channel_count == 2u
                    || outputInfo.channel_count == 4u);

            constexpr uint32_t audioFrames = 128u;
            constexpr uint32_t inputChannels = 128u;
            constexpr uint32_t outputChannels = 4u;
            constexpr uint32_t activeChannels = 8u;
            constexpr uint32_t audioBlocks = 24u;
            std::array<std::array<float, audioFrames>, inputChannels>
                audioInput {};
            std::array<std::array<float, audioFrames>, outputChannels>
                audioOutput {};
            std::array<float*, inputChannels> inputPointers {};
            std::array<float*, outputChannels> outputPointers {};
            for (uint32_t channel = 0u; channel < inputChannels; ++channel) {
                inputPointers[channel] = audioInput[channel].data();
            }
            for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
                outputPointers[channel] = audioOutput[channel].data();
            }
            clap_audio_buffer_t inputBuffer {};
            inputBuffer.data32 = inputPointers.data();
            inputBuffer.channel_count = inputInfo.channel_count;
            clap_audio_buffer_t outputBuffer {};
            outputBuffer.data32 = outputPointers.data();
            outputBuffer.channel_count = outputInfo.channel_count;
            clap_process_t processBlock {};
            processBlock.frames_count = audioFrames;
            processBlock.audio_inputs = &inputBuffer;
            processBlock.audio_inputs_count = 1u;
            processBlock.audio_outputs = &outputBuffer;
            processBlock.audio_outputs_count = 1u;

            bool activated = false;
            bool processing = false;
            if (ok) {
                ok = gui->show(plugin);
                activated = ok
                    && plugin->activate(plugin, 48000.0, 1u, audioFrames);
                processing = activated && plugin->start_processing(plugin);
                ok = ok && activated && processing;
            }
            uint64_t sampleCursor = 0u;
            for (uint32_t block = 0u; ok && block < audioBlocks; ++block) {
                for (uint32_t frame = 0u; frame < audioFrames; ++frame) {
                    const double time = static_cast<double>(sampleCursor++)
                        / 48000.0;
                    for (uint32_t channel = 0u;
                         channel < activeChannels; ++channel) {
                        const double frequency = 173.0
                            + 47.0 * static_cast<double>(channel);
                        const float level = 0.18f
                            - 0.009f * static_cast<float>(channel);
                        audioInput[channel][frame] = level
                            * static_cast<float>(std::sin(
                                2.0 * s3g::kPi * frequency * time
                                + 0.31 * static_cast<double>(channel)));
                    }
                }
                for (auto& channel : audioOutput) channel.fill(0.0f);
                processBlock.steady_time =
                    static_cast<int64_t>(block) * audioFrames;
                ok = plugin->process(plugin, &processBlock)
                    != CLAP_PROCESS_ERROR;
            }
            if (processing) plugin->stop_processing(plugin);
            if (activated) plugin->deactivate(plugin);
            if (ok) [document setNeedsDisplay:YES];
        }
        const bool documentationMidiInstrument = documentationCapture
            && (formantMatrix
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.low-frequency-synth") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.processor-stack") == 0);
        const bool documentationLiveSignal = documentationCapture
            && (documentationObjectDecoder
                || documentationAdaptiveDecoder
                || documentationStereoDecoder
                || documentationHeadDecoder
                || documentationEffectDelay
                || documentationEffectPitch
                || documentationEffectGain
                || documentationResonancePrint
                || partialTrace
                || responseTrace
                || faultProcessor
                || errantProcessor
                || formantMatrix
                || documentationMidiInstrument
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.macro-shred-8ch") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.processor-conduit") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.processor-fissure") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.drum-echo") == 0
                || feedbackShift
                || documentationAmbiGroupMatrix);
        if (ok && documentationLiveSignal) {
            failureStage = "documentation live signal";
            const auto* audioPorts =
                static_cast<const clap_plugin_audio_ports_t*>(
                    plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
            clap_audio_port_info_t inputInfo {};
            clap_audio_port_info_t outputInfo {};
            const uint32_t inputPortCount = audioPorts
                ? audioPorts->count(plugin, true) : 0u;
            const uint32_t outputPortCount = audioPorts
                ? audioPorts->count(plugin, false) : 0u;
            const bool hasInput = inputPortCount == 1u;
            ok = audioPorts
                && inputPortCount <= 1u
                && outputPortCount == 1u
                && (!hasInput
                    || audioPorts->get(plugin, 0u, true, &inputInfo))
                && audioPorts->get(plugin, 0u, false, &outputInfo)
                && (!hasInput || inputInfo.channel_count <= 64u)
                && outputInfo.channel_count <= 64u;

            constexpr uint32_t audioFrames = 128u;
            constexpr uint32_t maximumChannels = 64u;
            std::array<std::array<float, audioFrames>, maximumChannels>
                audioInput {};
            std::array<std::array<float, audioFrames>, maximumChannels>
                audioOutput {};
            std::array<float*, maximumChannels> inputPointers {};
            std::array<float*, maximumChannels> outputPointers {};
            for (uint32_t channel = 0u;
                 channel < maximumChannels; ++channel) {
                inputPointers[channel] = audioInput[channel].data();
                outputPointers[channel] = audioOutput[channel].data();
            }
            clap_audio_buffer_t inputBuffer {};
            inputBuffer.data32 = inputPointers.data();
            inputBuffer.channel_count = hasInput
                ? inputInfo.channel_count : 0u;
            clap_audio_buffer_t outputBuffer {};
            outputBuffer.data32 = outputPointers.data();
            outputBuffer.channel_count = outputInfo.channel_count;
            clap_process_t processBlock {};
            processBlock.frames_count = audioFrames;
            processBlock.audio_inputs = hasInput ? &inputBuffer : nullptr;
            processBlock.audio_inputs_count = hasInput ? 1u : 0u;
            processBlock.audio_outputs = &outputBuffer;
            processBlock.audio_outputs_count = 1u;

            const auto sourceA = s3g::acnSn3dBasis7(
                s3g::directionFromAed(-54.0f, 22.0f));
            const auto sourceB = s3g::acnSn3dBasis7(
                s3g::directionFromAed(76.0f, -18.0f));
            const auto sourceC = s3g::acnSn3dBasis7(
                s3g::directionFromAed(146.0f, 38.0f));
            const bool documentationHistorySignal = formantMatrix
                || feedbackShift
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.macro-shred-8ch") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.processor-conduit") == 0
                || std::strcmp(pluginId,
                    "org.s3g.s3g-dsp.processor-fissure") == 0;
            const uint32_t audioBlocks = documentationHistorySignal
                ? 1200u : (documentationAmbiGroupMatrix
                ? 180u : (responseTrace ? 320u
                    : (documentationResonancePrint ? 180u
                        : (partialTrace ? 120u
                        : (documentationMidiInstrument ? 320u
                            : (errantProcessor ? 600u
                            : (faultProcessor ? 36u : 28u)))))));
            bool activated = false;
            bool processing = false;
            if (ok) {
                ok = gui->show(plugin);
                activated = ok
                    && plugin->activate(plugin, 48000.0, 1u, audioFrames);
                processing = activated && plugin->start_processing(plugin);
                ok = ok && activated && processing;
            }
            clap_id captureEffectParam = CLAP_INVALID_ID;
            clap_id applyEffectParam = CLAP_INVALID_ID;
            clap_id documentationTriggerParam = CLAP_INVALID_ID;
            if (ok && (responseTrace || documentationResonancePrint)) {
                const char* captureName = responseTrace
                    ? "Capture response" : "Capture print";
                const char* applyName = responseTrace
                    ? "Apply response" : "Apply print";
                const uint32_t parameterCount = params->count(plugin);
                for (uint32_t index = 0u;
                     index < parameterCount; ++index) {
                    clap_param_info_t info {};
                    if (!params->get_info(plugin, index, &info)) {
                        ok = false;
                        break;
                    }
                    if (std::strcmp(info.name, captureName) == 0) {
                        captureEffectParam = info.id;
                    } else if (std::strcmp(info.name, applyName) == 0) {
                        applyEffectParam = info.id;
                    }
                }
                ok = ok && params->flush
                    && captureEffectParam != CLAP_INVALID_ID
                    && applyEffectParam != CLAP_INVALID_ID;
                if (ok) {
                    SingleParamEventInput event {};
                    setSingleParamEvent(event, captureEffectParam, 1.0);
                    params->flush(plugin, &event.events, nullptr);
                    setSingleParamEvent(event, captureEffectParam, 0.0);
                    params->flush(plugin, &event.events, nullptr);
                }
            }
            if (ok && errantProcessor) {
                const uint32_t parameterCount = params->count(plugin);
                for (uint32_t index = 0u;
                     index < parameterCount; ++index) {
                    clap_param_info_t info {};
                    if (!params->get_info(plugin, index, &info)) {
                        ok = false;
                        break;
                    }
                    if (std::strcmp(info.name, "Trigger") == 0) {
                        documentationTriggerParam = info.id;
                        break;
                    }
                }
                ok = ok && documentationTriggerParam != CLAP_INVALID_ID;
            }
            SingleParamEventInput documentationTrigger {};
            if (documentationTriggerParam != CLAP_INVALID_ID) {
                setSingleParamEvent(
                    documentationTrigger, documentationTriggerParam, 1.0);
            }
            SingleNoteEventInput documentationNote {};
            if (documentationMidiInstrument) {
                setSingleNoteOnEvent(documentationNote,
                    std::strcmp(pluginId,
                        "org.s3g.s3g-dsp.low-frequency-synth") == 0
                        ? 36 : 48);
            }
            uint64_t sampleCursor = 0u;
            for (uint32_t block = 0u;
                 ok && block < audioBlocks; ++block) {
                if ((responseTrace && block == 250u)
                    || (documentationResonancePrint && block == 120u)) {
                    SingleParamEventInput event {};
                    setSingleParamEvent(event, applyEffectParam, 1.0);
                    params->flush(plugin, &event.events, nullptr);
                }
                if (hasInput) {
                    for (uint32_t frame = 0u;
                         frame < audioFrames; ++frame) {
                        const double time =
                            static_cast<double>(sampleCursor++) / 48000.0;
                        const float inputScale = documentationResonancePrint
                            ? 0.45f : 1.0f;
                        const float historyMotion = documentationHistorySignal
                            ? 0.18f + 0.82f * std::fabs(static_cast<float>(
                                std::sin(2.0 * s3g::kPi * 0.63 * time)))
                            : 1.0f;
                        const float a = inputScale * historyMotion * 0.30f
                            * static_cast<float>(
                            std::sin(2.0 * s3g::kPi * 197.0 * time));
                        const float b = inputScale * historyMotion * 0.21f
                            * static_cast<float>(
                            std::sin(2.0 * s3g::kPi * 431.0 * time + 0.61));
                        const float c = inputScale * historyMotion * 0.14f
                            * static_cast<float>(
                            std::sin(2.0 * s3g::kPi * 733.0 * time + 1.37));
                        for (uint32_t channel = 0u;
                             channel < inputInfo.channel_count; ++channel) {
                            const float field = channel < sourceA.size()
                                ? sourceA[channel] * a + sourceB[channel] * b
                                    + sourceC[channel] * c
                                : 0.0f;
                            const float busAccent =
                                1.0f - 0.08f * static_cast<float>(
                                    (channel / 16u) % 4u);
                            audioInput[channel][frame] = field * busAccent;
                        }
                    }
                }
                for (auto& channel : audioOutput) channel.fill(0.0f);
                processBlock.steady_time =
                    static_cast<int64_t>(block) * audioFrames;
                processBlock.in_events = errantProcessor
                        && (block == 0u || block == 300u)
                    ? &documentationTrigger.events
                    : (documentationMidiInstrument && block == 0u
                        ? &documentationNote.events : nullptr);
                ok = plugin->process(plugin, &processBlock)
                    != CLAP_PROCESS_ERROR;
                if (ok && documentationHistorySignal
                    && (block % 7u) == 6u) {
                    if ([document respondsToSelector:
                            @selector(captureDocumentationHistorySample)]) {
                        [document captureDocumentationHistorySample];
                    } else if ([document respondsToSelector:
                            @selector(refresh:)]) {
                        [document refresh:nil];
                    }
                }
                if (ok && documentationHistorySignal
                    && (block % 64u) == 63u) {
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                }
            }
            if (processing) plugin->stop_processing(plugin);
            if (activated) plugin->deactivate(plugin);
            if (ok) [document setNeedsDisplay:YES];
        }
        if (ok && documentationCapture && feedbackShift) {
            // The initial page-navigation pass validates the controls before
            // audio starts. Re-capture the auxiliary pages after the live
            // documentation run so their activity and governor meters retain
            // the same audible state as the main PATCH-page image.
            failureStage = "active Feedback Shift documentation pages";
            const char* captureDirectory = std::getenv(
                "S3G_GUI_SMOKE_PDF_DIR");
            const auto clickFeedbackPage = [&](NSPoint point) {
                [document mouseDown:mouseEvent(
                    NSEventTypeLeftMouseDown, point)];
                [document mouseUp:mouseEvent(
                    NSEventTypeLeftMouseUp, point)];
            };
            @try {
                for (uint32_t page = 1u; ok && page < 4u; ++page) {
                    clickFeedbackPage(NSMakePoint(71.0 + page * 92.0, 54.0));
                    ok = [[document valueForKey:@"page"] unsignedIntValue]
                        == page;
                    if (ok && [document respondsToSelector:
                            @selector(refresh:)]) {
                        [document refresh:nil];
                    }
                    if (ok) {
                        [document setNeedsDisplay:YES];
                        [document displayIfNeeded];
                        NSData* pageRender = [document dataWithPDFInsideRect:
                            [document bounds]];
                        ok = pageRender && [pageRender length] > 0u;
                        if (ok && captureDirectory && captureDirectory[0]) {
                            NSString* directory = [NSString
                                stringWithUTF8String:captureDirectory];
                            [[NSFileManager defaultManager]
                                createDirectoryAtPath:directory
                                withIntermediateDirectories:YES
                                attributes:nil error:nil];
                            NSString* pageName = [[NSString stringWithFormat:
                                @"%s.page%u", pluginId, page + 1u]
                                stringByAppendingPathExtension:@"pdf"];
                            ok = [pageRender writeToFile:[directory
                                    stringByAppendingPathComponent:pageName]
                                atomically:YES];
                        }
                    }
                }
                clickFeedbackPage(NSMakePoint(71.0, 54.0));
                ok = ok && [[document valueForKey:@"page"] unsignedIntValue]
                    == 0u;
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok && documentationCapture && analyzer) {
            failureStage = "documentation analyzer signal";
            constexpr uint32_t audioFrames = 128u;
            constexpr uint32_t audioChannels = 64u;
            constexpr uint32_t audioBlocks = 96u;
            std::array<std::array<float, audioFrames>, audioChannels>
                audioInput {};
            std::array<std::array<float, audioFrames>, audioChannels>
                audioOutput {};
            std::array<float*, audioChannels> inputPointers {};
            std::array<float*, audioChannels> outputPointers {};
            for (uint32_t channel = 0u; channel < audioChannels; ++channel) {
                inputPointers[channel] = audioInput[channel].data();
                outputPointers[channel] = audioOutput[channel].data();
            }
            clap_audio_buffer_t inputBuffer {};
            inputBuffer.data32 = inputPointers.data();
            inputBuffer.channel_count = audioChannels;
            clap_audio_buffer_t outputBuffer {};
            outputBuffer.data32 = outputPointers.data();
            outputBuffer.channel_count = audioChannels;
            clap_process_t processBlock {};
            processBlock.frames_count = audioFrames;
            processBlock.audio_inputs = &inputBuffer;
            processBlock.audio_inputs_count = 1u;
            processBlock.audio_outputs = &outputBuffer;
            processBlock.audio_outputs_count = 1u;

            const auto sourceA = s3g::acnSn3dBasis7(
                s3g::directionFromAed(-68.0f, 24.0f));
            const auto sourceB = s3g::acnSn3dBasis7(
                s3g::directionFromAed(38.0f, -16.0f));
            const auto sourceC = s3g::acnSn3dBasis7(
                s3g::directionFromAed(142.0f, 41.0f));
            ok = gui->show(plugin)
                && plugin->activate(plugin, 48000.0, 1u, audioFrames)
                && plugin->start_processing(plugin);
            uint64_t sampleCursor = 0u;
            for (uint32_t block = 0u; ok && block < audioBlocks; ++block) {
                for (uint32_t frame = 0u; frame < audioFrames; ++frame) {
                    const double t = static_cast<double>(sampleCursor++)
                        / 48000.0;
                    const float a = 0.30f * static_cast<float>(
                        std::sin(2.0 * s3g::kPi * 233.0 * t));
                    const float b = 0.20f * static_cast<float>(
                        std::sin(2.0 * s3g::kPi * 521.0 * t + 0.37));
                    const float c = 0.13f * static_cast<float>(
                        std::sin(2.0 * s3g::kPi * 809.0 * t + 1.11));
                    for (uint32_t channel = 0u;
                         channel < audioChannels; ++channel) {
                        const float channelTrim = 1.0f
                            - 0.22f * static_cast<float>(channel)
                                / static_cast<float>(audioChannels - 1u);
                        audioInput[channel][frame] = channelTrim
                            * (sourceA[channel] * a
                                + sourceB[channel] * b
                                + sourceC[channel] * c);
                    }
                }
                for (auto& channel : audioOutput) channel.fill(0.0f);
                ok = plugin->process(plugin, &processBlock)
                    != CLAP_PROCESS_ERROR;
                if (ok && (block % 4u) == 3u) {
                    [document setNeedsDisplay:YES];
                    [document displayIfNeeded];
                }
            }
            if (ok) {
                [document setNeedsDisplay:YES];
                [document displayIfNeeded];
            }
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
        }
        if (ok && documentationBreakbeatSlicer) {
            failureStage = "documentation Slicer Break Edit page";
            @try {
                [document setDocumentationPage:1u];
                NSData* breakEdit = [document dataWithPDFInsideRect:
                    [document bounds]];
                ok = breakEdit && [breakEdit length] > 0u;
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                if (ok && captureDirectory && captureDirectory[0]) {
                    NSString* directory = [NSString
                        stringWithUTF8String:captureDirectory];
                    [[NSFileManager defaultManager]
                        createDirectoryAtPath:directory
                        withIntermediateDirectories:YES
                        attributes:nil error:nil];
                    NSString* fileName = [[NSString stringWithFormat:
                        @"%s.break-edit", pluginId]
                        stringByAppendingPathExtension:@"pdf"];
                    ok = [breakEdit writeToFile:
                        [directory stringByAppendingPathComponent:fileName]
                        atomically:YES];
                }
                if (ok) {
                    failureStage = "documentation Slicer Mixer page";
                    [document setDocumentationPage:2u];
                    NSData* mixer = [document dataWithPDFInsideRect:
                        [document bounds]];
                    ok = mixer && [mixer length] > 0u;
                    if (ok && captureDirectory && captureDirectory[0]) {
                        NSString* directory = [NSString
                            stringWithUTF8String:captureDirectory];
                        [[NSFileManager defaultManager]
                            createDirectoryAtPath:directory
                            withIntermediateDirectories:YES
                            attributes:nil error:nil];
                        NSString* fileName = [[NSString stringWithFormat:
                            @"%s.mixer", pluginId]
                            stringByAppendingPathExtension:@"pdf"];
                        ok = [mixer writeToFile:
                            [directory stringByAppendingPathComponent:fileName]
                            atomically:YES];
                    }
                }
                [document setDocumentationPage:0u];
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok && documentationCapture
            && std::strcmp(pluginId,
                "org.s3g.s3g-dsp.processor-stack") == 0) {
            failureStage = "documentation Processor Stack Score page";
            @try {
                ok = [document respondsToSelector:
                    @selector(setDocumentationPage:)];
                if (ok) {
                    [document setDocumentationPage:3u];
                    ok = [document respondsToSelector:
                        @selector(loadDocumentationScore)];
                    if (ok) [document loadDocumentationScore];
                    NSTimer* timer = [document valueForKey:@"timer"];
                    ok = timer
                        && std::abs(timer.timeInterval - 1.0 / 60.0)
                            < 1.0e-9
                        && timer.tolerance <= 1.0 / 240.0 + 1.0e-9;
                }
            } @catch (NSException*) {
                ok = false;
            }
        }
        if (ok) failureStage = "render";
        if (ok) {
            NSData* rendered = [document dataWithPDFInsideRect:[document bounds]];
            ok = rendered && [rendered length] > 0u;
            if (ok) {
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                if (captureDirectory && captureDirectory[0]) {
                    NSString* directory = [NSString
                        stringWithUTF8String:captureDirectory];
                    [[NSFileManager defaultManager]
                        createDirectoryAtPath:directory
                        withIntermediateDirectories:YES
                        attributes:nil
                        error:nil];
                    NSString* fileName = [[NSString
                        stringWithUTF8String:pluginId]
                        stringByAppendingPathExtension:@"pdf"];
                    ok = [rendered writeToFile:
                        [directory stringByAppendingPathComponent:fileName]
                        atomically:YES];
                }
            }
        }
        if (ok && responsive
            && (testWidth < nativeWidth || testHeight < nativeHeight)) {
            [[scroll contentView] scrollToPoint:NSMakePoint(
                testWidth < nativeWidth ? 120.0 : 0.0,
                testHeight < nativeHeight ? 70.0 : 0.0)];
            [scroll reflectScrolledClipView:[scroll contentView]];
            const NSPoint origin = [[scroll contentView] bounds].origin;
            ok = (testWidth < nativeWidth
                    ? origin.x > 100.0 : origin.x < 1.0)
                && (testHeight < nativeHeight
                    ? origin.y > 50.0 : origin.y < 1.0);
        }
        if (ok) failureStage = "show and hide";
        ok = ok && gui->show(plugin) && gui->hide(plugin);
        if (ok && parameterSurfacePanel) {
            ok = ![parameterSurfacePanel isVisible];
        }

        gui->destroy(plugin);
        [parent release];
        plugin->destroy(plugin);

        // Also exercise a host tearing down the plug-in while its GUI still
        // exists. Every family member owns the viewport through plug-in
        // destruction as a defensive fallback.
        if (ok) failureStage = "destruction fallback";
        const clap_plugin_t* teardownPlugin = factory
            ? factory->create_plugin(factory, &host, pluginId)
            : nullptr;
        if (teardownPlugin && teardownPlugin->init(teardownPlugin)) {
            const auto* teardownGui = static_cast<const clap_plugin_gui_t*>(
                teardownPlugin->get_extension(teardownPlugin, CLAP_EXT_GUI));
            NSView* teardownParent = [[NSView alloc] initWithFrame:NSMakeRect(
                0.0, 0.0, testWidth, testHeight)];
            clap_window_t teardownWindow {};
            teardownWindow.api = CLAP_WINDOW_API_COCOA;
            teardownWindow.cocoa = teardownParent;
            ok = ok && teardownGui
                && teardownGui->create(teardownPlugin, CLAP_WINDOW_API_COCOA, false)
                && (!(responsive || dynamic)
                    || teardownGui->set_size(teardownPlugin, testWidth, testHeight))
                && teardownGui->set_parent(teardownPlugin, &teardownWindow);
            if (ok && (responsive || dynamic)) {
                ok = teardownGui->show(teardownPlugin);
            }
            if (responsive || dynamic) {
                teardownPlugin->destroy(teardownPlugin);
            } else {
                // Fixed legacy family members currently rely on the host's
                // conventional gui.destroy() before plug-in destruction.
                teardownGui->destroy(teardownPlugin);
                teardownPlugin->destroy(teardownPlugin);
            }
            ok = ok && [[teardownParent subviews] count] == 0u;
            [teardownParent release];
        } else {
            ok = false;
        }
        entry->deinit();
        // Keep the bundle loaded: Objective-C classes remain registered for the
        // process lifetime, which mirrors production plug-in hosts.

        if (!ok) {
            std::cerr << (responsive
                    ? "Responsive" : (dynamic ? "Dynamic" : "Fixed"))
                << " GUI smoke failed for " << pluginId
                << " at " << failureStage << "\n";
            return 1;
        }
        std::cout << (responsive
                ? "Responsive" : (dynamic ? "Dynamic" : "Fixed"))
            << " GUI smoke passed for " << pluginId << "\n";
    }
    return 0;
}
