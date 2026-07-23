#include "s3g_ambi_insect_encoder.h"
#include "s3g_ambi_insect_presets.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiInsectMaxChannels;
constexpr uint32_t kStateVersion = 4;
constexpr uint32_t kCustomPresetMagic = 0x31534e49u; // INS1
constexpr uint32_t kCustomPresetVersion = 4;

constexpr clap_id kPresetParamId = 1;
constexpr clap_id kOrderParamId = 2;
constexpr clap_id kVoicesParamId = 3;
constexpr clap_id kRegimeParamId = 4;
constexpr clap_id kActivityParamId = 5;
constexpr clap_id kTemperatureParamId = 6;
constexpr clap_id kVariationParamId = 7;
constexpr clap_id kCouplingParamId = 8;
constexpr clap_id kPhraseRateParamId = 9;
constexpr clap_id kChirpRateParamId = 10;
constexpr clap_id kPulseRateParamId = 11;
constexpr clap_id kCallLengthParamId = 12;
constexpr clap_id kRestParamId = 13;
constexpr clap_id kBodyPitchParamId = 14;
constexpr clap_id kBodySizeParamId = 15;
constexpr clap_id kRaspParamId = 16;
constexpr clap_id kWingParamId = 17;
constexpr clap_id kBrightnessParamId = 18;
constexpr clap_id kResonanceParamId = 19;
constexpr clap_id kAirParamId = 20;
constexpr clap_id kFieldRateParamId = 21;
constexpr clap_id kRoamParamId = 22;
constexpr clap_id kCohesionParamId = 23;
constexpr clap_id kScatterParamId = 24;
constexpr clap_id kOrbitParamId = 25;
constexpr clap_id kLiftParamId = 26;
constexpr clap_id kNearPassParamId = 27;
constexpr clap_id kInertiaParamId = 28;
constexpr clap_id kDirectionParamId = 29;
constexpr clap_id kElevationParamId = 30;
constexpr clap_id kRangeParamId = 31;
constexpr clap_id kOutputParamId = 32;
constexpr clap_id kPlaceParamId = 33;
constexpr clap_id kEnvironmentReturnParamId = 34;
constexpr clap_id kEnvironmentSizeParamId = 35;
constexpr clap_id kEnvironmentDecayParamId = 36;
constexpr clap_id kEnvironmentDampingParamId = 37;
constexpr clap_id kCallTypeParamId = 38;
constexpr clap_id kFieldListenModeParamId = 39;

constexpr size_t kStateV1ParamsSize =
    offsetof(s3g::AmbiInsectParams, callType);
constexpr size_t kStateV2ParamsSize =
    offsetof(s3g::AmbiInsectParams, sceneSeed);
constexpr size_t kStateV3ParamsSize =
    offsetof(s3g::AmbiInsectParams, fieldListenMode);
static_assert((kStateV1ParamsSize % alignof(uint32_t)) == 0u);
static_assert((kStateV2ParamsSize % alignof(uint32_t)) == 0u);
static_assert((kStateV3ParamsSize % alignof(uint32_t)) == 0u);

struct SavedStateV1 {
    uint32_t version = 1u;
    std::array<uint8_t, kStateV1ParamsSize> params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};
static_assert(offsetof(SavedStateV1, params) == sizeof(uint32_t));
static_assert(offsetof(SavedStateV1, presetIndex)
    == sizeof(uint32_t) + kStateV1ParamsSize);

struct SavedStateV2 {
    uint32_t version = 2u;
    std::array<uint8_t, kStateV2ParamsSize> params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};
static_assert(offsetof(SavedStateV2, params) == sizeof(uint32_t));
static_assert(offsetof(SavedStateV2, presetIndex)
    == sizeof(uint32_t) + kStateV2ParamsSize);

struct SavedStateV3 {
    uint32_t version = 3u;
    std::array<uint8_t, kStateV3ParamsSize> params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};
static_assert(offsetof(SavedStateV3, params) == sizeof(uint32_t));
static_assert(offsetof(SavedStateV3, presetIndex)
    == sizeof(uint32_t) + kStateV3ParamsSize);

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiInsectParams params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};

