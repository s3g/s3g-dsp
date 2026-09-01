#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
#include "s3g_sample_cutups.h"
#include "s3g_sample_cutups_analysis.h"
#include "s3g_sample_lanes.h"
#define S3GSampleLanesView S3GSampleCutupsView
#elif defined(S3G_SAMPLE_GRAINS_VARIANT)
#include "s3g_sample_grains.h"
#define S3GSampleLanesView S3GSampleGrainsView
#else
#include "s3g_sample_lanes.h"
#endif
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_sample_storage.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/audio-ports-config.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include "s3g_sample_lanes_prelude.inc"

bool parameterAvailableForPlugin(const Plugin& instance, clap_id id) noexcept
{
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    if (id >= kOutputModeParamId && id <= kAvoidAdjacentParamId)
        return instance.outputChannelCount != 2u;
#elif defined(S3G_SAMPLE_GRAINS_VARIANT)
    const bool stereo = instance.outputChannelCount == 2u;
    if (id >= kOutputModeParamId && id <= kAvoidAdjacentParamId)
        return !stereo;
    if (id >= kChannelModeParamId && id <= kMonoSpreadParamId)
        return stereo;
#else
    (void)instance;
    (void)id;
#endif
    return true;
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
    if (pending.kind != s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_gesture_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = pending.kind
                == s3g::clap_gui::ParamEventKind::GestureBegin
            ? CLAP_EVENT_PARAM_GESTURE_BEGIN
            : CLAP_EVENT_PARAM_GESTURE_END;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        return output->try_push(output, &event.header);
    }
    clap_event_param_value_t event {};
    event.header.size = sizeof(event);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = pending.value;
    return output->try_push(output, &event.header);
}

void serviceGuiParamEvents(Plugin& instance,
    const clap_output_events_t* output) noexcept
{
    if (instance.guiParamConsumer.test_and_set(std::memory_order_acquire))
        return;
    s3g::clap_gui::ParamEvent pending {};
    while (instance.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value)
            setParam(instance, pending.paramId, pending.value);
        instance.guiParamEvents.pop();
    }
    instance.guiParamConsumer.clear(std::memory_order_release);
}

void queueGuiParamGesture(Plugin& instance, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def || !parameterAvailableForPlugin(instance, id)) return;
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, clampParam(*def, value) },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!instance.guiParamEvents.pushBatch(events.data(), events.size()))
        return;
    setParam(instance, id, events[1u].value, true);
    requestGuiParamService(instance);
}

void queueGuiParamBegin(Plugin& instance, clap_id id)
{
    if (!parameterAvailableForPlugin(instance, id)) return;
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 }))
        requestGuiParamService(instance);
}

void queueGuiParamValue(Plugin& instance, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def || !parameterAvailableForPlugin(instance, id)) return;
    value = clampParam(*def, value);
    if (!instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) return;
    setParam(instance, id, value, true);
    requestGuiParamService(instance);
}

void queueGuiParamEnd(Plugin& instance, clap_id id)
{
    if (!parameterAvailableForPlugin(instance, id)) return;
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 }))
        requestGuiParamService(instance);
}

const char* transportName(int value) noexcept
{
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    return value <= 0 ? "Free" : "Host";
#else
    constexpr std::array<const char*, 3u> names {{
        "Forward", "Reverse", "Ping Pong",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
#endif
}

const char* rateBasisName(int value) noexcept
{
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    constexpr std::array<const char*, 8u> names {{
        "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 7))];
#else
    return value <= 0 ? "Normal" : "Hertz";
#endif
}

const char* pathName(int value) noexcept
{
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    constexpr std::array<const char*, 6u> names {{
        "Down", "Up", "Palindrome", "Random", "Random Cycle", "Manual",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 5))];
#else
    constexpr std::array<const char*, 8u> names {{
        "Down", "Up", "Triangle", "Sine", "Steps Down", "Steps Up",
        "Random", "Manual",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 7))];
#endif
}

const char* blendName(int value) noexcept
{
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    constexpr std::array<const char*, 7u> names {{
        "Timeline", "Forward", "Reverse", "Palindrome", "Random", "Walk",
        "Manual",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 6))];
#else
    return value <= 0 ? "Crossfade" : "Jump";
#endif
}

const char* shapeName(int value) noexcept
{
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    return value <= 0 ? "Equal" : "Transient";
#else
    constexpr std::array<const char*, 4u> names {{
        "Linear", "Smooth", "Exponential", "Plateau",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 3))];
#endif
}

const char* voiceName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Poly", "Mono", "Legato",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}

const char* triggerName(int value) noexcept
{
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    constexpr std::array<const char*, 3u> names {{
        "Gate", "One Shot", "Toggle",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 1, 3) - 1)];
#else
    constexpr std::array<const char*, 4u> names {{
        "Auto", "Gate", "One Shot", "Toggle",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 3))];
#endif
}

const char* outputModeName(int value) noexcept
{ return value <= 0 ? "Preserve Field" : "Distribute"; }

const char* allocationCadenceName(int value) noexcept
{
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    constexpr std::array<const char*, 3u> names {{ "Note", "Cut", "Pattern" }};
#else
    constexpr std::array<const char*, 3u> names {{ "Note", "Lane", "Turn" }};
#endif
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}

const char* traversalName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Sequential", "Reverse", "Palindrome", "Random", "Random Cycle",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}

const char* outputWidthName(int value) noexcept
{ return value <= 0 ? "Mono" : "Stereo"; }

const char* pairLayoutName(int value) noexcept
{ return value <= 0 ? "Adjacent" : "Split Banks"; }

const char* onOffName(int value) noexcept
{ return value <= 0 ? "Off" : "On"; }

#if defined(S3G_SAMPLE_GRAINS_VARIANT)
const char* channelModeName(int value) noexcept
{
    constexpr std::array<const char*, 6u> names {{
        "Preserve Origins", "Mono Sum", "Left", "Right", "Mid", "Side",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 5))];
}

const char* stereoLinkName(int value) noexcept
{ return value <= 0 ? "Linked" : "Independent"; }

const char* grainSourceModeName(int value) noexcept
{
    constexpr std::array<const char*, 4u> names {{
        "Scan", "Freeze", "Cloud", "Slice",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 3))];
}

const char* grainEnvelopeName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Parzen", "Sine", "Hann", "Triangle", "Gaussian",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}

const char* grainTimingName(int value) noexcept
{ return value <= 0 ? "Regular" : "Scatter"; }

const char* grainPositionBiasName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Behind", "Around", "Ahead",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}

const char* grainSourceAdvanceName(int value) noexcept
{ return value <= 0 ? "Scan" : "Grain"; }

const char* grainMutateName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Ordinary", "Sorter", "Stutter", "Shrink", "Doublets",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}
#endif

const char* midiName(int value, char* buffer, std::size_t size) noexcept
{
    if (value <= 0) return "Omni";
    std::snprintf(buffer, size, "Channel %d", std::clamp(value, 1, 16));
    return buffer;
}

template <typename Name>
bool parseNamedValue(const char* display, double* value, int count, Name name)
{
    for (int index = 0; index < count; ++index) {
        if (strcasecmp(display, name(index)) == 0) {
            *value = static_cast<double>(index);
            return true;
        }
    }
    return false;
}

uint32_t paramsCount(const clap_plugin_t* plugin)
{
    if (!plugin) return 0u;
    const auto& instance = *self(plugin);
    return static_cast<uint32_t>(std::count_if(kParamDefs.begin(),
        kParamDefs.end(), [&instance](const ParamDef& def) {
            return parameterAvailableForPlugin(instance, def.id);
        }));
}

bool paramsGetInfo(const clap_plugin_t* plugin, uint32_t index,
    clap_param_info_t* info)
{
    if (!plugin || !info) return false;
    const auto& instance = *self(plugin);
    const ParamDef* available = nullptr;
    uint32_t availableIndex = 0u;
    for (const auto& def : kParamDefs) {
        if (!parameterAvailableForPlugin(instance, def.id)) continue;
        if (availableIndex++ == index) {
            available = &def;
            break;
        }
    }
    if (!available) return false;
    const auto& def = *available;
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "%s", def.module);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!plugin || !value || !paramDef(id)
        || !parameterAvailableForPlugin(*self(plugin), id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!plugin || !display || size == 0u || !paramDef(id)
        || !parameterAvailableForPlugin(*self(plugin), id)) return false;
    if (id == kTransportParamId)
        std::snprintf(display, size, "%s", transportName(
            static_cast<int>(std::lround(value))));
    else if (id == kRateBasisParamId)
        std::snprintf(display, size, "%s", rateBasisName(
            static_cast<int>(std::lround(value))));
    else if (id == kPathParamId)
        std::snprintf(display, size, "%s", pathName(
            static_cast<int>(std::lround(value))));
    else if (id == kBlendParamId)
        std::snprintf(display, size, "%s", blendName(
            static_cast<int>(std::lround(value))));
    else if (id == kShapeParamId)
        std::snprintf(display, size, "%s", shapeName(
            static_cast<int>(std::lround(value))));
    else if (id == kVoiceModeParamId)
        std::snprintf(display, size, "%s", voiceName(
            static_cast<int>(std::lround(value))));
    else if (id == kTriggerParamId)
        std::snprintf(display, size, "%s", triggerName(
            static_cast<int>(std::lround(value))));
    else if (id == kOutputModeParamId)
        std::snprintf(display, size, "%s", outputModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kAllocationCadenceParamId)
        std::snprintf(display, size, "%s", allocationCadenceName(
            static_cast<int>(std::lround(value))));
    else if (id == kTraversalParamId)
        std::snprintf(display, size, "%s", traversalName(
            static_cast<int>(std::lround(value))));
    else if (id == kOutputWidthParamId)
        std::snprintf(display, size, "%s", outputWidthName(
            static_cast<int>(std::lround(value))));
    else if (id == kPairLayoutParamId)
        std::snprintf(display, size, "%s", pairLayoutName(
            static_cast<int>(std::lround(value))));
    else if (id == kAvoidAdjacentParamId)
        std::snprintf(display, size, "%s", onOffName(
            static_cast<int>(std::lround(value))));
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    else if (id == kTempoSyncParamId)
        std::snprintf(display, size, "%s", onOffName(
            static_cast<int>(std::lround(value))));
