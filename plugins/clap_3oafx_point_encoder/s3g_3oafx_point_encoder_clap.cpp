#include "s3g_ambisonic_point_encoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kPointCount = s3g::kAmbiPointEncoderMaxPoints;
constexpr uint32_t kDefaultPointCount = s3g::kAmbiPointEncoderPrototypePoints;
constexpr uint32_t kMixerBankSize = 16u;
constexpr uint32_t kMixerBankCount = kPointCount / kMixerBankSize;
constexpr uint32_t kOutputChannels = s3g::kAmbiPointEncoderMaxChannels;
constexpr uint32_t kStateVersion = 12;

constexpr clap_id kPointParamId = 1;
constexpr clap_id kAzimuthParamId = 2;
constexpr clap_id kElevationParamId = 3;
constexpr clap_id kDistanceParamId = 4;
constexpr clap_id kPointGainParamId = 5;
constexpr clap_id kMotionModeParamId = 6;
constexpr clap_id kMotionAmountParamId = 7;
constexpr clap_id kRateParamId = 8;
constexpr clap_id kAttractParamId = 9;
constexpr clap_id kRepelParamId = 10;
constexpr clap_id kDragParamId = 11;
constexpr clap_id kSwirlParamId = 12;
constexpr clap_id kBrownianParamId = 13;
constexpr clap_id kOutputParamId = 14;
constexpr clap_id kPointMuteParamId = 15;
constexpr clap_id kUpperHemisphereParamId = 16;
constexpr clap_id kMotionSceneParamId = 17;
constexpr clap_id kCollisionParamId = 18;
constexpr clap_id kImpactParamId = 19;
constexpr clap_id kPhysicsScaleParamId = 20;
constexpr clap_id kPoltergeistParamId = 21;
constexpr clap_id kPoltergeistRateParamId = 22;
constexpr clap_id kPoltergeistReachParamId = 23;
constexpr clap_id kPoltergeistChaosParamId = 24;
constexpr clap_id kPoltergeistRadiusParamId = 25;
constexpr clap_id kDopplerParamId = 26;
constexpr clap_id kAirParamId = 27;
constexpr clap_id kOrderParamId = 28;
constexpr clap_id kActivePointsParamId = 29;
constexpr clap_id kPerPointParamBase = 1000;
constexpr clap_id kPerPointParamStride = 8;

enum class PerPointParamKind : uint32_t {
    Azimuth = 0,
    Elevation = 1,
    Distance = 2,
    Gain = 3,
    Mute = 4,
    Solo = 5,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiPointEncoderParams params {};
    std::array<s3g::AmbiPoint, s3g::kAmbiPointEncoderMaxPoints> points {};
    int32_t guiViewMode = 2;
    double guiViewAzDeg = -35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
};

struct SavedStateV11 {
    uint32_t version = 11;
    s3g::AmbiPointEncoderParams params {};
    std::array<s3g::AmbiPoint, s3g::kAmbiPointEncoderMaxPoints> points {};
    int32_t guiViewMode = 2;
    double guiViewAzDeg = -35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
};

struct LegacyAmbiPointEncoderParams {
    uint32_t activePoints = s3g::kAmbiPointEncoderPrototypePoints;
    uint32_t selectedPoint = 0;
    float selectedAzimuthDeg = 0.0f;
    float selectedElevationDeg = 0.0f;
    float selectedDistance = 1.0f;
    float selectedGain = 1.0f;
    bool selectedEnabled = true;
    bool upperHemisphereOnly = false;
    uint32_t motionScene = 0;
    s3g::AmbiPointMotionMode motionMode = s3g::AmbiPointMotionMode::Off;
    float motionAmount = 0.0f;
    float rateHz = 0.035f;
    float attract = 0.05f;
    float repel = 0.03f;
    float drag = 0.94f;
    float swirl = 0.0f;
    float brownian = 0.0f;
    float collision = 0.0f;
    float impact = 0.0f;
    float physicsScale = 1.0f;
    float poltergeist = 0.0f;
    float poltergeistRate = 1.0f;
    float poltergeistReach = 0.55f;
    float poltergeistRadius = 0.28f;
    float poltergeistChaos = 0.0f;
    float doppler = 0.0f;
    float air = 0.0f;
    float outputGainDb = -6.0f;
};

struct SavedStateV10 {
    uint32_t version = 10;
    LegacyAmbiPointEncoderParams params {};
    std::array<s3g::AmbiPoint, s3g::kAmbiPointEncoderMaxPoints> points {};
    int32_t guiViewMode = 2;
    double guiViewAzDeg = -35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
};

struct SavedStateV9 {
    uint32_t version = 9;
    LegacyAmbiPointEncoderParams params {};
    std::array<s3g::AmbiPoint, s3g::kAmbiPointEncoderMaxPoints> points {};
};

s3g::AmbiPointEncoderParams upgradeLegacyParams(const LegacyAmbiPointEncoderParams& old)
{
    s3g::AmbiPointEncoderParams params {};
    params.activePoints = old.activePoints;
    params.selectedPoint = old.selectedPoint;
    params.selectedAzimuthDeg = old.selectedAzimuthDeg;
    params.selectedElevationDeg = old.selectedElevationDeg;
    params.selectedDistance = old.selectedDistance;
    params.selectedGain = old.selectedGain;
    params.selectedEnabled = old.selectedEnabled;
    params.upperHemisphereOnly = old.upperHemisphereOnly;
    params.motionScene = old.motionScene;
    params.motionMode = old.motionMode;
    params.motionAmount = old.motionAmount;
    params.rateHz = old.rateHz;
    params.attract = old.attract;
    params.repel = old.repel;
    params.drag = old.drag;
    params.swirl = old.swirl;
    params.brownian = old.brownian;
    params.collision = old.collision;
    params.impact = old.impact;
    params.physicsScale = old.physicsScale;
    params.poltergeist = old.poltergeist;
    params.poltergeistRate = old.poltergeistRate;
    params.poltergeistReach = old.poltergeistReach;
    params.poltergeistRadius = old.poltergeistRadius;
    params.poltergeistChaos = old.poltergeistChaos;
    params.doppler = old.doppler;
    params.air = old.air;
    params.outputGainDb = old.outputGainDb;
    params.order = 3;
    return params;
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::AmbiPointEncoderParams params {};
    s3g::AmbiPointEncoder encoder;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
    int guiViewMode = 2;
    double guiViewAzDeg = -35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t offset = 0;
    while (offset < size) {
        const int64_t got = stream->read(stream, bytes + offset, size - offset);
        if (got <= 0) return false;
        offset += static_cast<size_t>(got);
    }
    return true;
}

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0;
    while (offset < size) {
        const int64_t wrote = stream->write(stream, bytes + offset, size - offset);
        if (wrote <= 0) return false;
        offset += static_cast<size_t>(wrote);
    }
    return true;
}

const char* motionName(uint32_t index)
{
    static constexpr const char* kNames[] { "OFF", "DRFT", "ORBT", "SWRM", "PULS", "PHYS" };
    return kNames[std::min<uint32_t>(index, 5u)];
}

const char* motionDisplayName(uint32_t index)
{
    static constexpr const char* kNames[] { "OFF", "DRIFT", "ORBIT", "SWARM", "PULSE", "PHYS" };
    return kNames[std::min<uint32_t>(index, 5u)];
}

const char* sceneName(uint32_t index)
{
    static constexpr const char* kNames[] {
        "MANUAL",
        "DRIFT: BREEZE",
        "ORBIT: GLIDE",
        "PHYS: BOUNCE",
        "PHYS: COLLIDE",
        "PHYS: SCATTER",
        "CUSTOM",
        "PHYS: ELASTIC",
        "DRIFT: MEANDER",
        "DRIFT: CROSSWIND",
        "ORBIT: WIDE",
        "ORBIT: TIGHT",
        "SWARM: FLOCK",
        "SWARM: CLOUD",
        "SWARM: FRENZY",
        "PULSE: BREATHE",
        "PULSE: RIPPLE",
        "PULSE: THROB",
    };
    return kNames[std::min<uint32_t>(index, s3g::kAmbiPointEncoderMaxMotionScene)];
}

const char* styleName(uint32_t scene)
{
    static constexpr const char* kNames[] {
        "MANUAL", "BREEZE", "GLIDE", "BOUNCE", "COLLIDE", "SCATTER", "CUSTOM", "ELASTIC",
        "MEANDER", "CROSSWIND", "WIDE", "TIGHT", "FLOCK", "CLOUD", "FRENZY", "BREATHE", "RIPPLE", "THROB",
    };
    return kNames[std::min<uint32_t>(scene, s3g::kAmbiPointEncoderMaxMotionScene)];
}

struct MotionStyleList {
    const uint32_t* scenes = nullptr;
    uint32_t count = 0;
};

MotionStyleList motionStyleList(s3g::AmbiPointMotionMode mode)
{
    static constexpr std::array<uint32_t, 1> kOff { 0u };
    static constexpr std::array<uint32_t, 4> kDrift { 1u, 8u, 9u, 6u };
    static constexpr std::array<uint32_t, 4> kOrbit { 2u, 10u, 11u, 6u };
    static constexpr std::array<uint32_t, 4> kSwarm { 12u, 13u, 14u, 6u };
    static constexpr std::array<uint32_t, 4> kPulse { 15u, 16u, 17u, 6u };
    static constexpr std::array<uint32_t, 5> kPhysics { 3u, 4u, 5u, 7u, 6u };
    switch (mode) {
    case s3g::AmbiPointMotionMode::Drift: return { kDrift.data(), static_cast<uint32_t>(kDrift.size()) };
    case s3g::AmbiPointMotionMode::Orbit: return { kOrbit.data(), static_cast<uint32_t>(kOrbit.size()) };
    case s3g::AmbiPointMotionMode::Swarm: return { kSwarm.data(), static_cast<uint32_t>(kSwarm.size()) };
    case s3g::AmbiPointMotionMode::Pulse: return { kPulse.data(), static_cast<uint32_t>(kPulse.size()) };
    case s3g::AmbiPointMotionMode::Phys: return { kPhysics.data(), static_cast<uint32_t>(kPhysics.size()) };
    default: return { kOff.data(), static_cast<uint32_t>(kOff.size()) };
    }
}

uint32_t defaultSceneForMotion(s3g::AmbiPointMotionMode mode)
{
    const MotionStyleList styles = motionStyleList(mode);
    return styles.scenes[0];
}

int styleMenuIndex(s3g::AmbiPointMotionMode mode, uint32_t scene)
{
    const MotionStyleList styles = motionStyleList(mode);
    for (uint32_t i = 0; i < styles.count; ++i) {
        if (styles.scenes[i] == scene) return static_cast<int>(i);
    }
    return -1;
}