struct CustomPresetFile {
    uint32_t magic = kCustomPresetMagic;
    uint32_t version = kCustomPresetVersion;
    char name[64] {};
    s3g::AmbiInsectParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiInsectEncoder engine {};
    s3g::AmbiInsectParams params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    uint32_t randomSeed = 0x6d2b79f5u;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    std::array<std::atomic<float>, s3g::kAmbiInsectMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiInsectMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiInsectMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiInsectMaxVoices> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiInsectMaxVoices> guiCall {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiInsectMaxVoices> guiMethod {};
    std::array<std::atomic<float>, s3g::kAmbiFieldListenerMaxLobes>
        guiListenEnvelope {};
    std::array<std::atomic<float>, s3g::kAmbiFieldListenerMaxLobes>
        guiListenWeight {};
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->write(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->read(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool saveCustomPresetFile(const char* path, const Plugin& plugin, const char* name)
{
    if (!path || !*path) return false;
    CustomPresetFile file {};
    std::snprintf(file.name, sizeof(file.name), "%s", name && *name ? name : "Custom");
    file.params = plugin.params;
    FILE* handle = std::fopen(path, "wb");
    if (!handle) return false;
    const bool ok = std::fwrite(&file, 1, sizeof(file), handle) == sizeof(file);
    std::fclose(handle);
    return ok;
}

bool loadCustomPresetFile(const char* path, CustomPresetFile& file)
{
    if (!path || !*path) return false;
    FILE* handle = std::fopen(path, "rb");
    if (!handle) return false;
    file = {};
    bool ok = std::fread(&file.magic, 1, sizeof(file.magic), handle) == sizeof(file.magic)
        && std::fread(&file.version, 1, sizeof(file.version), handle) == sizeof(file.version)
        && file.magic == kCustomPresetMagic
        && (file.version >= 1u && file.version <= kCustomPresetVersion)
        && std::fread(file.name, 1, sizeof(file.name), handle) == sizeof(file.name);
    if (ok && file.version == 1u) {
        std::array<uint8_t, kStateV1ParamsSize> legacy {};
        ok = std::fread(legacy.data(), 1, legacy.size(), handle) == legacy.size();
        if (ok) std::memcpy(&file.params, legacy.data(), legacy.size());
    } else if (ok && file.version == 2u) {
        std::array<uint8_t, kStateV2ParamsSize> legacy {};
        ok = std::fread(legacy.data(), 1, legacy.size(), handle) == legacy.size();
        if (ok) std::memcpy(&file.params, legacy.data(), legacy.size());
    } else if (ok && file.version == 3u) {
        std::array<uint8_t, kStateV3ParamsSize> legacy {};
        ok = std::fread(legacy.data(), 1, legacy.size(), handle) == legacy.size();
        if (ok) std::memcpy(&file.params, legacy.data(), legacy.size());
    } else if (ok) {
        ok = std::fread(&file.params, 1, sizeof(file.params), handle) == sizeof(file.params);
    }
    std::fclose(handle);
    return ok;
}

constexpr const char* kRegimeNames[] = {
    "CHIRPERS", "TRILLERS", "CICADAS", "FLYERS", "TICKERS",
    "MIXED SWARM", "TREMULATORS"
};

constexpr const char* kCallTypeNames[] = {
    "CALLING SONG",
    "CONGREGATIONAL SONG",
    "RESPONSE CALL",
    "PREMATING SONG",
    "COURTSHIP SONG",
    "AGREEMENT SONG",
    "JUMPING SONG",
    "RIVALRY CALL",
    "POSTCOPULATORY CALL",
    "DEFENSIVE CALL",
    "FLIGHT NOISE",
};

constexpr const char* kProductionMethodNames[] = {
    "STRIDULATION",
    "STRIDULATION",
    "TYMBALISATION",
    "WING VIBRATION",
    "PERCUSSION",
    "MIXED PRODUCTION",
    "TREMULATION",
};

constexpr const char* kPlaceNames[] = {
    "MEADOW", "FOREST FLOOR", "CANOPY", "MARSH", "PORCH", "GREENHOUSE", "INTERIOR WALL"
};

struct MechanismLabels {
    const char* phraseRate;
    const char* chirpRate;
    const char* pulseRate;
    const char* callLength;
    const char* rest;
    const char* bodyPitch;
    const char* bodySize;
    const char* rasp;
    const char* wing;
    const char* brightness;
    const char* resonance;
    const char* air;
    bool showPulseRate;
};

constexpr std::array<MechanismLabels, s3g::kAmbiInsectRegimeCount> kMechanismLabels {{
    { "BOUT RATE", "CHIRP RATE", "TOOTH RATE", "CHIRP LENGTH", "INTERCALL",
        "WING RES", "BODY SIZE", "FILE ROUGH", "WING RAD", "BRIGHTNESS",
        "WING Q", "AIR RAD", true },
    { "BOUT RATE", "TRILL RATE", "TOOTH RATE", "TRILL LENGTH", "INTERCALL",
        "WING RES", "BODY SIZE", "FILE ROUGH", "WING RAD", "BRIGHTNESS",
        "WING Q", "AIR RAD", true },
    { "BOUT RATE", "BUCKLE TRAIN", "RIB RATE", "TRAIN LENGTH", "INTERCALL",
        "TYMBAL PITCH", "ABDOMEN SIZE", "BUCKLE NOISE", "TYMBAL PLATE",
        "SPECTRAL EDGE", "ABDOMEN Q", "AIR RAD", true },
    { "FLIGHT BOUT", "CALL MOD", "WING RATE", "BOUT LENGTH", "INTERBOUT",
        "WINGBEAT", "BODY MASS", "WAKE NOISE", "WING RAD", "PARTIALS",
        "THORAX Q", "FLIGHT AIR", false },
    { "BOUT RATE", "IMPACT RATE", "CONTACT RATE", "EVENT LENGTH", "INTEREVENT",
        "SHELL PITCH", "BODY MASS", "IMPACT HARD", "CONTACT", "BRIGHTNESS",
        "SHELL Q", "AIR CLICK", false },
    { "PHRASE RATE", "CALL RATE", "PULSE RATE", "CALL LENGTH", "REST",
        "BODY PITCH", "BODY SIZE", "ROUGHNESS", "RADIATION", "BRIGHTNESS",
        "RESONANCE", "AIR", true },
    { "BOUT RATE", "TREM RATE", "CONTACT RATE", "BOUT LENGTH", "INTERBOUT",
        "SUBSTRATE RES", "BODY MASS", "CONTACT NOISE", "BODY MOTION",
        "CONTACT TONE", "SUBSTRATE Q", "AIR LEAK", true },
}};

const MechanismLabels& mechanismLabels(uint32_t regime)
{
    return kMechanismLabels[
        std::min<uint32_t>(regime, s3g::kAmbiInsectRegimeCount - 1u)];
}

void randomizeSafe(Plugin& plugin)
{
    uint32_t seed = plugin.randomSeed ^ static_cast<uint32_t>(std::lround(plugin.outputPeak.load(std::memory_order_relaxed) * 1000000.0f));
    const auto fieldListenMode = plugin.params.fieldListenMode;
    auto p = s3g::ambiInsectCinematicRandomParams(seed);
    p.fieldListenMode = fieldListenMode;

    plugin.randomSeed = seed;
    plugin.params = p;
    plugin.presetIndex = 0u;
    std::snprintf(plugin.customPresetName, sizeof(plugin.customPresetName), "Random");
    plugin.engine.setParams(plugin.params);
    plugin.engine.beginTransition();
    plugin.params = plugin.engine.params();
}

bool assignParam(s3g::AmbiInsectParams& params, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: params.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kVoicesParamId: params.voices = static_cast<uint32_t>(std::lround(value)); return true;
    case kRegimeParamId: params.regime = static_cast<uint32_t>(std::lround(value)); return true;
    case kActivityParamId: params.activity = static_cast<float>(value); return true;
    case kTemperatureParamId: params.temperature = static_cast<float>(value); return true;
    case kVariationParamId: params.variation = static_cast<float>(value); return true;
    case kCouplingParamId: params.coupling = static_cast<float>(value); return true;
    case kPhraseRateParamId: params.phraseRateHz = static_cast<float>(value); return true;
    case kChirpRateParamId: params.chirpRateHz = static_cast<float>(value); return true;
    case kPulseRateParamId: params.pulseRateHz = static_cast<float>(value); return true;
    case kCallLengthParamId: params.callLength = static_cast<float>(value); return true;
    case kRestParamId: params.rest = static_cast<float>(value); return true;
    case kBodyPitchParamId: params.bodyPitchHz = static_cast<float>(value); return true;
    case kBodySizeParamId: params.bodySize = static_cast<float>(value); return true;
    case kRaspParamId: params.rasp = static_cast<float>(value); return true;
    case kWingParamId: params.wing = static_cast<float>(value); return true;
    case kBrightnessParamId: params.brightness = static_cast<float>(value); return true;
    case kResonanceParamId: params.resonance = static_cast<float>(value); return true;
    case kAirParamId: params.air = static_cast<float>(value); return true;
    case kFieldRateParamId: params.fieldRateHz = static_cast<float>(value); return true;
    case kRoamParamId: params.roam = static_cast<float>(value); return true;
    case kCohesionParamId: params.cohesion = static_cast<float>(value); return true;
    case kScatterParamId: params.scatter = static_cast<float>(value); return true;
    case kOrbitParamId: params.orbit = static_cast<float>(value); return true;
    case kLiftParamId: params.lift = static_cast<float>(value); return true;
    case kNearPassParamId: params.nearPass = static_cast<float>(value); return true;
    case kInertiaParamId: params.spatialFollow = static_cast<float>(value); return true;
    case kDirectionParamId: params.centerAzimuthDeg = static_cast<float>(value); return true;
    case kElevationParamId: params.centerElevationDeg = static_cast<float>(value); return true;
    case kRangeParamId: params.centerDistance = static_cast<float>(value); return true;
    case kOutputParamId: params.outputGainDb = static_cast<float>(value); return true;
    case kPlaceParamId: params.place = static_cast<uint32_t>(std::lround(value)); return true;
    case kEnvironmentReturnParamId: params.space = static_cast<float>(value); return true;
    case kEnvironmentSizeParamId: params.environmentSize = static_cast<float>(value); return true;
    case kEnvironmentDecayParamId: params.environmentDecay = static_cast<float>(value); return true;
    case kEnvironmentDampingParamId: params.environmentDamping = static_cast<float>(value); return true;
    case kCallTypeParamId: params.callType = static_cast<uint32_t>(std::lround(value)); return true;
    case kFieldListenModeParamId:
        params.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
            std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(value)), 0u, 3u));
        return true;
    default: return false;
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id == kPresetParamId) {
        p.presetIndex = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, s3g::kAmbiInsectFactoryPresetCount - 1u);
        p.customPresetName[0] = '\0';
        p.params = s3g::ambiInsectFactoryPreset(p.presetIndex);
        p.engine.setParams(p.params);
        p.engine.beginTransition();
        p.params = p.engine.params();
        return;
    }
    if (!assignParam(p.params, id, value)) return;
    p.engine.setParams(p.params);
    if (id == kOrderParamId || id == kVoicesParamId || id == kRegimeParamId
        || id == kCallTypeParamId || id == kPlaceParamId) {
        p.engine.beginTransition();
    }
    p.params = p.engine.params();
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p && p->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
#endif
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->engine.prepare(sampleRate);
    p->engine.setParams(p->params);
    p->params = p->engine.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->engine.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