#endif
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    else if (id == kChannelModeParamId)
        std::snprintf(display, size, "%s", channelModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kStereoLinkParamId)
        std::snprintf(display, size, "%s", stereoLinkName(
            static_cast<int>(std::lround(value))));
    else if (id == kGrainSourceModeParamId)
        std::snprintf(display, size, "%s", grainSourceModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kGrainEnvelopeParamId)
        std::snprintf(display, size, "%s", grainEnvelopeName(
            static_cast<int>(std::lround(value))));
    else if (id == kGrainTimingParamId)
        std::snprintf(display, size, "%s", grainTimingName(
            static_cast<int>(std::lround(value))));
    else if (id == kPositionBiasParamId)
        std::snprintf(display, size, "%s", grainPositionBiasName(
            static_cast<int>(std::lround(value))));
    else if (id == kSourceAdvanceParamId)
        std::snprintf(display, size, "%s", grainSourceAdvanceName(
            static_cast<int>(std::lround(value))));
    else if (id == kGrainMutateParamId)
        std::snprintf(display, size, "%s", grainMutateName(
            static_cast<int>(std::lround(value))));
    else if (id == kSourceTimeSyncParamId)
        std::snprintf(display, size, "%s", onOffName(
            static_cast<int>(std::lround(value))));
#endif
    else if (id == kMidiParamId) {
        char buffer[32] {};
        std::snprintf(display, size, "%s", midiName(
            static_cast<int>(std::lround(value)), buffer, sizeof(buffer)));
    }
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    else if (id == kRateParamId)
        std::snprintf(display, size, "%.2f Hz", value);
    else if (id == kOutParamId)
        std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTuneParamId)
        std::snprintf(display, size, "%+.2f st", value);
    else if (id == kFineParamId)
        std::snprintf(display, size, "%+.1f ct", value);
    else if (id == kAttackParamId || id == kReleaseParamId)
        std::snprintf(display, size, "%.1f ms", value * 1000.0);
    else if (id == kActiveOutputsParamId)
        std::snprintf(display, size, "%02d",
            static_cast<int>(std::lround(value)));
    else if (id == kRootParamId || id == kSeedParamId
        || id == kCyclesParamId || id == kOffsetParamId
        || id == kSkewParamId)
        std::snprintf(display, size, "%d",
            static_cast<int>(std::lround(value)));
    else if (id == kPanParamId)
        std::snprintf(display, size, "%+.2f", value);
    else if (id == kStartParamId || id == kEndParamId
        || id == kManualLaneParamId || id == kCurveParamId
        || id == kLaneSlewParamId || id == kVelocityParamId
        || id == kCutReverseChanceParamId
        || id == kCutLevelVariationParamId)
        std::snprintf(display, size, "%.1f %%", value * 100.0);
    else if (id == kLoopCrossfadeParamId)
        std::snprintf(display, size, "%.1f ms", value);
    else if (id >= kLane1SpeedParamId && id <= kLane2SpeedParamId)
        std::snprintf(display, size, "%.2f BPM", value);
    else if (id == kCutPitchVariationParamId)
        std::snprintf(display, size, "%.2f st", value);
#else
    else if (id == kRateParamId)
        std::snprintf(display, size,
            paramValue(*self(plugin), kRateBasisParamId) < 0.5
                ? "%.2f x" : "%.2f Hz", value);
    else if (id == kOutParamId)
        std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTuneParamId)
        std::snprintf(display, size, "%+.2f st", value);
    else if (id == kFineParamId)
        std::snprintf(display, size, "%+.1f ct", value);
    else if (id == kAttackParamId || id == kReleaseParamId
        || id == kLaneSlewParamId)
        std::snprintf(display, size, "%.1f ms", value * 1000.0);
    else if (id == kActiveOutputsParamId)
        std::snprintf(display, size, "%02d",
            static_cast<int>(std::lround(value)));
    else if (id == kRootParamId || id == kSeedParamId
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
        || id == kRegionCountParamId
#endif
        )
        std::snprintf(display, size, "%d",
            static_cast<int>(std::lround(value)));
    else if (id == kManualLaneParamId)
        std::snprintf(display, size, "%.2f", 1.0 + value * 3.0);
    else if (id == kPanParamId || id == kCurveParamId)
        std::snprintf(display, size, "%+.2f", value);
    else if (id == kStartParamId || id == kEndParamId
        || id == kLoopCrossfadeParamId || id == kOffsetParamId
        || id == kSkewParamId || id == kVelocityParamId)
        std::snprintf(display, size, "%.1f %%", value * 100.0);
    else if (id == kCyclesParamId)
        std::snprintf(display, size, "%.2f x", value);
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    else if (id == kGrainDensityParamId)
        std::snprintf(display, size, "%.2f Hz", value);
    else if (id == kGrainSizeParamId)
        std::snprintf(display, size, "%.1f ms", value);
    else if (id == kPitchSprayParamId || id == kGrainPitchShiftParamId)
        std::snprintf(display, size, "%.2f st", value);
    else if (id == kSourcePositionParamId
        || id == kPositionSprayParamId || id == kReverseChanceParamId
        || id == kMutateAmountParamId || id == kMonoSpreadParamId
        || id == kGrainSizeVariationParamId
        || id == kGrainLevelVariationParamId
        || id == kTimingScatterParamId)
        std::snprintf(display, size, "%.1f %%", value * 100.0);
    else if (id == kEnvelopeSkewParamId)
        std::snprintf(display, size, "%+.1f %%", value * 100.0);
#endif
    else if (id >= kLane1SpeedParamId && id <= kLane4NudgeParamId) {
        const clap_id laneOffset = (id - kLane1SpeedParamId) % 3u;
        if (laneOffset < 2u)
            std::snprintf(display, size, "%.2f x", value);
        else std::snprintf(display, size, "%+.1f %%", value * 100.0);
    }
#endif
    else return false;
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id,
    const char* display, double* value)
{
    if (!plugin || !display || !value || !paramDef(id)
        || !parameterAvailableForPlugin(*self(plugin), id)) return false;
    if (id == kTransportParamId)
        return parseNamedValue(display, value,
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
            2,
#else
            3,
#endif
            transportName);
    if (id == kRateBasisParamId)
        return parseNamedValue(display, value,
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
            8,
#else
            2,
#endif
            rateBasisName);
    if (id == kPathParamId)
        return parseNamedValue(display, value,
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
            6,
#else
            8,
#endif
            pathName);
    if (id == kBlendParamId)
        return parseNamedValue(display, value,
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
            7,
#else
            2,
#endif
            blendName);
    if (id == kShapeParamId)
        return parseNamedValue(display, value,
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
            2,
#else
            4,
#endif
            shapeName);
    if (id == kVoiceModeParamId)
        return parseNamedValue(display, value, 3, voiceName);
    if (id == kTriggerParamId) {
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
        for (int trigger = 1; trigger <= 3; ++trigger) {
            if (strcasecmp(display, triggerName(trigger)) == 0) {
                *value = static_cast<double>(trigger);
                return true;
            }
        }
        return false;
#else
        return parseNamedValue(display, value, 4, triggerName);
#endif
    }
    if (id == kOutputModeParamId)
        return parseNamedValue(display, value, 2, outputModeName);
    if (id == kAllocationCadenceParamId)
        return parseNamedValue(display, value, 3, allocationCadenceName);
    if (id == kTraversalParamId)
        return parseNamedValue(display, value, 5, traversalName);
    if (id == kOutputWidthParamId)
        return parseNamedValue(display, value, 2, outputWidthName);
    if (id == kPairLayoutParamId)
        return parseNamedValue(display, value, 2, pairLayoutName);
    if (id == kAvoidAdjacentParamId)
        return parseNamedValue(display, value, 2, onOffName);
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    if (id == kTempoSyncParamId)
        return parseNamedValue(display, value, 2, onOffName);
#endif
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    if (id == kChannelModeParamId)
        return parseNamedValue(display, value, 6, channelModeName);
    if (id == kStereoLinkParamId)
        return parseNamedValue(display, value, 2, stereoLinkName);
    if (id == kGrainSourceModeParamId)
        return parseNamedValue(display, value, 4, grainSourceModeName);
    if (id == kGrainEnvelopeParamId)
        return parseNamedValue(display, value, 5, grainEnvelopeName);
    if (id == kGrainTimingParamId)
        return parseNamedValue(display, value, 2, grainTimingName);
    if (id == kPositionBiasParamId)
        return parseNamedValue(display, value, 3, grainPositionBiasName);
    if (id == kSourceAdvanceParamId)
        return parseNamedValue(display, value, 2, grainSourceAdvanceName);
    if (id == kGrainMutateParamId)
        return parseNamedValue(display, value, 5, grainMutateName);
    if (id == kSourceTimeSyncParamId)
        return parseNamedValue(display, value, 2, onOffName);
#endif
    if (id == kMidiParamId) {
        if (strcasecmp(display, "Omni") == 0) {
            *value = 0.0;
            return true;
        }
        for (int channel = 1; channel <= 16; ++channel) {
            char buffer[32] {};
            if (strcasecmp(display, midiName(channel, buffer,
                    sizeof(buffer))) == 0) {
                *value = channel;
                return true;
            }
        }
    }
    char* end = nullptr;
    double parsed = std::strtod(display, &end);
    if (end == display) return false;
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    if ((id == kStartParamId || id == kEndParamId
            || id == kManualLaneParamId || id == kCurveParamId
            || id == kLaneSlewParamId || id == kVelocityParamId
            || id == kCutReverseChanceParamId
            || id == kCutLevelVariationParamId)
        && std::strchr(display, '%')) parsed *= 0.01;
    if ((id == kAttackParamId || id == kReleaseParamId)
        && (std::strstr(display, "ms") || std::strstr(display, "MS")))
        parsed *= 0.001;
#else
    if ((id == kStartParamId || id == kEndParamId
            || id == kLoopCrossfadeParamId || id == kOffsetParamId
            || id == kSkewParamId || id == kVelocityParamId
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
            || id == kSourcePositionParamId
            || id == kPositionSprayParamId
            || id == kReverseChanceParamId
            || id == kMutateAmountParamId || id == kMonoSpreadParamId
            || id == kGrainSizeVariationParamId
            || id == kGrainLevelVariationParamId
            || id == kTimingScatterParamId
            || id == kEnvelopeSkewParamId
#endif
            || (id >= kLane1NudgeParamId && id <= kLane4NudgeParamId
                && (id - kLane1NudgeParamId) % 3u == 0u))
        && std::strchr(display, '%')) parsed *= 0.01;
    if ((id == kAttackParamId || id == kReleaseParamId
            || id == kLaneSlewParamId)
        && (std::strstr(display, "ms") || std::strstr(display, "MS")))
        parsed *= 0.001;
    if (id == kManualLaneParamId) parsed = (parsed - 1.0) / 3.0;
#endif
    *value = parsed;
    return true;
}

