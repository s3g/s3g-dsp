#pragma once

#include "s3g_drum_primitives.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g {

enum class DrumTomArticulation : uint8_t {
    Head = 0u,
    RimStick = 1u,
};

enum class DrumTomSlot : uint8_t {
    Low = 0u,
    Mid = 1u,
    High = 2u,
};

// A fully resolved, per-hit tom timbre. Instrument-level controls are reduced
// to this vocabulary before entering the voice bank, which lets Floor Tom and
// the three-tom instrument share the synthesis and lifecycle implementation.
struct DrumTomVoiceSettings {
    float frequencyHz = 72.0f;
    float pitchDropSemitones = 8.0f;
    float pitchSweepMs = 45.0f;
    float shellSpread = 0.42f;
    float body = 0.70f;
    float ring = 0.42f;
    float bodyDecaySeconds = 0.70f;
    float punch = 0.72f;
    float damping = 0.35f;
    float rimLevel = 0.55f;
    float rimCharacter = 0.48f;
    float rimDecaySeconds = 0.070f;
    float stickLevel = 0.35f;
    float stickTone = 0.58f;
    float stickDecayMs = 4.5f;
    float stereoPosition = 0.0f;
    float velocitySensitivity = 0.90f;
    uint32_t seedSalt = 0x544f4d20u;
};

inline DrumTomVoiceSettings drumSanitizeTomVoiceSettings(
    DrumTomVoiceSettings settings, double sampleRate)
{
    const float sr = static_cast<float>(drumSafeSampleRate(sampleRate));
    settings.frequencyHz = clamp(
        drumFiniteOr(settings.frequencyHz, 72.0f), 18.0f, sr * 0.20f);
    settings.pitchDropSemitones = clamp(
        drumFiniteOr(settings.pitchDropSemitones, 8.0f), -12.0f, 36.0f);
    settings.pitchSweepMs = clamp(
        drumFiniteOr(settings.pitchSweepMs, 45.0f), 0.5f, 500.0f);
    settings.shellSpread = clamp(
        drumFiniteOr(settings.shellSpread, 0.42f), 0.0f, 1.0f);
    settings.body = clamp(drumFiniteOr(settings.body, 0.70f), 0.0f, 1.0f);
    settings.ring = clamp(drumFiniteOr(settings.ring, 0.42f), 0.0f, 1.0f);
    settings.bodyDecaySeconds = clamp(
        drumFiniteOr(settings.bodyDecaySeconds, 0.70f), 0.01f, 8.0f);
    settings.punch = clamp(
        drumFiniteOr(settings.punch, 0.72f), 0.0f, 1.0f);
    settings.damping = clamp(
        drumFiniteOr(settings.damping, 0.35f), 0.0f, 1.0f);
    settings.rimLevel = clamp(
        drumFiniteOr(settings.rimLevel, 0.55f), 0.0f, 1.0f);
    settings.rimCharacter = clamp(
        drumFiniteOr(settings.rimCharacter, 0.48f), 0.0f, 1.0f);
    settings.rimDecaySeconds = clamp(
        drumFiniteOr(settings.rimDecaySeconds, 0.070f), 0.005f, 1.0f);
    settings.stickLevel = clamp(
        drumFiniteOr(settings.stickLevel, 0.35f), 0.0f, 1.0f);
    settings.stickTone = clamp(
        drumFiniteOr(settings.stickTone, 0.58f), 0.0f, 1.0f);
    settings.stickDecayMs = clamp(
        drumFiniteOr(settings.stickDecayMs, 4.5f), 0.25f, 80.0f);
    settings.stereoPosition = clamp(
        drumFiniteOr(settings.stereoPosition, 0.0f), -1.0f, 1.0f);
    settings.velocitySensitivity = clamp(
        drumFiniteOr(settings.velocitySensitivity, 0.90f), 0.0f, 1.0f);
    return settings;
}