#if defined(__APPLE__)
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiFieldListenerMaxLobes; ++lobe) {
        p->guiListenEnvelope[lobe].store(0.0f, std::memory_order_relaxed);
        p->guiListenWeight[lobe].store(1.0f, std::memory_order_relaxed);
    }
#endif
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            applyParam(p, param->param_id, param->value);
        }
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (output.data32) s3g::clearAudioBufferFromChannel(output, 0, frames);
    if (!output.data32 || outChannels == 0u) return CLAP_PROCESS_CONTINUE;

    std::array<float*, kOutputChannels> outputs {};
    for (uint32_t ch = 0u; ch < outChannels; ++ch) outputs[ch] = output.data32[ch];
    p->engine.setParams(p->params);
    p->engine.process(outputs.data(), outChannels, frames);
    p->params = p->engine.params();
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0u; ch < outChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[ch][frame]));
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
#if defined(__APPLE__)
    const uint32_t voices = std::min<uint32_t>(p->params.voices, s3g::kAmbiInsectMaxVoices);
    for (uint32_t voice = 0u; voice < voices; ++voice) {
        const auto point = p->engine.voicePoint(voice);
        p->guiAzimuth[voice].store(point.azimuthDeg, std::memory_order_relaxed);
        p->guiElevation[voice].store(point.elevationDeg, std::memory_order_relaxed);
        p->guiDistance[voice].store(point.distance, std::memory_order_relaxed);
        p->guiEnergy[voice].store(p->engine.voiceEnergy(voice), std::memory_order_relaxed);
        p->guiCall[voice].store(p->engine.voiceCallLevel(voice), std::memory_order_relaxed);
        p->guiMethod[voice].store(
            p->engine.voiceProductionMethod(voice),
            std::memory_order_relaxed);
    }
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiFieldListenerMaxLobes; ++lobe) {
        p->guiListenEnvelope[lobe].store(
            p->engine.fieldListenEnvelope(lobe), std::memory_order_relaxed);
        p->guiListenWeight[lobe].store(
            p->engine.fieldListenWeight(lobe), std::memory_order_relaxed);
    }