void applyMotionSceneDefaults(s3g::AmbiPointEncoderParams& params, uint32_t scene)
{
    params.motionScene = std::min<uint32_t>(scene, s3g::kAmbiPointEncoderMaxMotionScene);
    auto setKinematicStyle = [&](s3g::AmbiPointMotionMode mode,
                                 float amount,
                                 float rate,
                                 float attract,
                                 float repel,
                                 float drag,
                                 float swirl,
                                 float brownian,
                                 float scale) {
        params.motionMode = mode;
        params.motionAmount = amount;
        params.rateHz = rate;
        params.attract = attract;
        params.repel = repel;
        params.drag = drag;
        params.swirl = swirl;
        params.brownian = brownian;
        params.collision = 0.0f;
        params.impact = 0.0f;
        params.physicsScale = scale;
        params.poltergeist = 0.0f;
        params.poltergeistRate = 1.0f;
        params.poltergeistReach = 0.55f;
        params.poltergeistRadius = 0.28f;
        params.poltergeistChaos = 0.0f;
    };
    switch (params.motionScene) {
    case 0:
        params.motionMode = s3g::AmbiPointMotionMode::Off;
        params.motionAmount = 0.0f;
        params.rateHz = 0.035f;
        params.attract = 0.05f;
        params.repel = 0.03f;
        params.drag = 0.94f;
        params.swirl = 0.0f;
        params.brownian = 0.0f;
        params.collision = 0.0f;
        params.impact = 0.0f;
        params.physicsScale = 1.0f;
        params.poltergeist = 0.0f;
        params.poltergeistRate = 1.0f;
        params.poltergeistReach = 0.55f;
        params.poltergeistRadius = 0.28f;
        params.poltergeistChaos = 0.0f;
        break;
    case 1:
        params.motionMode = s3g::AmbiPointMotionMode::Drift;
        params.motionAmount = 0.35f;
        params.rateHz = 0.035f;
        params.attract = 0.03f;
        params.repel = 0.02f;
        params.drag = 0.94f;
        params.swirl = 0.0f;
        params.brownian = 0.02f;
        params.collision = 0.0f;
        params.impact = 0.0f;
        params.physicsScale = 1.0f;
        params.poltergeist = 0.18f;
        params.poltergeistRate = 0.55f;
        params.poltergeistReach = 0.38f;
        params.poltergeistRadius = 0.24f;
        params.poltergeistChaos = 0.12f;
        break;
    case 2:
        params.motionMode = s3g::AmbiPointMotionMode::Orbit;
        params.motionAmount = 0.28f;
        params.rateHz = 0.018f;
        params.attract = 0.025f;
        params.repel = 0.02f;
        params.drag = 0.86f;
        params.swirl = 0.02f;
        params.brownian = 0.0f;
        params.collision = 0.0f;
        params.impact = 0.0f;
        params.physicsScale = 1.0f;
        params.poltergeist = 0.14f;
        params.poltergeistRate = 0.38f;
        params.poltergeistReach = 0.44f;
        params.poltergeistRadius = 0.22f;
        params.poltergeistChaos = 0.04f;
        break;
    case 3:
        params.motionMode = s3g::AmbiPointMotionMode::Phys;
        params.motionAmount = 0.92f;
        params.rateHz = 0.045f;
        params.attract = 0.135f;
        params.repel = 0.08f;
        params.drag = 0.86f;
        params.swirl = 0.0f;
        params.brownian = 0.045f;
        params.collision = 0.95f;
        params.impact = 1.0f;
        params.physicsScale = 1.0f;
        params.poltergeist = 0.62f;
        params.poltergeistRate = 0.72f;
        params.poltergeistReach = 0.52f;
        params.poltergeistRadius = 0.30f;
        params.poltergeistChaos = 0.22f;
        break;
    case 4:
        params.motionMode = s3g::AmbiPointMotionMode::Phys;
        params.motionAmount = 1.0f;
        params.rateHz = 0.038f;
        params.attract = 0.035f;
        params.repel = 0.24f;
        params.drag = 0.84f;
        params.swirl = 0.0f;
        params.brownian = 0.060f;
        params.collision = 1.0f;
        params.impact = 1.0f;
        params.physicsScale = 1.0f;
        params.poltergeist = 0.78f;
        params.poltergeistRate = 0.86f;
        params.poltergeistReach = 0.68f;
        params.poltergeistRadius = 0.34f;
        params.poltergeistChaos = 0.36f;
        break;
    case 5:
        params.motionMode = s3g::AmbiPointMotionMode::Phys;
        params.motionAmount = 1.0f;
        params.rateHz = 0.055f;
        params.attract = 0.018f;
        params.repel = 0.22f;
        params.drag = 0.78f;
        params.swirl = 0.0f;
        params.brownian = 0.24f;
        params.collision = 0.86f;
        params.impact = 1.0f;
        params.physicsScale = 1.0f;
        params.poltergeist = 0.92f;
        params.poltergeistRate = 1.32f;
        params.poltergeistReach = 0.82f;
        params.poltergeistRadius = 0.40f;
        params.poltergeistChaos = 0.72f;
        break;
    case 7:
        params.motionMode = s3g::AmbiPointMotionMode::Phys;
        params.motionAmount = 0.82f;
        params.rateHz = 0.030f;
        params.attract = 0.070f;
        params.repel = 0.08f;
        params.drag = 0.88f;
        params.swirl = 0.018f;
        params.brownian = 0.018f;
        params.collision = 0.62f;
        params.impact = 0.74f;
        params.physicsScale = 1.0f;
        params.poltergeist = 0.34f;
        params.poltergeistRate = 0.62f;
        params.poltergeistReach = 0.58f;
        params.poltergeistRadius = 0.30f;
        params.poltergeistChaos = 0.18f;
        break;
    case 8:
        setKinematicStyle(s3g::AmbiPointMotionMode::Drift,
                          0.58f, 0.014f, 0.012f, 0.025f, 0.975f, 0.012f, 0.0f, 1.35f);
        break;
    case 9:
        setKinematicStyle(s3g::AmbiPointMotionMode::Drift,
                          0.76f, 0.085f, 0.035f, 0.055f, 0.88f, 0.075f, 0.0f, 1.10f);
        break;
    case 10:
        setKinematicStyle(s3g::AmbiPointMotionMode::Orbit,
                          0.58f, 0.012f, 0.008f, 0.075f, 0.95f, 0.018f, 0.0f, 1.55f);
        break;
    case 11:
        setKinematicStyle(s3g::AmbiPointMotionMode::Orbit,
                          0.72f, 0.095f, 0.12f, 0.012f, 0.84f, 0.09f, 0.0f, 0.62f);
        break;
    case 12:
        setKinematicStyle(s3g::AmbiPointMotionMode::Swarm,
                          0.62f, 0.035f, 0.070f, 0.065f, 0.92f, 0.012f, 0.055f, 1.0f);
        break;
    case 13:
        setKinematicStyle(s3g::AmbiPointMotionMode::Swarm,
                          0.42f, 0.018f, 0.014f, 0.022f, 0.978f, 0.0f, 0.028f, 1.60f);
        break;
    case 14:
        setKinematicStyle(s3g::AmbiPointMotionMode::Swarm,
                          0.95f, 0.13f, 0.035f, 0.14f, 0.74f, 0.055f, 0.24f, 1.20f);
        break;
    case 15:
        setKinematicStyle(s3g::AmbiPointMotionMode::Pulse,
                          0.48f, 0.022f, 0.040f, 0.018f, 0.95f, 0.0f, 0.0f, 1.0f);
        break;
    case 16:
        setKinematicStyle(s3g::AmbiPointMotionMode::Pulse,
                          0.74f, 0.085f, 0.055f, 0.060f, 0.88f, 0.018f, 0.0f, 1.25f);
        break;
    case 17:
        setKinematicStyle(s3g::AmbiPointMotionMode::Pulse,
                          0.94f, 0.22f, 0.11f, 0.085f, 0.79f, -0.030f, 0.0f, 0.82f);
        break;
    default:
        break;
    }
}

clap_id perPointParamId(uint32_t point, PerPointParamKind kind)
{
    return kPerPointParamBase + static_cast<clap_id>(point) * kPerPointParamStride + static_cast<clap_id>(kind);
}

bool decodePerPointParam(clap_id id, uint32_t& point, PerPointParamKind& kind)
{
    if (id < kPerPointParamBase) return false;
    const clap_id local = id - kPerPointParamBase;
    point = static_cast<uint32_t>(local / kPerPointParamStride);
    const uint32_t k = static_cast<uint32_t>(local % kPerPointParamStride);
    if (point >= kPointCount || k > static_cast<uint32_t>(PerPointParamKind::Solo)) return false;
    kind = static_cast<PerPointParamKind>(k);
    return true;
}

void applyPerPointParam(Plugin& p, uint32_t point, PerPointParamKind kind, double value)
{
    switch (kind) {
    case PerPointParamKind::Azimuth: p.encoder.setPointAzimuth(point, static_cast<float>(std::clamp(value, -180.0, 180.0))); break;
    case PerPointParamKind::Elevation: p.encoder.setPointElevation(point, static_cast<float>(std::clamp(value, p.params.upperHemisphereOnly ? 0.0 : -90.0, 90.0))); break;
    case PerPointParamKind::Distance: p.encoder.setPointDistance(point, static_cast<float>(std::clamp(value, 0.15, 2.0))); break;
    case PerPointParamKind::Gain: p.encoder.setPointGain(point, static_cast<float>(std::clamp(value, 0.0, 2.0))); break;
    case PerPointParamKind::Mute: p.encoder.setPointEnabled(point, value < 0.5); break;
    case PerPointParamKind::Solo: p.encoder.setPointSolo(point, value >= 0.5); break;
    }
    p.params = p.encoder.params();
}

