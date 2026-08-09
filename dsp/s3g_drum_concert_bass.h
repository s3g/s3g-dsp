#pragma once

#include "s3g_drum_character.h"
#include "s3g_drum_primitives.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

enum class DrumConcertBassArticulation : uint32_t {
    Center = 0u,
    Muted = 1u,
    Rim = 2u,
};

inline DrumConcertBassArticulation drumConcertBassArticulationForMidi(
    int midiNote)
{
    if (midiNote == 35) return DrumConcertBassArticulation::Muted;
    if (midiNote == 37) return DrumConcertBassArticulation::Rim;
    return DrumConcertBassArticulation::Center;
}

inline int drumConcertBassTrackedMidiNote(
    DrumConcertBassArticulation articulation, int midiNote)
{
    return articulation == DrumConcertBassArticulation::Center
        ? std::max(0, std::min(127, midiNote)) : 36;
}

// The first eighteen controls are resolved and latched for each strike.
// Character, width, and output trim remain live; velocity response is read
// when the next strike begins. Field order is the established CLAP/state ABI.
struct DrumConcertBassParams {
    float tuneHz = 44.0f;
    float noteTracking = 0.50f;
    float size = 0.56f;
    float headTension = 0.45f;
    float strikePosition = 0.20f;
    float beaterHardness = 0.34f;
    float impact = 0.58f;
    float body = 0.88f;
    float bodyDecaySeconds = 3.20f;
    float damping = 0.22f;
    float bloom = 0.58f;
    float air = 0.62f;
    float shell = 0.28f;
    float shellTone = 0.38f;
    float mutedDecaySeconds = 0.32f;
    float rimLevel = 0.55f;
    float rimTone = 0.35f;
    float rimDecaySeconds = 0.14f;
    DrumCharacterParams character {};
    float stereoWidth = 0.16f;
    float velocitySensitivity = 0.90f;
    float outputGainDb = -6.0f;
};

namespace drum_concert_bass_detail {

inline float resolvedFrequency(const DrumConcertBassParams& params,
    DrumConcertBassArticulation articulation, int midiNote)
{
    const int trackedNote = drumConcertBassTrackedMidiNote(
        articulation, midiNote);
    const float semitones = static_cast<float>(trackedNote - 36)
        * params.noteTracking;
    return params.tuneHz * std::exp2(semitones / 12.0f);
}

// A difference of two one-pole pressure responses forms a broad, smooth band.
// Its exact white-noise variance is calculated at configuration time, making
// each band approximately unit RMS regardless of cutoff or host sample rate.
class NormalizedPressureBand {
public:
    void configure(float lowHz, float highHz, float sampleRate)
    {
        sampleRate = std::max(1.0f, drumFiniteOr(sampleRate, 48000.0f));
        lowHz = clamp(drumFiniteOr(lowHz, 40.0f), 2.0f,
            sampleRate * 0.40f);
        highHz = clamp(drumFiniteOr(highHz, 240.0f),
            lowHz * 1.05f, sampleRate * 0.45f);
        lowCoefficient_ = drumOnePoleFrequencyCoefficient(lowHz, sampleRate);
        highCoefficient_ = drumOnePoleFrequencyCoefficient(highHz,
            sampleRate);

        const double c0 = lowCoefficient_;
        const double c1 = highCoefficient_;
        const double r0 = 1.0 - c0;
        const double r1 = 1.0 - c1;
        const auto squaredCascadeEnergy = [](double coefficient,
                                                 double radius) {
            const double x = radius * radius;
            return std::pow(coefficient, 4.0) * (1.0 + x)
                / std::max(1.0e-18, std::pow(1.0 - x, 3.0));
        };
        const double crossX = r0 * r1;
        const double crossEnergy = c0 * c0 * c1 * c1
            * (1.0 + crossX)
            / std::max(1.0e-18, std::pow(1.0 - crossX, 3.0));
        const double variance = squaredCascadeEnergy(c0, r0)
            + squaredCascadeEnergy(c1, r1) - 2.0 * crossEnergy;
        // DrumRandom is uniform on [-1, 1], with variance 1/3.
        normalization_ = static_cast<float>(std::sqrt(3.0
            / std::max(variance, 1.0e-12))
            * std::sqrt(48000.0 / static_cast<double>(sampleRate)));
        reset();
    }

    void reset()
    {
        lowStateOne_ = 0.0f;
        lowStateTwo_ = 0.0f;
        highStateOne_ = 0.0f;
        highStateTwo_ = 0.0f;
    }