#endif
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    info->id = 20;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParams[] {
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiInsectFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, 64.0, 28.0, true },
    { kRegimeParamId, "Regime", 0.0, static_cast<double>(s3g::kAmbiInsectRegimeCount - 1u), 0.0, true },
    { kCallTypeParamId, "Call Type", 0.0, static_cast<double>(s3g::kAmbiInsectCallTypeCount - 1u), 0.0, true },
    { kActivityParamId, "Activity", 0.0, 1.0, 0.62, false },
    { kTemperatureParamId, "Temperature", 0.0, 1.0, 0.56, false },
    { kVariationParamId, "Variation", 0.0, 1.0, 0.22, false },
    { kCouplingParamId, "Coupling", 0.0, 1.0, 0.24, false },
    { kPhraseRateParamId, "Phrase Rate", 0.01, 8.0, 0.08, false },
    { kChirpRateParamId, "Chirp Rate", 0.2, 80.0, 1.43, false },
    { kPulseRateParamId, "Pulse Rate", 20.0, 8000.0, 58.0, false },
    { kCallLengthParamId, "Call Length", 0.0, 1.0, 0.12, false },
    { kRestParamId, "Rest", 0.0, 1.0, 0.30, false },
    { kBodyPitchParamId, "Body Pitch", 90.0, 14000.0, 4200.0, false },
    { kBodySizeParamId, "Body Size", 0.0, 1.0, 0.42, false },
    { kRaspParamId, "Rasp", 0.0, 1.0, 0.38, false },
    { kWingParamId, "Wing", 0.0, 1.0, 0.18, false },
    { kBrightnessParamId, "Brightness", 0.0, 1.0, 0.62, false },
    { kResonanceParamId, "Resonance", 0.0, 1.0, 0.58, false },
    { kAirParamId, "Air", 0.0, 1.0, 0.24, false },
    { kFieldRateParamId, "Field Rate", 0.001, 2.0, 0.035, false },
    { kRoamParamId, "Roam", 0.0, 1.0, 0.34, false },
    { kCohesionParamId, "Cohesion", 0.0, 1.0, 0.42, false },
    { kScatterParamId, "Scatter", 0.0, 1.0, 0.54, false },
    { kOrbitParamId, "Orbit", 0.0, 1.0, 0.16, false },
    { kLiftParamId, "Lift", 0.0, 1.0, 0.24, false },
    { kNearPassParamId, "Near Pass", 0.0, 1.0, 0.12, false },
    { kInertiaParamId, "Inertia", 0.0, 1.0, 0.72, false },
    { kDirectionParamId, "Direction", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Elevation", -90.0, 90.0, 0.0, false },
    { kRangeParamId, "Range", 0.15, 2.0, 1.0, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kPlaceParamId, "Place", 0.0, static_cast<double>(s3g::kAmbiInsectPlaceCount - 1u), 0.0, true },
    { kEnvironmentReturnParamId, "Env Return", 0.0, 1.0, 0.16, false },
    { kEnvironmentSizeParamId, "Env Size", 0.0, 1.0, 0.5, false },
    { kEnvironmentDecayParamId, "Env Decay", 0.0, 1.0, 0.5, false },
    { kEnvironmentDampingParamId, "Env Damping", 0.0, 1.0, 0.5, false },
    { kFieldListenModeParamId, "Field Listen Mode", 0.0, 3.0, 0.0, true },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

const char* paramModule(clap_id id)
{
    switch (id) {
    case kPresetParamId: return "Global";
    case kOrderParamId:
    case kVoicesParamId:
    case kRegimeParamId:
    case kCallTypeParamId:
    case kActivityParamId:
    case kTemperatureParamId:
    case kVariationParamId:
    case kCouplingParamId: return "Insect Source";
    case kPhraseRateParamId:
    case kChirpRateParamId:
    case kPulseRateParamId:
    case kCallLengthParamId:
    case kRestParamId: return "Call Structure";
    case kBodyPitchParamId:
    case kBodySizeParamId:
    case kRaspParamId:
    case kWingParamId:
    case kBrightnessParamId:
    case kResonanceParamId:
    case kAirParamId:
    case kOutputParamId: return "Body and Tone";
    case kFieldRateParamId:
    case kRoamParamId:
    case kCohesionParamId:
    case kScatterParamId:
    case kOrbitParamId:
    case kLiftParamId:
    case kNearPassParamId:
    case kInertiaParamId: return "Swarm Motion";
    case kDirectionParamId:
    case kElevationParamId:
    case kRangeParamId: return "Field Origin";
    case kPlaceParamId:
    case kEnvironmentReturnParamId:
    case kEnvironmentSizeParamId:
    case kEnvironmentDecayParamId:
    case kEnvironmentDampingParamId: return "Environment Field";
    case kFieldListenModeParamId: return "Field Listener";
    default: return "Ambi Insect Encoder";
    }
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, paramModule(def.id), sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    auto* p = self(plugin);
    const auto params = p->params;
    switch (id) {
    case kPresetParamId: *value = p->presetIndex; return true;
    case kOrderParamId: *value = params.order; return true;
    case kVoicesParamId: *value = params.voices; return true;
    case kRegimeParamId: *value = params.regime; return true;
    case kActivityParamId: *value = params.activity; return true;
    case kTemperatureParamId: *value = params.temperature; return true;
    case kVariationParamId: *value = params.variation; return true;
    case kCouplingParamId: *value = params.coupling; return true;
    case kPhraseRateParamId: *value = params.phraseRateHz; return true;
    case kChirpRateParamId: *value = params.chirpRateHz; return true;
    case kPulseRateParamId: *value = params.pulseRateHz; return true;
    case kCallLengthParamId: *value = params.callLength; return true;
    case kRestParamId: *value = params.rest; return true;
    case kBodyPitchParamId: *value = params.bodyPitchHz; return true;
    case kBodySizeParamId: *value = params.bodySize; return true;
    case kRaspParamId: *value = params.rasp; return true;
    case kWingParamId: *value = params.wing; return true;
    case kBrightnessParamId: *value = params.brightness; return true;
    case kResonanceParamId: *value = params.resonance; return true;
    case kAirParamId: *value = params.air; return true;
    case kFieldRateParamId: *value = params.fieldRateHz; return true;
    case kRoamParamId: *value = params.roam; return true;
    case kCohesionParamId: *value = params.cohesion; return true;
    case kScatterParamId: *value = params.scatter; return true;
    case kOrbitParamId: *value = params.orbit; return true;
    case kLiftParamId: *value = params.lift; return true;
    case kNearPassParamId: *value = params.nearPass; return true;
    case kInertiaParamId: *value = params.spatialFollow; return true;
    case kDirectionParamId: *value = params.centerAzimuthDeg; return true;
    case kElevationParamId: *value = params.centerElevationDeg; return true;
    case kRangeParamId: *value = params.centerDistance; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kPlaceParamId: *value = params.place; return true;
    case kEnvironmentReturnParamId: *value = params.space; return true;
    case kEnvironmentSizeParamId: *value = params.environmentSize; return true;
    case kEnvironmentDecayParamId: *value = params.environmentDecay; return true;
    case kEnvironmentDampingParamId: *value = params.environmentDamping; return true;
    case kCallTypeParamId: *value = params.callType; return true;
    case kFieldListenModeParamId:
        *value = static_cast<uint32_t>(params.fieldListenMode);
        return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, size, "%s", s3g::ambiInsectFactoryPresetInfo(static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kRegimeParamId) {
        std::snprintf(display, size, "%s", kRegimeNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiInsectRegimeCount - 1u)]);
    } else if (id == kCallTypeParamId) {
        std::snprintf(display, size, "%s", kCallTypeNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiInsectCallTypeCount - 1u)]);
    } else if (id == kPlaceParamId) {
        std::snprintf(display, size, "%s", kPlaceNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiInsectPlaceCount - 1u)]);
    } else if (id == kFieldListenModeParamId) {
        static constexpr const char* names[] {
            "OFF", "FOLLOW", "COUNTER", "BALANCE"
        };
        std::snprintf(display, size, "%s", names[std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 0u, 3u)]);
    } else if (id == kPhraseRateParamId || id == kFieldRateParamId) {
        std::snprintf(display, size, value < 0.1 ? "%.3f Hz" : "%.2f Hz", value);
    } else if (id == kChirpRateParamId) {
        std::snprintf(display, size, "%.2f Hz", value);
    } else if (id == kPulseRateParamId || id == kBodyPitchParamId) {
        std::snprintf(display, size, value < 1000.0 ? "%.1f Hz" : "%.2f kHz", value < 1000.0 ? value : value * 0.001);
    } else if (id == kDirectionParamId || id == kElevationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kActivityParamId || id == kTemperatureParamId || id == kVariationParamId
        || id == kCouplingParamId || id == kCallLengthParamId || id == kRestParamId
        || id == kBodySizeParamId || id == kRaspParamId || id == kWingParamId
        || id == kBrightnessParamId || id == kResonanceParamId || id == kAirParamId
        || id == kRoamParamId || id == kCohesionParamId || id == kScatterParamId
        || id == kOrbitParamId || id == kLiftParamId || id == kNearPassParamId
        || id == kInertiaParamId || id == kEnvironmentReturnParamId
        || id == kEnvironmentSizeParamId || id == kEnvironmentDecayParamId
        || id == kEnvironmentDampingParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else {
        std::snprintf(display, size, "%.2f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;

    if (id == kPresetParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiInsectFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiInsectFactoryPresetInfo(index).name) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    if (id == kRegimeParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiInsectRegimeCount; ++index) {
            if (std::strcmp(display, kRegimeNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kCallTypeParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiInsectCallTypeCount; ++index) {
            if (std::strcmp(display, kCallTypeNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kPlaceParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiInsectPlaceCount; ++index) {
            if (std::strcmp(display, kPlaceNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kFieldListenModeParamId) {
        static constexpr const char* names[] {
            "OFF", "FOLLOW", "COUNTER", "BALANCE"
        };
        for (uint32_t index = 0u; index < std::size(names); ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = index;
                return true;
            }
        }
    }

    *value = std::atof(display);
    if ((id == kPulseRateParamId || id == kBodyPitchParamId)
        && std::strstr(display, "kHz")) {
        *value *= 1000.0;
    }
    if (id == kActivityParamId || id == kTemperatureParamId || id == kVariationParamId
        || id == kCouplingParamId || id == kCallLengthParamId || id == kRestParamId
        || id == kBodySizeParamId || id == kRaspParamId || id == kWingParamId
        || id == kBrightnessParamId || id == kResonanceParamId || id == kAirParamId
        || id == kRoamParamId || id == kCohesionParamId || id == kScatterParamId
        || id == kOrbitParamId || id == kLiftParamId || id == kNearPassParamId
        || id == kInertiaParamId || id == kEnvironmentReturnParamId
        || id == kEnvironmentSizeParamId || id == kEnvironmentDecayParamId
        || id == kEnvironmentDampingParamId) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    SavedState state {};
    state.version = kStateVersion;
    state.params = p->params;
    state.presetIndex = p->presetIndex;
    std::snprintf(state.customPresetName, sizeof(state.customPresetName), "%s", p->customPresetName);
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    if (version == 1u) {
        SavedStateV1 state {};
        state.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(state.version),
                sizeof(state) - sizeof(state.version))) {
            return false;
        }
        p->params = {};
        std::memcpy(&p->params, state.params.data(), state.params.size());
        p->presetIndex = std::min<uint32_t>(
            state.presetIndex, s3g::kAmbiInsectFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName,
            sizeof(p->customPresetName), "%s", state.customPresetName);
    } else if (version == 2u) {
        SavedStateV2 state {};
        state.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(state.version),
                sizeof(state) - sizeof(state.version))) {
            return false;
        }
        p->params = {};
        std::memcpy(&p->params, state.params.data(), state.params.size());
        p->presetIndex = std::min<uint32_t>(
            state.presetIndex, s3g::kAmbiInsectFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName,
            sizeof(p->customPresetName), "%s", state.customPresetName);
    } else if (version == 3u) {
        SavedStateV3 state {};
        state.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(state.version),
                sizeof(state) - sizeof(state.version))) {
            return false;
        }
        p->params = {};
        std::memcpy(&p->params, state.params.data(), state.params.size());
        p->presetIndex = std::min<uint32_t>(
            state.presetIndex, s3g::kAmbiInsectFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName,
            sizeof(p->customPresetName), "%s", state.customPresetName);
    } else if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(state.version),
                sizeof(state) - sizeof(state.version))) {
            return false;
        }
        p->params = state.params;
        p->presetIndex = std::min<uint32_t>(
            state.presetIndex, s3g::kAmbiInsectFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName,
            sizeof(p->customPresetName), "%s", state.customPresetName);
    } else {
        return false;
    }
    p->engine.setParams(p->params);
    p->engine.beginTransition();
    p->params = p->engine.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

