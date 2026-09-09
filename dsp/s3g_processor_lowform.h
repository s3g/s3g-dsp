#pragma once

#include "s3g_bass_amplifier.h"
#include "s3g_bass_shred.h"
#include "s3g_break_bus.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

enum class BassVoiceMode : uint32_t { Mono = 0u, Poly };
enum class BassFoundationWave : uint32_t { Sine = 0u, Triangle, Rounded };
enum class BassBodyEngine : uint32_t {
    Pressure = 0u, Swarm, Modal, Acid, Rave, Phase, Throat, Sync
};
enum class BassTextureMode : uint32_t { Off = 0u, Noise, Corrode, Ring, Wire };
enum class BassFilterType : uint32_t { Bypass = 0u, Low12, Low24, Band12, High12, Notch };
enum class BassModShape : uint32_t { Off = 0u, Sine, Triangle, Ramp, Square, Random, Steps };
enum class BassModTarget : uint32_t {
    Off = 0u, Amp, Pitch, BodyMorph, BodyDensity, TextureLevel,
    TextureColor, Cutoff, Resonance, Width,
    FoundationLevel, BodyLevel, BodyControlC, BodyControlD, BodyControlE,
    BodyWidth, TextureTrack, TextureWidth, FilterDrive, KeyTrack,
    AmplifierTube, ShredAmount, ShredFeedback, ShredColor, ShredMix,
    DynamicsAmount, DynamicsBite, DynamicsSaturation, DynamicsTilt
};
enum class BassMotionClock : uint32_t { Free = 0u, Transport };

inline constexpr uint32_t kBassVoiceCount = 8u;
inline constexpr uint32_t kBassModCount = 3u;
inline constexpr uint32_t kBassModShapeCount = 7u;
inline constexpr uint32_t kBassModTargetCount = 29u;
inline constexpr uint32_t kBassExpressionSourceCount = 3u;

inline const char* bassVoiceModeName(BassVoiceMode v)
{
    return v == BassVoiceMode::Poly ? "POLY 8" : "MONO";
}

inline const char* bassFoundationWaveName(BassFoundationWave v)
{
    switch (v) {
    case BassFoundationWave::Sine: return "SINE";
    case BassFoundationWave::Triangle: return "TRIANGLE";
    case BassFoundationWave::Rounded: return "ROUNDED";
    }
    return "SINE";
}

inline const char* bassBodyEngineName(BassBodyEngine v)
{
    switch (v) {
    case BassBodyEngine::Pressure: return "PRESSURE";
    case BassBodyEngine::Swarm: return "SWARM";
    case BassBodyEngine::Modal: return "MODAL";
    case BassBodyEngine::Acid: return "ACID";
    case BassBodyEngine::Rave: return "RAVE";
    case BassBodyEngine::Phase: return "PHASE";
    case BassBodyEngine::Throat: return "THROAT";
    case BassBodyEngine::Sync: return "SYNC";
    }
    return "PRESSURE";
}

inline const char* bassTextureModeName(BassTextureMode v)
{
    switch (v) {
    case BassTextureMode::Off: return "OFF";
    case BassTextureMode::Noise: return "NOISE";
    case BassTextureMode::Corrode: return "CORRODE";
    case BassTextureMode::Ring: return "RING";
    case BassTextureMode::Wire: return "WIRE";
    }
    return "OFF";
}

inline const char* bassFilterTypeName(BassFilterType v)
{
    switch (v) {
    case BassFilterType::Bypass: return "BYPASS";
    case BassFilterType::Low12: return "LOW 12";
    case BassFilterType::Low24: return "LOW 24";
    case BassFilterType::Band12: return "BAND 12";
    case BassFilterType::High12: return "HIGH 12";
    case BassFilterType::Notch: return "NOTCH";
    }
    return "BYPASS";
}

inline const char* bassModShapeName(BassModShape v)
{
    switch (v) {
    case BassModShape::Off: return "OFF";
    case BassModShape::Sine: return "SINE";
    case BassModShape::Triangle: return "TRIANGLE";
    case BassModShape::Ramp: return "RAMP";
    case BassModShape::Square: return "SQUARE";
    case BassModShape::Random: return "RANDOM";
    case BassModShape::Steps: return "STEPS";
    }
    return "OFF";
}

inline const char* bassModTargetName(BassModTarget v)
{
    switch (v) {
    case BassModTarget::Off: return "OFF";
    case BassModTarget::Amp: return "VOICE / AMPLITUDE";
    case BassModTarget::Pitch: return "VOICE / PITCH";
    case BassModTarget::BodyMorph: return "BODY / CONTROL A";
    case BassModTarget::BodyDensity: return "BODY / CONTROL B";
    case BassModTarget::TextureLevel: return "TEXTURE / LEVEL";
    case BassModTarget::TextureColor: return "TEXTURE / COLOR";
    case BassModTarget::Cutoff: return "FILTER / CUTOFF";
    case BassModTarget::Resonance: return "FILTER / RESONANCE";
    // Keep the original value 9 as the combined-width destination so older
    // projects retain their exact modulation routing.
    case BassModTarget::Width: return "BODY + TEXTURE / WIDTH";
    case BassModTarget::FoundationLevel: return "FOUNDATION / LEVEL";
    case BassModTarget::BodyLevel: return "BODY / LEVEL";
    case BassModTarget::BodyControlC: return "BODY / CONTROL C";
    case BassModTarget::BodyControlD: return "BODY / CONTROL D";
    case BassModTarget::BodyControlE: return "BODY / CONTROL E";
    case BassModTarget::BodyWidth: return "BODY / WIDTH";
    case BassModTarget::TextureTrack: return "TEXTURE / PITCH TRACK";
    case BassModTarget::TextureWidth: return "TEXTURE / WIDTH";
    case BassModTarget::FilterDrive: return "FILTER / DRIVE";
    case BassModTarget::KeyTrack: return "FILTER / KEY TRACK";
    case BassModTarget::AmplifierTube: return "AMPLIFIER / TUBE";
    case BassModTarget::ShredAmount: return "SHRED / AMOUNT";
    case BassModTarget::ShredFeedback: return "SHRED / FEEDBACK";
    case BassModTarget::ShredColor: return "SHRED / COLOR";
    case BassModTarget::ShredMix: return "SHRED / MIX";
    case BassModTarget::DynamicsAmount: return "DYNAMICS / AMOUNT";
    case BassModTarget::DynamicsBite: return "DYNAMICS / BITE";
    case BassModTarget::DynamicsSaturation: return "DYNAMICS / SATURATION";
    case BassModTarget::DynamicsTilt: return "DYNAMICS / TILT";
    }
    return "OFF";
}

inline const char* bassMotionClockName(BassMotionClock v)
{
    return v == BassMotionClock::Transport ? "TRANSPORT" : "FREE";
}

inline constexpr std::array<const char*, 16u> kBassMotionDivisionNames {{
    "8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/2T", "1/4",
    "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T",
    "1/64", "1/64T"
}};

inline constexpr std::array<float, 16u> kBassMotionDivisionBeats {{
    32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 4.0f / 3.0f, 1.0f,
    2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f,
    0.125f, 1.0f / 12.0f, 0.0625f, 1.0f / 24.0f
}};

struct BassModParams {
    float shape = static_cast<float>(BassModShape::Off);
    float rate = 0.35f;
    float depth = 0.0f;
    float target = static_cast<float>(BassModTarget::Off);
    float secondaryDepth = 0.0f;
    float secondaryTarget = static_cast<float>(BassModTarget::Off);
};

struct BassExpressionRoute {
    float target = static_cast<float>(BassModTarget::Off);
    float depth = 0.0f;
};

struct LowformParams {
    float outputGainDb = -12.0f;
    float voiceMode = static_cast<float>(BassVoiceMode::Poly);
    float glideMs = 28.0f;

    float foundationWave = static_cast<float>(BassFoundationWave::Sine);
    float foundationOctave = 0.0f;
    float foundationLevel = 0.93f;
    float pitchPunchSemitones = 3.0f;
    float punchTimeMs = 48.0f;
    float foundationReturn = 0.72f;

    float bodyEngine = static_cast<float>(BassBodyEngine::Pressure);
    float bodyLevel = 0.62f;
    float bodyControlA = 0.42f;
    float bodyControlB = 0.48f;
    float bodyWidth = 0.47f;
    float bodyControlC = 0.40f;
    float bodyControlD = 0.24f;
    float bodyControlE = 0.59f;

    float textureMode = static_cast<float>(BassTextureMode::Off);
    float textureLevel = 0.0f;
    float textureColor = 0.52f;
    float textureTrack = 0.72f;
    float textureWidth = 0.47f;

    float filterType = static_cast<float>(BassFilterType::Low24);
    float cutoffHz = 820.0f;
    float resonance = 0.18f;
    float filterDrive = 0.31f;
    float keyTrack = 0.34f;
    float foundationFilter = 0.12f;
    float bodyFilter = 1.0f;
    float textureFilter = 1.0f;

    float attackSeconds = 0.004f;
    float decaySeconds = 0.22f;
    float sustain = 0.78f;
    float releaseSeconds = 0.30f;
    float filterEnvelopeOctaves = 1.4f;
    float filterDecaySeconds = 0.18f;

    float motionClock = static_cast<float>(BassMotionClock::Free);
    std::array<BassModParams, kBassModCount> mods {{
        { static_cast<float>(BassModShape::Sine), 0.25f, 0.0f,
            static_cast<float>(BassModTarget::Cutoff) },
        { static_cast<float>(BassModShape::Triangle), 0.47f, 0.0f,
            static_cast<float>(BassModTarget::BodyMorph) },
        { static_cast<float>(BassModShape::Random), 0.60f, 0.0f,
            static_cast<float>(BassModTarget::TextureLevel) },
    }};
    std::array<BassExpressionRoute, kBassExpressionSourceCount>
        expressionRoutes {{
            { static_cast<float>(BassModTarget::Amp), 0.55f },
            { static_cast<float>(BassModTarget::BodyDensity), 0.44f },
            { static_cast<float>(BassModTarget::BodyMorph), 0.60f },
        }};

    float bodyDecaySeconds = 12.0f;
    float textureAttackSeconds = 0.0f;
    float textureDecaySeconds = 12.0f;

    float modalDrive = 0.25f;
    float modWheelAmount = 0.75f;
    float dynamicsBite = 0.03f;

    float tube = 0.20f;
    float shredCircuit = static_cast<float>(BassShredCircuit::Shred);
    float shred = 0.06f;
    float shredFeedback = 0.0f;
    float shredColor = 0.55f;
    float shredMix = 0.0f;
    float dynamics = 0.26f;
    float saturation = 0.12f;
    float clip = 0.0f;
    float tilt = 0.0f;
    float maximizer = 0.24f;
};