void readParameterEvents(Plugin& instance,
    const clap_input_events_t* events) noexcept
{
    if (!events || !events->size || !events->get) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        if (!parameterAvailableForPlugin(instance, event->param_id)) continue;
        setParam(instance, event->param_id, event->value);
    }
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto& instance = *self(plugin);
    readParameterEvents(instance, input);
    serviceGuiParamEvents(instance, output);
}

const clap_plugin_params_t params {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto& instance = *self(plugin);
    SavedState saved;
    for (const auto& def : kParamDefs)
        saved.parameters[paramIndex(def.id)] = paramValue(instance, def.id);
    std::array<std::shared_ptr<const SampleAsset>,
        s3g::sample::kSampleLaneCount> assets;
    std::array<std::string, s3g::sample::kSampleLaneCount> paths;
    StorageMode mode;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        assets = instance.controlAssets;
        paths = instance.samplePaths;
        mode = instance.storageMode;
    }
    saved.storageMode = static_cast<uint8_t>(mode);
    saved.manualPath.pointCount = manualPathSnapshot(instance,
        saved.manualPath.points);
    uint64_t embeddedBytes = 0u;
    for (std::size_t lane = 0u; lane < assets.size(); ++lane) {
        if (mode == StorageMode::Project
            && instance.projectFileRegistrations[lane].registered())
            paths[lane] = instance.projectFileRegistrations[lane].absolutePath();
        if (mode == StorageMode::Project && !paths[lane].empty()) {
            const ReaperContext context = s3g::sample_storage::reaperContext(
                instance.host);
            std::string relative;
            std::string error;
            if (s3g::sample_storage::makeProjectRelativePath(context,
                    paths[lane], relative, &error)) paths[lane] = relative;
        }
        std::snprintf(saved.lanes[lane].path.data(),
            saved.lanes[lane].path.size(), "%s", paths[lane].c_str());
        if (!assets[lane]) continue;
        auto& state = saved.lanes[lane];
        state.channelCount = assets[lane]->channelCount;
        state.frameCount = assets[lane]->frameCount();
        state.sampleRate = assets[lane]->sampleRate;
        const uint64_t bytes = static_cast<uint64_t>(state.channelCount)
            * state.frameCount * sizeof(float);
        const bool mustEmbed = mode == StorageMode::Embed
            || paths[lane].empty();
        if (mustEmbed) {
            if (embeddedBytes > kMaximumEmbeddedAudioBytes - bytes)
                return false;
            embeddedBytes += bytes;
            state.embedded = 1u;
        }
    }
    if (!s3g::clap_state::writeAll(stream, &saved, sizeof(saved)))
        return false;
    for (std::size_t lane = 0u; lane < assets.size(); ++lane) {
        if (!assets[lane] || saved.lanes[lane].embedded == 0u) continue;
        for (uint8_t channel = 0u;
             channel < assets[lane]->channelCount; ++channel) {
            const auto& samples = assets[lane]->channels[channel];
            if (!s3g::clap_state::writeAll(stream, samples.data(),
                    samples.size() * sizeof(float))) return false;
        }
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto& instance = *self(plugin);
    StateHeader header;
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic
#if defined(S3G_SAMPLE_CUTUPS_VARIANT) \
    || defined(S3G_SAMPLE_GRAINS_VARIANT)
        || header.version != kStateVersion
        || header.parameterCount != kParamCount
#else
        || (header.version != kStateVersion
            && header.version != kRoutingPreviousStateVersion
            && header.version != kPreviousStateVersion
            && header.version != kLegacyStateVersion)
        || (header.version == kStateVersion
            && header.parameterCount != kParamCount)
        || (header.version == kRoutingPreviousStateVersion
            && header.parameterCount != kPreviousParamCount)
        || (header.version == kPreviousStateVersion
            && header.parameterCount != kPreviousParamCount)
        || (header.version == kLegacyStateVersion
            && header.parameterCount != kLegacyParamCount)
#endif
        ) return false;
    SavedState saved;
    saved.magic = header.magic;
    saved.version = header.version;
    saved.parameterCount = header.parameterCount;
    saved.storageMode = header.storageMode;
    saved.reserved = header.reserved;
    for (const auto& def : kParamDefs)
        saved.parameters[paramIndex(def.id)] = def.defaultValue;
    if (!s3g::clap_state::readAll(stream, saved.parameters.data(),
            static_cast<std::size_t>(header.parameterCount)
                * sizeof(double))
        || !s3g::clap_state::readAll(stream, saved.lanes.data(),
            saved.lanes.size() * sizeof(LaneState))) return false;
    if (
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
        true
#elif !defined(S3G_SAMPLE_GRAINS_VARIANT)
        header.version >= kRoutingPreviousStateVersion
#else
        true
#endif
        && !s3g::clap_state::readAll(stream, &saved.manualPath,
            sizeof(saved.manualPath))) return false;
    for (const auto& def : kParamDefs)
        setParam(instance, def.id,
            saved.parameters[paramIndex(def.id)]);
    publishManualPath(instance, saved.manualPath.points,
        saved.manualPath.pointCount, false);
    const StorageMode mode = s3g::sample_storage::sanitizeStorageMode(
        saved.storageMode);
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.storageMode = mode;
    }
    uint64_t embeddedBytes = 0u;
    for (const auto& lane : saved.lanes) if (lane.embedded != 0u) {
        const uint64_t bytes = static_cast<uint64_t>(lane.channelCount)
            * lane.frameCount * sizeof(float);
        if (lane.channelCount == 0u
            || lane.channelCount > std::min<uint32_t>(
                instance.outputChannelCount,
                static_cast<uint32_t>(
                    s3g::sample::kMaximumAudioChannels))
            || lane.frameCount == 0u || !(lane.sampleRate > 0.0)
            || embeddedBytes > kMaximumEmbeddedAudioBytes - bytes)
            return false;
        embeddedBytes += bytes;
    }
    for (std::size_t lane = 0u; lane < saved.lanes.size(); ++lane) {
        instance.projectFileRegistrations[lane].clear();
        const auto& state = saved.lanes[lane];
        const std::string locator(state.path.data(), strnlen(
            state.path.data(), state.path.size()));
        std::shared_ptr<const SampleAsset> asset;
        std::string runtimePath = locator;
        if (state.embedded != 0u) {
            auto decoded = std::make_shared<SampleAsset>();
            decoded->sampleRate = state.sampleRate;
            decoded->channelCount = state.channelCount;
            for (uint8_t channel = 0u; channel < state.channelCount;
                 ++channel) {
                decoded->channels[channel].resize(state.frameCount);
                if (!s3g::clap_state::readAll(stream,
                        decoded->channels[channel].data(),
                        decoded->channels[channel].size() * sizeof(float)))
                    return false;
            }
            if (!decoded->valid()) return false;
            asset = std::move(decoded);
        } else if (!locator.empty()) {
            if (mode == StorageMode::Project
                && !std::filesystem::path(locator).is_absolute()) {
                std::string error;
                const ReaperContext context
                    = s3g::sample_storage::reaperContext(instance.host);
                (void)s3g::sample_storage::resolveProjectRelativePath(
                    context, locator, runtimePath, &error);
            }
#if defined(__APPLE__)
            std::string error;
            if (!runtimePath.empty())
                (void)decodeSampleFile(runtimePath, asset, error,
                    instance.outputChannelCount);
#endif
        }
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.linkSourcePaths[lane] = mode == StorageMode::Project
                ? std::string() : runtimePath;
            instance.projectRelativePaths[lane]
                = mode == StorageMode::Project ? locator : std::string();
            instance.sourceFileBytes[lane] = regularFileByteCount(runtimePath);
            instance.projectStoragePending[lane]
                = mode == StorageMode::Project && !runtimePath.empty();
            instance.projectCopyInFlight[lane] = false;
        }
        (void)publishAsset(instance, lane, asset, runtimePath, false);
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
        (void)publishMetadata(instance, lane,
            asset ? analyzeCutupsLane(*asset) : nullptr);
#endif
        if (mode == StorageMode::Project && !runtimePath.empty()) {
            const ReaperContext context = s3g::sample_storage::reaperContext(
                instance.host);
            (void)instance.projectFileRegistrations[lane].reset(context,
                runtimePath, nullptr, nullptr, instance.plugin.desc->name);
        }
    }
    instance.engine.reset();
    instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    requestProcess(instance);
    return true;
}

const clap_plugin_state_t state { stateSave, stateLoad };

bool midiChannelAccepted(const Plugin& instance, uint8_t channel) noexcept
{
    const int configured = static_cast<int>(std::lround(
        paramValue(instance, kMidiParamId)));
    return configured == 0 || configured == static_cast<int>(channel) + 1;
}

void requestAction(Plugin& instance, uint32_t action) noexcept
{
    instance.requestedActions.fetch_or(action, std::memory_order_release);
    instance.actionFeedback.fetch_or(action, std::memory_order_release);
    requestProcess(instance);
}