    float process(float input)
    {
        input = drumFiniteOr(input, 0.0f);
        lowStateOne_ = flushDenormal(lowStateOne_
            + (input - lowStateOne_) * lowCoefficient_);
        lowStateTwo_ = flushDenormal(lowStateTwo_
            + (lowStateOne_ - lowStateTwo_) * lowCoefficient_);
        highStateOne_ = flushDenormal(highStateOne_
            + (input - highStateOne_) * highCoefficient_);
        highStateTwo_ = flushDenormal(highStateTwo_
            + (highStateOne_ - highStateTwo_) * highCoefficient_);
        const float output = (highStateTwo_ - lowStateTwo_)
            * normalization_;
        if (!std::isfinite(output) || std::abs(output) > 32.0f) {
            reset();
            return 0.0f;
        }
        return output;
    }

private:
    float lowStateOne_ = 0.0f;
    float lowStateTwo_ = 0.0f;
    float highStateOne_ = 0.0f;
    float highStateTwo_ = 0.0f;
    float lowCoefficient_ = 0.005f;
    float highCoefficient_ = 0.05f;
    float normalization_ = 1.0f;
};

// A restrained pitch anchor. The drum's audible mass comes from the pressure
// bands below; these freely decaying modes provide identity without turning
// the result into a sparse chord of long-lived sinusoids.
class TonalAnchor {
public:
    void configure(float restingFrequencyHz, float strikeFrequencyScale,
        float decaySeconds, float glideSeconds, float amplitude,
        float sampleRate)
    {
        sampleRate = std::max(1.0f, drumFiniteOr(sampleRate, 48000.0f));
        restingFrequencyHz = clamp(drumFiniteOr(restingFrequencyHz, 80.0f),
            1.0f, sampleRate * 0.45f);
        strikeFrequencyScale = clamp(drumFiniteOr(strikeFrequencyScale, 1.0f),
            1.0f, 1.25f);
        const float strikeFrequency = clamp(restingFrequencyHz
            * strikeFrequencyScale, 1.0f, sampleRate * 0.45f);
        const float radius = drumDecayMultiplier(decaySeconds, sampleRate);
        const float restingAngle = 2.0f * kPi * restingFrequencyHz
            / sampleRate;
        const float strikeAngle = 2.0f * kPi * strikeFrequency / sampleRate;
        targetReal_ = radius * std::cos(restingAngle);
        targetImaginary_ = radius * std::sin(restingAngle);
        rotationReal_ = radius * std::cos(strikeAngle);
        rotationImaginary_ = radius * std::sin(strikeAngle);
        glideCoefficient_ = drumOnePoleTimeCoefficient(glideSeconds,
            sampleRate);
        real_ = 0.0f;
        imaginary_ = -drumFiniteOr(amplitude, 0.0f);
    }

    void reset()
    {
        real_ = 0.0f;
        imaginary_ = 0.0f;
    }

    float process()
    {
        rotationReal_ += (targetReal_ - rotationReal_) * glideCoefficient_;
        rotationImaginary_ += (targetImaginary_ - rotationImaginary_)
            * glideCoefficient_;
        const float output = real_;
        const float nextReal = real_ * rotationReal_
            - imaginary_ * rotationImaginary_;
        const float nextImaginary = real_ * rotationImaginary_
            + imaginary_ * rotationReal_;
        if (!std::isfinite(nextReal) || !std::isfinite(nextImaginary)
            || std::abs(nextReal) > 32.0f
            || std::abs(nextImaginary) > 32.0f) {
            reset();
            return 0.0f;
        }
        real_ = flushDenormal(nextReal);
        imaginary_ = flushDenormal(nextImaginary);
        return output;
    }

    bool active(float threshold = 1.0e-6f) const
    {
        return real_ * real_ + imaginary_ * imaginary_
            > threshold * threshold;
    }

private:
    float real_ = 0.0f;
    float imaginary_ = 0.0f;
    float rotationReal_ = 0.999f;
    float rotationImaginary_ = 0.0f;
    float targetReal_ = 0.999f;
    float targetImaginary_ = 0.0f;
    float glideCoefficient_ = 0.0001f;
};

struct Voice {
    static constexpr uint32_t kTonalModes = 6u;
    static constexpr uint32_t kRimModes = 4u;

    void prepare(double sampleRate)
    {
        sampleRate_ = static_cast<float>(drumSafeSampleRate(sampleRate));
        reset();
    }

