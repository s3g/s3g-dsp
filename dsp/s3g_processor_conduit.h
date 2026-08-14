#pragma once

#include "s3g_analog_drive_circuits.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace s3g {

enum class ProcessorConduitMaterial : uint32_t {
    Metal = 0,
    Glass,
    Plastic,
    Wood,
    Water,
    Skin,
    Direct,
    MetalVessel,
    GlassVessel,
    PlasticVessel,
    DeepBronze,
    TieredBronze,
    BroadBronze,
    BrightBronze,
    CarbonLaminate,
    GlassPlate,
    SteelShell,
    AluminumPlate,
    Porcelain,
    Earthenware,
    SprucePlate,
    TensionedSkin,
    LoadedMembrane,
    CoupledMembrane,
    CavityMembrane,
    LooseMembrane,
    Count,
};

inline constexpr uint32_t kProcessorConduitMaterialCount =
    static_cast<uint32_t>(ProcessorConduitMaterial::Count);

inline const char* processorConduitMaterialName(
    ProcessorConduitMaterial material)
{
    switch (material) {
    case ProcessorConduitMaterial::Metal: return "METAL";
    case ProcessorConduitMaterial::Glass: return "GLASS";
    case ProcessorConduitMaterial::Plastic: return "PLASTIC";
    case ProcessorConduitMaterial::Wood: return "WOOD";
    case ProcessorConduitMaterial::Water: return "WATER";
    case ProcessorConduitMaterial::Skin: return "SKIN";
    case ProcessorConduitMaterial::Direct: return "DIRECT";
    case ProcessorConduitMaterial::MetalVessel: return "METAL VESSEL";
    case ProcessorConduitMaterial::GlassVessel: return "GLASS VESSEL";
    case ProcessorConduitMaterial::PlasticVessel: return "PLASTIC VESSEL";
    case ProcessorConduitMaterial::DeepBronze: return "DEEP BRONZE";
    case ProcessorConduitMaterial::TieredBronze: return "TIERED BRONZE";
    case ProcessorConduitMaterial::BroadBronze: return "BROAD BRONZE";
    case ProcessorConduitMaterial::BrightBronze: return "BRIGHT BRONZE";
    case ProcessorConduitMaterial::CarbonLaminate: return "CARBON LAM.";
    case ProcessorConduitMaterial::GlassPlate: return "GLASS PLATE";
    case ProcessorConduitMaterial::SteelShell: return "STEEL SHELL";
    case ProcessorConduitMaterial::AluminumPlate: return "ALUM. PLATE";
    case ProcessorConduitMaterial::Porcelain: return "PORCELAIN";
    case ProcessorConduitMaterial::Earthenware: return "EARTHENWARE";
    case ProcessorConduitMaterial::SprucePlate: return "SPRUCE PLATE";
    case ProcessorConduitMaterial::TensionedSkin: return "TENSIONED SKIN";
    case ProcessorConduitMaterial::LoadedMembrane: return "LOADED MEM.";
    case ProcessorConduitMaterial::CoupledMembrane: return "COUPLED MEM.";
    case ProcessorConduitMaterial::CavityMembrane: return "CAVITY MEM.";
    case ProcessorConduitMaterial::LooseMembrane: return "LOOSE MEM.";
    case ProcessorConduitMaterial::Count: break;
    }
    return "METAL";
}

enum class ProcessorConduitPedal : uint32_t {
    Shred = 0,
    Wool,
    Rat,
    ZoneA,
    ZoneB,
    FuzzI,
    FuzzII,
    Diode,
    Count,
};

inline constexpr uint32_t kProcessorConduitPedalCount =
    static_cast<uint32_t>(ProcessorConduitPedal::Count);

inline const char* processorConduitPedalName(ProcessorConduitPedal pedal)
{
    if (pedal == ProcessorConduitPedal::Shred) return "SHRED";
    const uint32_t index = static_cast<uint32_t>(pedal);
    if (index > 0u && index < kProcessorConduitPedalCount) {
        return analogDriveCircuitName(
            static_cast<AnalogDriveCircuit>(index - 1u));
    }
    return "SHRED";
}

enum class ProcessorConduitPedalPosition : uint32_t {
    PreDriver = 0,
    PostPiezo,
    PostMic,
    Count,
};

inline constexpr uint32_t kProcessorConduitPedalPositionCount =
    static_cast<uint32_t>(ProcessorConduitPedalPosition::Count);

inline const char* processorConduitPedalPositionName(
    ProcessorConduitPedalPosition position)
{
    switch (position) {
    case ProcessorConduitPedalPosition::PreDriver: return "PRE DRIVER";
    case ProcessorConduitPedalPosition::PostPiezo: return "PIEZO > PA";
    case ProcessorConduitPedalPosition::PostMic: return "MIC > LOOP";
    case ProcessorConduitPedalPosition::Count: break;
    }
    return "PRE DRIVER";
}

enum class ProcessorConduitInputListen : uint32_t {
    Channel1 = 0,
    Channel2,
    SumMono,
    Stereo,
    Count,
};

inline constexpr uint32_t kProcessorConduitInputListenCount =
    static_cast<uint32_t>(ProcessorConduitInputListen::Count);

inline const char* processorConduitInputListenName(
    ProcessorConduitInputListen listen)
{
    switch (listen) {
    case ProcessorConduitInputListen::Channel1: return "CHANNEL 1";
    case ProcessorConduitInputListen::Channel2: return "CHANNEL 2";
    case ProcessorConduitInputListen::SumMono: return "SUM MONO";
    case ProcessorConduitInputListen::Stereo: return "STEREO";
    case ProcessorConduitInputListen::Count: break;
    }
    return "STEREO";
}

struct ProcessorConduitParams {
    ProcessorConduitMaterial material = ProcessorConduitMaterial::Metal;
    float inputGainDb = 6.0f;
    float driver = 0.45f;
    float size = 0.58f;
    float tension = 0.48f;
    float damping = 0.38f;
    float pickup = 0.72f;
    float contact = 0.58f;
    float feedback = 0.16f;
    float mix = 0.82f;
    float outputGainDb = -6.0f;
    ProcessorConduitPedal pedal = ProcessorConduitPedal::Shred;
    float pedalDrive = 0.36f;
    float pedalTone = 0.55f;
    float octaveDown = 0.0f;
    float octaveDrag = 0.68f;
    float paDrive = 0.46f;
    float micMotion = 0.32f;
    float chamber = 0.62f;
    float stereoWidth = 0.68f;
    ProcessorConduitPedalPosition pedalPosition =
        ProcessorConduitPedalPosition::PreDriver;
    float pedalMix = 0.60f;
    ProcessorConduitInputListen inputListen =
        ProcessorConduitInputListen::Stereo;
};

// One-lane dual-grain pitch shifter specialized for a fixed -12 semitone
// ratio. Larger DRAG windows trade consonant precision for the smeared,
// heavy temporal impression of slowed tape while remaining a realtime effect.
class ProcessorConduitOctaveDown {
public:
    void prepare(double sampleRate, double maximumWindowSeconds = 0.30)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        const auto size = static_cast<size_t>(std::ceil(sampleRate_
            * std::max(0.25, maximumWindowSeconds))) + 8u;
        buffer_.assign(std::max<size_t>(size, 256u), 0.0f);
        reset();
    }

    void reset()
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0u;
        validSamples_ = 0u;
        phaseA_ = 0.0f;
        phaseB_ = 0.5f;
        windowSamples_ = static_cast<float>(sampleRate_ * 0.120);
        lowpass_ = 0.0f;
    }

    float processSample(float input, float amount, float drag)
    {
        if (buffer_.size() < 8u) return input;
        input = std::isfinite(input) ? input : 0.0f;
        amount = clamp(std::isfinite(amount) ? amount : 0.0f,
            0.0f, 1.0f);
        drag = clamp(std::isfinite(drag) ? drag : 0.5f,
            0.0f, 1.0f);

        buffer_[writeIndex_] = input;
        validSamples_ = std::min(buffer_.size(), validSamples_ + 1u);
        const float targetWindowMs = 45.0f + drag * drag * 195.0f;
        const float targetWindow = clamp(targetWindowMs * 0.001f
                * static_cast<float>(sampleRate_),
            8.0f, static_cast<float>(buffer_.size() - 4u));
        windowSamples_ += (targetWindow - windowSamples_) * 0.0012f;

        constexpr float kRatioDelta = 0.5f;
        const float phaseStep = kRatioDelta
            / std::max(8.0f, windowSamples_);
        phaseA_ = wrapPhase(phaseA_ + phaseStep);
        phaseB_ = wrapPhase(phaseB_ + phaseStep);
        const float first = readPhase(phaseA_);
        const float second = readPhase(phaseB_);
        const float firstWeight = grainWindow(phaseA_);
        const float secondWeight = grainWindow(phaseB_);
        const float shifted = (first * firstWeight
                + second * secondWeight)
            / std::max(0.0001f, firstWeight + secondWeight);

        const float cutoff = 6800.0f * std::pow(0.20f, drag);
        const float coefficient = 1.0f - std::exp(-2.0f * kPi
            * std::min(cutoff, static_cast<float>(sampleRate_ * 0.45))
            / static_cast<float>(sampleRate_));
        lowpass_ += (shifted - lowpass_) * coefficient;
        lowpass_ = flushDenormal(lowpass_);
        writeIndex_ = (writeIndex_ + 1u) % buffer_.size();
        return flushDenormal(lerp(input, lowpass_, amount));
    }

    float windowMilliseconds() const
    {
        return windowSamples_ * 1000.0f / static_cast<float>(sampleRate_);
    }