std::size_t collectRenderEvents(Plugin& instance,
    const clap_input_events_t* input, uint32_t frameCount) noexcept
{
    std::size_t resultCount = 0u;
    const auto append = [&](uint32_t frame, LaneEventKind kind,
                            uint64_t noteId, uint8_t key, float velocity,
                            uint8_t channel) {
        if (resultCount >= instance.blockEvents.size()) return;
        instance.blockEvents[resultCount++] = {
            std::min(frame, frameCount), kind, noteId, key, velocity, channel,
        };
    };
    const uint32_t actions = instance.requestedActions.exchange(
        0u, std::memory_order_acq_rel);
    if ((actions & kActionStopAll) != 0u)
        append(0u, LaneEventKind::StopAll, 0u, 0u, 0.0f, 0u);
    if ((actions & kActionPreview) != 0u)
        append(0u, LaneEventKind::Preview, 0xffffffffffffffffull,
            static_cast<uint8_t>(std::lround(
                paramValue(instance, kRootParamId))), 1.0f, 0u);
    if (!input || !input->size || !input->get) return resultCount;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* header = input->get(input, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (header->type == CLAP_EVENT_PARAM_VALUE
            && header->size >= sizeof(clap_event_param_value_t)) {
            const auto* event = reinterpret_cast<
                const clap_event_param_value_t*>(header);
            if (!parameterAvailableForPlugin(instance, event->param_id))
                continue;
            setParam(instance, event->param_id, event->value);
            continue;
        }
        if ((header->type == CLAP_EVENT_NOTE_ON
                || header->type == CLAP_EVENT_NOTE_OFF
                || header->type == CLAP_EVENT_NOTE_CHOKE)
            && header->size >= sizeof(clap_event_note_t)) {
            const auto* event = reinterpret_cast<const clap_event_note_t*>(
                header);
            if (event->channel < 0 || event->channel > 15
                || event->key < 0 || event->key > 127
                || !midiChannelAccepted(instance,
                    static_cast<uint8_t>(event->channel))) continue;
            append(header->time, header->type == CLAP_EVENT_NOTE_ON
                    ? LaneEventKind::NoteOn : LaneEventKind::NoteOff,
                event->note_id >= 0
                    ? static_cast<uint64_t>(
                        static_cast<uint32_t>(event->note_id)) + 1u : 0u,
                static_cast<uint8_t>(event->key),
                static_cast<float>(event->velocity),
                static_cast<uint8_t>(event->channel));
            continue;
        }
        if (header->type != CLAP_EVENT_MIDI
            || header->size < sizeof(clap_event_midi_t)) continue;
        const auto* event = reinterpret_cast<const clap_event_midi_t*>(header);
        const uint8_t status = event->data[0u] & 0xf0u;
        const uint8_t channel = event->data[0u] & 0x0fu;
        if (!midiChannelAccepted(instance, channel)) continue;
        const uint8_t key = event->data[1u] & 0x7fu;
        const uint8_t value = event->data[2u] & 0x7fu;
        if (status == 0x90u && value != 0u)
            append(header->time, LaneEventKind::NoteOn, 0u, key,
                static_cast<float>(value) / 127.0f, channel);
        else if (status == 0x80u || (status == 0x90u && value == 0u))
            append(header->time, LaneEventKind::NoteOff, 0u, key,
                0.0f, channel);
        else if (status == 0xb0u && key == 123u)
            append(header->time, LaneEventKind::StopAll, 0u, 0u,
                0.0f, channel);
    }
    return resultCount;
}

bool pluginInit(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    initializeParams(instance);
    setParam(instance, kActiveOutputsParamId,
        static_cast<double>(instance.outputChannelCount));
    instance.retainedAssets.reserve(64u);
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    instance.retainedMetadata.reserve(64u);
#endif
    instance.reaperContext = s3g::sample_storage::reaperContext(
        instance.host);
    if (instance.host && instance.host->get_extension) {
        instance.hostParams = static_cast<const clap_host_params_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_PARAMS));
        instance.hostState = static_cast<const clap_host_state_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_STATE));
    }
#if defined(__APPLE__)
    if (!startLoader(instance)) return false;
#endif
    return true;
}

void destroyGui(Plugin& instance);

void pluginDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance) return;
    for (auto& registration : instance->projectFileRegistrations)
        registration.clear();
#if defined(__APPLE__)
    destroyGui(*instance);
    stopLoader(*instance);
#endif
    delete instance;
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maximumFrames)
{
    auto& instance = *self(plugin);
    instance.sampleRate = sampleRate;
    instance.maximumFrames = maximumFrames;
    instance.active = instance.engine.prepare(sampleRate,
        instance.outputChannelCount);
    if (!instance.active) return false;
    for (uint32_t channel = 0u; channel < instance.outputChannelCount;
         ++channel)
        instance.scratch[channel].assign(maximumFrames, 0.0f);
    instance.audioAssets = {};
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    instance.audioMetadata = {};
#endif
    return true;
}

void pluginDeactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.active = false;
    instance.processing.store(false, std::memory_order_release);
    instance.engine.reset();
    instance.cursorCount.store(0u, std::memory_order_release);
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    instance.grainCursorCount.store(0u, std::memory_order_release);
#endif
    instance.audioAssets = {};
    for (auto& channel : instance.scratch) channel.clear();
    std::lock_guard<std::mutex> lock(instance.statusMutex);
    instance.retainedAssets.clear();
    for (const auto& asset : instance.controlAssets)
        if (asset) instance.retainedAssets.push_back(asset);
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    instance.audioMetadata = {};
    instance.retainedMetadata.clear();
    for (const auto& metadata : instance.controlMetadata)
        if (metadata) instance.retainedMetadata.push_back(metadata);
#endif
}

bool pluginStartProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing.store(true, std::memory_order_release);
    return true;
}

void pluginStopProcessing(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.processing.store(false, std::memory_order_release);
    instance.cursorCount.store(0u, std::memory_order_release);
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    instance.grainCursorCount.store(0u, std::memory_order_release);
#endif
    instance.activeVoiceCount.store(0u, std::memory_order_release);
}

void pluginReset(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.engine.reset();
    instance.cursorCount.store(0u, std::memory_order_release);
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    instance.grainCursorCount.store(0u, std::memory_order_release);
#endif
}

clap_process_status pluginProcess(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto& instance = *self(plugin);
    if (!process || process->frames_count > instance.maximumFrames
        || process->audio_outputs_count < 1u || !process->audio_outputs
        || process->audio_outputs[0u].channel_count
            < instance.outputChannelCount)
        return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(instance, process->out_events);
    for (std::size_t lane = 0u; lane < instance.audioAssets.size(); ++lane) {
        const auto* asset = instance.publishedAssets[lane].load(
            std::memory_order_acquire);
        if (asset != instance.audioAssets[lane]) {
            instance.audioAssets[lane] = asset;
            instance.engine.setPreparedAsset(lane, asset);
        }
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
        const auto* metadata = instance.publishedMetadata[lane].load(
            std::memory_order_acquire);
        if (metadata != instance.audioMetadata[lane]) {
            instance.audioMetadata[lane] = metadata;
            instance.engine.setPreparedMetadata(lane, metadata);
        }
#endif
    }
    const std::size_t eventCount = collectRenderEvents(instance,
        process->in_events, process->frames_count);
    std::array<float*, 32u> scratchPointers {};
    for (uint32_t channel = 0u; channel < instance.outputChannelCount;
         ++channel)
        scratchPointers[channel] = instance.scratch[channel].data();
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    double hostTempoBpm = 120.0;
    if (process->transport
        && (process->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(process->transport->tempo)
        && process->transport->tempo > 0.0)
        hostTempoBpm = process->transport->tempo;
    instance.engine.render(settingsSnapshot(instance),
        instance.blockEvents.data(), eventCount,
        scratchPointers.data(), instance.outputChannelCount,
        process->frames_count, hostTempoBpm);
#else
    instance.engine.render(settingsSnapshot(instance),
        instance.blockEvents.data(), eventCount,
        scratchPointers.data(), instance.outputChannelCount,
        process->frames_count);
#endif
    const auto& cursors = instance.engine.voiceCursors();
    const uint32_t cursorCount = instance.engine.voiceCursorCount();
    for (uint32_t index = 0u;
         index < s3g::sample::kMaximumLaneVoices; ++index) {
        instance.cursorSourcePositions[index].store(index < cursorCount
                ? cursors[index].sourcePositionNormalized : -1.0f,
            std::memory_order_release);
        instance.cursorLanePositions[index].store(index < cursorCount
                ? cursors[index].lanePositionNormalized : 0.0f,
            std::memory_order_release);
        instance.cursorPathPhases[index].store(index < cursorCount
                ? cursors[index].pathPhase : 0.0f,
            std::memory_order_release);
        instance.cursorKeys[index].store(index < cursorCount
                ? cursors[index].key : 0u, std::memory_order_release);
        for (std::size_t lane = 0u; lane < s3g::sample::kSampleLaneCount;
             ++lane) {
            instance.cursorLaneSourcePositions[index][lane].store(
                index < cursorCount
                    ? cursors[index].laneSourcePositions[lane] : -1.0f,
                std::memory_order_release);
            instance.cursorLaneWeights[index][lane].store(index < cursorCount
                    ? cursors[index].laneWeights[lane] : 0.0f,
                std::memory_order_release);
        }
    }
    instance.cursorCount.store(cursorCount, std::memory_order_release);
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    const auto& grainCursors = instance.engine.grainCursors();
    const uint32_t grainCursorCount = instance.engine.grainCursorCount();
    for (uint32_t index = 0u;
         index < s3g::sample::kMaximumPublishedGrainCursors; ++index) {
        instance.grainCursorPhases[index].store(index < grainCursorCount
                ? grainCursors[index].phase : 0.0f,
            std::memory_order_release);
        instance.grainCursorGains[index].store(index < grainCursorCount
                ? grainCursors[index].gain : 0.0f,
            std::memory_order_release);
        instance.grainCursorSourcePositions[index].store(
            index < grainCursorCount
                ? grainCursors[index].sourcePositionNormalized : 0.0f,
            std::memory_order_release);
        instance.grainCursorLanePositions[index].store(
            index < grainCursorCount
                ? grainCursors[index].lanePositionNormalized : 0.0f,
            std::memory_order_release);
        instance.grainCursorPathClockPhases[index].store(
            index < grainCursorCount
                ? grainCursors[index].pathClockPhase : 0.0f,
            std::memory_order_release);
        for (std::size_t lane = 0u;
             lane < s3g::sample::kSampleLaneCount; ++lane) {
            instance.grainCursorLaneSourcePositions[index][lane].store(
                index < grainCursorCount
                    ? grainCursors[index].laneSourcePositions[lane] : -1.0f,
                std::memory_order_release);
            instance.grainCursorLaneSourceSpans[index][lane].store(
                index < grainCursorCount
                    ? grainCursors[index].laneSourceSpans[lane] : 0.0f,
                std::memory_order_release);
            instance.grainCursorLaneWeights[index][lane].store(
                index < grainCursorCount
                    ? grainCursors[index].laneWeights[lane] : 0.0f,
                std::memory_order_release);
        }
    }
    instance.grainCursorCount.store(grainCursorCount,
        std::memory_order_release);
#endif
    instance.activeVoiceCount.store(instance.engine.activeVoiceCount(),
        std::memory_order_release);
    instance.outputPeak.store(instance.engine.outputPeak(),
        std::memory_order_release);
    auto& output = process->audio_outputs[0u];
    output.constant_mask = 0u;
    for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
        const float* source = channel < instance.outputChannelCount
            ? instance.scratch[channel].data() : nullptr;
        if (output.data32 && output.data32[channel]) {
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                output.data32[channel][frame] = source ? source[frame] : 0.0f;
        } else if (output.data64 && output.data64[channel]) {
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                output.data64[channel][frame] = source ? source[frame] : 0.0;
        } else return CLAP_PROCESS_ERROR;
    }
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    serviceLoads(*self(plugin));
#else
    (void)plugin;
#endif
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{ return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!plugin || !info || isInput || index != 0u) return false;
    const uint32_t channels = self(plugin)->outputChannelCount;
    *info = {};
    info->id = 20u;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = channels;
    info->port_type = channels == 2u ? CLAP_PORT_STEREO : nullptr;
    info->in_place_pair = CLAP_INVALID_ID;
    std::snprintf(info->name, sizeof(info->name), "%s",
        channels == 2u ? "Stereo Out" : "32 Channel Out");
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet,
};