    void reset()
    {
        lowBand_.reset();
        bodyBand_.reset();
        skinBand_.reset();
        airBand_.reset();
        attackBand_.reset();
        sideBand_.reset();
        for (auto& mode : tonal_) mode.reset();
        for (auto& mode : rim_) mode.reset();
        lowEnvelope_.reset();
        bodyEnvelope_.reset();
        skinEnvelope_.reset();
        airEnvelope_.reset();
        attackEnvelope_.reset();
        rimEnvelope_.reset();
        random_.reset(1u);
        pressureNoiseCurrent_.fill(0.0f);
        pressureNoiseNext_.fill(0.0f);
        pressureNoisePhase_ = 0.0f;
        pressureNoiseStep_ = 1.0f;
        ageSamples_ = 0u;
        onsetSamples_ = 1u;
        active_ = false;
        serial_ = 0u;
    }

    void trigger(const DrumConcertBassParams& params,
        DrumConcertBassArticulation articulation, float velocity,
        int midiNote, uint32_t voiceIndex, uint64_t serial)
    {
        reset();
        if (!(velocity > 0.0f)) return;
        serial_ = serial;
        const float sr = sampleRate_;
        const bool muted = articulation == DrumConcertBassArticulation::Muted;
        const bool rimStrike = articulation == DrumConcertBassArticulation::Rim;
        const float fundamental = clamp(resolvedFrequency(
            params, articulation, midiNote), 20.0f, sr * 0.08f);
        const float velocityGain = lerp(1.0f,
            clamp(velocity, 0.0f, 1.0f), params.velocitySensitivity);
        amplitude_ = velocityGain;
        articulation_ = articulation;
        (void)voiceIndex;
        // These independent source seeds were screened across every factory
        // preset and articulation for high early energy and bounded crest.
        // Cycling a small palette preserves hit variation without allowing an
        // unlucky stochastic burst to consume the output headroom.
        static constexpr std::array<uint32_t, 16u> pressureSeeds {{
            1838u, 1037u, 2258u, 3830u, 992u, 774u, 1158u, 2551u,
            1179u, 1576u, 1711u, 1980u, 2125u, 3008u, 2520u, 1310u,
        }};
        random_.reset(pressureSeeds[static_cast<uint32_t>((serial - 1u)
            % pressureSeeds.size())]);
        for (float& value : pressureNoiseCurrent_) {
            value = random_.bipolar();
        }
        for (float& value : pressureNoiseNext_) {
            value = random_.bipolar();
        }
        pressureNoisePhase_ = 0.0f;
        pressureNoiseStep_ = 48000.0f / sr;

        const auto smooth = [](float value) {
            value = clamp(value, 0.0f, 1.0f);
            return value * value * (3.0f - 2.0f * value);
        };
        const float kettle = smooth((0.52f - params.size) / 0.42f);
        const float taiko = smooth((params.size - 0.58f) / 0.36f);
        float responseDecay = muted
            ? params.mutedDecaySeconds * lerp(1.0f, 0.65f, params.damping)
            : params.bodyDecaySeconds * lerp(1.0f, 0.20f, params.damping);
        if (rimStrike) responseDecay = params.rimDecaySeconds;

        const float toneWarp = lerp(0.92f, 1.12f, params.shellTone);
        const float formWidth = lerp(1.0f, 1.22f, taiko);
        lowBand_.configure(fundamental * lerp(0.54f, 0.68f, kettle),
            fundamental * lerp(1.42f, 1.72f, taiko), sr);
        bodyBand_.configure(fundamental * lerp(1.05f, 1.20f, kettle),
            fundamental * lerp(4.6f, 6.2f, taiko) * toneWarp, sr);
        skinBand_.configure(fundamental * lerp(2.9f, 3.7f,
                params.headTension),
            fundamental * lerp(10.5f, 15.0f,
                params.strikePosition) * formWidth * toneWarp, sr);
        airBand_.configure(fundamental * lerp(4.5f, 6.0f, params.air),
            fundamental * lerp(14.0f, 24.0f,
                params.beaterHardness) * toneWarp, sr);
        attackBand_.configure(lerp(120.0f, 420.0f,
                params.beaterHardness),
            lerp(1250.0f, 7600.0f,
                params.beaterHardness * params.beaterHardness), sr);
        sideBand_.configure(fundamental * 1.35f,
            fundamental * lerp(7.0f, 15.0f,
                params.strikePosition) * toneWarp, sr);

        lowEnvelope_.configure(std::max(0.035f, responseDecay
            * lerp(0.84f, 1.18f, params.bloom)), sr);
        bodyEnvelope_.configure(std::max(0.030f, responseDecay
            * lerp(0.34f, 0.66f, params.bloom)), sr);
        skinEnvelope_.configure(std::max(0.022f, responseDecay
            * lerp(0.045f, 0.12f, params.bloom)), sr);
        airEnvelope_.configure(std::max(0.020f, responseDecay
            * lerp(0.025f, 0.075f, params.air)), sr);
        attackEnvelope_.configure(rimStrike
                ? lerp(0.025f, 0.075f, params.rimDecaySeconds)
                : lerp(0.080f, 0.028f,
                    params.beaterHardness * params.beaterHardness),
            sr);
        rimEnvelope_.configure(params.rimDecaySeconds, sr);
        lowEnvelope_.trigger();
        bodyEnvelope_.trigger();
        skinEnvelope_.trigger();
        airEnvelope_.trigger();
        attackEnvelope_.trigger();
        if (rimStrike) rimEnvelope_.trigger();

        static constexpr std::array<float, kTonalModes> openRatios {{
            1.000f, 1.593f, 2.136f, 2.653f, 3.155f, 3.598f,
        }};
        static constexpr std::array<float, kTonalModes> kettleRatios {{
            1.000f, 1.500f, 2.000f, 2.440f, 2.900f, 3.380f,
        }};
        static constexpr std::array<float, kTonalModes> taikoRatios {{
            1.000f, 1.540f, 2.040f, 2.580f, 3.160f, 3.700f,
        }};
        static constexpr std::array<float, kTonalModes> tonalWeights {{
            0.72f, 1.0f, 0.78f, 0.55f, 0.38f, 0.27f,
        }};
        const float riseCents = (rimStrike ? 0.16f : 1.0f)
            * lerp(30.0f, 118.0f, params.impact) * velocity * velocity;
        const float glideSeconds = lerp(0.14f, 0.64f, params.bloom);
        float tonalEnergy = 0.0f;
        for (uint32_t index = 0u; index < kTonalModes; ++index) {
            const float order = static_cast<float>(index)
                / static_cast<float>(kTonalModes - 1u);
            float ratio = lerp(openRatios[index], kettleRatios[index],
                kettle);
            ratio = lerp(ratio, taikoRatios[index], taiko);
            ratio *= 1.0f + (params.headTension - 0.5f) * order * 0.045f;
            const float centerWeight = 1.0f / (1.0f + order * 1.5f);
            const float edgeWeight = 0.62f + std::sqrt(order) * 0.76f;
            const float weight = tonalWeights[index] * lerp(centerWeight,
                edgeWeight, params.strikePosition);
            tonalEnergy += weight * weight;
            tonal_[index].configure(fundamental * ratio,
                std::exp2(riseCents * lerp(1.0f, 0.45f, order) / 1200.0f),
                std::max(0.025f, responseDecay * lerp(0.82f, 0.28f, order)),
                glideSeconds, weight, sr);
        }
        tonalNormalization_ = 1.0f / std::sqrt(std::max(0.01f,
            tonalEnergy * 0.5f));

        const float rimBase = lerp(260.0f, 1360.0f,
            params.rimTone * params.rimTone);
        static constexpr std::array<float, kRimModes> rimRatios {{
            1.0f, 1.46f, 2.18f, 3.24f,
        }};
        static constexpr std::array<float, kRimModes> rimWeights {{
            1.0f, 0.68f, 0.40f, 0.24f,
        }};
        float rimEnergy = 0.0f;
        for (uint32_t index = 0u; index < kRimModes; ++index) {
            const float weight = rimStrike ? rimWeights[index] : 0.0f;
            rimEnergy += weight * weight;
            rim_[index].configure(rimBase * rimRatios[index], 1.0f,
                std::max(0.012f, params.rimDecaySeconds
                    * lerp(0.92f, 0.32f,
                        static_cast<float>(index)
                            / static_cast<float>(kRimModes - 1u))),
                0.02f, weight, sr);
        }
        rimNormalization_ = 1.0f / std::sqrt(std::max(0.01f,
            rimEnergy * 0.5f));

        const float headLevel = rimStrike
            ? 0.11f : lerp(0.12f, 1.0f, params.body);
        lowGain_ = headLevel * lerp(0.50f, 0.78f, params.bloom)
            * (rimStrike ? 0.18f : 1.0f);
        bodyGain_ = headLevel * lerp(0.62f, 0.94f, params.impact)
            * lerp(0.90f, 1.08f, params.shell);
        skinGain_ = headLevel * lerp(0.07f, 0.20f,
                params.beaterHardness)
            * lerp(0.82f, 1.18f, params.strikePosition);
        airGain_ = headLevel * params.air * lerp(0.04f, 0.12f,
            params.bloom);
        attackGain_ = lerp(0.07f, 0.25f, params.impact)
            * lerp(0.55f, 1.0f, params.beaterHardness);
        tonalGain_ = headLevel * (lerp(0.12f, 0.42f, kettle)
            + taiko * 0.06f) * lerp(0.92f, 1.08f,
                params.headTension);
        rimGain_ = rimStrike ? lerp(0.46f, 0.92f, params.rimLevel) : 0.0f;
        sideGain_ = headLevel * lerp(0.08f, 0.20f,
            params.strikePosition) * lerp(0.72f, 1.0f, params.air);
        if (muted) {
            lowGain_ *= 0.72f;
            bodyGain_ *= 1.06f;
            skinGain_ *= 1.12f;
        }
        if (rimStrike) {
            bodyGain_ *= 0.48f;
            skinGain_ *= 0.72f;
            attackGain_ *= lerp(1.0f, 1.38f, params.rimTone);
            tonalGain_ *= 0.42f;
        }

        const float onsetMs = rimStrike
            ? lerp(2.2f, 0.65f, params.rimTone)
            : (muted ? lerp(2.8f, 0.75f,
                    params.beaterHardness * params.beaterHardness)
                : lerp(4.6f, 0.85f,
                    params.beaterHardness * params.beaterHardness));
        onsetSamples_ = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::round(onsetMs * 0.001f * sr)));
        voiceGain_ = 0.86f * lerp(0.90f, 1.06f, params.impact);
        active_ = true;
    }

    void process(float& mid, float& side)
    {
        if (!active_) return;
        const float onsetProgress = std::min(1.0f,
            static_cast<float>(ageSamples_)
                / static_cast<float>(onsetSamples_));
        const float onsetSine = std::sin(0.5f * kPi * onsetProgress);
        const float onset = onsetSine * onsetSine;
        ++ageSamples_;

        const float lowEnvelope = lowEnvelope_.process();
        const float bodyEnvelope = bodyEnvelope_.process();
        const float skinEnvelope = skinEnvelope_.process();
        const float airEnvelope = airEnvelope_.process();
        const float attackEnvelope = attackEnvelope_.process();
        const float rimEnvelope = rimEnvelope_.process();
        const auto pressureNoise = nextPressureNoiseFrame();
        const float low = lowBand_.process(pressureNoise[0u]) * lowEnvelope;
        const float body = bodyBand_.process(pressureNoise[1u]) * bodyEnvelope;
        const float skin = skinBand_.process(pressureNoise[2u]) * skinEnvelope;
        const float air = airBand_.process(pressureNoise[3u]) * airEnvelope;
        const float attack = attackBand_.process(pressureNoise[4u])
            * attackEnvelope;
        const float spatial = sideBand_.process(pressureNoise[5u])
            * bodyEnvelope;

        float tonal = 0.0f;
        float tonalSide = 0.0f;
        for (uint32_t index = 0u; index < kTonalModes; ++index) {
            const float value = tonal_[index].process();
            tonal += value;
            if (index > 1u) tonalSide += value
                * ((index & 1u) ? 0.11f : -0.11f);
        }
        float rim = 0.0f;
        float rimSide = 0.0f;
        for (uint32_t index = 0u; index < kRimModes; ++index) {
            const float value = rim_[index].process() * rimEnvelope;
            rim += value;
            rimSide += value * ((index & 1u) ? 0.18f : -0.18f);
        }

        const float pressureMid = low * lowGain_ + body * bodyGain_
            + skin * skinGain_ + air * airGain_ + attack * attackGain_
            + tonal * tonalNormalization_ * tonalGain_
            + rim * rimNormalization_ * rimGain_;
        const float pressureSide = spatial * sideGain_
            + tonalSide * tonalNormalization_ * tonalGain_
            + rimSide * rimNormalization_ * rimGain_;
        mid += amplitude_ * voiceGain_ * pressureMid * onset;
        side += amplitude_ * voiceGain_ * pressureSide * onset;

        if (ageSamples_ >= onsetSamples_ && !pressureActive()) active_ = false;
    }

    bool active() const { return active_; }
    uint64_t serial() const { return serial_; }