void applyParam(Plugin& p, clap_id id, double value)
{
    uint32_t point = 0;
    PerPointParamKind kind {};
    if (decodePerPointParam(id, point, kind)) {
        applyPerPointParam(p, point, kind, value);
        return;
    }
    switch (id) {
    case kPointParamId: p.params.selectedPoint = static_cast<uint32_t>(std::clamp(std::round(value), 1.0, static_cast<double>(std::max<uint32_t>(1u, p.params.activePoints)))) - 1u; break;
    case kAzimuthParamId: p.params.selectedAzimuthDeg = static_cast<float>(std::clamp(value, -180.0, 180.0)); break;
    case kElevationParamId: p.params.selectedElevationDeg = static_cast<float>(std::clamp(value, p.params.upperHemisphereOnly ? 0.0 : -90.0, 90.0)); break;
    case kDistanceParamId: p.params.selectedDistance = static_cast<float>(std::clamp(value, 0.15, 2.0)); break;
    case kPointGainParamId: p.params.selectedGain = static_cast<float>(std::clamp(value, 0.0, 2.0)); break;
    case kMotionModeParamId: {
        const auto mode = static_cast<s3g::AmbiPointMotionMode>(static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 5.0)));
        if (mode != p.params.motionMode) {
            applyMotionSceneDefaults(p.params, defaultSceneForMotion(mode));
        }
        break;
    }
    case kMotionAmountParamId: p.params.motionAmount = static_cast<float>(std::clamp(value, 0.0, 1.0)); p.params.motionScene = 6; break;
    case kRateParamId: p.params.rateHz = static_cast<float>(std::clamp(value, 0.005, 0.50)); p.params.motionScene = 6; break;
    case kAttractParamId: p.params.attract = static_cast<float>(std::clamp(value, 0.0, 0.24)); p.params.motionScene = 6; break;
    case kRepelParamId: p.params.repel = static_cast<float>(std::clamp(value, 0.0, 0.24)); p.params.motionScene = 6; break;
    case kDragParamId: p.params.drag = static_cast<float>(std::clamp(value, 0.45, 0.995)); p.params.motionScene = 6; break;
    case kSwirlParamId: p.params.swirl = static_cast<float>(std::clamp(value, -0.24, 0.24)); p.params.motionScene = 6; break;
    case kBrownianParamId: p.params.brownian = static_cast<float>(std::clamp(value, 0.0, 0.24)); p.params.motionScene = 6; break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    case kPointMuteParamId: p.params.selectedEnabled = value < 0.5; break;
    case kUpperHemisphereParamId: p.params.upperHemisphereOnly = value >= 0.5; break;
    case kMotionSceneParamId: applyMotionSceneDefaults(p.params, static_cast<uint32_t>(std::clamp(std::round(value), 0.0, static_cast<double>(s3g::kAmbiPointEncoderMaxMotionScene)))); break;
    case kCollisionParamId: p.params.collision = static_cast<float>(std::clamp(value, 0.0, 1.0)); p.params.motionScene = 6; break;
    case kImpactParamId: p.params.impact = static_cast<float>(std::clamp(value, 0.0, 1.0)); p.params.motionScene = 6; break;
    case kPhysicsScaleParamId: p.params.physicsScale = static_cast<float>(std::clamp(value, 0.25, 2.0)); p.params.motionScene = 6; break;
    case kPoltergeistParamId: p.params.poltergeist = static_cast<float>(std::clamp(value, 0.0, 1.0)); p.params.motionScene = 6; break;
    case kPoltergeistRateParamId: p.params.poltergeistRate = static_cast<float>(std::clamp(value, 0.05, 4.0)); p.params.motionScene = 6; break;
    case kPoltergeistReachParamId: p.params.poltergeistReach = static_cast<float>(std::clamp(value, 0.0, 1.0)); p.params.motionScene = 6; break;
    case kPoltergeistRadiusParamId: p.params.poltergeistRadius = static_cast<float>(std::clamp(value, 0.04, 1.0)); p.params.motionScene = 6; break;
    case kPoltergeistChaosParamId: p.params.poltergeistChaos = static_cast<float>(std::clamp(value, 0.0, 1.0)); p.params.motionScene = 6; break;
    case kDopplerParamId: p.params.doppler = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kAirParamId: p.params.air = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kOrderParamId: p.params.order = static_cast<uint32_t>(std::clamp(std::round(value), 1.0, static_cast<double>(s3g::kAmbiPointEncoderMaxOrder))); break;
    case kActivePointsParamId: p.params.activePoints = static_cast<uint32_t>(std::clamp(std::round(value), 1.0, static_cast<double>(kPointCount))); break;
    default: break;
    }
    p.encoder.setParams(p.params);
    p.params = p.encoder.params();
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    // State may be restored before activate(), so establish the fixed 64-point
    // scene and depth allocation as soon as the CLAP instance is initialized.
    p->encoder.prepare(p->sampleRate, kPointCount);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
    return true;
}

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    const auto scene = p->encoder.editPoints();
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->encoder.prepare(sampleRate, kPointCount);
    p->encoder.setParams(p->params);
    p->encoder.setScene(scene);
    p->params = p->encoder.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->encoder.resetMotion();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
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

    if (proc->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto* input = proc->audio_inputs_count > 0 ? &proc->audio_inputs[0] : nullptr;
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t inChannels = input ? std::min<uint32_t>(input->channel_count, kPointCount) : 0u;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);

    if (output.data32) {
        s3g::clearAudioBufferFromChannel(output, 0, frames);
    }
    if (!output.data32 || outChannels == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }

    std::array<const float*, kPointCount> inputPtrs {};
    std::array<float*, kOutputChannels> outputPtrs {};
    for (uint32_t ch = 0; ch < inChannels; ++ch) {
        inputPtrs[ch] = input && input->data32 ? input->data32[ch] : nullptr;
    }
    for (uint32_t ch = 0; ch < outChannels; ++ch) {
        outputPtrs[ch] = output.data32[ch];
    }

    p->encoder.setParams(p->params);
    p->encoder.processBlock(inputPtrs.data(), outputPtrs.data(), outChannels, frames);
    p->params = p->encoder.params();
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

    float blockPeak = 0.0f;
    for (uint32_t ch = 0; ch < outChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t i = 0; i < frames; ++i) {
            blockPeak = std::max(blockPeak, std::fabs(output.data32[ch][i]));
        }
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "64 Point In" : "1-7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kPointCount : kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kMotionModeParamId, "Motion", 0.0, 5.0, 0.0 },
    { kMotionAmountParamId, "Amount", 0.0, 1.0, 0.0 },
    { kRateParamId, "Rate", 0.005, 0.50, 0.035 },
    { kAttractParamId, "Attract", 0.0, 0.24, 0.05 },
    { kRepelParamId, "Repel", 0.0, 0.24, 0.03 },
    { kDragParamId, "Drag", 0.45, 0.995, 0.88 },
    { kSwirlParamId, "Swirl", -0.24, 0.24, 0.0 },
    { kBrownianParamId, "Brownian", 0.0, 0.24, 0.0 },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0 },
    { kUpperHemisphereParamId, "Upper Hemisphere", 0.0, 1.0, 0.0 },
    { kMotionSceneParamId, "Motion Style", 0.0, static_cast<double>(s3g::kAmbiPointEncoderMaxMotionScene), 0.0 },
    { kCollisionParamId, "Collision", 0.0, 1.0, 0.0 },
    { kImpactParamId, "Impact", 0.0, 1.0, 0.0 },
    { kPhysicsScaleParamId, "Physics Scale", 0.25, 2.0, 1.0 },
    { kPoltergeistParamId, "Poltergeist", 0.0, 1.0, 0.0 },
    { kPoltergeistRateParamId, "Poltergeist Rate", 0.05, 4.0, 1.0 },
    { kPoltergeistReachParamId, "Poltergeist Reach", 0.0, 1.0, 0.55 },
    { kPoltergeistRadiusParamId, "Poltergeist Radius", 0.04, 1.0, 0.28 },
    { kPoltergeistChaosParamId, "Poltergeist Chaos", 0.0, 1.0, 0.0 },
    { kDopplerParamId, "Doppler", 0.0, 1.0, 0.0 },
    { kAirParamId, "Air", 0.0, 1.0, 0.0 },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0 },
    { kActivePointsParamId, "Active Points", 1.0, 64.0, 16.0 },
};

constexpr uint32_t kBaseParamCount = static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0]));
constexpr uint32_t kPerPointParamCount = kPointCount * 6u;