uint32_t audioPortsConfigCount(const clap_plugin_t*) { return 1u; }
bool audioPortsConfigGet(const clap_plugin_t* plugin, uint32_t index,
    clap_audio_ports_config_t* config)
{
    if (!plugin || !config || index != 0u) return false;
    const auto& instance = *self(plugin);
    *config = {};
    config->id = instance.outputConfigId;
    std::snprintf(config->name, sizeof(config->name), "%s",
        instance.outputChannelCount == 2u ? "Stereo" : "32 Channel");
    config->output_port_count = 1u;
    config->has_main_output = true;
    config->main_output_channel_count = instance.outputChannelCount;
    config->main_output_port_type = instance.outputChannelCount == 2u
        ? CLAP_PORT_STEREO : nullptr;
    return true;
}
bool audioPortsConfigSelect(const clap_plugin_t* plugin, clap_id id)
{ return plugin && id == self(plugin)->outputConfigId; }
const clap_plugin_audio_ports_config_t audioPortsConfig {
    audioPortsConfigCount, audioPortsConfigGet, audioPortsConfigSelect,
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{ return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || !isInput || index != 0u) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "MIDI In");
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet,
};

uint32_t noteNameCount(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    std::lock_guard<std::mutex> lock(instance.statusMutex);
    for (const auto& asset : instance.controlAssets) if (asset) return 1u;
    return 0u;
}

bool noteNameGet(const clap_plugin_t* plugin, uint32_t index,
    clap_note_name_t* noteName)
{
    if (!noteName || index != 0u || noteNameCount(plugin) == 0u)
        return false;
    *noteName = {};
    noteName->port = 0;
    noteName->channel = -1;
    noteName->key = static_cast<int16_t>(std::lround(
        paramValue(*self(plugin), kRootParamId)));
    std::snprintf(noteName->name, sizeof(noteName->name), "%s",
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
        "GRAINS SCAN ROOT");
#else
        "LANES ROOT");
#endif
    return true;
}

const clap_plugin_note_name_t noteNames {
    noteNameCount, noteNameGet,
};

#if defined(__APPLE__)

#include "s3g_sample_lanes_gui.inc"

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto& instance = *self(plugin);
    if (instance.guiView) return true;
    S3GSampleLanesView* view = [[S3GSampleLanesView alloc]
        initWithPlugin:&instance];
    if (!view) return false;
    instance.guiView = (__bridge_retained void*)view;
    if (!s3g::clap_gui::createResponsiveViewport(instance.guiViewport, view,
            kGuiWidth, kGuiHeight, 480u, 360u)) {
        void* owned = instance.guiView;
        instance.guiView = nullptr;
        (void)(__bridge_transfer NSView*)owned;
        return false;
    }
    return true;
}

void destroyGui(Plugin& instance)
{
    if (!instance.guiView) return;
    [(__bridge S3GSampleLanesView*)instance.guiView stopTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance.guiViewport,
        instance.guiView);
}

void guiDestroy(const clap_plugin_t* plugin) { destroyGui(*self(plugin)); }
bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        480u, 360u);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{ return s3g::clap_gui::getResponsiveResizeHints(hints); }

bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        480u, 360u);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto& instance = *self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(instance.guiViewport,
        (__bridge NSView*)window->cocoa, instance.host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{ return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance.guiViewport, false)) return false;
    [(__bridge S3GSampleLanesView*)instance.guiView startTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView) return false;
    [(__bridge S3GSampleLanesView*)instance.guiView stopTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance.guiViewport, true);
}

const clap_plugin_gui_t gui {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};

#else

void destroyGui(Plugin&) {}

#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG) == 0)
        return &audioPortsConfig;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0) return &noteNames;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui;
#endif
    return nullptr;
}

const char* const stereoFeatures[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const char* const multichannelFeatures[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
const clap_plugin_descriptor_t stereoDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-cutups",
    "s3g Sample Cutups 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.1.0",
    "Four-file cut-up instrument with transient regions and per-file tempo analysis.",
    stereoFeatures,
};

const clap_plugin_descriptor_t multichannelDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-cutups-32",
    "s3g Sample Cutups 32",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.1.0",
    "Four-file cut-up instrument with transient regions and 32-channel allocation.",
    multichannelFeatures,
};
#elif defined(S3G_SAMPLE_GRAINS_VARIANT)
const clap_plugin_descriptor_t stereoDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-grains",
    "s3g Sample Grains 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.4.1",
    "Four-file granular instrument with shaped source-field paths and event mutation.",
    stereoFeatures,
};

const clap_plugin_descriptor_t multichannelDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-grains-32",
    "s3g Sample Grains 32",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.4.1",
    "Four-file granular instrument with field preservation or per-grain 32-channel allocation.",
    multichannelFeatures,
};
#else
const clap_plugin_descriptor_t stereoDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-lanes",
    "s3g Sample Lanes 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.4.0",
    "Four-file seamless loop instrument with shaped two-dimensional read-head paths.",
    stereoFeatures,
};

const clap_plugin_descriptor_t multichannelDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-lanes-32",
    "s3g Sample Lanes 32",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.4.0",
    "Four-file loop-path instrument with field-preserving or allocated 32-channel output.",
    multichannelFeatures,
};
#endif

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId) return nullptr;
    const clap_plugin_descriptor_t* descriptor = nullptr;
    uint32_t outputChannels = 0u;
    clap_id outputConfigId = CLAP_INVALID_ID;
    if (std::strcmp(pluginId, stereoDescriptor.id) == 0) {
        descriptor = &stereoDescriptor;
        outputChannels = 2u;
        outputConfigId = kStereoOutputConfigId;
    } else if (std::strcmp(pluginId, multichannelDescriptor.id) == 0) {
        descriptor = &multichannelDescriptor;
        outputChannels = 32u;
        outputConfigId = kThirtyTwoChannelOutputConfigId;
    } else return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->outputChannelCount = outputChannels;
    instance->outputConfigId = outputConfigId;
    for (auto& position : instance->cursorSourcePositions)
        position.store(-1.0f, std::memory_order_relaxed);
    instance->plugin.desc = descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = pluginInit;
    instance->plugin.destroy = pluginDestroy;
    instance->plugin.activate = pluginActivate;
    instance->plugin.deactivate = pluginDeactivate;
    instance->plugin.start_processing = pluginStartProcessing;
    instance->plugin.stop_processing = pluginStopProcessing;
    instance->plugin.reset = pluginReset;
    instance->plugin.process = pluginProcess;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = pluginOnMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 2u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    if (index == 0u) return &stereoDescriptor;
    if (index == 1u) return &multichannelDescriptor;
    return nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};