constexpr uint32_t kGuiWidth = 1160;
constexpr uint32_t kGuiHeight = 858;

struct GuiSliderSpec {
    clap_id id;
    CGFloat panelX;
    CGFloat y;
    double min;
    double max;
    bool logarithmic;
};

constexpr GuiSliderSpec kGuiSliders[] {
    { kVoicesParamId, 630, 156, 1.0, 64.0, false },
    { kActivityParamId, 630, 182, 0.0, 1.0, false },
    { kTemperatureParamId, 630, 208, 0.0, 1.0, false },
    { kVariationParamId, 630, 234, 0.0, 1.0, false },
    { kCouplingParamId, 630, 260, 0.0, 1.0, false },
    { kPhraseRateParamId, 630, 344, 0.01, 8.0, true },
    { kChirpRateParamId, 630, 370, 0.2, 80.0, true },
    { kPulseRateParamId, 630, 396, 20.0, 8000.0, true },
    { kCallLengthParamId, 630, 422, 0.0, 1.0, false },
    { kRestParamId, 630, 448, 0.0, 1.0, false },
    { kBodyPitchParamId, 630, 532, 90.0, 14000.0, true },
    { kBodySizeParamId, 630, 558, 0.0, 1.0, false },
    { kRaspParamId, 630, 584, 0.0, 1.0, false },
    { kWingParamId, 630, 610, 0.0, 1.0, false },
    { kBrightnessParamId, 630, 636, 0.0, 1.0, false },
    { kResonanceParamId, 630, 662, 0.0, 1.0, false },
    { kAirParamId, 630, 688, 0.0, 1.0, false },
    { kOutputParamId, 630, 714, -60.0, 12.0, false },
    { kFieldRateParamId, 896, 78, 0.001, 2.0, true },
    { kRoamParamId, 896, 104, 0.0, 1.0, false },
    { kCohesionParamId, 896, 130, 0.0, 1.0, false },
    { kScatterParamId, 896, 156, 0.0, 1.0, false },
    { kOrbitParamId, 896, 182, 0.0, 1.0, false },
    { kLiftParamId, 896, 208, 0.0, 1.0, false },
    { kNearPassParamId, 896, 234, 0.0, 1.0, false },
    { kInertiaParamId, 896, 260, 0.0, 1.0, false },
    { kDirectionParamId, 896, 370, -180.0, 180.0, false },
    { kElevationParamId, 896, 396, -90.0, 90.0, false },
    { kRangeParamId, 896, 422, 0.15, 2.0, false },
    { kEnvironmentReturnParamId, 896, 558, 0.0, 1.0, false },
    { kEnvironmentSizeParamId, 896, 584, 0.0, 1.0, false },
    { kEnvironmentDecayParamId, 896, 610, 0.0, 1.0, false },
    { kEnvironmentDampingParamId, 896, 636, 0.0, 1.0, false },
};

const GuiSliderSpec* guiSliderSpec(clap_id id)
{
    for (const auto& spec : kGuiSliders) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

bool guiParamVisible(uint32_t regime, clap_id id)
{
    return id != kPulseRateParamId || mechanismLabels(regime).showPulseRate;
}

CGFloat guiSliderY(uint32_t regime, const GuiSliderSpec& spec)
{
    if (!mechanismLabels(regime).showPulseRate
        && (spec.id == kCallLengthParamId || spec.id == kRestParamId)) {
        return spec.y - 26.0;
    }
    return spec.y;
}

double sliderNorm(const GuiSliderSpec& spec, double value)
{
    if (spec.logarithmic) {
        if (spec.min <= 0.0) {
            if (value <= 0.0) return 0.0;
            constexpr double zeroZone = 0.02;
            const double minPositive = std::max(0.000001, spec.max * 0.001);
            const double logNorm = std::log(std::max(minPositive, value) / minPositive)
                / std::log(spec.max / minPositive);
            return std::clamp(zeroZone + (1.0 - zeroZone) * logNorm, zeroZone, 1.0);
        }
        const double minValue = std::max(0.000001, spec.min);
        return std::clamp(std::log(std::max(minValue, value) / minValue) / std::log(spec.max / minValue), 0.0, 1.0);
    }
    return std::clamp((value - spec.min) / std::max(0.000001, spec.max - spec.min), 0.0, 1.0);
}

double sliderValue(const GuiSliderSpec& spec, NSPoint point)
{
    const double norm = std::clamp((static_cast<double>(point.x) - (spec.panelX + 108.0)) / 82.0, 0.0, 1.0);
    if (spec.logarithmic) {
        if (spec.min <= 0.0) {
            constexpr double zeroZone = 0.02;
            if (norm <= zeroZone) return 0.0;
            const double minPositive = std::max(0.000001, spec.max * 0.001);
            const double logNorm = (norm - zeroZone) / (1.0 - zeroZone);
            return minPositive * std::pow(spec.max / minPositive, logNorm);
        }
        return spec.min * std::pow(spec.max / spec.min, norm);
    }
    return spec.min + norm * (spec.max - spec.min);
}

@interface S3GAmbiInsectEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    uint32_t _selectedVoice;
    int _dragParam;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    int _fieldPage;
    int _dragBreakpointRow;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSRect _openMenuRect;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiInsectEncoderView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _selectedVoice = 0;
        _dragParam = 0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _fieldPage = 0;
        _dragBreakpointRow = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _openMenuRect = NSZeroRect;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)timerTick:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 608); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 558); }
- (NSRect)presetMenuRect { return NSMakeRect(382, 13, 190, 15); }
- (NSRect)savePresetButtonRect { return NSMakeRect(580, 13, 46, 15); }
- (NSRect)loadPresetButtonRect { return NSMakeRect(632, 13, 46, 15); }
- (NSRect)randomizeButtonRect { return NSMakeRect(684, 13, 66, 15); }
- (NSRect)fieldListenModeRect:(int)mode
{
    return NSMakeRect(1004.0 + mode * 32.0, 678.0, 29.0, 13.0);
}

- (NSRect)pageButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(panel.origin.x + 104.0 + index * 48.0, panel.origin.y + 4.0, 43.0, 13.0);
}

- (NSRect)viewButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    const CGFloat w = 38.0;
    const CGFloat gap = 5.0;
    return NSMakeRect(NSMaxX(panel) - 10.0 - (3.0 - index) * w - (2.0 - index) * gap, panel.origin.y + 4.0, w, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    const CGFloat w = 18.0;
    const CGFloat gap = 4.0;
    const CGFloat x = [self viewButtonRect:0].origin.x - 12.0 - (2.0 - index) * w - (1.0 - index) * gap;
    return NSMakeRect(x, [self fieldPanelRect].origin.y + 4.0, w, 13.0);
}

