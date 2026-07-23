#pragma once

#include "s3g_macro_shred.h"
#include "s3g_math.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace s3g {

constexpr uint32_t kPsdRawFieldChannels = 8;
constexpr uint32_t kPsdRawFieldTapeSize = 65536;
constexpr uint32_t kPsdRawFieldCodebookSize = 32;
constexpr uint32_t kPsdRawFieldTransformSize = 32;
constexpr uint32_t kPsdRawFieldLpcOrder = 6;
constexpr uint32_t kPsdRawFieldDiscLookahead = 32;
constexpr std::size_t kPsdRawFieldMaxSourceBytes = 64u * 1024u * 1024u;

struct PsdRawFieldSource {
    std::vector<uint8_t> bytes;
    uint64_t originalByteCount = 0u;
    uint32_t loadedByteCount = 0u;
    bool truncated = false;
    bool waveform = false;
    uint32_t sourceSampleRate = 0u;
    uint32_t sourceChannelCount = 0u;
    uint32_t sourceBitsPerSample = 0u;
    uint64_t sourceFrameCount = 0u;
    uint64_t loadedFrameCount = 0u;
    uint64_t sourceDataByteCount = 0u;
};

struct PsdRawFieldWaveformInfo {
    uint32_t sampleRate = 0u;
    uint32_t channelCount = 0u;
    uint32_t bitsPerSample = 0u;
    uint64_t sourceFrameCount = 0u;
    uint64_t loadedFrameCount = 0u;
    uint64_t sourceDataByteCount = 0u;
    bool truncated = false;
};

inline std::shared_ptr<const PsdRawFieldSource> makePsdRawFieldSource(
    std::vector<uint8_t> data,
    uint64_t originalByteCount = 0u,
    const PsdRawFieldWaveformInfo& waveform = {})
{
    if (data.empty()) return {};
    const std::size_t sourceSize = data.size();
    const std::size_t loaded = std::min(sourceSize, kPsdRawFieldMaxSourceBytes);
    std::size_t tapeSize = kPsdRawFieldTapeSize;
    while (tapeSize < loaded) tapeSize <<= 1u;

    try {
        auto source = std::make_shared<PsdRawFieldSource>();
        data.resize(loaded);
        data.resize(tapeSize);
        for (std::size_t i = loaded; i < tapeSize; ++i) data[i] = data[i % loaded];
        source->bytes = std::move(data);
        source->originalByteCount = originalByteCount > 0u ? originalByteCount : static_cast<uint64_t>(sourceSize);
        source->loadedByteCount = static_cast<uint32_t>(loaded);
        source->waveform = waveform.sampleRate > 0u && waveform.channelCount > 0u
            && waveform.loadedFrameCount > 0u;
        source->sourceSampleRate = waveform.sampleRate;
        source->sourceChannelCount = waveform.channelCount;
        source->sourceBitsPerSample = waveform.bitsPerSample;
        source->sourceFrameCount = waveform.sourceFrameCount;
        source->loadedFrameCount = waveform.loadedFrameCount;
        source->sourceDataByteCount = waveform.sourceDataByteCount;
        source->truncated = source->waveform
            ? waveform.truncated
            : source->originalByteCount > static_cast<uint64_t>(loaded);
        return source;
    } catch (...) {
        return {};
    }
}

inline std::shared_ptr<const PsdRawFieldSource> makePsdRawFieldSource(
    const uint8_t* data,
    std::size_t size,
    uint64_t originalByteCount = 0u,
    const PsdRawFieldWaveformInfo& waveform = {})
{
    if (!data || size == 0u) return {};
    try {
        const std::size_t loaded = std::min(size, kPsdRawFieldMaxSourceBytes);
        std::vector<uint8_t> bytes(data, data + loaded);
        return makePsdRawFieldSource(std::move(bytes), originalByteCount > 0u ? originalByteCount : size, waveform);
    } catch (...) {
        return {};
    }
}

enum class PsdRawFieldCodecMode : uint32_t {
    RawPcm = 0,
    DeltaPcm = 1,
    Adpcm = 2,
    MuLaw = 3,
    ALaw = 4,
    CelpScramble = 5,
    DiscConceal = 6,
    Cvsd = 7,
    SubbandAdpcm = 8,
    LpcPulse = 9,
    BlockTransform = 10,
    Predictive = 11,
    ModemFsk = 12,
    FaxQam = 13,
    SigmaOneBit = 14,
    HybridPredictive = 15,
};
constexpr uint32_t kPsdRawFieldCodecModeCount = 16;

enum class PsdRawFieldChannelScheme : uint32_t {
    Parallel = 0,
    Deinterleave = 1,
    Planes = 2,
    Shuffled = 3,
    Divergent = 4,
};

struct PsdRawFieldParams {
    float scanRate = 0.44f;
    float texture = 0.62f;
    float geometry = 0.64f;
    float chaos = 0.58f;
    float fold = 0.68f;
    float evolve = 0.0f;
    PsdRawFieldChannelScheme channelScheme = PsdRawFieldChannelScheme::Deinterleave;
    float channelSpread = 0.72f;
    PsdRawFieldCodecMode codecMode = PsdRawFieldCodecMode::MuLaw;
    float codecRate = 0.35f;
    float bitDepth = 8.0f;
    float codecDamage = 0.28f;
    float drive = 0.68f;
    float shred = 0.58f;
    float resonance = 0.18f;
    float gainDb = -12.0f;
    uint32_t seed = 0x50434431u;
    PsdRawFieldCodecMode fieldCodecMode = PsdRawFieldCodecMode::MuLaw;
};

class PsdRawField {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        pitchSmoothing_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.006));
        for (uint32_t k = 0u; k < kPsdRawFieldTransformSize; ++k) {
            for (uint32_t n = 0u; n < kPsdRawFieldTransformSize; ++n) {
                transformCosine_[k][n] = std::cos(
                    kPi / static_cast<float>(kPsdRawFieldTransformSize)
                    * (static_cast<float>(n) + 0.5f) * static_cast<float>(k));
            }
        }
        shaper_.prepare(sampleRate_, kPsdRawFieldChannels);
        setParams(params_);
        reset();
        ready_ = true;
    }

    void reset()
    {
        tapeSeed_ = params_.seed ? params_.seed : 0x50434431u;
        if (source_ && !source_->bytes.empty()) {
            tape_ = source_->bytes;
        } else {
            tape_.resize(kPsdRawFieldTapeSize);
            for (uint32_t i = 0; i < kPsdRawFieldTapeSize; ++i) {
                tape_[i] = renderVirtualByte(i);
            }
        }
        tapeMask_ = static_cast<uint32_t>(tape_.size() - 1u);
        waveformSource_ = source_ && source_->waveform && tape_.size() >= kPsdRawFieldChannels;
        cursorSize_ = waveformSource_
            ? static_cast<uint32_t>(tape_.size() / kPsdRawFieldChannels)
            : static_cast<uint32_t>(tape_.size());
        cursorMask_ = cursorSize_ - 1u;
        evolveSeed_ = hash(tapeSeed_ ^ 0xb5297a4du);
        evolveRegionStart_ = 0u;
        evolveRegionLength_ = 0u;
        evolveRegionCounter_ = 0u;
        evolveBlend_ = 0.0f;
        evolveWaitSamples_ = params_.evolve > 0.0001f ? 0.0f : static_cast<float>(sampleRate_);
        evolveActive_ = false;
        for (uint32_t ch = 0; ch < kPsdRawFieldChannels; ++ch) {
            cursor_[ch] = waveformSource_
                ? 0.0
                : static_cast<double>((ch * 7919u + 257u) & cursorMask_);
            prev_[ch] = 0.0f;
            textureState_[ch] = 0.0f;
            dcX_[ch] = 0.0f;
            dcY_[ch] = 0.0f;
            connectStart_[ch] = 0.0f;
            connectTarget_[ch] = 0.0f;
            connectCurrent_[ch] = 0.0f;
            connectPhase_[ch] = 1.0f;
            connectInc_[ch] = 1.0f;
            connectSeed_[ch] = hash(tapeSeed_ ^ (ch * 0x9e3779b9u) ^ 0x31415926u);
            breakpoint_[ch] = BreakpointState {};
            breakpoint_[ch].seed = hash(tapeSeed_ ^ (ch * 0x7f4a7c15u) ^ 0x27182818u);
            breakpoint_[ch].currentAmp = readTapeAudio(distributedPosition(ch * 4099u, ch, 0u));
            breakpoint_[ch].nextAmp = readTapeAudio(distributedPosition(ch * 4099u + 257u, ch, 1u));
            breakpoint_[ch].phase = 1.0f;
            breakpoint_[ch].inc = 1.0f;
            hold_[ch] = 0.0f;
            held_[ch] = 0.0f;
            predictor_[ch] = 0.0f;
            step_[ch] = 0.035f;
            frameDrop_[ch] = false;
            codeGain_[ch] = 0.5f;
            codePitch_[ch] = 48u;
            pitch_[ch].fill(0.0f);
            pitchWrite_[ch] = 0u;
            celpLossSamples_[ch] = 0u;
            celpRecoveryRemaining_[ch] = 0u;
            celpRecoveryTotal_[ch] = 1u;
            celpRecoveryStart_[ch] = 0.0f;
            celpLastOutput_[ch] = 0.0f;
            codecSample_[ch] = 0u;
            heldTick_[ch] = true;
            discHistory_[ch].fill(0.0f);
            discLossHistory_[ch].fill(false);
            discWrite_[ch] = 0u;
            discBuffered_[ch] = 0u;
            discErrorRemaining_[ch] = 0u;
            discErrorTotal_[ch] = 1u;
            discErrorKind_[ch] = 0u;
            discRepeatLength_[ch] = 32u;
            discLastOutput_[ch] = 0.0f;
            discSlope_[ch] = 0.0f;
            discErrorStart_[ch] = 0.0f;
            discWasConcealing_[ch] = false;
            discRecoveryRemaining_[ch] = 0u;
            discRecoveryTotal_[ch] = 1u;
            discRecoveryStart_[ch] = 0.0f;
            cvsdIntegrator_[ch] = 0.0f;
            cvsdStep_[ch] = 0.02f;
            cvsdLastBit_[ch] = false;
            cvsdRun_[ch] = 0u;
            cvsdClock_[ch] = 0.0f;
            cvsdOutput_[ch] = 0.0f;
            subbandLowState_[ch] = 0.0f;
            subbandLowPredictor_[ch] = 0.0f;
            subbandHighPredictor_[ch] = 0.0f;
            subbandLowStep_[ch] = 0.025f;
            subbandHighStep_[ch] = 0.02f;
            subbandEvenOutput_[ch] = 0.0f;
            subbandOddOutput_[ch] = 0.0f;
            subbandPairPhase_[ch] = false;
            lpcInputHistory_[ch].fill(0.0f);
            lpcSynthHistory_[ch].fill(0.0f);
            lpcCoefficients_[ch].fill(0.0f);
            lpcEnergy_[ch] = 0.01f;
            lpcPhase_[ch] = 0.0f;
            transformInput_[ch].fill(0.0f);
            transformOutput_[ch].fill(0.0f);
            transformWrite_[ch] = 0u;
            transformRead_[ch] = 0u;
            transformReady_[ch] = false;
            transformBlock_[ch] = 0u;
            transformLossBlocks_[ch] = 0u;
            predictiveOne_[ch] = 0.0f;
            predictiveTwo_[ch] = 0.0f;
            predictiveScale_[ch] = 0.08f;
            hybridOne_[ch] = 0.0f;
            hybridTwo_[ch] = 0.0f;
            hybridError_[ch] = 0.0f;
            modemPhase_[ch] = 0.0f;
            modemClock_[ch] = 0.0f;
            modemFrequency_[ch] = 900.0f + static_cast<float>(ch) * 37.0f;
            modemAmplitude_[ch] = 0.2f;
            faxPhase_[ch] = 0.0f;
            faxClock_[ch] = 0.0f;
            faxI_[ch] = 0.0f;
            faxQ_[ch] = 0.0f;
            faxPreviousI_[ch] = 0.0f;
            faxPreviousQ_[ch] = 0.0f;
            faxTargetI_[ch] = 0.0f;
            faxTargetQ_[ch] = 0.0f;
            faxPreviousInput_[ch] = 0.0f;
            faxTrainingRemaining_[ch] = 16u;
            faxSymbol_[ch] = 0u;
            faxWasDrop_[ch] = false;
            sigmaError_[ch] = 0.0f;
            sigmaOutput_[ch] = 0.0f;
            sigmaClock_[ch] = 0.0f;
            sigmaLastBit_[ch] = false;
        }
        shaper_.reset();
        sectionPhase_ = 0.0f;
        frameSample_ = 0u;
        codecFrameCounter_ = 0u;
        loudnessEnergy_ = 0.04f;
        loudnessGain_ = 1.0f;
        currentPitchRatio_ = targetPitchRatio_;
    }

    void setParams(const PsdRawFieldParams& params)
    {
        const PsdRawFieldParams sanitized = sanitize(params);
        if (params_.evolve <= 0.0001f && sanitized.evolve > 0.0001f && !evolveActive_) {
            evolveWaitSamples_ = 0.0f;
        }
        params_ = sanitized;
    }

    PsdRawFieldParams params() const { return params_; }
    bool ready() const { return ready_; }
    void setPitchRatio(float ratio) { targetPitchRatio_ = clamp(ratio, 1.0f / 64.0f, 64.0f); }
    float pitchRatio() const { return targetPitchRatio_; }
    void setSource(std::shared_ptr<const PsdRawFieldSource> source) { source_ = std::move(source); }
    std::shared_ptr<const PsdRawFieldSource> source() const { return source_; }
    uint32_t tapeSize() const { return static_cast<uint32_t>(tape_.size()); }
    uint8_t byteAt(uint32_t index) const
    {
        return static_cast<uint8_t>(clamp((readTapeAudio(index) * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
    }
    float cursorPosition(uint32_t channel) const
    {
        return channel < kPsdRawFieldChannels ? static_cast<float>(cursor_[channel]) : 0.0f;
    }

    void randomizeByteField(uint32_t salt)
    {
        params_.seed = hash(params_.seed ^ salt ^ 0x7f4a7c15u);
        reset();
    }

    void process(float* const* output, uint32_t outputChannels, uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        const uint32_t channels = std::min<uint32_t>(outputChannels, kPsdRawFieldChannels);
        const float gain = dbToGain(params_.gainDb);
        constexpr float dcR = 0.9975f;
        const float baseStep = waveformSource_
            ? static_cast<float>(source_->sourceSampleRate / sampleRate_)
                * std::pow(2.0f, (params_.scanRate - 0.5f) * 16.0f)
            : 0.00002f * std::pow(375000.0f, params_.scanRate);
        const float sectionRate = lerp(0.0000003f, 0.00008f, params_.scanRate * params_.scanRate);
        const uint32_t frameSamples = static_cast<uint32_t>(std::max(1.0, sampleRate_ * 0.010));
        const float shapeMix = clamp(std::max({ params_.drive * 0.9f, params_.shred, params_.resonance }), 0.0f, 1.0f);
        const float calibrationGain = dbToGain(calibrationDb());
        const float energyAttack = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.12));
        const float energyRelease = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.90));
        const float gainSmoothing = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate_ * 0.60));
        configureShaper();

        for (uint32_t i = 0; i < frames; ++i) {
            currentPitchRatio_ += (targetPitchRatio_ - currentPitchRatio_) * pitchSmoothing_;
            advanceEvolution();
            sectionPhase_ += sectionRate;
            sectionPhase_ -= std::floor(sectionPhase_);
            const float section = triangle(sectionPhase_);
            const bool newCodecFrame = frameSample_ == 0u;
            std::array<float, kPsdRawFieldChannels> shapedInput {};
            std::array<float, kPsdRawFieldChannels> shapedOutput {};

            for (uint32_t ch = 0; ch < channels; ++ch) {
                const float planeSkew = 1.0f
                    + (static_cast<float>(ch) - 3.5f) * 0.014f * params_.chaos * params_.channelSpread;
                const float rowBump = waveformSource_
                    ? 1.0f
                    : 1.0f + 0.48f * params_.texture * pulse(rowPhase(ch), 0.035f);
                cursor_[ch] += baseStep * currentPitchRatio_ * planeSkew * rowBump;
                while (cursor_[ch] >= static_cast<double>(cursorSize_)) {
                    cursor_[ch] -= static_cast<double>(cursorSize_);
                }

                const uint32_t basePos = static_cast<uint32_t>(cursor_[ch]) & cursorMask_;
                const uint32_t pos = distributedPosition(basePos, ch, 0u);
                const uint32_t stride = 1u + ((ch * 17u) & 31u);
                const float a = readTapeAudio(pos);
                const float b = readTapeAudio(distributedPosition(basePos + stride, ch, 1u));
                const float c = readTapeAudio(distributedPosition(basePos + 257u + ch * 97u, ch, 2u));

                const float localSmoothing = lerp(0.84f, 0.16f, params_.texture);
                float raw = lerp(a, (a + b) * 0.5f, localSmoothing);
                raw = lerp(raw, c, params_.texture * (0.20f + 0.62f * section));
                raw += metadataTick(pos, ch) * params_.texture * params_.texture * 0.65f;

                raw = breakpointStage(raw, ch, pos);
                const float textureCoeff = lerp(0.001f, 0.85f, params_.texture * params_.texture);
                textureState_[ch] += (raw - textureState_[ch]) * textureCoeff;
                raw = lerp(textureState_[ch], raw, params_.texture);
                const float folded = std::sin(raw * (1.0f + params_.fold * 8.5f));
                raw = lerp(raw, folded, params_.fold);
                raw = connectionStage(raw, ch, pos);
                raw = softClip(raw * 1.55f);

                const float smoothAmount = lerp(0.28f, 0.06f, params_.texture);
                const float smoothed = lerp(raw, prev_[ch], smoothAmount);
                prev_[ch] = smoothed;
                const float dc = smoothed - dcX_[ch] + dcR * dcY_[ch];
                dcX_[ch] = smoothed;
                dcY_[ch] = dc;
                if (newCodecFrame) updateCodecFrame(ch, pos);
                shapedInput[ch] = codecStage(dc, ch);
            }

            if (shapeMix > 0.0001f) shaper_.processFrame(shapedInput.data(), shapedOutput.data());
            else shapedOutput = shapedInput;

            std::array<float, kPsdRawFieldChannels> shapedFrame {};
            float mean = 0.0f;
            float originalEnergy = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                shapedFrame[ch] = lerp(shapedInput[ch], shapedOutput[ch], shapeMix);
                mean += shapedFrame[ch];
                originalEnergy += shapedFrame[ch] * shapedFrame[ch];
            }
            mean /= static_cast<float>(std::max<uint32_t>(1u, channels));
            originalEnergy /= static_cast<float>(std::max<uint32_t>(1u, channels));

            float spatialEnergy = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                const float residual = shapedFrame[ch] - mean;
                shapedFrame[ch] = mean + residual * params_.channelSpread;
                spatialEnergy += shapedFrame[ch] * shapedFrame[ch];
            }
            spatialEnergy /= static_cast<float>(std::max<uint32_t>(1u, channels));
            const float spreadGain = clamp(std::sqrt((originalEnergy + 1.0e-8f) / (spatialEnergy + 1.0e-8f)), 0.70f, 2.25f);

            float calibratedEnergy = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                shapedFrame[ch] *= spreadGain * calibrationGain;
                calibratedEnergy += shapedFrame[ch] * shapedFrame[ch];
            }
            calibratedEnergy /= static_cast<float>(std::max<uint32_t>(1u, channels));
            const float energyCoeff = calibratedEnergy > loudnessEnergy_ ? energyAttack : energyRelease;
            loudnessEnergy_ += (calibratedEnergy - loudnessEnergy_) * energyCoeff;
            float loudnessTarget = 1.0f;
            if (loudnessEnergy_ > 1.0e-7f) {
                loudnessTarget = clamp(0.20f / std::sqrt(loudnessEnergy_), 0.75f, 1.35f);
            }
            loudnessGain_ += (loudnessTarget - loudnessGain_) * gainSmoothing;

            for (uint32_t ch = 0; ch < channels; ++ch) {
                if (output[ch]) output[ch][i] = softClip(shapedFrame[ch] * loudnessGain_ * gain);
            }
            for (uint32_t ch = channels; ch < outputChannels; ++ch) {
                if (output[ch]) output[ch][i] = 0.0f;
            }
            if (newCodecFrame) ++codecFrameCounter_;
            frameSample_ = (frameSample_ + 1u) % frameSamples;
        }
    }