private:
    std::array<float, 6u> nextPressureNoiseFrame()
    {
        std::array<float, 6u> frame {};
        for (uint32_t index = 0u; index < frame.size(); ++index) {
            frame[index] = lerp(pressureNoiseCurrent_[index],
                pressureNoiseNext_[index], pressureNoisePhase_);
        }
        pressureNoisePhase_ += pressureNoiseStep_;
        while (pressureNoisePhase_ >= 1.0f) {
            pressureNoiseCurrent_ = pressureNoiseNext_;
            for (float& value : pressureNoiseNext_) {
                value = random_.bipolar();
            }
            pressureNoisePhase_ -= 1.0f;
        }
        return frame;
    }

    bool pressureActive() const
    {
        if (lowEnvelope_.active(2.0e-6f)
            || bodyEnvelope_.active(2.0e-6f)
            || skinEnvelope_.active(2.0e-6f)
            || airEnvelope_.active(2.0e-6f)
            || attackEnvelope_.active(2.0e-6f)
            || rimEnvelope_.active(2.0e-6f)) return true;
        for (const auto& mode : tonal_) if (mode.active(2.0e-6f)) return true;
        for (const auto& mode : rim_) if (mode.active(2.0e-6f)) return true;
        return false;
    }

    float sampleRate_ = 48000.0f;
    NormalizedPressureBand lowBand_ {};
    NormalizedPressureBand bodyBand_ {};
    NormalizedPressureBand skinBand_ {};
    NormalizedPressureBand airBand_ {};
    NormalizedPressureBand attackBand_ {};
    NormalizedPressureBand sideBand_ {};
    std::array<TonalAnchor, kTonalModes> tonal_ {};
    std::array<TonalAnchor, kRimModes> rim_ {};
    DrumExponentialEnvelope lowEnvelope_ {};
    DrumExponentialEnvelope bodyEnvelope_ {};
    DrumExponentialEnvelope skinEnvelope_ {};
    DrumExponentialEnvelope airEnvelope_ {};
    DrumExponentialEnvelope attackEnvelope_ {};
    DrumExponentialEnvelope rimEnvelope_ {};
    DrumRandom random_ {};
    std::array<float, 6u> pressureNoiseCurrent_ {};
    std::array<float, 6u> pressureNoiseNext_ {};
    DrumConcertBassArticulation articulation_ =
        DrumConcertBassArticulation::Center;
    uint32_t ageSamples_ = 0u;
    uint32_t onsetSamples_ = 1u;
    float pressureNoisePhase_ = 0.0f;
    float pressureNoiseStep_ = 1.0f;
    float amplitude_ = 1.0f;
    float lowGain_ = 0.6f;
    float bodyGain_ = 0.8f;
    float skinGain_ = 0.14f;
    float airGain_ = 0.07f;
    float attackGain_ = 0.14f;
    float tonalGain_ = 0.15f;
    float rimGain_ = 0.0f;
    float sideGain_ = 0.1f;
    float tonalNormalization_ = 1.0f;
    float rimNormalization_ = 1.0f;
    float voiceGain_ = 0.86f;
    bool active_ = false;
    uint64_t serial_ = 0u;
};

} // namespace drum_concert_bass_detail