// Conservative -120 dB voice bound. The fixed modal decay and envelope
// definitions below use the same factors so CLAP wrappers can safely derive a
// prospective tail without duplicating the synthesis implementation.
inline double drumTomVoiceTailSeconds(const DrumTomVoiceSettings& source,
    DrumTomArticulation articulation, double sampleRate = 48000.0)
{
    const DrumTomVoiceSettings settings = drumSanitizeTomVoiceSettings(
        source, sampleRate);
    if (articulation == DrumTomArticulation::RimStick) {
        const double shellCoupling = std::min<double>(
            settings.bodyDecaySeconds,
            settings.rimDecaySeconds * (2.20 + settings.ring));
        const double dampingScale = 1.0 - settings.damping * 0.48;
        const double longestShellMode = shellCoupling * dampingScale
            * (0.92 + settings.ring * 0.70);
        const double longestRimMode = settings.rimDecaySeconds
            * (3.20 - settings.rimCharacter);
        return std::min(40.0, std::max({
            0.05,
            shellCoupling * 2.15,
            longestShellMode * 2.15,
            longestRimMode * 2.15,
            static_cast<double>(settings.rimDecaySeconds) * 2.15,
            static_cast<double>(settings.stickDecayMs)
                * 0.001 * 1.80 * 2.15,
        }));
    }
    const double dampingScale = 1.0 - settings.damping * 0.48;
    const double longestMode = settings.bodyDecaySeconds * dampingScale
        * (0.92 + settings.ring * 0.70);
    const double strikeSeconds = 0.002
        + (0.025 - 0.002) * (1.0 - settings.punch);
    return std::min(40.0, std::max({
        0.08,
        longestMode * 2.15,
        static_cast<double>(settings.bodyDecaySeconds) * 2.15,
        strikeSeconds * 2.15,
        static_cast<double>(settings.pitchSweepMs) * 0.001 * 2.15,
    }));
}

// Fixed-capacity, allocation-free polyphonic tom voice bank. It deliberately
// stops before character, width and output gain: those are live instrument
// controls owned by the Floor Tom and Tom Kit cores.
template <uint32_t VoiceCount>
class DrumTomVoiceBank {
    static_assert(VoiceCount > 0u, "a tom bank needs at least one voice");

public:
    static constexpr uint32_t kVoiceCount = VoiceCount;

    void prepare(double sampleRate)
    {
        sampleRate_ = drumSafeSampleRate(sampleRate);
        reset();
    }

    void reset()
    {
        voices_.fill({});
        triggerCounter_ = 0u;
        activeVoiceCount_ = 0u;
    }

    bool trigger(const DrumTomVoiceSettings& source,
        DrumTomArticulation articulation, float velocity)
    {
        const DrumTomVoiceSettings settings = drumSanitizeTomVoiceSettings(
            source, sampleRate_);
        velocity = clamp(drumFiniteOr(velocity, 1.0f), 0.0f, 1.0f);
        const float velocityGain = lerp(
            1.0f, velocity, settings.velocitySensitivity);
        if (velocityGain <= 1.0e-7f) return false;

        uint32_t selected = 0u;
        float quietest = std::numeric_limits<float>::max();
        bool foundInactive = false;
        for (uint32_t index = 0u; index < voices_.size(); ++index) {
            const Voice& candidate = voices_[index];
            if (!candidate.active) {
                selected = index;
                foundInactive = true;
                break;
            }
            const float activity = voiceActivitySquared(candidate);
            if (activity < quietest) {
                quietest = activity;
                selected = index;
            }
        }

        Voice& voice = voices_[selected];
        voice = {};
        initialiseVoice(voice, selected, ++triggerCounter_, settings,
            articulation, velocity, velocityGain);
        if (foundInactive) {
            ++activeVoiceCount_;
        } else {
            activeVoiceCount_ = countActiveVoices();
        }
        return true;
    }