private:
    static float wrapPhase(float phase)
    {
        phase -= std::floor(phase);
        return phase < 0.0f ? phase + 1.0f : phase;
    }

    static float grainWindow(float phase)
    {
        return 0.5f - 0.5f * std::cos(2.0f * kPi
            * clamp(phase, 0.0f, 1.0f));
    }

    float readPhase(float phase) const
    {
        return readDelay(clamp(windowSamples_ * phase,
            1.0f, static_cast<float>(buffer_.size() - 4u)));
    }

    float readDelay(float delaySamples) const
    {
        float read = static_cast<float>(writeIndex_) - delaySamples;
        const float size = static_cast<float>(buffer_.size());
        while (read < 0.0f) read += size;
        while (read >= size) read -= size;
        const auto firstIndex = static_cast<size_t>(read);
        const auto secondIndex = (firstIndex + 1u) % buffer_.size();
        const auto sample = [&](size_t index) {
            if (validSamples_ < buffer_.size()) {
                const size_t age = (writeIndex_ + buffer_.size() - index)
                    % buffer_.size();
                if (age >= validSamples_) return 0.0f;
            }
            return buffer_[index];
        };
        const float fraction = read - static_cast<float>(firstIndex);
        const float first = sample(firstIndex);
        return first + (sample(secondIndex) - first) * fraction;
    }

    double sampleRate_ = 48000.0;
    std::vector<float> buffer_;
    size_t writeIndex_ = 0u;
    size_t validSamples_ = 0u;
    float phaseA_ = 0.0f;
    float phaseB_ = 0.5f;
    float windowSamples_ = 5760.0f;
    float lowpass_ = 0.0f;
};

// A reduced-order transducer -> material -> contact-pickup system. The delay
// is the propagation path. Eight dispersive modes form the material body, and
// the nonlinear stages sit at the actuator and pickup rather than around a
// conventional dry vocal distortion chain.
class ProcessorConduit {
public:
    static constexpr uint32_t kModeCount = 8u;

    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        const auto delaySize = static_cast<size_t>(
            std::ceil(sampleRate_ * kMaximumPropagationSeconds)) + 4u;
        propagation_.assign(std::max<size_t>(delaySize, 8u), 0.0f);
        const auto airSize = static_cast<size_t>(
            std::ceil(sampleRate_ * kMaximumAirPathSeconds)) + 8u;
        for (auto& path : airPath_) {
            path.assign(std::max<size_t>(airSize, 32u), 0.0f);
        }
        const auto chamberSize = static_cast<size_t>(
            std::ceil(sampleRate_ * kMaximumChamberSeconds)) + 8u;
        for (auto& line : chamberLines_) {
            line.assign(std::max<size_t>(chamberSize, 64u), 0.0f);
        }
        for (auto& octave : octave_) octave.prepare(sampleRate_);
        updateFixedCoefficients();
        reset();
    }

    void reset()
    {
        std::fill(propagation_.begin(), propagation_.end(), 0.0f);
        for (auto& path : airPath_) {
            std::fill(path.begin(), path.end(), 0.0f);
        }
        for (auto& line : chamberLines_) {
            std::fill(line.begin(), line.end(), 0.0f);
        }
        writeIndex_ = 0u;
        airWriteIndex_ = 0u;
        chamberWriteIndex_ = 0u;
        inputDcIn_ = 0.0f;
        inputDcOut_ = 0.0f;
        driverLowpass_ = 0.0f;
        feedbackLowpass_ = 0.0f;
        feedbackSpectralLow_ = 0.0f;
        feedbackDcIn_ = 0.0f;
        feedbackDcOut_ = 0.0f;
        contactStates_.fill({});
        outputDcIn_.fill(0.0f);
        outputDcOut_.fill(0.0f);
        chamberDamping_.fill(0.0f);
        pa_ = {};
        motionPosition_ = 0.0f;
        motionVelocity_ = 0.0f;
        motionTarget_ = 0.0f;
        motionAngle_ = 0.0f;
        motionAngleVelocity_ = 0.0f;
        motionAngleTarget_ = 0.0f;
        motionSamplesRemaining_ = 0u;
        airDelaySamples_.fill(static_cast<float>(sampleRate_ * 0.004));
        breakupCaptureStart_ = 0u;
        breakupCellSamples_ = 16u;
        breakupPhase_ = 0.0f;
        breakupRepeatIndex_ = 0u;
        breakupRepeatCount_ = 0u;
        breakupActive_ = false;
        breakupActivity_ = 0.0f;
        randomState_ = 0x93d7a4c1u;
        safetyEnvelope_ = 0.0f;
        sourceEnvelope_ = 0.0f;
        feedbackToneEnvelope_ = 0.0f;
        feedbackToneGovernor_ = 1.0f;
        overloadLevel_ = 0.0f;
        overloadRoughness_ = 0.0f;
        overloadMask_ = 0.0f;
        previousFeedbackWitness_ = 0.0f;
        materialActivity_ = 0.0f;
        governorReduction_ = 0.0f;
        for (auto& lane : pedalLanes_) {
            lane = {};
            lane.activePedal = target_.pedal;
            lane.previousPedal = lane.activePedal;
            lane.fade = 1.0f;
        }
        for (auto& octave : octave_) octave.reset();
        modes_.fill({});
        smoothed_ = target_;
        refreshTargets(true);
        inputGain_ = inputGainTarget_;
        outputGain_ = outputGainTarget_;
        delaySamples_ = delaySamplesTarget_;
        chamberDelaySamples_ = chamberDelayTargets_;
        for (uint32_t i = 0u; i < kModeCount; ++i) {
            modes_[i].pole = modes_[i].poleTarget;
            modes_[i].radius2 = modes_[i].radius2Target;
            modes_[i].gainLeft = modes_[i].gainLeftTarget;
            modes_[i].gainRight = modes_[i].gainRightTarget;
        }
    }

    void panic() { reset(); }

    void setParams(const ProcessorConduitParams& params)
    {
        target_ = sanitize(params);
        if (!propagation_.empty()) refreshTargets(false);
    }

    ProcessorConduitParams params() const { return target_; }

    float processSample(float input)
    {
        float left = 0.0f;
        float right = 0.0f;
        processFrame(input, left, right);
        return flushDenormal((left + right) * 0.5f);
    }

    void processFrame(float input, float& left, float& right)
    {
        left = 0.0f;
        right = 0.0f;
        if (propagation_.empty()) return;
        input = std::isfinite(input) ? input : 0.0f;
        smoothParams();

        inputGain_ += (inputGainTarget_ - inputGain_) * parameterSmoothingCoeff_;
        outputGain_ += (outputGainTarget_ - outputGain_) * parameterSmoothingCoeff_;
        delaySamples_ += (delaySamplesTarget_ - delaySamples_)
            * delaySmoothingCoeff_;
        for (uint32_t i = 0u; i < chamberDelaySamples_.size(); ++i) {
            chamberDelaySamples_[i] += (chamberDelayTargets_[i]
                    - chamberDelaySamples_[i])
                * delaySmoothingCoeff_;
        }

        const float dcBlocked = input - inputDcIn_ + dcPole_ * inputDcOut_;
        inputDcIn_ = input;
        inputDcOut_ = flushDenormal(dcBlocked);
        const float dry = clamp(inputDcOut_ * inputGain_, -8.0f, 8.0f);

        const float sourceMagnitude = std::abs(dry);
        sourceEnvelope_ += (sourceMagnitude - sourceEnvelope_)
            * (sourceMagnitude > sourceEnvelope_
                ? sourceAttackCoeff_ : sourceReleaseCoeff_);
        sourceEnvelope_ = flushDenormal(sourceEnvelope_);
        const float sourceGate = clamp(sourceEnvelope_ * 22.0f,
            0.0f, 1.0f);

        // PRE DRIVER makes the pedal part of the actuator excitation. The
        // other positions are real topology changes farther down the path.
        const float pedalOutput = smoothed_.pedalPosition
                == ProcessorConduitPedalPosition::PreDriver
            ? processPedal(pedalLanes_[0u], dry) : dry;
        const float driverAmount = smoothed_.driver;
        const float driverCutoff = 11000.0f
            * std::pow(0.22f, driverAmount * driverAmount);
        driverLowpass_ += (pedalOutput - driverLowpass_)
            * onePoleCoeff(driverCutoff);
        driverLowpass_ = flushDenormal(driverLowpass_);
        const float driverBias = driverAmount * 0.16f;
        const float driverGain = 1.0f + driverAmount * 26.0f;
        float actuator = std::tanh(driverLowpass_ * driverGain + driverBias)
            - std::tanh(driverBias);
        actuator *= 0.72f + 0.28f / std::sqrt(driverGain);

        // LF Synth's fast envelope and Stack's nonlinear request curve govern
        // the complete PA -> moving microphone -> material loop.
        safetyEnvelope_ += (std::abs(feedbackDcOut_) - safetyEnvelope_)
            * safetyCoeff_;
        const float excess = std::max(0.0f, safetyEnvelope_ - 0.48f);
        const float fastGovernor = 1.0f / (1.0f + excess * 12.0f);
        const float requestedFeedback = std::min(0.972f,
            smoothed_.feedback
                * (0.42f + smoothed_.feedback * 0.60f));
        const float overloadGain = lerp(1.0f, 0.16f, overloadMask_);
        const float feedbackGain = requestedFeedback * fastGovernor
            * feedbackToneGovernor_ * overloadGain * sourceGate;
        const float combinedGovernor = std::min(fastGovernor,
            std::min(feedbackToneGovernor_, overloadGain));
        governorReduction_ += ((1.0f - combinedGovernor)
                - governorReduction_) * meterCoeff_;
        const float propagationInput = clamp(
            actuator + feedbackDcOut_ * feedbackGain, -2.5f, 2.5f);

        propagation_[writeIndex_] = propagationInput;
        const float arrived = readPropagation(delaySamples_);
        writeIndex_ = (writeIndex_ + 1u) % propagation_.size();

        float modalLeft = 0.0f;
        float modalRight = 0.0f;
        for (auto& mode : modes_) {
            mode.pole += (mode.poleTarget - mode.pole) * modeSmoothingCoeff_;
            mode.radius2 += (mode.radius2Target - mode.radius2)
                * modeSmoothingCoeff_;
            mode.gainLeft += (mode.gainLeftTarget - mode.gainLeft)
                * modeSmoothingCoeff_;
            mode.gainRight += (mode.gainRightTarget - mode.gainRight)
                * modeSmoothingCoeff_;
            const float excitation = arrived * mode.excitation;
            const float value = excitation + mode.pole * mode.y1
                - mode.radius2 * mode.y2;
            mode.y2 = mode.y1;
            mode.y1 = flushDenormal(clamp(value, -6.0f, 6.0f));
            modalLeft += mode.y1 * mode.gainLeft;
            modalRight += mode.y1 * mode.gainRight;
        }

        const auto& material = definition(smoothed_.material);
        const float direct = material.directTransmission;
        float bodyLeft = lerp(modalLeft * 0.62f, arrived, direct);
        float bodyRight = lerp(modalRight * 0.62f, arrived, direct);
        bodyLeft = clamp(bodyLeft, -5.0f, 5.0f);
        bodyRight = clamp(bodyRight, -5.0f, 5.0f);

        float chamberLeft = 0.0f;
        float chamberRight = 0.0f;
        processChamber((bodyLeft + bodyRight) * 0.5f,
            material.chamberResponse * smoothed_.chamber,
            material.brightness, chamberLeft, chamberRight);
        const float chamberMix = clamp(material.chamberResponse
                * smoothed_.chamber,
            0.0f, 0.92f);
        bodyLeft = clamp(bodyLeft + chamberLeft * chamberMix * 1.65f,
            -5.0f, 5.0f);
        bodyRight = clamp(bodyRight + chamberRight * chamberMix * 1.65f,
            -5.0f, 5.0f);

        // A high-impedance virtual piezo preamp follows each pickup. Its
        // infrasonic pole is intentionally below the vocal bass range; CONTACT
        // adds velocity and nonlinear pressure without removing the body.
        float contactLeft = processContact(contactStates_[0u], bodyLeft);
        float contactRight = processContact(contactStates_[1u], bodyRight);
        if (smoothed_.pedalPosition
                == ProcessorConduitPedalPosition::PostPiezo) {
            contactLeft = processPedal(pedalLanes_[0u], contactLeft);
            contactRight = processPedal(pedalLanes_[1u], contactRight);
        }
        const float octaveLeft = octave_[0u].processSample(contactLeft,
            smoothed_.octaveDown, smoothed_.octaveDrag);
        const float octaveRight = octave_[1u].processSample(contactRight,
            smoothed_.octaveDown, smoothed_.octaveDrag);
        const float contactMid = (contactLeft + contactRight) * 0.5f;
        const float octaveMid = (octaveLeft + octaveRight) * 0.5f;
        const float feedbackWitness = lerp(contactMid, octaveMid,
            smoothed_.octaveDown * 0.65f);

        float paLeft = 0.0f;
        float paRight = 0.0f;
        processFeedbackPath(feedbackWitness, paLeft, paRight);

        const float paAudible = smoothed_.feedback
            * (0.025f + smoothed_.paDrive * 0.14f);
        float wetLeft = octaveLeft + paLeft * paAudible;
        float wetRight = octaveRight + paRight * paAudible;
        const float wetMid = (wetLeft + wetRight) * 0.5f;
        const float width = smoothed_.stereoWidth;
        wetLeft = lerp(wetMid, wetLeft, width * 1.35f);
        wetRight = lerp(wetMid, wetRight, width * 1.35f);

        const std::array<float, 2u> wet {{ wetLeft, wetRight }};
        std::array<float, 2u> output {};
        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            const float wetDc = wet[channel] - outputDcIn_[channel]
                + dcPole_ * outputDcOut_[channel];
            outputDcIn_[channel] = wet[channel];
            outputDcOut_[channel] = flushDenormal(wetDc);
            const float mixed = lerp(dry, outputDcOut_[channel],
                smoothed_.mix);
            output[channel] = std::tanh(clamp(
                mixed * outputGain_, -8.0f, 8.0f));
        }
        left = flushDenormal(output[0u]);
        right = flushDenormal(output[1u]);

        const float bodyMagnitude = std::max(
            std::abs(bodyLeft), std::abs(bodyRight));
        materialActivity_ += (bodyMagnitude - materialActivity_)
            * meterCoeff_;
    }

    float materialActivity() const
    {
        return clamp(materialActivity_, 0.0f, 1.0f);
    }

    float governorReduction() const
    {
        return clamp(governorReduction_, 0.0f, 1.0f);
    }

    float feedbackBreakupActivity() const
    {
        return clamp(breakupActivity_, 0.0f, 1.0f);
    }

    float virtualMicPosition() const
    {
        return clamp(motionPosition_, -1.0f, 1.0f);
    }

    float fundamentalHz() const { return fundamentalHz_; }
    float propagationMilliseconds() const
    {
        return delaySamples_ * 1000.0f / static_cast<float>(sampleRate_);
    }
    float octaveWindowMilliseconds() const
    {
        return octave_[0u].windowMilliseconds();
    }