inline double drumConcertBassTailSeconds(const DrumConcertBassParams& params,
    double sampleRate = 48000.0)
{
    (void)sampleRate;
    const float damping = clamp(drumFiniteOr(params.damping, 0.28f),
        0.0f, 1.0f);
    const double body = clamp(drumFiniteOr(params.bodyDecaySeconds, 2.20f),
        0.15f, 8.0f) * lerp(1.0f, 0.20f, damping);
    const double bloom = body * lerp(0.84f, 1.18f,
        clamp(drumFiniteOr(params.bloom, 0.62f), 0.0f, 1.0f));
    const double muted = clamp(drumFiniteOr(
        params.mutedDecaySeconds, 0.32f), 0.04f, 1.5f)
        * lerp(1.0f, 0.65f, damping);
    const double rim = clamp(drumFiniteOr(params.rimDecaySeconds, 0.14f),
        0.015f, 1.0f);
    const double response = std::max({ body, bloom, muted, rim })
        * 2.05 + 0.02;
    return std::min(40.0, std::max(response,
        params.character.drive > 1.0e-6f ? 0.50 : 0.0));
}

class DrumConcertBass {
public:
    static constexpr uint32_t kVoiceCount = 12u;

    void prepare(double sampleRate)
    {
        sampleRate_ = drumSafeSampleRate(sampleRate);
        for (auto& voice : voices_) voice.prepare(sampleRate_);
        character_.prepare(sampleRate_);
        smoothingCoefficient_ = drumOnePoleTimeCoefficient(
            0.005f, static_cast<float>(sampleRate_));
        limiterReleaseCoefficient_ = drumOnePoleTimeCoefficient(
            0.090f, static_cast<float>(sampleRate_));
        subsonicCoefficient_ = std::exp(-2.0f * kPi * 23.0f
            / static_cast<float>(sampleRate_));
        reset();
    }