    void processFrame(float& mid, float& side)
    {
        mid = 0.0f;
        side = 0.0f;
        uint32_t remaining = 0u;
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            processVoice(voice, mid, side);
            if (voice.active) ++remaining;
        }
        activeVoiceCount_ = remaining;
        if (!std::isfinite(mid) || !std::isfinite(side)) {
            mid = 0.0f;
            side = 0.0f;
            reset();
        }
    }

    bool active() const { return activeVoiceCount_ != 0u; }
    uint32_t activeVoiceCount() const { return activeVoiceCount_; }

private:
    static constexpr uint32_t kHeadModeCount = 4u;
    static constexpr uint32_t kRimModeCount = 3u;

    struct Voice {
        bool active = false;
        DrumTomArticulation articulation = DrumTomArticulation::Head;
        float velocityGain = 1.0f;
        float velocityBrightness = 1.0f;
        float bodyLevel = 0.70f;
        float ringLevel = 0.42f;
        float punchLevel = 0.72f;
        float rimLevel = 0.55f;
        float rimCharacter = 0.48f;
        float stickLevel = 0.35f;
        float stereoPosition = 0.0f;
        float finalFrequencyHz = 72.0f;
        float pitchDropSemitones = 8.0f;
        float bodyPhase = 0.0f;
        float stickPhase = 0.0f;
        float stickFrequencyHz = 2200.0f;
        float contactPhase = 1.0f;
        float contactIncrement = 1.0f;
        float attackHighpassCoefficient = 0.02f;
        float attackLowpassCoefficient = 0.3f;
        float stickHighpassCoefficient = 0.08f;
        float stickLowpassCoefficient = 0.4f;
        float attackMidLow = 0.0f;
        float attackMidBand = 0.0f;
        float attackMidSmooth = 0.0f;
        float attackSideLow = 0.0f;
        float attackSideBand = 0.0f;
        float attackSideSmooth = 0.0f;
        float stickMidLow = 0.0f;
        float stickMidBand = 0.0f;
        float stickMidSmooth = 0.0f;
        float stickSideLow = 0.0f;
        float stickSideBand = 0.0f;
        float stickSideSmooth = 0.0f;
        std::array<DrumModalResonator, kHeadModeCount> headModes {};
        std::array<float, kHeadModeCount> headWeights {};
        std::array<DrumModalResonator, kRimModeCount> rimModes {};
        std::array<float, kRimModeCount> rimWeights {};
        DrumExponentialEnvelope pitchEnvelope {};
        DrumExponentialEnvelope bodyEnvelope {};
        DrumExponentialEnvelope attackEnvelope {};
        DrumExponentialEnvelope rimEnvelope {};
        DrumExponentialEnvelope stickEnvelope {};
        DrumRandom random {};
        uint32_t ageSamples = 0u;
        uint32_t maximumAgeSamples = 1u;
    };

    void initialiseVoice(Voice& voice, uint32_t voiceIndex,
        uint64_t triggerIndex, const DrumTomVoiceSettings& settings,
        DrumTomArticulation articulation, float velocity,
        float velocityGain)
    {
        const float sr = static_cast<float>(sampleRate_);
        voice.active = true;
        voice.articulation = articulation;
        voice.velocityGain = velocityGain;
        voice.velocityBrightness = lerp(1.0f,
            0.32f + velocity * 0.68f, settings.velocitySensitivity);
        voice.bodyLevel = settings.body;
        voice.ringLevel = settings.ring;
        voice.punchLevel = settings.punch;
        voice.rimLevel = settings.rimLevel;
        voice.rimCharacter = settings.rimCharacter;
        voice.stickLevel = settings.stickLevel;
        voice.stereoPosition = settings.stereoPosition;
        voice.finalFrequencyHz = settings.frequencyHz;
        voice.pitchDropSemitones = settings.pitchDropSemitones;
        voice.random.reset(drumMixedSeed(voiceIndex, triggerIndex,
            settings.seedSalt
                ^ (articulation == DrumTomArticulation::Head
                    ? 0x48454144u : 0x52494d53u)));

        const bool rimStrike = articulation == DrumTomArticulation::RimStick;
        const float dampingScale = 1.0f - settings.damping * 0.48f;
        const float shellDecay = rimStrike
            ? std::min(settings.bodyDecaySeconds,
                settings.rimDecaySeconds
                    * (2.20f + settings.ring))
            : settings.bodyDecaySeconds;
        const float spread = settings.shellSpread;
        const std::array<float, kHeadModeCount> headRatios {{
            1.0f,
            1.47f + spread * 0.15f,
            2.08f + spread * 0.36f,
            2.72f + spread * 0.70f,
        }};
        const std::array<float, kHeadModeCount> headDecays {{
            shellDecay * dampingScale,
            shellDecay * dampingScale * lerp(0.72f, 1.32f, settings.ring),
            shellDecay * dampingScale * lerp(0.42f, 1.62f, settings.ring),
            shellDecay * dampingScale * lerp(0.25f, 1.30f, settings.ring),
        }};
        voice.headWeights = {{
            0.18f + settings.body * 0.34f,
            0.12f + settings.body * 0.12f,
            settings.ring * (0.08f + spread * 0.10f),
            settings.ring * (0.05f + spread * 0.09f),
        }};
        const float shellStrike = rimStrike
            ? settings.rimLevel * (0.055f + settings.rimCharacter * 0.16f)
            : 0.72f + settings.body * 0.28f;
        for (uint32_t mode = 0u; mode < kHeadModeCount; ++mode) {
            voice.headModes[mode].configure(
                std::min(sr * 0.44f,
                    settings.frequencyHz * headRatios[mode]),
                std::max(0.004f, headDecays[mode]), sr);
            const float polarity = mode == 0u ? 1.0f
                : (voice.random.bipolar() >= 0.0f ? 1.0f : -1.0f);
            const float strike = shellStrike * polarity
                * (0.90f + voice.random.unipolar() * 0.10f);
            if (rimStrike) {
                // A finite rim collision injects velocity into the shell; it
                // does not teleport every shell mode to a shared displacement.
                voice.headModes[mode].strike(0.0f, -strike);
            } else {
                voice.headModes[mode].strike(strike,
                    strike * voice.random.bipolar() * 0.18f);
            }
        }

        const float pitchSeconds = settings.pitchSweepMs * 0.001f;
        voice.pitchEnvelope.configure(pitchSeconds, sr, 0.01f);
        voice.pitchEnvelope.trigger();
        voice.bodyEnvelope.configure(rimStrike
                ? std::max(0.006f, shellDecay * 0.72f)
                : settings.bodyDecaySeconds,
            sr);
        voice.bodyEnvelope.trigger();
        const float attackSeconds = lerp(0.025f, 0.002f, settings.punch);
        voice.attackEnvelope.configure(attackSeconds, sr);
        voice.attackEnvelope.trigger();

        // Rim Character changes topology as well as brightness. Dark values
        // emphasize a compact electronic knock, the middle emphasizes hollow
        // wood-like ratios, and high values add short bright/noisy rim energy.
        const float character = settings.rimCharacter;
        const float darkBase = 300.0f + settings.frequencyHz * 3.2f;
        const float woodBase = 650.0f + settings.frequencyHz * 2.6f;
        const float brightBase = 1350.0f + settings.frequencyHz * 3.8f;
        const float rimBase = character < 0.5f
            ? lerp(darkBase, woodBase, character * 2.0f)
            : lerp(woodBase, brightBase, (character - 0.5f) * 2.0f);
        const std::array<float, kRimModeCount> rimRatios {{
            1.0f,
            1.44f + character * 0.25f,
            2.12f + character * 0.70f,
        }};
        const float rimResonanceScale = lerp(3.20f, 2.20f, character);
        const std::array<float, kRimModeCount> rimDecays {{
            settings.rimDecaySeconds * rimResonanceScale,
            settings.rimDecaySeconds * rimResonanceScale
                * lerp(0.90f, 0.68f, character),
            settings.rimDecaySeconds * rimResonanceScale
                * lerp(0.72f, 0.42f, character),
        }};
        voice.rimWeights = {{
            lerp(0.68f, 0.24f, character),
            0.20f + 0.24f * (1.0f - std::abs(character * 2.0f - 1.0f)),
            0.08f + character * 0.30f,
        }};
        for (uint32_t mode = 0u; mode < kRimModeCount; ++mode) {
            voice.rimModes[mode].configure(
                std::min(sr * 0.44f, rimBase * rimRatios[mode]),
                std::max(0.004f, rimDecays[mode]), sr);
            if (rimStrike) {
                // Rim level is applied at the output exactly once. Keeping
                // excitation independent prevents quiet settings from losing
                // their resonant wood/metal body beneath the stick impulse.
                const float strike = (0.68f
                        + voice.random.unipolar() * 0.18f)
                    * (mode == 0u ? 1.0f
                        : (voice.random.bipolar() >= 0.0f ? 1.0f : -1.0f));
                // Pure quadrature is the velocity state of the rim collision.
                // It prevents tiny real-state offsets from summing into one
                // coherent, clip-like leading lobe across all rim modes.
                voice.rimModes[mode].strike(0.0f, -strike);
            }
        }
        voice.rimEnvelope.configure(settings.rimDecaySeconds, sr);
        voice.rimEnvelope.reset(rimStrike ? 1.0f : 0.0f);
        voice.stickEnvelope.configure(
            settings.stickDecayMs * 0.001f * 1.80f, sr);
        voice.stickEnvelope.reset(rimStrike ? 1.0f : 0.0f);
        if (rimStrike) {
            const float brightness = (settings.rimCharacter
                + settings.stickTone) * 0.5f;
            const float contactSeconds = lerp(
                0.00180f, 0.00110f, brightness);
            voice.contactPhase = 0.0f;
            voice.contactIncrement = 1.0f
                / std::max(1.0f, contactSeconds * sr);
        } else {
            // A membrane hit develops over a finite contact interval. The
            // complete voice is revealed by this ramp so randomized modal
            // polarity can never become a sample-zero displacement step.
            const float contactShape = std::pow(settings.punch, 1.20f);
            const float contactSeconds = lerp(
                0.00300f, 0.00075f, contactShape);
            voice.contactPhase = 0.0f;
            voice.contactIncrement = 1.0f
                / std::max(1.0f, contactSeconds * sr);
        }

        const float headHighpassHz = lerp(70.0f, 520.0f,
            settings.damping);
        const float dampingLowpassHz = lerp(9200.0f, 1800.0f,
            settings.damping);
        const float punchLowpassHz = lerp(1700.0f, 9600.0f,
            std::pow(settings.punch, 1.35f));
        const float headLowpassHz = std::min(
            dampingLowpassHz, punchLowpassHz)
            * lerp(0.72f, 1.10f, voice.velocityBrightness);
        voice.attackHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            headHighpassHz, sr);
        voice.attackLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            std::max(headHighpassHz * 1.3f, headLowpassHz), sr);

        const float stickHighpassHz = lerp(180.0f, 1400.0f,
            settings.stickTone);
        const float stickLowpassHz = lerp(3200.0f, 9000.0f,
            settings.stickTone) * lerp(0.76f, 1.08f,
                voice.velocityBrightness);
        voice.stickHighpassCoefficient = drumOnePoleFrequencyCoefficient(
            stickHighpassHz, sr);
        voice.stickLowpassCoefficient = drumOnePoleFrequencyCoefficient(
            std::max(stickHighpassHz * 1.3f, stickLowpassHz), sr);
        voice.stickFrequencyHz = std::min(sr * 0.42f,
            560.0f * std::pow(6.5f, settings.stickTone));

        voice.maximumAgeSamples = drumLongestTailSamples(sampleRate_, {
            drumTomVoiceTailSeconds(settings, articulation, sampleRate_),
        }, 0.05, 40.0);
    }

    static float voiceActivitySquared(const Voice& voice)
    {
        float modal = 0.0f;
        for (uint32_t mode = 0u; mode < kHeadModeCount; ++mode) {
            modal += voice.headModes[mode].magnitudeSquared()
                * voice.headWeights[mode] * voice.headWeights[mode];
        }
        float rim = 0.0f;
        for (uint32_t mode = 0u; mode < kRimModeCount; ++mode) {
            rim += voice.rimModes[mode].magnitudeSquared()
                * voice.rimWeights[mode] * voice.rimWeights[mode];
        }
        const float body = voice.bodyEnvelope.value() * voice.bodyLevel
            * voice.velocityGain;
        const float attack = voice.attackEnvelope.value()
            * voice.punchLevel * voice.velocityGain;
        const float rimNoise = voice.rimEnvelope.value() * voice.rimLevel
            * voice.velocityGain;
        const float stick = voice.stickEnvelope.value() * voice.stickLevel
            * voice.velocityGain;
        return modal + rim + body * body + attack * attack
            + rimNoise * rimNoise + stick * stick;
    }

    void processVoice(Voice& voice, float& mid, float& side)
    {
        const float sr = static_cast<float>(sampleRate_);
        const bool rimStrike =
            voice.articulation == DrumTomArticulation::RimStick;
        const float noiseMid = voice.random.bipolar();
        const float noiseSide = (voice.random.bipolar()
            - voice.random.bipolar()) * 0.5f;

        float shell = 0.0f;
        float upperShell = 0.0f;
        for (uint32_t mode = 0u; mode < kHeadModeCount; ++mode) {
            const float value = voice.headModes[mode].process()
                * voice.headWeights[mode];
            shell += value;
            if (mode > 0u) upperShell += value;
        }
        shell *= voice.velocityGain;
        upperShell *= voice.velocityGain;

        const float pitchEnvelope = voice.pitchEnvelope.process();
        const float pitchAmount = rimStrike
            ? voice.pitchDropSemitones * 0.12f
            : voice.pitchDropSemitones;
        const float bodyFrequency = clamp(voice.finalFrequencyHz
                * std::exp2(pitchAmount * pitchEnvelope / 12.0f),
            18.0f, sr * 0.40f);
        voice.bodyPhase += bodyFrequency / sr;
        voice.bodyPhase -= std::floor(voice.bodyPhase);
        const float bodyEnvelope = voice.bodyEnvelope.process();
        const float bodyScale = rimStrike
            ? 0.025f + voice.rimCharacter * 0.075f
            : 0.22f + voice.bodyLevel * 0.30f;
        const float body = std::sin(2.0f * kPi * voice.bodyPhase)
            * bodyEnvelope * bodyScale * voice.velocityGain;

        voice.attackMidLow += (noiseMid - voice.attackMidLow)
            * voice.attackHighpassCoefficient;
        const float attackMidHigh = noiseMid - voice.attackMidLow;
        voice.attackMidBand += (attackMidHigh - voice.attackMidBand)
            * voice.attackLowpassCoefficient;
        voice.attackMidSmooth += (voice.attackMidBand
                - voice.attackMidSmooth)
            * voice.attackLowpassCoefficient;
        voice.attackSideLow += (noiseSide - voice.attackSideLow)
            * voice.attackHighpassCoefficient;
        const float attackSideHigh = noiseSide - voice.attackSideLow;
        voice.attackSideBand += (attackSideHigh - voice.attackSideBand)
            * voice.attackLowpassCoefficient;
        voice.attackSideSmooth += (voice.attackSideBand
                - voice.attackSideSmooth)
            * voice.attackLowpassCoefficient;
        const float attackEnvelope = voice.attackEnvelope.process();
        // RimStick already has a dedicated filtered contact path. Sharing the
        // head's broadband burst only adds an unrelated click. Punch now owns
        // the head burst completely, including a genuinely silent zero.
        const float attackGain = rimStrike ? 0.0f
            : voice.punchLevel * voice.punchLevel * 0.30f
                * voice.velocityBrightness;
        const float attack = voice.attackMidSmooth * attackEnvelope
            * attackGain * voice.velocityGain;
        const float attackSide = voice.attackSideSmooth * attackEnvelope
            * attackGain * voice.velocityGain * 0.32f;

        float rimModes = 0.0f;
        for (uint32_t mode = 0u; mode < kRimModeCount; ++mode) {
            rimModes += voice.rimModes[mode].process()
                * voice.rimWeights[mode];
        }
        rimModes *= voice.rimLevel * voice.velocityGain;
        const float rimEnvelope = voice.rimEnvelope.process();

        voice.stickMidLow += (noiseMid - voice.stickMidLow)
            * voice.stickHighpassCoefficient;
        const float stickMidHigh = noiseMid - voice.stickMidLow;
        voice.stickMidBand += (stickMidHigh - voice.stickMidBand)
            * voice.stickLowpassCoefficient;
        voice.stickMidSmooth += (voice.stickMidBand
                - voice.stickMidSmooth)
            * voice.stickLowpassCoefficient;
        voice.stickSideLow += (noiseSide - voice.stickSideLow)
            * voice.stickHighpassCoefficient;
        const float stickSideHigh = noiseSide - voice.stickSideLow;
        voice.stickSideBand += (stickSideHigh - voice.stickSideBand)
            * voice.stickLowpassCoefficient;
        voice.stickSideSmooth += (voice.stickSideBand
                - voice.stickSideSmooth)
            * voice.stickLowpassCoefficient;
        voice.stickPhase += voice.stickFrequencyHz / sr;
        voice.stickPhase -= std::floor(voice.stickPhase);
        // Sine begins the tonal stick at zero displacement. The contact ramp
        // can then reveal it without the cosine's initial pressure step.
        const float stickOscillator = std::sin(
            2.0f * kPi * voice.stickPhase);
        const float stickEnvelope = voice.stickEnvelope.process();
        const float noisyAmount = 0.08f + voice.rimCharacter * 0.42f;
        const float stickMid = (voice.stickMidSmooth * noisyAmount
                + stickOscillator * (1.0f - noisyAmount) * 0.55f)
            * stickEnvelope * voice.stickLevel * voice.velocityGain
            * voice.velocityBrightness;
        const float stickSide = voice.stickSideSmooth * stickEnvelope
            * voice.stickLevel * voice.velocityGain
            * voice.velocityBrightness * (0.22f + voice.rimCharacter * 0.38f)
            * 0.55f;
        const float rimNoise = voice.stickMidSmooth * rimEnvelope
            * voice.rimLevel * voice.rimCharacter * 0.018f
            * voice.velocityGain;

        const float detail = attack + rimModes + rimNoise + stickMid;
        float contactGain = 1.0f;
        if (voice.contactPhase < 1.0f) {
            const float phase = voice.contactPhase;
            contactGain = phase * phase * (3.0f - 2.0f * phase);
            voice.contactPhase = std::min(
                1.0f, phase + voice.contactIncrement);
        }
        mid += (shell + body + detail) * contactGain;
        side += (attackSide + stickSide
            + voice.stereoPosition
                * (upperShell * 0.22f + detail * 0.48f)) * contactGain;

        ++voice.ageSamples;
        if (voiceActivitySquared(voice) < 1.0e-12f
            || voice.ageSamples >= voice.maximumAgeSamples) {
            voice.active = false;
        }
    }

    uint32_t countActiveVoices() const
    {
        uint32_t count = 0u;
        for (const Voice& voice : voices_) {
            if (voice.active) ++count;
        }
        return count;
    }

    double sampleRate_ = 48000.0;
    std::array<Voice, VoiceCount> voices_ {};
    uint64_t triggerCounter_ = 0u;
    uint32_t activeVoiceCount_ = 0u;
};

} // namespace s3g