- (s3g::Vec3)voiceWorld:(uint32_t)voice
{
    if (!_plugin || voice >= s3g::kAmbiInsectMaxVoices) return { 0.0f, 0.0f, 0.0f };
    const float az = _plugin->guiAzimuth[voice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[voice].load(std::memory_order_relaxed);
    const float dist = _plugin->guiDistance[voice].load(std::memory_order_relaxed);
    const s3g::Vec3 dir = s3g::directionFromAed(az, el);
    return { dir.x * dist, dir.y * dist, dir.z * dist };
}

- (NSPoint)projectWorld:(s3g::Vec3)point depth:(CGFloat*)depth
{
    const NSRect field = [self fieldRect];
    const CGFloat scale = std::min(field.size.width, field.size.height) * 0.36 * std::clamp(_viewZoom, 0.55, 2.20);
    const CGFloat centerX = NSMidX(field);
    const CGFloat centerY = NSMidY(field);
    const float azimuth = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 - se * point.z;
    const float z2 = se * y1 + ce * point.z;
    if (depth) *depth = z2;
    return NSMakePoint(centerX + x1 * scale, centerY - y2 * scale);
}

- (NSPoint)projectVoice:(uint32_t)voice depth:(CGFloat*)depth
{
    return [self projectWorld:[self voiceWorld:voice] depth:depth];
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 0.0;
        _viewElDeg = -90.0;
    } else {
        _viewAzDeg = 38.0;
        _viewElDeg = 32.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (NSString*)customPresetDirectory
{
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Music/s3g/Presets/Ambi Insect Encoder"];
}

- (void)saveCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3ginsect" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3ginsect", [self presetDisplayName]]];
    if ([panel runModal] != NSModalResponseOK) return;
    NSString* name = [[[[panel URL] lastPathComponent] stringByDeletingPathExtension] copy];
    if (saveCustomPresetFile([[[panel URL] path] UTF8String], *_plugin, [name UTF8String])) {
        std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName), "%s", [name UTF8String]);
    }
    [name release];
    [self setNeedsDisplay:YES];
}

- (void)loadCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3ginsect" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    if ([panel runModal] != NSModalResponseOK) return;
    CustomPresetFile file {};
    if (!loadCustomPresetFile([[[panel URL] path] UTF8String], file)) return;
    _plugin->params = file.params;
    _plugin->engine.setParams(_plugin->params);
    _plugin->engine.beginTransition();
    _plugin->params = _plugin->engine.params();
    std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName), "%s", file.name[0] ? file.name : "Custom");
    [self setNeedsDisplay:YES];
}

- (NSColor*)voiceColor:(uint32_t)voice selected:(BOOL)selected
{
    const float el = _plugin->guiElevation[voice].load(std::memory_order_relaxed);
    const uint32_t method =
        _plugin->guiMethod[voice].load(std::memory_order_relaxed);
    static constexpr float hues[] = {
        0.31f, 0.14f, 0.03f, 0.52f, 0.64f, 0.82f
    };
    const float hue = hues[std::min<uint32_t>(
        method, s3g::kAmbiInsectProductionMethodCount - 1u)];
    const float sat = selected ? 0.76f : 0.58f;
    const float bri = selected ? 0.98f : 0.70f + std::max(0.0f, el) / 90.0f * 0.20f;
    return [NSColor colorWithCalibratedHue:hue saturation:sat brightness:bri alpha:selected ? 1.0 : 0.84];
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    _fieldPage = 0;
    s3g::clap_gui::drawPanelHeader(@"INSECT FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:0], header, @"FIELD", _fieldPage == 0, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) s3g::clap_gui::drawHeaderButton([self viewButtonRect:i], header, labels[i], i == _viewMode, attrs, style);
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1, 1)] addClip];
    const CGFloat radius = std::min(field.size.width, field.size.height) * 0.36 * _viewZoom;
    [s3g::clap_gui::color(0x303030) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMidX(field) - radius, NSMidY(field) - radius, radius * 2.0, radius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];
    [s3g::clap_gui::color(0x242424) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMinX(field) + 18, NSMidY(field)) toPoint:NSMakePoint(NSMaxX(field) - 18, NSMidY(field))];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMidX(field), NSMinY(field) + 18) toPoint:NSMakePoint(NSMidX(field), NSMaxY(field) - 18)];

    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiInsectMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    std::array<NSPoint, s3g::kAmbiInsectMaxVoices> projected {};
    for (uint32_t voice = 0; voice < voices; ++voice) projected[voice] = [self projectVoice:voice depth:nullptr];
    [s3g::clap_gui::color(0x5c5c5c, 0.18) setStroke];
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const uint32_t next = (voice + 1u) % voices;
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:projected[voice]];
        [path lineToPoint:projected[next]];
        [path setLineWidth:0.55];
        [path stroke];
    }
    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x080808), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const BOOL selected = voice == _selectedVoice;
        const float energy = _plugin->guiEnergy[voice].load(std::memory_order_relaxed);
        const float call = std::clamp(_plugin->guiCall[voice].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float activity = std::clamp(std::sqrt(std::max(0.0f, energy)) * 18.0f, 0.0f, 1.0f);
        const CGFloat base = voices > 32u ? 7.0 : 9.0;
        const CGFloat size = (selected ? base + 5.0 : base) + activity * 5.0f + call * 7.0f;
        const NSRect marker = NSMakeRect(projected[voice].x - size * 0.5, projected[voice].y - size * 0.5, size, size);
        if (call > 0.04f || activity > 0.04f) {
            const CGFloat halo = size * (1.15 + call * 1.9 + activity * 0.6);
            NSRect haloRect = NSMakeRect(projected[voice].x - halo * 0.5, projected[voice].y - halo * 0.5, halo, halo);
            [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:0.05 + call * 0.18 + activity * 0.08] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:haloRect] fill];
        }
        [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:(selected ? 0.98 : 0.22 + call * 0.54 + activity * 0.20)] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xe6e6e6 : 0x4f4f4f, selected ? 1.0 : 0.22 + call * 0.46 + activity * 0.18) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        if (call > 0.28f || selected) {
            [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5, NSMidY(marker) - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
        }
    }
    [NSGraphicsContext restoreGraphicsState];

    const float az = _plugin->guiAzimuth[_selectedVoice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[_selectedVoice].load(std::memory_order_relaxed);
    const float dist = _plugin->guiDistance[_selectedVoice].load(std::memory_order_relaxed);
    const float energy = _plugin->guiEnergy[_selectedVoice].load(std::memory_order_relaxed);
    const float call = _plugin->guiCall[_selectedVoice].load(std::memory_order_relaxed);
    NSString* readout = [NSString stringWithFormat:@"V%02u  AZ%+.1f  EL%+.1f  D%.2f  CALL%.2f  E%.3f", _selectedVoice + 1u, az, el, dist, call, energy];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
    [@"CALL ACTIVITY + SWARM MOTION     DIRECT ACN/SN3D" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19) withAttributes:valueAttrs];
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    char display[64] {};
    paramsValueToText(nullptr, param, value, display, sizeof(display));
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:display],
        sliderNorm(*spec, value), guiSliderY(_plugin->params.regime, *spec),
        attrs, valueAttrs, style, spec->panelX + 16, spec->panelX + 108, spec->panelX + 196, 82);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value panelX:(CGFloat)panelX y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16, panelX + 108, 124);
}