    void reset()
    {
        for (auto& voice : voices_) voice.reset();
        triggerSerial_ = 0u;
        smoothedWidth_ = params_.stereoWidth;
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        smoothedOutputGain_ = outputGainTarget_;
        limiterGain_ = 1.0f;
        subsonicLeft_ = {};
        subsonicRight_ = {};
        character_.reset();
    }

    void setParams(DrumConcertBassParams params)
    {
        const bool wasInactive = !active();
        params_ = sanitize(params);
        outputGainTarget_ = dbToGain(params_.outputGainDb);
        character_.setParams(params_.character);
        if (wasInactive) {
            smoothedWidth_ = params_.stereoWidth;
            smoothedOutputGain_ = outputGainTarget_;
            character_.reset();
        }
    }

    DrumConcertBassParams params() const { return params_; }

    void trigger(DrumConcertBassArticulation articulation, float velocity,
        int midiNote = 36)
    {
        if (!(velocity > 0.0f)) return;
        uint32_t selected = kVoiceCount;
        uint64_t oldest = UINT64_MAX;
        for (uint32_t index = 0u; index < kVoiceCount; ++index) {
            if (!voices_[index].active()) {
                selected = index;
                break;
            }
            if (voices_[index].serial() < oldest) {
                oldest = voices_[index].serial();
                selected = index;
            }
        }
        ++triggerSerial_;
        voices_[selected].trigger(params_, articulation,
            clamp(velocity, 0.0f, 1.0f), midiNote, selected, triggerSerial_);
    }