private:
    static constexpr float kMaximumPropagationSeconds = 0.090f;
    static constexpr float kMaximumAirPathSeconds = 0.110f;
    static constexpr float kMaximumChamberSeconds = 0.240f;

    struct MaterialDefinition {
        std::array<float, kModeCount> ratios;
        std::array<float, kModeCount> gains;
        float speed;
        float decay;
        float brightness;
        float directTransmission;
        float chamberResponse;
    };

    struct Mode {
        float y1 = 0.0f;
        float y2 = 0.0f;
        float pole = 0.0f;
        float poleTarget = 0.0f;
        float radius2 = 0.0f;
        float radius2Target = 0.0f;
        float gainLeft = 0.0f;
        float gainLeftTarget = 0.0f;
        float gainRight = 0.0f;
        float gainRightTarget = 0.0f;
        float excitation = 0.0f;
    };

    struct ContactState {
        float low = 0.0f;
        float previousInput = 0.0f;
        float bass = 0.0f;
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
    };

    struct PaModeState {
        float first = 0.0f;
        float second = 0.0f;
    };

    struct PaState {
        float previousInput = 0.0f;
        float preampMemory = 0.0f;
        float toneLow = 0.0f;
        float toneMid = 0.0f;
        float toneHigh = 0.0f;
        float transformerLow = 0.0f;
        float sagEnvelope = 0.0f;
        float coilEnvelope = 0.0f;
        float speakerDc = 0.0f;
        std::array<PaModeState, 4u> modes {};
        std::array<float, 2u> micLow {};
    };

    struct PedalState {
        float memory = 0.0f;
        float low = 0.0f;
        float high = 0.0f;
        float envelope = 0.0f;
    };

    struct PedalLane {
        std::array<PedalState, kProcessorConduitPedalCount> states {};
        std::array<PedalState, kProcessorConduitPedalCount> idleStates {};
        ProcessorConduitPedal activePedal = ProcessorConduitPedal::Shred;
        ProcessorConduitPedal previousPedal = ProcessorConduitPedal::Shred;
        float fade = 1.0f;
        float previousInput = 0.0f;
        float dcInput = 0.0f;
        float dcOutput = 0.0f;
    };

    static ProcessorConduitParams sanitize(ProcessorConduitParams params)
    {
        params.material = static_cast<ProcessorConduitMaterial>(
            std::min<uint32_t>(static_cast<uint32_t>(params.material),
                kProcessorConduitMaterialCount - 1u));
        params.inputGainDb = clamp(params.inputGainDb, -24.0f, 36.0f);
        params.driver = clamp(params.driver, 0.0f, 1.0f);
        params.size = clamp(params.size, 0.0f, 1.0f);
        params.tension = clamp(params.tension, 0.0f, 1.0f);
        params.damping = clamp(params.damping, 0.0f, 1.0f);
        params.pickup = clamp(params.pickup, 0.0f, 1.0f);
        params.contact = clamp(params.contact, 0.0f, 1.0f);
        params.feedback = clamp(params.feedback, 0.0f, 1.0f);
        params.mix = clamp(params.mix, 0.0f, 1.0f);
        params.outputGainDb = clamp(params.outputGainDb, -60.0f, 6.0f);
        params.pedal = static_cast<ProcessorConduitPedal>(
            std::min<uint32_t>(static_cast<uint32_t>(params.pedal),
                kProcessorConduitPedalCount - 1u));
        params.pedalDrive = clamp(params.pedalDrive, 0.0f, 1.0f);
        params.pedalTone = clamp(params.pedalTone, 0.0f, 1.0f);
        params.octaveDown = clamp(params.octaveDown, 0.0f, 1.0f);
        params.octaveDrag = clamp(params.octaveDrag, 0.0f, 1.0f);
        params.paDrive = clamp(params.paDrive, 0.0f, 1.0f);
        params.micMotion = clamp(params.micMotion, 0.0f, 1.0f);
        params.chamber = clamp(params.chamber, 0.0f, 1.0f);
        params.stereoWidth = clamp(params.stereoWidth, 0.0f, 1.0f);
        params.pedalPosition = static_cast<ProcessorConduitPedalPosition>(
            std::min<uint32_t>(static_cast<uint32_t>(params.pedalPosition),
                kProcessorConduitPedalPositionCount - 1u));
        params.pedalMix = clamp(params.pedalMix, 0.0f, 1.0f);
        params.inputListen = static_cast<ProcessorConduitInputListen>(
            std::min<uint32_t>(static_cast<uint32_t>(params.inputListen),
                kProcessorConduitInputListenCount - 1u));
        return params;
    }

    static const MaterialDefinition& definition(
        ProcessorConduitMaterial material)
    {
        static constexpr std::array<MaterialDefinition,
            kProcessorConduitMaterialCount> definitions {{
            { { 1.00f, 1.59f, 2.14f, 2.91f, 3.82f, 4.90f, 6.12f, 7.48f },
              { 1.00f, 0.82f, 0.70f, 0.61f, 0.50f, 0.42f, 0.35f, 0.28f },
              1.00f, 1.18f, 1.00f, 0.035f, 0.0f },
            { { 1.00f, 1.47f, 2.09f, 2.93f, 4.11f, 5.32f, 7.03f, 9.20f },
              { 0.72f, 1.00f, 0.58f, 0.82f, 0.48f, 0.64f, 0.42f, 0.36f },
              1.26f, 1.38f, 1.18f, 0.025f, 0.0f },
            { { 1.00f, 1.93f, 2.88f, 3.95f, 5.10f, 6.22f, 7.42f, 8.80f },
              { 1.00f, 0.70f, 0.48f, 0.38f, 0.29f, 0.22f, 0.17f, 0.13f },
              0.72f, 0.48f, 0.66f, 0.065f, 0.0f },
            { { 1.00f, 1.71f, 2.64f, 3.54f, 4.83f, 6.12f, 7.50f, 9.10f },
              { 1.00f, 0.84f, 0.60f, 0.51f, 0.35f, 0.27f, 0.20f, 0.14f },
              0.81f, 0.67f, 0.72f, 0.055f, 0.0f },
            { { 1.00f, 1.32f, 1.91f, 2.53f, 3.34f, 4.28f, 5.51f, 7.02f },
              { 1.00f, 0.92f, 0.70f, 0.54f, 0.42f, 0.31f, 0.23f, 0.17f },
              0.28f, 0.82f, 0.42f, 0.10f, 0.0f },
            { { 1.00f, 1.86f, 2.73f, 3.62f, 4.55f, 5.52f, 6.52f, 7.60f },
              { 1.00f, 0.76f, 0.57f, 0.43f, 0.33f, 0.25f, 0.19f, 0.14f },
              0.58f, 0.92f, 0.58f, 0.075f, 0.0f },
            { { 1.00f, 1.78f, 2.64f, 3.72f, 4.86f, 6.11f, 7.48f, 8.92f },
              { 1.00f, 0.40f, 0.25f, 0.16f, 0.11f, 0.08f, 0.06f, 0.04f },
              2.40f, 0.20f, 0.94f, 0.94f, 0.0f },
            { { 1.00f, 1.43f, 2.06f, 2.77f, 3.68f, 4.82f, 6.08f, 7.61f },
              { 1.00f, 0.88f, 0.67f, 0.58f, 0.45f, 0.36f, 0.28f, 0.22f },
              0.92f, 1.28f, 0.92f, 0.035f, 0.92f },
            { { 1.00f, 1.51f, 2.17f, 3.05f, 4.22f, 5.67f, 7.12f, 9.04f },
              { 0.78f, 1.00f, 0.62f, 0.79f, 0.49f, 0.61f, 0.39f, 0.31f },
              1.18f, 1.46f, 1.12f, 0.025f, 0.96f },
            { { 1.00f, 1.89f, 2.81f, 3.91f, 5.02f, 6.31f, 7.72f, 9.18f },
              { 1.00f, 0.72f, 0.52f, 0.39f, 0.30f, 0.23f, 0.18f, 0.14f },
              0.68f, 0.62f, 0.62f, 0.07f, 0.84f },
            { { 1.0000f, 1.0375f, 1.6630f, 2.0000f,
                2.6970f, 3.0000f, 5.0000f, 5.1200f },
              { 1.00f, 0.72f, 0.92f, 0.68f, 0.62f, 0.55f, 0.42f, 0.36f },
              0.88f, 1.55f, 0.62f, 0.03f, 0.22f },
            { { 1.000f, 1.006f, 2.000f, 2.014f,
                2.400f, 2.414f, 3.000f, 3.025f },
              { 1.00f, 0.70f, 0.86f, 0.61f, 0.71f, 0.52f, 0.59f, 0.43f },
              0.94f, 1.28f, 0.76f, 0.03f, 0.28f },
            { { 1.0000f, 1.0200f, 2.0000f, 2.1053f,
                2.9649f, 3.0000f, 3.5789f, 3.6491f },
              { 1.00f, 0.78f, 0.88f, 0.64f, 0.70f, 0.55f, 0.50f, 0.42f },
              0.82f, 1.72f, 0.58f, 0.035f, 0.24f },
            { { 1.00f, 1.08f, 2.00f, 2.08f,
                4.00f, 4.12f, 5.10f, 5.35f },
              { 0.82f, 0.66f, 1.00f, 0.74f, 0.83f, 0.62f, 0.58f, 0.46f },
              1.08f, 0.92f, 1.26f, 0.025f, 0.18f },
            { { 1.0000f, 1.0060f, 2.7423f, 2.7670f,
                4.0000f, 4.0320f, 5.8420f, 5.9472f },
              { 1.00f, 0.62f, 0.76f, 0.50f, 0.58f, 0.39f, 0.44f, 0.31f },
              0.74f, 0.78f, 0.82f, 0.08f, 0.08f },
            { { 1.0000f, 1.0030f, 2.0428f, 2.0510f,
                2.9572f, 2.9749f, 3.7808f, 3.7997f },
              { 0.74f, 0.60f, 1.00f, 0.72f, 0.67f, 0.51f, 0.56f, 0.43f },
              1.28f, 1.36f, 1.17f, 0.025f, 0.08f },
            { { 1.0000f, 1.0015f, 1.6150f, 1.6182f,
                2.3200f, 2.3258f, 3.1150f, 3.1243f },
              { 1.00f, 0.82f, 0.91f, 0.70f, 0.73f, 0.58f, 0.54f, 0.43f },
              1.12f, 1.84f, 0.94f, 0.03f, 0.90f },
            { { 1.0000f, 1.0040f, 1.9062f, 1.9176f,
                3.0938f, 3.1093f, 3.4166f, 3.4439f },
              { 1.00f, 0.72f, 0.82f, 0.61f, 0.58f, 0.45f, 0.42f, 0.34f },
              1.34f, 1.02f, 1.05f, 0.04f, 0.06f },
            { { 1.0000f, 1.0060f, 1.7350f, 1.7558f,
                2.5800f, 2.6032f, 3.5350f, 3.5986f },
              { 0.86f, 0.68f, 1.00f, 0.76f, 0.69f, 0.54f, 0.55f, 0.42f },
              1.06f, 1.18f, 1.08f, 0.03f, 0.92f },
            { { 1.0000f, 1.0180f, 1.5000f, 1.5375f,
                2.0800f, 2.1216f, 2.7400f, 2.8359f },
              { 1.00f, 0.66f, 0.79f, 0.55f, 0.57f, 0.42f, 0.38f, 0.29f },
              0.66f, 0.56f, 0.48f, 0.07f, 0.78f },
            { { 1.0000f, 1.0080f, 2.7614f, 2.8001f,
                4.0000f, 4.0560f, 5.8420f, 5.9472f },
              { 1.00f, 0.64f, 0.74f, 0.47f, 0.52f, 0.36f, 0.39f, 0.27f },
              0.78f, 0.68f, 0.66f, 0.09f, 0.08f },
            { { 1.0000f, 1.0120f, 1.5933f, 1.6252f,
                2.1355f, 2.1782f, 2.6531f, 2.7194f },
              { 1.00f, 0.82f, 0.88f, 0.72f, 0.76f, 0.61f, 0.57f, 0.48f },
              0.56f, 0.76f, 0.60f, 0.08f, 0.12f },
            { { 1.0000f, 1.0100f, 2.1073f, 2.1368f,
                2.7397f, 2.7890f, 3.2378f, 3.2701f },
              { 1.00f, 0.64f, 0.58f, 0.83f, 0.49f, 0.44f, 0.70f, 0.38f },
              0.46f, 0.58f, 0.44f, 0.10f, 0.18f },
            { { 1.0000f, 1.0280f, 1.5933f, 1.6491f,
                2.1355f, 2.2252f, 2.2954f, 2.3643f },
              { 1.00f, 0.91f, 0.84f, 0.75f, 0.70f, 0.62f, 0.58f, 0.52f },
              0.52f, 0.84f, 0.56f, 0.08f, 0.22f },
            { { 1.0000f, 1.0250f, 1.3400f, 1.3561f,
                2.1351f, 2.1692f, 2.8616f, 2.9131f },
              { 1.00f, 0.78f, 0.68f, 0.57f, 0.63f, 0.87f, 0.50f, 0.43f },
              0.42f, 1.04f, 0.50f, 0.08f, 0.96f },
            { { 1.0000f, 1.0180f, 1.6230f, 1.6619f,
                2.2263f, 2.2976f, 2.4120f, 2.4602f },
              { 1.00f, 0.79f, 0.63f, 0.70f, 0.52f, 0.42f, 0.35f, 0.28f },
              0.38f, 0.42f, 0.38f, 0.11f, 0.10f },
        }};
        const uint32_t index = std::min<uint32_t>(
            static_cast<uint32_t>(material),
            kProcessorConduitMaterialCount - 1u);
        return definitions[index];
    }

    static float materialPitchScale(ProcessorConduitMaterial material)
    {
        switch (material) {
        case ProcessorConduitMaterial::DeepBronze: return 0.16f;
        case ProcessorConduitMaterial::TieredBronze: return 0.46f;
        case ProcessorConduitMaterial::BroadBronze: return 0.34f;
        case ProcessorConduitMaterial::BrightBronze: return 0.74f;
        case ProcessorConduitMaterial::CarbonLaminate: return 0.23f;
        case ProcessorConduitMaterial::GlassPlate: return 0.50f;
        case ProcessorConduitMaterial::SteelShell: return 0.16f;
        case ProcessorConduitMaterial::AluminumPlate: return 0.32f;
        case ProcessorConduitMaterial::Porcelain: return 0.40f;
        case ProcessorConduitMaterial::Earthenware: return 0.26f;
        case ProcessorConduitMaterial::SprucePlate: return 0.20f;
        case ProcessorConduitMaterial::TensionedSkin: return 0.25f;
        case ProcessorConduitMaterial::LoadedMembrane: return 0.12f;
        case ProcessorConduitMaterial::CoupledMembrane: return 0.19f;
        case ProcessorConduitMaterial::CavityMembrane: return 0.14f;
        case ProcessorConduitMaterial::LooseMembrane: return 0.10f;
        default: return 1.0f;
        }
    }

    static float fold(float value)
    {
        const float shifted = value + 1.0f;
        const float wrapped = shifted - 4.0f * std::floor(shifted * 0.25f);
        return wrapped <= 2.0f ? wrapped - 1.0f : 3.0f - wrapped;
    }

    float processSelectedPedal(PedalLane& lane,
        ProcessorConduitPedal pedal, float input)
    {
        const uint32_t index = std::min<uint32_t>(
            static_cast<uint32_t>(pedal),
            kProcessorConduitPedalCount - 1u);
        const float drive = smoothed_.pedalDrive;
        const float tone = smoothed_.pedalTone;
        const float bias = (tone - 0.5f) * 0.13f;
        if (pedal == ProcessorConduitPedal::Shred) {
            const float pressured = std::tanh(input
                * (1.0f + drive * drive * 11.0f));
            return lerp(pressured,
                fold(pressured * (1.0f + drive * 5.5f)),
                drive * drive * 0.72f);
        }
        const auto circuit = static_cast<AnalogDriveCircuit>(index - 1u);
        const float processed = processAnalogDriveCircuit(circuit,
            lane.states[index], input, drive, tone, bias,
            static_cast<float>(sampleRate_ * 2.0));
        const float idle = processAnalogDriveCircuit(circuit,
            lane.idleStates[index], 0.0f, drive, tone, bias,
            static_cast<float>(sampleRate_ * 2.0));
        return processed - idle;
    }

    float processPedal(PedalLane& lane, float input)
    {
        if (smoothed_.pedal != lane.activePedal) {
            lane.previousPedal = lane.activePedal;
            lane.activePedal = smoothed_.pedal;
            lane.fade = 0.0f;
        }
        const auto renderPair = [&](ProcessorConduitPedal pedal) {
            const float midpoint = 0.5f * (lane.previousInput + input);
            return 0.5f * (processSelectedPedal(lane, pedal, midpoint)
                + processSelectedPedal(lane, pedal, input));
        };
        const float active = renderPair(lane.activePedal);
        float selected = active;
        if (lane.fade < 1.0f) {
            selected = lerp(renderPair(lane.previousPedal), active, lane.fade);
            lane.fade = std::min(1.0f,
                lane.fade + circuitFadeCoeff_);
        }
        lane.previousInput = input;
        const float bounded = std::tanh(clamp(selected, -4.0f, 4.0f));
        const float dcBlocked = bounded - lane.dcInput
            + pedalDcPole_ * lane.dcOutput;
        lane.dcInput = bounded;
        lane.dcOutput = flushDenormal(dcBlocked);
        return lerp(input, lane.dcOutput, smoothed_.pedalMix);
    }

    float nextRandom()
    {
        randomState_ ^= randomState_ << 13u;
        randomState_ ^= randomState_ >> 17u;
        randomState_ ^= randomState_ << 5u;
        return static_cast<float>((randomState_ >> 8u) & 0x00ffffffu)
            / 16777216.0f;
    }

    static float readDelay(const std::vector<float>& buffer,
        size_t writeIndex, float delaySamples)
    {
        if (buffer.size() < 4u) return 0.0f;
        const float size = static_cast<float>(buffer.size());
        float position = static_cast<float>(writeIndex) - delaySamples;
        while (position < 0.0f) position += size;
        while (position >= size) position -= size;
        const auto first = static_cast<size_t>(position);
        const auto second = (first + 1u) % buffer.size();
        const float fraction = position - static_cast<float>(first);
        return buffer[first]
            + (buffer[second] - buffer[first]) * fraction;
    }

    static float readPosition(const std::vector<float>& buffer,
        float position)
    {
        if (buffer.size() < 4u) return 0.0f;
        const float size = static_cast<float>(buffer.size());
        while (position < 0.0f) position += size;
        while (position >= size) position -= size;
        const auto first = static_cast<size_t>(position);
        const auto second = (first + 1u) % buffer.size();
        const float fraction = position - static_cast<float>(first);
        return buffer[first]
            + (buffer[second] - buffer[first]) * fraction;
    }

    float processContact(ContactState& state, float body)
    {
        const float contactAmount = smoothed_.contact;
        const float contactCutoff = 150.0f
            * std::pow(13.0f, contactAmount);
        state.low += (body - state.low) * onePoleCoeff(contactCutoff);
        state.low = flushDenormal(state.low);
        state.bass += (body - state.bass) * onePoleCoeff(105.0f);
        state.bass = flushDenormal(state.bass);
        const float velocity = body - state.previousInput;
        state.previousInput = body;
        const float pickupSignal = body
            + state.bass * (0.16f + (1.0f - contactAmount) * 0.12f)
            + (body - state.low) * (0.12f + contactAmount * 0.98f)
            + velocity * contactAmount * 1.55f;

        // The virtual piezo preamp presents a very high impedance. A 5.5 Hz
        // pole blocks offset while leaving proximity-heavy and octave-down
        // vocal fundamentals intact.
        const float wideBand = pickupSignal - state.dcInput
            + piezoDcPole_ * state.dcOutput;
        state.dcInput = pickupSignal;
        state.dcOutput = flushDenormal(wideBand);
        const float contactDrive = 1.0f + contactAmount * 28.0f;
        const float contactBias = contactAmount * 0.10f;
        const float saturated = std::tanh(
            state.dcOutput * contactDrive + contactBias)
            - std::tanh(contactBias);
        const float folded = fold(state.dcOutput
            * (1.0f + contactAmount * 6.5f));
        const float foldMix = clamp((contactAmount - 0.54f) * 1.38f,
            0.0f, 0.60f);
        return flushDenormal(clamp(lerp(saturated, folded, foldMix),
            -2.2f, 2.2f));
    }

    void processChamber(float input, float amount, float brightness,
        float& left, float& right)
    {
        amount = clamp(amount, 0.0f, 1.0f);
        std::array<float, 4u> delayed {};
        for (uint32_t i = 0u; i < delayed.size(); ++i) {
            delayed[i] = readDelay(chamberLines_[i], chamberWriteIndex_,
                chamberDelaySamples_[i]);
            const float cutoff = lerp(1700.0f, 9200.0f, brightness)
                * lerp(0.48f, 1.0f, 1.0f - smoothed_.damping);
            chamberDamping_[i] += (delayed[i] - chamberDamping_[i])
                * onePoleCoeff(cutoff);
            chamberDamping_[i] = flushDenormal(chamberDamping_[i]);
        }
        const float a = chamberDamping_[0u];
        const float b = chamberDamping_[1u];
        const float c = chamberDamping_[2u];
        const float d = chamberDamping_[3u];
        const std::array<float, 4u> matrix {{
            (a + b + c + d) * 0.5f,
            (a - b + c - d) * 0.5f,
            (a + b - c - d) * 0.5f,
            (a - b - c + d) * 0.5f,
        }};
        const float chamberFeedback = clamp(
            lerp(0.32f, 0.885f, amount)
                * lerp(0.96f, 0.56f, smoothed_.damping),
            0.0f, 0.91f);
        const std::array<float, 4u> injection {{
            0.31f, -0.27f, 0.23f, -0.19f,
        }};
        for (uint32_t i = 0u; i < chamberLines_.size(); ++i) {
            chamberLines_[i][chamberWriteIndex_] = flushDenormal(
                clamp(input * injection[i] * amount
                        + matrix[i] * chamberFeedback,
                    -3.0f, 3.0f));
        }
        chamberWriteIndex_ = (chamberWriteIndex_ + 1u)
            % chamberLines_[0u].size();
        left = clamp(a * 0.56f + b * 0.32f - c * 0.21f + d * 0.12f,
            -3.0f, 3.0f);
        right = clamp(b * 0.52f + c * 0.35f - d * 0.22f + a * 0.11f,
            -3.0f, 3.0f);
    }

    void processPaSystem(float input, float& nearMic, float& sideMic)
    {
        const float drive = smoothed_.paDrive;
        const auto amplifierStage = [&](float sample) {
            const float bias = 0.012f + drive * 0.052f;
            const float first = std::tanh(sample
                    * (1.0f + drive * drive * 16.0f) + bias)
                - std::tanh(bias);
            pa_.preampMemory += (first - pa_.preampMemory)
                * onePoleCoeff(lerp(4200.0f, 9800.0f, 1.0f - drive));
            const float second = std::tanh(pa_.preampMemory
                * (1.0f + drive * 7.2f));
            pa_.toneLow += (second - pa_.toneLow) * onePoleCoeff(145.0f);
            pa_.toneMid += (second - pa_.toneMid) * onePoleCoeff(1150.0f);
            pa_.toneHigh += (second - pa_.toneHigh) * onePoleCoeff(5600.0f);
            const float low = pa_.toneLow;
            const float middle = pa_.toneMid - pa_.toneLow;
            const float high = pa_.toneHigh - pa_.toneMid;
            const float voiced = low * 1.08f + middle * (0.88f + drive * 0.42f)
                + high * (0.54f + drive * 0.36f);
            pa_.sagEnvelope += (std::abs(voiced) - pa_.sagEnvelope)
                * (std::abs(voiced) > pa_.sagEnvelope
                    ? paSagAttackCoeff_ : paSagReleaseCoeff_);
            const float rail = 1.0f / (1.0f + pa_.sagEnvelope
                * drive * 2.8f);
            const float power = std::tanh(voiced
                * (1.1f + drive * 5.0f) * rail);
            pa_.transformerLow += (power - pa_.transformerLow)
                * onePoleCoeff(72.0f);
            return power + pa_.transformerLow * 0.14f;
        };
        const float midpoint = (pa_.previousInput + input) * 0.5f;
        const float power = (amplifierStage(midpoint)
                + amplifierStage(input))
            * 0.5f;
        pa_.previousInput = input;
        pa_.coilEnvelope += (std::abs(power) - pa_.coilEnvelope)
            * (std::abs(power) > pa_.coilEnvelope
                ? paCoilAttackCoeff_ : paCoilReleaseCoeff_);
        const float compression = 1.0f / (1.0f
            + pa_.coilEnvelope * (0.35f + drive * 1.65f));
        const std::array<float, 4u> frequencies {{
            86.0f, 338.0f, 1180.0f, 3420.0f,
        }};
        const std::array<float, 4u> radii {{
            0.992f, 0.978f, 0.953f, 0.918f,
        }};
        const std::array<float, 4u> gains {{
            0.27f, 0.22f, 0.15f, 0.09f,
        }};
        std::array<float, 4u> modeValues {};
        const float protection = overloadMask_ * overloadMask_;
        for (uint32_t i = 0u; i < pa_.modes.size(); ++i) {
            const float frequency = frequencies[i]
                * (i == 0u ? 1.0f + pa_.modes[0u].first
                    * drive * 0.028f : 1.0f);
            const float radius = std::pow(radii[i],
                    48000.0f / static_cast<float>(sampleRate_))
                * lerp(1.0f, 0.985f, protection);
            const float coefficient = 2.0f * radius * std::cos(
                2.0f * kPi * frequency / static_cast<float>(sampleRate_));
            const float resonant = power * (1.0f - radius) * gains[i]
                    * lerp(1.0f, 0.64f, protection)
                + coefficient * pa_.modes[i].first
                - radius * radius * pa_.modes[i].second;
            pa_.modes[i].second = pa_.modes[i].first;
            pa_.modes[i].first = flushDenormal(clamp(resonant,
                -3.2f, 3.2f));
            modeValues[i] = pa_.modes[i].first;
        }
        const float modal = modeValues[0u] * 1.22f
            + modeValues[1u] * 1.04f + modeValues[2u] * 0.86f
            + modeValues[3u] * 0.62f;
        pa_.speakerDc += (modal - pa_.speakerDc) * onePoleCoeff(28.0f);
        const float displacement = modal - pa_.speakerDc;
        const float coneBreakup = std::tanh((power
                - modeValues[0u] * 0.31f)
            * (1.0f + drive * drive * 9.0f)) * drive * 0.26f;
        const float speaker = std::tanh((power * 0.43f
                + displacement * 2.15f + coneBreakup) * compression
            * (1.0f + drive * 3.7f));
        nearMic = speaker * 0.76f + modeValues[0u] * 0.48f
            + modeValues[1u] * 0.33f + modeValues[2u] * 0.18f;
        sideMic = speaker * 0.55f + modeValues[0u] * 0.25f
            - modeValues[1u] * 0.29f + modeValues[2u] * 0.40f
            - modeValues[3u] * 0.30f + coneBreakup * 0.31f;
        const float peak = std::max(std::abs(nearMic), std::abs(sideMic));
        const float gain = peak > 2.8f ? 2.8f / peak : 1.0f;
        nearMic = flushDenormal(nearMic * gain);
        sideMic = flushDenormal(sideMic * gain);
    }

    void beginFeedbackBreakup(float motion)
    {
        if (airPath_[0u].size() < 32u) return;
        const float cellMs = lerp(9.0f, 1.2f, motion)
            * lerp(0.72f, 1.35f, nextRandom());
        breakupCellSamples_ = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(cellMs * 0.001f
                * static_cast<float>(sampleRate_))),
            12u, static_cast<uint32_t>(airPath_[0u].size() - 4u));
        breakupCaptureStart_ = (airWriteIndex_ + airPath_[0u].size()
                - breakupCellSamples_)
            % airPath_[0u].size();
        breakupRepeatCount_ = 2u + static_cast<uint32_t>(
            std::lround(motion * 5.0f + nextRandom() * 2.0f));
        breakupRepeatIndex_ = 0u;
        breakupPhase_ = 0.0f;
        breakupActive_ = true;
    }

    void processFeedbackBreakup(float motion, float& left, float& right)
    {
        if (!breakupActive_ || breakupCellSamples_ < 2u) {
            breakupActivity_ += (0.0f - breakupActivity_)
                * breakupReleaseCoeff_;
            return;
        }
        const float phase = clamp(breakupPhase_, 0.0f,
            static_cast<float>(breakupCellSamples_ - 1u));
        const bool reverse = motion > 0.70f
            && (breakupRepeatIndex_ % 3u) == 2u;
        const float local = reverse
            ? static_cast<float>(breakupCellSamples_ - 1u) - phase : phase;
        const float position = static_cast<float>(breakupCaptureStart_)
            + local;
        const float normalized = phase
            / static_cast<float>(std::max(1u, breakupCellSamples_ - 1u));
        const float window = 0.5f - 0.5f
            * std::cos(2.0f * kPi * normalized);
        const float capturedLeft = readPosition(airPath_[0u], position)
            * window;
        const float capturedRight = readPosition(airPath_[1u], position)
            * window;
        const float stress = clamp(feedbackToneEnvelope_ * 1.8f,
            0.0f, 1.0f);
        const float mix = motion * motion * (0.18f + stress * 0.58f);
        left = lerp(left, capturedLeft, mix);
        right = lerp(right, capturedRight, mix);
        const float activityTarget = std::max(
            std::abs(capturedLeft), std::abs(capturedRight)) * mix;
        breakupActivity_ += (activityTarget - breakupActivity_)
            * (activityTarget > breakupActivity_
                ? breakupAttackCoeff_ : breakupReleaseCoeff_);
        const float playbackRate = lerp(0.82f, 1.62f,
            motion * (0.35f + 0.65f * std::abs(motionAngle_)));
        breakupPhase_ += playbackRate;
        if (breakupPhase_ >= static_cast<float>(breakupCellSamples_)) {
            breakupPhase_ = 0.0f;
            if (++breakupRepeatIndex_ >= breakupRepeatCount_) {
                breakupActive_ = false;
            }
        }
    }

    void updateMicMotion()
    {
        const float motion = smoothed_.micMotion;
        if (motionSamplesRemaining_ == 0u) {
            motionTarget_ = (nextRandom() * 2.0f - 1.0f) * motion;
            motionAngleTarget_ = (nextRandom() * 2.0f - 1.0f) * motion;
            const float intervalMs = lerp(210.0f, 13.0f,
                    std::pow(motion, 1.35f))
                * lerp(0.58f, 1.42f, nextRandom());
            motionSamplesRemaining_ = std::max<uint32_t>(1u,
                static_cast<uint32_t>(intervalMs * 0.001f
                    * static_cast<float>(sampleRate_)));
            const float triggerChance = motion
                * (0.20f + motion * 0.56f);
            if (feedbackToneEnvelope_ > 0.012f
                && nextRandom() < triggerChance) {
                beginFeedbackBreakup(motion);
            }
        } else {
            --motionSamplesRemaining_;
        }
        const float motionHz = 0.9f + motion * motion * 15.0f;
        const float omega = 2.0f * kPi * motionHz
            / static_cast<float>(sampleRate_);
        const float spring = omega * omega;
        const float drag = std::exp(-omega * 1.35f);
        motionVelocity_ = (motionVelocity_
                + (motionTarget_ - motionPosition_) * spring) * drag;
        motionPosition_ = clamp(motionPosition_ + motionVelocity_,
            -1.0f, 1.0f);
        motionAngleVelocity_ = (motionAngleVelocity_
                + (motionAngleTarget_ - motionAngle_) * spring * 0.73f)
            * std::exp(-omega * 1.08f);
        motionAngle_ = clamp(motionAngle_ + motionAngleVelocity_,
            -1.0f, 1.0f);
    }

    void processFeedbackPath(float witness, float& spatialLeft,
        float& spatialRight)
    {
        float paNear = 0.0f;
        float paSide = 0.0f;
        processPaSystem(witness, paNear, paSide);
        updateMicMotion();
        airPath_[0u][airWriteIndex_] = paNear;
        airPath_[1u][airWriteIndex_] = paSide;

        const float motion = smoothed_.micMotion;
        const float distance = clamp(0.48f + motionPosition_ * 0.42f,
            0.04f, 0.96f);
        const float sideOffset = motionAngle_ * motion * 0.13f;
        const std::array<float, 2u> distances {{
            clamp(distance + sideOffset, 0.02f, 1.0f),
            clamp(distance - sideOffset, 0.02f, 1.0f),
        }};
        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            const float target = (0.00065f
                    + distances[channel] * distances[channel] * 0.036f)
                * static_cast<float>(sampleRate_);
            airDelaySamples_[channel] += (target - airDelaySamples_[channel])
                * airMotionDelayCoeff_;
        }
        float movingNear = readDelay(airPath_[0u], airWriteIndex_,
            airDelaySamples_[0u]);
        float movingSide = readDelay(airPath_[1u], airWriteIndex_,
            airDelaySamples_[1u]);
        airWriteIndex_ = (airWriteIndex_ + 1u) % airPath_[0u].size();

        const float angleBlend = clamp(0.28f + motionAngle_ * 0.42f,
            0.0f, 1.0f);
        spatialLeft = lerp(movingNear, movingSide, angleBlend * 0.62f);
        spatialRight = lerp(movingSide, movingNear,
            (1.0f - angleBlend) * 0.46f);
        const float polarPattern = lerp(1.0f,
            0.18f + 0.82f * std::cos(motionAngle_ * kPi), motion);
        const float distanceGain = lerp(1.30f, 0.42f, distance);
        spatialLeft *= polarPattern * distanceGain;
        spatialRight *= polarPattern * distanceGain;
        processFeedbackBreakup(motion, spatialLeft, spatialRight);

        const float micCutoff = lerp(1800.0f, 10500.0f, 1.0f - distance)
            * lerp(1.0f, 0.74f, motion);
        pa_.micLow[0u] += (spatialLeft - pa_.micLow[0u])
            * onePoleCoeff(micCutoff);
        pa_.micLow[1u] += (spatialRight - pa_.micLow[1u])
            * onePoleCoeff(micCutoff * 0.91f);
        spatialLeft = flushDenormal(pa_.micLow[0u]);
        spatialRight = flushDenormal(pa_.micLow[1u]);
        if (smoothed_.pedalPosition
                == ProcessorConduitPedalPosition::PostMic) {
            spatialLeft = processPedal(pedalLanes_[0u], spatialLeft);
            spatialRight = processPedal(pedalLanes_[1u], spatialRight);
        }
        const float movingMic = (spatialLeft + spatialRight) * 0.5f;

        const float feedbackCutoff = materialFeedbackCutoff(
            smoothed_.material, smoothed_.damping);
        feedbackLowpass_ += (movingMic - feedbackLowpass_)
            * onePoleCoeff(feedbackCutoff);
        feedbackLowpass_ = flushDenormal(feedbackLowpass_);

        // Stack-style overload detection reacts to sustained level and edge
        // roughness. Movement and PA breakup therefore make the mask breathe
        // and fracture instead of simply turning the loop down.
        const float feedbackMagnitude = std::abs(feedbackLowpass_);
        const float feedbackRoughness = std::abs(
            feedbackLowpass_ - previousFeedbackWitness_);
        previousFeedbackWitness_ = feedbackLowpass_;
        overloadLevel_ += (feedbackMagnitude - overloadLevel_)
            * (feedbackMagnitude > overloadLevel_
                ? overloadLevelAttackCoeff_ : overloadLevelReleaseCoeff_);
        overloadRoughness_ += (feedbackRoughness - overloadRoughness_)
            * (feedbackRoughness > overloadRoughness_
                ? overloadRoughnessAttackCoeff_
                : overloadRoughnessReleaseCoeff_);
        overloadLevel_ = flushDenormal(overloadLevel_);
        overloadRoughness_ = flushDenormal(overloadRoughness_);
        const float normalizedRoughness = overloadRoughness_
            / std::max(0.035f, overloadLevel_);
        const float levelStress = clamp((overloadLevel_ - 0.50f) / 0.68f,
            0.0f, 1.0f);
        const float loopStress = clamp((safetyEnvelope_ - 0.42f) / 0.64f,
            0.0f, 1.0f);
        const float roughStress = clamp((normalizedRoughness - 0.27f)
                / 0.82f,
            0.0f, 1.0f);
        const float overloadTarget = std::max(loopStress,
                levelStress * 0.80f)
            * (0.25f + roughStress * 0.75f);
        overloadMask_ += (overloadTarget - overloadMask_)
            * (overloadTarget > overloadMask_
                ? overloadMaskAttackCoeff_ : overloadMaskReleaseCoeff_);
        overloadMask_ = flushDenormal(clamp(overloadMask_, 0.0f, 1.0f));

        feedbackSpectralLow_ += (feedbackLowpass_ - feedbackSpectralLow_)
            * onePoleCoeff(lerp(1450.0f, 3300.0f,
                definition(smoothed_.material).brightness * 0.7f));
        feedbackSpectralLow_ = flushDenormal(feedbackSpectralLow_);
        const float spectralMask = overloadMask_ * 0.84f;
        const float energyMask = lerp(1.0f, 0.70f, overloadMask_);
        const float maskedFeedback = lerp(feedbackLowpass_,
                feedbackSpectralLow_ * 0.86f, spectralMask)
            * energyMask;

        const float feedbackToneMagnitude = std::abs(maskedFeedback);
        feedbackToneEnvelope_ += (feedbackToneMagnitude
                - feedbackToneEnvelope_)
            * (feedbackToneMagnitude > feedbackToneEnvelope_
                ? feedbackGovernorAttackCoeff_
                : feedbackGovernorReleaseCoeff_);
        feedbackToneEnvelope_ = flushDenormal(feedbackToneEnvelope_);
        const float feedbackToneExcess = std::max(0.0f,
            feedbackToneEnvelope_ - 0.78f);
        const float feedbackGovernorTarget = 1.0f
            / (1.0f + feedbackToneExcess * 7.5f);
        feedbackToneGovernor_ += (feedbackGovernorTarget
                - feedbackToneGovernor_)
            * (feedbackGovernorTarget < feedbackToneGovernor_
                ? feedbackGovernorAttackCoeff_
                : feedbackGovernorReleaseCoeff_);
        feedbackToneGovernor_ = flushDenormal(clamp(
            feedbackToneGovernor_, 0.0f, 1.0f));

        const float feedbackDc = maskedFeedback - feedbackDcIn_
            + dcPole_ * feedbackDcOut_;
        feedbackDcIn_ = maskedFeedback;
        feedbackDcOut_ = flushDenormal(feedbackDc);
    }

    float onePoleCoeff(float cutoffHz) const
    {
        const float cutoff = clamp(cutoffHz, 8.0f,
            static_cast<float>(sampleRate_ * 0.45));
        return 1.0f - std::exp(-2.0f * kPi * cutoff
            / static_cast<float>(sampleRate_));
    }

    static float materialFeedbackCutoff(ProcessorConduitMaterial material,
        float damping)
    {
        const auto& def = definition(material);
        return 900.0f + def.brightness * 6200.0f
            * (1.0f - damping * 0.62f);
    }

    void smoothParams()
    {
        auto smooth = [this](float& current, float target) {
            current += (target - current) * parameterSmoothingCoeff_;
        };
        smooth(smoothed_.driver, target_.driver);
        smooth(smoothed_.size, target_.size);
        smooth(smoothed_.tension, target_.tension);
        smooth(smoothed_.damping, target_.damping);
        smooth(smoothed_.pickup, target_.pickup);
        smooth(smoothed_.contact, target_.contact);
        smooth(smoothed_.feedback, target_.feedback);
        smooth(smoothed_.mix, target_.mix);
        smoothed_.material = target_.material;
        smoothed_.pedal = target_.pedal;
        smooth(smoothed_.pedalDrive, target_.pedalDrive);
        smooth(smoothed_.pedalTone, target_.pedalTone);
        smooth(smoothed_.octaveDown, target_.octaveDown);
        smooth(smoothed_.octaveDrag, target_.octaveDrag);
        smooth(smoothed_.paDrive, target_.paDrive);
        smooth(smoothed_.micMotion, target_.micMotion);
        smooth(smoothed_.chamber, target_.chamber);
        smooth(smoothed_.stereoWidth, target_.stereoWidth);
        smoothed_.pedalPosition = target_.pedalPosition;
        smooth(smoothed_.pedalMix, target_.pedalMix);
    }

    void refreshTargets(bool force)
    {
        if (force || target_.inputGainDb != derivedInputGainDb_) {
            derivedInputGainDb_ = target_.inputGainDb;
            inputGainTarget_ = dbToGain(target_.inputGainDb);
        }
        if (force || target_.outputGainDb != derivedOutputGainDb_) {
            derivedOutputGainDb_ = target_.outputGainDb;
            outputGainTarget_ = dbToGain(target_.outputGainDb);
        }

        const auto& material = definition(target_.material);
        const float sizeFrequency = 410.0f
            * std::pow(2.0f, (0.5f - target_.size) * 3.15f);
        const float tensionScale = std::pow(2.0f,
            (target_.tension - 0.5f) * 1.8f);
        fundamentalHz_ = clamp(sizeFrequency * tensionScale
                * materialPitchScale(target_.material),
            18.0f, static_cast<float>(sampleRate_ * 0.18));

        const float pathSeconds = (0.0014f
            + target_.size * target_.size * 0.045f)
            / std::max(0.2f, material.speed)
            * lerp(1.0f, 0.13f, material.directTransmission);
        const float pickupPath = 0.24f + target_.pickup * 0.76f;
        delaySamplesTarget_ = clamp(pathSeconds * pickupPath
                * static_cast<float>(sampleRate_),
            1.0f, static_cast<float>(propagation_.size() - 3u));

        const float pickupPosition = 0.06f + target_.pickup * 0.88f;
        const float secondPickupPosition = clamp(
            0.90f - target_.pickup * 0.68f, 0.08f, 0.92f);
        for (uint32_t i = 0u; i < kModeCount; ++i) {
            auto& mode = modes_[i];
            const float dispersion = 1.0f
                + (target_.tension - 0.5f) * 0.026f
                    * static_cast<float>(i * i);
            const float frequency = clamp(fundamentalHz_
                    * material.ratios[i] * dispersion,
                24.0f, static_cast<float>(sampleRate_ * 0.44));
            const float modeDamping = 0.035f
                + target_.damping * target_.damping * 15.0f;
            const float decaySeconds = clamp(material.decay
                    * (1.55f / modeDamping)
                    / (1.0f + static_cast<float>(i) * 0.18f
                        / std::max(0.25f, material.brightness)),
                0.018f, 7.5f);
            const float radius = std::exp(-1.0f
                / (decaySeconds * static_cast<float>(sampleRate_)));
            mode.poleTarget = 2.0f * radius * std::cos(
                2.0f * kPi * frequency / static_cast<float>(sampleRate_));
            mode.radius2Target = radius * radius;
            const float position = std::sin(kPi
                * static_cast<float>(i + 1u) * pickupPosition);
            const float secondPosition = std::sin(kPi
                * static_cast<float>(i + 1u) * secondPickupPosition);
            const float pickupWeight = 0.22f + position * 0.78f;
            const float secondPickupWeight = 0.22f
                + secondPosition * 0.78f;
            mode.gainLeftTarget = material.gains[i] * pickupWeight
                / (1.0f + static_cast<float>(i) * 0.16f);
            mode.gainRightTarget = material.gains[i] * secondPickupWeight
                / (1.0f + static_cast<float>(i) * 0.16f);
            mode.excitation = std::sqrt(std::max(1.0e-7f,
                1.0f - radius)) * (0.72f + material.brightness * 0.38f);
        }

        const float chamberBaseSeconds = (0.010f
                + target_.size * target_.size * 0.086f)
            / std::max(0.52f, material.speed);
        static constexpr std::array<float, 4u> chamberRatios {{
            0.73f, 0.91f, 1.13f, 1.39f,
        }};
        for (uint32_t i = 0u; i < chamberDelayTargets_.size(); ++i) {
            chamberDelayTargets_[i] = clamp(chamberBaseSeconds
                    * chamberRatios[i] * static_cast<float>(sampleRate_),
                3.0f, static_cast<float>(chamberLines_[i].size() - 3u));
        }
    }

    float readPropagation(float delaySamples) const
    {
        const float size = static_cast<float>(propagation_.size());
        float read = static_cast<float>(writeIndex_) - delaySamples;
        while (read < 0.0f) read += size;
        while (read >= size) read -= size;
        const auto a = static_cast<size_t>(read);
        const auto b = (a + 1u) % propagation_.size();
        const float fraction = read - static_cast<float>(a);
        return propagation_[a]
            + (propagation_[b] - propagation_[a]) * fraction;
    }

    void updateFixedCoefficients()
    {
        const float sr = static_cast<float>(sampleRate_);
        parameterSmoothingCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.025f));
        modeSmoothingCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.060f));
        delaySmoothingCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.080f));
        safetyCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.012f));
        sourceAttackCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.003f));
        sourceReleaseCoeff_ = 1.0f - std::exp(-1.0f / (sr * 1.650f));
        feedbackGovernorAttackCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.002f));
        feedbackGovernorReleaseCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.180f));
        overloadLevelAttackCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.006f));
        overloadLevelReleaseCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.110f));
        overloadRoughnessAttackCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.003f));
        overloadRoughnessReleaseCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.055f));
        overloadMaskAttackCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.009f));
        overloadMaskReleaseCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.260f));
        paSagAttackCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.006f));
        paSagReleaseCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.170f));
        paCoilAttackCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.004f));
        paCoilReleaseCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.095f));
        airMotionDelayCoeff_ = 1.0f
            - std::exp(-1.0f / (sr * 0.0018f));
        breakupAttackCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.004f));
        breakupReleaseCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.085f));
        circuitFadeCoeff_ = 1.0f / std::max(1.0f, sr * 0.020f);
        meterCoeff_ = 1.0f - std::exp(-1.0f / (sr * 0.055f));
        dcPole_ = std::exp(-2.0f * kPi * 8.0f / sr);
        piezoDcPole_ = std::exp(-2.0f * kPi * 5.5f / sr);
        pedalDcPole_ = std::exp(-2.0f * kPi * 12.0f / sr);
    }

    double sampleRate_ = 48000.0;
    ProcessorConduitParams target_ {};
    ProcessorConduitParams smoothed_ {};
    std::array<Mode, kModeCount> modes_ {};
    std::array<ProcessorConduitOctaveDown, 2u> octave_ {};
    std::vector<float> propagation_;
    std::array<std::vector<float>, 2u> airPath_ {};
    std::array<std::vector<float>, 4u> chamberLines_ {};
    size_t writeIndex_ = 0u;
    size_t airWriteIndex_ = 0u;
    size_t chamberWriteIndex_ = 0u;
    float delaySamples_ = 1.0f;
    float delaySamplesTarget_ = 1.0f;
    std::array<float, 2u> airDelaySamples_ {{ 192.0f, 192.0f }};
    std::array<float, 4u> chamberDelaySamples_ {{
        720.0f, 960.0f, 1200.0f, 1440.0f,
    }};
    std::array<float, 4u> chamberDelayTargets_ {{
        720.0f, 960.0f, 1200.0f, 1440.0f,
    }};
    std::array<float, 4u> chamberDamping_ {};
    float fundamentalHz_ = 220.0f;
    float inputDcIn_ = 0.0f;
    float inputDcOut_ = 0.0f;
    float driverLowpass_ = 0.0f;
    float feedbackLowpass_ = 0.0f;
    float feedbackSpectralLow_ = 0.0f;
    float feedbackDcIn_ = 0.0f;
    float feedbackDcOut_ = 0.0f;
    std::array<ContactState, 2u> contactStates_ {};
    std::array<float, 2u> outputDcIn_ {};
    std::array<float, 2u> outputDcOut_ {};
    PaState pa_ {};
    float motionPosition_ = 0.0f;
    float motionVelocity_ = 0.0f;
    float motionTarget_ = 0.0f;
    float motionAngle_ = 0.0f;
    float motionAngleVelocity_ = 0.0f;
    float motionAngleTarget_ = 0.0f;
    uint32_t motionSamplesRemaining_ = 0u;
    size_t breakupCaptureStart_ = 0u;
    uint32_t breakupCellSamples_ = 16u;
    float breakupPhase_ = 0.0f;
    uint32_t breakupRepeatIndex_ = 0u;
    uint32_t breakupRepeatCount_ = 0u;
    bool breakupActive_ = false;
    float breakupActivity_ = 0.0f;
    uint32_t randomState_ = 0x93d7a4c1u;
    float safetyEnvelope_ = 0.0f;
    float sourceEnvelope_ = 0.0f;
    float feedbackToneEnvelope_ = 0.0f;
    float feedbackToneGovernor_ = 1.0f;
    float overloadLevel_ = 0.0f;
    float overloadRoughness_ = 0.0f;
    float overloadMask_ = 0.0f;
    float previousFeedbackWitness_ = 0.0f;
    float materialActivity_ = 0.0f;
    float governorReduction_ = 0.0f;
    std::array<PedalLane, 2u> pedalLanes_ {};
    float inputGainTarget_ = 1.0f;
    float inputGain_ = 1.0f;
    float outputGainTarget_ = 0.501187f;
    float outputGain_ = 0.501187f;
    float parameterSmoothingCoeff_ = 0.001f;
    float modeSmoothingCoeff_ = 0.0004f;
    float delaySmoothingCoeff_ = 0.0003f;
    float safetyCoeff_ = 0.002f;
    float sourceAttackCoeff_ = 0.002f;
    float sourceReleaseCoeff_ = 0.00002f;
    float feedbackGovernorAttackCoeff_ = 0.002f;
    float feedbackGovernorReleaseCoeff_ = 0.0001f;
    float overloadLevelAttackCoeff_ = 0.002f;
    float overloadLevelReleaseCoeff_ = 0.0002f;
    float overloadRoughnessAttackCoeff_ = 0.002f;
    float overloadRoughnessReleaseCoeff_ = 0.0003f;
    float overloadMaskAttackCoeff_ = 0.002f;
    float overloadMaskReleaseCoeff_ = 0.00008f;
    float paSagAttackCoeff_ = 0.002f;
    float paSagReleaseCoeff_ = 0.0001f;
    float paCoilAttackCoeff_ = 0.003f;
    float paCoilReleaseCoeff_ = 0.0002f;
    float airMotionDelayCoeff_ = 0.01f;
    float breakupAttackCoeff_ = 0.004f;
    float breakupReleaseCoeff_ = 0.0003f;
    float circuitFadeCoeff_ = 0.001f;
    float meterCoeff_ = 0.001f;
    float dcPole_ = 0.998f;
    float piezoDcPole_ = 0.999f;
    float pedalDcPole_ = 0.997f;
    float derivedInputGainDb_ = 1000.0f;
    float derivedOutputGainDb_ = 1000.0f;
};

} // namespace s3g