- (NSString*)presetDisplayName
{
    if (_plugin->customPresetName[0]) return [NSString stringWithFormat:@"CUSTOM: %s", _plugin->customPresetName];
    return [NSString stringWithUTF8String:s3g::ambiInsectFactoryPresetInfo(_plugin->presetIndex).name];
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _plugin->params;
    const uint32_t regime = std::min<uint32_t>(
        p.regime, s3g::kAmbiInsectRegimeCount - 1u);
    const auto& labels = mechanismLabels(regime);
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 254, style);
    s3g::clap_gui::drawPanelHeader(@"INSECT SOURCE", true, 630, 42, 250, 21, attrs, style);
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] panelX:630 y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"REGIME" value:[NSString stringWithUTF8String:kRegimeNames[
        regime]] panelX:630 y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"CALL TYPE" value:[NSString stringWithUTF8String:kCallTypeNames[
        std::min<uint32_t>(p.callType, s3g::kAmbiInsectCallTypeCount - 1u)]]
        panelX:630 y:130 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:p.voices attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ACTIVITY" param:kActivityParamId value:p.activity attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TEMPERATURE" param:kTemperatureParamId value:p.temperature attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VARIATION" param:kVariationParamId value:p.variation attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"COUPLING" param:kCouplingParamId value:p.coupling attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 308, 250, 176, style);
    s3g::clap_gui::drawPanelHeader(@"CALL STRUCTURE", true, 630, 308, 250, 21, attrs, style);
    [self drawSlider:[NSString stringWithUTF8String:labels.phraseRate] param:kPhraseRateParamId value:p.phraseRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:[NSString stringWithUTF8String:labels.chirpRate] param:kChirpRateParamId value:p.chirpRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    if (labels.showPulseRate) {
        [self drawSlider:[NSString stringWithUTF8String:labels.pulseRate] param:kPulseRateParamId value:p.pulseRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    }
    [self drawSlider:[NSString stringWithUTF8String:labels.callLength] param:kCallLengthParamId value:p.callLength attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:[NSString stringWithUTF8String:labels.rest] param:kRestParamId value:p.rest attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 496, 250, 252, style);
    NSString* modelTitle = [NSString stringWithFormat:@"%s MODEL",
        kProductionMethodNames[regime]];
    s3g::clap_gui::drawPanelHeader(modelTitle, true, 630, 496, 250, 21, attrs, style);
    [self drawSlider:[NSString stringWithUTF8String:labels.bodyPitch] param:kBodyPitchParamId value:p.bodyPitchHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:[NSString stringWithUTF8String:labels.bodySize] param:kBodySizeParamId value:p.bodySize attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:[NSString stringWithUTF8String:labels.rasp] param:kRaspParamId value:p.rasp attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:[NSString stringWithUTF8String:labels.wing] param:kWingParamId value:p.wing attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:[NSString stringWithUTF8String:labels.brightness] param:kBrightnessParamId value:p.brightness attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:[NSString stringWithUTF8String:labels.resonance] param:kResonanceParamId value:p.resonance attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:[NSString stringWithUTF8String:labels.air] param:kAirParamId value:p.air attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"OUTPUT" param:kOutputParamId value:p.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 280, style);
    s3g::clap_gui::drawPanelHeader(@"SWARM MOTION", true, 896, 42, 246, 21, attrs, style);
    [self drawSlider:@"FIELD RATE" param:kFieldRateParamId value:p.fieldRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ROAM" param:kRoamParamId value:p.roam attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"COHESION" param:kCohesionParamId value:p.cohesion attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SCATTER" param:kScatterParamId value:p.scatter attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ORBIT" param:kOrbitParamId value:p.orbit attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"LIFT" param:kLiftParamId value:p.lift attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"NEAR PASS" param:kNearPassParamId value:p.nearPass attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"INERTIA" param:kInertiaParamId value:p.spatialFollow attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 334, 246, 150, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD ORIGIN", true, 896, 334, 246, 21, attrs, style);
    [self drawSlider:@"DIRECTION" param:kDirectionParamId value:p.centerAzimuthDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ELEVATION" param:kElevationParamId value:p.centerElevationDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RANGE" param:kRangeParamId value:p.centerDistance attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 496, 246, 166, style);
    s3g::clap_gui::drawPanelHeader(@"ENVIRONMENT FIELD", true, 896, 496, 246, 21, attrs, style);
    [self drawMenu:@"PLACE" value:[NSString stringWithUTF8String:kPlaceNames[
        std::min<uint32_t>(p.place, s3g::kAmbiInsectPlaceCount - 1u)]] panelX:896 y:532 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV RETURN" param:kEnvironmentReturnParamId value:p.space attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV SIZE" param:kEnvironmentSizeParamId value:p.environmentSize attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV DECAY" param:kEnvironmentDecayParamId value:p.environmentDecay attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV DAMPING" param:kEnvironmentDampingParamId value:p.environmentDamping attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 674, 246, 74, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD LISTENER", true, 896, 674, 246, 21, attrs, style);
    const NSRect listenHeader = NSMakeRect(896, 674, 246, 21);
    static NSString* listenModeLabels[] = { @"OFF", @"FOL", @"CTR", @"BAL" };
    for (int mode = 0; mode < 4; ++mode) {
        s3g::clap_gui::drawHeaderButton([self fieldListenModeRect:mode],
            listenHeader, listenModeLabels[mode],
            static_cast<int>(p.fieldListenMode) == mode, attrs, style);
    }
    [@"HEARD" drawAtPoint:NSMakePoint(912, 704) withAttributes:attrs];
    [@"GAIN" drawAtPoint:NSMakePoint(912, 726) withAttributes:attrs];
    float maximumEnvelope = 0.0f;
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiFieldListenerMaxLobes; ++lobe) {
        maximumEnvelope = std::max(maximumEnvelope,
            _plugin->guiListenEnvelope[lobe].load(std::memory_order_relaxed));
    }
    const bool listening =
        p.fieldListenMode != s3g::AmbiFieldListenMode::Off;
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiFieldListenerMaxLobes; ++lobe) {
        const CGFloat x = 1004.0 + lobe * 15.4;
        const float heard = maximumEnvelope > 1.0e-7f
            ? std::clamp(
                _plugin->guiListenEnvelope[lobe].load(std::memory_order_relaxed)
                    / maximumEnvelope,
                0.0f, 1.0f)
            : 0.0f;
        const float weight = _plugin->guiListenWeight[lobe].load(
            std::memory_order_relaxed);
        const float weightNorm =
            std::clamp((weight - 0.40f) / 2.10f, 0.0f, 1.0f);
        const NSRect heardMeter = NSMakeRect(x, 708, 12, 7);
        const NSRect gainMeter = NSMakeRect(x, 730, 12, 7);
        [s3g::clap_gui::color(0x303030) setFill];
        NSRectFill(heardMeter);
        NSRectFill(gainMeter);
        [s3g::clap_gui::color(
            listening ? 0xd8d8d8 : 0x747474,
            0.24 + heard * 0.76) setFill];
        NSRectFill(NSMakeRect(
            heardMeter.origin.x, heardMeter.origin.y,
            heardMeter.size.width * heard, heardMeter.size.height));
        [s3g::clap_gui::color(
            listening ? 0xe0e0e0 : 0x686868,
            listening ? 0.88 : 0.38) setFill];
        NSRectFill(NSMakeRect(
            gainMeter.origin.x, gainMeter.origin.y,
            gainMeter.size.width * weightNorm, gainMeter.size.height));
    }
}