private:
    enum class Distribution : uint32_t {
        Uniform = 0,
        Gaussian = 1,
        Cauchy = 2,
        Logistic = 3,
        Arcsine = 4,
        Exponential = 5,
        Binary = 6,
    };

    struct BreakpointState {
        uint32_t seed = 1u;
        float currentAmp = 0.0f;
        float nextAmp = 0.0f;
        float phase = 1.0f;
        float inc = 1.0f;
        float last = 0.0f;
        float tendencyCenter = 0.0f;
        bool resting = false;
    };

    static PsdRawFieldParams sanitize(PsdRawFieldParams p)
    {
        p.scanRate = clamp(p.scanRate, 0.0f, 1.0f);
        p.texture = clamp(p.texture, 0.0f, 1.0f);
        p.geometry = clamp(p.geometry, 0.0f, 1.0f);
        p.chaos = clamp(p.chaos, 0.0f, 1.0f);
        p.fold = clamp(p.fold, 0.0f, 1.0f);
        p.evolve = clamp(p.evolve, 0.0f, 1.0f);
        p.channelScheme = static_cast<PsdRawFieldChannelScheme>(std::min<uint32_t>(4u, static_cast<uint32_t>(p.channelScheme)));
        p.channelSpread = clamp(p.channelSpread, 0.0f, 1.0f);
        p.codecMode = static_cast<PsdRawFieldCodecMode>(std::min<uint32_t>(
            kPsdRawFieldCodecModeCount - 1u, static_cast<uint32_t>(p.codecMode)));
        p.codecRate = clamp(p.codecRate, 0.0f, 1.0f);
        p.bitDepth = clamp(p.bitDepth, 2.0f, 16.0f);
        p.codecDamage = clamp(p.codecDamage, 0.0f, 1.0f);
        p.drive = clamp(p.drive, 0.0f, 1.0f);
        p.shred = clamp(p.shred, 0.0f, 1.0f);
        p.resonance = clamp(p.resonance, 0.0f, 1.0f);
        p.gainDb = clamp(p.gainDb, -60.0f, 6.0f);
        p.fieldCodecMode = static_cast<PsdRawFieldCodecMode>(std::min<uint32_t>(
            kPsdRawFieldCodecModeCount - 1u, static_cast<uint32_t>(p.fieldCodecMode)));
        return p;
    }

    void configureShaper()
    {
        MacroShredParams p {};
        p.inputGainDb = lerp(-3.0f, 36.0f, params_.drive);
        p.pressure = clamp(params_.drive * 0.82f + params_.shred * 0.18f, 0.0f, 1.0f);
        p.shred = params_.shred;
        p.feedback = params_.resonance * 0.90f;
        p.color = lerp(0.42f, 0.72f, params_.texture);
        p.react = lerp(0.22f, 0.62f, params_.chaos);
        p.tune = 0.94f;
        p.body = lerp(0.70f, 0.42f, params_.shred);
        p.spread = params_.channelSpread * 0.75f;
        p.deviation = params_.channelSpread * 0.82f;
        p.skew = (params_.chaos - 0.5f) * 0.8f;
        p.center = 0.5f;
        p.glideMs = lerp(180.0f, 35.0f, params_.chaos);
        p.mix = 1.0f;
        p.outputGainDb = -3.0f - 7.0f * clamp(params_.drive * 0.72f + params_.shred * 0.28f, 0.0f, 1.0f);
        shaper_.setParams(p);
    }

    float calibrationDb() const
    {
        float db = 9.0f * (params_.codecRate * params_.codecRate - 0.35f * 0.35f);
        db -= 3.0f * (params_.drive - 0.68f);
        db += 3.5f * (params_.shred - 0.58f);
        db -= 2.0f * (params_.resonance - 0.18f);
        db += 2.0f * (params_.geometry - 0.64f);
        if (params_.codecMode == PsdRawFieldCodecMode::CelpScramble) {
            db += 5.0f - 11.0f * (params_.codecDamage - 0.28f);
        } else if (params_.codecMode == PsdRawFieldCodecMode::LpcPulse) {
            db += 2.5f - 4.0f * params_.codecDamage;
        } else if (params_.codecMode == PsdRawFieldCodecMode::BlockTransform) {
            db += 2.0f;
        } else if (params_.codecMode == PsdRawFieldCodecMode::ModemFsk
            || params_.codecMode == PsdRawFieldCodecMode::FaxQam
            || params_.codecMode == PsdRawFieldCodecMode::SigmaOneBit) {
            db -= 2.5f;
        }
        return clamp(db, -8.0f, 10.0f);
    }

    uint32_t distributedPosition(uint32_t pos, uint32_t ch, uint32_t lane) const
    {
        if (waveformSource_) return distributedWaveformPosition(pos, ch, lane);
        pos &= tapeMask_;
        uint64_t distributed = pos;
        switch (params_.channelScheme) {
        case PsdRawFieldChannelScheme::Deinterleave:
            distributed = static_cast<uint64_t>(pos) * kPsdRawFieldChannels + ch + lane * 257u;
            break;
        case PsdRawFieldChannelScheme::Planes:
            distributed = static_cast<uint64_t>(pos)
                + static_cast<uint64_t>(ch) * (tape_.size() / kPsdRawFieldChannels)
                + lane * 1021u;
            break;
        case PsdRawFieldChannelScheme::Shuffled:
            distributed = hash(pos ^ tapeSeed_ ^ (ch * 0x9e3779b9u) ^ (lane * 0x85ebca6bu));
            break;
        case PsdRawFieldChannelScheme::Divergent:
            distributed = static_cast<uint64_t>(pos) * (3u + ch * 2u + lane * 11u)
                + ch * 4099u + lane * 8191u;
            break;
        case PsdRawFieldChannelScheme::Parallel:
        default:
            distributed = pos + ch * 17u * static_cast<uint32_t>(lane + 1u);
            break;
        }
        const double wrapped = static_cast<double>(distributed & tapeMask_);
        const uint32_t mixed = static_cast<uint32_t>(
            static_cast<double>(pos) + (wrapped - static_cast<double>(pos)) * params_.channelSpread);
        return mixed & tapeMask_;
    }

    uint32_t distributedWaveformPosition(uint32_t pos, uint32_t ch, uint32_t lane) const
    {
        pos &= cursorMask_;
        uint64_t distributedFrame = pos;
        uint32_t distributedLane = ch & (kPsdRawFieldChannels - 1u);
        switch (params_.channelScheme) {
        case PsdRawFieldChannelScheme::Parallel:
            distributedLane = 0u;
            break;
        case PsdRawFieldChannelScheme::Planes:
            distributedFrame = static_cast<uint64_t>(pos)
                + static_cast<uint64_t>(ch) * (cursorSize_ / kPsdRawFieldChannels);
            break;
        case PsdRawFieldChannelScheme::Shuffled:
            distributedFrame = hash(pos ^ tapeSeed_ ^ (ch * 0x9e3779b9u) ^ (lane * 0x85ebca6bu));
            break;
        case PsdRawFieldChannelScheme::Divergent:
            distributedFrame = static_cast<uint64_t>(pos) * (3u + ch * 2u + lane * 11u)
                + ch * 4099u + lane * 8191u;
            break;
        case PsdRawFieldChannelScheme::Deinterleave:
        default:
            break;
        }
        distributedFrame &= cursorMask_;
        const double mixedFrame = static_cast<double>(pos)
            + (static_cast<double>(distributedFrame) - static_cast<double>(pos)) * params_.channelSpread;
        return ((static_cast<uint32_t>(mixedFrame) & cursorMask_) * kPsdRawFieldChannels
            + distributedLane) & tapeMask_;
    }

    uint8_t renderVirtualByte(uint32_t index) const
    {
        return renderVirtualByte(index, tapeSeed_);
    }

    uint8_t renderVirtualByte(uint32_t index, uint32_t seed) const
    {
        const uint32_t row = index >> 8u;
        const uint32_t x = index & 255u;

        switch (params_.fieldCodecMode) {
        case PsdRawFieldCodecMode::DeltaPcm: {
            constexpr uint32_t segmentSize = 128u;
            const uint32_t segment = index / segmentSize;
            const float phase = static_cast<float>(index & (segmentSize - 1u))
                / static_cast<float>(segmentSize - 1u);
            const float direction = (hash(segment ^ seed) & 1u) ? 1.0f : -1.0f;
            const float slope = (phase * 2.0f - 1.0f) * direction;
            const float offset = fieldNoise(segment ^ seed ^ 0xa511e9b3u) * 0.24f;
            const float staircase = (static_cast<float>((index >> 3u) & 7u) / 7.0f - 0.5f) * 0.10f;
            return fieldByte(slope * 0.76f + offset + staircase);
        }
        case PsdRawFieldCodecMode::Adpcm: {
            const uint32_t block = index >> 7u;
            const float phase = static_cast<float>(index & 127u) * (1.0f / 128.0f);
            const float envelope = 0.42f + hash01(block ^ seed) * 0.52f;
            const float curve = std::sin(2.0f * kPi * phase)
                + std::sin(6.0f * kPi * phase + fieldNoise(block ^ seed) * 0.8f) * 0.28f;
            const float transient = (index & 127u) < 5u
                ? fieldNoise(block ^ seed ^ 0x68e31da4u) * (1.0f - phase * 25.6f)
                : 0.0f;
            return fieldByte(curve * envelope * 0.68f + transient * 0.55f);
        }
        case PsdRawFieldCodecMode::MuLaw: {
            const uint32_t local = index & 255u;
            const float detail = std::sin(2.0f * kPi * static_cast<float>(index % 37u) / 37.0f)
                + interpolatedFieldNoise(index, 23u, seed ^ 0x632be59bu) * 0.55f;
            const float quiet = detail * (0.055f + hash01((index >> 8u) ^ seed) * 0.15f);
            if (local < 3u) return fieldByte((hash(row ^ seed) & 1u) ? 0.98f : -0.98f);
            return fieldByte(quiet);
        }
        case PsdRawFieldCodecMode::ALaw: {
            const uint32_t local = index & 255u;
            const float envelope = 0.16f + 0.36f * triangle(
                static_cast<float>(local) * (1.0f / 256.0f));
            const float speechLike = std::sin(2.0f * kPi * static_cast<float>(index % 53u) / 53.0f)
                + 0.34f * std::sin(2.0f * kPi * static_cast<float>(index % 17u) / 17.0f);
            const float edge = local == 0u ? fieldNoise(row ^ seed) * 0.82f : 0.0f;
            return fieldByte(speechLike * envelope + edge);
        }
        case PsdRawFieldCodecMode::CelpScramble: {
            const uint32_t frame = index / 160u;
            const uint32_t local = index % 160u;
            const uint32_t pitch = 27u + (hash(frame ^ seed) % 69u);
            const float phase = static_cast<float>(local % pitch) / static_cast<float>(pitch);
            const float voiced = std::sin(2.0f * kPi * phase)
                + 0.36f * std::sin(4.0f * kPi * phase + 0.4f)
                + (phase * 2.0f - 1.0f) * 0.22f;
            const bool unvoiced = (hash(frame ^ seed ^ 0x91e10da5u) & 3u) == 0u;
            const float excitation = unvoiced ? fieldNoise(index ^ seed) * 0.62f : voiced * 0.56f;
            return fieldByte(excitation * (0.48f + hash01(frame ^ seed ^ 0xb5297a4du) * 0.46f));
        }
        case PsdRawFieldCodecMode::DiscConceal: {
            const uint32_t sector = index >> 8u;
            const uint32_t motifSector = sector & ~3u;
            const uint32_t motifGroup = sector >> 2u;
            const float phase = static_cast<float>(x) * (1.0f / 256.0f);
            if (x < 8u) {
                const uint32_t marker = x ^ motifSector ^ seed;
                return (marker & 1u) ? 0xf3u : 0x0cu;
            }
            const float motif = std::sin(2.0f * kPi * phase * (2.0f + static_cast<float>(motifGroup & 3u)))
                + 0.32f * bipolarTriangle(phase * 4.0f - std::floor(phase * 4.0f));
            return fieldByte(motif * 0.62f + fieldNoise((x >> 4u) ^ motifSector ^ seed) * 0.10f);
        }
        case PsdRawFieldCodecMode::Cvsd: {
            constexpr uint32_t segmentSize = 64u;
            const uint32_t segment = index >> 6u;
            const float phase = static_cast<float>(index & 63u) * (1.0f / 63.0f);
            const float direction = (hash(segment ^ seed) & 1u) ? 1.0f : -1.0f;
            float slope = (phase * 2.0f - 1.0f) * direction;
            if ((segment % 5u) == 0u) slope = std::round(slope * 3.0f) / 3.0f;
            return fieldByte(slope * (0.38f + hash01(segment ^ seed ^ 0xd1b54a35u) * 0.52f));
        }
        case PsdRawFieldCodecMode::SubbandAdpcm: {
            const float low = interpolatedFieldNoise(index, 384u, seed ^ 0x7f4a7c15u) * 0.72f;
            const float alternating = ((index + (hash(row ^ seed) & 1u)) & 1u) ? 1.0f : -1.0f;
            const float burstEnvelope = (row & 7u) < 3u ? 0.28f : 0.07f;
            const float high = alternating * burstEnvelope
                + fieldNoise(index ^ seed ^ 0x68e31da4u) * burstEnvelope * 0.45f;
            return fieldByte(low + high);
        }
        case PsdRawFieldCodecMode::LpcPulse: {
            const uint32_t phrase = index >> 8u;
            const uint32_t pitch = 43u + (hash(phrase ^ seed) % 54u);
            const float phase = static_cast<float>(index % pitch) / static_cast<float>(pitch);
            const float decay = std::exp(-phase * 6.5f);
            const float resonances = std::sin(6.0f * kPi * phase) * 0.62f
                + std::sin(10.0f * kPi * phase + 0.7f) * 0.30f;
            const bool breath = (hash(phrase ^ seed ^ 0x85ebca6bu) & 7u) == 0u;
            const float value = breath
                ? fieldNoise(index ^ seed) * (0.18f + decay * 0.38f)
                : resonances * decay;
            return fieldByte(value * (0.58f + hash01(phrase ^ seed) * 0.34f));
        }
        case PsdRawFieldCodecMode::BlockTransform: {
            const uint32_t block = index >> 5u;
            const uint32_t n = index & (kPsdRawFieldTransformSize - 1u);
            const uint32_t blockHash = hash(block ^ seed);
            const uint32_t binOne = 1u + (blockHash & 3u);
            const uint32_t binTwo = 5u + ((blockHash >> 4u) & 7u);
            const float position = static_cast<float>(n) + 0.5f;
            float value = std::cos(kPi * position * static_cast<float>(binOne)
                    / static_cast<float>(kPsdRawFieldTransformSize)) * 0.68f
                + std::cos(kPi * position * static_cast<float>(binTwo)
                    / static_cast<float>(kPsdRawFieldTransformSize)) * 0.27f;
            if ((block & 15u) == 0u && n == 0u) value = fieldNoise(blockHash) * 0.98f;
            return fieldByte(value);
        }
        case PsdRawFieldCodecMode::Predictive: {
            const float base = interpolatedFieldNoise(index, 192u, seed ^ 0x45d9f3bu) * 0.78f;
            const float curvature = std::sin(2.0f * kPi * static_cast<float>(index % 311u) / 311.0f) * 0.18f;
            const uint32_t local = index & 255u;
            const float innovation = local < 6u
                ? fieldNoise(row ^ seed ^ 0xa511e9b3u) * (1.0f - static_cast<float>(local) / 6.0f) * 0.72f
                : 0.0f;
            return fieldByte(base + curvature + innovation);
        }
        case PsdRawFieldCodecMode::ModemFsk: {
            const uint32_t symbol = index >> 4u;
            const uint32_t localSymbol = symbol & 31u;
            uint32_t state = hash(symbol ^ seed) & 3u;
            if (localSymbol < 8u) state = localSymbol & 1u ? 3u : 0u;
            constexpr float levels[] = { -0.92f, -0.31f, 0.31f, 0.92f };
            return fieldByte(levels[state]);
        }
        case PsdRawFieldCodecMode::FaxQam: {
            const uint32_t line = index >> 8u;
            const uint32_t repeatedLine = line >> 2u;
            if (x < 8u) return (x & 1u) ? 0xffu : 0x00u;
            float pixel = 0.0f;
            switch (repeatedLine & 3u) {
            case 0u: pixel = ((x >> 4u) & 1u) ? 0.82f : -0.82f; break;
            case 1u: pixel = static_cast<float>(x) * (2.0f / 255.0f) - 1.0f; break;
            case 2u: pixel = (((x >> 3u) ^ repeatedLine) & 1u) ? 0.72f : -0.72f; break;
            case 3u:
            default:
                pixel = std::abs(static_cast<int32_t>(x) - 128) < static_cast<int32_t>(24u + (hash(repeatedLine ^ seed) & 63u))
                    ? -0.88f : 0.74f;
                break;
            }
            return fieldByte(pixel);
        }
        case PsdRawFieldCodecMode::SigmaOneBit: {
            const float slow = std::sin(2.0f * kPi * static_cast<float>(index % 2048u) / 2048.0f) * 0.34f;
            const float drift = interpolatedFieldNoise(index, 768u, seed ^ 0x6d2b79f5u) * 0.24f;
            const float fine = std::sin(2.0f * kPi * static_cast<float>(index % 257u) / 257.0f) * 0.07f;
            return fieldByte(slow + drift + fine);
        }
        case PsdRawFieldCodecMode::HybridPredictive: {
            const float base = interpolatedFieldNoise(index, 160u, seed ^ 0xb5297a4du) * 0.72f;
            const float correction = std::sin(2.0f * kPi * static_cast<float>(index % 29u) / 29.0f) * 0.13f
                + fieldNoise(index ^ seed ^ 0x91e10da5u) * 0.08f;
            const float edge = (index & 127u) == 0u ? fieldNoise((index >> 7u) ^ seed) * 0.48f : 0.0f;
            return fieldByte(base + correction + edge);
        }
        case PsdRawFieldCodecMode::RawPcm:
        default: {
            const uint32_t plane = (row >> 2u) & 7u;
            const uint32_t section = (row >> 5u) & 7u;
            const uint32_t h = hash(index ^ seed ^ (plane * 0x9e3779b9u));
            const uint8_t noise = static_cast<uint8_t>(h >> 24u);
            const uint8_t ramp = static_cast<uint8_t>((x * (3u + plane * 2u) + row * 11u) & 255u);
            const uint8_t contour = static_cast<uint8_t>((std::sin(
                static_cast<float>(x) * 0.043f + static_cast<float>(row) * 0.017f + static_cast<float>(plane))
                * 0.5f + 0.5f) * 255.0f);
            const uint8_t mask = (hash((row >> 1u) ^ (x >> 3u) ^ seed) & 1u) ? 0xffu : 0x00u;
            if ((row & 31u) == 0u) return static_cast<uint8_t>((row * 13u + section * 29u) & 255u);
            if ((x & 63u) == 0u) return static_cast<uint8_t>(0x80u | ((row + plane * 19u) & 0x7fu));
            if (section == 0u) return static_cast<uint8_t>(0x38u + ((x + row) & 15u));
            if (section == 1u) return lerpByte(ramp, noise, 0.72f);
            if (section == 2u) return lerpByte(contour, ramp, 0.64f);
            if (section == 3u) return lerpByte(mask, noise, 0.48f);
            return lerpByte(static_cast<uint8_t>(ramp ^ contour), noise, 0.68f);
        }
        }
    }

    static float fieldNoise(uint32_t value) { return hash01(value) * 2.0f - 1.0f; }

    static float interpolatedFieldNoise(uint32_t index, uint32_t period, uint32_t seed)
    {
        period = std::max<uint32_t>(1u, period);
        const uint32_t cell = index / period;
        const float phase = static_cast<float>(index % period) / static_cast<float>(period);
        const float smooth = phase * phase * (3.0f - 2.0f * phase);
        return lerp(fieldNoise(cell ^ seed), fieldNoise((cell + 1u) ^ seed), smooth);
    }

    static float bipolarTriangle(float phase)
    {
        phase -= std::floor(phase);
        return triangle(phase) * 2.0f - 1.0f;
    }

    static uint8_t fieldByte(float value)
    {
        return static_cast<uint8_t>(std::round((clamp(value, -1.0f, 1.0f) * 0.5f + 0.5f) * 255.0f));
    }

    float readTapeAudio(uint32_t index) const
    {
        index &= tapeMask_;
        const float current = byteToAudio(tape_[index]);
        if (!evolveActive_ || evolveRegionLength_ == 0u) return current;
        const uint32_t offset = (index + static_cast<uint32_t>(tape_.size()) - evolveRegionStart_)
            & tapeMask_;
        if (offset >= evolveRegionLength_) return current;
        const float blend = evolveBlend_ * evolveBlend_ * (3.0f - 2.0f * evolveBlend_);
        return lerp(current, byteToAudio(evolveTarget_[offset]), blend);
    }

    void startEvolutionRegion()
    {
        const uint32_t channel = evolveRegionCounter_ % kPsdRawFieldChannels;
        const uint32_t lane = (evolveRegionCounter_ / kPsdRawFieldChannels) & 3u;
        const uint32_t center = distributedPosition(static_cast<uint32_t>(cursor_[channel]), channel, lane);
        evolveRegionLength_ = 256u + static_cast<uint32_t>(1792.0f * params_.evolve * params_.evolve);
        evolveRegionLength_ = std::min<uint32_t>(evolveRegionLength_, 2048u);
        evolveRegionStart_ = (center + static_cast<uint32_t>(tape_.size()) - evolveRegionLength_ / 2u)
            & tapeMask_;
        evolveSeed_ = hash(evolveSeed_ ^ (evolveRegionCounter_ * 0x9e3779b9u) ^ 0x68e31da4u);
        for (uint32_t i = 0; i < evolveRegionLength_; ++i) {
            const uint32_t index = (evolveRegionStart_ + i) & tapeMask_;
            const uint8_t regenerated = renderVirtualByte(index, evolveSeed_);
            const uint8_t mutation = static_cast<uint8_t>(hash(index ^ evolveSeed_ ^ 0xd1b54a35u) >> 24u);
            evolveTarget_[i] = lerpByte(regenerated, mutation, 0.12f + params_.evolve * 0.44f);
        }
        ++evolveRegionCounter_;
        evolveBlend_ = 0.0f;
        evolveActive_ = true;
    }

    void commitEvolutionRegion()
    {
        for (uint32_t i = 0; i < evolveRegionLength_; ++i) {
            const uint32_t index = (evolveRegionStart_ + i) & tapeMask_;
            tape_[index] = evolveTarget_[i];
        }
        evolveBlend_ = 0.0f;
        evolveActive_ = false;
        const float intervalSeconds = lerp(12.0f, 0.35f, params_.evolve * params_.evolve);
        evolveWaitSamples_ = intervalSeconds * static_cast<float>(sampleRate_);
    }

    void advanceEvolution()
    {
        if (evolveActive_) {
            const float morphSeconds = lerp(3.0f, 0.15f, params_.evolve);
            evolveBlend_ += 1.0f / std::max(1.0f, morphSeconds * static_cast<float>(sampleRate_));
            if (evolveBlend_ >= 1.0f) commitEvolutionRegion();
            return;
        }
        if (params_.evolve <= 0.0001f) return;
        evolveWaitSamples_ -= 1.0f;
        if (evolveWaitSamples_ <= 0.0f) startEvolutionRegion();
    }

    float breakpointStage(float raw, uint32_t ch, uint32_t pos)
    {
        if (params_.geometry <= 0.0001f) return raw;
        auto& s = breakpoint_[ch];
        if (s.phase >= 1.0f) {
            s.seed = hash(s.seed ^ pos ^ tapeSeed_ ^ (ch * 0x85ebca6bu));
            const uint32_t flavor = (ch + ((tapeSeed_ >> 9u) & 7u)) % 7u;
            const float ampRand = distributionSample(static_cast<Distribution>(flavor), s.seed, ch, pos);
            const float durRand = distributionSample(static_cast<Distribution>((flavor + 3u) % 7u), s.seed ^ 0xa511e9b3u, ch, pos + 197u);
            const float oracle = readTapeAudio(distributedPosition(pos + (s.seed & 4095u), ch, 3u));
            const float memory = lerp(0.90f, 0.18f, params_.chaos);
            const float tendency = lerp(0.18f, 0.70f, params_.texture);
            const float tendencyTarget = lerp(oracle, s.tendencyCenter, memory);
            s.tendencyCenter = lerp(tendencyTarget, raw, tendency * 0.40f);
            const float step = lerp(0.03f, 1.45f, std::pow(params_.geometry, 1.35f) * lerp(0.45f, 1.0f, params_.chaos));
            s.currentAmp = s.last;
            s.nextAmp = clamp(lerp(s.currentAmp, s.tendencyCenter + ampRand * step, 1.0f - memory * 0.72f), -1.0f, 1.0f);

            const float rate = std::pow(params_.scanRate, 1.2f);
            const float minSamples = lerp(96.0f, 2.0f, rate);
            const float maxSamples = lerp(14000.0f, 72.0f, rate) * lerp(1.0f, 0.42f, params_.chaos);
            const float durShape = clamp(std::abs(durRand), 0.0f, 1.0f);
            const float octaveJump = std::pow(2.0f, params_.chaos * ampRand * 5.0f);
            const float samples = lerp(minSamples, maxSamples, durShape)
                / std::max(0.0625f, octaveJump * currentPitchRatio_);
            s.inc = 1.0f / clamp(samples, 2.0f, 24000.0f);
            s.phase = 0.0f;
            const float restProbability = params_.geometry * std::pow(params_.chaos, 3.0f) * 0.18f;
            s.resting = hash01(s.seed ^ 0x632be59bu) < restProbability;
        }

        s.phase = std::min(1.0f, s.phase + s.inc);
        const float t = s.phase;
        const float smoothT = t * t * (3.0f - 2.0f * t);
        const float badT = std::sin((t + hash01(s.seed ^ 0x91e10da5u) * params_.chaos)
            * kPi * (1.0f + 7.0f * params_.chaos));
        const float curve = lerp(smoothT, badT, params_.chaos * params_.chaos * 0.72f);
        float value = lerp(s.currentAmp, s.nextAmp, curve);
        if (s.resting) value *= lerp(0.05f, 0.30f, 1.0f - params_.chaos);
        const float edge = (s.nextAmp - s.currentAmp) * std::sin(t * kPi) * params_.chaos * 0.75f;
        s.last = softClip(value + edge);
        return lerp(raw, s.last, params_.geometry * 0.92f);
    }

    float connectionStage(float raw, uint32_t ch, uint32_t pos)
    {
        const float amount = clamp((params_.geometry - 0.42f) / 0.58f, 0.0f, 1.0f);
        if (amount <= 0.0001f) return raw;
        const float downVector = std::sqrt(params_.codecRate);
        const float disaster = clamp(params_.chaos * 0.78f + downVector * 0.34f, 0.0f, 1.0f);
        if (connectPhase_[ch] >= 1.0f) {
            connectSeed_[ch] = hash(connectSeed_[ch] ^ pos ^ (ch * 0x45d9f3bu));
            const float r0 = hash01(connectSeed_[ch]);
            const float r1 = hash01(connectSeed_[ch] ^ 0xa511e9b3u);
            const float r2 = hash01(connectSeed_[ch] ^ 0x63d83595u);
            const float rate = clamp(params_.scanRate * 0.68f + params_.chaos * 0.32f, 0.0f, 1.0f);
            const float minPeriod = lerp(96.0f, 3.0f, rate * rate) * lerp(1.0f, 3.0f, downVector);
            const float maxPeriod = lerp(10000.0f, 72.0f, rate * rate) * lerp(1.0f, 1.7f, downVector);
            const float chaosPeriod = lerp((minPeriod + maxPeriod) * 0.5f, lerp(minPeriod, maxPeriod, r0), params_.chaos);
            const float octaveJump = std::pow(2.0f, std::floor(lerp(-3.0f, 4.0f, r1) * params_.chaos));
            const float samples = clamp(
                chaosPeriod / std::max(0.125f, octaveJump * currentPitchRatio_), 2.0f, 16000.0f);
            const uint32_t jump = static_cast<uint32_t>(lerp(1.0f, 4096.0f, params_.chaos * r2));
            connectStart_[ch] = connectCurrent_[ch];
            connectTarget_[ch] = lerp(raw, readTapeAudio(pos + jump + ch * 379u), amount);
            connectPhase_[ch] = 0.0f;
            connectInc_[ch] = 1.0f / samples;
        }

        connectPhase_[ch] = std::min(1.0f, connectPhase_[ch] + connectInc_[ch]);
        const float t = connectPhase_[ch];
        const float smoothT = t * t * (3.0f - 2.0f * t);
        const float badT = std::sin((t + hash01(connectSeed_[ch] ^ 0x91e10da5u) * params_.chaos)
            * kPi * (1.0f + 9.0f * disaster));
        const float steps = lerp(10.0f, 2.0f, downVector);
        const float vectorT = std::floor(t * steps) / std::max(1.0f, steps - 1.0f);
        float curve = lerp(smoothT, badT, disaster);
        curve = lerp(curve, vectorT, downVector * amount);
        const float connected = lerp(connectStart_[ch], connectTarget_[ch], curve);
        connectCurrent_[ch] = softClip(connected + (connected - raw) * disaster * 0.8f);
        return lerp(raw, connectCurrent_[ch], amount);
    }

    float distributionSample(Distribution distribution, uint32_t seed, uint32_t ch, uint32_t pos) const
    {
        const float u0 = clamp(hash01(seed ^ pos ^ (ch * 503u)), 0.0001f, 0.9999f);
        const float u1 = clamp(hash01(seed ^ 0x9e3779b9u ^ (pos * 17u)), 0.0001f, 0.9999f);
        switch (distribution) {
        case Distribution::Gaussian:
            return clamp(std::sqrt(-2.0f * std::log(u0)) * std::cos(2.0f * kPi * u1) * 0.34f, -1.0f, 1.0f);
        case Distribution::Cauchy:
            return clamp(std::tan(kPi * (u0 - 0.5f)) * 0.18f, -1.0f, 1.0f);
        case Distribution::Logistic:
            return clamp(std::log(u0 / (1.0f - u0)) * 0.18f, -1.0f, 1.0f);
        case Distribution::Arcsine:
            return clamp(std::sin(kPi * (u0 - 0.5f)), -1.0f, 1.0f);
        case Distribution::Exponential:
            return clamp((1.0f - std::exp(-5.0f * u0)) * 2.0f - 1.0f, -1.0f, 1.0f);
        case Distribution::Binary:
            return u0 < 0.5f ? -1.0f : 1.0f;
        case Distribution::Uniform:
        default:
            return u0 * 2.0f - 1.0f;
        }
    }

    static float quantize(float x, float bits)
    {
        const float levels = std::max(3.0f, std::pow(2.0f, std::floor(bits)) - 1.0f);
        return std::round(clamp(x, -1.0f, 1.0f) * levels) / levels;
    }

    static float quantizeRange(float x, float bits, float range)
    {
        range = std::max(0.0001f, range);
        return quantize(x / range, bits) * range;
    }

    static uint8_t encodeMuLawCode(float x)
    {
        int32_t sample = static_cast<int32_t>(
            std::round(clamp(x, -1.0f, 1.0f) * 32767.0f));
        const uint8_t mask = sample < 0 ? 0x7fu : 0xffu;
        if (sample < 0) sample = -sample;
        sample = std::min<int32_t>(sample, 32635) + 0x84;

        uint32_t segment = 0u;
        for (int32_t value = sample >> 7; value > 1 && segment < 7u; value >>= 1) {
            ++segment;
        }
        const uint8_t mantissa = static_cast<uint8_t>((sample >> (segment + 3u)) & 0x0f);
        return static_cast<uint8_t>(((segment << 4u) | mantissa) ^ mask);
    }

    static float decodeMuLawCode(uint8_t code)
    {
        const uint8_t value = static_cast<uint8_t>(~code);
        int32_t sample = (static_cast<int32_t>(value & 0x0fu) << 3) + 0x84;
        sample <<= (value >> 4u) & 0x07u;
        sample -= 0x84;
        if ((value & 0x80u) != 0u) sample = -sample;
        return clamp(static_cast<float>(sample) / 32768.0f, -1.0f, 1.0f);
    }

    static uint8_t encodeALawCode(float x)
    {
        int32_t sample = static_cast<int32_t>(
            std::round(clamp(x, -1.0f, 1.0f) * 32767.0f));
        const uint8_t mask = sample >= 0 ? 0xd5u : 0x55u;
        if (sample < 0) sample = -sample - 1;
        sample >>= 3;

        constexpr std::array<int32_t, 8> segmentEnds {
            0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff
        };
        uint32_t segment = 0u;
        while (segment < segmentEnds.size() && sample > segmentEnds[segment]) ++segment;
        if (segment >= segmentEnds.size()) return static_cast<uint8_t>(0x7fu ^ mask);

        uint8_t value = static_cast<uint8_t>(segment << 4u);
        value |= static_cast<uint8_t>(
            (sample >> (segment < 2u ? 1u : segment)) & 0x0f);
        return static_cast<uint8_t>(value ^ mask);
    }

    static float decodeALawCode(uint8_t code)
    {
        const uint8_t value = static_cast<uint8_t>(code ^ 0x55u);
        int32_t sample = static_cast<int32_t>(value & 0x0fu) << 4;
        const uint32_t segment = (value & 0x70u) >> 4u;
        if (segment == 0u) {
            sample += 8;
        } else {
            sample += 0x108;
            if (segment > 1u) sample <<= segment - 1u;
        }
        if ((value & 0x80u) == 0u) sample = -sample;
        return clamp(static_cast<float>(sample) / 32768.0f, -1.0f, 1.0f);
    }

    uint8_t damageCodeword(uint8_t code, uint32_t ch, uint32_t salt) const
    {
        const uint32_t retainedBits = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::floor(params_.bitDepth)), 2u, 8u);
        if (retainedBits < 8u) {
            code &= static_cast<uint8_t>(0xffu << (8u - retainedBits));
        }

        const uint32_t h = hash(codecSample_[ch] * 0x9e3779b9u
            ^ tapeSeed_ ^ (ch * 0x85ebca6bu) ^ salt);
        if (hash01(h) < params_.codecDamage * params_.codecDamage * 0.20f) {
            code ^= static_cast<uint8_t>(1u << ((h >> 19u) & 7u));
        }
        return code;
    }

    float compandRoundTrip(float x, bool aLaw, uint32_t ch) const
    {
        uint8_t code = aLaw ? encodeALawCode(x) : encodeMuLawCode(x);
        code = damageCodeword(code, ch, aLaw ? 0xa511e9b3u : 0x632be59bu);
        return aLaw ? decodeALawCode(code) : decodeMuLawCode(code);
    }

    float downsampleStage(float x, uint32_t ch)
    {
        heldTick_[ch] = false;
        if (codecSample_[ch] == 0u) {
            hold_[ch] = 0.0f;
            held_[ch] = x;
            heldTick_[ch] = true;
            return x;
        }
        const float holdSamples = codecInterval();
        hold_[ch] += 1.0f;
        if (hold_[ch] >= holdSamples) {
            hold_[ch] -= holdSamples;
            held_[ch] = x;
            heldTick_[ch] = true;
        }
        return held_[ch];
    }

    float codecInterval() const
    {
        return std::pow(2.0f, params_.codecRate * 14.0f);
    }

    bool codecClockTick(float& clock) const
    {
        const float interval = codecInterval();
        clock += 1.0f;
        if (clock < interval) return false;
        clock -= interval;
        return true;
    }

    void updateCodecFrame(uint32_t ch, uint32_t pos)
    {
        const uint32_t shared = hash(codecFrameCounter_ * 0x9e3779b9u ^ tapeSeed_ ^ 0x6d2b79f5u);
        const uint32_t independent = hash(pos ^ (ch * 0x45d9f3bu) ^ shared);
        const uint32_t selector = hash(shared ^ (ch * 0x27d4eb2du));
        const uint32_t h = hash01(selector) < params_.channelSpread ? independent : shared;
        const float loss = params_.codecDamage * params_.codecDamage * 0.35f;
        frameDrop_[ch] = static_cast<float>(h & 0xffffu) / 65535.0f < loss;
        codeGain_[ch] = lerp(0.15f, 0.9f, static_cast<float>((h >> 16u) & 0xffu) / 255.0f);
        codePitch_[ch] = 24u + ((h >> 24u) & 95u);
        if (params_.codecMode == PsdRawFieldCodecMode::DiscConceal
            && frameDrop_[ch] && discErrorRemaining_[ch] == 0u) {
            const float durationScale = std::pow(2.0f, params_.codecRate * 11.0f);
            const float durationRandom = 0.20f + 0.80f * hash01(h ^ 0xa511e9b3u);
            const uint32_t duration = static_cast<uint32_t>(8.0f + durationScale * durationRandom);
            discErrorTotal_[ch] = std::clamp<uint32_t>(duration, 8u, 4096u);
            discErrorRemaining_[ch] = discErrorTotal_[ch];
            discErrorKind_[ch] = discErrorTotal_[ch] <= kPsdRawFieldDiscLookahead ? 0u
                : discErrorTotal_[ch] <= 256u ? 1u
                : discErrorTotal_[ch] <= 1024u ? 2u
                : 3u;
            discRepeatLength_[ch] = 8u + ((h >> 21u) & 127u);
            discErrorStart_[ch] = discLastOutput_[ch];
        }
    }

    float codecStage(float input, uint32_t ch)
    {
        const float clean = clamp(input, -1.0f, 1.0f);
        const bool ownsClock = params_.codecMode == PsdRawFieldCodecMode::Cvsd
            || params_.codecMode == PsdRawFieldCodecMode::ModemFsk
            || params_.codecMode == PsdRawFieldCodecMode::FaxQam
            || params_.codecMode == PsdRawFieldCodecMode::SigmaOneBit;
        float x = ownsClock ? clean : downsampleStage(clean, ch);
        if (static_cast<uint32_t>(params_.codecMode) < static_cast<uint32_t>(PsdRawFieldCodecMode::CelpScramble)
            && frameDrop_[ch]) {
            x = predictor_[ch] * lerp(0.68f, 0.985f, params_.codecDamage);
        }

        switch (params_.codecMode) {
        case PsdRawFieldCodecMode::DeltaPcm: {
            const float delta = quantize(x - predictor_[ch], params_.bitDepth);
            x = predictor_[ch] + delta * lerp(0.95f, 1.55f, params_.codecDamage);
            break;
        }
        case PsdRawFieldCodecMode::Adpcm: {
            if (!heldTick_[ch]) {
                x = predictor_[ch];
                break;
            }
            const float error = x - predictor_[ch];
            const uint32_t codeBits = std::clamp<uint32_t>(
                static_cast<uint32_t>(std::floor(params_.bitDepth)), 2u, 5u);
            const float levels = static_cast<float>((1u << (codeBits - 1u)) - 1u);
            float q = clamp(
                std::round(error / std::max(step_[ch], 0.0001f)), -levels, levels);
            const uint32_t h = hash(codecSample_[ch] ^ tapeSeed_
                ^ (ch * 0x9e3779b9u) ^ 0x68e31da4u);
            if (hash01(h) < params_.codecDamage * params_.codecDamage * 0.12f) {
                q = clamp(-q + ((h & 1u) ? 1.0f : -1.0f), -levels, levels);
            }
            x = clamp(predictor_[ch] + q * step_[ch], -1.2f, 1.2f);
            const float normalizedCode = std::abs(q) / levels;
            const float stepMultiplier = std::exp(lerp(
                std::log(0.92f),
                std::log(2.20f + params_.codecDamage * 0.55f),
                std::pow(normalizedCode, 1.65f)));
            step_[ch] *= stepMultiplier;
            const float minimumStep = std::pow(2.0f, -std::min(14.0f, params_.bitDepth));
            step_[ch] = clamp(step_[ch], minimumStep, 0.85f);
            predictor_[ch] = x;
            break;
        }
        case PsdRawFieldCodecMode::MuLaw:
            x = compandRoundTrip(x, false, ch);
            break;
        case PsdRawFieldCodecMode::ALaw:
            x = compandRoundTrip(x, true, ch);
            break;
        case PsdRawFieldCodecMode::CelpScramble:
            x = celpScramble(x, ch);
            break;
        case PsdRawFieldCodecMode::DiscConceal:
            x = discConceal(x, ch);
            break;
        case PsdRawFieldCodecMode::Cvsd:
            x = cvsdCodec(clean, ch);
            break;
        case PsdRawFieldCodecMode::SubbandAdpcm:
            x = subbandCodec(x, ch);
            break;
        case PsdRawFieldCodecMode::LpcPulse:
            x = lpcCodec(x, ch);
            break;
        case PsdRawFieldCodecMode::BlockTransform:
            x = transformCodec(x, ch);
            break;
        case PsdRawFieldCodecMode::Predictive:
            x = predictiveCodec(x, ch);
            break;
        case PsdRawFieldCodecMode::ModemFsk:
            x = modemCodec(clean, ch);
            break;
        case PsdRawFieldCodecMode::FaxQam:
            x = faxCodec(clean, ch);
            break;
        case PsdRawFieldCodecMode::SigmaOneBit:
            x = sigmaCodec(clean, ch);
            break;
        case PsdRawFieldCodecMode::HybridPredictive:
            x = hybridCodec(x, ch);
            break;
        case PsdRawFieldCodecMode::RawPcm:
        default:
            x = quantize(x, params_.bitDepth);
            break;
        }

        predictor_[ch] += (x - predictor_[ch]) * lerp(0.18f, 0.82f, params_.codecDamage);
        ++codecSample_[ch];
        return clamp(x, -1.5f, 1.5f);
    }

    float celpScramble(float x, uint32_t ch)
    {
        const uint32_t delay = std::min<uint32_t>(codePitch_[ch], static_cast<uint32_t>(pitch_[ch].size() - 1u));
        const uint32_t read = (pitchWrite_[ch] + static_cast<uint32_t>(pitch_[ch].size()) - delay)
            % static_cast<uint32_t>(pitch_[ch].size());
        float coded = 0.0f;
        if (frameDrop_[ch]) {
            const float adaptive = pitch_[ch][read];
            const float attenuation = std::exp(
                -static_cast<float>(celpLossSamples_[ch])
                / static_cast<float>(sampleRate_ * lerp(0.045f, 0.16f, 1.0f - params_.codecDamage)));
            const float noise = hash01(codecSample_[ch] ^ tapeSeed_
                ^ (ch * 0x85ebca6bu) ^ 0x91e10da5u) * 2.0f - 1.0f;
            coded = clamp((adaptive * 0.96f + noise * codeGain_[ch] * 0.035f)
                * attenuation, -1.0f, 1.0f);
            ++celpLossSamples_[ch];
            celpRecoveryRemaining_[ch] = 0u;
        } else {
            const float adaptive = pitch_[ch][read] * lerp(0.15f, 0.86f, params_.codecDamage);
            const uint32_t codeIndex = static_cast<uint32_t>(
                (std::abs(x) * 32767.0f) + static_cast<float>(ch * 13u))
                & (kPsdRawFieldCodebookSize - 1u);
            const float code = readTapeAudio(codeIndex * 1543u + ch * 257u + tapeSeed_);
            const float excitation = lerp(
                x,
                adaptive + code * codeGain_[ch],
                lerp(0.20f, 1.0f, params_.codecDamage));
            coded = compandRoundTrip(excitation, (ch & 1u) != 0u, ch);

            if (celpLossSamples_[ch] > 0u) {
                celpRecoveryTotal_[ch] = std::max<uint32_t>(
                    8u, static_cast<uint32_t>(sampleRate_ * 0.004));
                celpRecoveryRemaining_[ch] = celpRecoveryTotal_[ch];
                celpRecoveryStart_[ch] = celpLastOutput_[ch];
                celpLossSamples_[ch] = 0u;
            }
            if (celpRecoveryRemaining_[ch] > 0u) {
                const float phase = 1.0f
                    - static_cast<float>(celpRecoveryRemaining_[ch])
                        / static_cast<float>(celpRecoveryTotal_[ch]);
                const float smooth = phase * phase * (3.0f - 2.0f * phase);
                coded = lerp(celpRecoveryStart_[ch], coded, smooth);
                --celpRecoveryRemaining_[ch];
            }
        }
        pitch_[ch][pitchWrite_[ch]] = coded;
        pitchWrite_[ch] = (pitchWrite_[ch] + 1u) % static_cast<uint32_t>(pitch_[ch].size());
        celpLastOutput_[ch] = coded;
        return coded;
    }

    float discConceal(float x, uint32_t ch)
    {
        const float clean = quantize(x, params_.bitDepth);
        auto& history = discHistory_[ch];
        auto& losses = discLossHistory_[ch];
        const uint32_t size = static_cast<uint32_t>(history.size());
        const bool currentLoss = discErrorRemaining_[ch] > 0u;
        history[discWrite_[ch]] = clean;
        losses[discWrite_[ch]] = currentLoss;
        discWrite_[ch] = (discWrite_[ch] + 1u) % size;
        discBuffered_[ch] = std::min<uint32_t>(size, discBuffered_[ch] + 1u);
        if (currentLoss) --discErrorRemaining_[ch];

        const uint32_t newest = (discWrite_[ch] + size - 1u) % size;
        const uint32_t readIndex = discBuffered_[ch] > kPsdRawFieldDiscLookahead
            ? (newest + size - kPsdRawFieldDiscLookahead) % size
            : newest;
        const bool concealed = losses[readIndex];
        float output = history[readIndex];

        if (concealed) {
            uint32_t previousDistance = 0u;
            uint32_t nextDistance = 0u;
            const uint32_t backwardLimit = std::min<uint32_t>(size - 1u, discBuffered_[ch] - 1u);
            for (uint32_t distance = 1u; distance <= backwardLimit; ++distance) {
                const uint32_t index = (readIndex + size - distance) % size;
                if (!losses[index]) {
                    previousDistance = distance;
                    break;
                }
            }
            if (discBuffered_[ch] > kPsdRawFieldDiscLookahead) {
                for (uint32_t distance = 1u; distance <= kPsdRawFieldDiscLookahead; ++distance) {
                    const uint32_t index = (readIndex + distance) % size;
                    if (!losses[index]) {
                        nextDistance = distance;
                        break;
                    }
                }
            }

            const uint32_t elapsed = previousDistance > 0u ? previousDistance - 1u : 0u;
            const float phase = clamp(
                static_cast<float>(elapsed)
                    / static_cast<float>(std::max<uint32_t>(1u, discErrorTotal_[ch])),
                0.0f,
                1.0f);
            if (previousDistance > 0u && nextDistance > 0u) {
                const uint32_t previousIndex = (readIndex + size - previousDistance) % size;
                const uint32_t nextIndex = (readIndex + nextDistance) % size;
                const float interpolationPhase = static_cast<float>(previousDistance)
                    / static_cast<float>(previousDistance + nextDistance);
                const float smooth = interpolationPhase * interpolationPhase
                    * (3.0f - 2.0f * interpolationPhase);
                output = lerp(history[previousIndex], history[nextIndex], smooth);
            } else {
                const uint32_t previousIndex = previousDistance > 0u
                    ? (readIndex + size - previousDistance) % size
                    : readIndex;
                const float previous = previousDistance > 0u
                    ? history[previousIndex] : discErrorStart_[ch];
                const uint32_t olderIndex = (previousIndex + size - 1u) % size;
                const float slope = losses[olderIndex]
                    ? discSlope_[ch] : previous - history[olderIndex];
                switch (discErrorKind_[ch]) {
                case 0u:
                    output = previous + slope * static_cast<float>(std::min<uint32_t>(elapsed, 12u));
                    break;
                case 1u: {
                    const uint32_t repeatLength = std::min<uint32_t>(
                        discRepeatLength_[ch], std::max<uint32_t>(1u, previousDistance));
                    const uint32_t repeatIndex = (previousIndex + size
                        - (elapsed % repeatLength)) % size;
                    output = losses[repeatIndex] ? previous : history[repeatIndex];
                    output *= lerp(1.0f, 0.68f, phase);
                    break;
                }
                case 2u:
                    output = (previous + slope * static_cast<float>(std::min<uint32_t>(elapsed, 16u)))
                        * (1.0f - phase);
                    break;
                case 3u:
                default:
                    output = previous * std::exp(-static_cast<float>(elapsed) * 0.055f) * 0.08f;
                    break;
                }
            }
            discWasConcealing_[ch] = true;
            discRecoveryRemaining_[ch] = 0u;
        } else {
            if (discWasConcealing_[ch]) {
                discRecoveryTotal_[ch] = 24u;
                discRecoveryRemaining_[ch] = discRecoveryTotal_[ch];
                discRecoveryStart_[ch] = discLastOutput_[ch];
                discWasConcealing_[ch] = false;
            }
            if (discRecoveryRemaining_[ch] > 0u) {
                const float phase = 1.0f
                    - static_cast<float>(discRecoveryRemaining_[ch])
                        / static_cast<float>(discRecoveryTotal_[ch]);
                const float smooth = phase * phase * (3.0f - 2.0f * phase);
                output = lerp(discRecoveryStart_[ch], output, smooth);
                --discRecoveryRemaining_[ch];
            }
        }
        output = clamp(output, -1.2f, 1.2f);
        discSlope_[ch] = lerp(discSlope_[ch], output - discLastOutput_[ch], 0.18f);
        discLastOutput_[ch] = output;
        return output;
    }

    float cvsdCodec(float x, uint32_t ch)
    {
        if (codecSample_[ch] == 0u || codecClockTick(cvsdClock_[ch])) {
            bool bit = x >= cvsdIntegrator_[ch];
            const float flipChance = params_.codecDamage * params_.codecDamage * 0.28f;
            if (hash01(codecSample_[ch] ^ tapeSeed_ ^ (ch * 0x632be59bu)) < flipChance) bit = !bit;

            if (bit == cvsdLastBit_[ch]) {
                cvsdRun_[ch] = std::min<uint32_t>(cvsdRun_[ch] + 1u, 255u);
            } else {
                cvsdRun_[ch] = 1u;
            }
            cvsdStep_[ch] *= cvsdRun_[ch] >= 3u
                ? 1.32f + params_.codecDamage * 0.24f
                : lerp(0.94f, 0.975f, params_.codecDamage);
            const float minimumStep = std::pow(2.0f, -std::min(14.0f, params_.bitDepth)) * 2.0f;
            const float maximumStep = lerp(0.18f, 0.78f, params_.codecDamage);
            cvsdStep_[ch] = clamp(cvsdStep_[ch], minimumStep, maximumStep);
            cvsdIntegrator_[ch] += (bit ? 1.0f : -1.0f) * cvsdStep_[ch];
            cvsdIntegrator_[ch] *= lerp(0.9998f, 0.996f, params_.codecDamage);
            cvsdIntegrator_[ch] = clamp(cvsdIntegrator_[ch], -1.0f, 1.0f);
            cvsdLastBit_[ch] = bit;
        }
        const float bitRate = static_cast<float>(sampleRate_) / codecInterval();
        const float cutoff = clamp(bitRate * lerp(0.11f, 0.24f,
            clamp((params_.bitDepth - 2.0f) / 14.0f, 0.0f, 1.0f)), 12.0f, 12000.0f);
        const float smoothing = 1.0f - std::exp(
            -2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
        cvsdOutput_[ch] += (cvsdIntegrator_[ch] - cvsdOutput_[ch]) * smoothing;
        return cvsdOutput_[ch];
    }

    float adaptiveBandCodec(
        float input,
        float& predictor,
        float& step,
        float bits,
        uint32_t ch,
        uint32_t salt)
    {
        const float levels = std::max(1.0f, std::pow(2.0f, std::floor(clamp(bits, 2.0f, 10.0f)) - 1.0f) - 1.0f);
        const float error = input - predictor;
        float code = clamp(std::round(error / std::max(step, 0.0001f)), -levels, levels);
        const float upset = params_.codecDamage * params_.codecDamage * 0.07f;
        if (hash01(codecSample_[ch] ^ tapeSeed_ ^ salt) < upset) {
            code = -code + (hash01(codecSample_[ch] ^ salt ^ 0xa511e9b3u) < 0.5f ? -1.0f : 1.0f);
            code = clamp(code, -levels, levels);
        }
        const float reconstructed = predictor + code * step;
        const float normalizedCode = std::abs(code) / levels;
        step *= std::exp(lerp(
            std::log(0.94f),
            std::log(1.82f + params_.codecDamage * 0.25f),
            std::pow(normalizedCode, 1.55f)));
        step = clamp(step, 0.0002f, 0.65f);
        predictor = clamp(reconstructed, -1.2f, 1.2f);
        return clamp(reconstructed, -1.2f, 1.2f);
    }

    float subbandCodec(float x, uint32_t ch)
    {
        const float output = subbandPairPhase_[ch]
            ? subbandOddOutput_[ch] : subbandEvenOutput_[ch];
        if (!subbandPairPhase_[ch]) {
            subbandLowState_[ch] = x;
            subbandPairPhase_[ch] = true;
            return output;
        }

        constexpr float inverseSqrtTwo = 0.7071067811865476f;
        const float low = (subbandLowState_[ch] + x) * inverseSqrtTwo;
        const float high = (subbandLowState_[ch] - x) * inverseSqrtTwo;
        const float lowBits = clamp(params_.bitDepth * 0.68f + 0.8f, 2.0f, 10.0f);
        const float highBits = clamp(params_.bitDepth * 0.44f, 2.0f, 8.0f);
        float lowCoded = adaptiveBandCodec(
            low, subbandLowPredictor_[ch], subbandLowStep_[ch], lowBits, ch, 0x68e31da4u);
        float highCoded = adaptiveBandCodec(
            high, subbandHighPredictor_[ch], subbandHighStep_[ch], highBits, ch, 0xb5297a4du);
        if (frameDrop_[ch]) {
            lowCoded = lerp(lowCoded, subbandLowPredictor_[ch], 0.72f);
            highCoded *= 0.08f;
        }
        const float bandLeak = params_.codecDamage * 0.38f;
        highCoded = highCoded * (1.0f - bandLeak)
            + subbandHighPredictor_[ch] * bandLeak * 0.22f;
        subbandEvenOutput_[ch] = clamp(
            (lowCoded + highCoded) * inverseSqrtTwo, -1.2f, 1.2f);
        subbandOddOutput_[ch] = clamp(
            (lowCoded - highCoded) * inverseSqrtTwo, -1.2f, 1.2f);
        subbandPairPhase_[ch] = false;
        return output;
    }

    float lpcCodec(float x, uint32_t ch)
    {
        float analysisPrediction = 0.0f;
        float synthesisPrediction = 0.0f;
        float historyEnergy = 0.02f;
        for (uint32_t order = 0u; order < kPsdRawFieldLpcOrder; ++order) {
            analysisPrediction += lpcCoefficients_[ch][order] * lpcInputHistory_[ch][order];
            synthesisPrediction += lpcCoefficients_[ch][order] * lpcSynthHistory_[ch][order];
            historyEnergy += lpcInputHistory_[ch][order] * lpcInputHistory_[ch][order];
        }
        const float residual = x - analysisPrediction;
        lpcEnergy_[ch] += (x * x - lpcEnergy_[ch]) * 0.006f;
        const float adaptation = lerp(0.018f, 0.002f, params_.codecDamage)
            * residual / historyEnergy;
        float coefficientSum = 0.0f;
        for (uint32_t order = 0u; order < kPsdRawFieldLpcOrder; ++order) {
            lpcCoefficients_[ch][order] = clamp(
                lpcCoefficients_[ch][order] + adaptation * lpcInputHistory_[ch][order], -0.72f, 0.72f);
            coefficientSum += std::abs(lpcCoefficients_[ch][order]);
        }
        if (coefficientSum > 0.94f) {
            const float scale = 0.94f / coefficientSum;
            for (float& coefficientValue : lpcCoefficients_[ch]) coefficientValue *= scale;
        }

        const float pitchPeriod = clamp(
            static_cast<float>(codePitch_[ch]) * std::pow(2.0f, params_.codecRate * 2.2f), 12.0f, 720.0f);
        lpcPhase_[ch] += 1.0f;
        float pulseExcitation = 0.0f;
        if (lpcPhase_[ch] >= pitchPeriod) {
            lpcPhase_[ch] -= pitchPeriod;
            pulseExcitation = (hash(codecSample_[ch] ^ tapeSeed_ ^ (ch * 0x9e3779b9u)) & 1u) ? 1.0f : -1.0f;
        }
        const float noise = hash01(codecSample_[ch] ^ tapeSeed_ ^ (ch * 0x85ebca6bu)) * 2.0f - 1.0f;
        const float energy = std::sqrt(std::max(1.0e-6f, lpcEnergy_[ch]));
        const float replacement = (pulseExcitation * 1.5f + noise * 0.22f) * energy;
        float replacementMix = 0.18f + params_.codecDamage * 0.72f
            + (1.0f - clamp((params_.bitDepth - 2.0f) / 14.0f, 0.0f, 1.0f)) * 0.22f;
        if (frameDrop_[ch]) replacementMix = 1.0f;
        const float excitation = quantizeRange(
            lerp(residual, replacement, clamp(replacementMix, 0.0f, 1.0f)),
            params_.bitDepth,
            std::max(0.025f, energy * 3.0f));
        const float output = clamp(synthesisPrediction + excitation, -1.25f, 1.25f);

        for (uint32_t order = kPsdRawFieldLpcOrder - 1u; order > 0u; --order) {
            lpcInputHistory_[ch][order] = lpcInputHistory_[ch][order - 1u];
            lpcSynthHistory_[ch][order] = lpcSynthHistory_[ch][order - 1u];
        }
        lpcInputHistory_[ch][0] = x;
        lpcSynthHistory_[ch][0] = output;
        return output;
    }

    void renderTransformBlock(uint32_t ch)
    {
        ++transformBlock_[ch];
        if (frameDrop_[ch] && transformReady_[ch]) {
            ++transformLossBlocks_[ch];
            const float attenuation = std::pow(0.86f, static_cast<float>(transformLossBlocks_[ch]));
            for (uint32_t n = 0u; n < kPsdRawFieldTransformSize; ++n) {
                transformOutput_[ch][n] *= attenuation;
            }
            return;
        }
        transformLossBlocks_[ch] = 0u;

        std::array<float, kPsdRawFieldTransformSize> coefficients {};
        float maximum = 0.0001f;
        for (uint32_t k = 0u; k < kPsdRawFieldTransformSize; ++k) {
            float coefficient = 0.0f;
            for (uint32_t n = 0u; n < kPsdRawFieldTransformSize; ++n) {
                coefficient += transformInput_[ch][n] * transformCosine_[k][n];
            }
            coefficients[k] = coefficient;
            maximum = std::max(maximum, std::abs(coefficient));
        }

        const float detail = clamp((params_.bitDepth - 2.0f) / 14.0f, 0.0f, 1.0f);
        float scale = std::pow(2.0f, std::ceil(std::log2(maximum)));
        const uint32_t scaleHash = hash(
            transformBlock_[ch] ^ tapeSeed_ ^ (ch * 0x85ebca6bu) ^ 0x45d9f3bu);
        if (hash01(scaleHash) < params_.codecDamage * params_.codecDamage * 0.055f) {
            const int32_t exponentShift = (scaleHash & 1u) != 0u ? 1 : -1;
            scale *= std::pow(2.0f, static_cast<float>(exponentShift));
        }
        scale = clamp(scale, 0.0001f, 64.0f);
        const float keepRatio = clamp(
            1.0f - params_.codecDamage * 0.78f - (1.0f - detail) * 0.26f, 0.08f, 1.0f);
        const uint32_t keepBands = std::max<uint32_t>(2u,
            static_cast<uint32_t>(std::round(keepRatio * kPsdRawFieldTransformSize)));
        for (uint32_t k = 0u; k < kPsdRawFieldTransformSize; ++k) {
            if (k >= keepBands) {
                const float residue = readTapeAudio(
                    transformBlock_[ch] * 97u + k * 1543u + ch * 257u);
                coefficients[k] = residue * maximum * params_.codecDamage * 0.10f;
                continue;
            }
            const float spectralPosition = static_cast<float>(k)
                / static_cast<float>(kPsdRawFieldTransformSize - 1u);
            const float localBits = clamp(
                params_.bitDepth + 1.0f - spectralPosition * (2.0f + params_.codecDamage * 7.0f),
                2.0f,
                16.0f);
            coefficients[k] = quantizeRange(coefficients[k], localBits, scale);
            const float upset = params_.codecDamage * params_.codecDamage * 0.018f;
            const uint32_t coefficientHash = hash(
                transformBlock_[ch] ^ (k * 0x9e3779b9u) ^ tapeSeed_ ^ (ch * 0x632be59bu));
            if (hash01(coefficientHash) < upset) {
                const float bitWeight = std::pow(
                    2.0f,
                    -static_cast<float>(1u + ((coefficientHash >> 17u) & 3u)));
                coefficients[k] += ((coefficientHash & 1u) ? 1.0f : -1.0f)
                    * scale * bitWeight;
            }
        }

        const float inverseScale = 2.0f / static_cast<float>(kPsdRawFieldTransformSize);
        for (uint32_t n = 0u; n < kPsdRawFieldTransformSize; ++n) {
            float output = coefficients[0] * 0.5f;
            for (uint32_t k = 1u; k < kPsdRawFieldTransformSize; ++k) {
                output += coefficients[k] * transformCosine_[k][n];
            }
            transformOutput_[ch][n] = clamp(output * inverseScale, -1.25f, 1.25f);
        }
    }

    float transformCodec(float x, uint32_t ch)
    {
        float output = transformReady_[ch] ? transformOutput_[ch][transformRead_[ch]] : 0.0f;
        if (transformReady_[ch]) {
            transformRead_[ch] = (transformRead_[ch] + 1u) % kPsdRawFieldTransformSize;
        }
        transformInput_[ch][transformWrite_[ch]++] = x;
        if (transformWrite_[ch] >= kPsdRawFieldTransformSize) {
            transformWrite_[ch] = 0u;
            renderTransformBlock(ch);
            transformRead_[ch] = 0u;
            transformReady_[ch] = true;
        }
        return output;
    }

    float predictiveCodec(float x, uint32_t ch)
    {
        const float prediction = clamp(1.82f * predictiveOne_[ch] - 0.84f * predictiveTwo_[ch], -1.2f, 1.2f);
        const float residual = x - prediction;
        predictiveScale_[ch] += (std::abs(residual) * 2.4f + 0.004f - predictiveScale_[ch]) * 0.012f;
        float codedResidual = quantizeRange(residual, params_.bitDepth, predictiveScale_[ch]);
        if (frameDrop_[ch]) codedResidual *= lerp(0.0f, 0.35f, 1.0f - params_.codecDamage);
        const float upset = params_.codecDamage * params_.codecDamage * 0.08f;
        if (hash01(codecSample_[ch] ^ tapeSeed_ ^ 0x91e10da5u) < upset) {
            const float shift = std::pow(2.0f, std::floor(params_.codecDamage * 4.0f));
            codedResidual *= (hash(codecSample_[ch] ^ tapeSeed_) & 1u) ? shift : -shift;
        }
        const float decoderPrediction = lerp(prediction, predictiveOne_[ch], params_.codecDamage * 0.32f);
        const float output = clamp(decoderPrediction + codedResidual, -1.25f, 1.25f);
        predictiveTwo_[ch] = predictiveOne_[ch];
        predictiveOne_[ch] = output;
        return output;
    }

    float modemCodec(float x, uint32_t ch)
    {
        if ((codecSample_[ch] == 0u || codecClockTick(modemClock_[ch])) && !frameDrop_[ch]) {
            const uint32_t stateShift = std::min<uint32_t>(3u,
                static_cast<uint32_t>(std::max(0.0f, params_.bitDepth - 2.0f)) / 4u);
            const uint32_t states = 2u << stateShift;
            uint32_t symbol = static_cast<uint32_t>(clamp(
                std::floor((x * 0.5f + 0.5f) * static_cast<float>(states)),
                0.0f,
                static_cast<float>(states - 1u)));
            const float upset = params_.codecDamage * params_.codecDamage * 0.24f;
            if (hash01(codecSample_[ch] ^ tapeSeed_ ^ 0x7f4a7c15u) < upset) {
                symbol = (symbol + 1u + (hash(codecSample_[ch] ^ ch) % (states - 1u))) % states;
                modemPhase_[ch] += hash01(codecSample_[ch] ^ 0xa511e9b3u) * 0.25f;
            }
            const float laneSkew = (static_cast<float>(ch) - 3.5f) * 21.0f * params_.channelSpread;
            modemFrequency_[ch] = lerp(430.0f, 3180.0f,
                static_cast<float>(symbol) / static_cast<float>(states - 1u)) + laneSkew;
            modemAmplitude_[ch] = lerp(0.28f, 0.86f, std::abs(x));
        }
        modemPhase_[ch] += modemFrequency_[ch] / static_cast<float>(sampleRate_);
        modemPhase_[ch] -= std::floor(modemPhase_[ch]);
        float carrier = std::sin(2.0f * kPi * modemPhase_[ch]);
        carrier += std::sin(4.0f * kPi * modemPhase_[ch]) * params_.codecDamage * 0.22f;
        const float mix = clamp(0.52f + params_.codecDamage * 0.43f
            + (8.0f - std::min(8.0f, params_.bitDepth)) * 0.025f, 0.0f, 0.97f);
        return lerp(x, carrier * modemAmplitude_[ch] * 0.78f, mix);
    }

    float faxCodec(float x, uint32_t ch)
    {
        const bool symbolTick = codecSample_[ch] == 0u || codecClockTick(faxClock_[ch]);
        if (symbolTick) {
            faxPreviousI_[ch] = faxTargetI_[ch];
            faxPreviousQ_[ch] = faxTargetQ_[ch];
            if (frameDrop_[ch] && !faxWasDrop_[ch]) {
                faxTrainingRemaining_[ch] = 12u
                    + static_cast<uint32_t>(params_.codecDamage * 28.0f);
            }
            faxWasDrop_[ch] = frameDrop_[ch];

            float nextI = 0.0f;
            float nextQ = 0.0f;
            if (faxTrainingRemaining_[ch] > 0u) {
                constexpr std::array<std::array<float, 2>, 4> training {
                    std::array<float, 2> { 0.7071f, 0.7071f },
                    std::array<float, 2> { -0.7071f, 0.7071f },
                    std::array<float, 2> { -0.7071f, -0.7071f },
                    std::array<float, 2> { 0.7071f, -0.7071f },
                };
                const auto& point = training[faxSymbol_[ch] & 3u];
                nextI = point[0];
                nextQ = point[1];
                --faxTrainingRemaining_[ch];
            } else {
                const float slope = clamp(
                    (x - faxPreviousInput_[ch]) * lerp(1.4f, 4.5f, params_.codecDamage),
                    -1.0f,
                    1.0f);
                const uint32_t amplitudeIndex = static_cast<uint32_t>(clamp(
                    std::floor((x * 0.5f + 0.5f) * 4.0f), 0.0f, 3.0f));
                const uint32_t slopeIndex = static_cast<uint32_t>(clamp(
                    std::floor((slope * 0.5f + 0.5f) * 4.0f), 0.0f, 3.0f));
                if (params_.bitDepth < 6.0f) {
                    constexpr std::array<std::array<float, 2>, 4> qpsk {
                        std::array<float, 2> { 0.7071f, 0.7071f },
                        std::array<float, 2> { -0.7071f, 0.7071f },
                        std::array<float, 2> { -0.7071f, -0.7071f },
                        std::array<float, 2> { 0.7071f, -0.7071f },
                    };
                    const auto& point = qpsk[(amplitudeIndex ^ slopeIndex) & 3u];
                    nextI = point[0];
                    nextQ = point[1];
                } else if (params_.bitDepth < 11.0f) {
                    constexpr std::array<std::array<float, 2>, 8> qam8 {
                        std::array<float, 2> { 1.0f, 0.0f },
                        std::array<float, 2> { 0.65f, 0.65f },
                        std::array<float, 2> { 0.0f, 1.0f },
                        std::array<float, 2> { -0.65f, 0.65f },
                        std::array<float, 2> { -1.0f, 0.0f },
                        std::array<float, 2> { -0.65f, -0.65f },
                        std::array<float, 2> { 0.0f, -1.0f },
                        std::array<float, 2> { 0.65f, -0.65f },
                    };
                    const auto& point = qam8[
                        ((amplitudeIndex << 1u) ^ slopeIndex ^ faxSymbol_[ch]) & 7u];
                    nextI = point[0];
                    nextQ = point[1];
                } else {
                    constexpr std::array<float, 4> levels {
                        -1.0f, -0.3333333f, 0.3333333f, 1.0f
                    };
                    nextI = levels[amplitudeIndex];
                    nextQ = levels[slopeIndex];
                }
            }

            const uint32_t h = hash(codecSample_[ch] ^ tapeSeed_
                ^ (ch * 0x27d4eb2du) ^ 0xd1b54a35u);
            if (hash01(h) < params_.codecDamage * params_.codecDamage * 0.22f) {
                std::swap(nextI, nextQ);
                nextQ = -nextQ;
                faxPhase_[ch] += ((h & 1u) ? 1.0f : -1.0f)
                    * params_.codecDamage * 0.125f;
            }
            faxTargetI_[ch] = nextI;
            faxTargetQ_[ch] = nextQ;
            faxPreviousInput_[ch] = x;
            ++faxSymbol_[ch];
        }

        const float symbolPhase = clamp(faxClock_[ch] / codecInterval(), 0.0f, 1.0f);
        const float raisedCosine = 0.5f - 0.5f * std::cos(kPi * symbolPhase);
        const float shapedI = lerp(faxPreviousI_[ch], faxTargetI_[ch], raisedCosine);
        const float shapedQ = lerp(faxPreviousQ_[ch], faxTargetQ_[ch], raisedCosine);
        const float equalizer = lerp(0.34f, 0.025f, params_.codecDamage);
        const float coupling = params_.codecDamage * 0.08f;
        faxI_[ch] += (shapedI + faxQ_[ch] * coupling - faxI_[ch]) * equalizer;
        faxQ_[ch] += (shapedQ - faxI_[ch] * coupling - faxQ_[ch]) * equalizer;
        const float carrierFrequency = 1700.0f
            + (static_cast<float>(ch) - 3.5f) * 13.0f * params_.channelSpread;
        faxPhase_[ch] += carrierFrequency / static_cast<float>(sampleRate_);
        faxPhase_[ch] -= std::floor(faxPhase_[ch]);
        const float phase = 2.0f * kPi * faxPhase_[ch];
        const float qam = (faxI_[ch] * std::cos(phase) - faxQ_[ch] * std::sin(phase)) * 0.58f;
        return lerp(x, qam, lerp(0.60f, 0.96f, params_.codecDamage));
    }

    float sigmaCodec(float x, uint32_t ch)
    {
        if (codecSample_[ch] == 0u || codecClockTick(sigmaClock_[ch])) {
            const float feedback = sigmaLastBit_[ch] ? 1.0f : -1.0f;
            sigmaError_[ch] += x - feedback;
            sigmaError_[ch] *= lerp(0.9998f, 0.996f, params_.codecDamage);
            sigmaError_[ch] = clamp(sigmaError_[ch], -4.0f, 4.0f);
            bool bit = sigmaError_[ch] >= 0.0f;
            const float flipChance = params_.codecDamage * params_.codecDamage * 0.20f;
            if (hash01(codecSample_[ch] ^ tapeSeed_ ^ 0x68e31da4u) < flipChance) bit = !bit;
            sigmaLastBit_[ch] = bit;
        }
        const float detail = clamp((params_.bitDepth - 2.0f) / 14.0f, 0.0f, 1.0f);
        const float encoded = sigmaLastBit_[ch] ? 1.0f : -1.0f;
        const float bitRate = static_cast<float>(sampleRate_) / codecInterval();
        const float cutoff = clamp(bitRate * lerp(0.08f, 0.22f, detail), 8.0f, 12000.0f);
        const float reconstruction = 1.0f - std::exp(
            -2.0f * kPi * cutoff / static_cast<float>(sampleRate_));
        sigmaOutput_[ch] += (encoded - sigmaOutput_[ch]) * reconstruction;
        return sigmaOutput_[ch];
    }

    float hybridCodec(float x, uint32_t ch)
    {
        const float prediction = clamp(hybridOne_[ch] * 0.78f + hybridTwo_[ch] * 0.18f, -1.2f, 1.2f);
        const float shaping = lerp(0.28f, 0.92f,
            clamp((params_.bitDepth - 2.0f) / 14.0f, 0.0f, 1.0f));
        const float residual = x + hybridError_[ch] * shaping - prediction;
        predictiveScale_[ch] += (std::abs(residual) * 2.0f + 0.003f - predictiveScale_[ch]) * 0.008f;
        const float base = prediction + quantizeRange(residual, params_.bitDepth, predictiveScale_[ch]);
        float correctionGain = 1.0f - params_.codecDamage * 0.94f;
        if (frameDrop_[ch]) correctionGain = 0.0f;
        float correction = x - base;
        if (hash01(codecSample_[ch] ^ tapeSeed_ ^ 0xb5297a4du)
            < params_.codecDamage * params_.codecDamage * 0.05f) {
            correction = -correction * lerp(0.5f, 2.0f, params_.codecDamage);
        }
        const float output = clamp(base + correction * correctionGain, -1.25f, 1.25f);
        hybridError_[ch] = clamp(x - output, -1.0f, 1.0f);
        hybridTwo_[ch] = hybridOne_[ch];
        hybridOne_[ch] = output;
        return output;
    }

    static uint32_t hash(uint32_t x)
    {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return x;
    }

    static float hash01(uint32_t x) { return static_cast<float>(hash(x) & 0xffffu) / 65535.0f; }

    static uint8_t lerpByte(uint8_t a, uint8_t b, float t)
    {
        return static_cast<uint8_t>(clamp(lerp(static_cast<float>(a), static_cast<float>(b), t), 0.0f, 255.0f));
    }

    static float byteToAudio(uint8_t b) { return (static_cast<float>(b) - 127.5f) * (1.0f / 127.5f); }
    static float softClip(float x) { return x / (1.0f + std::abs(x)); }
    static float triangle(float phase) { return 1.0f - std::abs(phase * 2.0f - 1.0f); }

    static float pulse(float phase, float width)
    {
        return phase < width ? 1.0f - phase / std::max(width, 0.0001f) : 0.0f;
    }

    float rowPhase(uint32_t ch) const
    {
        const double row = cursor_[ch] * (1.0 / 256.0);
        return row - std::floor(row);
    }

    static float metadataTick(uint32_t pos, uint32_t ch)
    {
        const uint32_t period = 97u + ch * 23u;
        if (((pos + ch * 11u) % period) > 2u) return 0.0f;
        return ((pos / period) & 1u) ? 0.42f : -0.42f;
    }

    PsdRawFieldParams params_ {};
    double sampleRate_ = 48000.0;
    bool ready_ = false;
    uint32_t tapeSeed_ = 0x50434431u;
    uint32_t tapeMask_ = kPsdRawFieldTapeSize - 1u;
    uint32_t cursorSize_ = kPsdRawFieldTapeSize;
    uint32_t cursorMask_ = kPsdRawFieldTapeSize - 1u;
    bool waveformSource_ = false;
    float sectionPhase_ = 0.0f;
    uint32_t frameSample_ = 0u;
    std::shared_ptr<const PsdRawFieldSource> source_ {};
    std::vector<uint8_t> tape_ = std::vector<uint8_t>(kPsdRawFieldTapeSize);
    std::array<uint8_t, 2048> evolveTarget_ {};
    uint32_t evolveSeed_ = 1u;
    uint32_t evolveRegionStart_ = 0u;
    uint32_t evolveRegionLength_ = 0u;
    uint32_t evolveRegionCounter_ = 0u;
    float evolveBlend_ = 0.0f;
    float evolveWaitSamples_ = 0.0f;
    bool evolveActive_ = false;
    std::array<double, kPsdRawFieldChannels> cursor_ {};
    std::array<float, kPsdRawFieldChannels> prev_ {};
    std::array<float, kPsdRawFieldChannels> textureState_ {};
    std::array<float, kPsdRawFieldChannels> dcX_ {};
    std::array<float, kPsdRawFieldChannels> dcY_ {};
    std::array<float, kPsdRawFieldChannels> connectStart_ {};
    std::array<float, kPsdRawFieldChannels> connectTarget_ {};
    std::array<float, kPsdRawFieldChannels> connectCurrent_ {};
    std::array<float, kPsdRawFieldChannels> connectPhase_ {};
    std::array<float, kPsdRawFieldChannels> connectInc_ {};
    std::array<uint32_t, kPsdRawFieldChannels> connectSeed_ {};
    std::array<BreakpointState, kPsdRawFieldChannels> breakpoint_ {};
    std::array<float, kPsdRawFieldChannels> hold_ {};
    std::array<float, kPsdRawFieldChannels> held_ {};
    std::array<float, kPsdRawFieldChannels> predictor_ {};
    std::array<float, kPsdRawFieldChannels> step_ {};
    std::array<bool, kPsdRawFieldChannels> frameDrop_ {};
    std::array<float, kPsdRawFieldChannels> codeGain_ {};
    std::array<uint32_t, kPsdRawFieldChannels> codePitch_ {};
    std::array<std::array<float, 160>, kPsdRawFieldChannels> pitch_ {};
    std::array<uint32_t, kPsdRawFieldChannels> pitchWrite_ {};
    std::array<uint32_t, kPsdRawFieldChannels> celpLossSamples_ {};
    std::array<uint32_t, kPsdRawFieldChannels> celpRecoveryRemaining_ {};
    std::array<uint32_t, kPsdRawFieldChannels> celpRecoveryTotal_ {};
    std::array<float, kPsdRawFieldChannels> celpRecoveryStart_ {};
    std::array<float, kPsdRawFieldChannels> celpLastOutput_ {};
    uint64_t codecFrameCounter_ = 0u;
    std::array<uint32_t, kPsdRawFieldChannels> codecSample_ {};
    std::array<bool, kPsdRawFieldChannels> heldTick_ {};
    std::array<std::array<float, 256>, kPsdRawFieldChannels> discHistory_ {};
    std::array<std::array<bool, 256>, kPsdRawFieldChannels> discLossHistory_ {};
    std::array<uint32_t, kPsdRawFieldChannels> discWrite_ {};
    std::array<uint32_t, kPsdRawFieldChannels> discBuffered_ {};
    std::array<uint32_t, kPsdRawFieldChannels> discErrorRemaining_ {};
    std::array<uint32_t, kPsdRawFieldChannels> discErrorTotal_ {};
    std::array<uint32_t, kPsdRawFieldChannels> discErrorKind_ {};
    std::array<uint32_t, kPsdRawFieldChannels> discRepeatLength_ {};
    std::array<float, kPsdRawFieldChannels> discLastOutput_ {};
    std::array<float, kPsdRawFieldChannels> discSlope_ {};
    std::array<float, kPsdRawFieldChannels> discErrorStart_ {};
    std::array<bool, kPsdRawFieldChannels> discWasConcealing_ {};
    std::array<uint32_t, kPsdRawFieldChannels> discRecoveryRemaining_ {};
    std::array<uint32_t, kPsdRawFieldChannels> discRecoveryTotal_ {};
    std::array<float, kPsdRawFieldChannels> discRecoveryStart_ {};
    std::array<float, kPsdRawFieldChannels> cvsdIntegrator_ {};
    std::array<float, kPsdRawFieldChannels> cvsdStep_ {};
    std::array<bool, kPsdRawFieldChannels> cvsdLastBit_ {};
    std::array<uint32_t, kPsdRawFieldChannels> cvsdRun_ {};
    std::array<float, kPsdRawFieldChannels> cvsdClock_ {};
    std::array<float, kPsdRawFieldChannels> cvsdOutput_ {};
    std::array<float, kPsdRawFieldChannels> subbandLowState_ {};
    std::array<float, kPsdRawFieldChannels> subbandLowPredictor_ {};
    std::array<float, kPsdRawFieldChannels> subbandHighPredictor_ {};
    std::array<float, kPsdRawFieldChannels> subbandLowStep_ {};
    std::array<float, kPsdRawFieldChannels> subbandHighStep_ {};
    std::array<float, kPsdRawFieldChannels> subbandEvenOutput_ {};
    std::array<float, kPsdRawFieldChannels> subbandOddOutput_ {};
    std::array<bool, kPsdRawFieldChannels> subbandPairPhase_ {};
    std::array<std::array<float, kPsdRawFieldLpcOrder>, kPsdRawFieldChannels> lpcInputHistory_ {};
    std::array<std::array<float, kPsdRawFieldLpcOrder>, kPsdRawFieldChannels> lpcSynthHistory_ {};
    std::array<std::array<float, kPsdRawFieldLpcOrder>, kPsdRawFieldChannels> lpcCoefficients_ {};
    std::array<float, kPsdRawFieldChannels> lpcEnergy_ {};
    std::array<float, kPsdRawFieldChannels> lpcPhase_ {};
    std::array<std::array<float, kPsdRawFieldTransformSize>, kPsdRawFieldTransformSize> transformCosine_ {};
    std::array<std::array<float, kPsdRawFieldTransformSize>, kPsdRawFieldChannels> transformInput_ {};
    std::array<std::array<float, kPsdRawFieldTransformSize>, kPsdRawFieldChannels> transformOutput_ {};
    std::array<uint32_t, kPsdRawFieldChannels> transformWrite_ {};
    std::array<uint32_t, kPsdRawFieldChannels> transformRead_ {};
    std::array<bool, kPsdRawFieldChannels> transformReady_ {};
    std::array<uint32_t, kPsdRawFieldChannels> transformBlock_ {};
    std::array<uint32_t, kPsdRawFieldChannels> transformLossBlocks_ {};
    std::array<float, kPsdRawFieldChannels> predictiveOne_ {};
    std::array<float, kPsdRawFieldChannels> predictiveTwo_ {};
    std::array<float, kPsdRawFieldChannels> predictiveScale_ {};
    std::array<float, kPsdRawFieldChannels> hybridOne_ {};
    std::array<float, kPsdRawFieldChannels> hybridTwo_ {};
    std::array<float, kPsdRawFieldChannels> hybridError_ {};
    std::array<float, kPsdRawFieldChannels> modemPhase_ {};
    std::array<float, kPsdRawFieldChannels> modemClock_ {};
    std::array<float, kPsdRawFieldChannels> modemFrequency_ {};
    std::array<float, kPsdRawFieldChannels> modemAmplitude_ {};
    std::array<float, kPsdRawFieldChannels> faxPhase_ {};
    std::array<float, kPsdRawFieldChannels> faxClock_ {};
    std::array<float, kPsdRawFieldChannels> faxI_ {};
    std::array<float, kPsdRawFieldChannels> faxQ_ {};
    std::array<float, kPsdRawFieldChannels> faxPreviousI_ {};
    std::array<float, kPsdRawFieldChannels> faxPreviousQ_ {};
    std::array<float, kPsdRawFieldChannels> faxTargetI_ {};
    std::array<float, kPsdRawFieldChannels> faxTargetQ_ {};
    std::array<float, kPsdRawFieldChannels> faxPreviousInput_ {};
    std::array<uint32_t, kPsdRawFieldChannels> faxTrainingRemaining_ {};
    std::array<uint32_t, kPsdRawFieldChannels> faxSymbol_ {};
    std::array<bool, kPsdRawFieldChannels> faxWasDrop_ {};
    std::array<float, kPsdRawFieldChannels> sigmaError_ {};
    std::array<float, kPsdRawFieldChannels> sigmaOutput_ {};
    std::array<float, kPsdRawFieldChannels> sigmaClock_ {};
    std::array<bool, kPsdRawFieldChannels> sigmaLastBit_ {};
    float loudnessEnergy_ = 0.04f;
    float loudnessGain_ = 1.0f;
    float targetPitchRatio_ = 1.0f;
    float currentPitchRatio_ = 1.0f;
    float pitchSmoothing_ = 0.003f;
    MacroShred shaper_ {};
};