class Lowform {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::clamp(sampleRate, 8000.0, 768000.0) : 48000.0;
        amplifier_.prepare(sampleRate_);
        shred_.prepare(sampleRate_);
        breakBus_.prepare(sampleRate_);
        reset();
        setParams(params_);
    }

    void reset()
    {
        for (auto& voice : voices_) voice.reset();
        amplifier_.reset();
        shred_.reset();
        breakBus_.reset();
        cleanLow_ = {};
        wetLow_ = {};
        modActivity_.fill(0.0f);
        outputPeak_ = 0.0f;
        ageCounter_ = 1u;
    }

    void setParams(LowformParams params)
    {
        params_ = sanitize(params);
        BassAmplifierParams amp;
        amp.valvePreamp = params_.tube;
        amp.powerStage = params_.tube * 0.74f;
        amp.supplySag = params_.tube * 0.46f;
        amp.cabinet = params_.tube * 0.58f;
        amplifier_.setParams(amp);

        BassShredParams shred;
        shred.shred = params_.shred;
        shred.feedback = params_.shredFeedback;
        shred.feedbackToneLevel = 1.0f;
        shred.color = params_.shredColor;
        shred.mix = params_.shredMix;
        shred.circuit = static_cast<BassShredCircuit>(std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(params_.shredCircuit)),
            kBassShredCircuitCount - 1u));
        shred_.setParams(shred);

        BreakBusParams bus;
        bus.press = params_.dynamics;
        bus.snap = 0.08f + params_.dynamics * 0.16f;
        bus.recovery = 0.34f;
        bus.saturation = params_.saturation;
        bus.bite = params_.dynamicsBite;
        bus.clip = params_.clip;
        bus.tilt = params_.tilt;
        bus.linkMode = BreakBusLinkMode::All;
        breakBus_.setParams(bus);
    }

    const LowformParams& params() const noexcept { return params_; }

    void beginBlock() noexcept { breakBus_.beginBlock(); }

    void setTransport(double beat, double tempo, bool playing) noexcept
    {
        transportBeat_ = std::isfinite(beat) ? beat : 0.0;
        tempo_ = std::isfinite(tempo) ? std::clamp(tempo, 1.0, 999.0) : 120.0;
        transportPlaying_ = playing;
    }

    void noteOn(int key, float velocity, int32_t noteId = -1,
        int16_t channel = 0)
    {
        key = std::clamp(key, 0, 127);
        velocity = clamp(std::isfinite(velocity) ? velocity : 1.0f,
            0.0f, 1.0f);
        if (!(velocity > 0.0f)) {
            noteOff(key, noteId, channel);
            return;
        }
        Voice* voice = nullptr;
        const auto mode = enumValue<BassVoiceMode>(params_.voiceMode, 1u);
        if (mode == BassVoiceMode::Mono) {
            voice = &voices_[0u];
        } else {
            voice = matchingVoice(key, noteId, channel);
            if (!voice) {
                voice = &*std::min_element(voices_.begin(), voices_.end(),
                    [](const Voice& a, const Voice& b) {
                        if (a.active != b.active) return !a.active;
                        if (a.envelope != b.envelope)
                            return a.envelope < b.envelope;
                        return a.age < b.age;
                    });
            }
        }
        const bool legato = voice->active && mode == BassVoiceMode::Mono;
        voice->start(key, velocity, noteId, channel, legato,
            ++ageCounter_, params_, sampleRate_);
        if (mode == BassVoiceMode::Mono) {
            for (uint32_t i = 1u; i < voices_.size(); ++i) voices_[i].kill();
        }
    }

    void noteOff(int key, int32_t noteId = -1, int16_t channel = -1)
    {
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            const bool noteMatches = noteId >= 0
                ? voice.noteId == noteId && (key < 0 || voice.key == key)
                : (key < 0 || voice.key == key);
            const bool channelMatches = channel < 0 || voice.channel == channel;
            if (noteMatches && channelMatches) voice.release();
        }
    }

    bool retuneNote(int oldKey, int32_t noteId, int16_t channel,
        int newKey, float velocity)
    {
        newKey = std::clamp(newKey, 0, 127);
        velocity = clamp(std::isfinite(velocity) ? velocity : 1.0f,
            0.0f, 1.0f);
        for (auto& voice : voices_) {
            if (!voice.active || voice.key != oldKey) continue;
            if (noteId >= 0 && voice.noteId != noteId) continue;
            if (channel >= 0 && voice.channel != channel) continue;
            voice.key = newKey;
            voice.targetHz = Voice::midiHz(static_cast<float>(newKey));
            voice.velocity = velocity;
            voice.age = ++ageCounter_;
            return true;
        }
        return false;
    }

    void allNotesOff()
    {
        for (auto& voice : voices_) voice.release();
    }

    void allSoundOff(int16_t channel = -1)
    {
        for (auto& voice : voices_) {
            if (channel < 0 || voice.channel == channel) voice.kill();
        }
        if (channel < 0) shred_.interruptFeedback();
    }

    void setPressure(int key, int32_t noteId, int16_t channel, float value)
    {
        forMatchingVoices(key, noteId, channel, [value](Voice& voice) {
            voice.pressure = clamp(std::isfinite(value) ? value : 0.0f,
                0.0f, 1.0f);
        });
    }

    void setTimbre(int key, int32_t noteId, int16_t channel, float value)
    {
        forMatchingVoices(key, noteId, channel, [value](Voice& voice) {
            voice.timbre = clamp(std::isfinite(value) ? value : 0.0f,
                0.0f, 1.0f);
        });
    }

    void setTuning(int key, int32_t noteId, int16_t channel, float semitones)
    {
        forMatchingVoices(key, noteId, channel, [semitones](Voice& voice) {
            voice.tuning = clamp(std::isfinite(semitones) ? semitones : 0.0f,
                -48.0f, 48.0f);
        });
    }

    void setPitchBend(float semitones) noexcept
    {
        pitchBendSemitones_ = clamp(
            std::isfinite(semitones) ? semitones : 0.0f, -24.0f, 24.0f);
    }

    void setModWheel(float value) noexcept
    {
        modWheel_ = clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    }

    bool active() const noexcept
    {
        return std::any_of(voices_.begin(), voices_.end(),
            [](const Voice& voice) { return voice.active; })
            || shred_.feedbackActivity() > 1.0e-5f;
    }

    float outputPeak() const noexcept { return outputPeak_; }
    float modActivity(uint32_t index) const noexcept
    {
        return index < modActivity_.size() ? modActivity_[index] : 0.0f;
    }

    void processFrame(float& left, float& right)
    {
        left = 0.0f;
        right = 0.0f;
        float cleanLeft = 0.0f;
        float cleanRight = 0.0f;
        float averagePitch = 55.0f;
        float pitchWeight = 0.0f;
        float finishWeight = 0.0f;
        float amplifierTubeMod = 0.0f;
        float shredAmountMod = 0.0f;
        float shredFeedbackMod = 0.0f;
        float shredColorMod = 0.0f;
        float shredMixMod = 0.0f;
        float dynamicsAmountMod = 0.0f;
        float dynamicsBiteMod = 0.0f;
        float dynamicsSaturationMod = 0.0f;
        float dynamicsTiltMod = 0.0f;
        std::array<float, kBassModCount> modActivity {};
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            VoiceOutput out = voice.process(params_, sampleRate_,
                transportBeat_, transportPlaying_, pitchBendSemitones_,
                modWheel_);
            left += out.left;
            right += out.right;
            cleanLeft += out.foundation;
            cleanRight += out.foundation;
            averagePitch += out.pitchHz * out.level;
            pitchWeight += out.level;
            amplifierTubeMod += out.amplifierTubeMod * out.level;
            shredAmountMod += out.shredAmountMod * out.level;
            shredFeedbackMod += out.shredFeedbackMod * out.level;
            shredColorMod += out.shredColorMod * out.level;
            shredMixMod += out.shredMixMod * out.level;
            dynamicsAmountMod += out.dynamicsAmountMod * out.level;
            dynamicsBiteMod += out.dynamicsBiteMod * out.level;
            dynamicsSaturationMod += out.dynamicsSaturationMod * out.level;
            dynamicsTiltMod += out.dynamicsTiltMod * out.level;
            for (uint32_t i = 0u; i < modActivity.size(); ++i)
                modActivity[i] += out.modActivity[i] * out.level;
            finishWeight += out.level;
        }
        if (pitchWeight > 0.0f) averagePitch /= 1.0f + pitchWeight;
        if (finishWeight > 0.0f) {
            const float inverseWeight = 1.0f / finishWeight;
            amplifierTubeMod *= inverseWeight;
            shredAmountMod *= inverseWeight;
            shredFeedbackMod *= inverseWeight;
            shredColorMod *= inverseWeight;
            shredMixMod *= inverseWeight;
            dynamicsAmountMod *= inverseWeight;
            dynamicsBiteMod *= inverseWeight;
            dynamicsSaturationMod *= inverseWeight;
            dynamicsTiltMod *= inverseWeight;
            for (uint32_t i = 0u; i < modActivity.size(); ++i)
                modActivity[i] *= inverseWeight;
        }
        modActivity_ = modActivity;

        const float normalization = enumValue<BassVoiceMode>(
            params_.voiceMode, 1u) == BassVoiceMode::Poly ? 0.72f : 1.0f;
        left *= normalization;
        right *= normalization;
        cleanLeft *= normalization;
        cleanRight *= normalization;

        const float tube = clamp(params_.tube
            + amplifierTubeMod * 0.5f, 0.0f, 1.0f);
        BassAmplifierParams amp;
        amp.valvePreamp = tube;
        amp.powerStage = tube * 0.74f;
        amp.supplySag = tube * 0.46f;
        amp.cabinet = tube * 0.58f;
        amplifier_.setParams(amp);
        BassShredParams shred;
        shred.shred = clamp(params_.shred
            + shredAmountMod * 0.5f, 0.0f, 1.0f);
        shred.feedback = clamp(params_.shredFeedback
            + shredFeedbackMod * 0.5f, 0.0f, 1.0f);
        shred.feedbackToneLevel = 1.0f;
        shred.color = clamp(params_.shredColor
            + shredColorMod * 0.5f, 0.0f, 1.0f);
        shred.mix = clamp(params_.shredMix
            + shredMixMod * 0.5f, 0.0f, 1.0f);
        shred.circuit = static_cast<BassShredCircuit>(std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(params_.shredCircuit)),
            kBassShredCircuitCount - 1u));
        shred_.setModulatedParams(shred);
        amplifier_.processStereo(left, right);
        shred_.processStereo(left, right, averagePitch);
        breakBus_.setModulation(
            dynamicsAmountMod * 0.5f,
            dynamicsAmountMod * 0.08f,
            dynamicsBiteMod * 0.5f,
            dynamicsSaturationMod * 0.5f,
            dynamicsTiltMod * 0.5f);
        float frame[2u] { left, right };
        breakBus_.processFrame(frame, 2u);
        left = frame[0u];
        right = frame[1u];

        const float lowCoefficient = 1.0f - std::exp(
            -2.0f * kPi * 118.0f / static_cast<float>(sampleRate_));
        const auto lowPass = [lowCoefficient](float input, LowPair& state) {
            state.one += (input - state.one) * lowCoefficient;
            state.two += (state.one - state.two) * lowCoefficient;
            state.one = flushDenormal(state.one);
            state.two = flushDenormal(state.two);
            return state.two;
        };
        const float wetLowL = lowPass(left, wetLow_[0u]);
        const float wetLowR = lowPass(right, wetLow_[1u]);
        const float cleanLowL = lowPass(cleanLeft, cleanLow_[0u]);
        const float cleanLowR = lowPass(cleanRight, cleanLow_[1u]);
        const float lowReturn = params_.foundationReturn;
        left += (cleanLowL - wetLowL) * lowReturn;
        right += (cleanLowR - wetLowR) * lowReturn;

        const float maximize = params_.maximizer;
        if (maximize > 0.0f) {
            const float drive = 1.0f + maximize * maximize * 7.0f;
            const float norm = std::max(0.01f, std::tanh(drive));
            left = lerp(left, std::tanh(left * drive) / norm, maximize);
            right = lerp(right, std::tanh(right * drive) / norm, maximize);
        }
        const float gain = dbToGain(params_.outputGainDb);
        left = flushDenormal(clamp(left * gain, -2.0f, 2.0f));
        right = flushDenormal(clamp(right * gain, -2.0f, 2.0f));
        outputPeak_ = std::max(std::max(std::fabs(left), std::fabs(right)),
            outputPeak_ * 0.9995f);
    }