- (NSRect)menuBoxRect:(int)menu
{
    switch (menu) {
    case 1: return [self presetMenuRect];
    case 2: return NSMakeRect(738, 77, 124, 15);
    case 3: return NSMakeRect(738, 103, 124, 15);
    case 4: return NSMakeRect(738, 129, 124, 15);
    case 5: return NSMakeRect(1004, 531, 124, 15);
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return s3g::kAmbiInsectFactoryPresetCount;
    case 2: return 7u;
    case 3: return s3g::kAmbiInsectRegimeCount;
    case 4: return s3g::kAmbiInsectCallTypeCount;
    case 5: return s3g::kAmbiInsectPlaceCount;
    default: return 0u;
    }
}

- (void)openMenu:(int)menu
{
    _openMenu = menu;
    _menuItemCount = [self menuCount:menu];
    _hoverMenuItem = -1;
    const NSRect box = [self menuBoxRect:menu];
    const CGFloat itemH = 21.0;
    CGFloat y = NSMaxY(box) + 2.0;
    const CGFloat height = itemH * _menuItemCount;
    if (y + height > kGuiHeight - 8.0) y = box.origin.y - height - 2.0;
    _openMenuRect = NSMakeRect(box.origin.x, y, box.size.width, height);
    [self setNeedsDisplay:YES];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* regimeItems[] = { @"CHIRPERS", @"TRILLERS", @"CICADAS", @"FLYERS", @"TICKERS", @"MIXED SWARM", @"TREMULATORS" };
    static NSString* callTypeItems[] = {
        @"CALLING SONG", @"CONGREGATIONAL SONG", @"RESPONSE CALL",
        @"PREMATING SONG", @"COURTSHIP SONG", @"AGREEMENT SONG",
        @"JUMPING SONG", @"RIVALRY CALL", @"POSTCOPULATORY CALL",
        @"DEFENSIVE CALL", @"FLIGHT NOISE"
    };
    static NSString* placeItems[] = { @"MEADOW", @"FOREST FLOOR", @"CANOPY", @"MARSH", @"PORCH", @"GREENHOUSE", @"INTERIOR WALL" };
    static NSString* presetItems[s3g::kAmbiInsectFactoryPresetCount];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t i = 0; i < s3g::kAmbiInsectFactoryPresetCount; ++i) presetItems[i] = [[NSString stringWithUTF8String:s3g::ambiInsectFactoryPresetInfo(i).name] retain];
    });
    NSString** items = presetItems;
    int selected = static_cast<int>(_plugin->presetIndex);
    if (_openMenu == 2) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order) - 1;
    } else if (_openMenu == 3) {
        items = regimeItems;
        selected = static_cast<int>(_plugin->params.regime);
    } else if (_openMenu == 4) {
        items = callTypeItems;
        selected = static_cast<int>(_plugin->params.callType);
    } else if (_openMenu == 5) {
        items = placeItems;
        selected = static_cast<int>(_plugin->params.place);
    }
    s3g::clap_gui::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    [@"s3g AMBI INSECT ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    s3g::clap_gui::drawMenu(@"PRESET", [self presetDisplayName], 14, attrs, valueAttrs, style, 320, 382, 190);
    s3g::clap_gui::drawHeaderActionButton([self savePresetButtonRect], [self savePresetButtonRect], @"SAVE", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self loadPresetButtonRect], [self loadPresetButtonRect], @"LOAD", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self randomizeButtonRect], [self randomizeButtonRect], @"RANDOM", attrs, style);
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)), kGuiWidth, 14, valueAttrs, 18);
    [self drawField:attrs valueAttrs:valueAttrs style:style];
    [self drawPanels:attrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (int)hitVoice:(NSPoint)point
{
    if (!NSPointInRect(point, [self fieldRect])) return -1;
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiInsectMaxVoices);
    int best = -1;
    CGFloat bestDistance = 15.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const NSPoint projected = [self projectVoice:voice depth:nullptr];
        const CGFloat distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(voice);
        }
    }
    return best;
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)point
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    applyParam(*_plugin, param, sliderValue(*spec, point));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kPresetParamId, hit);
            else if (_openMenu == 2) applyParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 3) applyParam(*_plugin, kRegimeParamId, hit);
            else if (_openMenu == 4) applyParam(*_plugin, kCallTypeParamId, hit);
            else if (_openMenu == 5) applyParam(*_plugin, kPlaceParamId, hit);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self presetMenuRect])) { [self openMenu:1]; return; }
    if (NSPointInRect(point, [self savePresetButtonRect])) { [self saveCustomPreset]; return; }
    if (NSPointInRect(point, [self loadPresetButtonRect])) { [self loadCustomPreset]; return; }
    if (NSPointInRect(point, [self randomizeButtonRect])) {
        randomizeSafe(*_plugin);
        _selectedVoice = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self menuBoxRect:2])) { [self openMenu:2]; return; }
    if (NSPointInRect(point, [self menuBoxRect:3])) { [self openMenu:3]; return; }
    if (NSPointInRect(point, [self menuBoxRect:4])) { [self openMenu:4]; return; }
    if (NSPointInRect(point, [self menuBoxRect:5])) { [self openMenu:5]; return; }
    for (int mode = 0; mode < 4; ++mode) {
        if (NSPointInRect(point, [self fieldListenModeRect:mode])) {
            applyParam(*_plugin, kFieldListenModeParamId, mode);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const NSRect panel = [self fieldPanelRect];
    if (NSPointInRect(point, panel)) {
        for (int i = 0; i < 1; ++i) {
            if (NSPointInRect(point, [self pageButtonRect:i])) {
                _fieldPage = i;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(point, [self zoomButtonRect:i])) {
                _viewZoom = std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.20);
                [self storeViewState];
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(point, [self viewButtonRect:i])) {
                [self setViewPreset:i];
                return;
            }
        }
        const int hit = [self hitVoice:point];
        if (hit >= 0) {
            _selectedVoice = static_cast<uint32_t>(hit);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, [self fieldRect])) {
            _dragView = YES;
            _lastDragPoint = point;
            _viewMode = -1;
            [self storeViewState];
            return;
        }
    }
    _dragParam = 0;
    for (const auto& spec : kGuiSliders) {
        if (!guiParamVisible(_plugin->params.regime, spec.id)) continue;
        const CGFloat y = guiSliderY(_plugin->params.regime, spec);
        if (NSPointInRect(point, NSMakeRect(spec.panelX + 8, y - 8, 230, 24))) {
            _dragParam = static_cast<int>(spec.id);
            [self setParam:spec.id fromPoint:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _fieldPage = 0;
    if (_dragView) {
        const CGFloat dx = point.x - _lastDragPoint.x;
        const CGFloat dy = point.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam) [self setParam:static_cast<clap_id>(_dragParam) fromPoint:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = 0;
    _dragView = NO;
    _dragBreakpointRow = -1;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const int next = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiInsectEncoderView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return;
    [static_cast<S3GAmbiInsectEncoderView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    p->guiVisible = false;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win)
{
    if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(win->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GAmbiInsectEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiInsectEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };

#endif

namespace {

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-insect-encoder-64",
    "s3g Ambi Insect Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.2.0",
    "Procedural bioacoustic insect calls and swarm motion with direct 7OA ACN/SN3D output.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiInsectFactoryPreset(0u);
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->params);
    p->params = p->engine.params();
#if defined(__APPLE__)
    for (auto& weight : p->guiListenWeight) {
        weight.store(1.0f, std::memory_order_relaxed);
    }
#endif
    p->plugin.desc = &descriptor;
    p->plugin.plugin_data = p;
    p->plugin.init = init;
    p->plugin.destroy = destroy;
    p->plugin.activate = activate;
    p->plugin.deactivate = deactivate;
    p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing;
    p->plugin.reset = reset;
    p->plugin.process = process;
    p->plugin.get_extension = getExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0u ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