namespace {

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

const ParamDef* paramDef(clap_id id) noexcept
{
    for (const auto& def : kParamDefs) if (def.id == id) return &def;
    return nullptr;
}

std::size_t paramIndex(clap_id id) noexcept
{
    for (std::size_t index = 0u; index < kParamDefs.size(); ++index)
        if (kParamDefs[index].id == id) return index;
    return kParamCount;
}

double paramValue(const Plugin& instance, clap_id id) noexcept
{
    const auto index = paramIndex(id);
    return index < kParamCount
        ? instance.parameters[index].load(std::memory_order_acquire) : 0.0;
}

double clampParam(const ParamDef& def, double value) noexcept
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

void markStateDirty(Plugin& instance)
{
    if (instance.host && instance.hostState
        && instance.hostState->mark_dirty)
        instance.hostState->mark_dirty(instance.host);
}

void setParam(Plugin& instance, clap_id id, double value,
    bool dirty) noexcept
{
    const auto* def = paramDef(id);
    const auto index = paramIndex(id);
    if (!def || index >= kParamCount) return;
    value = clampParam(*def, value);
    constexpr double epsilon = 1.0e-5;
    if (id == kStartParamId)
        value = std::min(value, paramValue(instance, kEndParamId) - epsilon);
    else if (id == kEndParamId)
        value = std::max(value, paramValue(instance, kStartParamId) + epsilon);
    const double previous = instance.parameters[index].exchange(value,
        std::memory_order_acq_rel);
    if (previous != value) {
        instance.cursorRevision.fetch_add(1u, std::memory_order_release);
        if (dirty) markStateDirty(instance);
    }
}

void initializeParams(Plugin& instance) noexcept
{
    for (const auto& def : kParamDefs)
        instance.parameters[paramIndex(def.id)].store(def.defaultValue,
            std::memory_order_relaxed);
}

uint32_t manualPathSnapshot(const Plugin& instance,
    std::array<LanePathPoint, s3g::sample::kMaximumLanePathPoints>& points)
    noexcept
{
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t before = instance.manualPathRevision.load(
            std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const uint32_t count = std::min<uint32_t>(
            instance.manualPathPointCount.load(std::memory_order_acquire),
            static_cast<uint32_t>(points.size()));
        for (uint32_t index = 0u; index < count; ++index) {
            points[index].phase = instance.manualPathPhases[index].load(
                std::memory_order_relaxed);
            points[index].lane = instance.manualPathLanes[index].load(
                std::memory_order_relaxed);
        }
        const uint32_t after = instance.manualPathRevision.load(
            std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) return count;
    }
    return 0u;
}

void publishManualPath(Plugin& instance,
    const std::array<LanePathPoint,
        s3g::sample::kMaximumLanePathPoints>& requestedPoints,
    uint32_t pointCount, bool dirty) noexcept
{
    std::array<LanePathPoint, s3g::sample::kMaximumLanePathPoints> points {};
    pointCount = std::min<uint32_t>(pointCount,
        static_cast<uint32_t>(points.size()));
    if (pointCount >=
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
        1u
#else
        2u
#endif
        ) {
        std::copy_n(requestedPoints.begin(), pointCount, points.begin());
        for (uint32_t index = 0u; index < pointCount; ++index) {
            points[index].phase = std::clamp(
                std::isfinite(points[index].phase)
                    ? points[index].phase : 0.0f, 0.0f, 1.0f);
            points[index].lane = std::clamp(
                std::isfinite(points[index].lane)
                    ? points[index].lane : 0.0f, 0.0f, 1.0f);
        }
#if !defined(S3G_SAMPLE_CUTUPS_VARIANT)
        std::sort(points.begin(), points.begin() + pointCount,
            [](const LanePathPoint& first, const LanePathPoint& second) {
                return first.phase < second.phase;
            });
        points[0u].phase = 0.0f;
        points[pointCount - 1u].phase = 1.0f;
        constexpr float minimumGap = 0.0001f;
        uint32_t write = 1u;
        for (uint32_t read = 1u; read + 1u < pointCount; ++read) {
            if (points[read].phase <= points[write - 1u].phase + minimumGap
                || points[read].phase >= 1.0f - minimumGap) continue;
            points[write++] = points[read];
        }
        points[write++] = points[pointCount - 1u];
        pointCount = write;
#endif
    } else pointCount = 0u;

    instance.manualPathRevision.fetch_add(1u, std::memory_order_acq_rel);
    for (uint32_t index = 0u; index < pointCount; ++index) {
        instance.manualPathPhases[index].store(points[index].phase,
            std::memory_order_relaxed);
        instance.manualPathLanes[index].store(points[index].lane,
            std::memory_order_relaxed);
    }
    instance.manualPathPointCount.store(pointCount,
        std::memory_order_release);
    instance.manualPathRevision.fetch_add(1u, std::memory_order_release);
    instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    if (dirty) markStateDirty(instance);
    requestProcess(instance);
}

InstrumentSettings settingsSnapshot(const Plugin& instance) noexcept
{
    InstrumentSettings settings;
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    settings.clockBasis = static_cast<CutClockBasis>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kTransportParamId))));
    settings.division = static_cast<CutDivision>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kRateBasisParamId))));
    settings.regionMode = static_cast<CutRegionMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kShapeParamId))));
    settings.fileOrder = static_cast<CutFileOrder>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kPathParamId))));
    settings.sourceOrder = static_cast<CutSourceOrder>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kBlendParamId))));
    settings.outputMode = static_cast<CutOutputMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kOutputModeParamId))));
    settings.allocationCadence = static_cast<CutAllocationCadence>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kAllocationCadenceParamId))));
    settings.voiceMode = static_cast<VoiceMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kVoiceModeParamId))));
    settings.triggerMode = static_cast<TriggerMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kTriggerParamId))));
    settings.start = paramValue(instance, kStartParamId);
    settings.end = paramValue(instance, kEndParamId);
    settings.cutRateHz = static_cast<float>(paramValue(instance,
        kRateParamId));
    settings.swing = static_cast<float>(paramValue(instance, kCurveParamId));
    settings.timeVariation = static_cast<float>(paramValue(instance,
        kLaneSlewParamId));
    settings.gate = static_cast<float>(paramValue(instance,
        kManualLaneParamId));
    settings.joinMilliseconds = static_cast<float>(paramValue(instance,
        kLoopCrossfadeParamId));
    settings.regionCount = static_cast<uint32_t>(std::lround(
        paramValue(instance, kCyclesParamId)));
    settings.patternLength = static_cast<uint32_t>(std::lround(
        paramValue(instance, kOffsetParamId)));
    settings.repeatCount = static_cast<uint32_t>(std::lround(
        paramValue(instance, kSkewParamId)));
    settings.reverseChance = static_cast<float>(paramValue(instance,
        kCutReverseChanceParamId));
    settings.pitchVariationSemitones = static_cast<float>(paramValue(instance,
        kCutPitchVariationParamId));
    settings.levelVariation = static_cast<float>(paramValue(instance,
        kCutLevelVariationParamId));
    settings.outputGainDecibels = static_cast<float>(paramValue(instance,
        kOutParamId));
    settings.rootNote = static_cast<uint8_t>(std::lround(
        paramValue(instance, kRootParamId)));
    settings.tuneSemitones = static_cast<float>(paramValue(instance,
        kTuneParamId));
    settings.fineTuneCents = static_cast<float>(paramValue(instance,
        kFineParamId));
    settings.attackSeconds = static_cast<float>(paramValue(instance,
        kAttackParamId));
    settings.releaseSeconds = static_cast<float>(paramValue(instance,
        kReleaseParamId));
    settings.pan = static_cast<float>(paramValue(instance, kPanParamId));
    settings.velocitySensitivity = static_cast<float>(paramValue(instance,
        kVelocityParamId));
    settings.seed = static_cast<uint32_t>(std::lround(paramValue(instance,
        kSeedParamId)));
    settings.tempoSync = paramValue(instance, kTempoSyncParamId) >= 0.5;
    for (std::size_t lane = 0u; lane < settings.laneBpm.size(); ++lane)
        settings.laneBpm[lane] = static_cast<float>(paramValue(instance,
            laneBpmParamId(lane)));
    settings.activeOutputChannels = std::max<uint32_t>(2u,
        std::min<uint32_t>(instance.outputChannelCount,
            static_cast<uint32_t>(std::lround(paramValue(instance,
                kActiveOutputsParamId)))));
    settings.outputRouting.traversal
        = static_cast<s3g::routing::OutputTraversal>(static_cast<uint8_t>(
            std::lround(paramValue(instance, kTraversalParamId))));
    settings.outputRouting.width
        = static_cast<s3g::routing::OutputVoiceWidth>(static_cast<uint8_t>(
            std::lround(paramValue(instance, kOutputWidthParamId))));
    settings.outputRouting.pairLayout
        = static_cast<s3g::routing::StereoPairLayout>(static_cast<uint8_t>(
            std::lround(paramValue(instance, kPairLayoutParamId))));
    settings.outputRouting.avoidAdjacent
        = paramValue(instance, kAvoidAdjacentParamId) >= 0.5;
    std::array<LanePathPoint, s3g::sample::kMaximumLanePathPoints> points {};
    const uint32_t pointCount = manualPathSnapshot(instance, points);
    for (uint32_t index = 0u; index < pointCount
         && index < settings.manualPattern.size(); ++index) {
        settings.manualPattern[index].source = points[index].phase;
        settings.manualPattern[index].lane = static_cast<uint8_t>(std::lround(
            std::clamp(points[index].lane, 0.0f, 1.0f) * 3.0f));
    }
    if (instance.outputChannelCount == 2u) {
        settings.outputMode = CutOutputMode::Preserve;
        settings.activeOutputChannels = 2u;
        settings.outputRouting = {};
    }
    return settings;
#else
    settings.transport = static_cast<LaneTransport>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kTransportParamId))));
    settings.rateBasis = static_cast<LaneRateBasis>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kRateBasisParamId))));
    settings.path = static_cast<LanePath>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kPathParamId))));
    settings.blend = static_cast<LaneBlend>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kBlendParamId))));
    settings.pathShape = static_cast<LanePathShape>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kShapeParamId))));
    settings.voiceMode = static_cast<VoiceMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kVoiceModeParamId))));
    settings.triggerMode = static_cast<TriggerMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kTriggerParamId))));
    settings.start = paramValue(instance, kStartParamId);
    settings.end = paramValue(instance, kEndParamId);
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    settings.loopCrossfade = 0.0;
#else
    settings.loopCrossfade = paramValue(instance, kLoopCrossfadeParamId);
#endif
    settings.rate = static_cast<float>(paramValue(instance, kRateParamId));
    settings.pathCycles = static_cast<float>(
        paramValue(instance, kCyclesParamId));
    settings.pathOffset = static_cast<float>(
        paramValue(instance, kOffsetParamId));
    settings.pathSkew = static_cast<float>(
        paramValue(instance, kSkewParamId));
    settings.pathCurve = static_cast<float>(
        paramValue(instance, kCurveParamId));
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    settings.manualLane = 0.5f;
#else
    settings.manualLane = static_cast<float>(
        paramValue(instance, kManualLaneParamId));
#endif
    settings.manualPathPointCount = manualPathSnapshot(instance,
        settings.manualPathPoints);
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    settings.laneSlewSeconds = 0.0f;
#else
    settings.laneSlewSeconds = static_cast<float>(
        paramValue(instance, kLaneSlewParamId));
#endif
    settings.outputGainDecibels = static_cast<float>(
        paramValue(instance, kOutParamId));
    settings.rootNote = static_cast<uint8_t>(std::lround(
        paramValue(instance, kRootParamId)));
    settings.tuneSemitones = static_cast<float>(
        paramValue(instance, kTuneParamId));
    settings.fineTuneCents = static_cast<float>(
        paramValue(instance, kFineParamId));
    settings.attackSeconds = static_cast<float>(
        paramValue(instance, kAttackParamId));
    settings.releaseSeconds = static_cast<float>(
        paramValue(instance, kReleaseParamId));
    settings.pan = static_cast<float>(paramValue(instance, kPanParamId));
    settings.velocitySensitivity = static_cast<float>(
        paramValue(instance, kVelocityParamId));
    settings.seed = static_cast<uint32_t>(std::lround(
        paramValue(instance, kSeedParamId)));
    settings.outputMode = static_cast<LaneOutputMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kOutputModeParamId))));
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    settings.allocationCadence = LaneAllocationCadence::Note;
#else
    settings.allocationCadence = static_cast<LaneAllocationCadence>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kAllocationCadenceParamId))));