    void processFrame(float& left, float& right)
    {
        float mid = 0.0f;
        float side = 0.0f;
        for (auto& voice : voices_) voice.process(mid, side);
        if (params_.stereoWidth == 0.0f) {
            smoothedWidth_ = 0.0f;
        } else {
            smoothedWidth_ = flushDenormal(smoothedWidth_
                + (params_.stereoWidth - smoothedWidth_)
                    * smoothingCoefficient_);
        }
        smoothedOutputGain_ = flushDenormal(smoothedOutputGain_
            + (outputGainTarget_ - smoothedOutputGain_)
                * smoothingCoefficient_);
        float frameLeft = mid + side * smoothedWidth_;
        float frameRight = mid - side * smoothedWidth_;
        frameLeft = processSubsonic(frameLeft, subsonicLeft_);
        frameRight = processSubsonic(frameRight, subsonicRight_);
        character_.processFrame(frameLeft, frameRight);
        if (params_.stereoWidth == 0.0f) {
            const float mono = (frameLeft + frameRight) * 0.5f;
            frameLeft = mono;
            frameRight = mono;
        }
        const float preLimitedLeft = frameLeft * smoothedOutputGain_;
        const float preLimitedRight = frameRight * smoothedOutputGain_;
        const float peak = std::max(std::abs(preLimitedLeft),
            std::abs(preLimitedRight));
        // Normal strikes are designed to remain below this stage. It exists
        // only for pathological polyphony or deliberately raised output trim.
        constexpr float kSafetyCeiling = 0.96f;
        const float desiredLimiter = peak > kSafetyCeiling
            ? kSafetyCeiling / peak : 1.0f;
        if (desiredLimiter < limiterGain_) {
            limiterGain_ = desiredLimiter;
        } else {
            limiterGain_ = flushDenormal(limiterGain_
                + (desiredLimiter - limiterGain_)
                    * limiterReleaseCoefficient_);
        }
        left = drumSafeOutput(preLimitedLeft * limiterGain_);
        right = drumSafeOutput(preLimitedRight * limiterGain_);
    }

    void processBlock(float* left, float* right, uint32_t frames)
    {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            float l = 0.0f;
            float r = 0.0f;
            processFrame(l, r);
            if (left && right) {
                left[frame] = l;
                right[frame] = r;
            } else if (left) {
                left[frame] = (l + r) * 0.5f;
            } else if (right) {
                right[frame] = (l + r) * 0.5f;
            }
        }
    }

    bool active() const
    {
        for (const auto& voice : voices_) if (voice.active()) return true;
        return character_.active()
            || std::abs(subsonicLeft_.output) > 1.0e-7f
            || std::abs(subsonicRight_.output) > 1.0e-7f;
    }