uint32_t paramsCount(const clap_plugin_t*) { return kBaseParamCount + kPerPointParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (index < kBaseParamCount) {
        const auto& def = kParamDefs[index];
        info->id = def.id;
        if (def.id == kPointParamId || def.id == kMotionModeParamId || def.id == kPointMuteParamId || def.id == kUpperHemisphereParamId || def.id == kMotionSceneParamId || def.id == kOrderParamId || def.id == kActivePointsParamId) {
            info->flags |= CLAP_PARAM_IS_STEPPED;
        }
        std::strncpy(info->name, def.name, sizeof(info->name));
        std::strncpy(info->module, "Ambi Point Encoder", sizeof(info->module));
        info->min_value = def.min;
        info->max_value = def.max;
        info->default_value = def.def;
        return true;
    }

    const uint32_t pointIndex = (index - kBaseParamCount) / 6u;
    const auto kind = static_cast<PerPointParamKind>((index - kBaseParamCount) % 6u);
    info->id = perPointParamId(pointIndex, kind);
    const char* suffix = "";
    double min = 0.0;
    double max = 1.0;
    double def = 0.0;
    switch (kind) {
    case PerPointParamKind::Azimuth: suffix = "Azimuth"; min = -180.0; max = 180.0; def = 0.0; break;
    case PerPointParamKind::Elevation: suffix = "Elevation"; min = -90.0; max = 90.0; def = 0.0; break;
    case PerPointParamKind::Distance: suffix = "Distance"; min = 0.15; max = 2.0; def = 1.0; break;
    case PerPointParamKind::Gain: suffix = "Gain"; min = 0.0; max = 2.0; def = 1.0; break;
    case PerPointParamKind::Mute: suffix = "Mute"; info->flags |= CLAP_PARAM_IS_STEPPED; min = 0.0; max = 1.0; def = 0.0; break;
    case PerPointParamKind::Solo: suffix = "Solo"; info->flags |= CLAP_PARAM_IS_STEPPED; min = 0.0; max = 1.0; def = 0.0; break;
    }
    std::snprintf(info->name, sizeof(info->name), "P%02u %s", pointIndex + 1u, suffix);
    std::snprintf(info->module, sizeof(info->module), "Ambi Points");
    info->min_value = min;
    info->max_value = max;
    info->default_value = def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    uint32_t point = 0;
    PerPointParamKind kind {};
    if (decodePerPointParam(id, point, kind)) {
        const auto pnt = self(plugin)->encoder.editPoint(point);
        switch (kind) {
        case PerPointParamKind::Azimuth: *value = pnt.azimuthDeg; return true;
        case PerPointParamKind::Elevation: *value = pnt.elevationDeg; return true;
        case PerPointParamKind::Distance: *value = pnt.distance; return true;
        case PerPointParamKind::Gain: *value = pnt.gain; return true;
        case PerPointParamKind::Mute: *value = pnt.enabled ? 0.0 : 1.0; return true;
        case PerPointParamKind::Solo: *value = pnt.solo ? 1.0 : 0.0; return true;
        }
    }
    const auto& p = self(plugin)->params;
    switch (id) {
    case kPointParamId: *value = static_cast<double>(p.selectedPoint + 1u); return true;
    case kAzimuthParamId: *value = p.selectedAzimuthDeg; return true;
    case kElevationParamId: *value = p.selectedElevationDeg; return true;
    case kDistanceParamId: *value = p.selectedDistance; return true;
    case kPointGainParamId: *value = p.selectedGain; return true;
    case kMotionModeParamId: *value = static_cast<double>(static_cast<uint32_t>(p.motionMode)); return true;
    case kMotionAmountParamId: *value = p.motionAmount; return true;
    case kRateParamId: *value = p.rateHz; return true;
    case kAttractParamId: *value = p.attract; return true;
    case kRepelParamId: *value = p.repel; return true;
    case kDragParamId: *value = p.drag; return true;
    case kSwirlParamId: *value = p.swirl; return true;
    case kBrownianParamId: *value = p.brownian; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    case kPointMuteParamId: *value = p.selectedEnabled ? 0.0 : 1.0; return true;
    case kUpperHemisphereParamId: *value = p.upperHemisphereOnly ? 1.0 : 0.0; return true;
    case kMotionSceneParamId: *value = static_cast<double>(p.motionScene); return true;
    case kCollisionParamId: *value = p.collision; return true;
    case kImpactParamId: *value = p.impact; return true;
    case kPhysicsScaleParamId: *value = p.physicsScale; return true;
    case kPoltergeistParamId: *value = p.poltergeist; return true;
    case kPoltergeistRateParamId: *value = p.poltergeistRate; return true;
    case kPoltergeistReachParamId: *value = p.poltergeistReach; return true;
    case kPoltergeistRadiusParamId: *value = p.poltergeistRadius; return true;
    case kPoltergeistChaosParamId: *value = p.poltergeistChaos; return true;
    case kDopplerParamId: *value = p.doppler; return true;
    case kAirParamId: *value = p.air; return true;
    case kOrderParamId: *value = static_cast<double>(p.order); return true;
    case kActivePointsParamId: *value = static_cast<double>(p.activePoints); return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    uint32_t point = 0;
    PerPointParamKind kind {};
    if (decodePerPointParam(id, point, kind)) {
        switch (kind) {
        case PerPointParamKind::Azimuth:
        case PerPointParamKind::Elevation: std::snprintf(display, size, "%+.1f deg", value); return true;
        case PerPointParamKind::Distance:
        case PerPointParamKind::Gain: std::snprintf(display, size, "%.2f", value); return true;
        case PerPointParamKind::Mute: std::snprintf(display, size, "%s", value >= 0.5 ? "MUT" : "ON"); return true;
        case PerPointParamKind::Solo: std::snprintf(display, size, "%s", value >= 0.5 ? "SOLO" : "OFF"); return true;
        }
    }
    if (id == kPointParamId) std::snprintf(display, size, "P%.0f", value);
    else if (id == kAzimuthParamId || id == kElevationParamId) std::snprintf(display, size, "%+.1f deg", value);
    else if (id == kDistanceParamId) std::snprintf(display, size, "%.2f", value);
    else if (id == kPointGainParamId) std::snprintf(display, size, "%.2f", value);
    else if (id == kMotionModeParamId) std::snprintf(display, size, "%s", motionName(static_cast<uint32_t>(std::round(value))));
    else if (id == kPointMuteParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "MUT" : "ON");
    else if (id == kUpperHemisphereParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "UPR" : "FULL");
    else if (id == kMotionSceneParamId) std::snprintf(display, size, "%s", sceneName(static_cast<uint32_t>(std::round(value))));
    else if (id == kRateParamId) std::snprintf(display, size, "%.2f Hz", value);
    else if (id == kPoltergeistRateParamId) std::snprintf(display, size, "%.2fx", value);
    else if (id == kPoltergeistRadiusParamId) std::snprintf(display, size, "%.2f", value);
    else if (id == kPhysicsScaleParamId) std::snprintf(display, size, "%.2f", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kActivePointsParamId) std::snprintf(display, size, "%.0f", value);
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    SavedState s {};
    s.params = p->params;
    s.points = p->encoder.editPoints();
#if defined(__APPLE__)
    s.guiViewMode = p->guiViewMode;
    s.guiViewAzDeg = p->guiViewAzDeg;
    s.guiViewElDeg = p->guiViewElDeg;
    s.guiViewZoom = p->guiViewZoom;
#endif
    return writeExact(stream, &s, sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    uint32_t version = 0;
    if (!readExact(stream, &version, sizeof(version))) return false;
    bool initializeExpandedPoints = false;
    if (version == kStateVersion) {
        s.version = version;
        if (!readExact(stream, reinterpret_cast<char*>(&s) + sizeof(version), sizeof(s) - sizeof(version))) return false;
    } else if (version == 11) {
        SavedStateV11 old {};
        old.version = version;
        if (!readExact(stream, reinterpret_cast<char*>(&old) + sizeof(version), sizeof(old) - sizeof(version))) return false;
        s.params = old.params;
        s.points = old.points;
        s.guiViewMode = old.guiViewMode;
        s.guiViewAzDeg = old.guiViewAzDeg;
        s.guiViewElDeg = old.guiViewElDeg;
        s.guiViewZoom = old.guiViewZoom;
        initializeExpandedPoints = true;
    } else if (version == 10) {
        SavedStateV10 old {};
        old.version = version;
        if (!readExact(stream, reinterpret_cast<char*>(&old) + sizeof(version), sizeof(old) - sizeof(version))) return false;
        s.params = upgradeLegacyParams(old.params);
        s.points = old.points;
        s.guiViewMode = old.guiViewMode;
        s.guiViewAzDeg = old.guiViewAzDeg;
        s.guiViewElDeg = old.guiViewElDeg;
        s.guiViewZoom = old.guiViewZoom;
        initializeExpandedPoints = true;
    } else if (version == 9) {
        SavedStateV9 old {};
        old.version = version;
        if (!readExact(stream, reinterpret_cast<char*>(&old) + sizeof(version), sizeof(old) - sizeof(version))) return false;
        s.params = upgradeLegacyParams(old.params);
        s.points = old.points;
        initializeExpandedPoints = true;
    } else {
        return false;
    }
    auto* p = self(plugin);
    if (initializeExpandedPoints) {
        const auto defaults = p->encoder.editPoints();
        for (uint32_t point = kDefaultPointCount; point < kPointCount; ++point) {
            s.points[point] = defaults[point];
        }
        s.params.activePoints = kDefaultPointCount;
    }
    p->params = s.params;
    p->encoder.setParams(p->params);
    p->encoder.setScene(s.points);
    p->params = p->encoder.params();
#if defined(__APPLE__)
    p->guiViewMode = std::clamp<int>(s.guiViewMode, -1, 2);
    p->guiViewAzDeg = std::clamp(s.guiViewAzDeg, -180.0, 180.0);
    p->guiViewElDeg = std::clamp(s.guiViewElDeg, -90.0, 90.0);
    p->guiViewZoom = std::clamp(s.guiViewZoom, 0.55, 2.20);
#endif
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3G3OAFXPointEncoderView : NSView {
    void* _plugin;
    int _dragSlider;
    int _dragMixerPoint;
    int _dragPoint;
    BOOL _dragMixerOutput;
    NSTimer* _timer;
    int _viewMode;
    int _leftPage;
    uint32_t _mixerBank;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _hasPointSelection;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSPoint _menuOrigin;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawOpenMenu:(NSDictionary*)attrs;
- (void)updateMenuHover:(NSPoint)point;
- (void)drawPointField:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawPointMixer:(NSRect)rect attrs:(NSDictionary*)attrs labelAttrs:(NSDictionary*)labelAttrs;
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawMixerBankButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)pageButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)mixerBankButtonRect:(uint32_t)index inRect:(NSRect)rect;
- (void)setViewPreset:(int)mode;
- (CGFloat)viewScaleForRect:(NSRect)rect;
- (NSPoint)projectWorldPoint:(s3g::Vec3)p rect:(NSRect)rect depth:(CGFloat*)depth;
- (NSPoint)projectDirection:(s3g::Vec3)dir distance:(float)distance rect:(NSRect)rect depth:(CGFloat*)depth;
- (int)hitPointAt:(NSPoint)pt inRect:(NSRect)rect;
- (NSRect)mixerGainRect:(uint32_t)index inRect:(NSRect)rect;
- (NSRect)mixerMuteRect:(uint32_t)index inRect:(NSRect)rect;
- (NSRect)mixerSoloRect:(uint32_t)index inRect:(NSRect)rect;
- (NSRect)mixerOutputTrackRect:(NSRect)rect;
- (void)updateMixerGain:(NSPoint)point inRect:(NSRect)rect;
- (void)updateMixerOutput:(NSPoint)point inRect:(NSRect)rect;
- (void)updateDraggedPoint:(NSPoint)point inRect:(NSRect)rect;
- (void)updateSlider:(NSPoint)point;
- (void)storeViewState;
@end

static NSColor* c(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static float linearToSrgb(float v)
{
    const float x = std::clamp(v, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static NSColor* pointColorFromAed(float azDeg, float elDeg, float distance, bool selected, bool enabled)
{
    if (!enabled) return c(0x404848, 0.45);
    const float hue = std::fmod((azDeg / 360.0f) + 1.0f, 1.0f);
    const float light = std::clamp((std::clamp(elDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.28f, 0.88f);
    const float chroma = std::clamp(distance / 2.4f, 0.08f, 1.0f) * 0.37f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float r = linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float g = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float bl = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    const float grayMix = selected ? 0.08f : 0.18f;
    r = r * (1.0f - grayMix) + 0.74f * grayMix;
    g = g * (1.0f - grayMix) + 0.74f * grayMix;
    bl = bl * (1.0f - grayMix) + 0.74f * grayMix;
    return [NSColor colorWithCalibratedRed:r green:g blue:bl alpha:selected ? 1.0 : 0.88];
}

@implementation S3G3OAFXPointEncoderView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 716)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _dragMixerPoint = -1;
        _dragPoint = -1;
        _dragMixerOutput = NO;
        _timer = nil;
        auto* p = static_cast<Plugin*>(plugin);
        _viewMode = p ? p->guiViewMode : 2;
        _leftPage = 0;
        _mixerBank = p ? p->params.selectedPoint / kMixerBankSize : 0u;
        _viewAzDeg = p ? p->guiViewAzDeg : -35.0;
        _viewElDeg = p ? p->guiViewElDeg : 34.0;
        _viewZoom = p ? p->guiViewZoom : 1.0;
        _hasPointSelection = NO;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _menuOrigin = NSMakePoint(0, 0);
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) {
        [self removeTrackingArea:area];
    }
    [super updateTrackingAreas];
    NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:[area autorelease]];
}
- (void)dealloc { [self storeViewState]; [self stopRefreshTimer]; [super dealloc]; }
- (void)storeViewState
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    p->guiViewMode = _viewMode;
    p->guiViewAzDeg = _viewAzDeg;
    p->guiViewElDeg = _viewElDeg;
    p->guiViewZoom = _viewZoom;
}
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/60.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, small, style, 640, 724, 832, 92);
}
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    s3g::clap_gui::drawMenu(name, value, y, attrs, small, style, 640, 724, 124);
}
- (void)drawOpenMenu:(NSDictionary*)attrs
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    static NSString* muteItems[] = { @"ON", @"MUTED" };
    static NSString* hemiItems[] = { @"FULL SPHERE", @"UPPER HEMI" };
    static NSString* motionItems[] = { @"OFF", @"DRIFT", @"ORBIT", @"SWARM", @"PULSE", @"PHYS" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    std::array<NSString*, 5> styleItems {};
    NSString** items = motionItems;
    if (_openMenu == 1) items = muteItems;
    else if (_openMenu == 2) items = hemiItems;
    else if (_openMenu == 5) items = orderItems;
    const CGFloat itemH = 18.0;
    const CGFloat w = 124.0;
    NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, w, itemH * static_cast<CGFloat>(_menuItemCount));
    s3g::clap_gui::Style style;
    auto* p = static_cast<Plugin*>(_plugin);
    int selected = -1;
    if (p) {
        const auto prm = p->encoder.params();
        if (_openMenu == 4) {
            const MotionStyleList styles = motionStyleList(prm.motionMode);
            for (uint32_t i = 0; i < styles.count; ++i) {
                styleItems[i] = [NSString stringWithUTF8String:styleName(styles.scenes[i])];
            }
            items = styleItems.data();
        }
        if (_openMenu == 1) selected = prm.selectedEnabled ? 0 : 1;
        else if (_openMenu == 2) selected = prm.upperHemisphereOnly ? 1 : 0;
        else if (_openMenu == 3) selected = static_cast<int>(prm.motionMode);
        else if (_openMenu == 4) selected = styleMenuIndex(prm.motionMode, prm.motionScene);
        else if (_openMenu == 5) selected = static_cast<int>(prm.order) - 1;
    }
    s3g::clap_gui::drawDropdownMenu(menuRect, itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    const CGFloat itemH = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 124.0, itemH * static_cast<CGFloat>(_menuItemCount));
    const int next = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 38.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    const CGFloat x = NSMaxX(rect) - 10.0 - (3.0 - static_cast<CGFloat>(index)) * w - (2.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, rect.origin.y + 4.0, w, h);
}
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 18.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 4.0;
    const CGFloat viewStart = [self viewButtonRect:0 inRect:rect].origin.x;
    const CGFloat x = viewStart - 12.0 - (2.0 - static_cast<CGFloat>(index)) * w - (1.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, rect.origin.y + 4.0, w, h);
}
- (NSRect)pageButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 48.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    return NSMakeRect(rect.origin.x + 104.0 + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 4.0, w, h);
}
- (NSRect)mixerBankButtonRect:(uint32_t)index inRect:(NSRect)rect
{
    const CGFloat w = 42.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 4.0;
    const CGFloat total = static_cast<CGFloat>(kMixerBankCount) * w + static_cast<CGFloat>(kMixerBankCount - 1u) * gap;
    return NSMakeRect(NSMaxX(rect) - 10.0 - total + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 4.0, w, h);
}
- (void)drawPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"FIELD", @"MIXER" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 2; ++i) {
        s3g::clap_gui::drawHeaderButton([self pageButtonRect:i inRect:rect], rect, labels[i], i == _leftPage, attrs, style);
    }
}
- (void)drawMixerBankButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t active = p ? std::clamp<uint32_t>(p->params.activePoints, 1u, kPointCount) : kDefaultPointCount;
    const uint32_t bankCount = std::max<uint32_t>(1u, (active + kMixerBankSize - 1u) / kMixerBankSize);
    _mixerBank = std::min<uint32_t>(_mixerBank, bankCount - 1u);
    s3g::clap_gui::Style style;
    for (uint32_t bank = 0; bank < bankCount; ++bank) {
        const uint32_t first = bank * kMixerBankSize + 1u;
        const uint32_t last = std::min<uint32_t>((bank + 1u) * kMixerBankSize, active);
        NSString* label = [NSString stringWithFormat:@"%u-%u", first, last];
        s3g::clap_gui::drawHeaderButton([self mixerBankButtonRect:bank inRect:rect], rect, label, bank == _mixerBank, attrs, style);
    }
}
- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"-", @"+" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 2; ++i) {
        s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i inRect:rect], rect, labels[i], false, attrs, style);
    }
}
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:rect], rect, labels[i], i == _viewMode, attrs, style);
    }
}
- (CGFloat)viewScaleForRect:(NSRect)rect
{
    return std::min(rect.size.width, rect.size.height) * 0.34 * std::clamp(_viewZoom, 0.55, 2.20);
}
- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = -35.0;
        _viewElDeg = 34.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}