class PsdRawFieldMorph {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        for (auto& engine : engines_) {
            engine.prepare(sampleRate_);
            engine.setPitchRatio(pitchRatio_);
        }
        ready_ = true;
        reset();
    }

    void reset()
    {
        for (auto& engine : engines_) {
            engine.setSource(source_);
            engine.setParams(params_);
            engine.setPitchRatio(pitchRatio_);
            engine.reset();
        }
        activeEngine_ = 0u;
        targetEngine_ = 1u;
        transitionPosition_ = 0u;
        transitionSamples_ = 1u;
        transitioning_ = false;
    }

    void setParams(const PsdRawFieldParams& params)
    {
        params_ = params;
        if (transitioning_) engines_[targetEngine_].setParams(params_);
        else engines_[activeEngine_].setParams(params_);
    }

    void setSource(std::shared_ptr<const PsdRawFieldSource> source)
    {
        source_ = std::move(source);
        for (auto& engine : engines_) engine.setSource(source_);
    }

    void transitionTo(const PsdRawFieldParams& params, float seconds = 0.75f)
    {
        transitionToSource(source_, params, seconds);
    }

    void transitionToSource(
        std::shared_ptr<const PsdRawFieldSource> source,
        const PsdRawFieldParams& params,
        float seconds = 0.75f)
    {
        source_ = std::move(source);
        params_ = params;
        if (!ready_) {
            for (auto& engine : engines_) {
                engine.setSource(source_);
                engine.setParams(params_);
            }
            return;
        }
        if (transitioning_ && transitionProgress() >= 0.5f) activeEngine_ = targetEngine_;
        transitioning_ = false;
        targetEngine_ = 1u - activeEngine_;
        engines_[targetEngine_].setSource(source_);
        engines_[targetEngine_].setParams(params_);
        engines_[targetEngine_].setPitchRatio(pitchRatio_);
        engines_[targetEngine_].reset();
        transitionPosition_ = 0u;
        transitionSamples_ = std::max<uint64_t>(64u,
            static_cast<uint64_t>(sampleRate_ * clamp(seconds, 0.02f, 4.0f)));
        transitioning_ = true;
    }

    PsdRawFieldParams params() const { return params_; }
    std::shared_ptr<const PsdRawFieldSource> source() const { return source_; }
    bool ready() const { return ready_; }
    bool transitioning() const { return transitioning_; }
    float transitionProgress() const
    {
        if (!transitioning_) return 1.0f;
        return clamp(static_cast<float>(transitionPosition_) / static_cast<float>(transitionSamples_), 0.0f, 1.0f);
    }

    void setPitchRatio(float ratio)
    {
        pitchRatio_ = clamp(ratio, 1.0f / 64.0f, 64.0f);
        for (auto& engine : engines_) engine.setPitchRatio(pitchRatio_);
    }

    void process(float* const* output, uint32_t outputChannels, uint32_t frames)
    {
        if (!ready_ || !output || frames == 0u) return;
        if (!transitioning_) {
            engines_[activeEngine_].process(output, outputChannels, frames);
            return;
        }

        uint32_t processed = 0u;
        while (processed < frames) {
            const uint32_t chunk = std::min<uint32_t>(kMorphChunk, frames - processed);
            for (uint32_t ch = 0; ch < kPsdRawFieldChannels; ++ch) {
                sourcePtrs_[ch] = sourceScratch_[ch].data();
                targetPtrs_[ch] = targetScratch_[ch].data();
            }
            engines_[activeEngine_].process(sourcePtrs_.data(), kPsdRawFieldChannels, chunk);
            engines_[targetEngine_].process(targetPtrs_.data(), kPsdRawFieldChannels, chunk);

            const uint32_t channels = std::min<uint32_t>(outputChannels, kPsdRawFieldChannels);
            for (uint32_t i = 0; i < chunk; ++i) {
                const float phase = clamp(
                    static_cast<float>(transitionPosition_ + i) / static_cast<float>(transitionSamples_),
                    0.0f,
                    1.0f);
                const float mix = phase * phase * (3.0f - 2.0f * phase);
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    if (output[ch]) {
                        output[ch][processed + i] = lerp(sourceScratch_[ch][i], targetScratch_[ch][i], mix);
                    }
                }
                for (uint32_t ch = channels; ch < outputChannels; ++ch) {
                    if (output[ch]) output[ch][processed + i] = 0.0f;
                }
            }
            transitionPosition_ += chunk;
            processed += chunk;
            if (transitionPosition_ >= transitionSamples_) {
                activeEngine_ = targetEngine_;
                transitioning_ = false;
                if (processed < frames) {
                    std::array<float*, kPsdRawFieldChannels> remainderPtrs {};
                    const uint32_t renderChannels = std::min<uint32_t>(outputChannels, kPsdRawFieldChannels);
                    for (uint32_t ch = 0; ch < renderChannels; ++ch) {
                        remainderPtrs[ch] = output[ch] ? output[ch] + processed : nullptr;
                    }
                    engines_[activeEngine_].process(remainderPtrs.data(), renderChannels, frames - processed);
                    for (uint32_t ch = renderChannels; ch < outputChannels; ++ch) {
                        if (output[ch]) std::fill(output[ch] + processed, output[ch] + frames, 0.0f);
                    }
                }
                return;
            }
        }
    }

private:
    static constexpr uint32_t kMorphChunk = 256u;
    PsdRawFieldParams params_ {};
    std::shared_ptr<const PsdRawFieldSource> source_ {};
    std::array<PsdRawField, 2> engines_ {};
    uint32_t activeEngine_ = 0u;
    uint32_t targetEngine_ = 1u;
    uint64_t transitionPosition_ = 0u;
    uint64_t transitionSamples_ = 1u;
    double sampleRate_ = 48000.0;
    float pitchRatio_ = 1.0f;
    bool ready_ = false;
    bool transitioning_ = false;
    std::array<std::array<float, kMorphChunk>, kPsdRawFieldChannels> sourceScratch_ {};
    std::array<std::array<float, kMorphChunk>, kPsdRawFieldChannels> targetScratch_ {};
    std::array<float*, kPsdRawFieldChannels> sourcePtrs_ {};
    std::array<float*, kPsdRawFieldChannels> targetPtrs_ {};
};

} // namespace s3g