private:
    template <typename E>
    static E enumValue(float value, uint32_t maximum)
    {
        const auto index = std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(std::max(0.0f, value))), maximum);
        return static_cast<E>(index);
    }

    struct LowPair { float one = 0.0f; float two = 0.0f; };

    struct SvfState {
        float ic1 = 0.0f;
        float ic2 = 0.0f;
        float ic1b = 0.0f;
        float ic2b = 0.0f;

        void reset() { ic1 = ic2 = ic1b = ic2b = 0.0f; }

        static float stage(float input, float g, float k, BassFilterType type,
            float& stateOne, float& stateTwo)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v1 = a1 * (stateOne + g * (input - stateTwo));
            const float v2 = stateTwo + g * v1;
            stateOne = flushDenormal(2.0f * v1 - stateOne);
            stateTwo = flushDenormal(2.0f * v2 - stateTwo);
            if (type == BassFilterType::Band12) return v1;
            if (type == BassFilterType::High12) return input - k * v1 - v2;
            if (type == BassFilterType::Notch) return input - k * v1;
            return v2;
        }

        float process(float input, BassFilterType type, float cutoff,
            float resonance, float sampleRate)
        {
            if (type == BassFilterType::Bypass) return input;
            cutoff = clamp(cutoff, 18.0f, sampleRate * 0.43f);
            const float g = std::tan(kPi * cutoff / sampleRate);
            const float k = 2.0f - resonance * 1.92f;
            float output = stage(input, g, k, type, ic1, ic2);
            if (type == BassFilterType::Low24) {
                output = stage(output, g, k, BassFilterType::Low12,
                    ic1b, ic2b);
            }
            return flushDenormal(output);
        }
    };

    enum class EnvelopeStage : uint8_t { Off, Attack, Decay, Sustain, Release };

    struct ModRuntime {
        float phase = 0.0f;
        float held = 0.0f;
        uint32_t rng = 0x9e3779b9u;
    };

    struct VoiceOutput {
        float left = 0.0f;
        float right = 0.0f;
        float foundation = 0.0f;
        float pitchHz = 55.0f;
        float level = 0.0f;
        float amplifierTubeMod = 0.0f;
        float shredAmountMod = 0.0f;
        float shredFeedbackMod = 0.0f;
        float shredColorMod = 0.0f;
        float shredMixMod = 0.0f;
        float dynamicsAmountMod = 0.0f;
        float dynamicsBiteMod = 0.0f;
        float dynamicsSaturationMod = 0.0f;
        float dynamicsTiltMod = 0.0f;
        std::array<float, kBassModCount> modActivity {};
    };

    struct Voice {
        static constexpr uint32_t kModalModeCount = 12u;
        static constexpr uint32_t kRaveDelayCapacity = 4096u;
        static_assert((kRaveDelayCapacity & (kRaveDelayCapacity - 1u)) == 0u);

        struct ModalModeState {
            double phase = 0.0;
            float strikeAmplitude = 0.0f;
            float sustainAmplitude = 0.0f;
        };

        bool active = false;
        bool gate = false;
        int key = -1;
        int32_t noteId = -1;
        int16_t channel = 0;
        uint64_t age = 0u;
        float velocity = 0.0f;
        float pressure = 0.0f;
        float pressureSmoothed = 0.0f;
        float timbre = 0.0f;
        float timbreSmoothed = 0.0f;
        float tuning = 0.0f;
        float tuningSmoothed = 0.0f;
        float envelope = 0.0f;
        EnvelopeStage envelopeStage = EnvelopeStage::Off;
        float pitchEnvelope = 0.0f;
        float filterEnvelope = 0.0f;
        float bodyLayerEnvelope = 1.0f;
        float textureLayerEnvelope = 1.0f;
        bool textureLayerAttacking = false;
        float currentHz = 55.0f;
        float targetHz = 55.0f;
        float foundationPhase = 0.0f;
        float bodyPhase = 0.0f;
        float ringPhase = 0.0f;
        std::array<float, 7u> swarmPhases {};
        std::array<float, 7u> swarmDriftPhases {};
        std::array<ModalModeState, kModalModeCount> modalModes {};
        float modalPendingExcitation = 0.0f;
        float modalMaterial = 0.42f;
        float modalPosition = 0.48f;
        float modalDecay = 0.40f;
        float modalSustain = 0.24f;
        float modalDamping = 0.50f;
        float modalDriveSmoothed = 0.25f;
        float modalSideLow = 0.0f;
        float bodyOnsetPhase = 1.0f;
        BassBodyEngine renderedBodyEngine = BassBodyEngine::Pressure;
        float lastBodyLeft = 0.0f;
        float lastBodyRight = 0.0f;
        float bodyContinuityLeft = 0.0f;
        float bodyContinuityRight = 0.0f;
        uint32_t bodyContinuityPosition = 1u;
        uint32_t bodyContinuityFrames = 1u;
        float lastOutputLeft = 0.0f;
        float lastOutputRight = 0.0f;
        float voiceContinuitySourceLeft = 0.0f;
        float voiceContinuitySourceRight = 0.0f;
        uint32_t voiceContinuityPosition = 1u;
        uint32_t voiceContinuityFrames = 1u;
        bool voiceContinuityPending = false;
        std::array<float, 4u> acidFilterStages {};
        float acidCutoffLog2 = std::log2(820.0f);
        float acidOscillatorDc = 0.0f;
        float acidBodyDc = 0.0f;
        float acidEdgeDc = 0.0f;
        float raveSubPhase = 0.0f;
        float raveSmearPhase = 0.0f;
        float raveNoiseLow = 0.0f;
        float raveBodyDc = 0.0f;
        float raveWaveSmoothed = 0.42f;
        float raveSubSmoothed = 0.48f;
        float raveSmearSmoothed = 0.40f;
        float raveNoiseSmoothed = 0.24f;
        float raveDriveSmoothed = 0.59f;
        std::array<float, kRaveDelayCapacity> raveDelay {};
        uint32_t raveDelayWrite = 0u;
        float phaseModulatorPhase = 0.0f;
        float phaseFeedbackSample = 0.0f;
        float phaseCarrierSmoothed = 0.42f;
        float phaseRatioSmoothed = 0.48f;
        float phaseIndexSmoothed = 0.40f;
        float phaseFeedbackSmoothed = 0.24f;
        float phaseFoldSmoothed = 0.59f;
        std::array<SvfState, 6u> throatBands {};
        std::array<float, 6u> throatCoefficients {};
        uint32_t throatCoefficientCounter = 0u;
        float throatVowelSmoothed = 0.42f;
        float throatShiftSmoothed = 0.48f;
        float throatFocusSmoothed = 0.40f;
        float throatSourceSmoothed = 0.24f;
        float throatDriveSmoothed = 0.59f;
        float throatSideLow = 0.0f;
        float syncMasterPhase = 0.0f;
        float syncSlavePhase = 0.0f;
        float syncSweepEnvelope = 1.0f;
        float syncWaveSmoothed = 0.42f;
        float syncRatioSmoothed = 0.48f;
        float syncSweepSmoothed = 0.40f;
        float syncDecaySmoothed = 0.24f;
        float syncRingSmoothed = 0.59f;
        float syncOutputLeft = 0.0f;
        float syncOutputRight = 0.0f;
        float corrode = 0.0f;
        uint32_t corrodeCounter = 0u;
        uint32_t rng = 0x6d2b79f5u;
        std::array<ModRuntime, kBassModCount> modRuntime {};
        std::array<std::array<SvfState, 2u>, 3u> filters {};

        void reset()
        {
            *this = Voice {};
        }

        void kill()
        {
            active = false;
            gate = false;
            envelope = 0.0f;
            envelopeStage = EnvelopeStage::Off;
        }

        static float midiHz(float note)
        {
            return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        }

        void start(int newKey, float newVelocity, int32_t newNoteId,
            int16_t newChannel, bool legato, uint64_t newAge,
            const LowformParams& params, double sampleRate)
        {
            const bool wasActive = active;
            if (!legato && wasActive) {
                voiceContinuitySourceLeft = lastOutputLeft;
                voiceContinuitySourceRight = lastOutputRight;
                voiceContinuityPending = true;
            }
            key = newKey;
            noteId = newNoteId;
            channel = newChannel;
            velocity = newVelocity;
            age = newAge;
            gate = true;
            active = true;
            targetHz = midiHz(static_cast<float>(key));
            if (!legato) {
                envelope = 0.0f;
                currentHz = targetHz;
                foundationPhase = bodyPhase = ringPhase = 0.0f;
                swarmPhases = { 0.02f, 0.17f, 0.31f, 0.48f,
                    0.59f, 0.74f, 0.88f };
                swarmDriftPhases = { 0.03f, 0.19f, 0.37f, 0.51f,
                    0.68f, 0.81f, 0.94f };
                if (!wasActive) {
                    modalModes.fill({});
                    modalMaterial = params.bodyControlA;
                    modalPosition = params.bodyControlB;
                    modalDecay = params.bodyControlC;
                    modalSustain = params.bodyControlD;
                    modalDamping = params.bodyControlE;
                    modalDriveSmoothed = params.modalDrive;
                    modalSideLow = 0.0f;
                }
                const float attackMilliseconds = params.attackSeconds * 1000.0f;
                const float strikeForAttack = std::exp(-std::max(0.0f,
                    attackMilliseconds - 1.0f) / 6.0f);
                const float velocityStrike = newVelocity
                    * lerp(0.20f, newVelocity, 0.80f);
                const auto selectedBodyEngine = static_cast<BassBodyEngine>(
                    std::min<uint32_t>(7u, static_cast<uint32_t>(std::lround(
                        std::max(0.0f, params.bodyEngine)))));
                if (selectedBodyEngine == BassBodyEngine::Modal) {
                    modalPendingExcitation = clamp(modalPendingExcitation
                        + velocityStrike * strikeForAttack, 0.0f, 1.8f);
                } else {
                    modalPendingExcitation = 0.0f;
                }
                // The ladder still receives a C1 onset. Modal excitation is
                // itself distributed over a short force packet below.
                bodyOnsetPhase = 0.0f;
                renderedBodyEngine = selectedBodyEngine;
                lastBodyLeft = 0.0f;
                lastBodyRight = 0.0f;
                bodyContinuityPosition = bodyContinuityFrames;
                acidFilterStages.fill(0.0f);
                acidCutoffLog2 = std::log2(clamp(params.cutoffHz,
                    25.0f, static_cast<float>(sampleRate * 0.41)));
                acidOscillatorDc = 0.0f;
                acidBodyDc = 0.0f;
                acidEdgeDc = 0.0f;
                raveSubPhase = 0.0f;
                raveSmearPhase = std::fmod(
                    0.071f * static_cast<float>(newKey + 3), 1.0f);
                raveNoiseLow = 0.0f;
                raveBodyDc = 0.0f;
                raveWaveSmoothed = params.bodyControlA;
                raveSubSmoothed = params.bodyControlB;
                raveSmearSmoothed = params.bodyControlC;
                raveNoiseSmoothed = params.bodyControlD;
                raveDriveSmoothed = params.bodyControlE;
                raveDelay.fill(0.0f);
                raveDelayWrite = 0u;
                phaseModulatorPhase = 0.0f;
                phaseFeedbackSample = 0.0f;
                phaseCarrierSmoothed = params.bodyControlA;
                phaseRatioSmoothed = params.bodyControlB;
                phaseIndexSmoothed = params.bodyControlC;
                phaseFeedbackSmoothed = params.bodyControlD;
                phaseFoldSmoothed = params.bodyControlE;
                for (auto& band : throatBands) band.reset();
                throatCoefficients.fill(0.0f);
                throatCoefficientCounter = 0u;
                throatVowelSmoothed = params.bodyControlA;
                throatShiftSmoothed = params.bodyControlB;
                throatFocusSmoothed = params.bodyControlC;
                throatSourceSmoothed = params.bodyControlD;
                throatDriveSmoothed = params.bodyControlE;
                throatSideLow = 0.0f;
                syncMasterPhase = 0.0f;
                syncSlavePhase = 0.0f;
                syncSweepEnvelope = 1.0f;
                syncWaveSmoothed = params.bodyControlA;
                syncRatioSmoothed = params.bodyControlB;
                syncSweepSmoothed = params.bodyControlC;
                syncDecaySmoothed = params.bodyControlD;
                syncRingSmoothed = params.bodyControlE;
                syncOutputLeft = 0.0f;
                syncOutputRight = 0.0f;
                for (auto& layer : filters)
                    for (auto& filter : layer) filter.reset();
                for (uint32_t i = 0u; i < modRuntime.size(); ++i) {
                    modRuntime[i].phase = std::fmod(
                        0.173f * (static_cast<float>(newKey + 1)
                            + static_cast<float>(i * 13u)), 1.0f);
                    modRuntime[i].rng = rng ^ (0x9e3779b9u * (i + 1u));
                    modRuntime[i].held = static_cast<float>(
                        modRuntime[i].rng & 0xffffu) / 32767.5f - 1.0f;
                }
                pressure = 0.0f;
                pressureSmoothed = 0.0f;
                timbre = 0.0f;
                timbreSmoothed = 0.0f;
                tuning = 0.0f;
                tuningSmoothed = 0.0f;
                bodyLayerEnvelope = 1.0f;
                textureLayerEnvelope = params.textureAttackSeconds > 0.0f
                    ? 0.0f : 1.0f;
                textureLayerAttacking = params.textureAttackSeconds > 0.0f;
            }
            envelopeStage = EnvelopeStage::Attack;
            pitchEnvelope = 1.0f;
            filterEnvelope = 1.0f;
        }

        void release()
        {
            gate = false;
            if (active) envelopeStage = EnvelopeStage::Release;
        }

        static float polyBlep(float phase, float increment)
        {
            if (phase < increment) {
                const float x = phase / increment;
                return x + x - x * x - 1.0f;
            }
            if (phase > 1.0f - increment) {
                const float x = (phase - 1.0f) / increment;
                return x * x + x + x + 1.0f;
            }
            return 0.0f;
        }

        static float nextPhase(float& phase, float increment)
        {
            phase += increment;
            phase -= std::floor(phase);
            return phase;
        }

        float randomBipolar()
        {
            rng ^= rng << 13u;
            rng ^= rng >> 17u;
            rng ^= rng << 5u;
            return static_cast<float>(rng & 0x00ffffffu)
                    * (2.0f / 16777215.0f) - 1.0f;
        }

        float readRaveDelay(float delaySamples) const
        {
            delaySamples = clamp(delaySamples, 1.0f,
                static_cast<float>(kRaveDelayCapacity - 2u));
            float position = static_cast<float>(raveDelayWrite)
                - delaySamples;
            if (position < 0.0f)
                position += static_cast<float>(kRaveDelayCapacity);
            const uint32_t first = static_cast<uint32_t>(position)
                & (kRaveDelayCapacity - 1u);
            const uint32_t second = (first + 1u)
                & (kRaveDelayCapacity - 1u);
            return lerp(raveDelay[first], raveDelay[second],
                position - std::floor(position));
        }

        static float roundedWave(float phase)
        {
            const float sine = std::sin(2.0f * kPi * phase);
            return std::tanh(sine * 1.65f) / std::tanh(1.65f);
        }

        static float waveFoundation(BassFoundationWave wave, float phase)
        {
            if (wave == BassFoundationWave::Triangle)
                return 1.0f - 4.0f * std::fabs(phase - 0.5f);
            if (wave == BassFoundationWave::Rounded)
                return roundedWave(phase);
            return std::sin(2.0f * kPi * phase);
        }

        static std::array<uint32_t, 3u> modalIdentity(uint32_t mode)
        {
            constexpr std::array<std::array<uint32_t, 3u>, kModalModeCount>
                identities {{
                {{ 0u, 1u, 0u }}, {{ 1u, 1u, 0u }}, {{ 1u, 1u, 1u }},
                {{ 0u, 2u, 0u }}, {{ 2u, 1u, 0u }}, {{ 2u, 1u, 1u }},
                {{ 1u, 2u, 0u }}, {{ 1u, 2u, 1u }},
                {{ 3u, 1u, 0u }}, {{ 3u, 1u, 1u }},
                {{ 0u, 3u, 0u }}, {{ 4u, 1u, 0u }},
            }};
            return identities[std::min<uint32_t>(
                mode, kModalModeCount - 1u)];
        }

        static float modalShape(uint32_t mode, float radius, float theta)
        {
            const auto identity = modalIdentity(mode);
            const uint32_t angular = identity[0u];
            const uint32_t radial = identity[1u];
            const bool sineVariant = identity[2u] != 0u;
            radius = clamp(radius, 0.0f, 0.98f);
            const float radialShape = radial == 1u
                ? std::cos(radius * kPi * 0.5f)
                : std::cos(radius
                    * (static_cast<float>(radial) - 0.5f) * kPi);
            if (angular == 0u) return radialShape;
            const float angularShape = sineVariant
                ? std::sin(static_cast<float>(angular) * theta)
                : std::cos(static_cast<float>(angular) * theta);
            return radialShape * angularShape
                * std::pow(std::max(0.03f, radius),
                    std::min(2.0f, static_cast<float>(angular) * 0.52f));
        }

        static float modalExcitationShape(uint32_t mode, float position)
        {
            const float radius = lerp(0.04f, 0.94f,
                clamp(position, 0.0f, 1.0f));
            constexpr float strikeTheta = -0.41822433f;
            return modalShape(mode, radius, strikeTheta);
        }

        static float modalRatio(uint32_t index, float material)
        {
            constexpr std::array<float, kModalModeCount> circular {{
                1.000f, 1.593f, 1.593f, 2.135f, 2.295f, 2.295f,
                2.653f, 2.653f, 2.918f, 2.918f, 3.500f, 3.600f,
            }};
            constexpr std::array<float, kModalModeCount> loaded {{
                1.000f, 2.000f, 2.000f, 3.000f, 3.000f, 3.000f,
                4.000f, 4.000f, 5.000f, 5.000f, 6.000f, 7.000f,
            }};
            return lerp(circular[std::min<uint32_t>(
                index, kModalModeCount - 1u)], loaded[std::min<uint32_t>(
                index, kModalModeCount - 1u)], material);
        }

        float modValue(uint32_t index, const BassModParams& params,
            float sampleRate, double beat, bool transport)
        {
            auto& runtime = modRuntime[index];
            const auto shape = enumValue<BassModShape>(params.shape,
                kBassModShapeCount - 1u);
            if (shape == BassModShape::Off) return 0.0f;
            float phase = runtime.phase;
            if (transport) {
                const uint32_t division = std::min<uint32_t>(
                    static_cast<uint32_t>(std::lround(
                        clamp(params.rate, 0.0f, 1.0f) * 15.0f)), 15u);
                const double cycles = beat / kBassMotionDivisionBeats[division];
                phase = static_cast<float>(cycles - std::floor(cycles));
                if (phase + 0.5f < runtime.phase) {
                    runtime.rng ^= runtime.rng << 13u;
                    runtime.rng ^= runtime.rng >> 17u;
                    runtime.rng ^= runtime.rng << 5u;
                    runtime.held = static_cast<float>(
                        runtime.rng & 0xffffu) / 32767.5f - 1.0f;
                }
                runtime.phase = phase;
            } else {
                const float rateHz = 0.03f * std::pow(800.0f,
                    clamp(params.rate, 0.0f, 1.0f));
                const float old = runtime.phase;
                runtime.phase += rateHz / sampleRate;
                if (runtime.phase >= 1.0f) {
                    runtime.phase -= 1.0f;
                    runtime.rng ^= runtime.rng << 13u;
                    runtime.rng ^= runtime.rng >> 17u;
                    runtime.rng ^= runtime.rng << 5u;
                    runtime.held = static_cast<float>(
                        runtime.rng & 0xffffu) / 32767.5f - 1.0f;
                }
                phase = old;
            }
            switch (shape) {
            case BassModShape::Sine:
                return std::sin(2.0f * kPi * phase);
            case BassModShape::Triangle:
                return 1.0f - 4.0f * std::fabs(phase - 0.5f);
            case BassModShape::Ramp:
                return phase * 2.0f - 1.0f;
            case BassModShape::Square:
                return phase < 0.5f ? 1.0f : -1.0f;
            case BassModShape::Random:
                return runtime.held;
            case BassModShape::Steps: {
                const float step = std::floor(phase * 8.0f) / 7.0f;
                return step * 2.0f - 1.0f;
            }
            case BassModShape::Off: break;
            }
            return 0.0f;
        }

        void updateEnvelope(const LowformParams& params,
            float sampleRate)
        {
            const auto coefficient = [sampleRate](float seconds) {
                return 1.0f - std::exp(-1.0f
                    / (std::max(0.00005f, seconds) * sampleRate));
            };
            switch (envelopeStage) {
            case EnvelopeStage::Attack:
                envelope += (1.0f - envelope)
                    * coefficient(params.attackSeconds * 0.25f);
                if (envelope >= 0.999f) {
                    envelope = 1.0f;
                    envelopeStage = EnvelopeStage::Decay;
                }
                break;
            case EnvelopeStage::Decay:
                envelope += (params.sustain - envelope)
                    * coefficient(params.decaySeconds * 0.28f);
                if (std::fabs(envelope - params.sustain) < 0.001f) {
                    envelope = params.sustain;
                    envelopeStage = EnvelopeStage::Sustain;
                }
                break;
            case EnvelopeStage::Sustain:
                envelope = params.sustain;
                if (!gate) envelopeStage = EnvelopeStage::Release;
                break;
            case EnvelopeStage::Release:
                envelope += (0.0f - envelope)
                    * coefficient(params.releaseSeconds * 0.22f);
                if (envelope < 1.0e-5f) kill();
                break;
            case EnvelopeStage::Off: kill(); break;
            }
        }

        VoiceOutput process(const LowformParams& params,
            double sampleRateDouble, double beat, bool transportPlaying,
            float pitchBend, float modWheel)
        {
            VoiceOutput out;
            const float sampleRate = static_cast<float>(sampleRateDouble);
            updateEnvelope(params, sampleRate);
            if (!active) return out;

            if (params.bodyDecaySeconds < 11.999f) {
                bodyLayerEnvelope *= std::exp(-1.0f
                    / std::max(1.0f,
                        params.bodyDecaySeconds * sampleRate));
            }
            if (textureLayerAttacking) {
                const float coefficient = 1.0f - std::exp(-1.0f
                    / std::max(1.0f,
                        params.textureAttackSeconds * sampleRate * 0.145f));
                textureLayerEnvelope +=
                    (1.0f - textureLayerEnvelope) * coefficient;
                if (textureLayerEnvelope >= 0.999f) {
                    textureLayerEnvelope = 1.0f;
                    textureLayerAttacking = false;
                }
            } else if (params.textureDecaySeconds < 11.999f) {
                textureLayerEnvelope *= std::exp(-1.0f
                    / std::max(1.0f,
                        params.textureDecaySeconds * sampleRate));
            }

            const float tuningSlew = 1.0f - std::exp(
                -1.0f / (0.0025f * sampleRate));
            const float pressureSlew = 1.0f - std::exp(
                -1.0f / (0.006f * sampleRate));
            const float timbreSlew = 1.0f - std::exp(
                -1.0f / (0.010f * sampleRate));
            tuningSmoothed += (tuning - tuningSmoothed) * tuningSlew;
            pressureSmoothed +=
                (pressure - pressureSmoothed) * pressureSlew;
            timbreSmoothed += (timbre - timbreSmoothed) * timbreSlew;

            const bool synchronized = enumValue<BassMotionClock>(
                params.motionClock, 1u) == BassMotionClock::Transport;
            float ampMod = 0.0f;
            float pitchMod = 0.0f;
            float morphMod = 0.0f;
            float densityMod = 0.0f;
            float textureLevelMod = 0.0f;
            float textureColorMod = 0.0f;
            float cutoffMod = 0.0f;
            float resonanceMod = 0.0f;
            float widthMod = 0.0f;
            float foundationLevelMod = 0.0f;
            float bodyLevelMod = 0.0f;
            float bodyControlCMod = 0.0f;
            float bodyControlDMod = 0.0f;
            float bodyControlEMod = 0.0f;
            float bodyWidthMod = 0.0f;
            float textureTrackMod = 0.0f;
            float textureWidthMod = 0.0f;
            float filterDriveMod = 0.0f;
            float keyTrackMod = 0.0f;
            float amplifierTubeMod = 0.0f;
            float shredAmountMod = 0.0f;
            float shredFeedbackMod = 0.0f;
            float shredColorMod = 0.0f;
            float shredMixMod = 0.0f;
            float dynamicsAmountMod = 0.0f;
            float dynamicsBiteMod = 0.0f;
            float dynamicsSaturationMod = 0.0f;
            float dynamicsTiltMod = 0.0f;
            const auto route = [&](BassModTarget target, float amount) {
                switch (target) {
                case BassModTarget::Amp: ampMod += amount; break;
                case BassModTarget::Pitch: pitchMod += amount; break;
                case BassModTarget::BodyMorph: morphMod += amount; break;
                case BassModTarget::BodyDensity: densityMod += amount; break;
                case BassModTarget::TextureLevel:
                    textureLevelMod += amount; break;
                case BassModTarget::TextureColor:
                    textureColorMod += amount; break;
                case BassModTarget::Cutoff: cutoffMod += amount; break;
                case BassModTarget::Resonance: resonanceMod += amount; break;
                case BassModTarget::Width: widthMod += amount; break;
                case BassModTarget::FoundationLevel:
                    foundationLevelMod += amount; break;
                case BassModTarget::BodyLevel: bodyLevelMod += amount; break;
                case BassModTarget::BodyControlC:
                    bodyControlCMod += amount; break;
                case BassModTarget::BodyControlD:
                    bodyControlDMod += amount; break;
                case BassModTarget::BodyControlE:
                    bodyControlEMod += amount; break;
                case BassModTarget::BodyWidth:
                    bodyWidthMod += amount; break;
                case BassModTarget::TextureTrack:
                    textureTrackMod += amount; break;
                case BassModTarget::TextureWidth:
                    textureWidthMod += amount; break;
                case BassModTarget::FilterDrive:
                    filterDriveMod += amount; break;
                case BassModTarget::KeyTrack: keyTrackMod += amount; break;
                case BassModTarget::AmplifierTube:
                    amplifierTubeMod += amount; break;
                case BassModTarget::ShredAmount:
                    shredAmountMod += amount; break;
                case BassModTarget::ShredFeedback:
                    shredFeedbackMod += amount; break;
                case BassModTarget::ShredColor:
                    shredColorMod += amount; break;
                case BassModTarget::ShredMix:
                    shredMixMod += amount; break;
                case BassModTarget::DynamicsAmount:
                    dynamicsAmountMod += amount; break;
                case BassModTarget::DynamicsBite:
                    dynamicsBiteMod += amount; break;
                case BassModTarget::DynamicsSaturation:
                    dynamicsSaturationMod += amount; break;
                case BassModTarget::DynamicsTilt:
                    dynamicsTiltMod += amount; break;
                case BassModTarget::Off: break;
                }
            };
            const float motionScale = clamp(1.0f
                + modWheel * params.modWheelAmount, 0.0f, 2.0f);
            for (uint32_t i = 0u; i < params.mods.size(); ++i) {
                const float source = modValue(i, params.mods[i], sampleRate,
                    beat, synchronized);
                out.modActivity[i] = source;
                route(enumValue<BassModTarget>(params.mods[i].target,
                    kBassModTargetCount - 1u),
                    source * params.mods[i].depth * motionScale);
                route(enumValue<BassModTarget>(
                    params.mods[i].secondaryTarget,
                    kBassModTargetCount - 1u),
                    source * params.mods[i].secondaryDepth * motionScale);
            }
            const std::array<float, kBassExpressionSourceCount>
                expressionSources {{
                velocity, pressureSmoothed, timbreSmoothed
            }};
            for (uint32_t i = 0u; i < expressionSources.size(); ++i) {
                route(enumValue<BassModTarget>(
                    params.expressionRoutes[i].target,
                    kBassModTargetCount - 1u),
                    expressionSources[i] * params.expressionRoutes[i].depth);
            }
            out.amplifierTubeMod = amplifierTubeMod;
            out.shredAmountMod = shredAmountMod;
            out.shredFeedbackMod = shredFeedbackMod;
            out.shredColorMod = shredColorMod;
            out.shredMixMod = shredMixMod;
            out.dynamicsAmountMod = dynamicsAmountMod;
            out.dynamicsBiteMod = dynamicsBiteMod;
            out.dynamicsSaturationMod = dynamicsSaturationMod;
            out.dynamicsTiltMod = dynamicsTiltMod;

            const float glideSeconds = params.glideMs * 0.001f;
            const float glideCoefficient = glideSeconds > 0.00001f
                ? 1.0f - std::exp(-1.0f / (glideSeconds * sampleRate))
                : 1.0f;
            currentHz += (targetHz - currentHz) * glideCoefficient;
            const float pitchDecay = 1.0f - std::exp(-1.0f
                / (std::max(0.001f, params.punchTimeMs * 0.001f) * sampleRate));
            pitchEnvelope += (0.0f - pitchEnvelope) * pitchDecay;
            const float filterDecay = 1.0f - std::exp(-1.0f
                / (std::max(0.001f, params.filterDecaySeconds) * sampleRate));
            filterEnvelope += (0.0f - filterEnvelope) * filterDecay;
            const float semitones = tuningSmoothed + pitchBend
                + params.pitchPunchSemitones * pitchEnvelope
                + pitchMod * 12.0f;
            const float pitchHz = clamp(currentHz * std::pow(2.0f,
                semitones / 12.0f), 8.0f, sampleRate * 0.22f);
            out.pitchHz = pitchHz;

            const float foundationGain = clamp(params.foundationLevel
                + foundationLevelMod * 0.5f, 0.0f, 1.30f);
            const float bodyGain = clamp(params.bodyLevel
                + bodyLevelMod * 0.5f, 0.0f, 1.16f);
            const float bodyControlC = clamp(params.bodyControlC
                + bodyControlCMod * 0.5f, 0.0f, 1.0f);
            const float bodyControlD = clamp(params.bodyControlD
                + bodyControlDMod * 0.5f, 0.0f, 1.0f);
            const float bodyControlE = clamp(params.bodyControlE
                + bodyControlEMod * 0.5f, 0.0f, 1.0f);
            const float bodyWidth = clamp(params.bodyWidth
                + (widthMod + bodyWidthMod) * 0.5f, 0.0f, 1.0f);
            const float textureTrack = clamp(params.textureTrack
                + textureTrackMod * 0.5f, 0.0f, 1.0f);
            const float textureWidth = clamp(params.textureWidth
                + (widthMod + textureWidthMod) * 0.5f, 0.0f, 1.0f);
            const float filterDrive = clamp(params.filterDrive
                + filterDriveMod * 0.5f, 0.0f, 1.0f);
            const float keyTrack = clamp(params.keyTrack
                + keyTrackMod * 0.5f, 0.0f, 1.0f);
            const float fundamentalHz = pitchHz * (params.foundationOctave < 0.0f
                ? 0.5f : 1.0f);
            const float foundationIncrement = std::min(0.45f,
                fundamentalHz / sampleRate);
            nextPhase(foundationPhase, foundationIncrement);
            const float foundation = waveFoundation(
                enumValue<BassFoundationWave>(params.foundationWave, 2u),
                foundationPhase) * foundationGain;

            float bodyLeft = 0.0f;
            float bodyRight = 0.0f;
            const auto bodyEngine = enumValue<BassBodyEngine>(
                params.bodyEngine, 7u);
            if (bodyEngine == BassBodyEngine::Modal
                && renderedBodyEngine != BassBodyEngine::Modal) {
                for (auto& mode : modalModes) {
                    mode.strikeAmplitude = 0.0f;
                    mode.sustainAmplitude = 0.0f;
                }
                modalSideLow = 0.0f;
                const float switchExcitation = velocity
                    * lerp(0.20f, velocity, 0.80f) * 0.55f;
                modalPendingExcitation = clamp(modalPendingExcitation
                    + switchExcitation, 0.0f, 1.2f);
            }
            if (bodyEngine == BassBodyEngine::Rave
                && renderedBodyEngine != BassBodyEngine::Rave) {
                raveDelay.fill(0.0f);
                raveDelayWrite = 0u;
                raveSubPhase = bodyPhase * 0.5f;
                raveNoiseLow = 0.0f;
                raveBodyDc = 0.0f;
                raveWaveSmoothed = params.bodyControlA;
                raveSubSmoothed = params.bodyControlB;
                raveSmearSmoothed = params.bodyControlC;
                raveNoiseSmoothed = params.bodyControlD;
                raveDriveSmoothed = params.bodyControlE;
            }
            if (bodyEngine == BassBodyEngine::Phase
                && renderedBodyEngine != BassBodyEngine::Phase) {
                phaseModulatorPhase = bodyPhase;
                phaseFeedbackSample = 0.0f;
                phaseCarrierSmoothed = params.bodyControlA;
                phaseRatioSmoothed = params.bodyControlB;
                phaseIndexSmoothed = params.bodyControlC;
                phaseFeedbackSmoothed = params.bodyControlD;
                phaseFoldSmoothed = params.bodyControlE;
            }
            if (bodyEngine == BassBodyEngine::Throat
                && renderedBodyEngine != BassBodyEngine::Throat) {
                for (auto& band : throatBands) band.reset();
                throatCoefficients.fill(0.0f);
                throatCoefficientCounter = 0u;
                throatVowelSmoothed = params.bodyControlA;
                throatShiftSmoothed = params.bodyControlB;
                throatFocusSmoothed = params.bodyControlC;
                throatSourceSmoothed = params.bodyControlD;
                throatDriveSmoothed = params.bodyControlE;
                throatSideLow = 0.0f;
            }
            if (bodyEngine == BassBodyEngine::Sync
                && renderedBodyEngine != BassBodyEngine::Sync) {
                syncMasterPhase = bodyPhase;
                syncSlavePhase = 0.0f;
                syncSweepEnvelope = 1.0f;
                syncWaveSmoothed = params.bodyControlA;
                syncRatioSmoothed = params.bodyControlB;
                syncSweepSmoothed = params.bodyControlC;
                syncDecaySmoothed = params.bodyControlD;
                syncRingSmoothed = params.bodyControlE;
                syncOutputLeft = 0.0f;
                syncOutputRight = 0.0f;
            }
            float bodyOnsetGain = 1.0f;
            if ((bodyEngine == BassBodyEngine::Acid
                    || bodyEngine == BassBodyEngine::Rave
                    || bodyEngine == BassBodyEngine::Phase
                    || bodyEngine == BassBodyEngine::Throat
                    || bodyEngine == BassBodyEngine::Sync)
                && bodyOnsetPhase < 1.0f) {
                bodyOnsetPhase = std::min(1.0f, bodyOnsetPhase
                    + 1.0f / std::max(1.0f, sampleRate * 0.003f));
                bodyOnsetGain = 0.5f - 0.5f
                    * std::cos(kPi * bodyOnsetPhase);
            }
            if (bodyEngine == BassBodyEngine::Pressure) {
                const float shape = clamp(params.bodyControlA
                    + morphMod * 0.5f,
                    0.0f, 1.0f);
                const float pressureAmount = clamp(params.bodyControlB
                    + densityMod * 0.5f,
                    0.0f, 1.0f);
                const float pulseWidth = lerp(0.12f, 0.88f,
                    bodyControlC);
                const float crossMix = bodyControlD * 0.48f;
                const float engineDrive = 1.0f + bodyControlE * 5.2f;
                const float increment = std::min(0.45f, pitchHz / sampleRate);
                nextPhase(bodyPhase, increment);
                float saw = bodyPhase * 2.0f - 1.0f
                    - polyBlep(bodyPhase, increment);
                float pulsePhase = bodyPhase + pulseWidth;
                pulsePhase -= std::floor(pulsePhase);
                float pulse = bodyPhase < pulseWidth ? 1.0f : -1.0f;
                pulse += polyBlep(bodyPhase, increment)
                    - polyBlep(pulsePhase, increment);
                const float triangle = 1.0f
                    - 4.0f * std::fabs(bodyPhase - 0.5f);
                const float raw = lerp(triangle, saw, shape)
                    + pulse * pressureAmount * 0.42f;
                const float pressured = std::tanh(raw * engineDrive)
                    / std::max(0.01f, std::tanh(engineDrive));
                const float cross = saw * pulse;
                const float mono = lerp(pressured, cross, crossMix)
                    * bodyGain;
                bodyLeft = mono * (1.0f - bodyWidth * 0.10f);
                bodyRight = mono * (1.0f + bodyWidth * 0.10f);
            } else if (bodyEngine == BassBodyEngine::Swarm) {
                const float drive = clamp(params.bodyControlA
                    + morphMod * 0.5f,
                    0.0f, 1.0f);
                const float detune = clamp(params.bodyControlB
                    + densityMod * 0.5f,
                    0.0f, 1.0f);
                const uint32_t voiceCount = 1u + std::min<uint32_t>(6u,
                    static_cast<uint32_t>(std::lround(
                        bodyControlC * 6.0f)));
                const uint32_t firstVoice = (7u - voiceCount) / 2u;
                const uint32_t lastVoice = firstVoice + voiceCount;
                const float drift = bodyControlD;
                const float tone = lerp(0.22f, 1.0f, bodyControlE);
                constexpr std::array<float, 7u> cents {{
                    -19.0f, -11.0f, -5.0f, 0.0f, 6.0f, 12.0f, 20.0f }};
                for (uint32_t i = firstVoice; i < lastVoice; ++i) {
                    const float driftRate = 0.035f
                        + static_cast<float>(i) * 0.011f;
                    nextPhase(swarmDriftPhases[i], driftRate / sampleRate);
                    const float driftCents = std::sin(2.0f * kPi
                        * swarmDriftPhases[i]) * drift * 4.5f;
                    const float spread = lerp(0.05f, 2.0f, detune);
                    const float frequency = pitchHz * std::pow(2.0f,
                        (cents[i] * spread + driftCents) / 1200.0f);
                    const float increment = std::min(0.45f, frequency / sampleRate);
                    nextPhase(swarmPhases[i], increment);
                    const float saw = swarmPhases[i] * 2.0f - 1.0f
                        - polyBlep(swarmPhases[i], increment);
                    const float sine = std::sin(2.0f * kPi * swarmPhases[i]);
                    const float bright = lerp(sine, saw, tone);
                    const float shaped = lerp(bright,
                        std::tanh(bright * (1.5f + drive * 3.5f)),
                        drive * 0.72f);
                    const float pan = (static_cast<float>(i) - 3.0f) / 3.0f
                        * bodyWidth;
                    bodyLeft += shaped * (0.5f - pan * 0.28f);
                    bodyRight += shaped * (0.5f + pan * 0.28f);
                }
                const float normalization = std::sqrt(
                    static_cast<float>(voiceCount)) * 1.35f;
                bodyLeft *= bodyGain / normalization;
                bodyRight *= bodyGain / normalization;
            } else if (bodyEngine == BassBodyEngine::Modal) {
                const float materialTarget = clamp(params.bodyControlA
                    + morphMod * 0.5f,
                    0.0f, 1.0f);
                const float positionTarget = clamp(params.bodyControlB
                    + densityMod * 0.36f,
                    0.0f, 1.0f);
                const float decayTarget = bodyControlC;
                const float sustainTarget = bodyControlD;
                const float dampingTarget = bodyControlE;
                const auto smooth = [sampleRate](float& value, float target,
                    float seconds) {
                    value += (target - value) * (1.0f - std::exp(-1.0f
                        / std::max(1.0f, sampleRate * seconds)));
                };
                smooth(modalMaterial, materialTarget, 0.018f);
                smooth(modalPosition, positionTarget, 0.022f);
                smooth(modalDecay, decayTarget, 0.020f);
                smooth(modalSustain, sustainTarget, 0.014f);
                smooth(modalDamping, dampingTarget, 0.020f);
                smooth(modalDriveSmoothed, params.modalDrive, 0.020f);

                // A note adds energy through a short force packet instead of
                // preloading every oscillator at full amplitude. This keeps
                // the physical onset meaningful and bounded even under fast
                // retriggers or a stolen polyphonic voice.
                const float excitationCoefficient = 1.0f - std::exp(-1.0f
                    / std::max(1.0f, sampleRate * lerp(
                        0.0015f, 0.0040f, modalPosition)));
                const float excitation = modalPendingExcitation
                    * excitationCoefficient;
                modalPendingExcitation = std::max(0.0f,
                    modalPendingExcitation - excitation);
                if (modalPendingExcitation < 1.0e-7f)
                    modalPendingExcitation = 0.0f;

                const float baseDecaySeconds = 0.080f * std::pow(
                    43.75f, modalDecay);
                const float driveEnergy = clamp(velocity, 0.0f, 1.35f);
                float rawLeft = 0.0f;
                float rawRight = 0.0f;
                float energyWeight = 0.0f;
                for (uint32_t i = 0u; i < modalModes.size(); ++i) {
                    auto& mode = modalModes[i];
                    const float ratio = modalRatio(i, modalMaterial);
                    const float modeFrequency = clamp(pitchHz * ratio,
                        8.0f, sampleRate * 0.43f);
                    mode.phase += 2.0 * static_cast<double>(kPi)
                        * static_cast<double>(modeFrequency)
                        / static_cast<double>(sampleRate);
                    const double cycle = 2.0 * static_cast<double>(kPi);
                    if (mode.phase >= cycle)
                        mode.phase -= cycle * std::floor(mode.phase / cycle);

                    const float upper = std::max(0.0f, ratio - 1.0f);
                    const float shape = i == 0u ? 1.0f
                        : modalExcitationShape(i, modalPosition);
                    const float spectralFalloff = 1.0f / std::pow(
                        std::max(1.0f, ratio),
                        0.92f + modalDamping * 1.18f);
                    const float upperLevel = i == 0u ? 1.0f : 0.82f;
                    const float modeWeight = spectralFalloff * upperLevel
                        * (i == 0u ? 1.16f : 0.36f + std::fabs(shape) * 0.76f);
                    const float signedWeight = modeWeight
                        * (i == 0u ? 1.0f : shape);
                    energyWeight += modeWeight * modeWeight;

                    mode.strikeAmplitude = clamp(mode.strikeAmplitude
                        + excitation * signedWeight * 0.92f, -1.8f, 1.8f);
                    const float modeDecay = baseDecaySeconds
                        / (1.0f + upper * (0.10f
                            + modalDamping * 1.28f));
                    mode.strikeAmplitude *= std::exp(-1.0f
                        / std::max(1.0f, modeDecay * sampleRate));
                    if (std::fabs(mode.strikeAmplitude) < 1.0e-9f)
                        mode.strikeAmplitude = 0.0f;

                    const float drivenTarget = gate
                        ? driveEnergy * modalSustain * signedWeight
                            * (i == 0u ? 0.68f : 0.46f)
                        : 0.0f;
                    const float sustainSeconds = std::fabs(drivenTarget)
                            > std::fabs(mode.sustainAmplitude)
                        ? 0.020f : 0.090f;
                    smooth(mode.sustainAmplitude, drivenTarget,
                        sustainSeconds);
                    const float modalSample = std::sin(mode.phase) * clamp(
                        mode.strikeAmplitude + mode.sustainAmplitude,
                        -2.0f, 2.0f);
                    const float pan = i == 0u ? 0.0f
                        : ((i & 1u) ? -1.0f : 1.0f)
                            * (0.08f + static_cast<float>(i) * 0.012f);
                    rawLeft += modalSample * (0.5f - pan);
                    rawRight += modalSample * (0.5f + pan);
                }
                const float modalNormalization = 0.82f / std::sqrt(
                    std::max(0.30f, energyWeight));
                const float pickupDrive = 1.18f
                    + modalDriveSmoothed * 1.7f;
                const float pickupNorm = std::max(0.25f,
                    std::tanh(pickupDrive));
                rawLeft = std::tanh(rawLeft * modalNormalization
                    * pickupDrive) / pickupNorm;
                rawRight = std::tanh(rawRight * modalNormalization
                    * pickupDrive) / pickupNorm;
                const float mid = (rawLeft + rawRight) * 0.5f;
                float side = (rawLeft - rawRight) * 0.5f;
                const float sideCoefficient = 1.0f - std::exp(
                    -2.0f * kPi * 96.0f / sampleRate);
                modalSideLow += (side - modalSideLow) * sideCoefficient;
                modalSideLow = flushDenormal(modalSideLow);
                side = (side - modalSideLow) * bodyWidth;
                bodyLeft = (mid + side) * bodyGain * 0.92f;
                bodyRight = (mid - side) * bodyGain * 0.92f;
            } else if (bodyEngine == BassBodyEngine::Acid) {
                // Ambi Acid's compact voice, adapted as a playable Body:
                // anti-aliased saw/pulse into a twice-updated nonlinear
                // four-pole ladder. Wave scans saw to pulse; Resonance and
                // Drive expose the ladder directly. The resonant edge becomes a
                // bounded stereo component while its low body remains mono.
                const float wave = clamp(params.bodyControlA
                    + morphMod * 0.5f,
                    0.0f, 1.0f);
                const float acidResonanceControl = clamp(params.bodyControlB
                    + densityMod * 0.5f,
                    0.0f, 1.0f);
                const float pulseWidth = lerp(0.10f, 0.90f,
                    bodyControlC);
                const float acidDrive = bodyControlD;
                const float cutoffOffset = lerp(-4.0f, 4.0f,
                    bodyControlE);
                const float increment = std::min(0.42f, pitchHz / sampleRate);
                const float phase = bodyPhase;
                const float saw = phase * 2.0f - 1.0f
                    - polyBlep(phase, increment);
                float pulse = phase < pulseWidth ? 1.0f : -1.0f;
                pulse += polyBlep(phase, increment);
                float pulsePhase = phase - pulseWidth;
                if (pulsePhase < 0.0f) pulsePhase += 1.0f;
                pulse -= polyBlep(pulsePhase, increment);
                nextPhase(bodyPhase, increment);
                const float raw = lerp(saw, pulse, wave);
                const float oscillatorDcCoefficient = 1.0f - std::exp(
                    -2.0f * kPi * 9.0f / sampleRate);
                acidOscillatorDc += (raw - acidOscillatorDc)
                    * oscillatorDcCoefficient;
                const float oscillator = raw - acidOscillatorDc;

                const float keyRatio = std::pow(2.0f,
                    (static_cast<float>(key) - 36.0f) / 12.0f);
                const float cutoff = params.cutoffHz
                    * std::pow(keyRatio, keyTrack)
                    * std::pow(2.0f, params.filterEnvelopeOctaves
                        * filterEnvelope + cutoffMod * 4.0f + cutoffOffset);
                const float targetCutoff = clamp(cutoff, 25.0f,
                    sampleRate * 0.41f);
                const float targetCutoffLog2 = std::log2(targetCutoff);
                const float cutoffSmoothingSeconds =
                    targetCutoffLog2 > acidCutoffLog2 ? 0.0020f : 0.0060f;
                const float cutoffSmoothing = 1.0f - std::exp(-1.0f
                    / std::max(1.0f,
                        sampleRate * cutoffSmoothingSeconds));
                acidCutoffLog2 += (targetCutoffLog2 - acidCutoffLog2)
                    * cutoffSmoothing;
                const float effectiveCutoff = std::exp2(acidCutoffLog2);
                const float coefficient = 1.0f - std::exp(
                    -2.0f * kPi * effectiveCutoff / (sampleRate * 2.0f));
                const float acidResonance = clamp(0.34f
                    + acidResonanceControl * 0.48f
                    + params.resonance * 0.16f,
                    0.0f, 0.96f);
                const float driveGain = 1.0f + filterDrive * 3.2f
                    + acidDrive * 4.8f;
                const float feedback = acidResonance * 3.72f;
                for (uint32_t substep = 0u; substep < 2u; ++substep) {
                    float stageInput = std::tanh(oscillator * driveGain
                        - acidFilterStages[3u] * feedback);
                    for (auto& stage : acidFilterStages) {
                        stage += (stageInput - stage) * coefficient;
                        stage = flushDenormal(stage);
                        stageInput = std::tanh(stage);
                    }
                }
                float acidBody = acidFilterStages[3u]
                    * (1.0f + acidResonance * 1.35f) * 0.72f;
                float acidEdge = (acidFilterStages[2u]
                    - acidFilterStages[3u])
                    * (1.15f + acidResonance * 2.2f);
                const float bodyDcCoefficient = 1.0f - std::exp(
                    -2.0f * kPi * 12.0f / sampleRate);
                acidBodyDc += (acidBody - acidBodyDc) * bodyDcCoefficient;
                acidEdgeDc += (acidEdge - acidEdgeDc) * bodyDcCoefficient;
                acidBody -= acidBodyDc;
                acidEdge -= acidEdgeDc;
                const float mono = acidBody + acidEdge * 0.16f;
                const float side = acidEdge * bodyWidth * 0.12f;
                bodyLeft = (mono + side) * bodyGain * bodyOnsetGain;
                bodyRight = (mono - side) * bodyGain * bodyOnsetGain;
            } else if (bodyEngine == BassBodyEngine::Rave) {
                // RAVE is a phase-coherent bassline voice: a continuously
                // variable core oscillator, octave divider, pre-filter noise,
                // parallel overdrive, and a dual-tap short-delay smear. The
                // shared Shape section remains its main musical filter.
                const auto smoothRave = [sampleRate](float& current,
                    float target) {
                    const float coefficient = 1.0f - std::exp(-1.0f
                        / std::max(1.0f, sampleRate * 0.012f));
                    current += (target - current) * coefficient;
                };
                smoothRave(raveWaveSmoothed, clamp(params.bodyControlA
                    + morphMod * 0.5f, 0.0f, 1.0f));
                smoothRave(raveSubSmoothed, clamp(params.bodyControlB
                    + densityMod * 0.5f, 0.0f, 1.0f));
                smoothRave(raveSmearSmoothed, bodyControlC);
                smoothRave(raveNoiseSmoothed, bodyControlD);
                smoothRave(raveDriveSmoothed, bodyControlE);

                const float increment = std::min(0.42f,
                    pitchHz / sampleRate);
                const float phase = bodyPhase;
                const float triangle = 1.0f
                    - 4.0f * std::fabs(phase - 0.5f);
                const float saw = phase * 2.0f - 1.0f
                    - polyBlep(phase, increment);
                float pulse = phase < 0.5f ? 1.0f : -1.0f;
                pulse += polyBlep(phase, increment);
                float pulsePhase = phase - 0.5f;
                if (pulsePhase < 0.0f) pulsePhase += 1.0f;
                pulse -= polyBlep(pulsePhase, increment);
                const float core = raveWaveSmoothed < 0.5f
                    ? lerp(triangle, saw, raveWaveSmoothed * 2.0f)
                    : lerp(saw, pulse,
                        (raveWaveSmoothed - 0.5f) * 2.0f);
                nextPhase(bodyPhase, increment);

                const float subIncrement = std::min(0.42f,
                    pitchHz * 0.5f / sampleRate);
                const float subPhase = raveSubPhase;
                float divider = subPhase < 0.5f ? 1.0f : -1.0f;
                divider += polyBlep(subPhase, subIncrement);
                float dividerEdge = subPhase - 0.5f;
                if (dividerEdge < 0.0f) dividerEdge += 1.0f;
                divider -= polyBlep(dividerEdge, subIncrement);
                nextPhase(raveSubPhase, subIncrement);
                const float saturatedSub = std::tanh(divider * 1.55f)
                    / std::tanh(1.55f);

                const float rawNoise = randomBipolar();
                const float noiseCoefficient = 1.0f - std::exp(
                    -2.0f * kPi * 4200.0f / sampleRate);
                raveNoiseLow += (rawNoise - raveNoiseLow)
                    * noiseCoefficient;
                raveNoiseLow = flushDenormal(raveNoiseLow);
                const float clean = core * 0.66f
                    + saturatedSub * raveSubSmoothed * 0.54f
                    + raveNoiseLow * raveNoiseSmoothed * 0.20f;
                const float driveGain = 1.0f
                    + raveDriveSmoothed * 7.5f;
                const float driven = std::tanh(clean * driveGain)
                    / std::max(0.1f, std::tanh(driveGain));
                float raveBody = lerp(clean, driven,
                    raveDriveSmoothed * 0.88f);
                const float dcCoefficient = 1.0f - std::exp(
                    -2.0f * kPi * 8.0f / sampleRate);
                raveBodyDc += (raveBody - raveBodyDc) * dcCoefficient;
                raveBodyDc = flushDenormal(raveBodyDc);
                raveBody -= raveBodyDc;

                raveDelay[raveDelayWrite] = raveBody;
                nextPhase(raveSmearPhase,
                    (0.075f + raveSmearSmoothed * 0.12f) / sampleRate);
                const float smearBase = sampleRate
                    * (0.0014f + raveSmearSmoothed * 0.0048f);
                const float smearDepth = sampleRate
                    * (0.00008f + raveSmearSmoothed * 0.00095f);
                const float smearLeft = readRaveDelay(smearBase
                    + std::sin(2.0f * kPi * raveSmearPhase) * smearDepth);
                const float smearRight = readRaveDelay(smearBase
                    + std::sin(2.0f * kPi
                        * (raveSmearPhase + 0.37f)) * smearDepth);
                raveDelayWrite = (raveDelayWrite + 1u)
                    & (kRaveDelayCapacity - 1u);
                const float smearMid = (smearLeft + smearRight) * 0.5f;
                const float smearSide = (smearLeft - smearRight) * 0.5f
                    * bodyWidth;
                const float smearMix = raveSmearSmoothed * 0.74f;
                bodyLeft = lerp(raveBody, smearMid + smearSide, smearMix)
                    * bodyGain * bodyOnsetGain;
                bodyRight = lerp(raveBody, smearMid - smearSide, smearMix)
                    * bodyGain * bodyOnsetGain;
            } else if (bodyEngine == BassBodyEngine::Phase) {
                // PHASE is a compact two-operator phase-modulation voice.
                // Its feedback is bounded before entering the modulator and
                // the final fold is a continuous triangle fold, so every
                // direct control remains safe under audio-rate modulation.
                const auto smoothPhase = [sampleRate](float& current,
                    float target) {
                    current += (target - current) * (1.0f - std::exp(-1.0f
                        / std::max(1.0f, sampleRate * 0.010f)));
                };
                smoothPhase(phaseCarrierSmoothed, clamp(params.bodyControlA
                    + morphMod * 0.5f, 0.0f, 1.0f));
                smoothPhase(phaseRatioSmoothed, clamp(params.bodyControlB
                    + densityMod * 0.5f, 0.0f, 1.0f));
                smoothPhase(phaseIndexSmoothed, bodyControlC);
                smoothPhase(phaseFeedbackSmoothed, bodyControlD);
                smoothPhase(phaseFoldSmoothed, bodyControlE);

                const float ratio = 0.5f * std::pow(
                    16.0f, phaseRatioSmoothed);
                const float carrierIncrement = std::min(0.42f,
                    pitchHz / sampleRate);
                const float modulatorIncrement = std::min(0.45f,
                    pitchHz * ratio / sampleRate);
                const float feedback = phaseFeedbackSmoothed * 0.88f;
                const float modulator = std::sin(2.0f * kPi
                    * phaseModulatorPhase
                    + phaseFeedbackSample * feedback * 2.4f);
                phaseFeedbackSample = flushDenormal(std::tanh(
                    modulator * (1.0f + feedback * 1.8f)));
                nextPhase(phaseModulatorPhase, modulatorIncrement);

                const float index = phaseIndexSmoothed
                    * phaseIndexSmoothed * 7.5f;
                const auto carrierWave = [this](float phase) {
                    phase -= std::floor(phase);
                    const float sine = std::sin(2.0f * kPi * phase);
                    const float triangle = 1.0f
                        - 4.0f * std::fabs(phase - 0.5f);
                    const float roundedPulse = std::tanh(sine * 3.5f)
                        / std::tanh(3.5f);
                    return phaseCarrierSmoothed < 0.5f
                        ? lerp(sine, triangle,
                            phaseCarrierSmoothed * 2.0f)
                        : lerp(triangle, roundedPulse,
                            (phaseCarrierSmoothed - 0.5f) * 2.0f);
                };
                const float modulationCycles = modulator * index
                    / (2.0f * kPi);
                const float stereoOffset = index * bodyWidth * 0.012f;
                float phaseLeft = carrierWave(bodyPhase
                    + modulationCycles * (1.0f - stereoOffset));
                float phaseRight = carrierWave(bodyPhase
                    + modulationCycles * (1.0f + stereoOffset));
                nextPhase(bodyPhase, carrierIncrement);
                const float foldGain = 1.0f + phaseFoldSmoothed * 5.5f;
                const auto fold = [foldGain](float input) {
                    return (2.0f / kPi) * std::asin(std::sin(
                        input * foldGain * (kPi * 0.5f)));
                };
                phaseLeft = lerp(phaseLeft, fold(phaseLeft),
                    phaseFoldSmoothed);
                phaseRight = lerp(phaseRight, fold(phaseRight),
                    phaseFoldSmoothed);
                bodyLeft = phaseLeft * bodyGain * 0.74f * bodyOnsetGain;
                bodyRight = phaseRight * bodyGain * 0.74f * bodyOnsetGain;
            } else if (bodyEngine == BassBodyEngine::Throat) {
                // THROAT passes a variable glottal source through six
                // interpolated vowel resonators. Their coefficients update at
                // a bounded control rate while audio and parameter smoothing
                // remain sample-accurate.
                const auto smoothThroat = [sampleRate](float& current,
                    float target) {
                    current += (target - current) * (1.0f - std::exp(-1.0f
                        / std::max(1.0f, sampleRate * 0.014f)));
                };
                smoothThroat(throatVowelSmoothed, clamp(params.bodyControlA
                    + morphMod * 0.5f, 0.0f, 1.0f));
                smoothThroat(throatShiftSmoothed, clamp(params.bodyControlB
                    + densityMod * 0.5f, 0.0f, 1.0f));
                smoothThroat(throatFocusSmoothed, bodyControlC);
                smoothThroat(throatSourceSmoothed, bodyControlD);
                smoothThroat(throatDriveSmoothed, bodyControlE);

                constexpr std::array<std::array<float, 6u>, 5u> formants {{
                    {{ 650.0f, 1080.0f, 2650.0f, 2900.0f, 3250.0f, 3700.0f }},
                    {{ 400.0f, 1700.0f, 2600.0f, 3200.0f, 3580.0f, 4100.0f }},
                    {{ 290.0f, 1870.0f, 2800.0f, 3250.0f, 3540.0f, 4200.0f }},
                    {{ 400.0f, 800.0f, 2600.0f, 2800.0f, 3000.0f, 3500.0f }},
                    {{ 350.0f, 600.0f, 2400.0f, 2675.0f, 2950.0f, 3300.0f }},
                }};
                constexpr std::array<std::array<float, 6u>, 5u> levels {{
                    {{ 1.00f, 0.50f, 0.30f, 0.20f, 0.12f, 0.08f }},
                    {{ 0.70f, 1.00f, 0.35f, 0.20f, 0.12f, 0.08f }},
                    {{ 0.80f, 1.00f, 0.45f, 0.20f, 0.12f, 0.08f }},
                    {{ 1.00f, 0.70f, 0.25f, 0.16f, 0.10f, 0.06f }},
                    {{ 1.00f, 0.55f, 0.18f, 0.12f, 0.08f, 0.05f }},
                }};
                const float vowelPosition = throatVowelSmoothed * 4.0f;
                const uint32_t vowelOne = std::min<uint32_t>(3u,
                    static_cast<uint32_t>(vowelPosition));
                const uint32_t vowelTwo = vowelOne + 1u;
                const float vowelFraction = vowelPosition
                    - static_cast<float>(vowelOne);
                const float shiftRatio = std::pow(2.0f,
                    (throatShiftSmoothed - 0.5f) * 2.5f);
                if (throatCoefficientCounter == 0u) {
                    for (uint32_t i = 0u; i < throatBands.size(); ++i) {
                        const float frequency = clamp(lerp(
                            formants[vowelOne][i], formants[vowelTwo][i],
                            vowelFraction) * shiftRatio,
                            55.0f, sampleRate * 0.39f);
                        throatCoefficients[i] = std::tan(
                            kPi * frequency / sampleRate);
                    }
                }
                throatCoefficientCounter = (throatCoefficientCounter + 1u)
                    & 15u;

                const float increment = std::min(0.42f,
                    pitchHz / sampleRate);
                const float sourcePhase = bodyPhase;
                const float saw = sourcePhase * 2.0f - 1.0f
                    - polyBlep(sourcePhase, increment);
                const float pulseWidth = lerp(0.18f, 0.72f,
                    throatSourceSmoothed);
                float pulse = sourcePhase < pulseWidth ? 1.0f : -1.0f;
                pulse += polyBlep(sourcePhase, increment);
                float pulseEdge = sourcePhase - pulseWidth;
                if (pulseEdge < 0.0f) pulseEdge += 1.0f;
                pulse -= polyBlep(pulseEdge, increment);
                const float glottal = lerp(saw, pulse,
                    throatSourceSmoothed * 0.82f);
                nextPhase(bodyPhase, increment);

                const float resonance = 0.30f
                    + throatFocusSmoothed * 0.67f;
                const float damping = 2.0f - resonance * 1.92f;
                float rawLeft = 0.0f;
                float rawRight = 0.0f;
                float levelSum = 0.0f;
                for (uint32_t i = 0u; i < throatBands.size(); ++i) {
                    const float level = lerp(levels[vowelOne][i],
                        levels[vowelTwo][i], vowelFraction);
                    const float band = SvfState::stage(glottal,
                        throatCoefficients[i], damping,
                        BassFilterType::Band12,
                        throatBands[i].ic1, throatBands[i].ic2) * damping;
                    const float pan = i == 0u ? 0.0f
                        : ((i & 1u) ? -1.0f : 1.0f)
                            * (0.04f + static_cast<float>(i) * 0.018f)
                            * bodyWidth;
                    rawLeft += band * level * (0.5f - pan);
                    rawRight += band * level * (0.5f + pan);
                    levelSum += level;
                }
                const float normalization = 2.4f
                    / std::max(0.8f, levelSum);
                float mid = (rawLeft + rawRight) * 0.5f * normalization;
                float side = (rawLeft - rawRight) * 0.5f * normalization;
                const float sideCoefficient = 1.0f - std::exp(
                    -2.0f * kPi * 135.0f / sampleRate);
                throatSideLow += (side - throatSideLow) * sideCoefficient;
                throatSideLow = flushDenormal(throatSideLow);
                side -= throatSideLow;
                const float driveGain = 1.0f
                    + throatDriveSmoothed * 5.0f;
                const float driveNorm = std::max(0.1f,
                    std::tanh(driveGain));
                bodyLeft = std::tanh((mid + side) * driveGain)
                    / driveNorm * bodyGain * 0.78f * bodyOnsetGain;
                bodyRight = std::tanh((mid - side) * driveGain)
                    / driveNorm * bodyGain * 0.78f * bodyOnsetGain;
            } else {
                // SYNC uses a master at played pitch to reset a swept slave.
                // Ring introduces the master without adding another free
                // oscillator, preserving a firm pitch center for bass work.
                const auto smoothSync = [sampleRate](float& current,
                    float target) {
                    current += (target - current) * (1.0f - std::exp(-1.0f
                        / std::max(1.0f, sampleRate * 0.010f)));
                };
                smoothSync(syncWaveSmoothed, clamp(params.bodyControlA
                    + morphMod * 0.5f, 0.0f, 1.0f));
                smoothSync(syncRatioSmoothed, clamp(params.bodyControlB
                    + densityMod * 0.5f, 0.0f, 1.0f));
                smoothSync(syncSweepSmoothed, bodyControlC);
                smoothSync(syncDecaySmoothed, bodyControlD);
                smoothSync(syncRingSmoothed, bodyControlE);

                const float decaySeconds = 0.015f * std::pow(
                    80.0f, syncDecaySmoothed);
                syncSweepEnvelope *= std::exp(-1.0f
                    / std::max(1.0f, decaySeconds * sampleRate));
                if (syncSweepEnvelope < 1.0e-7f)
                    syncSweepEnvelope = 0.0f;
                const float ratio = 1.0f + syncRatioSmoothed * 7.0f;
                const float sweepOctaves = syncSweepSmoothed
                    * syncSweepSmoothed * 4.5f * syncSweepEnvelope;
                const float masterIncrement = std::min(0.42f,
                    pitchHz / sampleRate);
                const float slaveIncrement = std::min(0.46f,
                    pitchHz * ratio * std::pow(2.0f, sweepOctaves)
                        / sampleRate);
                syncMasterPhase += masterIncrement;
                const bool masterWrapped = syncMasterPhase >= 1.0f;
                if (masterWrapped) syncMasterPhase -= 1.0f;
                syncSlavePhase += slaveIncrement;
                if (masterWrapped) {
                    syncSlavePhase = syncMasterPhase
                        / std::max(1.0e-6f, masterIncrement)
                        * slaveIncrement;
                }
                syncSlavePhase -= std::floor(syncSlavePhase);

                const float triangle = 1.0f
                    - 4.0f * std::fabs(syncSlavePhase - 0.5f);
                const float saw = syncSlavePhase * 2.0f - 1.0f
                    - polyBlep(syncSlavePhase, slaveIncrement);
                float pulse = syncSlavePhase < 0.5f ? 1.0f : -1.0f;
                pulse += polyBlep(syncSlavePhase, slaveIncrement);
                float pulseEdge = syncSlavePhase - 0.5f;
                if (pulseEdge < 0.0f) pulseEdge += 1.0f;
                pulse -= polyBlep(pulseEdge, slaveIncrement);
                const float slave = syncWaveSmoothed < 0.5f
                    ? lerp(triangle, saw, syncWaveSmoothed * 2.0f)
                    : lerp(saw, pulse,
                        (syncWaveSmoothed - 0.5f) * 2.0f);
                const float master = std::sin(2.0f * kPi
                    * syncMasterPhase);
                const float ringed = slave * master * 1.55f;
                const float mono = lerp(slave, ringed,
                    syncRingSmoothed * 0.78f);
                const float side = (ringed - slave) * bodyWidth * 0.10f
                    + (pulse - saw) * bodyWidth * 0.025f;
                const float syncToneCoefficient = 1.0f - std::exp(
                    -2.0f * kPi * 6200.0f / sampleRate);
                syncOutputLeft += ((mono + side) - syncOutputLeft)
                    * syncToneCoefficient;
                syncOutputRight += ((mono - side) - syncOutputRight)
                    * syncToneCoefficient;
                syncOutputLeft = flushDenormal(syncOutputLeft);
                syncOutputRight = flushDenormal(syncOutputRight);
                bodyLeft = syncOutputLeft * bodyGain * 0.72f
                    * bodyOnsetGain;
                bodyRight = syncOutputRight * bodyGain * 0.72f
                    * bodyOnsetGain;
            }

            // Engine changes are rare control-rate events. Preserve exact body
            // continuity, then release the correction with a raised-cosine
            // window while the destination engine establishes its own state.
            if (bodyEngine != renderedBodyEngine) {
                bodyContinuityLeft = lastBodyLeft - bodyLeft;
                bodyContinuityRight = lastBodyRight - bodyRight;
                bodyContinuityFrames = std::max<uint32_t>(32u,
                    static_cast<uint32_t>(sampleRate * 0.010f));
                bodyContinuityPosition = 0u;
                renderedBodyEngine = bodyEngine;
            }
            if (bodyContinuityPosition < bodyContinuityFrames) {
                const float progress = static_cast<float>(
                    bodyContinuityPosition) / static_cast<float>(std::max(
                        1u, bodyContinuityFrames - 1u));
                const float correctionGain = 0.5f
                    + 0.5f * std::cos(kPi * progress);
                bodyLeft += bodyContinuityLeft * correctionGain;
                bodyRight += bodyContinuityRight * correctionGain;
                ++bodyContinuityPosition;
            }
            bodyLeft = clamp(bodyLeft, -1.6f, 1.6f);
            bodyRight = clamp(bodyRight, -1.6f, 1.6f);
            lastBodyLeft = bodyLeft;
            lastBodyRight = bodyRight;
            bodyLeft *= bodyLayerEnvelope;
            bodyRight *= bodyLayerEnvelope;

            float textureLeft = 0.0f;
            float textureRight = 0.0f;
            const float textureLevel = clamp(params.textureLevel
                + textureLevelMod * 0.5f, 0.0f, 1.0f);
            const float color = clamp(params.textureColor
                + textureColorMod * 0.5f,
                0.0f, 1.0f);
            const auto textureMode = enumValue<BassTextureMode>(
                params.textureMode, 4u);
            if (textureMode != BassTextureMode::Off && textureLevel > 0.0f) {
                const float noiseLeft = randomBipolar();
                const float noiseRight = randomBipolar();
                if (textureMode == BassTextureMode::Noise) {
                    textureLeft = noiseLeft;
                    textureRight = noiseRight;
                } else if (textureMode == BassTextureMode::Corrode) {
                    if (corrodeCounter == 0u) {
                        const float trackingHz = lerp(120.0f,
                            pitchHz * lerp(3.0f, 24.0f, color),
                            textureTrack);
                        corrodeCounter = std::max<uint32_t>(1u,
                            static_cast<uint32_t>(sampleRate
                                / std::max(1.0f, trackingHz)));
                        corrode = noiseLeft;
                    } else {
                        --corrodeCounter;
                    }
                    textureLeft = corrode;
                    textureRight = -corrode * 0.82f;
                } else if (textureMode == BassTextureMode::Ring) {
                    nextPhase(ringPhase, std::min(0.45f,
                        pitchHz * lerp(1.5f, 9.0f, color) / sampleRate));
                    const float ring = std::sin(2.0f * kPi * ringPhase);
                    textureLeft = bodyLeft * ring;
                    textureRight = bodyRight * -ring;
                } else {
                    const float wireL = std::sin(2.0f * kPi
                        * (bodyPhase * lerp(3.0f, 13.0f, color)
                            + noiseLeft * 0.035f));
                    const float wireR = std::sin(2.0f * kPi
                        * (bodyPhase * lerp(3.2f, 13.7f, color)
                            + noiseRight * 0.035f));
                    textureLeft = wireL * (0.55f + noiseLeft * 0.25f);
                    textureRight = wireR * (0.55f + noiseRight * 0.25f);
                }
                const float textureMid = (textureLeft + textureRight) * 0.5f;
                const float textureSide = (textureLeft - textureRight)
                    * 0.5f * textureWidth;
                textureLeft = (textureMid + textureSide)
                    * textureLevel * textureLayerEnvelope * 0.48f;
                textureRight = (textureMid - textureSide)
                    * textureLevel * textureLayerEnvelope * 0.48f;
            }

            const float keyRatio = std::pow(2.0f,
                (static_cast<float>(key) - 36.0f) / 12.0f);
            float cutoff = params.cutoffHz
                * std::pow(keyRatio, keyTrack)
                * std::pow(2.0f, params.filterEnvelopeOctaves
                    * filterEnvelope + cutoffMod * 4.0f);
            cutoff = clamp(cutoff, 18.0f, sampleRate * 0.43f);
            const float resonance = clamp(params.resonance
                + resonanceMod * 0.5f, 0.0f, 1.0f);
            const auto filterType = enumValue<BassFilterType>(
                params.filterType, 5u);
            const float drive = 1.0f + filterDrive * 5.0f;
            const auto filterLayer = [&](float input, uint32_t layer,
                uint32_t lane, float routing) {
                const float driven = std::tanh(input * drive)
                    / std::max(0.01f, std::tanh(drive));
                const float filtered = filters[layer][lane].process(
                    driven, filterType, cutoff, resonance, sampleRate);
                return lerp(input, filtered, routing);
            };
            const float foundationFilteredL = filterLayer(foundation, 0u, 0u,
                params.foundationFilter);
            const float foundationFilteredR = filterLayer(foundation, 0u, 1u,
                params.foundationFilter);
            bodyLeft = filterLayer(bodyLeft, 1u, 0u, params.bodyFilter);
            bodyRight = filterLayer(bodyRight, 1u, 1u, params.bodyFilter);
            textureLeft = filterLayer(textureLeft, 2u, 0u,
                params.textureFilter);
            textureRight = filterLayer(textureRight, 2u, 1u,
                params.textureFilter);

            const float expression = 0.72f;
            const float amplitude = envelope * expression
                * clamp(1.0f + ampMod * 0.72f, 0.0f, 2.0f);
            out.left = (foundationFilteredL + bodyLeft + textureLeft)
                * amplitude;
            out.right = (foundationFilteredR + bodyRight + textureRight)
                * amplitude;
            out.foundation = foundation * amplitude;
            out.level = envelope;
            if (voiceContinuityPending) {
                voiceContinuityFrames = std::max<uint32_t>(32u,
                    static_cast<uint32_t>(sampleRate * 0.008f));
                voiceContinuityPosition = 0u;
                voiceContinuityPending = false;
            }
            if (voiceContinuityPosition < voiceContinuityFrames) {
                const float progress = static_cast<float>(
                    voiceContinuityPosition) / static_cast<float>(std::max(
                        1u, voiceContinuityFrames - 1u));
                const float correctionGain = 0.5f
                    + 0.5f * std::cos(kPi * progress);
                out.left += (voiceContinuitySourceLeft - out.left)
                    * correctionGain;
                out.right += (voiceContinuitySourceRight - out.right)
                    * correctionGain;
                ++voiceContinuityPosition;
            }
            out.left = clamp(out.left, -2.0f, 2.0f);
            out.right = clamp(out.right, -2.0f, 2.0f);
            lastOutputLeft = out.left;
            lastOutputRight = out.right;
            (void)transportPlaying;
            return out;
        }
    };

    Voice* matchingVoice(int key, int32_t noteId, int16_t channel)
    {
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            const bool noteMatches = noteId >= 0
                ? voice.noteId == noteId && voice.key == key
                : voice.key == key;
            const bool channelMatches = channel < 0
                || voice.channel == channel;
            if (noteMatches && channelMatches)
                return &voice;
        }
        return nullptr;
    }

    template <typename Fn>
    void forMatchingVoices(int key, int32_t noteId, int16_t channel, Fn fn)
    {
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            const bool noteMatches = noteId >= 0
                ? voice.noteId == noteId && (key < 0 || voice.key == key)
                : (key < 0 || voice.key == key);
            const bool channelMatches = channel < 0 || voice.channel == channel;
            if (noteMatches && channelMatches) fn(voice);
        }
    }

    static LowformParams sanitize(LowformParams p)
    {
        const auto range = [](float v, float fallback,
                              float minimum, float maximum) {
            return clamp(std::isfinite(v) ? v : fallback,
                minimum, maximum);
        };
        const auto unit = [&range](float v, float fallback) {
            return range(v, fallback, 0.0f, 1.0f);
        };
        p.outputGainDb = range(p.outputGainDb, -12.0f, -36.0f, 6.0f);
        p.voiceMode = std::round(unit(p.voiceMode, 1.0f));
        p.glideMs = range(p.glideMs, 28.0f, 0.0f, 2000.0f);
        p.foundationWave = std::round(range(
            p.foundationWave, 0.0f, 0.0f, 2.0f));
        p.foundationOctave = std::round(range(
            p.foundationOctave, 0.0f, -1.0f, 0.0f));
        p.foundationLevel = range(p.foundationLevel, 0.93f, 0.0f, 1.30f);
        p.pitchPunchSemitones = range(
            p.pitchPunchSemitones, 3.0f, -24.0f, 24.0f);
        p.punchTimeMs = range(p.punchTimeMs, 48.0f, 5.0f, 500.0f);
        p.foundationReturn = unit(p.foundationReturn, 0.72f);
        p.bodyEngine = std::round(range(p.bodyEngine, 0.0f, 0.0f, 7.0f));
        p.bodyLevel = range(p.bodyLevel, 0.62f, 0.0f, 1.16f);
        p.bodyControlA = unit(p.bodyControlA, 0.42f);
        p.bodyControlB = unit(p.bodyControlB, 0.48f);
        p.bodyWidth = unit(p.bodyWidth, 0.47f);
        p.bodyControlC = unit(p.bodyControlC, 0.40f);
        p.bodyControlD = unit(p.bodyControlD, 0.24f);
        p.bodyControlE = unit(p.bodyControlE, 0.59f);
        p.textureMode = std::round(range(
            p.textureMode, 0.0f, 0.0f, 4.0f));
        p.textureLevel = unit(p.textureLevel, 0.0f);
        p.textureColor = unit(p.textureColor, 0.52f);
        p.textureTrack = unit(p.textureTrack, 0.72f);
        p.textureWidth = unit(p.textureWidth, 0.47f);
        p.filterType = std::round(range(
            p.filterType, 2.0f, 0.0f, 5.0f));
        p.cutoffHz = range(p.cutoffHz, 820.0f, 20.0f, 20000.0f);
        p.resonance = unit(p.resonance, 0.18f);
        p.filterDrive = unit(p.filterDrive, 0.31f);
        p.keyTrack = unit(p.keyTrack, 0.34f);
        p.foundationFilter = unit(p.foundationFilter, 0.12f);
        p.bodyFilter = unit(p.bodyFilter, 1.0f);
        p.textureFilter = unit(p.textureFilter, 1.0f);
        p.attackSeconds = range(
            p.attackSeconds, 0.004f, 0.0005f, 2.0f);
        p.decaySeconds = range(p.decaySeconds, 0.22f, 0.005f, 5.0f);
        p.sustain = unit(p.sustain, 0.78f);
        p.releaseSeconds = range(
            p.releaseSeconds, 0.30f, 0.005f, 8.0f);
        p.filterEnvelopeOctaves = range(
            p.filterEnvelopeOctaves, 1.4f, -6.0f, 6.0f);
        p.filterDecaySeconds = range(
            p.filterDecaySeconds, 0.18f, 0.005f, 5.0f);
        p.motionClock = std::round(unit(p.motionClock, 0.0f));
        for (auto& mod : p.mods) {
            mod.shape = std::round(range(mod.shape, 0.0f, 0.0f,
                static_cast<float>(kBassModShapeCount - 1u)));
            mod.rate = unit(mod.rate, 0.35f);
            mod.depth = range(mod.depth, 0.0f, -1.0f, 1.0f);
            mod.target = std::round(range(mod.target, 0.0f, 0.0f,
                static_cast<float>(kBassModTargetCount - 1u)));
            mod.secondaryDepth = range(
                mod.secondaryDepth, 0.0f, -1.0f, 1.0f);
            mod.secondaryTarget = std::round(range(
                mod.secondaryTarget, 0.0f, 0.0f,
                static_cast<float>(kBassModTargetCount - 1u)));
        }
        for (auto& route : p.expressionRoutes) {
            route.target = std::round(range(route.target, 0.0f, 0.0f,
                static_cast<float>(kBassModTargetCount - 1u)));
            route.depth = range(route.depth, 0.0f, -1.0f, 1.0f);
        }
        p.bodyDecaySeconds = range(
            p.bodyDecaySeconds, 12.0f, 0.02f, 12.0f);
        p.textureAttackSeconds = range(
            p.textureAttackSeconds, 0.0f, 0.0f, 2.0f);
        p.textureDecaySeconds = range(
            p.textureDecaySeconds, 12.0f, 0.02f, 12.0f);
        p.modalDrive = unit(p.modalDrive, 0.25f);
        p.modWheelAmount = unit(p.modWheelAmount, 0.75f);
        p.dynamicsBite = unit(p.dynamicsBite, 0.03f);
        p.tube = unit(p.tube, 0.2f);
        p.shredCircuit = std::round(range(p.shredCircuit, 0.0f, 0.0f,
            static_cast<float>(kBassShredCircuitCount - 1u)));
        p.shred = unit(p.shred, 0.06f);
        p.shredFeedback = unit(p.shredFeedback, 0.0f);
        p.shredColor = unit(p.shredColor, 0.55f);
        p.shredMix = unit(p.shredMix, 0.0f);
        p.dynamics = unit(p.dynamics, 0.26f);
        p.saturation = unit(p.saturation, 0.12f);
        p.clip = unit(p.clip, 0.0f);
        p.tilt = range(p.tilt, 0.0f, -1.0f, 1.0f);
        p.maximizer = unit(p.maximizer, 0.24f);
        return p;
    }

    double sampleRate_ = 48000.0;
    LowformParams params_ {};
    std::array<Voice, kBassVoiceCount> voices_ {};
    BassAmplifierCircuit amplifier_ {};
    BassShredStereo shred_ {};
    BreakBus breakBus_ {};
    std::array<LowPair, 2u> cleanLow_ {};
    std::array<LowPair, 2u> wetLow_ {};
    std::array<float, kBassModCount> modActivity_ {};
    double transportBeat_ = 0.0;
    double tempo_ = 120.0;
    bool transportPlaying_ = false;
    float pitchBendSemitones_ = 0.0f;
    float modWheel_ = 0.0f;
    float outputPeak_ = 0.0f;
    uint64_t ageCounter_ = 1u;
};

} // namespace s3g