#endif
    settings.activeOutputChannels = std::min<uint32_t>(
        instance.outputChannelCount, static_cast<uint32_t>(std::lround(
            paramValue(instance, kActiveOutputsParamId))));
    settings.activeOutputChannels = std::max<uint32_t>(2u,
        settings.activeOutputChannels);
    settings.outputRouting.traversal
        = static_cast<s3g::routing::OutputTraversal>(static_cast<uint8_t>(
            std::lround(paramValue(instance, kTraversalParamId))));
    settings.outputRouting.width
        = static_cast<s3g::routing::OutputVoiceWidth>(static_cast<uint8_t>(
            std::lround(paramValue(instance, kOutputWidthParamId))));
    settings.outputRouting.pairLayout
        = static_cast<s3g::routing::StereoPairLayout>(static_cast<uint8_t>(
            std::lround(paramValue(instance, kPairLayoutParamId))));
    settings.outputRouting.avoidAdjacent
        = paramValue(instance, kAvoidAdjacentParamId) >= 0.5;
    for (std::size_t lane = 0u; lane < s3g::sample::kSampleLaneCount;
         ++lane) {
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
        settings.laneSpeed[lane] = 1.0f;
        settings.laneStretch[lane] = 1.0f;
        settings.laneNudge[lane] = 0.0f;
#else
        settings.laneSpeed[lane] = static_cast<float>(paramValue(
            instance, laneSpeedParamId(lane)));
        settings.laneStretch[lane] = static_cast<float>(paramValue(
            instance, laneStretchParamId(lane)));
        settings.laneNudge[lane] = static_cast<float>(paramValue(
            instance, laneNudgeParamId(lane)));
#endif
    }
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    settings.grainSourceMode = static_cast<GrainSourceMode>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kGrainSourceModeParamId))));
    settings.grainDensityHz = static_cast<float>(paramValue(
        instance, kGrainDensityParamId));
    settings.grainSizeMilliseconds = static_cast<float>(paramValue(
        instance, kGrainSizeParamId));
    settings.sourcePosition = static_cast<float>(paramValue(
        instance, kSourcePositionParamId));
    settings.positionSpray = static_cast<float>(paramValue(
        instance, kPositionSprayParamId));
    settings.pitchSpraySemitones = static_cast<float>(paramValue(
        instance, kPitchSprayParamId));
    settings.reverseChance = static_cast<float>(paramValue(
        instance, kReverseChanceParamId));
    settings.grainPitchSemitones = static_cast<float>(paramValue(
        instance, kGrainPitchShiftParamId));
    settings.grainSizeVariation = static_cast<float>(paramValue(
        instance, kGrainSizeVariationParamId));
    settings.grainLevelVariation = static_cast<float>(paramValue(
        instance, kGrainLevelVariationParamId));
    settings.timingScatter = static_cast<float>(paramValue(
        instance, kTimingScatterParamId));
    settings.envelopeSkew = static_cast<float>(paramValue(
        instance, kEnvelopeSkewParamId));
    settings.positionBias = static_cast<GrainPositionBias>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kPositionBiasParamId))));
    settings.sourceAdvance = static_cast<GrainSourceAdvance>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kSourceAdvanceParamId))));
    settings.grainEnvelope = static_cast<GrainEnvelope>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kGrainEnvelopeParamId))));
    settings.grainTiming = static_cast<GrainTiming>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kGrainTimingParamId))));
    settings.grainMutate = static_cast<GrainMutate>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kGrainMutateParamId))));
    settings.mutateAmount = static_cast<float>(paramValue(
        instance, kMutateAmountParamId));
    settings.regionCount = static_cast<uint32_t>(std::lround(paramValue(
        instance, kRegionCountParamId)));
    settings.sourceTimeSync = paramValue(instance,
        kSourceTimeSyncParamId) >= 0.5;
    if (instance.outputChannelCount == 2u) {
        settings.outputMode = LaneOutputMode::Preserve;
        settings.activeOutputChannels = 2u;
        settings.outputRouting = {};
        settings.channelMode = static_cast<GrainChannelMode>(
            static_cast<uint8_t>(std::lround(
                paramValue(instance, kChannelModeParamId))));
        settings.stereoLink = static_cast<GrainStereoLink>(
            static_cast<uint8_t>(std::lround(
                paramValue(instance, kStereoLinkParamId))));
        settings.monoSpread = static_cast<float>(paramValue(
            instance, kMonoSpreadParamId));
    }
#endif
    return settings;
#endif
}

void requestProcess(Plugin& instance) noexcept
{
    if (instance.host && instance.host->request_process)
        instance.host->request_process(instance.host);
}

void requestGuiParamService(Plugin& instance) noexcept
{
    requestProcess(instance);
    if (instance.hostParams && instance.hostParams->request_flush)
        instance.hostParams->request_flush(instance.host);
}

uint64_t regularFileByteCount(const std::string& path) noexcept
{
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(path, error))
        return 0u;
    const auto bytes = std::filesystem::file_size(path, error);
    return error ? 0u : static_cast<uint64_t>(bytes);
}

std::string sampleDisplayName(const std::string& path)
{
    if (path.empty()) return "NO SAMPLE";
    const std::string name = std::filesystem::path(path).filename().string();
    return name.empty() ? s3g::sample_storage::abbreviatedPath(path) : name;
}

std::shared_ptr<const SampleAsset> currentAsset(Plugin& instance,
    std::size_t lane)
{
    std::lock_guard<std::mutex> lock(instance.statusMutex);
    return lane < instance.controlAssets.size()
        ? instance.controlAssets[lane] : nullptr;
}

bool publishAsset(Plugin& instance, std::size_t lane,
    std::shared_ptr<const SampleAsset> asset, std::string path,
    bool dirty)
{
    if (lane >= instance.controlAssets.size()
        || (asset && (!asset->valid()
            || asset->channelCount > instance.outputChannelCount)))
        return false;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (asset) instance.retainedAssets.push_back(asset);
        instance.controlAssets[lane] = std::move(asset);
        instance.samplePaths[lane] = std::move(path);
        const std::string prefix = "LANE " + std::to_string(lane + 1u)
            + " / ";
        instance.statuses[lane] = instance.controlAssets[lane]
            ? prefix + std::to_string(
                instance.controlAssets[lane]->channelCount) + " CH / "
                + sampleDisplayName(instance.samplePaths[lane])
            : prefix + (instance.samplePaths[lane].empty()
                ? "DROP OR LOAD" : "OFFLINE / "
                    + sampleDisplayName(instance.samplePaths[lane]));
        instance.publishedAssets[lane].store(
            instance.controlAssets[lane].get(), std::memory_order_release);
    }
    instance.cursorCount.store(0u, std::memory_order_release);
    instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    requestProcess(instance);
    if (dirty) markStateDirty(instance);
    return true;
}

#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
bool publishMetadata(Plugin& instance, std::size_t lane,
    std::shared_ptr<const CutupsLaneMetadata> metadata)
{
    if (lane >= instance.controlMetadata.size()
        || (metadata && !metadata->valid())) return false;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (metadata) instance.retainedMetadata.push_back(metadata);
        instance.controlMetadata[lane] = std::move(metadata);
        instance.publishedMetadata[lane].store(
            instance.controlMetadata[lane].get(), std::memory_order_release);
    }
    instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    requestProcess(instance);
    return true;
}
#endif

void clearLane(Plugin& instance, std::size_t lane)
{
    if (lane >= s3g::sample::kSampleLaneCount) return;
#if defined(__APPLE__)
    ++instance.loadGenerations[lane];
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.erase(std::remove_if(
            instance.loadRequests.begin(), instance.loadRequests.end(),
            [lane](const LoadRequest& request) {
                return request.lane == lane;
            }), instance.loadRequests.end());
    }
#endif
    instance.projectFileRegistrations[lane].clear();
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.linkSourcePaths[lane].clear();
        instance.projectRelativePaths[lane].clear();
        instance.sourceFileBytes[lane] = 0u;
        instance.projectStoragePending[lane] = false;
        instance.projectCopyInFlight[lane] = false;
    }
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
    (void)publishMetadata(instance, lane, nullptr);
#endif
    (void)publishAsset(instance, lane, nullptr, "", true);
}

void queueGuiParamGesture(Plugin& instance, clap_id id, double value);

#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
std::shared_ptr<const CutupsLaneMetadata> analyzeCutupsLane(
    const SampleAsset& asset)
{
    return std::make_shared<CutupsLaneMetadata>(
        s3g::sample::analyzeCutupsAsset(asset));
}
#endif

#if defined(__APPLE__)
bool decodeSampleFile(const std::string& path,
    std::shared_ptr<const SampleAsset>& assetOut, std::string& error,
    uint32_t maximumChannels)
{
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSError* nsError = nil;
        AVAudioFile* file = nsPath ? [[AVAudioFile alloc]
            initForReading:[NSURL fileURLWithPath:nsPath] error:&nsError]
            : nil;
        if (!file) {
            error = "COULD NOT OPEN SAMPLE";
            return false;
        }
        AVAudioFormat* format = [file processingFormat];
        const AVAudioChannelCount channels = [format channelCount];
        const AVAudioFramePosition fileFrames = [file length];
        maximumChannels = std::clamp<uint32_t>(maximumChannels, 1u,
            static_cast<uint32_t>(s3g::sample::kMaximumAudioChannels));
        if (channels < 1u || channels > maximumChannels || fileFrames < 1
            || static_cast<uint64_t>(fileFrames)
                > std::numeric_limits<uint32_t>::max()) {
            error = maximumChannels <= 2u
                ? "USE A MONO OR STEREO SAMPLE UNDER 2^32 FRAMES"
                : "USE A 1-16 CHANNEL SAMPLE UNDER 2^32 FRAMES";
            return false;
        }
        const auto frames = static_cast<AVAudioFrameCount>(fileFrames);
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
            initWithPCMFormat:format frameCapacity:frames];
        if (!buffer || ![file readIntoBuffer:buffer error:&nsError]
            || [buffer frameLength] == 0u || ![buffer floatChannelData]) {
            error = "SAMPLE DECODE FAILED";
            return false;
        }
        auto asset = std::make_shared<SampleAsset>();
        asset->sampleRate = [format sampleRate];
        asset->channelCount = static_cast<uint8_t>(channels);
        const uint32_t decodedFrames = [buffer frameLength];
        for (AVAudioChannelCount channel = 0u; channel < channels; ++channel)
            asset->channels[channel].assign(
                [buffer floatChannelData][channel],
                [buffer floatChannelData][channel] + decodedFrames);
        if (!asset->valid()) {
            error = "DECODED SAMPLE IS INVALID";
            return false;
        }
        assetOut = std::move(asset);
        error.clear();
        return true;
    }
}