private:
    struct HighpassState {
        float input = 0.0f;
        float output = 0.0f;
    };

    float processSubsonic(float input, HighpassState& state) const
    {
        const float output = flushDenormal(input - state.input
            + subsonicCoefficient_ * state.output);
        state.input = input;
        state.output = std::isfinite(output) ? output : 0.0f;
        return state.output;
    }

    static DrumConcertBassParams sanitize(DrumConcertBassParams p)
    {
        p.tuneHz = clamp(drumFiniteOr(p.tuneHz, 44.0f), 24.0f, 96.0f);
        p.noteTracking = clamp(drumFiniteOr(p.noteTracking, 0.50f), 0.0f, 1.0f);
        p.size = clamp(drumFiniteOr(p.size, 0.56f), 0.0f, 1.0f);
        p.headTension = clamp(drumFiniteOr(p.headTension, 0.45f), 0.0f, 1.0f);
        p.strikePosition = clamp(drumFiniteOr(p.strikePosition, 0.20f), 0.0f, 1.0f);
        p.beaterHardness = clamp(drumFiniteOr(p.beaterHardness, 0.34f), 0.0f, 1.0f);
        p.impact = clamp(drumFiniteOr(p.impact, 0.58f), 0.0f, 1.0f);
        p.body = clamp(drumFiniteOr(p.body, 0.88f), 0.0f, 1.0f);
        p.bodyDecaySeconds = clamp(drumFiniteOr(p.bodyDecaySeconds, 3.20f), 0.15f, 8.0f);
        p.damping = clamp(drumFiniteOr(p.damping, 0.22f), 0.0f, 1.0f);
        p.bloom = clamp(drumFiniteOr(p.bloom, 0.58f), 0.0f, 1.0f);
        p.air = clamp(drumFiniteOr(p.air, 0.62f), 0.0f, 1.0f);
        p.shell = clamp(drumFiniteOr(p.shell, 0.28f), 0.0f, 1.0f);
        p.shellTone = clamp(drumFiniteOr(p.shellTone, 0.38f), 0.0f, 1.0f);
        p.mutedDecaySeconds = clamp(drumFiniteOr(p.mutedDecaySeconds, 0.32f), 0.04f, 1.5f);
        p.rimLevel = clamp(drumFiniteOr(p.rimLevel, 0.55f), 0.0f, 1.0f);
        p.rimTone = clamp(drumFiniteOr(p.rimTone, 0.35f), 0.0f, 1.0f);
        p.rimDecaySeconds = clamp(drumFiniteOr(p.rimDecaySeconds, 0.14f), 0.015f, 1.0f);
        p.character.drive = clamp(drumFiniteOr(p.character.drive, 0.0f), 0.0f, 1.0f);
        p.character.bias = clamp(drumFiniteOr(p.character.bias, 0.0f), -1.0f, 1.0f);
        p.character.compression = clamp(drumFiniteOr(p.character.compression, 0.0f), 0.0f, 1.0f);
        p.character.sampleRateReduction = clamp(drumFiniteOr(p.character.sampleRateReduction, 0.0f), 0.0f, 1.0f);
        p.character.bitDepthReduction = clamp(drumFiniteOr(p.character.bitDepthReduction, 0.0f), 0.0f, 1.0f);
        p.character.reconstruction = clamp(drumFiniteOr(p.character.reconstruction, 0.0f), 0.0f, 1.0f);
        p.character.tone = clamp(drumFiniteOr(p.character.tone, 0.0f), -1.0f, 1.0f);
        p.stereoWidth = clamp(drumFiniteOr(p.stereoWidth, 0.16f), 0.0f, 1.0f);
        p.velocitySensitivity = clamp(drumFiniteOr(p.velocitySensitivity, 0.90f), 0.0f, 1.0f);
        p.outputGainDb = clamp(drumFiniteOr(p.outputGainDb, -6.0f), -36.0f, 12.0f);
        return p;
    }

    double sampleRate_ = 48000.0;
    DrumConcertBassParams params_ {};
    std::array<drum_concert_bass_detail::Voice, kVoiceCount> voices_ {};
    DrumCharacter character_ {};
    uint64_t triggerSerial_ = 0u;
    float smoothingCoefficient_ = 0.004f;
    float outputGainTarget_ = 0.501187f;
    float smoothedOutputGain_ = 0.501187f;
    float limiterGain_ = 1.0f;
    float limiterReleaseCoefficient_ = 0.00023f;
    float subsonicCoefficient_ = 0.9970f;
    HighpassState subsonicLeft_ {};
    HighpassState subsonicRight_ {};
    float smoothedWidth_ = 0.16f;
};

} // namespace s3g
