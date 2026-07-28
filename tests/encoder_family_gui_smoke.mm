#import <Cocoa/Cocoa.h>

#include <clap/clap.h>
#include <clap/ext/gui.h>
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
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <vector>

namespace {

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

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

        clap_host_t host {};
        host.clap_version = CLAP_VERSION_INIT;
        host.name = "s3g family GUI smoke";
        host.vendor = "s3g";
        host.url = "https://github.com/s3g/s3g-dsp";
        host.version = "1";
        host.get_extension = hostGetExtension;
        host.request_restart = hostRequest;
        host.request_process = hostRequest;
        host.request_callback = hostRequest;

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

        if (ok) failureStage = "GUI API";
        const auto* gui = static_cast<const clap_plugin_gui_t*>(
            plugin->get_extension(plugin, CLAP_EXT_GUI));
        ok = ok && gui
            && gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false);

        uint32_t width = 0u;
        uint32_t height = 0u;
        if (ok) failureStage = "resize contract";
        if (responsive || dynamic) {
            const uint32_t expectedMinimumWidth = responsiveWide
                ? nativeWidth
                : std::min(dynamic ? 720u : 480u, nativeWidth);
            const uint32_t expectedMinimumHeight = std::min(
                dynamic ? 430u : 360u, nativeHeight);
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

        const uint32_t testWidth = responsiveWide
            ? nativeWidth
            : ((responsive || dynamic)
                ? std::min(720u, nativeWidth) : nativeWidth);
        const uint32_t testHeight =
            (responsive || dynamic) ? std::min(540u, nativeHeight) : nativeHeight;
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
        const bool faultProcessor = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.fault") == 0;
        const bool noInputMixer = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.no-input-mixer-8ch") == 0;
        const bool partialTrace = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-effect-partial-trace-64") == 0;
        const bool responseTrace = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-effect-response-trace-64") == 0;
        const bool ambiEffectTrace = partialTrace || responseTrace;
        const bool parameterSurfaceEncoder = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-stochastic-encoder-64") == 0
            || std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-wrangler-encoder-64") == 0;
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
            ok = readValues(randomizedIds, initialValues)
                && readValues(protectedIds, protectedBefore)
                && clickDocument(randomPoint)
                && readValues(randomizedIds, firstRandom)
                && changedValueCount(initialValues, firstRandom) >= 4u
                && clickDocument(NSMakePoint(NSMidX(surfTab), NSMidY(surfTab)))
                && clickDocument(NSMakePoint(
                    NSMidX(surfaceButton(2u)), NSMidY(surfaceButton(2u))))
                && clickDocument(randomPoint)
                && readValues(randomizedIds, secondRandom)
                && changedValueCount(firstRandom, secondRandom) >= 4u
                && clickDocument(NSMakePoint(
                    NSMidX(surfaceButton(2u)), NSMidY(surfaceButton(2u))))
                && clickDocument(NSMakePoint(
                    NSMidX(surfaceButton(1u)), NSMidY(surfaceButton(1u))));

            MemoryPluginState beforeRandomState;
            MemoryPluginState afterRandomState;
            MemoryPluginState reenabledState;
            ok = ok && saveState(beforeRandomState)
                && clickDocument(randomPoint)
                && readValues(randomizedIds, thirdRandom)
                && changedValueCount(secondRandom, thirdRandom) >= 4u
                && readValues(protectedIds, protectedAfter)
                && protectedBefore == protectedAfter
                && saveState(afterRandomState);

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
                ok = decodeWorldSphereState(beforeRandomState, before)
                    && decodeWorldSphereState(afterRandomState, after)
                    && surfaceContractHolds(before, after);
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
                    && reenabledContractHolds(reenabled);
            } else if (ok) {
                WorldSphereSavedState<s3g::AmbiInsectParams> reenabled {};
                ok = decodeWorldSphereState(reenabledState, reenabled)
                    && reenabledContractHolds(reenabled);
            }
            if (ok) {
                ok = clickDocument(NSMakePoint(
                    NSMidX(surfaceButton(1u)), NSMidY(surfaceButton(1u))));
            }
        }
        if (ok && parameterSurfaceEncoder) {
            failureStage = "Parameter Surface POP window";
            const bool wrangler = std::strcmp(
                pluginId,
                "org.s3g.s3g-dsp.ambi-wrangler-encoder-64") == 0;
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
            auto clickDocument = [&](NSPoint point) {
                NSView* hit = [parent hitTest:
                    [parent convertPoint:point fromView:document]];
                if (hit != document) return false;
                [hit mouseDown:mouseEvent(NSEventTypeLeftMouseDown, point)];
                [hit mouseUp:mouseEvent(NSEventTypeLeftMouseUp, point)];
                return true;
            };
            @try {
                ok = gui->show(plugin)
                    && clickDocument(surfaceTab)
                    && [[document valueForKey:@"fieldPage"] intValue]
                        == surfacePage
                    && clickDocument(addButton)
                    && clickDocument(addButton)
                    && clickDocument(enableButton)
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
                // base scene wins immediately and the two cells survive.
                if (ok) {
                    const auto* pluginState =
                        static_cast<const clap_plugin_state_t*>(
                            plugin->get_extension(plugin, CLAP_EXT_STATE));
                    auto saveState = [&](MemoryPluginState& memory) {
                        clap_ostream_t output { &memory, stateWriteWhole };
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
                        std::clamp(NSMidX(randomRect),
                            NSMinX(randomRect) + 3.0,
                            visibleRight - 3.0),
                        NSMidY(randomRect));
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
                            && before.surface.cellCount == 2u
                            && after.surface.enabled == 0u
                            && after.surface.cellCount == 2u
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
                            && before.surface.cellCount == 2u
                            && after.surface.enabled == 0u
                            && after.surface.cellCount == 2u
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
                            && state.surface.cellCount == 2u;
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
                            && state.surface.cellCount == 2u;
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
                if (ok) clickNoInput(NSMakePoint(randomX, randomY));
                ok = ok
                    && params->get_value(plugin, 5u, &feedbackAfter)
                    && params->get_value(plugin, 1000u, &bodyAfter)
                    && (std::fabs(feedbackAfter - feedbackBefore) > 0.000001
                        || std::fabs(bodyAfter - bodyBefore) > 0.000001);

                const auto titleBand = s3g::gui_layout::matrixTitleBand(
                    s3g::gui_layout::kNoInputMixerFamilyLayout.canvas);
                const NSRect presetAnchor =
                    s3g::clap_gui::cocoaRect(titleBand.presetMenu);
                if (ok) clickNoInput(NSMakePoint(
                    NSMidX(presetAnchor), NSMidY(presetAnchor)));
                const uint32_t zoneWebIndex = 5u;
                if (ok) clickNoInput(NSMakePoint(
                    NSMidX(presetAnchor),
                    NSMaxY(presetAnchor) + 2.0
                        + 18.0 * (zoneWebIndex + 0.5)));
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
                    failureStage =
                        "No Input Mixer smooth mixer drag and POP window";
                    const auto& family =
                        s3g::gui_layout::kNoInputMixerFamilyLayout;
                    const NSRect plot = s3g::clap_gui::cocoaRect(
                        family.fieldPlot);
                    constexpr CGFloat tabWidth = 58.0;
                    constexpr CGFloat tabGap = 5.0;
                    const CGFloat tabStart = family.fieldPanel.x
                        + family.fieldPanel.width - 4.0 * tabWidth
                        - 3.0 * tabGap - 10.0;
                    clickNoInput(NSMakePoint(
                        tabStart + (tabWidth + tabGap) + tabWidth * 0.5,
                        family.fieldPanel.y + 11.0));

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

                    const NSPoint popPoint = NSMakePoint(
                        family.fieldPanel.x + family.fieldPanel.width
                            - 4.0 * tabWidth - 3.0 * tabGap - 68.0 + 24.0,
                        family.fieldPanel.y + 11.0);
                    if (ok) clickNoInput(popPoint);
                    NSPanel* mixerPanel = ok
                        ? [document valueForKey:@"mixerPanel"] : nil;
                    NSView* popup = mixerPanel
                        ? [mixerPanel contentView] : nil;
                    ok = ok && mixerPanel && [mixerPanel isVisible]
                        && popup;
                    if (ok) {
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
                    NSPanel* patchPanel = nil;
                    if (ok) clickNoInput(popPoint);
                    channelPanel = ok
                        ? [document valueForKey:@"channelPanel"] : nil;
                    if (ok) clickNoInput(popPoint);
                    safetyPanel = ok
                        ? [document valueForKey:@"safetyPanel"] : nil;
                    if (ok) clickNoInput(popPoint);
                    patchPanel = ok
                        ? [document valueForKey:@"patchPanel"] : nil;
                    ok = ok && channelPanel && safetyPanel && patchPanel
                        && [channelPanel isVisible]
                        && [safetyPanel isVisible]
                        && [patchPanel isVisible]
                        && NSWidth([[channelPanel contentView] bounds])
                            == nativeWidth
                        && NSHeight([[channelPanel contentView] bounds])
                            == nativeHeight;
                    const char* captureDirectory = std::getenv(
                        "S3G_GUI_SMOKE_PDF_DIR");
                    if (ok && captureDirectory && captureDirectory[0]) {
                        NSString* directory = [NSString
                            stringWithUTF8String:captureDirectory];
                        const std::array<std::pair<NSPanel*, NSString*>, 3u>
                            detached {{
                                { channelPanel, @"channel-pop" },
                                { safetyPanel, @"safety-pop" },
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
                    [patchPanel orderOut:nil];
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

            @try {
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
                            fieldPanel, 2u))
                    && [[document valueForKey:@"fieldPage"] intValue]
                        == 0
                    && [[document valueForKey:@"cameraView"] intValue]
                        == 2;
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