void loaderMain(Plugin* instance)
{
    for (;;) {
        LoadRequest request;
        {
            std::unique_lock<std::mutex> lock(instance->loaderMutex);
            instance->loaderCondition.wait(lock, [instance] {
                return instance->loaderStopping
                    || !instance->loadRequests.empty();
            });
            if (instance->loaderStopping) return;
            request = std::move(instance->loadRequests.front());
            instance->loadRequests.pop_front();
        }
        LoadResult result;
        result.generation = request.generation;
        result.lane = request.lane;
        result.sourcePath = std::move(request.path);
        result.decodedPath = result.sourcePath;
        result.copyOnly = request.copyOnly;
        result.projectLocationAvailable = request.projectLocation.available();
        result.error = std::move(request.projectError);
        if (request.projectLocation.available()) {
            result.projectCopy = s3g::sample_storage::copyFileIntoProject(
                request.projectLocation, result.sourcePath);
            if (result.projectCopy.success) {
                result.decodedPath = result.projectCopy.absolutePath;
                result.sourceFileBytes = result.projectCopy.byteCount;
            } else if (result.error.empty()) {
                result.error = result.projectCopy.error;
            }
        }
        if (result.sourceFileBytes == 0u)
            result.sourceFileBytes = regularFileByteCount(result.sourcePath);
        if (!result.copyOnly) {
            std::string decodeError;
            try {
                if (!decodeSampleFile(result.decodedPath, result.asset,
                        decodeError, instance->outputChannelCount))
                    result.asset.reset();
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
                if (result.asset)
                    result.metadata = analyzeCutupsLane(*result.asset);
#endif
            } catch (...) {
                result.asset.reset();
                decodeError = "SAMPLE DECODE EXCEEDED AVAILABLE MEMORY";
            }
            if (!result.asset) result.error = std::move(decodeError);
        }
        {
            std::lock_guard<std::mutex> lock(instance->loaderMutex);
            instance->loadResults.push_back(std::move(result));
        }
        if (instance->host && instance->host->request_callback)
            instance->host->request_callback(instance->host);
    }
}

bool startLoader(Plugin& instance)
{
    try { instance.loaderThread = std::thread(loaderMain, &instance); }
    catch (...) { return false; }
    return true;
}

void stopLoader(Plugin& instance)
{
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loaderStopping = true;
    }
    instance.loaderCondition.notify_all();
    if (instance.loaderThread.joinable()) instance.loaderThread.join();
}

void queueSampleLoad(Plugin& instance, std::size_t lane, std::string path)
{
    if (lane >= s3g::sample::kSampleLaneCount || path.empty()) return;
    LoadRequest request;
    request.generation = ++instance.loadGenerations[lane];
    request.lane = static_cast<uint8_t>(lane);
    request.path = path;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.linkSourcePaths[lane] = path;
        if (instance.storageMode == StorageMode::Project) {
            instance.reaperContext = s3g::sample_storage::reaperContext(
                instance.host);
            (void)s3g::sample_storage::queryProjectLocation(
                instance.reaperContext, request.projectLocation,
                &request.projectError);
            instance.projectStoragePending[lane] = true;
            instance.projectCopyInFlight[lane] = true;
        } else {
            instance.projectStoragePending[lane] = false;
            instance.projectCopyInFlight[lane] = false;
        }
        instance.statuses[lane] = "LANE " + std::to_string(lane + 1u)
            + " / DECODING...";
    }
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.erase(std::remove_if(
            instance.loadRequests.begin(), instance.loadRequests.end(),
            [lane](const LoadRequest& pending) {
                return pending.lane == lane;
            }), instance.loadRequests.end());
        instance.loadRequests.push_back(std::move(request));
    }
    instance.loaderCondition.notify_one();
}

void queueProjectCopy(Plugin& instance, std::size_t lane)
{
    if (lane >= s3g::sample::kSampleLaneCount) return;
    std::string path;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        path = !instance.linkSourcePaths[lane].empty()
            ? instance.linkSourcePaths[lane] : instance.samplePaths[lane];
        instance.projectStoragePending[lane] = !path.empty();
    }
    if (path.empty()) return;
    const ReaperContext context = s3g::sample_storage::reaperContext(
        instance.host);
    ProjectLocation location;
    std::string error;
    if (!s3g::sample_storage::queryProjectLocation(context, location,
            &error)) {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.statuses[lane] = "LANE " + std::to_string(lane + 1u)
            + " / PROJECT PENDING / SAVE PROJECT";
        instance.projectCopyInFlight[lane] = false;
        instance.nextProjectCopyProbe[lane]
            = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        return;
    }
    std::string absolute = path;
    if (!std::filesystem::path(absolute).is_absolute()) {
        if (!s3g::sample_storage::resolveProjectRelativePath(location, path,
                absolute, &error)) return;
    }
    std::string relative;
    if (s3g::sample_storage::makeProjectRelativePath(location, absolute,
            relative, &error) && regularFileByteCount(absolute) != 0u) {
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.samplePaths[lane] = absolute;
            instance.projectRelativePaths[lane] = relative;
            instance.projectStoragePending[lane] = false;
            instance.projectCopyInFlight[lane] = false;
            instance.statuses[lane] = "LANE " + std::to_string(lane + 1u)
                + " / PROJECT / " + sampleDisplayName(absolute);
        }
        (void)instance.projectFileRegistrations[lane].reset(context, absolute,
            nullptr, nullptr, instance.plugin.desc->name);
        markStateDirty(instance);
        return;
    }
    LoadRequest request;
    request.generation = ++instance.loadGenerations[lane];
    request.lane = static_cast<uint8_t>(lane);
    request.path = absolute;
    request.projectLocation = std::move(location);
    request.copyOnly = true;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.push_back(std::move(request));
    }
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.projectCopyInFlight[lane] = true;
        instance.statuses[lane] = "LANE " + std::to_string(lane + 1u)
            + " / COPYING TO PROJECT...";
    }
    instance.loaderCondition.notify_one();
}

void serviceLoads(Plugin& instance)
{
    std::deque<LoadResult> results;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        results.swap(instance.loadResults);
    }
    for (auto& result : results) {
        const std::size_t lane = result.lane;
        if (lane >= s3g::sample::kSampleLaneCount
            || result.generation != instance.loadGenerations[lane]) continue;
        StorageMode mode;
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            mode = instance.storageMode;
            instance.projectCopyInFlight[lane] = false;
        }
        if (result.copyOnly) {
            if (mode != StorageMode::Project
                || !result.projectCopy.success) {
                std::lock_guard<std::mutex> lock(instance.statusMutex);
                instance.statuses[lane] = "LANE "
                    + std::to_string(lane + 1u) + " / "
                    + (result.error.empty() ? "PROJECT COPY FAILED"
                        : result.error);
                instance.nextProjectCopyProbe[lane]
                    = std::chrono::steady_clock::now()
                        + std::chrono::seconds(1);
                continue;
            }
            const ReaperContext context = s3g::sample_storage::reaperContext(
                instance.host);
            {
                std::lock_guard<std::mutex> lock(instance.statusMutex);
                instance.samplePaths[lane] = result.projectCopy.absolutePath;
                instance.projectRelativePaths[lane]
                    = result.projectCopy.relativePath;
                instance.sourceFileBytes[lane] = result.projectCopy.byteCount;
                instance.projectStoragePending[lane] = false;
                instance.statuses[lane] = "LANE "
                    + std::to_string(lane + 1u) + " / PROJECT / "
                    + sampleDisplayName(result.projectCopy.absolutePath);
            }
            (void)instance.projectFileRegistrations[lane].reset(context,
                result.projectCopy.absolutePath, nullptr, nullptr,
                instance.plugin.desc->name);
            markStateDirty(instance);
            continue;
        }
        if (!result.asset) {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.statuses[lane] = "LANE " + std::to_string(lane + 1u)
                + " / " + (result.error.empty() ? "DECODE FAILED"
                    : result.error);
            continue;
        }
        std::string publishedPath = result.sourcePath;
        if (mode == StorageMode::Project && result.projectCopy.success) {
            publishedPath = result.projectCopy.absolutePath;
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.projectRelativePaths[lane]
                = result.projectCopy.relativePath;
            instance.projectStoragePending[lane] = false;
        }
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.sourceFileBytes[lane] = result.sourceFileBytes;
        }
        (void)publishAsset(instance, lane, result.asset, publishedPath, true);
#if defined(S3G_SAMPLE_CUTUPS_VARIANT)
        (void)publishMetadata(instance, lane, result.metadata);
        if (result.metadata && result.metadata->tempoValid) {
            if (result.metadata->tempoConfidence >= 0.62f
                && !result.metadata->tempoOctaveAmbiguous)
                setParam(instance, laneBpmParamId(lane),
                    result.metadata->analyzedBpm, true);
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            char tempoText[64] {};
            std::snprintf(tempoText, sizeof(tempoText), " / %.2f BPM%s",
                result.metadata->analyzedBpm,
                result.metadata->tempoOctaveAmbiguous ? "?" : "");
            instance.statuses[lane] += tempoText;
        }
#endif
        if (mode == StorageMode::Project) {
            const ReaperContext context = s3g::sample_storage::reaperContext(
                instance.host);
            if (result.projectCopy.success)
                (void)instance.projectFileRegistrations[lane].reset(context,
                    result.projectCopy.absolutePath, nullptr, nullptr,
                    instance.plugin.desc->name);
        }
    }

    // A Project-mode load may arrive before the host project has a writable
    // path. Keep playing the decoded source, then migrate it as soon as the
    // user saves the project instead of requiring a storage-mode toggle.
    std::array<bool, s3g::sample::kSampleLaneCount> retryProjectCopy {};
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (instance.storageMode == StorageMode::Project) {
            for (std::size_t lane = 0u;
                 lane < s3g::sample::kSampleLaneCount; ++lane) {
                if (instance.projectStoragePending[lane]
                    && !instance.projectCopyInFlight[lane]
                    && instance.controlAssets[lane]
                    && now >= instance.nextProjectCopyProbe[lane]) {
                    retryProjectCopy[lane] = true;
                    instance.nextProjectCopyProbe[lane]
                        = now + std::chrono::seconds(1);
                }
            }
        }
    }
    for (std::size_t lane = 0u; lane < retryProjectCopy.size(); ++lane)
        if (retryProjectCopy[lane]) queueProjectCopy(instance, lane);
}

void setStorageMode(Plugin& instance, StorageMode mode)
{
    mode = s3g::sample_storage::sanitizeStorageMode(
        static_cast<uint8_t>(mode));
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (instance.storageMode == mode) return;
        instance.storageMode = mode;
    }
    for (std::size_t lane = 0u; lane < s3g::sample::kSampleLaneCount;
         ++lane) {
        if (mode == StorageMode::Project) queueProjectCopy(instance, lane);
        else {
            instance.projectFileRegistrations[lane].clear();
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            const std::string external = !instance.linkSourcePaths[lane].empty()
                ? instance.linkSourcePaths[lane] : instance.samplePaths[lane];
            if (!external.empty()) instance.samplePaths[lane] = external;
            instance.projectRelativePaths[lane].clear();
            instance.statuses[lane] = "LANE " + std::to_string(lane + 1u)
                + " / " + s3g::sample_storage::storageModeName(mode)
                + " / " + sampleDisplayName(instance.samplePaths[lane]);
        }
    }
    markStateDirty(instance);
}
#endif

} // namespace