- (NSPoint)projectWorldPoint:(s3g::Vec3)p rect:(NSRect)rect depth:(CGFloat*)depth
{
    const CGFloat cx = rect.origin.x + rect.size.width * 0.50;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    const CGFloat scale = [self viewScaleForRect:rect];
    if (_viewMode == 0) {
        if (depth) *depth = static_cast<CGFloat>(p.z);
        return NSMakePoint(cx - static_cast<CGFloat>(p.y) * scale,
                           cy - static_cast<CGFloat>(p.x) * scale);
    }
    if (_viewMode == 1) {
        if (depth) *depth = static_cast<CGFloat>(p.x);
        return NSMakePoint(cx - static_cast<CGFloat>(p.y) * scale,
                           cy - static_cast<CGFloat>(p.z) * scale);
    }
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * p.x - sa * p.y;
    const float y1 = sa * p.x + ca * p.y;
    const float y2 = ce * y1 - se * p.z;
    const float z2 = se * y1 + ce * p.z;
    if (depth) *depth = static_cast<CGFloat>(z2);
    return NSMakePoint(cx + static_cast<CGFloat>(x1) * scale, cy - static_cast<CGFloat>(y2) * scale);
}
- (NSPoint)projectDirection:(s3g::Vec3)dir distance:(float)distance rect:(NSRect)rect depth:(CGFloat*)depth
{
    return [self projectWorldPoint:{ dir.x * distance, dir.y * distance, dir.z * distance } rect:rect depth:depth];
}
- (int)hitPointAt:(NSPoint)pt inRect:(NSRect)rect
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto points = p->encoder.points();
    const uint32_t active = std::clamp<uint32_t>(p->params.activePoints, 1u, kPointCount);
    int hit = -1;
    CGFloat best = 999999.0;
    for (uint32_t i = 0; i < active; ++i) {
        const auto& point = points[i];
        if (!point.enabled) continue;
        CGFloat depth = 0.0;
        const s3g::Vec3 dir = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
        const NSPoint projected = [self projectDirection:dir distance:point.distance rect:rect depth:&depth];
        const CGFloat dx = pt.x - projected.x;
        const CGFloat dy = pt.y - projected.y;
        const CGFloat d2 = dx * dx + dy * dy;
        if (d2 < best && d2 <= 144.0) {
            best = d2;
            hit = static_cast<int>(i);
        }
    }
    return hit;
}
- (void)drawPointField:(NSRect)rect attrs:(NSDictionary*)attrs
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto points = p->encoder.points();
    const auto collisionEnergy = p->encoder.collisionEnergy();
    const auto bondRelease = p->encoder.bondRelease();
    const auto params = p->params;
    const uint32_t active = std::clamp<uint32_t>(params.activePoints, 1u, kPointCount);
    [c(0x111111) setFill]; NSRectFill(rect);
    [c(0x565656) setStroke]; NSFrameRect(rect);
    [c(0x131313) setFill]; NSRectFill(NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width, 21));
    [c(0xb8b8b8) setFill]; NSRectFill(NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width, 2));
    [@"POINT FIELD" drawAtPoint:NSMakePoint(rect.origin.x + 10, rect.origin.y + 5) withAttributes:attrs];
    [self drawPageButtonsInRect:rect attrs:attrs];
    [self drawZoomButtonsInRect:rect attrs:attrs];
    [self drawViewButtonsInRect:rect attrs:attrs];

    const CGFloat cx = rect.origin.x + rect.size.width * 0.50;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    const CGFloat scale = [self viewScaleForRect:rect];
    [c(0x2b2b2b) setStroke];
    NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cx - scale, cy - scale, scale * 2.0, scale * 2.0)];
    [ring stroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx - scale, cy) toPoint:NSMakePoint(cx + scale, cy)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, cy - scale) toPoint:NSMakePoint(cx, cy + scale)];

    std::array<uint32_t, kPointCount> order {};
    std::array<CGFloat, kPointCount> depths {};
    std::array<NSPoint, kPointCount> projected {};
    std::array<s3g::Vec3, kPointCount> scenePositions {};
    std::array<bool, kPointCount> visible {};
    for (uint32_t i = 0; i < active; ++i) {
        order[i] = i;
        const auto& point = points[i];
        const s3g::Vec3 dir = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
        scenePositions[i] = { dir.x * point.distance, dir.y * point.distance, dir.z * point.distance };
        visible[i] = point.enabled;
        projected[i] = [self projectDirection:dir distance:point.distance rect:rect depth:&depths[i]];
    }
    std::sort(order.begin(), order.begin() + active, [&](uint32_t a, uint32_t b) { return depths[a] < depths[b]; });

    if (params.motionMode == s3g::AmbiPointMotionMode::Phys && params.motionAmount > 0.0001f && params.poltergeist > 0.0001f) {
        const s3g::Vec3 source = p->encoder.perturbationSource();
        const s3g::Vec3 prevSource = p->encoder.previousPerturbationSource();
        const float sourceDistance = std::sqrt(source.x * source.x + source.y * source.y + source.z * source.z);
        if (sourceDistance > 0.0001f) {
            const s3g::Vec3 sourceDir {
                source.x / sourceDistance,
                source.y / sourceDistance,
                source.z / sourceDistance
            };
            CGFloat sourceDepth = 0.0;
            const NSPoint sourcePoint = [self projectDirection:sourceDir distance:sourceDistance rect:rect depth:&sourceDepth];
            const float prevDistance = std::sqrt(prevSource.x * prevSource.x + prevSource.y * prevSource.y + prevSource.z * prevSource.z);
            if (prevDistance > 0.0001f) {
                const s3g::Vec3 prevDir {
                    prevSource.x / prevDistance,
                    prevSource.y / prevDistance,
                    prevSource.z / prevDistance
                };
                CGFloat prevDepth = 0.0;
                const NSPoint prevPoint = [self projectDirection:prevDir distance:prevDistance rect:rect depth:&prevDepth];
                [c(0xd8d8d8, 0.14 + params.poltergeist * 0.32) setStroke];
                NSBezierPath* sweep = [NSBezierPath bezierPath];
                [sweep moveToPoint:prevPoint];
                [sweep lineToPoint:sourcePoint];
                [sweep setLineWidth:1.5];
                [sweep stroke];
            }
            const float influenceRadius = 0.16f + params.poltergeistRadius * 0.78f;
            [c(0xd8d8d8, 0.12 + params.poltergeist * 0.18) setStroke];
            auto drawSphereRing = [&](int plane) {
                NSBezierPath* ringPath = [NSBezierPath bezierPath];
                constexpr int kSegments = 72;
                bool drawing = false;
                for (int s = 0; s <= kSegments; ++s) {
                    const float a = static_cast<float>(s) / static_cast<float>(kSegments) * 2.0f * static_cast<float>(M_PI);
                    const float ca = std::cos(a) * influenceRadius;
                    const float sa = std::sin(a) * influenceRadius;
                    s3g::Vec3 q = source;
                    if (plane == 0) {
                        q.x += ca;
                        q.y += sa;
                    } else if (plane == 1) {
                        q.x += ca;
                        q.z += sa;
                    } else {
                        q.y += ca;
                        q.z += sa;
                    }
                    if (params.upperHemisphereOnly && q.z < 0.0f) {
                        drawing = false;
                        continue;
                    }
                    CGFloat qDepth = 0.0;
                    const NSPoint qp = [self projectWorldPoint:q rect:rect depth:&qDepth];
                    if (!drawing) {
                        [ringPath moveToPoint:qp];
                        drawing = true;
                    } else {
                        [ringPath lineToPoint:qp];
                    }
                }
                [ringPath setLineWidth:1.05];
                [ringPath stroke];
            };
            drawSphereRing(0);
            drawSphereRing(1);
            drawSphereRing(2);
            [c(0xd8d8d8, 0.18 + params.poltergeist * 0.40) setStroke];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(sourcePoint.x - 6.0, sourcePoint.y)
                                      toPoint:NSMakePoint(sourcePoint.x + 6.0, sourcePoint.y)];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(sourcePoint.x, sourcePoint.y - 6.0)
                                      toPoint:NSMakePoint(sourcePoint.x, sourcePoint.y + 6.0)];
            NSFrameRect(NSMakeRect(sourcePoint.x - 4.0, sourcePoint.y - 4.0, 8.0, 8.0));
        }
    }

    std::array<std::array<bool, kPointCount>, kPointCount> edges {};
    for (uint32_t i = 0; i < active; ++i) {
        if (!visible[i]) continue;
        float bestA = 999999.0f;
        float bestB = 999999.0f;
        int neighborA = -1;
        int neighborB = -1;
        for (uint32_t j = 0; j < active; ++j) {
            if (i == j || !visible[j]) continue;
            const float dx = scenePositions[i].x - scenePositions[j].x;
            const float dy = scenePositions[i].y - scenePositions[j].y;
            const float dz = scenePositions[i].z - scenePositions[j].z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < bestA) {
                bestB = bestA;
                neighborB = neighborA;
                bestA = d2;
                neighborA = static_cast<int>(j);
            } else if (d2 < bestB) {
                bestB = d2;
                neighborB = static_cast<int>(j);
            }
        }
        if (neighborA >= 0) {
            const uint32_t a = std::min<uint32_t>(i, static_cast<uint32_t>(neighborA));
            const uint32_t b = std::max<uint32_t>(i, static_cast<uint32_t>(neighborA));
            edges[a][b] = true;
        }
        if (neighborB >= 0) {
            const uint32_t a = std::min<uint32_t>(i, static_cast<uint32_t>(neighborB));
            const uint32_t b = std::max<uint32_t>(i, static_cast<uint32_t>(neighborB));
            edges[a][b] = true;
        }
    }

    for (uint32_t a = 0; a < active; ++a) {
        for (uint32_t b = a + 1u; b < active; ++b) {
            if (!edges[a][b]) continue;
            const CGFloat release = std::clamp<CGFloat>(std::max(bondRelease[a], bondRelease[b]) / 2.0f, 0.0, 1.0);
            if (release > 0.64) continue;
            const CGFloat energy = std::clamp<CGFloat>(std::max(collisionEnergy[a], collisionEnergy[b]), 0.0, 1.0);
            const CGFloat heal = 1.0 - release;
            [c(energy > 0.02 ? 0xb8b8b8 : 0x6a6a6a, (0.48 + energy * 0.32) * heal) setStroke];
            NSBezierPath* edge = [NSBezierPath bezierPath];
            [edge setLineWidth:(((_hasPointSelection && (a == params.selectedPoint || b == params.selectedPoint)) ? 1.35 : 0.85) + energy * 0.85) * std::max<CGFloat>(0.45, heal)];
            [edge moveToPoint:projected[a]];
            [edge lineToPoint:projected[b]];
            [edge stroke];
        }
    }

    for (uint32_t sortedIndex = 0; sortedIndex < active; ++sortedIndex) {
        const uint32_t i = order[sortedIndex];
        const auto& point = points[i];
        if (!point.enabled) continue;
        const CGFloat x = projected[i].x;
        const CGFloat y = projected[i].y;
        const bool selected = _hasPointSelection && i == params.selectedPoint;
        const CGFloat depthNorm = std::clamp((depths[i] + 2.0) / 4.0, 0.0, 1.0);
        const CGFloat energy = std::clamp<CGFloat>(collisionEnergy[i], 0.0, 1.0);
        const CGFloat release = std::clamp<CGFloat>(bondRelease[i] / 2.0f, 0.0, 1.0);
        const CGFloat r = selected ? 6.0 : 3.0 + depthNorm * 1.2;
        [pointColorFromAed(point.azimuthDeg, point.elevationDeg, point.distance, selected, point.enabled) setFill];
        NSRectFill(NSMakeRect(x - r, y - r, r * 2.0, r * 2.0));
        if (energy > 0.02) {
            [c(0xf0f0f0, 0.25 + energy * 0.45) setStroke];
            NSFrameRect(NSMakeRect(x - r - 3.0, y - r - 3.0, r * 2.0 + 6.0, r * 2.0 + 6.0));
        }
        if (release > 0.02) {
            [c(0xf0f0f0, 0.12 + release * 0.36) setStroke];
            NSBezierPath* releaseBox = [NSBezierPath bezierPathWithRect:NSMakeRect(x - r - 5.0, y - r - 5.0, r * 2.0 + 10.0, r * 2.0 + 10.0)];
            CGFloat dash[] = { 2.0, 2.0 };
            [releaseBox setLineDash:dash count:2 phase:0.0];
            [releaseBox stroke];
        }
        if (selected) {
            [c(0xf2f2f2) setStroke];
            NSFrameRect(NSMakeRect(x - 10.0, y - 10.0, 20.0, 20.0));
        }
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        NSDictionary* idAttrs = @{ NSForegroundColorAttributeName:selected ? c(0xc8c8c8) : c(0x151515),
                                   NSFontAttributeName:s3g::clap_gui::uiFont(7.5) };
        NSSize size = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(x - size.width * 0.5, y - size.height * 0.5 - 0.5) withAttributes:idAttrs];
    }

    NSString* readout = _hasPointSelection
        ? [NSString stringWithFormat:@"P%u  AZ %+5.1f  EL %+5.1f  DST %.2f  %@",
           params.selectedPoint + 1u,
           params.selectedAzimuthDeg,
           params.selectedElevationDeg,
           params.selectedDistance,
           params.selectedEnabled ? @"ON" : @"MUT"]
        : @"NO POINT";
    [readout drawAtPoint:NSMakePoint(rect.origin.x + 10, NSMaxY(rect) - 24) withAttributes:attrs];
}
- (NSRect)mixerGainRect:(uint32_t)index inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(index % kMixerBankSize) * laneW;
    return NSMakeRect(x + 8.0, rect.origin.y + 128.0, 12.0, rect.size.height - 196.0);
}
- (NSRect)mixerMuteRect:(uint32_t)index inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(index % kMixerBankSize) * laneW;
    return NSMakeRect(x + 1.0, NSMaxY(rect) - 42.0, 14.0, 14.0);
}
- (NSRect)mixerSoloRect:(uint32_t)index inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(index % kMixerBankSize) * laneW;
    return NSMakeRect(x + 17.0, NSMaxY(rect) - 42.0, 14.0, 14.0);
}
- (NSRect)mixerOutputTrackRect:(NSRect)rect
{
    return NSMakeRect(rect.origin.x + 92.0, rect.origin.y + 75.0, rect.size.width - 182.0, 9.0);
}
- (void)drawPointMixer:(NSRect)rect attrs:(NSDictionary*)attrs labelAttrs:(NSDictionary*)labelAttrs
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto points = p->encoder.editPoints();
    const auto params = p->params;
    const uint32_t active = std::clamp<uint32_t>(params.activePoints, 1u, kPointCount);
    const uint32_t bankCount = std::max<uint32_t>(1u, (active + kMixerBankSize - 1u) / kMixerBankSize);
    _mixerBank = std::min<uint32_t>(_mixerBank, bankCount - 1u);
    const uint32_t firstPoint = _mixerBank * kMixerBankSize;
    const uint32_t pointEnd = std::min<uint32_t>(firstPoint + kMixerBankSize, active);
    bool anySolo = false;
    for (uint32_t i = 0; i < active; ++i) anySolo = anySolo || points[i].solo;

    [c(0x111111) setFill]; NSRectFill(rect);
    [c(0x565656) setStroke]; NSFrameRect(rect);
    [c(0x131313) setFill]; NSRectFill(NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width, 21));
    [c(0xb8b8b8) setFill]; NSRectFill(NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width, 2));
    [@"POINT MIXER" drawAtPoint:NSMakePoint(rect.origin.x + 10, rect.origin.y + 5) withAttributes:labelAttrs];
    [self drawPageButtonsInRect:rect attrs:attrs];
    [self drawMixerBankButtonsInRect:rect attrs:attrs];

    s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    s3g::clap_gui::drawSlider(@"OUTPUT",
                               [NSString stringWithFormat:@"%+.1f", params.outputGainDb],
                               (params.outputGainDb + 60.0f) / 72.0f,
                               rect.origin.y + 72.0,
                               attrs,
                               attrs,
                               style,
                               rect.origin.x + 12.0,
                               rect.origin.x + 92.0,
                               rect.origin.x + rect.size.width - 78.0,
                               rect.size.width - 182.0);

    const CGFloat laneW = 34.0;
    for (uint32_t i = firstPoint; i < pointEnd; ++i) {
        const auto& point = points[i];
        const uint32_t slot = i - firstPoint;
        const CGFloat laneX = rect.origin.x + 12.0 + static_cast<CGFloat>(slot) * laneW;
        const bool selected = _hasPointSelection && i == params.selectedPoint;
        const bool audible = point.enabled && (!anySolo || point.solo);
        if (selected) {
            [c(0x242424) setFill];
            NSRectFill(NSMakeRect(laneX - 2.0, rect.origin.y + 96.0, laneW - 3.0, rect.size.height - 102.0));
            [c(0x777777) setStroke];
            NSFrameRect(NSMakeRect(laneX - 2.0, rect.origin.y + 96.0, laneW - 3.0, rect.size.height - 102.0));
        }

        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        [label drawAtPoint:NSMakePoint(laneX + (i < 9 ? 10.0 : 7.0), rect.origin.y + 97.0) withAttributes:attrs];

        NSRect track = [self mixerGainRect:i inRect:rect];
        [c(0x181818) setFill]; NSRectFill(track);
        [c(0x545454) setStroke]; NSFrameRect(track);
        const CGFloat norm = std::clamp<CGFloat>(point.gain / 2.0f, 0.0, 1.0);
        NSRect fill = NSInsetRect(track, 2.0, 2.0);
        const CGFloat fullH = fill.size.height;
        fill.origin.y += fullH * (1.0 - norm);
        fill.size.height = std::max<CGFloat>(1.0, fullH * norm);
        [pointColorFromAed(point.azimuthDeg, point.elevationDeg, point.distance, selected, audible) setFill];
        NSRectFill(fill);
        [c(selected ? 0xf2f2f2 : 0x9a9a9a) setFill];
        NSRectFill(NSMakeRect(track.origin.x - 2.0,
                              track.origin.y + track.size.height * (1.0 - norm) - 1.0,
                              track.size.width + 4.0,
                              3.0));

        NSRect mute = [self mixerMuteRect:i inRect:rect];
        [c(point.enabled ? 0x151515 : 0x3a3a3a) setFill]; NSRectFill(mute);
        [c(point.enabled ? 0x5a5a5a : 0xd1d1d1) setStroke]; NSFrameRect(mute);
        [@"M" drawAtPoint:NSMakePoint(mute.origin.x + 3.0, mute.origin.y + 2.0) withAttributes:attrs];

        NSRect solo = [self mixerSoloRect:i inRect:rect];
        [c(point.solo ? 0xd1d1d1 : 0x151515) setFill]; NSRectFill(solo);
        [c(point.solo ? 0xf2f2f2 : 0x5a5a5a) setStroke]; NSFrameRect(solo);
        NSDictionary* soloAttrs = point.solo
            ? @{ NSForegroundColorAttributeName:c(0x111111), NSFontAttributeName:[attrs objectForKey:NSFontAttributeName] }
            : attrs;
        [@"S" drawAtPoint:NSMakePoint(solo.origin.x + 3.0, solo.origin.y + 2.0) withAttributes:soloAttrs];
    }
}
- (void)updateMixerGain:(NSPoint)point inRect:(NSRect)rect
{
    if (_dragMixerPoint < 0 || _dragMixerPoint >= static_cast<int>(kPointCount)) return;
    NSRect track = [self mixerGainRect:static_cast<uint32_t>(_dragMixerPoint) inRect:rect];
    const CGFloat norm = std::clamp((NSMaxY(track) - point.y) / track.size.height, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    applyPerPointParam(*p, static_cast<uint32_t>(_dragMixerPoint), PerPointParamKind::Gain, norm * 2.0);
    applyParam(*p, kPointParamId, static_cast<double>(_dragMixerPoint + 1));
    _hasPointSelection = YES;
    [self setNeedsDisplay:YES];
}
- (void)updateMixerOutput:(NSPoint)point inRect:(NSRect)rect
{
    NSRect track = [self mixerOutputTrackRect:rect];
    const CGFloat norm = std::clamp((point.x - track.origin.x) / track.size.width, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    applyParam(*p, kOutputParamId, -60.0 + norm * 72.0);
    [self setNeedsDisplay:YES];
}
- (void)updateDraggedPoint:(NSPoint)point inRect:(NSRect)rect
{
    if (_dragPoint < 0 || _dragPoint >= static_cast<int>(kPointCount)) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t pointIndex = static_cast<uint32_t>(_dragPoint);
    const auto current = p->encoder.editPoint(pointIndex);
    const CGFloat cx = rect.origin.x + rect.size.width * 0.50;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    const CGFloat scale = [self viewScaleForRect:rect];
    if (scale <= 1.0) return;

    if (_viewMode == 0) {
        const float px = static_cast<float>(std::clamp((cy - point.y) / scale, -2.0, 2.0));
        const float py = static_cast<float>(std::clamp((cx - point.x) / scale, -2.0, 2.0));
        const float elevation = p->params.upperHemisphereOnly
            ? std::max(0.0f, current.elevationDeg)
            : current.elevationDeg;
        const float elRad = elevation * static_cast<float>(M_PI / 180.0);
        const float planar = std::sqrt(px * px + py * py);
        const float distance = std::clamp(planar / std::max(0.05f, std::cos(elRad)), 0.15f, 2.0f);
        const float azimuth = std::atan2(py, px) * static_cast<float>(180.0 / M_PI);
        applyPerPointParam(*p, pointIndex, PerPointParamKind::Azimuth, azimuth);
        applyPerPointParam(*p, pointIndex, PerPointParamKind::Elevation, elevation);
        applyPerPointParam(*p, pointIndex, PerPointParamKind::Distance, distance);
    } else if (_viewMode == 1) {
        const float azRad = current.azimuthDeg * static_cast<float>(M_PI / 180.0);
        const float elRad = current.elevationDeg * static_cast<float>(M_PI / 180.0);
        const float preservedX = std::cos(elRad) * std::cos(azRad) * current.distance;
        const float py = static_cast<float>(std::clamp((cx - point.x) / scale, -2.0, 2.0));
        float pz = static_cast<float>(std::clamp((cy - point.y) / scale, -2.0, 2.0));
        if (p->params.upperHemisphereOnly) pz = std::max(0.0f, pz);
        const float distance = std::clamp(std::sqrt(preservedX * preservedX + py * py + pz * pz), 0.15f, 2.0f);
        const float azimuth = std::atan2(py, preservedX) * static_cast<float>(180.0 / M_PI);
        const float elevation = std::asin(std::clamp(pz / std::max(0.15f, distance), -1.0f, 1.0f)) * static_cast<float>(180.0 / M_PI);
        applyPerPointParam(*p, pointIndex, PerPointParamKind::Azimuth, azimuth);
        applyPerPointParam(*p, pointIndex, PerPointParamKind::Elevation, elevation);
        applyPerPointParam(*p, pointIndex, PerPointParamKind::Distance, distance);
    }
    applyParam(*p, kPointParamId, static_cast<double>(pointIndex + 1u));
    _hasPointSelection = YES;
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* lab = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();

    [@"s3g AMBI POINT ENCODER" drawAtPoint:NSMakePoint(18,14) withAttributes:titleAttrs];
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    [s3g::clap_gui::peakDbText(pk) drawAtPoint:NSMakePoint(716,14) withAttributes:small];
    [[NSString stringWithFormat:@"%uPT > %uOA", p->params.activePoints, p->params.order] drawAtPoint:NSMakePoint(794,14) withAttributes:small];

    NSRect leftPage = NSMakeRect(18, 42, 590, 656);
    if (_leftPage == 0) {
        [self drawPointField:leftPage attrs:small];
    } else {
        [self drawPointMixer:leftPage attrs:small labelAttrs:small];
    }

    s3g::clap_gui::drawPanelFrame(626, 42, 256, 224, style);
    s3g::clap_gui::drawPanelHeader(@"POINT", true, 626, 42, 256, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(626, 280, 256, 406, style);
    s3g::clap_gui::drawPanelHeader(@"MOTION", true, 626, 280, 256, 21, lab, style);

    const auto prm = p->params;
    [self drawSlider:@"POINT" value:[NSString stringWithFormat:@"%u", prm.selectedPoint + 1u] norm:static_cast<CGFloat>(prm.selectedPoint) / static_cast<CGFloat>(std::max<uint32_t>(1u, prm.activePoints - 1u)) y:76 attrs:small small:small];
    [self drawSlider:@"POINTS" value:[NSString stringWithFormat:@"%u", prm.activePoints] norm:static_cast<CGFloat>(prm.activePoints - 1u) / static_cast<CGFloat>(kPointCount - 1u) y:101 attrs:small small:small];
    [self drawSlider:@"AZIMUTH" value:[NSString stringWithFormat:@"%+.0f", prm.selectedAzimuthDeg] norm:(prm.selectedAzimuthDeg + 180.0f) / 360.0f y:126 attrs:small small:small];
    [self drawSlider:@"ELEV" value:[NSString stringWithFormat:@"%+.0f", prm.selectedElevationDeg] norm:(prm.selectedElevationDeg - (prm.upperHemisphereOnly ? 0.0f : -90.0f)) / (prm.upperHemisphereOnly ? 90.0f : 180.0f) y:151 attrs:small small:small];
    [self drawSlider:@"DISTANCE" value:[NSString stringWithFormat:@"%.2f", prm.selectedDistance] norm:(prm.selectedDistance - 0.15f) / 1.85f y:176 attrs:small small:small];
    [self drawSlider:@"GAIN" value:[NSString stringWithFormat:@"%.2f", prm.selectedGain] norm:prm.selectedGain / 2.0f y:201 attrs:small small:small];
    [self drawMenu:@"MUTE" value:(prm.selectedEnabled ? @"ON" : @"MUTED") y:223 attrs:small small:small];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", prm.order] y:245 attrs:small small:small];

    [self drawMenu:@"MOTION" value:[NSString stringWithUTF8String:motionDisplayName(static_cast<uint32_t>(prm.motionMode))] y:314 attrs:small small:small];
    [self drawMenu:@"STYLE" value:[NSString stringWithUTF8String:(styleMenuIndex(prm.motionMode, prm.motionScene) >= 0 ? styleName(prm.motionScene) : "CUSTOM")] y:336 attrs:small small:small];
    [self drawMenu:@"HEMISPHERE" value:(prm.upperHemisphereOnly ? @"UPPER HEMI" : @"FULL SPHERE") y:358 attrs:small small:small];
    [self drawSlider:@"AMOUNT" value:[NSString stringWithFormat:@"%.0f%%", prm.motionAmount * 100.0f] norm:prm.motionAmount y:380 attrs:small small:small];
    [self drawSlider:@"SCALE" value:[NSString stringWithFormat:@"%.2f", prm.physicsScale] norm:(prm.physicsScale - 0.25f) / 1.75f y:402 attrs:small small:small];
    [self drawSlider:@"RATE" value:[NSString stringWithFormat:@"%.3f", prm.rateHz] norm:(prm.rateHz - 0.005f) / 0.495f y:424 attrs:small small:small];
    [self drawSlider:@"COLLISION" value:[NSString stringWithFormat:@"%.0f%%", prm.collision * 100.0f] norm:prm.collision y:446 attrs:small small:small];
    [self drawSlider:@"IMPACT" value:[NSString stringWithFormat:@"%.0f%%", prm.impact * 100.0f] norm:prm.impact y:468 attrs:small small:small];
    [self drawSlider:@"DRAG" value:[NSString stringWithFormat:@"%.2f", prm.drag] norm:(prm.drag - 0.45f) / 0.545f y:490 attrs:small small:small];
    [self drawSlider:@"SWIRL" value:[NSString stringWithFormat:@"%+.2f", prm.swirl] norm:(prm.swirl + 0.24f) / 0.48f y:512 attrs:small small:small];
    [self drawSlider:@"POLTER" value:[NSString stringWithFormat:@"%.0f%%", prm.poltergeist * 100.0f] norm:prm.poltergeist y:534 attrs:small small:small];
    [self drawSlider:@"G-RATE" value:[NSString stringWithFormat:@"%.2fx", prm.poltergeistRate] norm:(prm.poltergeistRate - 0.05f) / 3.95f y:556 attrs:small small:small];
    [self drawSlider:@"G-REACH" value:[NSString stringWithFormat:@"%.0f%%", prm.poltergeistReach * 100.0f] norm:prm.poltergeistReach y:578 attrs:small small:small];
    [self drawSlider:@"G-RAD" value:[NSString stringWithFormat:@"%.2f", prm.poltergeistRadius] norm:(prm.poltergeistRadius - 0.04f) / 0.96f y:600 attrs:small small:small];
    [self drawSlider:@"G-CHAOS" value:[NSString stringWithFormat:@"%.0f%%", prm.poltergeistChaos * 100.0f] norm:prm.poltergeistChaos y:622 attrs:small small:small];
    [self drawSlider:@"DOPPLER" value:[NSString stringWithFormat:@"%.0f%%", prm.doppler * 100.0f] norm:prm.doppler y:644 attrs:small small:small];
    [self drawSlider:@"AIR" value:[NSString stringWithFormat:@"%.0f%%", prm.air * 100.0f] norm:prm.air y:666 attrs:small small:small];

    [self drawOpenMenu:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const double n = std::clamp((point.x - 726.0) / 104.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 1: applyParam(*p, kPointParamId, 1.0 + n * static_cast<double>(std::max<uint32_t>(1u, p->params.activePoints) - 1u)); break;
    case 2: applyParam(*p, kActivePointsParamId, 1.0 + n * static_cast<double>(kPointCount - 1u)); break;
    case 3: applyParam(*p, kAzimuthParamId, -180.0 + n * 360.0); break;
    case 4: applyParam(*p, kElevationParamId, p->params.upperHemisphereOnly ? n * 90.0 : -90.0 + n * 180.0); break;
    case 5: applyParam(*p, kDistanceParamId, 0.15 + n * 1.85); break;
    case 6: applyParam(*p, kPointGainParamId, n * 2.0); break;
    case 7: applyParam(*p, kPointMuteParamId, n >= 0.5 ? 1.0 : 0.0); break;
    case 8: applyParam(*p, kOrderParamId, 1.0 + n * 6.0); break;
    case 9: applyParam(*p, kMotionModeParamId, n * 5.0); break;
    case 10: break;
    case 11: applyParam(*p, kUpperHemisphereParamId, n >= 0.5 ? 1.0 : 0.0); break;
    case 12: applyParam(*p, kMotionAmountParamId, n); break;
    case 13: applyParam(*p, kPhysicsScaleParamId, 0.25 + n * 1.75); break;
    case 14: applyParam(*p, kRateParamId, 0.005 + n * 0.495); break;
    case 15: applyParam(*p, kCollisionParamId, n); break;
    case 16: applyParam(*p, kImpactParamId, n); break;
    case 17: applyParam(*p, kDragParamId, 0.45 + n * 0.545); break;
    case 18: applyParam(*p, kSwirlParamId, -0.24 + n * 0.48); break;
    case 19: applyParam(*p, kPoltergeistParamId, n); break;
    case 20: applyParam(*p, kPoltergeistRateParamId, 0.05 + n * 3.95); break;
    case 21: applyParam(*p, kPoltergeistReachParamId, n); break;
    case 22: applyParam(*p, kPoltergeistRadiusParamId, 0.04 + n * 0.96); break;
    case 23: applyParam(*p, kPoltergeistChaosParamId, n); break;
    case 24: applyParam(*p, kDopplerParamId, n); break;
    case 25: applyParam(*p, kAirParamId, n); break;
    default: break;
    }
    if (_dragSlider == 1 || _dragSlider == 2) _mixerBank = p->params.selectedPoint / kMixerBankSize;
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto menuOrigin = [](CGFloat preferredY, uint32_t itemCount) {
        const CGFloat itemH = 18.0;
        const CGFloat bottom = 654.0;
        return NSMakePoint(724.0, std::max<CGFloat>(28.0, std::min<CGFloat>(preferredY, bottom - itemH * static_cast<CGFloat>(itemCount))));
    };

    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 124.0, itemH * static_cast<CGFloat>(_menuItemCount));
        if (NSPointInRect(pt, menuRect)) {
            const uint32_t index = std::min<uint32_t>(_menuItemCount - 1u, static_cast<uint32_t>((pt.y - _menuOrigin.y) / itemH));
            auto* p = static_cast<Plugin*>(_plugin);
            if (_openMenu == 1) {
                applyParam(*p, kPointMuteParamId, index == 0 ? 0.0 : 1.0);
            } else if (_openMenu == 2) {
                applyParam(*p, kUpperHemisphereParamId, index == 0 ? 0.0 : 1.0);
            } else if (_openMenu == 3) {
                applyParam(*p, kMotionModeParamId, static_cast<double>(index));
            } else if (_openMenu == 4) {
                const MotionStyleList styles = motionStyleList(p->params.motionMode);
                applyParam(*p, kMotionSceneParamId, static_cast<double>(styles.scenes[std::min<uint32_t>(index, styles.count - 1u)]));
            } else if (_openMenu == 5) {
                applyParam(*p, kOrderParamId, static_cast<double>(index + 1u));
            }
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        [self setNeedsDisplay:YES];
        return;
    }

    const NSRect leftPage = NSMakeRect(18, 42, 590, 656);
    if (NSPointInRect(pt, leftPage)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self pageButtonRect:i inRect:leftPage])) {
                _leftPage = i;
                if (i == 1) _mixerBank = static_cast<Plugin*>(_plugin)->params.selectedPoint / kMixerBankSize;
                _dragView = NO;
                _dragMixerPoint = -1;
                _dragPoint = -1;
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }

    if (_leftPage == 1 && NSPointInRect(pt, leftPage)) {
        auto* p = static_cast<Plugin*>(_plugin);
        const uint32_t active = std::clamp<uint32_t>(p->params.activePoints, 1u, kPointCount);
        const uint32_t bankCount = std::max<uint32_t>(1u, (active + kMixerBankSize - 1u) / kMixerBankSize);
        for (uint32_t bank = 0; bank < bankCount; ++bank) {
            if (NSPointInRect(pt, [self mixerBankButtonRect:bank inRect:leftPage])) {
                _mixerBank = bank;
                _dragMixerPoint = -1;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        _mixerBank = std::min<uint32_t>(_mixerBank, bankCount - 1u);
        const uint32_t firstPoint = _mixerBank * kMixerBankSize;
        const uint32_t pointEnd = std::min<uint32_t>(firstPoint + kMixerBankSize, active);
        if (NSPointInRect(pt, NSInsetRect([self mixerOutputTrackRect:leftPage], -8.0, -8.0))) {
            _dragMixerOutput = YES;
            [self updateMixerOutput:pt inRect:leftPage];
            return;
        }
        for (uint32_t i = firstPoint; i < pointEnd; ++i) {
            if (NSPointInRect(pt, [self mixerMuteRect:i inRect:leftPage])) {
                const auto point = p->encoder.editPoint(i);
                applyPerPointParam(*p, i, PerPointParamKind::Mute, point.enabled ? 1.0 : 0.0);
                applyParam(*p, kPointParamId, static_cast<double>(i + 1u));
                _hasPointSelection = YES;
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(pt, [self mixerSoloRect:i inRect:leftPage])) {
                const auto point = p->encoder.editPoint(i);
                applyPerPointParam(*p, i, PerPointParamKind::Solo, point.solo ? 0.0 : 1.0);
                applyParam(*p, kPointParamId, static_cast<double>(i + 1u));
                _hasPointSelection = YES;
                [self setNeedsDisplay:YES];
                return;
            }
            NSRect gainHit = NSInsetRect([self mixerGainRect:i inRect:leftPage], -10.0, -10.0);
            if (NSPointInRect(pt, gainHit)) {
                _dragMixerPoint = static_cast<int>(i);
                [self updateMixerGain:pt inRect:leftPage];
                return;
            }
        }
    }

    if (_leftPage == 0 && NSPointInRect(pt, leftPage)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self zoomButtonRect:i inRect:leftPage])) {
                const CGFloat step = i == 0 ? -0.15 : 0.15;
                _viewZoom = std::clamp(_viewZoom + step, 0.55, 2.20);
                [self storeViewState];
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self viewButtonRect:i inRect:leftPage])) {
                [self setViewPreset:i];
                return;
            }
        }
        const int hit = [self hitPointAt:pt inRect:leftPage];
        if (hit >= 0) {
            auto* p = static_cast<Plugin*>(_plugin);
            applyParam(*p, kPointParamId, static_cast<double>(hit + 1));
            _mixerBank = static_cast<uint32_t>(hit) / kMixerBankSize;
            _hasPointSelection = YES;
            if (_viewMode == 0 || _viewMode == 1) {
                _dragPoint = hit;
                [self updateDraggedPoint:pt inRect:leftPage];
                return;
            }
            [self setNeedsDisplay:YES];
            return;
        }
        if (pt.y > leftPage.origin.y + 21.0) {
            _hasPointSelection = NO;
            _dragView = YES;
            _lastDragPoint = pt;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const CGFloat rows[] = { 76, 101, 126, 151, 176, 201, 223, 245, 314, 336, 358, 380, 402, 424, 446, 468, 490, 512, 534, 556, 578, 600, 622, 644, 666 };
    for (int i = 0; i < 25; ++i) {
        if (NSPointInRect(pt, NSMakeRect(636, rows[i] - 8, 236, 24))) {
            if (i == 6 || i == 7 || i == 8 || i == 9 || i == 10) {
                _openMenu = i == 6 ? 1 : (i == 7 ? 5 : (i == 8 ? 3 : (i == 9 ? 4 : 2)));
                _hoverMenuItem = -1;
                if (i == 7) _menuItemCount = 7u;
                else if (i == 8) _menuItemCount = 6u;
                else if (i == 9) _menuItemCount = motionStyleList(static_cast<Plugin*>(_plugin)->params.motionMode).count;
                else _menuItemCount = 2u;
                _menuOrigin = menuOrigin(rows[i] + 18.0, _menuItemCount);
                [self setNeedsDisplay:YES];
                return;
            }
            _dragSlider = i + 1;
            if (i <= 6) _hasPointSelection = YES;
            [self updateSlider:pt];
            return;
        }
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]];
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:pt];
    if (_dragPoint >= 0) {
        [self updateDraggedPoint:pt inRect:NSMakeRect(18, 42, 590, 656)];
        return;
    }
    if (_dragMixerPoint >= 0) {
        [self updateMixerGain:pt inRect:NSMakeRect(18, 42, 590, 656)];
        return;
    }
    if (_dragMixerOutput) {
        [self updateMixerOutput:pt inRect:NSMakeRect(18, 42, 590, 656)];
        return;
    }
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = pt;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragSlider > 0) [self updateSlider:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; _dragMixerPoint = -1; _dragPoint = -1; _dragMixerOutput = NO; _dragView = NO; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3G3OAFXPointEncoderView alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), 900u, 716u)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; [static_cast<S3G3OAFXPointEncoderView*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, 900u, 716u, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, 900u, 716u, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3G3OAFXPointEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3G3OAFXPointEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.3oafx-point-encoder-16pt",
    "s3g Ambi Point Encoder",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Up to 64 point-source inputs to selectable first- through seventh-order ACN/SN3D ambisonics with AED placement and physics motion.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->params.activePoints = kDefaultPointCount;
    p->host = host;
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
    p->plugin.get_extension = pluginGetExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
